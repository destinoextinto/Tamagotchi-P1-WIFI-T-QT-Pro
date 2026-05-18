const http = require("http");
const crypto = require("crypto");
const { URL } = require("url");

const PORT = Number(process.env.PORT || 8787);
const HOST = process.env.HOST || "0.0.0.0";
const TAMA_SHARED_SECRET = process.env.TAMA_SHARED_SECRET || "tama-local-dev";
const TURN_LIMIT_MS = Number(process.env.TURN_LIMIT_MS || 5 * 60 * 1000);
const SESSION_TIMEOUT_MS = Number(process.env.SESSION_TIMEOUT_MS || 45 * 1000);
const BUTTON_COOLDOWN_MS = Number(process.env.BUTTON_COOLDOWN_MS || 220);
const COMMAND_MAX_AGE_MS = Number(process.env.COMMAND_MAX_AGE_MS || 5000);

let activeSession = null;
let queue = [];
let latestTamaState = null;
let tamaLastSeenAt = 0;
let commandQueue = [];

const sessions = new Map();

function now() {
  return Date.now();
}

function newId() {
  return crypto.randomBytes(16).toString("hex");
}

function clampName(name) {
  const cleaned = String(name || "").trim().replace(/\s+/g, " ");
  return cleaned.slice(0, 32) || "Invitado";
}

function json(res, status, body) {
  const text = JSON.stringify(body);
  res.writeHead(status, {
    "Content-Type": "application/json; charset=utf-8",
    "Cache-Control": "no-store",
    "Access-Control-Allow-Origin": "*",
    "Access-Control-Allow-Methods": "GET,POST,OPTIONS",
    "Access-Control-Allow-Headers": "Content-Type,X-Tama-Token",
  });
  res.end(text);
}

function text(res, status, body, contentType = "text/plain; charset=utf-8") {
  res.writeHead(status, {
    "Content-Type": contentType,
    "Cache-Control": "no-store",
  });
  res.end(body);
}

function readBody(req) {
  return new Promise((resolve, reject) => {
    let body = "";
    req.on("data", (chunk) => {
      body += chunk;
      if (body.length > 1024 * 256) {
        reject(new Error("body too large"));
        req.destroy();
      }
    });
    req.on("end", () => resolve(body));
    req.on("error", reject);
  });
}

async function readJson(req) {
  const body = await readBody(req);
  if (!body) return {};
  try {
    return JSON.parse(body);
  } catch {
    const params = new URLSearchParams(body);
    return Object.fromEntries(params.entries());
  }
}

function sessionPublic(s) {
  if (!s) return null;
  return {
    id: s.id,
    name: s.name,
    startedAt: s.startedAt,
    lastSeenAt: s.lastSeenAt,
    turnEndsAt: s.startedAt + TURN_LIMIT_MS,
  };
}

function removeSession(session) {
  if (!session) return;
  sessions.delete(session.token);
  if (activeSession && activeSession.token === session.token) activeSession = null;
  queue = queue.filter((item) => item.token !== session.token);
}

function promoteQueue() {
  const t = now();

  if (activeSession) {
    const inactive = t - activeSession.lastSeenAt > SESSION_TIMEOUT_MS;
    const expired = t - activeSession.startedAt > TURN_LIMIT_MS;
    if (inactive || expired) removeSession(activeSession);
  }

  queue = queue.filter((item) => {
    const alive = sessions.has(item.token) && t - item.lastSeenAt <= SESSION_TIMEOUT_MS;
    if (!alive) sessions.delete(item.token);
    return alive;
  });

  if (!activeSession && queue.length > 0) {
    activeSession = queue.shift();
    activeSession.startedAt = t;
    activeSession.lastButtonAt = 0;
  }
}

function touchSession(token) {
  const session = sessions.get(token);
  if (!session) return null;
  session.lastSeenAt = now();
  promoteQueue();
  return session;
}

function queuePosition(token) {
  const index = queue.findIndex((item) => item.token === token);
  return index >= 0 ? index + 1 : 0;
}

function buildStateFor(token) {
  promoteQueue();
  const session = token ? touchSession(token) : null;
  const isActive = Boolean(session && activeSession && activeSession.token === session.token);
  const position = session && !isActive ? queuePosition(session.token) : 0;

  return {
    ok: true,
    tamaOnline: tamaLastSeenAt > 0 && now() - tamaLastSeenAt < 10000,
    tamaLastSeenAt,
    tama: latestTamaState,
    turn: {
      active: sessionPublic(activeSession),
      queue: queue.map(sessionPublic),
      turnLimitMs: TURN_LIMIT_MS,
      sessionTimeoutMs: SESSION_TIMEOUT_MS,
    },
    user: session
      ? {
          token: session.token,
          name: session.name,
          active: isActive,
          queued: !isActive && position > 0,
          position,
        }
      : null,
  };
}

async function handleJoin(req, res) {
  const body = await readJson(req);
  const t = now();
  const session = {
    token: newId(),
    id: newId().slice(0, 8),
    name: clampName(body.name),
    joinedAt: t,
    startedAt: t,
    lastSeenAt: t,
    lastButtonAt: 0,
  };
  sessions.set(session.token, session);
  promoteQueue();
  if (!activeSession) {
    activeSession = session;
  } else {
    queue.push(session);
  }
  json(res, 200, buildStateFor(session.token));
}

async function handleLeave(req, res) {
  const body = await readJson(req);
  const session = sessions.get(String(body.token || ""));
  if (session) removeSession(session);
  promoteQueue();
  json(res, 200, buildStateFor(null));
}

async function handleButton(req, res) {
  const body = await readJson(req);
  const session = touchSession(String(body.token || ""));
  if (!session || !activeSession || activeSession.token !== session.token) {
    json(res, 403, { ok: false, error: "No tienes el turno activo." });
    return;
  }

  const t = now();
  if (t - session.lastButtonAt < BUTTON_COOLDOWN_MS) {
    json(res, 429, { ok: false, error: "Boton muy rapido." });
    return;
  }

  const button = String(body.button || "").toLowerCase();
  if (!["left", "middle", "right"].includes(button)) {
    json(res, 400, { ok: false, error: "Boton invalido." });
    return;
  }

  session.lastButtonAt = t;
  commandQueue.push({
    id: newId().slice(0, 10),
    button,
    by: session.name,
    createdAt: t,
  });
  json(res, 200, { ok: true });
}

async function handleTamaState(req, res) {
  const token = req.headers["x-tama-token"] || new URL(req.url, "http://localhost").searchParams.get("token");
  if (token !== TAMA_SHARED_SECRET) {
    json(res, 401, { ok: false, error: "Token invalido." });
    return;
  }

  const body = await readJson(req);
  latestTamaState = body;
  tamaLastSeenAt = now();

  const cutoff = now() - COMMAND_MAX_AGE_MS;
  commandQueue = commandQueue.filter((command) => command.createdAt >= cutoff);
  const commands = commandQueue.splice(0, 8);
  const encoded = commands
    .map((command) => ({ left: "L", middle: "M", right: "R" }[command.button]))
    .filter(Boolean)
    .join("");

  json(res, 200, {
    ok: true,
    commands: encoded,
    poll_ms: 250,
  });
}

function handleApiState(req, res) {
  const url = new URL(req.url, "http://localhost");
  json(res, 200, buildStateFor(url.searchParams.get("session")));
}

function serveIndex(res) {
  text(res, 200, INDEX_HTML, "text/html; charset=utf-8");
}

const INDEX_HTML = String.raw`<!doctype html>
<html lang="es">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Tama Care Room</title>
<style>
:root{color-scheme:dark;--bg:#0b0c0f;--panel:#171a20;--line:#303640;--text:#f2f4f8;--muted:#a7b0bd;--ok:#55d68b;--warn:#f2c94c;--bad:#ff6b6b}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--text);font:15px/1.4 system-ui,-apple-system,Segoe UI,sans-serif}
main{width:min(980px,100%);margin:0 auto;padding:18px;display:grid;gap:14px}
.top,.row{display:flex;align-items:center;justify-content:space-between;gap:10px;flex-wrap:wrap}h1{font-size:23px;margin:0}
.layout{display:grid;grid-template-columns:minmax(280px,1fr) minmax(260px,340px);gap:14px}.panel,.card{background:var(--panel);border:1px solid var(--line);border-radius:8px}.panel{padding:14px}.card{padding:12px}
canvas{width:100%;max-width:420px;aspect-ratio:1;background:#000;border:1px solid var(--line);image-rendering:pixelated}
.screen{display:grid;place-items:center}.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(150px,1fr));gap:10px}
.label{color:var(--muted);font-size:12px;text-transform:uppercase;letter-spacing:.06em}.value{font-size:17px;margin-top:4px;word-break:break-word}
input,button{font:inherit}input{width:100%;border:1px solid var(--line);border-radius:6px;padding:10px;background:#0f1116;color:var(--text)}
button{border:1px solid var(--line);border-radius:6px;padding:10px 12px;background:#232833;color:var(--text);cursor:pointer}button:hover{border-color:#687182}button:disabled{opacity:.45;cursor:not-allowed}
.controls{display:grid;grid-template-columns:repeat(3,1fr);gap:8px}.controls button{min-height:58px;font-size:18px}
.ok{color:var(--ok)}.warn{color:var(--warn)}.bad{color:var(--bad)}.small{color:var(--muted);font-size:13px}.queue{display:grid;gap:6px;margin-top:8px}.queue div{padding:8px;border:1px solid var(--line);border-radius:6px}
@media(max-width:760px){.layout{grid-template-columns:1fr}}
</style>
</head>
<body>
<main>
  <div class="top">
    <h1>Tama Care Room</h1>
    <button id="audio">Activar sonido</button>
  </div>
  <div class="layout">
    <section class="panel screen"><canvas id="lcd" width="256" height="256"></canvas></section>
    <aside class="panel">
      <div id="joinBox">
        <div class="label">Entrar a la cola</div>
        <div class="row" style="margin-top:8px">
          <input id="name" placeholder="Tu nombre" autocomplete="name">
          <button id="join">Entrar</button>
        </div>
      </div>
      <div id="sessionBox" hidden>
        <div class="label">Tu turno</div>
        <div id="turnText" class="value">...</div>
        <div class="controls" style="margin-top:12px">
          <button data-button="left">Izq</button>
          <button data-button="middle">OK</button>
          <button data-button="right">Der</button>
        </div>
        <div class="row" style="margin-top:10px">
          <button id="leave">Terminar turno</button>
        </div>
      </div>
      <div class="card" style="margin-top:12px">
        <div class="label">Cuidador actual</div>
        <div id="active" class="value">...</div>
      </div>
      <div class="card" style="margin-top:12px">
        <div class="label">Lista de espera</div>
        <div id="queue" class="queue small">...</div>
      </div>
    </aside>
  </div>
  <section class="grid">
    <div class="card"><div class="label">T-QT</div><div id="online" class="value">...</div></div>
    <div class="card"><div class="label">Red</div><div id="net" class="value">...</div></div>
    <div class="card"><div class="label">Hora Tama</div><div id="tamaTime" class="value">--:--:--</div></div>
    <div class="card"><div class="label">Personaje</div><div id="character" class="value">...</div></div>
  </section>
</main>
<script>
const $=id=>document.getElementById(id);
const lcd=$('lcd'), c=lcd.getContext('2d');
let token=localStorage.getItem('tamaSession')||'', audioEnabled=false, audioCtx=null, osc=null, gain=null, beepTimer=null, lastSoundEvent=null;
const iconNames=['FOOD','LIGHT','GAME','MED','WC','STAT','DISC','ATTN'];

function api(path,body){
  return fetch(path,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body||{})}).then(r=>r.json());
}
function stopTone(){if(beepTimer){clearTimeout(beepTimer);beepTimer=null}if(osc){osc.stop();osc.disconnect();gain.disconnect();osc=null;gain=null}}
function startTone(freq){if(!audioEnabled||!audioCtx)return;if(!osc){osc=audioCtx.createOscillator();gain=audioCtx.createGain();osc.type='square';gain.gain.value=0.04;osc.connect(gain).connect(audioCtx.destination);osc.start()}osc.frequency.value=freq||880}
function playBeep(freq){stopTone();startTone(freq);beepTimer=setTimeout(()=>{beepTimer=null;stopTone()},90)}
function setTamaSound(sound){if(!sound||!audioEnabled||!audioCtx){stopTone();return}const event=Number(sound.event||0),freq=Number(sound.frequency||880);if(lastSoundEvent===null)lastSoundEvent=event;if(event!==lastSoundEvent){lastSoundEvent=event;playBeep(freq);return}if(sound.playing)startTone(freq);else if(!beepTimer)stopTone()}
function rowBit(hex,x){const b=parseInt(hex.slice((x>>3)*2,(x>>3)*2+2),16);return (b&(0x80>>(x&7)))!==0}
function drawScreen(screen){
  c.fillStyle='#000';c.fillRect(0,0,256,256);
  if(!screen||!screen.rows){c.fillStyle='#9aa3af';c.font='15px system-ui';c.textAlign='center';c.fillText('Esperando T-QT',128,128);return}
  const scale=8,ox=0,oy=56;c.fillStyle=screen.color||'#fff';
  for(let y=0;y<screen.height;y++)for(let x=0;x<screen.width;x++)if(rowBit(screen.rows[y],x))c.fillRect(ox+x*scale,oy+y*scale,scale,scale);
  c.font='10px system-ui';c.textAlign='center';c.textBaseline='top';
  for(let i=0;i<8;i++){const active=(screen.icons&(1<<i))!==0,x=i*32+16;c.fillStyle=active?'#fff':'#3a3f46';if(active){c.beginPath();c.moveTo(x,190);c.lineTo(x-5,198);c.lineTo(x+5,198);c.fill()}c.fillText(iconNames[i],x,210)}
}
function render(state){
  const user=state.user,tama=state.tama;
  $('joinBox').hidden=Boolean(user);$('sessionBox').hidden=!user;
  document.querySelectorAll('[data-button]').forEach(b=>b.disabled=!user?.active);
  $('turnText').innerHTML=!user?'Sin sesion':user.active?'<span class="ok">Tienes el mando</span>':'<span class="warn">En espera: #'+user.position+'</span>';
  $('active').textContent=state.turn.active?state.turn.active.name:'Libre';
  $('queue').innerHTML=state.turn.queue.length?state.turn.queue.map((q,i)=>'<div>#'+(i+1)+' '+q.name+'</div>').join(''):'Sin espera';
  $('online').innerHTML=state.tamaOnline?'<span class="ok">Online</span>':'<span class="bad">Sin conexion</span>';
  $('net').textContent=tama?.wifi?.ssid||'...';
  $('tamaTime').textContent=tama?.tama_time||'--:--:--';
  $('character').textContent=tama?.screen?.character??'...';
  drawScreen(tama?.screen);
  setTamaSound(tama?.sound);
}
async function tick(){
  try{const state=await fetch('/api/state?session='+encodeURIComponent(token),{cache:'no-store'}).then(r=>r.json());if(!state.user&&token){token='';localStorage.removeItem('tamaSession')}render(state)}catch(e){}
  setTimeout(tick,300);
}
$('join').onclick=async()=>{const state=await api('/api/join',{name:$('name').value});token=state.user.token;localStorage.setItem('tamaSession',token);render(state)};
$('leave').onclick=async()=>{await api('/api/leave',{token});token='';localStorage.removeItem('tamaSession')};
document.querySelectorAll('[data-button]').forEach(button=>button.onclick=()=>api('/api/button',{token,button:button.dataset.button}).catch(()=>{}));
$('audio').onclick=async()=>{if(audioEnabled){audioEnabled=false;stopTone();$('audio').textContent='Activar sonido';return}audioCtx=audioCtx||new (window.AudioContext||window.webkitAudioContext)();await audioCtx.resume();audioEnabled=true;$('audio').textContent='Desactivar sonido'};
window.addEventListener('keydown',e=>{const map={ArrowLeft:'left',Enter:'middle',ArrowRight:'right'};if(map[e.key]){e.preventDefault();api('/api/button',{token,button:map[e.key]}).catch(()=>{})}});
tick();
</script>
</body>
</html>`;

const server = http.createServer(async (req, res) => {
  try {
    if (req.method === "OPTIONS") {
      json(res, 204, {});
      return;
    }

    const url = new URL(req.url, "http://localhost");
    if (req.method === "GET" && url.pathname === "/") return serveIndex(res);
    if (req.method === "GET" && url.pathname === "/health") return json(res, 200, { ok: true });
    if (req.method === "GET" && url.pathname === "/api/state") return handleApiState(req, res);
    if (req.method === "POST" && url.pathname === "/api/join") return handleJoin(req, res);
    if (req.method === "POST" && url.pathname === "/api/leave") return handleLeave(req, res);
    if (req.method === "POST" && url.pathname === "/api/button") return handleButton(req, res);
    if (req.method === "POST" && url.pathname === "/api/tqt/state") return handleTamaState(req, res);

    json(res, 404, { ok: false, error: "Not found" });
  } catch (error) {
    console.error(error);
    json(res, 500, { ok: false, error: "Server error" });
  }
});

server.listen(PORT, HOST, () => {
  console.log(`Tama Care Room: http://localhost:${PORT}`);
  console.log(`T-QT endpoint: http://<PC-IP>:${PORT}/api/tqt/state`);
});

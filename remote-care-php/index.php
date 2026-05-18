<!doctype html>
<html lang="en">
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
    <button id="audio">Enable sound</button>
  </div>
  <div class="layout">
    <section class="panel screen"><canvas id="lcd" width="256" height="256"></canvas></section>
    <aside class="panel">
      <div id="joinBox">
        <div class="label">Join the queue</div>
        <div class="row" style="margin-top:8px">
          <input id="name" placeholder="Your name" autocomplete="name">
          <input id="room" placeholder="Room code" autocomplete="off">
          <button id="join">Join</button>
        </div>
        <p id="joinError" class="small bad"></p>
      </div>
      <div id="sessionBox" hidden>
        <div class="label">Your turn</div>
        <div id="turnText" class="value">...</div>
        <div class="controls" style="margin-top:12px">
          <button data-button="left">Left</button>
          <button data-button="middle">OK</button>
          <button data-button="right">Right</button>
        </div>
        <div class="row" style="margin-top:10px">
          <button id="leave">End turn</button>
        </div>
      </div>
      <div class="card" style="margin-top:12px">
        <div class="label">Current keeper</div>
        <div id="active" class="value">...</div>
      </div>
      <div class="card" style="margin-top:12px">
        <div class="label">Waitlist</div>
        <div id="queue" class="queue small">...</div>
      </div>
    </aside>
  </div>
  <section class="grid">
    <div class="card"><div class="label">T-QT</div><div id="online" class="value">...</div></div>
    <div class="card"><div class="label">Remote link</div><div id="net" class="value">...</div></div>
    <div class="card"><div class="label">Tama time</div><div id="tamaTime" class="value">--:--:--</div></div>
    <div class="card"><div class="label">Character</div><div id="character" class="value">...</div></div>
  </section>
</main>
<script>
const $=id=>document.getElementById(id);
const lcd=$('lcd'), c=lcd.getContext('2d');
let token=localStorage.getItem('tamaSession')||'', audioEnabled=false, audioCtx=null, osc=null, gain=null, beepTimer=null, lastSoundEvent=null;
const iconNames=['FOOD','LIGHT','GAME','MED','WC','STAT','DISC','ATTN'];

function post(action,body){
  return fetch('api.php?action='+encodeURIComponent(action),{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body||{})}).then(r=>r.json());
}
function stopTone(){if(beepTimer){clearTimeout(beepTimer);beepTimer=null}if(osc){osc.stop();osc.disconnect();gain.disconnect();osc=null;gain=null}}
function startTone(freq){if(!audioEnabled||!audioCtx)return;if(!osc){osc=audioCtx.createOscillator();gain=audioCtx.createGain();osc.type='square';gain.gain.value=0.04;osc.connect(gain).connect(audioCtx.destination);osc.start()}osc.frequency.value=freq||880}
function playBeep(freq){stopTone();startTone(freq);beepTimer=setTimeout(()=>{beepTimer=null;stopTone()},90)}
function setTamaSound(sound){if(!sound||!audioEnabled||!audioCtx){stopTone();return}const event=Number(sound.event||0),freq=Number(sound.frequency||880);if(lastSoundEvent===null)lastSoundEvent=event;if(event!==lastSoundEvent){lastSoundEvent=event;playBeep(freq);return}if(sound.playing)startTone(freq);else if(!beepTimer)stopTone()}
function rowBit(hex,x){const b=parseInt(hex.slice((x>>3)*2,(x>>3)*2+2),16);return (b&(0x80>>(x&7)))!==0}
function drawScreen(screen){
  c.fillStyle='#000';c.fillRect(0,0,256,256);
  if(!screen||!screen.rows){c.fillStyle='#9aa3af';c.font='15px system-ui';c.textAlign='center';c.fillText('Waiting for T-QT',128,128);return}
  const scale=8,ox=0,oy=56;c.fillStyle=screen.color||'#fff';
  for(let y=0;y<screen.height;y++)for(let x=0;x<screen.width;x++)if(rowBit(screen.rows[y],x))c.fillRect(ox+x*scale,oy+y*scale,scale,scale);
  c.font='10px system-ui';c.textAlign='center';c.textBaseline='top';
  for(let i=0;i<8;i++){const active=(screen.icons&(1<<i))!==0,x=i*32+16;c.fillStyle=active?'#fff':'#3a3f46';if(active){c.beginPath();c.moveTo(x,190);c.lineTo(x-5,198);c.lineTo(x+5,198);c.fill()}c.fillText(iconNames[i],x,210)}
}
function render(state){
  const user=state.user,tama=state.tama;
  $('joinBox').hidden=Boolean(user);$('sessionBox').hidden=!user;
  document.querySelectorAll('[data-button]').forEach(b=>b.disabled=!user?.active);
  $('turnText').innerHTML=!user?'No session':user.active?'<span class="ok">You have control</span>':'<span class="warn">Waiting: #'+user.position+'</span>';
  $('active').textContent=state.turn.active?state.turn.active.name:'Available';
  $('queue').innerHTML=state.turn.queue.length?state.turn.queue.map((q,i)=>'<div>#'+(i+1)+' '+q.name+'</div>').join(''):'No waitlist';
  $('online').innerHTML=state.tamaOnline?'<span class="ok">Online</span>':'<span class="bad">Offline</span>';
  $('net').textContent=state.tamaOnline?'Connected':'Waiting';
  $('tamaTime').textContent=tama?.tama_time||'--:--:--';
  $('character').textContent=tama?.screen?.character??'...';
  drawScreen(tama?.screen);
  setTamaSound(tama?.sound);
}
async function tick(){
  try{const state=await fetch('api.php?action=state&session='+encodeURIComponent(token),{cache:'no-store'}).then(r=>r.json());if(!state.user&&token){token='';localStorage.removeItem('tamaSession')}render(state)}catch(e){}
  setTimeout(tick,450);
}
$('join').onclick=async()=>{const state=await post('join',{name:$('name').value,room:$('room').value});if(!state.ok){$('joinError').textContent=state.error||'Could not join';return}$('joinError').textContent='';token=state.user.token;localStorage.setItem('tamaSession',token);render(state)};
$('leave').onclick=async()=>{await post('leave',{token});token='';localStorage.removeItem('tamaSession')};
document.querySelectorAll('[data-button]').forEach(button=>button.addEventListener('pointerdown',e=>{e.preventDefault();post('button',{token,button:button.dataset.button}).catch(()=>{})}));
$('audio').onclick=async()=>{if(audioEnabled){audioEnabled=false;stopTone();$('audio').textContent='Enable sound';return}audioCtx=audioCtx||new (window.AudioContext||window.webkitAudioContext)();await audioCtx.resume();audioEnabled=true;$('audio').textContent='Disable sound'};
window.addEventListener('keydown',e=>{const map={ArrowLeft:'left',Enter:'middle',ArrowRight:'right'};if(map[e.key]){e.preventDefault();post('button',{token,button:map[e.key]}).catch(()=>{})}});
tick();
</script>
</body>
</html>

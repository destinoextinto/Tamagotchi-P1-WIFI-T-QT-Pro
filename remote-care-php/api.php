<?php
$config = require __DIR__ . '/config.php';
$dataDir = __DIR__ . '/data';
$storeFile = $dataDir . '/state.json';

if (!is_dir($dataDir)) {
    mkdir($dataDir, 0755, true);
}

function now_ms() {
    return (int) round(microtime(true) * 1000);
}

function json_response($status, $body) {
    http_response_code($status);
    header('Content-Type: application/json; charset=utf-8');
    header('Cache-Control: no-store');
    header('Access-Control-Allow-Origin: *');
    header('Access-Control-Allow-Methods: GET,POST,OPTIONS');
    header('Access-Control-Allow-Headers: Content-Type,X-Tama-Token');
    echo json_encode($body);
    exit;
}

function read_request_data() {
    $raw = file_get_contents('php://input');
    $data = json_decode($raw, true);
    if (is_array($data)) {
        return $data;
    }
    $form = [];
    parse_str($raw, $form);
    return array_merge($_POST, $form);
}

function default_store() {
    return [
        'sessions' => [],
        'activeToken' => null,
        'queue' => [],
        'tama' => null,
        'tamaLastSeenAt' => 0,
        'commands' => [],
    ];
}

function open_store($file) {
    $fh = fopen($file, 'c+');
    if (!$fh) {
        json_response(500, ['ok' => false, 'error' => 'Could not open data/state.json']);
    }
    flock($fh, LOCK_EX);
    rewind($fh);
    $raw = stream_get_contents($fh);
    $store = json_decode($raw, true);
    if (!is_array($store)) {
        $store = default_store();
    }
    foreach (default_store() as $key => $value) {
        if (!array_key_exists($key, $store)) {
            $store[$key] = $value;
        }
    }
    return [$fh, $store];
}

function save_store($fh, $store) {
    rewind($fh);
    ftruncate($fh, 0);
    fwrite($fh, json_encode($store));
    fflush($fh);
    flock($fh, LOCK_UN);
    fclose($fh);
}

function new_token($bytes = 16) {
    return bin2hex(random_bytes($bytes));
}

function clean_name($name) {
    $name = trim(preg_replace('/\s+/', ' ', (string) $name));
    if ($name === '') {
        $name = 'Invitado';
    }
    return substr($name, 0, 32);
}

function remove_session(&$store, $token) {
    unset($store['sessions'][$token]);
    if ($store['activeToken'] === $token) {
        $store['activeToken'] = null;
    }
    $store['queue'] = array_values(array_filter($store['queue'], function ($item) use ($token) {
        return $item !== $token;
    }));
}

function promote_queue(&$store, $config) {
    $t = now_ms();
    $activeToken = $store['activeToken'];
    if ($activeToken && isset($store['sessions'][$activeToken])) {
        $active = $store['sessions'][$activeToken];
        $inactive = $t - $active['lastSeenAt'] > $config['session_timeout_ms'];
        $expired = $t - $active['startedAt'] > $config['turn_limit_ms'];
        if ($inactive || $expired) {
            remove_session($store, $activeToken);
        }
    } else {
        $store['activeToken'] = null;
    }

    $aliveQueue = [];
    foreach ($store['queue'] as $token) {
        if (isset($store['sessions'][$token]) && $t - $store['sessions'][$token]['lastSeenAt'] <= $config['session_timeout_ms']) {
            $aliveQueue[] = $token;
        } else {
            unset($store['sessions'][$token]);
        }
    }
    $store['queue'] = $aliveQueue;

    if (!$store['activeToken'] && count($store['queue']) > 0) {
        $next = array_shift($store['queue']);
        $store['activeToken'] = $next;
        $store['sessions'][$next]['startedAt'] = $t;
        $store['sessions'][$next]['lastButtonAt'] = 0;
    }
}

function touch_session(&$store, $token, $config) {
    if (!$token || !isset($store['sessions'][$token])) {
        promote_queue($store, $config);
        return null;
    }
    $store['sessions'][$token]['lastSeenAt'] = now_ms();
    promote_queue($store, $config);
    return isset($store['sessions'][$token]) ? $store['sessions'][$token] : null;
}

function public_session($session, $config) {
    if (!$session) {
        return null;
    }
    return [
        'id' => $session['id'],
        'name' => $session['name'],
        'startedAt' => $session['startedAt'],
        'lastSeenAt' => $session['lastSeenAt'],
        'turnEndsAt' => $session['startedAt'] + $config['turn_limit_ms'],
    ];
}

function build_state(&$store, $token, $config) {
    $session = touch_session($store, $token, $config);
    $activeToken = $store['activeToken'];
    $activeSession = $activeToken && isset($store['sessions'][$activeToken]) ? $store['sessions'][$activeToken] : null;
    $isActive = $session && $activeToken === $token;
    $position = 0;
    if ($session && !$isActive) {
        $index = array_search($token, $store['queue'], true);
        $position = $index === false ? 0 : $index + 1;
    }

    $queue = [];
    foreach ($store['queue'] as $queuedToken) {
        if (isset($store['sessions'][$queuedToken])) {
            $queue[] = public_session($store['sessions'][$queuedToken], $config);
        }
    }

    $tama = $store['tama'];
    if (is_array($tama) && isset($tama['wifi']) && is_array($tama['wifi'])) {
        unset($tama['wifi']['ssid']);
    }

    return [
        'ok' => true,
        'tamaOnline' => $store['tamaLastSeenAt'] > 0 && now_ms() - $store['tamaLastSeenAt'] < 10000,
        'tamaLastSeenAt' => $store['tamaLastSeenAt'],
        'tama' => $tama,
        'turn' => [
            'active' => public_session($activeSession, $config),
            'queue' => $queue,
            'turnLimitMs' => $config['turn_limit_ms'],
            'sessionTimeoutMs' => $config['session_timeout_ms'],
        ],
        'user' => $session ? [
            'token' => $token,
            'name' => $session['name'],
            'active' => $isActive,
            'queued' => !$isActive && $position > 0,
            'position' => $position,
        ] : null,
    ];
}

function pop_encoded_commands(&$store, $config) {
    $cutoff = now_ms() - $config['command_max_age_ms'];
    $fresh = [];
    $send = [];
    foreach ($store['commands'] as $command) {
        if ($command['createdAt'] >= $cutoff && count($send) < 8) {
            $send[] = $command;
        } elseif ($command['createdAt'] >= $cutoff) {
            $fresh[] = $command;
        }
    }
    $store['commands'] = $fresh;

    $encoded = '';
    foreach ($send as $command) {
        if ($command['button'] === 'left') {
            $encoded .= 'L';
        } elseif ($command['button'] === 'middle') {
            $encoded .= 'M';
        } elseif ($command['button'] === 'right') {
            $encoded .= 'R';
        }
    }
    return $encoded;
}

if ($_SERVER['REQUEST_METHOD'] === 'OPTIONS') {
    json_response(204, ['ok' => true]);
}

$action = isset($_GET['action']) ? $_GET['action'] : '';
if ($action === 'health') {
    json_response(200, ['ok' => true, 'php' => PHP_VERSION]);
}

list($fh, $store) = open_store($storeFile);

try {
    $tamaToken = isset($_SERVER['HTTP_X_TAMA_TOKEN']) ? $_SERVER['HTTP_X_TAMA_TOKEN'] : '';
    if ($tamaToken !== '' && !hash_equals($config['tama_shared_secret'], $tamaToken)) {
        save_store($fh, $store);
        json_response(401, ['ok' => false, 'error' => 'Invalid token']);
    }

    if ($_SERVER['REQUEST_METHOD'] === 'GET' && $action === 'poll' && $tamaToken !== '') {
        $encoded = pop_encoded_commands($store, $config);
        save_store($fh, $store);
        json_response(200, ['ok' => true, 'commands' => $encoded, 'poll_ms' => 80]);
    }

    if ($_SERVER['REQUEST_METHOD'] === 'POST' && $tamaToken !== '') {
        if (!hash_equals($config['tama_shared_secret'], $tamaToken)) {
            save_store($fh, $store);
            json_response(401, ['ok' => false, 'error' => 'Invalid token']);
        }

        $store['tama'] = read_request_data();
        $store['tamaLastSeenAt'] = now_ms();

        $encoded = pop_encoded_commands($store, $config);
        save_store($fh, $store);
        json_response(200, ['ok' => true, 'commands' => $encoded, 'poll_ms' => 80]);
    }

    if ($action === 'state') {
        $token = isset($_GET['session']) ? $_GET['session'] : '';
        $state = build_state($store, $token, $config);
        save_store($fh, $store);
        json_response(200, $state);
    }

    if ($_SERVER['REQUEST_METHOD'] !== 'POST') {
        save_store($fh, $store);
        json_response(404, ['ok' => false, 'error' => 'Route not found']);
    }

    $body = read_request_data();

    if ($action === 'join') {
        $room = isset($body['room']) ? trim((string) $body['room']) : '';
        if ($config['room_code'] !== '' && !hash_equals($config['room_code'], $room)) {
            save_store($fh, $store);
            json_response(403, ['ok' => false, 'error' => 'Wrong room code']);
        }

        $token = new_token();
        $session = [
            'token' => $token,
            'id' => substr(new_token(4), 0, 8),
            'name' => clean_name(isset($body['name']) ? $body['name'] : ''),
            'joinedAt' => now_ms(),
            'startedAt' => now_ms(),
            'lastSeenAt' => now_ms(),
            'lastButtonAt' => 0,
        ];
        $store['sessions'][$token] = $session;
        promote_queue($store, $config);
        if (!$store['activeToken']) {
            $store['activeToken'] = $token;
        } else {
            $store['queue'][] = $token;
        }
        $state = build_state($store, $token, $config);
        save_store($fh, $store);
        json_response(200, $state);
    }

    if ($action === 'leave') {
        $token = isset($body['token']) ? (string) $body['token'] : '';
        if ($token) {
            remove_session($store, $token);
        }
        promote_queue($store, $config);
        $state = build_state($store, '', $config);
        save_store($fh, $store);
        json_response(200, $state);
    }

    if ($action === 'button') {
        $token = isset($body['token']) ? (string) $body['token'] : '';
        $session = touch_session($store, $token, $config);
        if (!$session || $store['activeToken'] !== $token) {
            save_store($fh, $store);
            json_response(403, ['ok' => false, 'error' => 'You do not have the active turn']);
        }

        $button = strtolower(isset($body['button']) ? (string) $body['button'] : '');
        if (!in_array($button, ['left', 'middle', 'right'], true)) {
            save_store($fh, $store);
            json_response(400, ['ok' => false, 'error' => 'Invalid button']);
        }

        $now = now_ms();
        if ($now - $store['sessions'][$token]['lastButtonAt'] < $config['button_cooldown_ms']) {
            save_store($fh, $store);
            json_response(429, ['ok' => false, 'error' => 'Button too fast']);
        }

        $store['sessions'][$token]['lastButtonAt'] = $now;
        $store['commands'][] = [
            'id' => substr(new_token(5), 0, 10),
            'button' => $button,
            'by' => $store['sessions'][$token]['name'],
            'createdAt' => $now,
        ];
        save_store($fh, $store);
        json_response(200, ['ok' => true]);
    }

    save_store($fh, $store);
    json_response(404, ['ok' => false, 'error' => 'Action not found']);
} catch (Throwable $e) {
    save_store($fh, $store);
    json_response(500, ['ok' => false, 'error' => 'Internal error']);
}

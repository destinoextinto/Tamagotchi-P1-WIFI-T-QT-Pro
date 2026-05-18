# Tama Care Room

Servidor local para cuidar el T-QT Tamagotchi en grupo con turnos.

## Ejecutar localmente

```powershell
& 'C:\Users\manuc\.cache\codex-runtimes\codex-primary-runtime\dependencies\node\bin\node.exe' remote-care\server.js
```

Abre:

```text
http://localhost:8787
```

En la red local, otros dispositivos pueden usar:

```text
http://192.168.100.10:8787
```

## Como funciona

- El T-QT publica su estado en `POST /api/tqt/state`.
- El servidor guarda la ultima pantalla y sonido.
- Los usuarios entran con nombre.
- Si nadie tiene el mando, el primer usuario recibe el turno.
- Si ya hay alguien cuidando, los demas entran a la lista de espera.
- Solo el usuario con turno activo puede mandar botones.
- Al presionar `Terminar turno`, el siguiente usuario recibe el mando.
- Si el usuario activo se desconecta por 45 segundos, el turno se libera automaticamente.

## Variables utiles

```powershell
$env:PORT='8787'
$env:TAMA_SHARED_SECRET='cambia-este-token'
$env:TURN_LIMIT_MS='300000'
$env:SESSION_TIMEOUT_MS='45000'
```

El firmware debe usar el mismo `TAMA_SHARED_SECRET` en `TQT_REMOTE_TOKEN`, y debe apuntar a:

```text
http://TU_SERVIDOR/api/tqt/state
```

Para publicar esto en un dominio, lo ideal es poner este servidor detras de HTTPS y cambiar `TQT_REMOTE_URL` en el firmware al dominio publico.

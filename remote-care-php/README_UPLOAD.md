# Deploy Tama Care Room on shared PHP hosting

Upload the contents of this folder to the directory mapped to your subdomain, for example:

```text
http://tama.example.com/
```

The server should contain:

```text
/index.php
/api.php
/config.php
/.htaccess
/data/.htaccess
```

Then test:

```text
http://tama.example.com/api.php?action=health
```

It should respond with:

```json
{"ok":true,"php":"8.x.x"}
```

## Room code

The default room code is:

```text
Extinto2222
```

Change it in `config.php` before deploying your own public room.

## T-QT token

The shared token is configured in `config.php`:

```text
change-this-shared-secret
```

Change it before deploying, then use the same value in the firmware build flag:

```ini
-D TQT_REMOTE_TOKEN=\"your-real-shared-secret\"
```

The firmware URL should point at your deployed API:

```ini
-D TQT_REMOTE_URL=\"http://tama.example.com/api.php\"
```

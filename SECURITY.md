# Security Policy

## Supported versions

Only the latest release receives security fixes (currently 1.7.x). Update from the LICENSE / OTA card or download the newest installer from `releases/`.

## Reporting a vulnerability

Open a private security advisory on the GitHub repository (Security tab → Report a vulnerability) or contact the maintainer by e-mail. Please include the version, the steps to reproduce and the impact. You can expect an answer within a few days; accepted reports are fixed in the next release and credited unless you prefer otherwise.

## What the local server does to stay safe (1.7.0)

- Listens on 127.0.0.1 only; checks the `Host` header (DNS rebinding) and the `Origin` header (only local pages).
- Sends CSP `frame-ancestors`, `Permissions-Policy`, COOP / CORP, `nosniff`, `no-referrer` and `X-Frame-Options` on every response.
- Serves only the page and its assets (html / js / css / json / png / svg): never `bridge/`, `client/`, `scripts/`, `native/`, `installer/`, `dist/`, `releases/`, `node_modules/` or dotfiles.
- Rate limits (`/api` reads 900/min, actions 40/min, WebSocket 120/min, 24 open audio sockets); header, request and body size limits. `BRIDGE_RATE_LIMIT=0` turns the limits off for testing.
- Actions (license, update, driver download) need a custom `X-Mixer-Action` header, which forces a CORS preflight.
- Downloads: HTTPS only, host allow-list, every redirect hop is checked before it is requested, SHA-256 verified, never run automatically. Updates also need a publisher signature.
- License keys and the update manifest are verified with ECDSA P-256; the vendor private key is never shipped.
- The Windows helpers (`AudioDevices.exe`, `audio-devices.vbs`, `Audio Mixer.vbs`) are read-only or launch the visible `start-pc-mode.bat`; nothing runs hidden.

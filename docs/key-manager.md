# AmF X1 Key Manager

The vendor's licence key generator (`scripts/license-generator.js`) as a desktop app. It runs the generator's own server on
**127.0.0.1 only**, shows its page in an app window, and stops the server when the window closes. Signing, the PIN lock, editions,
SEND TO AMF X1 ON THIS PC and PUBLISH UPDATE are the generator's code, unchanged. The Key Manager adds a **KEY LIST**.

## Install

**Windows (x64):** download `releases/windows/AmF-X1-Key-Manager-<version>-win-x64.zip`, check it against its `.sha256` (or
`releases/windows/SHA256SUMS.txt`), and unzip it anywhere. You don't need Node.js. Double-click **AmF X1 Key Manager.exe**. The
first time, SmartScreen may say "unknown publisher" (the .exe is not code-signed yet): choose *More info → Run anyway*.

The window is Microsoft Edge (or Chrome) in app mode, with a separate profile in `%LOCALAPPDATA%\AmF X1 Key Manager\window-profile`
that has the password manager, autofill, sync and extensions turned off. If neither browser is found, the page opens in your default
browser, and the Key Manager stops about 2½ minutes after its last tab closes (a few seconds when the tab says goodbye).

**macOS:** in the repository, double-click `tools/key-manager/macos/AmF X1 Key Manager.command` (this needs Node.js). To make a
stand-alone `AmF X1 Key Manager.app`, run `sh tools/key-manager/macos/make-app.sh` **on a Mac**. It needs codesign, so it can't
be built on Linux or Windows.

**From source (any OS):** `npm run key-manager`. To rebuild the Windows zip: `npm run build:key-manager`.

## Unlock

Your keys are signed with the private key in your **key folder**:

| OS | Key folder |
|---|---|
| Windows | `%USERPROFILE%\.audio-mixer-vendor` |
| macOS / Linux | `~/.audio-mixer-vendor` |

You can choose another folder with `--dir <folder>` or `AUDIO_MIXER_VENDOR_DIR`. The folder holds `private.pem.enc` (encrypted with
your PIN), `public.jwk.json`, and the sealed logs (`sales.jsonl`, `issued.jsonl`).

When you start the Key Manager, enter your PIN, plus the authenticator code if you set up a second factor. The unlocked key stays in
this program's memory only. It is dropped when you click LOCK, after 15 minutes without use, or when you close the window. After 5
wrong PINs in a row, unlocking is blocked for 60 seconds. On a new computer with no key yet, the page offers CREATE MY KEY PAIR (a new
PIN-locked pair). It never overwrites an existing one.

## Issue keys

- **One key:** pick the plan, and optionally fill in name, e-mail and machine code (binds the key to one computer), expiry and seats.
  Then click GENERATE KEY and use COPY, E-MAIL or SEND VIA WHATSAPP.
- **Batch:** set "How many" above 1. You get that many keys with random serials, and DOWNLOAD CSV.
- **Paid edition:** BUILD EDITION TOKEN signs a Pro / Studio Master / Ultimate token for one sale. The sale goes into `sales.jsonl`.
- **Key list:** click KEY LIST in the header. It lists every key issued from the Key Manager and every edition sale, newest first.
  You can search, filter, COPY KEY to resend one, and EXPORT CSV. Cells that start with `=`, `+`, `-` or `@` are neutralised for
  spreadsheets. The records are sealed to your public key on disk and opened in memory only while the folder is unlocked. Keys
  issued earlier from the command line or the plain generator were not recorded, so they don't appear here.
- **Revocation:** keys are signed offline, so a key stops working only when the licence server's **signed revocation list** names it.
  The Key Manager can't sign that list. MARK REVOKE records the serial and a reason in `revoke-marks.json` (no customer data).
  Then COPY MARKED SERIALS and revoke those serials on the licence server.

## Send to the console on this PC

After issuing a single key, click **SEND TO AMF X1 ON THIS PC**. It writes `AmF-X1-<serial>.amixkey` into the console's data folder
(`%APPDATA%\AMFB Audio\AmF X1` on Windows; your Downloads folder if that fails). The Qt console's AUTO LICENCE picks it up and
activates it. Batches can't be sent this way.

## Publish an update

PUBLISH UPDATE signs `update.json` with the unlocked key and serves it at `http://127.0.0.1:<port>/update.json`. The installers come
from a `releases/` folder next to the .exe (from source: the repository's `releases/`). The Key Manager doesn't open the public
listener for customers' requests (`--public`). Use `node scripts/license-generator.js --public` for that.

## Back up the key folder

Copy the **whole key folder** to a USB drive or a password manager. Don't use a cloud-synced folder or the repository. Do it now, and
again after you change the PIN or set up the second factor. Without `private.pem.enc` **and** the PIN, you can't sign any more keys
for builds that trust this key. A copy of the folder is useless without the PIN, and the sealed sales and issued logs can only be read
with it.

## Security notes

- The server binds 127.0.0.1 only and refuses any other `Host` (DNS rebinding) and foreign origins. Every action needs the page's own
  header (CSRF). Every inline script and style on its pages is allowed only by its SHA-256 hash (CSP).
- The .exe contains Node.js (the official `win-x64/node.exe`, checked against the signed `SHASUMS256.txt.asc`) and the generator's
  source files. No key is ever embedded, and the build refuses a file that contains a private key. On start, it unpacks those files to
  `%LOCALAPPDATA%\AmF X1 Key Manager\app\<version>-<id>` and re-checks their SHA-256 every time.
- There's no telemetry. The log (`%LOCALAPPDATA%\AmF X1 Key Manager\key-manager.log`) records only start, stop and the port, never a
  key or a PIN. The only outbound request is the generator's own public-IP lookup for PUBLISH VENDOR ADDRESS.

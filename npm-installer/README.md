# audio-mixer-installer

Installs [Audio Mixer](https://github.com/amirf1389/audio-mixer) the way your operating system does it. It picks the right release file from the signed release manifest, checks the signature and the SHA-256, and only then runs the system's own installation.

```
npx audio-mixer-install                 # asks before it runs anything
npx audio-mixer-install --dry-run       # shows what it would download and run
npx audio-mixer-install uninstall
```

| System | File | What runs |
|---|---|---|
| Windows 64-bit | `Audio Mixer-<version>.exe` | the signed setup (Windows asks for administrator rights): installs to `C:\Program Files\Audio Mixer` |
| Windows 32-bit | `AudioMixer-<version>-x86.msi` | `msiexec /i` → `C:\Program Files (x86)\Audio Mixer` |
| macOS | `AudioMixer-<version>-macos.tar.gz` | `install.command` (→ `/Applications`, or `~/Applications`) |
| Debian / Ubuntu / Mint | `audio-mixer_<version>_all.deb` | `apt install` |
| other Linux | `AudioMixer-<version>-linux.tar.gz` | `install.sh` |
| Android / iOS | `.apk` / Xcode project | only downloaded (npm cannot install phone apps); the how-to is printed |

## ASIO driver (Windows)

The Windows installers always copy the driver files (`asio-driver\` in the install folder). **Registering** the driver, so that "Audio Mixer" appears in DAWs and OBS, is optional: the setup asks (default No), or pass `--asio`. `--yes` and `--quiet` never register it without `--asio`.

## Options

`--yes` no questions · `--quiet` silent Windows install · `--asio` register the ASIO driver · `--dry-run [--json]` show the plan · `--download-only [--out DIR]` · `--server URL` your OTA server or manifest · `--host NAME` one more allowed host · `--dir FOLDER` newest file of this system in a releases folder (offline) · `--file PATH` install this file · `--os / --arch / --distro` choose by hand · `--allow-http` (localhost, testing)

## What it checks

- The manifest must be signed with the Audio Mixer publisher key (pinned in `lib/public-key.json`); a manifest signed by anyone else is refused.
- The downloaded file must match the SHA-256 and size of the signed manifest; otherwise it is deleted and nothing runs.
- Only https is accepted (http only for localhost with `--allow-http`), and only from GitHub or the host of `--server`.
- No dependencies, and installing this package installs nothing else.

The default manifest is `releases/update.json` of the project on GitHub; it must be signed for the version you want (`node scripts/make-update.js --dir <key folder>`).

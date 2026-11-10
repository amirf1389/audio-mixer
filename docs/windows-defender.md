# Windows Defender, SmartScreen and "PUP" false positives

A program that is new, unsigned, packed, or writes other executables out of itself looks like a potentially unwanted program (PUP) to
heuristic scanners. AmF X1.exe is built so that none of those apply, and the rest is signing and reputation, which only the vendor can do.

## What the build does (scripts/build-native.js)

- **Nothing packed, nothing embedded.** The .exe is a plain static MinGW build. The ASIO driver DLL is a separate file next to it
  (`AudioMixerASIO64.dll`); `--install-driver` registers that file in place and copies nothing. The old single-file build that carried the
  DLL as a byte array and wrote it to Program Files is opt-in (`embedDriver`) and not used for releases.
- **Version resource and manifest** (`install/windows/amfx1.rc`, `amfx1.manifest`): CompanyName AMFB Audio, ProductName AmF X1, a file
  description, the version, OriginalFilename `AmF X1.exe`; the manifest asks for `asInvoker` (no elevation), declares Windows 8.1 / 10 / 11
  and DPI awareness. Properties › Details of the file shows all of it.
- **No elevation, no persistence.** The program never asks for administrator rights, installs no service, adds no autorun, touches no
  browser. Driver registration is the one HKLM write and only happens when the user runs `--install-driver` from an administrator prompt.
- **Network only when asked.** `--update` fetches the signed manifest from the project's hosts and verifies the ECDSA signature before
  anything is parsed; nothing is downloaded or run otherwise.

## What the vendor does before a release

1. **Sign** every .exe and .dll with your code-signing certificate: `AMFX1_SIGN_PFX=... AMFX1_SIGN_PASS=... node scripts/sign-windows.js "dist/windows/AmF X1.exe" dist/windows/AudioMixerASIO64.dll`
   then `node scripts/sign-windows.js --verify ...`. The script never creates a certificate and never stores the password. An **EV**
   certificate gives SmartScreen reputation immediately; an **OV** certificate earns it as the signed file is downloaded.
2. **Install with the installer** (`install/windows/amfx1.iss`), signed the same way, into Program Files. Users who run a bare .exe from
   Downloads see SmartScreen's "unknown publisher" until reputation exists; the signed installer avoids that.
3. **Submit a false positive** if Defender still flags a signed build: https://www.microsoft.com/wdsi/filesubmission (choose
   "Software developer", attach the signed file, give the product page). Defender usually clears a signed, clean file within a day or two.
4. **Publish checksums** next to the download (`scripts/release-label.js` writes them) so users can check what they got.

Nothing in this project tries to hide from or disable a scanner. The aim is that a scanner that looks sees exactly what the program is.

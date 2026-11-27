# AmF X1 Windows MSI (Microsoft Store, MSI submission)

Build: `scripts/build-msi.sh 1.26.0.0` (needs `wixl` from msitools). It walks `dist/windows/AmF-X1-<VERSION>-windows-x64/`,
writes `install/windows/amfx1.wxs` and builds `releases/windows/AmF-X1-<VERSION>-x64.msi`. It refuses to build when the
package folder holds private-looking files (*.pem, *.key, *.pfx, *.p12, *.jks, *.keystore, .env*, *token*, *secret*, *.log, settings*.json).

## What the MSI does
- Per-machine install, x64, to `C:\Program Files\AmF X1` (every file of the package folder, one component each).
- Component GUIDs are uuid5 of the relative path (stable across rebuilds). ProductCode is uuid5 of the version (new per version).
  UpgradeCode is fixed: `{8F3C2B71-4E6A-4D2F-9B58-1A7E0C9D3F64}`. Never change it.
- MajorUpgrade: installing a newer version removes the older one; installing an older version over a newer one is blocked.
- Start Menu shortcut "AmF X1" -> `amixos.exe`. Registry `HKLM\Software\AMFB Audio\AmF X1` (InstallDir, Version), as the NSIS setup.
- Apps & features: Publisher AMFB Audio, DisplayVersion, icon from amixos.exe, no Modify / no Repair (uninstall only).
- No dialogs: `msiexec /i AmF-X1-1.26.0.0-x64.msi /qn` installs silently.

## Left out on purpose (compared with install/windows/amfx1.nsi)
- **Windows Firewall rule** (TCP, local subnet, programs amixos.exe and "AmF X1 Network.exe") and **ASIO driver registration**
  (`"AmF X1.exe" --install-driver`). Both need deferred custom actions (netsh / the program run elevated, and their rollback and
  uninstall counterparts), which wixl does not support reliably. Instead:
  - ASIO: the MSI installs both drivers, AudioMixerASIO64.dll (64-bit hosts) and AudioMixerASIO32.dll (32-bit hosts), but
    registers neither. Register both (64-bit and 32-bit) from the app's own SETTINGS, run as administrator. By hand from an
    administrator prompt: `"AmF X1.exe" --install-driver` for the 64-bit one and
    `%WINDIR%\SysWOW64\regsvr32.exe "<install folder>\AudioMixerASIO32.dll"` for the 32-bit one (32-bit registry view).
  - Firewall: Windows asks to allow the program the first time the phone remote listens; accept it for private networks.
- The NSIS setup stays the full installer; it is unchanged.

## Partner Center fields
- App type: **MSI** (EXE or MSI app).
- Package URL: a versioned HTTPS URL that never changes content, e.g.
  `https://<host>/releases/AmF-X1-1.26.0.0-x64.msi` (a new URL for every version).
- Architecture: **x64**. Language: English.
- Installer parameters: none needed; the Store runs MSI packages silently with `/qn` automatically.
- Return codes (standard MSI, the Store knows them):

| Code | Meaning |
|------|---------|
| 0 | Success |
| 3010 | Success, reboot required |
| 1602 | Cancelled by the user |
| 1618 | Another installation is in progress |
| 1638 | Another version of the product is already installed |
| 1603 | Fatal error during installation |
| 112 | Disk full |

## Hosting
The 1.26.0.0 MSI is about 107 MB, over GitHub's 100 MB per-file limit for git, so it is not committed. Publish it as a release
asset (or on any HTTPS host) at the versioned URL above, with its SHA-256.

## Signing
The Store requires the MSI (and ideally the EXEs inside it) to be signed with a code-signing certificate that chains to a
trusted root. The owner signs it on their machine, e.g. `signtool sign /fd SHA256 /tr http://timestamp.digicert.com /td SHA256 /a AmF-X1-1.26.0.0-x64.msi`
(or `osslsigncode` on Linux). No certificate or key is in this repository and none must be added.

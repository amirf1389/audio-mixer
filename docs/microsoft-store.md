# Microsoft Store (MSIX) submission

## Build

1. `scripts/build-console-windows.sh` (the package folder in `dist/windows/AmF-X1-<ver>-windows-x64/`).
2. `MSIX_IDENTITY_NAME=... MSIX_PUBLISHER="CN=..." MSIX_PUBLISHER_DISPLAY_NAME="AMFB Audio" scripts/build-msix.sh`
   - packs with `makemsix` (from github.com/microsoft/msix-packaging, `./makelinux.sh --pack`; set `MAKEMSIX=` or put it on PATH)
     and checks the result with a `makemsix unpack` round trip (manifest and block map are schema-validated);
   - without `makemsix` it writes `releases/windows/AmF-X1-<ver>-x64-msix-layout.zip` (layout folder, `mapping.txt`,
     `pack-msix.cmd` for `MakeAppx pack` on Windows).
3. Output: `releases/windows/AmF-X1-<ver>-x64.msix`, **unsigned** - the Store signs it on ingestion. No signing keys are kept
   in the repository. To side-load for a local test, sign a copy with your own test certificate on Windows; never ship that.

Without the two environment variables the manifest carries `PLACEHOLDER` identity values; Partner Center rejects those.

## Partner Center

1. Partner Center > Apps and games > New product > MSIX or PWA app; reserve the name **AmF X1**.
2. Product management > **Product identity**: copy `Package/Identity/Name` -> `MSIX_IDENTITY_NAME`,
   `Package/Identity/Publisher` -> `MSIX_PUBLISHER`, `Package/Properties/PublisherDisplayName` -> `MSIX_PUBLISHER_DISPLAY_NAME`.
   Rebuild the .msix with them.
3. Start a submission:
   - **Pricing and availability**: markets, price (free download; paid editions keep using their licence key), visibility.
   - **Properties**: category Music, subcategory none; privacy policy URL required (the app uses the network and the
     microphone): `https://<site>/privacy.html` (the PDPA privacy policy page, deploy/website/privacy.html);
     support contact; system requirements (x64, Windows 10 1809 or later, audio interface recommended).
   - **Age ratings**: fill the IARC questionnaire - no violence, no user-to-user chat, no purchases inside the app, shares no
     location; network features (remote control on the local network). Expected result: 3+ / Everyone.
   - **Packages**: upload `AmF-X1-<ver>-x64.msix`; device family Desktop.
   - **Store listings**: description, at least one 1366x768 (or larger) screenshot, the 300x300 store logo.
   - **Submission options > Restricted capabilities**: justify `runFullTrust` (text below).
4. Submit; certification usually takes 1-3 business days.

### runFullTrust justification (paste into the submission)

> AmF X1 is a professional desktop audio mixing console built with Win32 and Qt. It needs full trust to open low-latency audio
> devices through WASAPI and user-installed ASIO drivers, to run its bundled local helper processes (the network remote-control
> service and the local web interface server that phones and tablets on the same network connect to), and to load the user's
> own VST3 audio plug-ins from their plug-in folders. It does not install drivers, services or machine-wide registry entries,
> and writes only to the user's own AppData folders.

## What the Store package leaves out (and why)

The Store forbids packages that install kernel-mode or ASIO drivers or write to HKLM. `build-msix.sh` therefore excludes:

- `drivers/` (the AmF X1 virtual audio / ASIO driver installers);
- `AudioMixerASIO64.dll` (the ASIO driver, registered machine-wide by the desktop setup);
- any `*.inf`, `*.sys`, `*.cat`, `*.reg`.

The Store build runs on WASAPI and on any ASIO driver the user installed themselves (from the audio interface maker, or the
desktop setup from the website). Users who need the AmF X1 virtual devices install the desktop edition instead.

## How the app behaves when packaged

The console detects an MSIX install (`GetCurrentPackageFullName`, `bg::packaged()` in `native/amixos/src/Background.cpp`):

- **Software update**: checking, downloading and running the self-updater are off; the Store delivers updates
  (`updater.packaged` is true for the interface).
- **Start with Windows**: the HKCU Run key is not written; the manifest's `StartupTask` (`AmFX1Startup`, off by default) is
  switched by the user in Windows Settings > Apps > Startup.
- **Files**: settings, journal, logs, designs, downloads and caches already live in the user's AppData (`am::paths::dataDir()`
  and `QStandardPaths`); nothing is written to the install folder, which MSIX makes read-only. The program folder is only read
  (web interface files, helper programs, plug-in shell).

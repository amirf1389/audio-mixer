#!/usr/bin/env bash
# MICROSOFT STORE PACKAGE: turns the Windows package folder (scripts/build-console-windows.sh ->
# dist/windows/AmF-X1-<ver>-windows-x64/) into an UNSIGNED .msix for Partner Center (the Store signs it).
#
#   MSIX_IDENTITY_NAME=12345AMFBAudio.AmFX1 MSIX_PUBLISHER="CN=XXXXXXXX-XXXX-..." scripts/build-msix.sh
#
# Identity Name, Publisher and PublisherDisplayName come from Partner Center > Product identity. Without them the
# manifest carries clearly marked placeholders (fine for a local check, refused by the Store).
# Packs with makemsix (github.com/microsoft/msix-packaging, built with makelinux.sh --pack; set MAKEMSIX or put it on PATH).
# Without makemsix: the layout folder, a mapping file and pack-msix.cmd (MakeAppx on Windows) are zipped instead.
#
# LEFT OUT (the Store forbids kernel/ASIO driver installs and machine-wide registry writes): drivers/, the ASIO driver DLL
# (AudioMixerASIO64.dll, registered into HKLM by the desktop setup), *.inf/*.sys/*.cat/*.reg. The Store build uses WASAPI and any
# ASIO driver the user installed themselves. See docs/microsoft-store.md.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
VER="$(tr -d ' \n' < "$ROOT/native/amixos/version.txt")"
SRC="${1:-$ROOT/dist/windows/AmF-X1-$VER-windows-x64}"
OUTDIR="$ROOT/releases/windows"; OUT="$OUTDIR/AmF-X1-$VER-x64.msix"
WORK="$ROOT/dist/msix"; LAYOUT="$WORK/layout"
ID_NAME="${MSIX_IDENTITY_NAME:-PLACEHOLDER.FromPartnerCenter.AmFX1}"
PUBLISHER="${MSIX_PUBLISHER:-CN=PLACEHOLDER-FROM-PARTNER-CENTER}"
PUB_DISPLAY="${MSIX_PUBLISHER_DISPLAY_NAME:-AMFB Audio}"
ICON="$ROOT/native/amixos/icons/amfx1.png"
[ -d "$SRC" ] || { echo "no package folder at $SRC (run scripts/build-console-windows.sh first)"; exit 1; }
[[ "$VER" =~ ^[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+$ ]] || { echo "version $VER is not a four-part number"; exit 1; }
case "$ID_NAME$PUBLISHER" in *PLACEHOLDER*) echo "WARNING: Identity Name / Publisher are placeholders: set MSIX_IDENTITY_NAME and MSIX_PUBLISHER from Partner Center";; esac

mkdir -p "$WORK" "$OUTDIR"; [ -d "$LAYOUT" ] && find "$LAYOUT" -mindepth 1 -delete; mkdir -p "$LAYOUT/Assets"
# the app, without drivers and driver installers
tar -C "$SRC" --exclude='./drivers' --exclude='AudioMixerASIO*.dll' --exclude='*.inf' --exclude='*.sys' --exclude='*.cat' --exclude='*.reg' -cf - . |
  tar -C "$LAYOUT" -xf -

# tiles and logos from the app icon, at the scales the Store asks for
python3 - "$ICON" "$LAYOUT/Assets" <<'PY'
import sys
from PIL import Image
src, out = Image.open(sys.argv[1]).convert("RGBA"), sys.argv[2]
def put(name, w, h):
    canvas = Image.new("RGBA", (w, h), (0, 0, 0, 0)); s = min(w, h)
    if (w, h) != (s, s) or "44x44" not in name and "StoreLogo" not in name: s = int(min(w, h) * 0.8)   # tiles keep a margin
    icon = src.resize((s, s), Image.LANCZOS); canvas.alpha_composite(icon, ((w - s) // 2, (h - s) // 2)); canvas.save(f"{out}/{name}.png")
for sc in (100, 125, 150, 200, 400):
    f = sc / 100
    put(f"Square44x44Logo.scale-{sc}", round(44 * f), round(44 * f))
    put(f"Square150x150Logo.scale-{sc}", round(150 * f), round(150 * f))
    put(f"Wide310x150Logo.scale-{sc}", round(310 * f), round(150 * f))
    put(f"StoreLogo.scale-{sc}", round(50 * f), round(50 * f))
for t in (16, 24, 32, 48, 256):
    put(f"Square44x44Logo.targetsize-{t}", t, t); put(f"Square44x44Logo.targetsize-{t}_altform-unplated", t, t)
PY

cat > "$LAYOUT/AppxManifest.xml" <<EOF
<?xml version="1.0" encoding="utf-8"?>
<Package xmlns="http://schemas.microsoft.com/appx/manifest/foundation/windows10"
         xmlns:uap="http://schemas.microsoft.com/appx/manifest/uap/windows10"
         xmlns:desktop="http://schemas.microsoft.com/appx/manifest/desktop/windows10"
         xmlns:rescap="http://schemas.microsoft.com/appx/manifest/foundation/windows10/restrictedcapabilities"
         IgnorableNamespaces="uap desktop rescap">
  <!-- Identity Name / Publisher / PublisherDisplayName: copy them from Partner Center > Product identity -->
  <Identity Name="$ID_NAME" Publisher="$PUBLISHER" Version="$VER" ProcessorArchitecture="x64" />
  <Properties>
    <DisplayName>AmF X1</DisplayName>
    <PublisherDisplayName>$PUB_DISPLAY</PublisherDisplayName>
    <Logo>Assets\StoreLogo.png</Logo>
    <Description>AmF X1 audio mixing console</Description>
  </Properties>
  <Dependencies>
    <TargetDeviceFamily Name="Windows.Desktop" MinVersion="10.0.17763.0" MaxVersionTested="10.0.22621.0" />
  </Dependencies>
  <Resources>
    <Resource Language="en-us" />
  </Resources>
  <Applications>
    <Application Id="AmFX1" Executable="amixos.exe" EntryPoint="Windows.FullTrustApplication">
      <uap:VisualElements DisplayName="AmF X1" Description="AmF X1 audio mixing console" BackgroundColor="transparent"
                          Square150x150Logo="Assets\Square150x150Logo.png" Square44x44Logo="Assets\Square44x44Logo.png">
        <uap:DefaultTile Wide310x150Logo="Assets\Wide310x150Logo.png" />
      </uap:VisualElements>
      <Extensions>
        <!-- START WITH WINDOWS: replaces the HKCU Run key in the packaged app (off until the user turns it on) -->
        <desktop:Extension Category="windows.startupTask" Executable="amixos.exe" EntryPoint="Windows.FullTrustApplication">
          <desktop:StartupTask TaskId="AmFX1Startup" Enabled="false" DisplayName="AmF X1" />
        </desktop:Extension>
      </Extensions>
    </Application>
  </Applications>
  <Capabilities>
    <Capability Name="internetClient" />
    <Capability Name="privateNetworkClientServer" />
    <rescap:Capability Name="runFullTrust" />
    <DeviceCapability Name="microphone" />
  </Capabilities>
</Package>
EOF

# mapping file (MakeAppx /f) and the Windows pack command, kept beside the layout either way
MAP="$WORK/mapping.txt"
{ echo '[Files]'; ( cd "$LAYOUT" && find . -type f | sed 's|^\./||' | sort ) | while IFS= read -r f; do w="${f//\//\\}"; echo "\"layout\\$w\" \"$w\""; done; } > "$MAP"
cat > "$WORK/pack-msix.cmd" <<EOF
@echo off
rem Pack on Windows with the Windows SDK's MakeAppx (unsigned: the Store signs it). Run from this folder.
MakeAppx.exe pack /o /h SHA256 /f mapping.txt /p AmF-X1-$VER-x64.msix
EOF

# PRIVACY / SECRETS AUDIT of the layout (fails the build): no keys, secrets, personal data, user state, databases, VCS or debug files
audit_fail=0
BAD="$(cd "$LAYOUT" && find . \( -iname '*.pem' -o -iname '*.key' -o -iname '*.pfx' -o -iname '*.p12' -o -iname '*.jks' -o -iname '*.keystore' \
  -o -iname '*.snk' -o -iname '.env*' -o -iname '*.pdb' -o -iname '*.db' -o -iname '*.sqlite*' -o -iname '*.log' -o -iname '*journal*' \
  -o -iname '*seed*' -o -iname '*token*' -o -iname 'settings*.json' -o -iname '*.ini' -o -iname '*vendor*private*' -o -name '.git' -o -name 'CMakeCache.txt' \) -print)"
[ -z "$BAD" ] || { echo "AUDIT: forbidden files in the package:"; echo "$BAD"; audit_fail=1; }
# real PEM private keys (a header followed by base64; Qt's TLS DLLs only hold the bare header strings), well-known token shapes
HITS="$( { grep -rlaP 'PRIVATE KEY-----\r?\n[A-Za-z0-9+/=]{40}' "$LAYOUT"   # all files, binaries too
  grep -rlIP 'AKIA[0-9A-Z]{16}|gh[pousr]_[A-Za-z0-9]{36}|xox[abpr]-[A-Za-z0-9-]{10,}|sk_live_[A-Za-z0-9]{16}|AIza[0-9A-Za-z_-]{35}' "$LAYOUT"; } || true)"   # text only: random bytes in node.exe match AKIA...
[ -z "$HITS" ] || { echo "AUDIT: secret-shaped content:"; echo "$HITS"; audit_fail=1; }
# personal e-mail addresses outside third-party licence texts
MAILS="$(grep -rlIE '[A-Za-z0-9._%+-]+@(gmail|hotmail|yahoo|outlook|icloud|live)\.[a-z.]+' "$LAYOUT" | grep -v '/licenses/' || true)"
[ -z "$MAILS" ] || { echo "AUDIT: personal e-mail addresses:"; echo "$MAILS"; audit_fail=1; }
mkdir -p "$WORK/audit"; node "$ROOT/scripts/security/audit.js" --root "$LAYOUT" --only secrets --offline --no-tools --fail-on MEDIUM --quiet --out "$WORK/audit/security-report.md" || audit_fail=1
[ "$audit_fail" = 0 ] || { echo "AUDIT FAILED: nothing packed"; exit 1; }
echo "audit: no keys, secrets, personal data or user state in the layout"

MAKEMSIX="${MAKEMSIX:-$(command -v makemsix || true)}"
if [ -n "$MAKEMSIX" ] && [ -x "$MAKEMSIX" ]; then
  rm -f "$OUT"; "$MAKEMSIX" pack -d "$LAYOUT" -p "$OUT"
  # round trip: unpack (parses and validates the manifest and block map against the schema)
  CHK="$WORK/unpack-check"; [ -d "$CHK" ] && find "$CHK" -mindepth 1 -delete; mkdir -p "$CHK"
  "$MAKEMSIX" unpack -p "$OUT" -d "$CHK" -ss
  cmp -s "$CHK/AppxManifest.xml" "$LAYOUT/AppxManifest.xml" && echo "unpack round trip: manifest valid and identical"
  echo "built $OUT ($(du -h "$OUT" | cut -f1), unsigned)"
else
  Z="$OUTDIR/AmF-X1-$VER-x64-msix-layout.zip"; rm -f "$Z"; ( cd "$WORK" && zip -qr "$Z" layout mapping.txt pack-msix.cmd )
  echo "makemsix not found: wrote $Z (run pack-msix.cmd on Windows)"
fi

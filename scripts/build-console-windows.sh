#!/bin/sh
# Cross-builds the AmF X1 console (native/amixos, Qt 6 Quick) for Windows x64 on Linux with mingw-w64 and the official Qt MinGW kit of the
# SAME version as the host's Qt 6 (the host's moc / rcc / qmlcachegen / qmltyperegistrar run here, the kit's libraries are linked), then
# assembles a runnable folder: amixos.exe, "AmF X1 Network.exe" (the service, built as amixos-daemon), "AmF X1.exe" + the ASIO DLL (from releases/windows or build-native.js), the
# Qt DLLs, the platform / image plugins, the QML modules and qt.conf. Usage: scripts/build-console-windows.sh [kit-dir] [out-dir]
#   kit-dir  the extracted win64_mingw kit (.../6.4.2/mingw_64). Without it, the script downloads the qtbase / qtdeclarative / qttools
#            7z archives of the host's Qt version from download.qt.io into ./.qt-win and extracts them (needs 7z and curl).
# The result is unsigned: sign amixos.exe, "AmF X1.exe", "AmF X1 Network.exe" and the DLL with scripts/sign-windows.js before release.
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"; VER="$(tr -d ' \n' < "$ROOT/native/amixos/version.txt")"
# the host's Qt version: Qt6ConfigVersion.cmake on older packages, Qt6ConfigVersionImpl.cmake on newer ones, else Qt's own qtpaths / qmake
QTCM=/usr/lib/x86_64-linux-gnu/cmake/Qt6
HOSTQT="$(grep -ho 'PACKAGE_VERSION "[0-9.]*"' "$QTCM/Qt6ConfigVersion.cmake" "$QTCM/Qt6ConfigVersionImpl.cmake" 2>/dev/null | head -1 | tr -dc '0-9.')"
[ -n "$HOSTQT" ] || HOSTQT="$( (qtpaths6 --qt-query QT_VERSION || qmake6 -query QT_VERSION || qmake -query QT_VERSION) 2>/dev/null | head -1 | tr -dc '0-9.')"
[ -n "$HOSTQT" ] || { echo "the host's Qt 6 version was not found (install qt6-base-dev, or pass the Windows kit's path as the first argument)"; exit 1; }
KIT="${1:-}"; OUT="${2:-$ROOT/dist/windows}"; NAME="AmF-X1-$VER-windows-x64"
command -v x86_64-w64-mingw32-g++ >/dev/null || { echo "mingw-w64 is needed (apt install mingw-w64)"; exit 1; }
if [ -z "$KIT" ]; then
  command -v 7z >/dev/null || { echo "7z is needed to unpack the Qt kit (apt install p7zip-full)"; exit 1; }
  Q="$(echo "$HOSTQT" | tr -d .)"; DL="$ROOT/.qt-win"; mkdir -p "$DL"; cd "$DL"
  BASE="https://download.qt.io/online/qtsdkrepository/windows_x86/desktop/qt6_$Q/qt.qt6.$Q.win64_mingw/"
  REL="$(curl -s "https://download.qt.io/online/qtsdkrepository/windows_x86/desktop/qt6_$Q/Updates.xml" | tr -d '\n' | sed 's/.*<Name>qt.qt6.'"$Q"'.win64_mingw<\/Name>.*<Version>\([^<]*\)<\/Version>.*/\1/' | sed 's/<.*//')"
  [ -n "$REL" ] || { echo "no win64_mingw package for Qt $HOSTQT on download.qt.io"; exit 1; }
  for a in qtbase qtdeclarative qttools; do [ -f "$a.7z" ] || curl -sL -o "$a.7z" "$BASE$REL$a-Windows-Windows_10_21H2-Mingw-Windows-Windows_10_21H2-X86_64.7z"; 7z x -y -bso0 -bsp0 "$a.7z"; done
  KIT="$DL/$HOSTQT/mingw_64"; cd "$ROOT"
fi
[ -f "$KIT/lib/cmake/Qt6/Qt6Config.cmake" ] || { echo "not a Qt kit: $KIT"; exit 1; }
# the kit's build tools are Windows programs: point them at the host's (same version)
for t in moc rcc uic qmlcachegen qmltyperegistrar qmlimportscanner cmake_automoc_parser qlalr tracegen qvkgen; do h="/usr/lib/qt6/libexec/$t"; [ -x "$h" ] && { [ -L "$KIT/bin/$t.exe" ] || mv -f "$KIT/bin/$t.exe" "$KIT/bin/$t.exe.win"; ln -sf "$h" "$KIT/bin/$t.exe"; }; done
B="$ROOT/build-win"; cmake -S "$ROOT/native/amixos" -B "$B" -DCMAKE_TOOLCHAIN_FILE="$ROOT/install/windows/mingw-cross.cmake" -DQT_WIN_KIT="$KIT" -DQT_HOST_PATH=/usr -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build "$B" -j"$(nproc)" --target amixos amixos-daemon amfb-hybrid amfb_network amfb_hybrid >/dev/null
D="$OUT/$NAME"; rm -rf "$D"; mkdir -p "$D/platforms" "$D/imageformats" "$D/qml"
cp "$B/amixos.exe" "$D/"; cp "$(find "$B" -name amixos-daemon.exe | head -1)" "$D/AmF X1 Network.exe"; cp "$(find "$B" -name amfb-hybrid.exe | head -1)" "$D/AmF X1 Hybrid.exe"
cp -r "$(find "$B" -maxdepth 2 -type d -name "AMFB Plugin Shell.vst3" | head -1)" "$D/"   # the PLUG-IN BUILDER exports copies of this shell (one VST3 that becomes each design)
cp "$(find "$B" -maxdepth 1 -name "AmF X1 Plugin Builder.exe" | head -1)" "$D/"
cp "$(find "$B" -maxdepth 1 -name "AmF X1 Diagnostics.exe" | head -1)" "$D/"   # DIAGNOSTICS: its own program (speakers, headphones, microphones, drivers, report)   # the PLUG-IN BUILDER: its own program (TOOLS opens it from the console)
cp "$(find "$B" -name "AmF X1 Network.dll" | head -1)" "$(find "$B" -name "AmF X1 Hybrid.dll" | head -1)" "$D/"   # the extensions the console loads into its own process   # the service program under the product's name
cp "$(find "$B" -name "amix_soxr.dll" | head -1)" "$D/"   # libsoxr (LGPL 2.1): its own DLL, loaded at run time, replaceable; the r8brain path works without it
cp "$ROOT/management/windows/AmF X1 My Account.url" "$D/"   # MY ACCOUNT: the customer page of the licence server (management/customer); the vendor MANAGER never ships
# our own programs must not lean on the C++ runtime DLLs of the Qt kit (an older libstdc++-6.dll lacks symbols the compiler here uses)
for p in amixos.exe "AmF X1 Network.exe" "AmF X1 Hybrid.exe" "AmF X1 Network.dll" "AmF X1 Hybrid.dll"; do if x86_64-w64-mingw32-objdump -p "$D/$p" | grep -qiE "DLL Name: (libstdc.*|libgcc_s.*|libwinpthread.*)"; then echo "$p needs a compiler runtime DLL: link it statically"; exit 1; fi; done
if [ -f "$ROOT/releases/windows/AmF X1.exe" ]; then cp "$ROOT/releases/windows/AmF X1.exe" "$ROOT/releases/windows/AudioMixerASIO64.dll" "$D/"; else node "$ROOT/scripts/build-native.js" --out "$D" >/dev/null; rm -f "$D/AmF X1 (32-bit).exe"; fi
# THE 32-BIT ASIO DRIVER (for 32-bit host programs): built here from source with i686-w64-mingw32 next to the 64-bit one. The setup registers
# it with Windows' 32-bit regsvr32 (SysWOW64), which writes the 32-bit registry view; no 32-bit helper program is shipped for that.
command -v i686-w64-mingw32-g++ >/dev/null || { echo "i686-w64-mingw32-g++ is needed for the 32-bit ASIO driver (apt install g++-mingw-w64-i686)"; exit 1; }
node -e 'require(process.argv[1]).buildAsio("x86", process.argv[2])' "$ROOT/scripts/build-drivers.js" "$D" >/dev/null
for p in AudioMixerASIO64.dll AudioMixerASIO32.dll; do if x86_64-w64-mingw32-objdump -p "$D/$p" | grep -qiE "DLL Name: (libstdc.*|libgcc_s.*|libwinpthread.*)"; then echo "$p needs a compiler runtime DLL: link it statically"; exit 1; fi; done
file "$D/AudioMixerASIO32.dll" | grep -q "PE32 executable (DLL).*Intel 80386" || { echo "AudioMixerASIO32.dll is not a 32-bit (i386) DLL"; exit 1; }
for l in Qt6Core Qt6Gui Qt6Widgets Qt6Network Qt6OpenGL Qt6Qml Qt6QmlModels Qt6QmlWorkerScript Qt6Quick Qt6QuickControls2 Qt6QuickControls2Impl Qt6QuickTemplates2 Qt6QuickLayouts Qt6QuickShapes libgcc_s_seh-1 libstdc++-6 libwinpthread-1; do cp "$KIT/bin/$l.dll" "$D/"; done
for l in Qt6QuickControls2Fusion Qt6QuickControls2FusionStyleImpl; do [ -f "$KIT/bin/$l.dll" ] && cp "$KIT/bin/$l.dll" "$D/"; done   # the Fusion style (the console uses it: palette-driven, dark)
# FFmpeg (LGPL 2.1+, decoders only, built by scripts/third_party/build-ffmpeg.sh windows): separate DLLs next to the program, opened at run
# time (the program starts without them and anyone may replace them), with the licence and where the exact source is
FF="$ROOT/third_party/ffmpeg-windows"; if [ -f "$FF/bin/avformat-61.dll" ]; then cp "$FF"/bin/avutil-59.dll "$FF"/bin/swresample-5.dll "$FF"/bin/avcodec-61.dll "$FF"/bin/avformat-61.dll "$D/"
  mkdir -p "$D/licenses"; cp "$FF/FFMPEG-LICENSE.txt" "$FF/FFMPEG-SOURCE.txt" "$FF/FFMPEG-BUILD.txt" "$D/licenses/" 2>/dev/null || true; echo "FFmpeg decoders: included"; else echo "FFmpeg decoders: not built (scripts/third_party/build-ffmpeg.sh windows), Windows' own decoders only"; fi
# MPEG-H (Fraunhofer FDK, needs MPEG-H patent licences for commercial use): only when asked for, once those licences are held
if [ "${AMIX_WITH_MPEGH:-0}" = 1 ] && [ -f "$ROOT/third_party/mpegh-windows/amfb_mpegh.dll" ]; then cp "$ROOT/third_party/mpegh-windows/amfb_mpegh.dll" "$D/"
  mkdir -p "$D/licenses"; cp "$ROOT/third_party/mpegh-windows/MPEGH-LICENSE.txt" "$ROOT/third_party/mpegh-windows/MPEGH-SOURCE.txt" "$D/licenses/"; echo "MPEG-H extension: included"; else echo "MPEG-H extension: left out"; fi
cp "$KIT/plugins/platforms/qwindows.dll" "$D/platforms/"; mkdir -p "$D/tls"; cp "$KIT/plugins/tls/qschannelbackend.dll" "$D/tls/" 2>/dev/null || true;   # TLS for the update check (Windows Schannel)
 cp "$KIT/plugins/imageformats/"*.dll "$D/imageformats/"
cp -r "$KIT/qml/QtQml" "$KIT/qml/QtQuick" "$KIT/qml/Qt" "$D/qml/" 2>/dev/null || true
rm -rf "$D/qml/QtQuick/Controls/Imagine" "$D/qml/QtQuick/Controls/Material" "$D/qml/QtQuick/Controls/Universal" "$D/qml/QtQuick/Controls/Windows" "$D/qml/QtQuick/Controls/designer" "$D/qml/QtQuick/Particles"
# QML modules the console never imports and whose Qt libraries are not shipped (they could not load anyway): left out of the package
rm -rf "$D/qml/Qt/test" "$D/qml/Qt/labs" "$D/qml/QtQuick/Dialogs" "$D/qml/QtQuick/LocalStorage" "$D/qml/QtQml/XmlListModel"
printf '[Paths]\nPrefix = .\nPlugins = .\nQml2Imports = qml\n' > "$D/qt.conf"; cp "$ROOT/LICENSE" "$D/" 2>/dev/null || true
# the driver folder and its documents travel with the program: the Steinberg ASIO licence / usage guidelines / change list, third-party licences
mkdir -p "$D/drivers/asio" "$D/licenses"
for f in "LICENSE.txt" "Steinberg ASIO Licensing Agreement.pdf" "Steinberg ASIO Usage Guidelines.pdf" "changes.txt"; do [ -f "$ROOT/$f" ] && cp "$ROOT/$f" "$D/drivers/asio/"; done
cp "$ROOT/drivers/asio/sdk/README.txt" "$D/drivers/asio/SDK-HEADERS.txt" 2>/dev/null || true; cp "$ROOT/drivers/vst3/sdk/LICENSE.txt" "$D/licenses/VST3-SDK-LICENSE.txt" 2>/dev/null || true
cp "$ROOT"/licenses/* "$D/licenses/" 2>/dev/null || true; cp "$ROOT/Additional Terms & Conditions" "$D/licenses/Additional Terms and Conditions.txt" 2>/dev/null || true
# WEB STACK (optional; OFF by default in the console, NETWORK page): the official Node.js win-x64 and nginx for Windows, verified, under web\node and
# web\nginx; the browser remote page under web\remote; the Node bridge under web\bridge; the nginx template and start / stop helpers under web\.
# Downloads are cached in AMIX_WEBDEPS (default .webdeps-win) and checked against pinned SHA-256 sums (Node: as listed in SHASUMS256.txt of the
# release; nginx: the sum of the release zip, and its .asc signature with gpg when AMIX_WEBDEPS/gnupg holds the nginx signing keys). Nothing in them is run here.
NODE_V=22.11.0; NODE_SHA=905373a059aecaf7f48c1ce10ffbd5334457ca00f678747f19db5ea7d256c236; NGINX_V=1.26.2; NGINX_SHA=942638cc31c836fe429fde22e4c46e497f89d3c2bbce46cd2c8800854fd71409
WD="${AMIX_WEBDEPS:-$ROOT/.webdeps-win}"; mkdir -p "$WD"
[ -f "$WD/node-v$NODE_V-win-x64.zip" ] || curl -sSfL -o "$WD/node-v$NODE_V-win-x64.zip" "https://nodejs.org/dist/v$NODE_V/node-v$NODE_V-win-x64.zip"
[ -f "$WD/SHASUMS256.txt" ] || curl -sSfL -o "$WD/SHASUMS256.txt" "https://nodejs.org/dist/v$NODE_V/SHASUMS256.txt"
[ -f "$WD/nginx-$NGINX_V.zip" ] || curl -sSfL -o "$WD/nginx-$NGINX_V.zip" "https://nginx.org/download/nginx-$NGINX_V.zip"
grep -q "^$NODE_SHA  node-v$NODE_V-win-x64.zip\$" "$WD/SHASUMS256.txt" || { echo "Node.js: SHASUMS256.txt does not list the pinned sum"; exit 1; }
echo "$NODE_SHA  $WD/node-v$NODE_V-win-x64.zip" | sha256sum -c --quiet || { echo "Node.js zip: wrong SHA-256"; exit 1; }
echo "$NGINX_SHA  $WD/nginx-$NGINX_V.zip" | sha256sum -c --quiet || { echo "nginx zip: wrong SHA-256"; exit 1; }
if [ -f "$WD/nginx-$NGINX_V.zip.asc" ] && [ -d "$WD/gnupg" ] && command -v gpg >/dev/null; then gpg --homedir "$WD/gnupg" -q --verify "$WD/nginx-$NGINX_V.zip.asc" "$WD/nginx-$NGINX_V.zip" 2>/dev/null || { echo "nginx zip: bad signature"; exit 1; }; echo "nginx: signature good"; fi
X="$(mktemp -d)"; unzip -q "$WD/node-v$NODE_V-win-x64.zip" "node-v$NODE_V-win-x64/node.exe" "node-v$NODE_V-win-x64/LICENSE" -d "$X"; unzip -q "$WD/nginx-$NGINX_V.zip" -d "$X"
mkdir -p "$D/web/node" "$D/web/nginx/conf" "$D/web/remote" "$D/web/bridge/bridge/sim" "$D/web/bridge/engine/node" "$D/licenses"
cp "$X/node-v$NODE_V-win-x64/node.exe" "$D/web/node/"; cp "$X/node-v$NODE_V-win-x64/LICENSE" "$D/licenses/NODEJS-LICENSE.txt"
N="$X/nginx-$NGINX_V"; cp "$N/nginx.exe" "$D/web/nginx/"; cp "$N/conf/mime.types" "$D/web/nginx/conf/"
cp "$N/docs/LICENSE" "$D/licenses/NGINX-LICENSE.txt"; cp "$N/docs/zlib.LICENSE" "$D/licenses/NGINX-zlib-LICENSE.txt"; cp "$N/docs/PCRE.LICENCE" "$D/licenses/NGINX-PCRE-LICENCE.txt"; cp "$N/docs/OpenSSL.LICENSE" "$D/licenses/NGINX-OpenSSL-LICENSE.txt"
rm -rf "$X"
printf 'Node.js v%s win-x64 (node.exe only)  sha256 %s  https://nodejs.org/dist/v%s/\nnginx %s for Windows  sha256 %s  https://nginx.org/download/\n' "$NODE_V" "$NODE_SHA" "$NODE_V" "$NGINX_V" "$NGINX_SHA" "$NGINX_SHA" > "$D/licenses/WEB-STACK-SOURCES.txt"
cp "$ROOT/install/windows/web/nginx.conf.in" "$D/web/nginx/conf/"; cp "$ROOT/install/windows/web/start-web-stack.cmd" "$ROOT/install/windows/web/stop-web-stack.cmd" "$D/web/"
cp "$ROOT/engine/cpp/app/daemon/remote_page.html" "$D/web/remote/index.html"
for f in "$ROOT"/bridge/*.js "$ROOT/bridge/license-public.json"; do case "$(basename "$f")" in test.js|audit.js|mail.js|flexasio.js) ;; *) cp "$f" "$D/web/bridge/bridge/";; esac; done
cp "$ROOT"/bridge/sim/*.js "$D/web/bridge/bridge/sim/"; cp "$ROOT/engine/node/index.js" "$D/web/bridge/engine/node/"; cp "$ROOT/package.json" "$D/web/bridge/"
# PACKAGE CHECK: the build fails when the network service, its extension or the web remote is not in the package
for f in "AmF X1 Network.exe" "AmF X1 Network.dll" "AmF X1.exe" AudioMixerASIO64.dll AudioMixerASIO32.dll amixos.exe Qt6Core.dll Qt6Network.dll platforms/qwindows.dll web/remote/index.html web/node/node.exe web/nginx/nginx.exe web/nginx/conf/mime.types web/nginx/conf/nginx.conf.in web/start-web-stack.cmd web/stop-web-stack.cmd web/bridge/bridge/server.js web/bridge/engine/node/index.js web/bridge/package.json licenses/NODEJS-LICENSE.txt licenses/NGINX-LICENSE.txt; do
  [ -s "$D/$f" ] || { echo "PACKAGE CHECK: $f is missing from $D"; exit 1; }; done
grep -q '/api/' "$D/web/remote/index.html" || { echo "PACKAGE CHECK: web/remote/index.html is not the browser remote"; exit 1; }
x86_64-w64-mingw32-objdump -p "$D/AmF X1 Network.exe" | grep -qiE "DLL Name: (Qt6Network|ws2_32)\.dll" || { echo "PACKAGE CHECK: AmF X1 Network.exe has no network imports"; exit 1; }
x86_64-w64-mingw32-objdump -p "$D/AmF X1 Network.exe" | sed -n 's/.*DLL Name: //p' | while read -r l; do case "$l" in [Qq]t6*|amix_*|av*|sw*) [ -f "$D/$l" ] || { echo "PACKAGE CHECK: AmF X1 Network.exe needs $l, not in the package"; exit 1; };; esac; done
NODE_LIST="$(node "$ROOT/scripts/require-closure.js" "$ROOT/bridge/server.js")" || exit 1
for f in $NODE_LIST; do [ -f "$D/web/bridge/$f" ] || { echo "PACKAGE CHECK: the bridge needs $f, not in web/bridge"; exit 1; }; done
echo "package check: network service, extension, web remote and web stack present"
cat > "$D/README.txt" <<TXT
AmF X1 by AMFB Audio $VER - Windows x64
  amixos.exe            the console (Qt 6 Quick): start this
  AmF X1.exe            the engine command line (--list-devices, --backend asio|wasapi|dsound, --update, --install-driver)
  AmF X1 Diagnostics.exe  the system check: speakers, headphones, microphones, ASIO drivers, a report
  AmF X1 Network.exe    the engine as a local HTTP / WebSocket service for the Android remote (binds 127.0.0.1)
  AudioMixerASIO64.dll  the ASIO driver: register from an administrator prompt with  "AmF X1.exe" --install-driver
  AudioMixerASIO32.dll  the 32-bit ASIO driver (32-bit host programs): register as administrator with  %WINDIR%\\SysWOW64\\regsvr32.exe AudioMixerASIO32.dll
  drivers\\asio\\         the Steinberg ASIO licence, usage guidelines and change list
  licenses\\              third-party licences and the additional terms
  web\\                   the WEB STACK (off unless switched on): web\\nginx (nginx for Windows), web\\node (Node.js), web\\bridge, web\\remote, start-web-stack.cmd / stop-web-stack.cmd
  Qt6*.dll, platforms\\, imageformats\\, qml\\   the Qt $HOSTQT runtime (MinGW build) the console needs; keep them next to amixos.exe
Data goes to %APPDATA%\\AMFB Audio\\AmF X1, cache to %LOCALAPPDATA%\\AMFB Audio\\AmF X1 (docs/install-layout.md). Unsigned: sign with scripts/sign-windows.js.
TXT
# CODE SIGNING (only with YOUR certificate): with AMFX1_SIGN_PFX set, every program and library of ours is signed before it is packed.
# Windows SmartScreen ("Windows protected your PC") and Defender judge an unsigned program by reputation it does not have; a signature
# from a certificate authority is the way that warning goes away. No certificate is made here and nothing is switched off on the PC.
if [ -n "${AMFX1_SIGN_PFX:-}" ]; then
  echo "== signing with the certificate in AMFX1_SIGN_PFX"
  ( cd "$OUT/$NAME" && for f in amixos.exe "AmF X1.exe" "AmF X1 Network.exe" "AmF X1 Hybrid.exe" "AmF X1 Diagnostics.exe" "AmF X1 Plugin Builder.exe" "AmF X1 Network.dll" "AmF X1 Hybrid.dll" amix_soxr.dll AudioMixerASIO64.dll AudioMixerASIO32.dll; do [ -f "$f" ] && node "$ROOT/scripts/sign-windows.js" "$f"; done )
else
  echo "== NOT SIGNED: no AMFX1_SIGN_PFX. Windows will show its protection screen for this build until it is signed (docs/windows-defender.md)."
fi
(cd "$OUT" && rm -f "$NAME.zip" && zip -q -r -9 "$NAME.zip" "$NAME" && sha256sum "$NAME.zip" > "$NAME.zip.sha256" && ls -la "$NAME.zip" && cat "$NAME.zip.sha256")

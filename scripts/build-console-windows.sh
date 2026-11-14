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
HOSTQT="$(grep -ho 'PACKAGE_VERSION "[0-9.]*"' /usr/lib/x86_64-linux-gnu/cmake/Qt6/Qt6ConfigVersion.cmake | head -1 | tr -dc '0-9.')"
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
cp "$(find "$B" -maxdepth 1 -name "AmF X1 Plugin Builder.exe" | head -1)" "$D/"   # the PLUG-IN BUILDER: its own program (TOOLS opens it from the console)
cp "$(find "$B" -name "AmF X1 Network.dll" | head -1)" "$(find "$B" -name "AmF X1 Hybrid.dll" | head -1)" "$D/"   # the extensions the console loads into its own process   # the service program under the product's name
# our own programs must not lean on the C++ runtime DLLs of the Qt kit (an older libstdc++-6.dll lacks symbols the compiler here uses)
for p in amixos.exe "AmF X1 Network.exe" "AmF X1 Hybrid.exe" "AmF X1 Network.dll" "AmF X1 Hybrid.dll"; do if x86_64-w64-mingw32-objdump -p "$D/$p" | grep -qiE "DLL Name: (libstdc.*|libgcc_s.*|libwinpthread.*)"; then echo "$p needs a compiler runtime DLL: link it statically"; exit 1; fi; done
if [ -f "$ROOT/releases/windows/AmF X1.exe" ]; then cp "$ROOT/releases/windows/AmF X1.exe" "$ROOT/releases/windows/AudioMixerASIO64.dll" "$D/"; else node "$ROOT/scripts/build-native.js" --out "$D" >/dev/null; rm -f "$D/AmF X1 (32-bit).exe" "$D/AudioMixerASIO32.dll"; fi
for l in Qt6Core Qt6Gui Qt6Network Qt6OpenGL Qt6Qml Qt6QmlModels Qt6QmlWorkerScript Qt6Quick Qt6QuickControls2 Qt6QuickControls2Impl Qt6QuickTemplates2 Qt6QuickLayouts Qt6QuickShapes libgcc_s_seh-1 libstdc++-6 libwinpthread-1; do cp "$KIT/bin/$l.dll" "$D/"; done
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
printf '[Paths]\nPrefix = .\nPlugins = .\nQml2Imports = qml\n' > "$D/qt.conf"; cp "$ROOT/LICENSE" "$D/" 2>/dev/null || true
# the driver folder and its documents travel with the program: the 32-bit ASIO DLL, the Steinberg ASIO licence / usage guidelines / change list, third-party licences
mkdir -p "$D/drivers/asio" "$D/licenses"
[ -f "$ROOT/releases/windows/AudioMixerASIO32.dll" ] && cp "$ROOT/releases/windows/AudioMixerASIO32.dll" "$D/drivers/asio/"
for f in "LICENSE.txt" "Steinberg ASIO Licensing Agreement.pdf" "Steinberg ASIO Usage Guidelines.pdf" "changes.txt"; do [ -f "$ROOT/$f" ] && cp "$ROOT/$f" "$D/drivers/asio/"; done
cp "$ROOT/drivers/asio/sdk/README.txt" "$D/drivers/asio/SDK-HEADERS.txt" 2>/dev/null || true; cp "$ROOT/drivers/vst3/sdk/LICENSE.txt" "$D/licenses/VST3-SDK-LICENSE.txt" 2>/dev/null || true
cp "$ROOT"/licenses/* "$D/licenses/" 2>/dev/null || true; cp "$ROOT/Additional Terms & Conditions" "$D/licenses/Additional Terms and Conditions.txt" 2>/dev/null || true
cat > "$D/README.txt" <<TXT
AmF X1 by AMFB Audio $VER - Windows x64
  amixos.exe            the console (Qt 6 Quick): start this
  AmF X1.exe            the engine command line (--list-devices, --backend asio|wasapi|dsound, --update, --install-driver)
  AmF X1 Network.exe    the engine as a local HTTP / WebSocket service for the Android remote (binds 127.0.0.1)
  AudioMixerASIO64.dll  the ASIO driver: register from an administrator prompt with  "AmF X1.exe" --install-driver
  drivers\\asio\\         the 32-bit ASIO driver DLL and the Steinberg ASIO licence, usage guidelines and change list
  licenses\\              third-party licences and the additional terms
  Qt6*.dll, platforms\\, imageformats\\, qml\\   the Qt $HOSTQT runtime (MinGW build) the console needs; keep them next to amixos.exe
Data goes to %APPDATA%\\AMFB Audio\\AmF X1, cache to %LOCALAPPDATA%\\AMFB Audio\\AmF X1 (docs/install-layout.md). Unsigned: sign with scripts/sign-windows.js.
TXT
(cd "$OUT" && rm -f "$NAME.zip" && zip -q -r -9 "$NAME.zip" "$NAME" && sha256sum "$NAME.zip" > "$NAME.zip.sha256" && ls -la "$NAME.zip" && cat "$NAME.zip.sha256")

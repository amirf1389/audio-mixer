#!/bin/sh
# Builds the engine, the C SDK, the daemon and the Perl module and packs them as dist/amixos-<version>-linux-<arch>.tar.gz:
#   bin/amixos-daemon  lib/libamix.so*  include/amix.h  perl/Audio-AmixOS (source, builds against lib/)  sdk/wasm/  docs/sdk.md
# The Qt console is not in the tarball (it needs the Qt runtime of the target; see native/amixos/README). Usage: scripts/package-linux.sh [build-dir]
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"; BUILD="${1:-$ROOT/build}"; VER="$(tr -d ' \n' < "$ROOT/native/amixos/version.txt")"; ARCH="$(uname -m)"
NAME="amixos-$VER-linux-$ARCH"; OUT="$ROOT/dist/$NAME"
cmake -S "$ROOT/engine/cpp" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build "$BUILD" -j"$(nproc)" --target amix amixos-daemon sdk_c_test >/dev/null
(cd "$BUILD" && ctest -R sdk_c_test --output-on-failure >/dev/null)
rm -rf "$OUT"; mkdir -p "$OUT/bin" "$OUT/lib" "$OUT/include" "$OUT/perl" "$OUT/sdk/wasm" "$OUT/docs" "$OUT/data" "$OUT/cache"   # data/ and cache/: the program saves next to itself when these are writable (install/layout.json)
cp "$BUILD/amixos-daemon" "$OUT/bin/"; cp -P "$BUILD"/libamix.so* "$OUT/lib/"; cp "$ROOT/sdk/c/include/amix.h" "$OUT/include/"
cp -r "$ROOT/bindings/perl/Audio-AmixOS" "$OUT/perl/"; rm -rf "$OUT/perl/Audio-AmixOS/blib"
cp "$ROOT/sdk/wasm/amix_dsp.c" "$ROOT/sdk/wasm/amix.mjs" "$ROOT/sdk/wasm/build.sh" "$ROOT/sdk/wasm/test.mjs" "$OUT/sdk/wasm/"; [ -f "$ROOT/sdk/wasm/amix_dsp.wasm" ] && cp "$ROOT/sdk/wasm/amix_dsp.wasm" "$OUT/sdk/wasm/"
cp "$ROOT/docs/sdk.md" "$OUT/docs/" 2>/dev/null || true; cp "$ROOT/LICENSE" "$OUT/" 2>/dev/null || true
cat > "$OUT/README.txt" <<TXT
A mix OS $VER for Linux ($ARCH)
  bin/amixos-daemon       the engine as a local HTTP / WebSocket service (binds 127.0.0.1; --allow-lan is unencrypted, use it only behind your own TLS proxy)
  lib/libamix.so          the C SDK (include/amix.h): offline mixer, live runtime (ALSA / null), meters, devices, licence
  perl/Audio-AmixOS       Perl XS module: AMIX_INC=../../include AMIX_LIB=../../lib perl Makefile.PL && make && make test
  sdk/wasm                the DSP core for browsers (amix_dsp.wasm + amix.mjs)
Install (program folder, the data stays inside it): sudo mkdir -p /opt/amf-x1 && sudo cp -r . /opt/amf-x1 && sudo chown -R $USER /opt/amf-x1/data /opt/amf-x1/cache
  then /opt/amf-x1/bin/amixos-daemon; data in /opt/amf-x1/data, cache in /opt/amf-x1/cache (install/layout.json; AMIX_DATA_DIR overrides)
Or system-wide libs: sudo cp lib/libamix.so* /usr/local/lib && sudo ldconfig && sudo cp include/amix.h /usr/local/include && sudo cp bin/amixos-daemon /usr/local/bin
The Qt console (amixos) is built from native/amixos with your distribution's Qt 6 (see the repository README).
TXT
mkdir -p "$ROOT/dist"; (cd "$ROOT/dist" && tar -czf "$NAME.tar.gz" "$NAME" && sha256sum "$NAME.tar.gz" > "$NAME.tar.gz.sha256")
rm -rf "$OUT"; ls -la "$ROOT/dist/$NAME.tar.gz"; cat "$ROOT/dist/$NAME.tar.gz.sha256"

#!/bin/sh
# Builds amix_dsp.wasm with plain clang (no emscripten needed): clang >= 15 with the wasm32 target and wasm-ld (package lld).
set -e
cd "$(dirname "$0")"
clang --target=wasm32 -O2 -nostdlib -fno-builtin -ffreestanding -Wall -Wextra -Werror -Wl,--no-entry -Wl,--export=__heap_base -Wl,--allow-undefined -o amix_dsp.wasm amix_dsp.c
ls -la amix_dsp.wasm

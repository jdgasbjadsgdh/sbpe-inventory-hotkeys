#!/bin/bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd -P)"
cd "$ROOT"
if [ "$(uname -s)" != Darwin ]; then echo 'Rebuilding here requires macOS and Apple Command Line Tools.'; exit 1; fi
if ! xcode-select -p >/dev/null 2>&1; then
  printf '%s\n' 'The prebuilt dylib does not require a rebuild.' \
    'Only if you want to rebuild from source: install Apple tools with xcode-select --install.'
  exit 1
fi
if command -v python3 >/dev/null 2>&1; then python3 tests/generate_loot_data.py; fi
xcrun clang -arch x86_64 -mmacosx-version-min=10.14 -march=x86-64 -std=c11 -dynamiclib -O2 \
  -Wl,-install_name,@rpath/sbpe-mac.dylib -o sbpe-mac-new.dylib src/sbpe.c src/websocket_transport.m -fobjc-arc -framework Foundation -lz -lpthread
mv sbpe-mac-new.dylib sbpe-mac.dylib
printf '%s\n' 'Build complete. Launch using RUN.command.'

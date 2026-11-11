#!/usr/bin/env bash
# Cross-compile the 32-bit Windows DLL from Linux using zig (pip: ziglang, installed into .tools/).
# One-time setup:  pip install --target .tools ziglang
set -euo pipefail
cd "$(dirname "$0")"
export PYTHONPATH="$PWD/.tools${PYTHONPATH:+:$PYTHONPATH}"
mkdir -p build .out
ZIG="python3 -m ziglang cc -target x86-windows-gnu -O2 -Wall"
$ZIG -shared -o .out/qol_ui.dll src/qol_ui.c src/filter.c src/itemdb.c src/party.c -luser32
cp .out/qol_ui.dll build/qol_ui.dll
cp src/qol_ui.ini build/qol_ui.ini
echo "built: build/qol_ui.dll build/qol_ui.ini"

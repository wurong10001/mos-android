#!/usr/bin/env bash
# 编译 tools/test_wasm_native.c (Termux 原生可执行文件, 直接在本机运行)
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
WASM3="$ROOT/app/src/main/cpp/third_party/wasm3"
OUT="$ROOT/tools/test_wasm_native"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

for f in m3_api_libc m3_bind m3_code m3_compile m3_core m3_deterministic \
         m3_env m3_exec m3_function m3_info m3_module m3_parse m3_snapshot \
         m3_validate m3_xxh64; do
    clang -std=gnu99 -O2 -I"$WASM3" -c "$WASM3/$f.c" -o "$WORK/$f.o"
done

clang++ -std=gnu++17 -O2 -I"$WASM3" \
    "$ROOT/tools/test_wasm_native.c" "$WORK"/*.o -o "$OUT"

echo "built $OUT"

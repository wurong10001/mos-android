#!/usr/bin/env bash
# 在没有 Android SDK 的 arm64 Termux 上编译验证 native 代码 (libmosandroid.so)
#
# 原理:
#   - Termux clang 原生就是 aarch64-linux-android target
#   - Termux 自带 NDK android/*.h 头文件 (与 bionic 匹配, 无需外部 NDK)
#   - native_app_glue 从 NDK zip 按需下载 (.ndk-headers/glue/)
#   - 直接链接设备系统库 /system/lib64/{liblog,libandroid}.so
#
# 用法:
#   python3 tools/fetch_ndk_headers.py .ndk-headers   # 首次 (只需 glue)
#   bash tools/build_native_arm64.sh
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
NDK_INC="$ROOT/.ndk-headers/sysroot"
GLUE="$ROOT/.ndk-headers/glue"
OUT="$ROOT/app/src/main/cpp"
API=24

CXX="${CXX:-clang++}"
CC="${CC:-clang}"

if [ ! -f "$GLUE/android_native_app_glue.h" ]; then
    echo "missing glue headers, run: python3 tools/fetch_ndk_headers.py $ROOT/.ndk-headers" >&2
    exit 1
fi

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

COMMON="-fPIC -O2 -Wall"

# 1) wasm3 (C99) -> 静态对象
WASM3_SRC=""
for f in m3_api_libc m3_bind m3_code m3_compile m3_core m3_deterministic \
         m3_env m3_exec m3_function m3_info m3_module m3_parse m3_snapshot \
         m3_validate m3_xxh64; do
    WASM3_SRC="$WASM3_SRC $OUT/third_party/wasm3/$f.c"
done
for f in $WASM3_SRC; do
    $CC -std=gnu99 $COMMON -I"$OUT/third_party/wasm3" -c "$f" -o "$WORK/$(basename "$f" .c).o"
done

# 2) native_app_glue (C) -> 对象
$CC -std=gnu99 $COMMON -I"$GLUE" -c "$GLUE/android_native_app_glue.c" \
    -o "$WORK/native_app_glue.o"

# 3) C++ 源码 + 全部对象 -> libmosandroid.so
$CXX -std=c++17 $COMMON -shared -static-libstdc++ \
    -I"$GLUE" -I"$OUT/third_party/wasm3" \
    "$OUT/main.cpp" "$OUT/engine.cpp" "$OUT/error_page.cpp" \
    "$OUT/crash_handler.cpp" "$OUT/log.cpp" "$OUT/strings.cpp" \
    "$OUT/wasm_runner.cpp" \
    "$WORK"/*.o \
    /system/lib64/liblog.so /system/lib64/libandroid.so \
    -o "$OUT/libmosandroid.so"

echo "built: $OUT/libmosandroid.so"
ls -la "$OUT/libmosandroid.so"

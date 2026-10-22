cmake_minimum_required(VERSION 3.22.1)

project("mosandroid")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---- wasm3: 轻量 WebAssembly 解释器 (静态库) ----
# 排除依赖外部库的组件: uvwasi/wasi/meta_wasi/tracer
set(WASM3_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/wasm3)
add_library(wasm3 STATIC
    ${WASM3_DIR}/m3_api_libc.c
    ${WASM3_DIR}/m3_bind.c
    ${WASM3_DIR}/m3_code.c
    ${WASM3_DIR}/m3_compile.c
    ${WASM3_DIR}/m3_core.c
    ${WASM3_DIR}/m3_deterministic.c
    ${WASM3_DIR}/m3_env.c
    ${WASM3_DIR}/m3_exec.c
    ${WASM3_DIR}/m3_function.c
    ${WASM3_DIR}/m3_info.c
    ${WASM3_DIR}/m3_module.c
    ${WASM3_DIR}/m3_parse.c
    ${WASM3_DIR}/m3_snapshot.c
    ${WASM3_DIR}/m3_validate.c
    ${WASM3_DIR}/m3_xxh64.c
)
target_include_directories(wasm3 PUBLIC ${WASM3_DIR})

# ---- NDK native_app_glue: NativeActivity 事件循环 ----
add_library(native_app_glue STATIC
    ${ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c)
target_include_directories(native_app_glue PUBLIC
    ${ANDROID_NDK}/sources/android/native_app_glue)

# ---- 主库: 编译为 libmosandroid.so, 由 NativeActivity 加载 ----
add_library(mosandroid SHARED
    main.cpp
    engine.cpp
    error_page.cpp
    crash_handler.cpp
    log.cpp
    strings.cpp
    wasm_runner.cpp)

# 静态链接 libc++, 避免运行时依赖 libc++_shared.so (应用库路径里没有它)
target_link_options(mosandroid PRIVATE -static-libstdc++)

target_link_libraries(mosandroid
    wasm3
    native_app_glue
    android
    log)

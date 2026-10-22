#pragma once
#include <android/native_window.h>

#include "wasm_runner.h"

struct android_app;

// 应用引擎: 管理 NativeActivity 生命周期 + Surface + wasm 渲染循环
struct Engine {
    struct android_app* app = nullptr;
    ANativeWindow* window = nullptr;

    WasmRunner wasm;
    int32_t frame = 0;

    // 帧率统计 / 限速
    int64_t fpsFrames = 0;
    int64_t fpsLastNs = 0;
    int64_t frameLastNs = 0;
};

bool engineInit(Engine* e, const uint8_t* wasmBytes, size_t size);
void engineTerm(Engine* e);
void engineHandleCmd(struct android_app* app, int32_t cmd);
void engineDrawFrame(Engine* e);

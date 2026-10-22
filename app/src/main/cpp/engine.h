#pragma once
#include <android/input.h>
#include <android/native_window.h>

#include <vector>

#include "wasm_runner.h"
#include "error_page.h"

struct android_app;

// 应用引擎: 管理 NativeActivity 生命周期 + Surface + wasm 渲染循环 + 错误页
struct Engine {
    struct android_app* app = nullptr;
    ANativeWindow* window = nullptr;

    WasmRunner wasm;
    ErrorPage errorPage;
    std::vector<uint8_t> wasmBytes;  // assets 里读出的 wasm 模块 (重试用)
    bool wasmLoaded = false;
    int renderFailCount = 0;
    const char* filesDir = nullptr;

    int32_t frame = 0;

    // 帧率统计 / 限速
    int64_t fpsFrames = 0;
    int64_t fpsLastNs = 0;
    int64_t frameLastNs = 0;
};

// 加载(或重新加载) wasm; 失败时自动打开错误页
bool engineLoadWasm(Engine* e);

void engineTerm(Engine* e);
void engineHandleCmd(struct android_app* app, int32_t cmd);
int32_t engineHandleInput(struct android_app* app, AInputEvent* event);
void engineDrawFrame(Engine* e);

#pragma once
#include <android/input.h>
#include <android/native_window.h>

#include <vector>

#include "wasm_runner.h"

struct android_app;

// 应用引擎: 管理 NativeActivity 生命周期 + Surface + wasm 渲染循环
// 错误展示委托给 Java 错误页 (org.wurong.mos.ErrorActivity, 见 java_ui.h)
struct Engine {
    struct android_app* app = nullptr;
    ANativeWindow* window = nullptr;
    bool focused = false;  // 窗口焦点 (错误页盖住时暂停渲染)

    WasmRunner wasm;
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

// 加载(或重新加载) wasm; 失败时拉起 Java 错误页
bool engineLoadWasm(Engine* e);

// 拉起 Java 错误页 (errorKey 见 java_ui.h; details 为动态错误内容)
void engineShowError(Engine* e, const char* errorKey, const char* details);

void engineTerm(Engine* e);
void engineHandleCmd(struct android_app* app, int32_t cmd);
int32_t engineHandleInput(struct android_app* app, AInputEvent* event);
void engineDrawFrame(Engine* e);

#include <android/asset_manager.h>
#include <android/log.h>
#include <android_native_app_glue.h>

#include <vector>

#include "engine.h"

#define LOG_TAG "main"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// 从 assets 读取 wasm 模块
static bool loadAssetWasm(struct android_app* app, std::vector<uint8_t>* out)
{
    AAssetManager* mgr = app->activity->assetManager;
    if (mgr == nullptr) return false;

    AAsset* asset = AAssetManager_open(mgr, "sample.wasm", AASSET_MODE_BUFFER);
    if (asset == nullptr) {
        LOGE("assets/sample.wasm not found");
        return false;
    }
    const void* buf = AAsset_getBuffer(asset);
    size_t len = AAsset_getLength(asset);
    bool ok = false;
    if (buf != nullptr && len > 0) {
        const uint8_t* begin = static_cast<const uint8_t*>(buf);
        out->assign(begin, begin + len);
        ok = true;
    }
    AAsset_close(asset);
    return ok;
}

// NativeActivity 入口 (由 android_native_app_glue 调用)
void android_main(struct android_app* app)
{
    Engine engine{};  // 默认成员初始化, 无需 memset
    app->userData = &engine;
    app->onAppCmd = engineHandleCmd;
    engine.app = app;

    std::vector<uint8_t> wasm;
    if (!loadAssetWasm(app, &wasm)) {
        LOGE("failed to load wasm module, exit");
        return;
    }
    LOGI("loaded sample.wasm: %zu bytes", wasm.size());

    if (!engineInit(&engine, wasm.data(), wasm.size())) {
        LOGE("engine init failed, exit");
        return;
    }

    // 主循环: 有窗口就渲染一帧
    // (ALooper_pollAll 在新版 NDK 中已被标记不可用, 改用 ALooper_pollOnce)
    while (true) {
        int events;
        struct android_poll_source* source;
        while (ALooper_pollOnce(engine.window != nullptr ? 0 : -1, nullptr,
                                &events,
                                reinterpret_cast<void**>(&source)) >= 0) {
            if (source != nullptr) {
                source->process(app, source);
            }
            if (app->destroyRequested != 0) {
                goto done;
            }
        }
        if (app->destroyRequested != 0) {
            break;
        }
        if (engine.window != nullptr) {
            engineDrawFrame(&engine);
        }
    }

done:
    engineTerm(&engine);
    LOGI("exit");
}

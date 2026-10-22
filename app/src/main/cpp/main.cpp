#include <android/asset_manager.h>
#include <android/log.h>
#include <android/native_activity.h>
#include <android/window.h>
#include <android_native_app_glue.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <string>
#include <vector>

#include "crash_handler.h"
#include "engine.h"
#include "java_ui.h"
#include "log.h"

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

// 测试用: 在日志目录放一个名为 test_crash 的文件, 启动时会故意崩溃,
// 用于验证崩溃处理与错误页 (详见 README)
static bool testCrashRequested(const char* dir)
{
    if (dir == nullptr) return false;
    char path[600];
    snprintf(path, sizeof(path), "%s/test_crash", dir);
    struct stat st;
    return stat(path, &st) == 0;
}

// NativeActivity 入口 (由 android_native_app_glue 调用)
void android_main(struct android_app* app)
{
    Engine engine{};
    app->userData = &engine;
    app->onAppCmd = engineHandleCmd;
    app->onInputEvent = engineHandleInput;
    engine.app = app;

    // 日志目录优先用外部应用专属目录 (文件管理器可直接查看, 无需权限),
    // 不可用时回退到内部私有目录
    const char* internalDir = app->activity->internalDataPath;
    const char* externalDir = app->activity->externalDataPath;
    mkdir(internalDir, 0750);
    if (externalDir != nullptr) mkdir(externalDir, 0750);

    const char* filesDir = externalDir;
    if (filesDir == nullptr || !moslog::canWrite(filesDir)) {
        filesDir = internalDir;
    }
    engine.filesDir = filesDir;

    moslog::init(filesDir);
    crash::install(filesDir);

    // 全屏: 隐藏状态栏 (可在任意线程调用)
    ANativeActivity_setWindowFlags(app->activity, AWINDOW_FLAG_FULLSCREEN, 0);

    LOGI("app start (pid=%d, files=%s, log=%s)", getpid(), filesDir,
         moslog::filePath());

    if (testCrashRequested(filesDir)) {
        LOGW("test_crash detected, crashing on purpose");
        volatile int* p = nullptr;
        *p = 42;  // 故意段错误
    }

    char logPath[700];
    char crashPath[700];
    snprintf(logPath, sizeof(logPath), "%s/mos.log", filesDir);
    snprintf(crashPath, sizeof(crashPath), "%s/crash.log", filesDir);

    // 上次崩溃过? -> 拉起 Java 错误页展示崩溃报告
    if (crash::hasPendingReport(filesDir)) {
        const std::string report = crash::takeReport(filesDir);
        if (!ui_show_error(app, "crash_prev", report.c_str(), logPath,
                           crashPath)) {
            LOGE("failed to show crash report page");
        }
        LOGI("previous crash report page shown");
    }

    // 读取 wasm 模块 (加载失败由 engineDrawFrame 里的重试逻辑处理)
    if (!loadAssetWasm(app, &engine.wasmBytes)) {
        LOGE("failed to load wasm module");
        engineShowError(&engine, "asset_missing", nullptr);
    } else {
        LOGI("loaded sample.wasm: %zu bytes", engine.wasmBytes.size());
    }

    // 主循环: 有窗口且有焦点时渲染 (错误页盖住时暂停, 省电)
    // (ALooper_pollAll 在新版 NDK 中已被标记不可用, 改用 ALooper_pollOnce)
    while (true) {
        int events;
        struct android_poll_source* source;
        while (ALooper_pollOnce(
                   engine.window != nullptr && engine.focused ? 0 : -1, nullptr,
                   &events, reinterpret_cast<void**>(&source)) >= 0) {
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
        engineDrawFrame(&engine);
    }

done:
    engineTerm(&engine);
    LOGI("app exit");
}

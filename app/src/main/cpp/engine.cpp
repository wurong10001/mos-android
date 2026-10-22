#include "engine.h"

#include <android/input.h>
#include <android/log.h>
#include <android_native_app_glue.h>
#include <time.h>
#include <cstdlib>
#include <cstring>

#include "crash_handler.h"
#include "log.h"
#include "strings.h"

#include <string>

#define LOG_TAG "Engine"

static const int64_t kFrameIntervalNs = 16000000LL;  // ~60fps

static int64_t nowNs()
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<int64_t>(ts.tv_sec) * 1000000000LL + ts.tv_nsec;
}

static void engineInitDisplay(Engine* e)
{
    // 软件渲染: 固定 32bit 像素格式 (wasm 输出 0xAABBGGRR, 小端)
    ANativeWindow_setBuffersGeometry(e->window, 0, 0, WINDOW_FORMAT_RGBX_8888);
}

static void engineTermDisplay(Engine* e)
{
    e->window = nullptr;
}

// title/body: 本地化文本 (错误页显示); logMsg: 英语 (写日志)
static void engineShowError(Engine* e, const std::string& title,
                            const std::string& body, const std::string& logMsg)
{
    LOGE("error page shown [%s]", logMsg.c_str());
    e->errorPage.open(title.c_str(), body);
}

void engineHandleCmd(struct android_app* app, int32_t cmd)
{
    Engine* e = static_cast<Engine*>(app->userData);
    switch (cmd) {
        case APP_CMD_INIT_WINDOW:
            e->window = app->window;
            engineInitDisplay(e);
            LOGI("window initialized");
            break;
        case APP_CMD_TERM_WINDOW:
            engineTermDisplay(e);
            LOGI("window terminated");
            break;
        case APP_CMD_GAINED_FOCUS:
            e->frameLastNs = 0;
            break;
        case APP_CMD_LOST_FOCUS:
            break;
        default:
            break;
    }
}

// 触摸事件: 错误页激活时拦截 (轻触重试, 长按清日志)
int32_t engineHandleInput(struct android_app* app, AInputEvent* event)
{
    Engine* e = static_cast<Engine*>(app->userData);
    if (!e->errorPage.active) return 0;
    if (AInputEvent_getType(event) != AINPUT_EVENT_TYPE_MOTION) return 0;

    static int64_t s_downTimeNs = 0;
    const int32_t action = AMotionEvent_getAction(event) & AMOTION_EVENT_ACTION_MASK;
    if (action == AMOTION_EVENT_ACTION_DOWN) {
        s_downTimeNs = nowNs();
    } else if (action == AMOTION_EVENT_ACTION_UP) {
        const bool longPress = (nowNs() - s_downTimeNs) > 1000000000LL;
        e->errorPage.onTouch(longPress);
    }
    return 1;
}

bool engineLoadWasm(Engine* e)
{
    const std::string details = strings::get().bodyDetails + "\n";
    if (e->wasmBytes.empty()) {
        engineShowError(e, strings::get().errWasmEmpty,
                        details + "assets/sample.wasm not in memory",
                        "wasm empty");
        return false;
    }
    if (!e->wasm.load(e->wasmBytes.data(), e->wasmBytes.size())) {
        engineShowError(e, strings::get().errWasmLoad,
                        details + e->wasm.lastError(),
                        std::string("wasm load: ") + e->wasm.lastError());
        return false;
    }
    int32_t offset = 0;
    if (!e->wasm.init(&offset)) {
        engineShowError(e, strings::get().errWasmInit,
                        details + e->wasm.lastError(),
                        std::string("wasm init: ") + e->wasm.lastError());
        return false;
    }
    e->wasmLoaded = true;
    e->renderFailCount = 0;
    LOGI("wasm loaded, framebuffer offset=0x%x", offset);
    return true;
}

void engineTerm(Engine* e)
{
    engineTermDisplay(e);
}

void engineDrawFrame(Engine* e)
{
    if (e->window == nullptr) return;

    // ---- 错误页模式 ----
    if (e->errorPage.active) {
        e->errorPage.draw(e->window);
        if (e->errorPage.exitRequested) {
            const bool clearLog = e->errorPage.clearLogRequested;
            e->errorPage.close();
            if (clearLog) {
                crash::clearReport(e->filesDir);
                LOGI("crash log cleared by user");
            }
            LOGI("retrying wasm...");
            engineLoadWasm(e);  // 失败会再次打开错误页
        }
        return;
    }

    // ---- 正常模式: 首次或重试时加载 wasm ----
    if (!e->wasmLoaded) {
        if (!engineLoadWasm(e)) return;
    }

    // 1. 让 wasm 绘制一帧 (内部回调 host_present 更新显存偏移/宽高)
    if (!e->wasm.render(e->frame++)) {
        if (++e->renderFailCount >= 3) {
            engineShowError(
                e, strings::get().errWasmRender,
                strings::get().bodyDetails + "\n" + e->wasm.lastError(),
                std::string("wasm render: ") + e->wasm.lastError());
        }
        return;
    }
    e->renderFailCount = 0;

    // 2. 把 wasm 虚拟显存 blit 到 Surface (最近邻缩放)
    const uint8_t* fb = e->wasm.framebuffer();
    const uint32_t fw = e->wasm.fbWidth();
    const uint32_t fh = e->wasm.fbHeight();
    if (fb == nullptr || fw == 0 || fh == 0) return;

    ANativeWindow_Buffer buffer;
    if (ANativeWindow_lock(e->window, &buffer, nullptr) != 0) {
        LOGE("ANativeWindow_lock failed");
        return;
    }

    uint32_t* xmap = static_cast<uint32_t*>(
        malloc(sizeof(uint32_t) * buffer.width));
    if (xmap != nullptr) {
        for (uint32_t x = 0; x < buffer.width; ++x) {
            xmap[x] = static_cast<uint32_t>(
                static_cast<uint64_t>(x) * fw / buffer.width);
        }
        const uint32_t* src = reinterpret_cast<const uint32_t*>(fb);
        uint32_t* dst = static_cast<uint32_t*>(buffer.bits);
        for (uint32_t y = 0; y < buffer.height; ++y) {
            const uint32_t sy = static_cast<uint32_t>(
                static_cast<uint64_t>(y) * fh / buffer.height);
            const uint32_t* srow = src + static_cast<size_t>(sy) * fw;
            uint32_t* drow = dst + static_cast<size_t>(y) * buffer.stride;
            for (uint32_t x = 0; x < buffer.width; ++x) {
                drow[x] = srow[xmap[x]];
            }
        }
        free(xmap);
    }

    ANativeWindow_unlockAndPost(e->window);

    // 3. 帧率统计 + 限速到 ~60fps
    const int64_t now = nowNs();
    if (e->fpsLastNs == 0) {
        e->fpsLastNs = now;
    }
    if (++e->fpsFrames >= 60) {
        const double secs = static_cast<double>(now - e->fpsLastNs) / 1e9;
        if (secs > 0.0) {
            LOGI("fps: %.1f", static_cast<double>(e->fpsFrames) / secs);
        }
        e->fpsFrames = 0;
        e->fpsLastNs = now;
    }

    if (e->frameLastNs == 0) {
        e->frameLastNs = now;
    } else {
        const int64_t target = e->frameLastNs + kFrameIntervalNs;
        if (target > now) {
            struct timespec ts;
            ts.tv_sec = 0;
            ts.tv_nsec = static_cast<long>(target - now);
            nanosleep(&ts, nullptr);
            e->frameLastNs = target;
        } else {
            e->frameLastNs = now;  // 落后太多则重置, 不追帧
        }
    }
}

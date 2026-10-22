#include "engine.h"

#include <android/log.h>
#include <time.h>
#include <cstdlib>
#include <cstring>

#define LOG_TAG "Engine"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

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

bool engineInit(Engine* e, const uint8_t* wasmBytes, size_t size)
{
    if (!e->wasm.load(wasmBytes, size)) {
        LOGE("wasm load failed: %s", e->wasm.lastError());
        return false;
    }
    int32_t offset = 0;
    if (!e->wasm.init(&offset)) {
        LOGE("wasm init failed: %s", e->wasm.lastError());
        return false;
    }
    return true;
}

void engineTerm(Engine* e)
{
    engineTermDisplay(e);
}

void engineDrawFrame(Engine* e)
{
    if (e->window == nullptr) return;

    // 1. 让 wasm 绘制一帧 (内部回调 host_present 更新显存偏移/宽高)
    if (!e->wasm.render(e->frame++)) {
        LOGE("wasm render failed: %s", e->wasm.lastError());
        return;
    }

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

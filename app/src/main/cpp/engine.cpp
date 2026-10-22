#include "engine.h"

#include <android/input.h>
#include <android/log.h>
#include <android_native_app_glue.h>
#include <jni.h>
#include <time.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "crash_handler.h"
#include "java_ui.h"
#include "log.h"

#include <string>

// 沉浸式全屏: 隐藏状态栏 + 导航栏 (滑动可临时呼出, 随后自动隐藏)
// setSystemUiVisibility 自 API 30 起被标记 deprecated, 但各版本仍然有效,
// 且比 WindowInsetsController (API 30+) 兼容更老的 minSdk
namespace {

// SYSTEM_UI_FLAG_HIDE_NAVIGATION(2) | FULLSCREEN(4) |
// LAYOUT_HIDE_NAVIGATION(512) | LAYOUT_FULLSCREEN(1024) | IMMERSIVE_STICKY(4096)
constexpr jint kImmersiveFlags = 2 | 4 | 512 | 1024 | 4096;

// 取 decor view (失败返回 nullptr); caller 负责 DeleteLocalRef
jobject getDecorView(JNIEnv* env, jobject activity)
{
    jclass activityClass = env->GetObjectClass(activity);
    if (activityClass == nullptr) return nullptr;
    jobject result = nullptr;
    jmethodID getWindow = env->GetMethodID(activityClass, "getWindow",
                                           "()Landroid/view/Window;");
    if (getWindow != nullptr) {
        jobject window = env->CallObjectMethod(activity, getWindow);
        if (window != nullptr) {
            jclass windowClass = env->GetObjectClass(window);
            jmethodID getDecor = env->GetMethodID(windowClass, "getDecorView",
                                                  "()Landroid/view/View;");
            if (getDecor != nullptr) {
                result = env->CallObjectMethod(window, getDecor);
            }
            env->DeleteLocalRef(windowClass);
        }
        env->DeleteLocalRef(window);
    }
    env->DeleteLocalRef(activityClass);
    return result;
}

bool applyImmersiveMode(struct android_app* app)
{
    if (app == nullptr || app->activity == nullptr) return false;
    JavaVM* vm = app->activity->vm;
    if (vm == nullptr) return false;

    JNIEnv* env = nullptr;
    if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_4) != JNI_OK) {
        if (vm->AttachCurrentThread(&env, nullptr) != JNI_OK) {
            LOGE("immersive: AttachCurrentThread failed");
            return false;
        }
        // android_main 线程长驻, 保持附加状态, 不 detach
    }
    if (env == nullptr) return false;

    jobject decorView = getDecorView(env, app->activity->clazz);
    bool ok = false;
    if (decorView != nullptr) {
        jclass viewClass = env->GetObjectClass(decorView);
        jmethodID setVis = env->GetMethodID(viewClass, "setSystemUiVisibility",
                                            "(I)V");
        if (setVis != nullptr) {
            env->CallVoidMethod(decorView, setVis, kImmersiveFlags);
            ok = true;
        }
        env->DeleteLocalRef(viewClass);
        env->DeleteLocalRef(decorView);
    }
    if (env->ExceptionCheck()) {
        env->ExceptionDescribe();
        env->ExceptionClear();
        ok = false;
    }
    return ok;
}

// 读取当前系统 UI 可见性标志; 失败返回 -1
jint querySystemUiVisibility(struct android_app* app)
{
    if (app == nullptr || app->activity == nullptr) return -1;
    JavaVM* vm = app->activity->vm;
    if (vm == nullptr) return -1;
    JNIEnv* env = nullptr;
    if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_4) != JNI_OK) return -1;
    if (env == nullptr) return -1;

    jint vis = -1;
    jobject decorView = getDecorView(env, app->activity->clazz);
    if (decorView != nullptr) {
        jclass viewClass = env->GetObjectClass(decorView);
        jmethodID getVis = env->GetMethodID(viewClass, "getSystemUiVisibility",
                                            "()I");
        if (getVis != nullptr) {
            vis = env->CallIntMethod(decorView, getVis);
        }
        env->DeleteLocalRef(viewClass);
        env->DeleteLocalRef(decorView);
    }
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        vis = -1;
    }
    return vis;
}

// 自愈: 系统事件 (焦点变化/窗口重建/呼出系统栏) 可能清除沉浸标志,
// 定期检查, 不一致就重新应用。这就是"概率隐藏"的根治。
void ensureImmersive(struct android_app* app, int64_t now)
{
    static int64_t lastCheckNs = 0;
    static bool lastOk = false;
    if (now - lastCheckNs < 500000000LL) return;  // 每 500ms 一次
    lastCheckNs = now;

    const jint vis = querySystemUiVisibility(app);
    const bool ok = (vis & kImmersiveFlags) == kImmersiveFlags;
    if (ok) {
        lastOk = true;
        return;
    }
    const bool applied = applyImmersiveMode(app);
    if (applied != lastOk || !applied) {
        LOGI("immersive mode restored (vis=0x%x, ok=%d)",
             static_cast<uint32_t>(vis < 0 ? 0 : vis), applied ? 1 : 0);
    }
    lastOk = applied;
}

}  // namespace

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
// 拉起 Java 错误页; details 为动态错误内容 (英语, 来自 wasm3 等)
void engineShowError(Engine* e, const char* errorKey, const char* details)
{
    LOGE("showing error page [%s]: %s", errorKey, details ? details : "");
    char logPath[700];
    char crashPath[700];
    snprintf(logPath, sizeof(logPath), "%s/mos.log", e->filesDir);
    snprintf(crashPath, sizeof(crashPath), "%s/crash.log", e->filesDir);
    if (!ui_show_error(e->app, errorKey, details, logPath, crashPath)) {
        LOGE("ui_show_error failed, error only logged");
    }
}

void engineHandleCmd(struct android_app* app, int32_t cmd)
{
    Engine* e = static_cast<Engine*>(app->userData);
    switch (cmd) {
        case APP_CMD_INIT_WINDOW:
            e->window = app->window;
            engineInitDisplay(e);
            if (applyImmersiveMode(app)) {
                LOGI("window initialized (immersive on)");
            } else {
                LOGI("window initialized (immersive failed)");
            }
            break;
        case APP_CMD_TERM_WINDOW:
            engineTermDisplay(e);
            LOGI("window terminated");
            break;
        case APP_CMD_GAINED_FOCUS:
            e->focused = true;
            e->frameLastNs = 0;
            e->renderFailCount = 0;  // 从错误页返回, 给重试新的配额
            applyImmersiveMode(app);  // 对话框/通知拉出系统栏后恢复
            LOGI("focus gained (wasm retry budget reset)");
            break;
        case APP_CMD_LOST_FOCUS:
            e->focused = false;  // 错误页盖住本窗口, 暂停渲染
            LOGI("focus lost (rendering paused)");
            break;
        default:
            break;
    }
}

// 输入事件: 目前不拦截 (错误页是 Java Activity, 自行处理触摸)
int32_t engineHandleInput(struct android_app* app, AInputEvent* event)
{
    (void)app;
    (void)event;
    return 0;
}

bool engineLoadWasm(Engine* e)
{
    if (e->wasmBytes.empty()) {
        engineShowError(e, "wasm_empty", "assets/sample.wasm not in memory");
        return false;
    }
    if (!e->wasm.load(e->wasmBytes.data(), e->wasmBytes.size())) {
        engineShowError(e, "wasm_load", e->wasm.lastError());
        return false;
    }
    int32_t offset = 0;
    if (!e->wasm.init(&offset)) {
        engineShowError(e, "wasm_init", e->wasm.lastError());
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
    if (e->window == nullptr || !e->focused) return;

    // ---- 正常模式: 首次或重试时加载 wasm ----
    if (!e->wasmLoaded) {
        if (!engineLoadWasm(e)) return;  // 失败会拉起 Java 错误页
    }

    // 1. 让 wasm 绘制一帧 (内部回调 host_present 更新显存偏移/宽高)
    if (!e->wasm.render(e->frame++)) {
        if (++e->renderFailCount >= 3) {
            engineShowError(e, "wasm_render", e->wasm.lastError());
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

    // 沉浸式全屏自愈: 系统栏被系统事件重新拉起时自动恢复
    ensureImmersive(e->app, now);

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

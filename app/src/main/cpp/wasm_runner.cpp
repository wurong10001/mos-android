#include "wasm_runner.h"

#include "wasm3.h"

#include "log.h"

#include <new>
#include <cstdio>
#include <cstring>

namespace {

// ---------------------------------------------------------------------------
// host 函数: wasm 通过 (import "env" ...) 调用
// 用 m3_LinkRawFunctionEx 注册, userdata 指向 WasmRunner 实例
// ---------------------------------------------------------------------------

// host_present(offset: i32, width: i32, height: i32)
// wasm 画完一帧后调用, 把"显存偏移 + 宽高"传给宿主
m3ApiRawFunction(host_present)
{
    m3ApiGetArg(uint32_t, offset)
    m3ApiGetArg(uint32_t, width)
    m3ApiGetArg(uint32_t, height)

    auto* self = static_cast<WasmRunner*>(_ctx->userdata);
    if (self) {
        self->onPresent(offset, width, height);
    }
    m3ApiSuccess()
}

// host_log(offset: i32, len: i32)
// 让宿主打印 wasm 线性内存里的一段字符串
m3ApiRawFunction(host_log)
{
    m3ApiGetArg(uint32_t, offset)
    m3ApiGetArg(uint32_t, len)

    auto* self = static_cast<WasmRunner*>(_ctx->userdata);
    if (self) {
        self->logString(_mem, offset, len);
    }
    m3ApiSuccess()
}

}  // namespace

WasmRunner::~WasmRunner()
{
    destroy();
}

void WasmRunner::destroy()
{
    if (runtime_) {
        m3_FreeRuntime(static_cast<IM3Runtime>(runtime_));  // 同时释放 module
        runtime_ = nullptr;
    }
    if (env_) {
        m3_FreeEnvironment(static_cast<IM3Environment>(env_));
        env_ = nullptr;
    }
    module_ = nullptr;
    fnRender_ = nullptr;
    delete[] wasmBytes_;
    wasmBytes_ = nullptr;
    wasmSize_ = 0;
    memBase_ = nullptr;
    memSize_ = 0;
    fbOffset_ = 0;
    fbWidth_ = 0;
    fbHeight_ = 0;
}

bool WasmRunner::load(const uint8_t* data, size_t size)
{
    lastError_[0] = '\0';
    destroy();  // 允许重复加载 (错误页重试)
    if (data == nullptr || size == 0) {
        snprintf(lastError_, sizeof(lastError_), "empty wasm data");
        return false;
    }

    // wasm3 要求模块字节在生命周期内保持有效 -> 拷贝一份
    wasmBytes_ = new (std::nothrow) uint8_t[size];
    if (wasmBytes_ == nullptr) {
        snprintf(lastError_, sizeof(lastError_), "out of memory");
        return false;
    }
    memcpy(wasmBytes_, data, size);
    wasmSize_ = size;

    IM3Environment env = m3_NewEnvironment();
    if (env == nullptr) {
        snprintf(lastError_, sizeof(lastError_), "m3_NewEnvironment failed");
        return false;
    }
    env_ = env;

    IM3Runtime runtime = m3_NewRuntime(env, 64 * 1024, nullptr);
    if (runtime == nullptr) {
        snprintf(lastError_, sizeof(lastError_), "m3_NewRuntime failed");
        return false;
    }
    runtime_ = runtime;

    IM3Module module = nullptr;
    M3Result res = m3_ParseModule(env, &module, wasmBytes_,
                                  static_cast<uint32_t>(wasmSize_));
    if (res != nullptr) {
        snprintf(lastError_, sizeof(lastError_), "m3_ParseModule: %s", res);
        return false;
    }
    module_ = module;

    // 注意: m3_LinkRawFunction 要求 module 已绑定 runtime, 必须先 LoadModule
    res = m3_LoadModule(runtime, module);
    if (res != nullptr) {
        snprintf(lastError_, sizeof(lastError_), "m3_LoadModule: %s", res);
        return false;
    }

    res = m3_LinkRawFunctionEx(module, "env", "host_present", "v(iii)",
                               &host_present, this);
    if (res != nullptr) {
        snprintf(lastError_, sizeof(lastError_), "link host_present: %s", res);
        return false;
    }
    res = m3_LinkRawFunctionEx(module, "env", "host_log", "v(ii)",
                               &host_log, this);
    if (res != nullptr) {
        snprintf(lastError_, sizeof(lastError_), "link host_log: %s", res);
        return false;
    }

    size_t memSize = 0;
    uint8_t* mem = m3_GetMemory(module, &memSize, 0);
    if (mem == nullptr || memSize == 0) {
        snprintf(lastError_, sizeof(lastError_), "module has no memory");
        return false;
    }
    memBase_ = mem;
    memSize_ = memSize;

    IM3Function fRender = nullptr;
    res = m3_FindFunction(&fRender, runtime, "render");
    if (res != nullptr || fRender == nullptr) {
        snprintf(lastError_, sizeof(lastError_), "export 'render' not found");
        return false;
    }
    fnRender_ = fRender;

    LOGI("wasm module loaded, linear memory: %zu bytes", memSize_);
    return true;
}

bool WasmRunner::init(int32_t* outOffset)
{
    IM3Function f = nullptr;
    M3Result res = m3_FindFunction(&f, static_cast<IM3Runtime>(runtime_), "init");
    if (res != nullptr || f == nullptr) {
        snprintf(lastError_, sizeof(lastError_), "export 'init' not found");
        return false;
    }
    res = m3_Call(f, 0, nullptr);
    if (res != nullptr) {
        snprintf(lastError_, sizeof(lastError_), "init(): %s", res);
        return false;
    }
    int32_t offset = 0;
    const void* rets[1] = {&offset};
    res = m3_GetResults(f, 1, rets);
    if (res != nullptr) {
        snprintf(lastError_, sizeof(lastError_), "init() results: %s", res);
        return false;
    }
    fbOffset_ = static_cast<uint32_t>(offset);
    if (outOffset != nullptr) {
        *outOffset = offset;
    }
    LOGI("init() -> framebuffer offset 0x%x", offset);
    return true;
}

bool WasmRunner::render(int32_t frame)
{
    IM3Function f = static_cast<IM3Function>(fnRender_);
    if (f == nullptr) {
        snprintf(lastError_, sizeof(lastError_), "render not loaded");
        return false;
    }
    const void* args[1] = {&frame};
    M3Result res = m3_Call(f, 1, args);
    if (res != nullptr) {
        snprintf(lastError_, sizeof(lastError_), "render(%d): %s", frame, res);
        return false;
    }
    return true;
}

void WasmRunner::onPresent(uint32_t offset, uint32_t width, uint32_t height)
{
    fbOffset_ = offset;
    fbWidth_ = width;
    fbHeight_ = height;
}

void WasmRunner::logString(void* mem, uint32_t offset, uint32_t len)
{
    const uint8_t* base = static_cast<const uint8_t*>(mem);
    size_t size = m3_GetMemorySizeAt(mem);
    if (base == nullptr ||
        static_cast<size_t>(offset) + static_cast<size_t>(len) > size) {
        LOGE("host_log: out of bounds (offset=%u len=%u size=%zu)",
             offset, len, size);
        return;
    }
    if (len > 512) {
        len = 512;  // 防止日志爆炸
    }
    LOGI("wasm: %.*s", static_cast<int>(len),
         reinterpret_cast<const char*>(base + offset));
}

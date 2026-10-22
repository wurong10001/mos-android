#pragma once
#include <cstddef>
#include <cstdint>

// WASM3 运行时封装:
//   - 加载 wasm 模块 (字节会被拷贝, 调用方数据可随即释放)
//   - 通过 m3_LinkRawFunctionEx 注册 host 函数 (env.host_present / env.host_log)
//   - 调用导出的 init() / render(frame)
//   - 把 wasm 线性内存当作"虚拟显存"暴露给 Native 层:
//     wasm 每帧画完后调用 host_present(offset, width, height) 通知宿主
class WasmRunner {
public:
    WasmRunner() = default;
    ~WasmRunner();

    WasmRunner(const WasmRunner&) = delete;
    WasmRunner& operator=(const WasmRunner&) = delete;

    bool load(const uint8_t* data, size_t size);

    // 调用导出的 init(), 返回显存偏移
    bool init(int32_t* outOffset);

    // 调用导出的 render(frame): wasm 绘制一帧并回调 host_present
    bool render(int32_t frame);

    // ---- 由 wasm 通过 host_present 回调 ----
    void onPresent(uint32_t offset, uint32_t width, uint32_t height);

    // ---- 由 wasm 通过 host_log 回调: 打印线性内存中的字符串 ----
    void logString(void* mem, uint32_t offset, uint32_t len);

    // 虚拟显存指针 = 线性内存基址 + host_present 给出的偏移
    const uint8_t* framebuffer() const { return memBase_ + fbOffset_; }
    uint32_t fbOffset() const { return fbOffset_; }
    uint32_t fbWidth() const { return fbWidth_; }
    uint32_t fbHeight() const { return fbHeight_; }
    size_t memorySize() const { return memSize_; }

    const char* lastError() const { return lastError_; }

private:
    void destroy();  // 释放 wasm3 环境 (支持重复 load)

    uint8_t* wasmBytes_ = nullptr;  // 模块字节拷贝 (wasm3 要求生命周期内有效)
    size_t wasmSize_ = 0;

    void* env_ = nullptr;       // IM3Environment
    void* runtime_ = nullptr;   // IM3Runtime
    void* module_ = nullptr;    // IM3Module
    void* fnRender_ = nullptr;  // IM3Function

    const uint8_t* memBase_ = nullptr;  // wasm 线性内存
    size_t memSize_ = 0;

    uint32_t fbOffset_ = 0;
    uint32_t fbWidth_ = 0;
    uint32_t fbHeight_ = 0;

    char lastError_[256] = {};
};

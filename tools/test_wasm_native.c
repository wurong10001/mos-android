// 在 arm64 Android 上直接运行 wasm3 + sample.wasm, 验证:
//   1. 模块解析/加载/导出查找
//   2. m3_LinkRawFunctionEx 注册的 host 函数能被 wasm 调用
//   3. wasm 线性内存中的显存像素值正确
// 编译: bash tools/build_wasm_test.sh   运行: ./tools/test_wasm_native [wasm路径]
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wasm3.h"

static uint32_t g_offset = 0, g_width = 0, g_height = 0;
static int g_present_count = 0;

// host_present(offset, width, height): wasm 通知宿主显存信息
m3ApiRawFunction(host_present)
{
    m3ApiGetArg(uint32_t, offset)
    m3ApiGetArg(uint32_t, width)
    m3ApiGetArg(uint32_t, height)
    g_offset = offset;
    g_width = width;
    g_height = height;
    g_present_count++;
    m3ApiSuccess()
}

// host_log(offset, len): 打印 wasm 内存中的字符串
m3ApiRawFunction(host_log)
{
    m3ApiGetArg(uint32_t, offset)
    m3ApiGetArg(uint32_t, len)
    printf("  wasm log: %.*s\n", (int)len, (const char*)((uint8_t*)_mem + offset));
    m3ApiSuccess()
}

static int failures = 0;

static void check(const char* name, long got, long want)
{
    const int ok = (got == want);
    if (!ok) failures++;
    printf("  [%s] %s: got %ld, want %ld\n", ok ? "OK" : "FAIL", name, got, want);
}

int main(int argc, char** argv)
{
    const char* path = argc > 1 ? argv[1] : "app/src/main/assets/sample.wasm";

    FILE* f = fopen(path, "rb");
    if (f == NULL) {
        printf("FAIL: cannot open %s\n", path);
        return 1;
    }
    fseek(f, 0, SEEK_END);
    const long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t* buf = (uint8_t*)malloc((size_t)size);
    if (fread(buf, 1, (size_t)size, f) != (size_t)size) {
        printf("FAIL: short read\n");
        return 1;
    }
    fclose(f);
    printf("loaded %s (%ld bytes)\n", path, size);

    IM3Environment env = m3_NewEnvironment();
    if (env == NULL) { printf("FAIL: m3_NewEnvironment\n"); return 1; }
    IM3Runtime runtime = m3_NewRuntime(env, 64 * 1024, NULL);
    if (runtime == NULL) { printf("FAIL: m3_NewRuntime\n"); return 1; }

    IM3Module module = NULL;
    M3Result r = m3_ParseModule(env, &module, buf, (uint32_t)size);
    if (r) { printf("FAIL: m3_ParseModule: %s\n", r); return 1; }
    printf("parsed module\n");

    // 先 LoadModule 再 link (wasm3 要求)
    r = m3_LoadModule(runtime, module);
    if (r) { printf("FAIL: m3_LoadModule: %s\n", r); return 1; }
    printf("loaded module\n");

    r = m3_LinkRawFunctionEx(module, "env", "host_present", "v(iii)", &host_present, NULL);
    if (r) { printf("FAIL: link host_present: %s\n", r); return 1; }
    r = m3_LinkRawFunctionEx(module, "env", "host_log", "v(ii)", &host_log, NULL);
    if (r) { printf("FAIL: link host_log: %s\n", r); return 1; }
    printf("linked host functions\n");

    // ---- init() ----
    IM3Function fInit = NULL;
    r = m3_FindFunction(&fInit, runtime, "init");
    if (r || fInit == NULL) { printf("FAIL: export 'init' not found\n"); return 1; }
    r = m3_Call(fInit, 0, NULL);
    if (r) { printf("FAIL: init(): %s\n", r); return 1; }
    int32_t offset = 0;
    const void* rets[1] = {&offset};
    m3_GetResults(fInit, 1, rets);
    printf("init() = %d\n", offset);
    check("init() framebuffer offset", offset, 0x1000);

    // ---- render(0) ----
    IM3Function fRender = NULL;
    r = m3_FindFunction(&fRender, runtime, "render");
    if (r || fRender == NULL) { printf("FAIL: export 'render' not found\n"); return 1; }
    int32_t frame = 0;
    const void* args[1] = {&frame};
    r = m3_Call(fRender, 1, args);
    if (r) { printf("FAIL: render(0): %s\n", r); return 1; }
    printf("render(0) done, present count=%d offset=0x%x %ux%u\n",
           g_present_count, g_offset, g_width, g_height);
    check("host_present count", g_present_count, 1);
    check("present offset", g_offset, 0x1000);
    check("present width", g_width, 256);
    check("present height", g_height, 256);

    // ---- 检查显存像素 ----
    size_t memSize = 0;
    uint8_t* mem = m3_GetMemory(module, &memSize, 0);
    if (mem == NULL) { printf("FAIL: m3_GetMemory\n"); return 1; }
    const uint32_t* px = (const uint32_t*)(mem + 0x1000);
    printf("memory: %p size=%zu\n", (void*)mem, memSize);
    check("px(0,0) frame0", px[0], (long)0xff000000);
    check("px(10,5) frame0", px[5 * 256 + 10], (long)0xff0f0f1e);

    // ---- render(7) ----
    frame = 7;
    r = m3_Call(fRender, 1, args);
    if (r) { printf("FAIL: render(7): %s\n", r); return 1; }
    check("px(10,5) frame7", px[5 * 256 + 10], (long)0xff084041);

    m3_FreeRuntime(runtime);
    m3_FreeEnvironment(env);
    free(buf);

    if (failures == 0) printf("ALL PASS\n");
    else printf("FAILED (%d)\n", failures);
    return failures == 0 ? 0 : 1;
}

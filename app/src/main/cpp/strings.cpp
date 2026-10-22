#include "strings.h"

#include <android/log.h>
#include <cstring>
#include <string>

#define LOG_TAG "strings"

namespace strings {
namespace {

// 换行符用数值表示, 避免源码里的转义序列出问题
const char kNL = 10;

Lang g_lang = Lang::kEn;

Texts buildEn()
{
    Texts t;
    t.errCrashPrev = "Crashed on last launch";
    t.errWasmLoad = "Failed to load WASM module";
    t.errWasmInit = "WASM init failed";
    t.errWasmRender = "WASM render failed";
    t.errWasmEmpty = "WASM module is empty";
    t.bodyCrashPrev = "The app crashed on the last launch. Crash report:";
    t.bodyAssetMissing = std::string("Cannot read assets/sample.wasm.") + kNL +
                         std::string("Make sure the APK contains it, or regenerate:") + kNL +
                         std::string("  python3 tools/gen_sample_wasm.py");
    t.bodyDetails = "Details:";
    t.hintTap = "Tap to retry";
    t.hintLongPress = "Long-press to clear log";
    t.hintFullLog = std::string("... (full log: mos.log)");
    return t;
}

Texts buildZhCn()
{
    Texts t;
    t.errCrashPrev = "上次启动时崩溃";
    t.errWasmLoad = "WASM 模块加载失败";
    t.errWasmInit = "WASM init 失败";
    t.errWasmRender = "WASM 渲染失败";
    t.errWasmEmpty = "WASM 模块为空";
    t.bodyCrashPrev = "应用上次启动时崩溃。以下是崩溃报告:";
    t.bodyAssetMissing = std::string("无法读取 assets/sample.wasm。") + kNL +
                         std::string("请确认 APK 内包含该文件, 或重新生成:") + kNL +
                         std::string("  python3 tools/gen_sample_wasm.py");
    t.bodyDetails = "错误详情:";
    t.hintTap = "轻触屏幕: 重试";
    t.hintLongPress = "长按: 清除日志";
    t.hintFullLog = std::string("... (完整日志见 mos.log)");
    return t;
}

Texts buildZhTw()
{
    Texts t;
    t.errCrashPrev = "上次啟動時崩潰";
    t.errWasmLoad = "WASM 模組載入失敗";
    t.errWasmInit = "WASM init 失敗";
    t.errWasmRender = "WASM 轉譯失敗";
    t.errWasmEmpty = "WASM 模組為空";
    t.bodyCrashPrev = "應用上次啟動時崩潰。以下是崩潰報告:";
    t.bodyAssetMissing = std::string("無法讀取 assets/sample.wasm。") + kNL +
                         std::string("請確認 APK 內包含該檔案, 或重新產生:") + kNL +
                         std::string("  python3 tools/gen_sample_wasm.py");
    t.bodyDetails = "錯誤詳情:";
    t.hintTap = "輕觸螢幕: 重試";
    t.hintLongPress = "長按: 清除日誌";
    t.hintFullLog = std::string("... (完整日誌見 mos.log)");
    return t;
}

const Texts& tableFor(Lang lang)
{
    static const Texts kEn = buildEn();
    static const Texts kZhCn = buildZhCn();
    static const Texts kZhTw = buildZhTw();
    switch (lang) {
        case Lang::kZhCn: return kZhCn;
        case Lang::kZhTw: return kZhTw;
        default:          return kEn;
    }
}

}  // namespace

void init(AConfiguration* config)
{
    g_lang = Lang::kEn;
    if (config == nullptr) return;

    char langStr[3] = {0};
    AConfiguration_getLanguage(config, langStr);
    if (std::strcmp(langStr, "zh") != 0) {
        __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "language: %s -> English",
                            langStr[0] ? langStr : "?");
        return;
    }

    char country[3] = {0};
    AConfiguration_getCountry(config, country);
    // 繁体中文地区: 台湾/香港/澳门
    if (std::strcmp(country, "TW") == 0 || std::strcmp(country, "HK") == 0 ||
        std::strcmp(country, "MO") == 0) {
        g_lang = Lang::kZhTw;
    } else {
        g_lang = Lang::kZhCn;
    }
    __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "language: zh-%s -> %s",
                        country[0] ? country : "??",
                        g_lang == Lang::kZhTw ? "Traditional Chinese"
                                              : "Simplified Chinese");
}

Lang lang()
{
    return g_lang;
}

const Texts& get()
{
    return tableFor(g_lang);
}

}  // namespace strings

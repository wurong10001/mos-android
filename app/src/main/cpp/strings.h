#pragma once
#include <android/configuration.h>

#include <string>

// 界面文本本地化: 错误页按系统语言显示 (简体中文 / 繁體中文 / English)
// 日志一律用英语 (见 log.h)
namespace strings {

enum class Lang { kEn, kZhCn, kZhTw };

// 根据 AConfiguration 检测语言; main 里调用一次
void init(AConfiguration* config);

Lang lang();

struct Texts {
    // 错误页标题
    std::string errCrashPrev;    // 上次启动崩溃
    std::string errWasmLoad;     // wasm 加载失败
    std::string errWasmInit;     // wasm init 失败
    std::string errWasmRender;   // wasm 渲染失败
    std::string errWasmEmpty;    // wasm 模块为空
    // 正文
    std::string bodyCrashPrev;   // 崩溃报告前言
    std::string bodyAssetMissing;// assets 缺失说明
    std::string bodyDetails;     // 动态错误详情的前导
    // 错误页界面
    std::string hintTap;         // 轻触重试
    std::string hintLongPress;   // 长按清日志
    std::string hintFullLog;     // 完整日志位置
};

const Texts& get();

}  // namespace strings

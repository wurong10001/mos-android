#pragma once
#include <android/native_window.h>

#include <string>
#include <vector>

// 原生错误页: 直接在 Surface 上用位图字体渲染文字
//   - 打开时传入标题和正文 (自动按屏宽换行)
//   - 轻触屏幕 = 重试; 长按 (>1s) = 清除崩溃日志并重试
struct ErrorPage {
    bool active = false;
    bool exitRequested = false;
    bool clearLogRequested = false;

    std::string title;
    std::vector<std::string> lines;

    void open(const char* pageTitle, const std::string& body);
    void close();

    // 渲染一帧 (窗口未就绪时安全返回)
    void draw(ANativeWindow* window);

    // 触摸事件: longPress = 长按
    void onTouch(bool longPress);
};

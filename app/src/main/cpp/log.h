#pragma once

// 简易日志: 同时输出到 logcat 和文件 (<filesDir>/mos.log)
// 内存中保留最近若干行, 供错误页显示
namespace moslog {

// filesDir: 日志目录 (优先外部应用专属目录 externalDataPath)
void init(const char* filesDir);

// 探测目录是否可写 (用于选择日志目录)
bool canWrite(const char* dir);

// 写一条日志, 用法: LOGI("value=%d", x)
void write(const char* level, const char* fmt, ...);

// 最近的内存日志 (最多 maxLines 行, 从旧到新)
void recentLines(int maxLines, const char** out, int* outCount);

// 日志文件路径 (可能为空, 表示未初始化)
const char* filePath();

}  // namespace moslog

#define LOGI(...) moslog::write("I", __VA_ARGS__)
#define LOGW(...) moslog::write("W", __VA_ARGS__)
#define LOGE(...) moslog::write("E", __VA_ARGS__)

#pragma once
#include <string>

// 原生崩溃捕获:
//   - 注册 SIGSEGV/SIGABRT/SIGBUS/SIGFPE/SIGILL/SIGTRAP 信号处理
//   - C++ 未捕获异常 (std::set_terminate)
//   - 崩溃时把报告写入 <filesDir>/crash.log, 然后按默认行为终止
//   - 下次启动时 main 检测到 crash.log -> 展示错误页
namespace crash {

void install(const char* filesDir);

// 是否存在上次的崩溃报告
bool hasPendingReport(const char* filesDir);

// 读出报告内容并删除文件
std::string takeReport(const char* filesDir);

void clearReport(const char* filesDir);

// 非信号类错误 (如 wasm 加载失败) 也走同一套报告
void writeReport(const char* reason, const char* detail);

}  // namespace crash

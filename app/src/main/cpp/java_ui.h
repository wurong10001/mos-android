#pragma once

struct android_app;

// 通过 JNI 启动 Java 错误页 (org.wurong.mos.ErrorActivity)
//   errorKey: crash_prev / wasm_load / wasm_init / wasm_render / wasm_empty / asset_missing
//   details:  动态详情 (崩溃报告内容或 wasm3 错误信息), 可为 nullptr
//   logPath / crashPath: 日志/崩溃报告文件路径, 可为 nullptr
// 返回 false 表示启动失败 (调用方应退回日志记录)
bool ui_show_error(struct android_app* app, const char* errorKey,
                   const char* details, const char* logPath,
                   const char* crashPath);

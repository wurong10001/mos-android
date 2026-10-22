#pragma once
// mos-repo 专属软件源客户端 (mos-android)
//
// 功能: 拉取源索引 + 下载文件到应用私有目录 <internalDataPath>/user/tmp/，
//       全程 SHA256 校验，校验失败不落盘。HTTP/摘要通过 JNI 委托
//       org.wurong.mos.RepoHttp（NDK 侧无 TLS 库）。
// 本轮只做下载逻辑；安装逻辑（如 .wasm 移入加载目录并交给 engineLoadWasm、
// .apk 触发安装）后续在 Download 调用方扩展。
//
// 线程: 必须在 native_app_glue 主循环线程以外的线程调用（会阻塞）。
//
// 用法示例:
//   std::vector<mosrepo::Item> items; std::string err;
//   if (mosrepo::LoadIndex(app, items, err)) {
//       for (auto& it : items) if (it.category == "mosapp") { ... }
//       std::string dest;
//       mosrepo::Download(app, items[0], dest, err);  // dest = user/tmp/<name>
//   }

#include <string>
#include <vector>

struct android_app;

namespace mosrepo {

struct Item {
    std::string name;      // 文件名
    std::string path;      // 仓库内路径 (uploads/<category>/...)
    std::string category;  // wallpaper / mosapp / ...
    std::string ext;       // 小写扩展名
    std::string url;       // raw 直链（失败自动切 jsDelivr）
    std::string sha256;    // 索引记录的小写十六进制 SHA-256
    long long size = 0;    // 字节数
};

// <internalDataPath>/user/tmp （RepoHttp.writeFile 负责建目录）
std::string TmpDir(struct android_app* app);

// 拉取 index.tsv 解析为列表；网络失败先 raw 后镜像。索引为空时返回 true 且 items 为空。
bool LoadIndex(struct android_app* app, std::vector<Item>& items, std::string& err);

// 下载 item 并校验 SHA256，成功写入 TmpDir 并返回 destPath。
// 校验失败/网络失败/写盘失败 -> false + err，且不保留半成品文件（数据仅在内存中校验后才落盘）。
bool Download(struct android_app* app, const Item& item, std::string& destPath, std::string& err);

}  // namespace mosrepo

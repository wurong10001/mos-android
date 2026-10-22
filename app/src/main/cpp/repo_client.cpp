#include "repo_client.h"

#include <android/log.h>
#include <android_native_app_glue.h>
#include <jni.h>

#include <cctype>
#include <cstdlib>
#include <string>
#include <vector>

#define MOSREPO_LOGI(...) __android_log_print(ANDROID_LOG_INFO, "MOSRepo", __VA_ARGS__)
#define MOSREPO_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "MOSRepo", __VA_ARGS__)

namespace mosrepo {
namespace {

constexpr int kIndexTimeoutMs = 15000;
constexpr int kDownloadTimeoutMs = 60000;

const char kRepoPrefix[] =
    "https://raw.githubusercontent.com/wurong10001/mos-repo/main/";
const char kCdnPrefix[] =
    "https://cdn.jsdelivr.net/gh/wurong10001/mos-repo@main/";

std::string IndexRawUrl() { return std::string(kRepoPrefix) + "index.tsv"; }
std::string IndexCdnUrl() { return std::string(kCdnPrefix) + "index.tsv"; }

std::string ToCdn(const std::string& url) {
    const std::string p(kRepoPrefix);
    if (url.rfind(p, 0) == 0) return std::string(kCdnPrefix) + url.substr(p.size());
    return url;
}

JNIEnv* GetJNIEnv(struct android_app* app) {
    if (app == nullptr) return nullptr;
    JavaVM* vm = reinterpret_cast<JavaVM*>(app->dataSpace);
    if (vm == nullptr) return nullptr;
    JNIEnv* env = nullptr;
    if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) == JNI_OK) return env;
    JavaVMAttachArgs args;
    args.version = JNI_VERSION_1_6;
    args.name = const_cast<char*>("MOSRepoThread");
    args.group = nullptr;
    if (vm->AttachCurrentThread(&env, &args) == JNI_OK) return env;
    return nullptr;
}

// 若存在挂起的 Java 异常则清除并返回其 message（用于把 IOException 等透出到 err）
std::string TakeException(JNIEnv* env) {
    if (!env->ExceptionCheck()) return "";
    jthrowable exc = env->ExceptionOccurred();
    env->ExceptionClear();
    std::string out;
    jclass throwable = env->FindClass("java/lang/Throwable");
    if (throwable != nullptr) {
        jmethodID getMessage =
            env->GetMethodID(throwable, "getMessage", "()Ljava/lang/String;");
        jstring msg =
            static_cast<jstring>(env->CallObjectMethod(exc, getMessage));
        if (msg != nullptr) {
            const char* c = env->GetStringUTFChars(msg, nullptr);
            if (c != nullptr) out = c;
            env->ReleaseStringUTFChars(msg, c);
            env->DeleteLocalRef(msg);
        }
        env->DeleteLocalRef(throwable);
    }
    env->DeleteLocalRef(exc);
    if (out.empty()) out = "Java 异常(无消息)";
    return out;
}

jclass FindRepoHttp(JNIEnv* env, std::string& err) {
    jclass cls = env->FindClass("org/wurong/mos/RepoHttp");
    if (cls == nullptr) {
        err = "找不到 org.wurong.mos.RepoHttp: " + TakeException(env);
        return nullptr;
    }
    return cls;
}

bool HttpGet(JNIEnv* env, const std::string& url, int timeoutMs,
             std::string& body, std::string& err) {
    jclass cls = FindRepoHttp(env, err);
    if (cls == nullptr) return false;
    jmethodID mid =
        env->GetStaticMethodID(cls, "httpGet", "(Ljava/lang/String;I)[B");
    if (mid == nullptr) {
        err = "RepoHttp.httpGet 方法签名缺失: " + TakeException(env);
        env->DeleteLocalRef(cls);
        return false;
    }
    jstring jurl = env->NewStringUTF(url.c_str());
    jbyteArray arr = static_cast<jbyteArray>(
        env->CallStaticObjectMethod(cls, mid, jurl, static_cast<jint>(timeoutMs)));
    env->DeleteLocalRef(jurl);
    std::string ex = TakeException(env);
    env->DeleteLocalRef(cls);
    if (!ex.empty()) {
        err = ex;
        return false;
    }
    if (arr == nullptr) {
        err = "httpGet 返回空: " + url;
        return false;
    }
    jsize len = env->GetArrayLength(arr);
    body.resize(static_cast<size_t>(len));
    if (len > 0) {
        env->GetByteArrayRegion(arr, 0, len,
                               reinterpret_cast<jbyte*>(&body[0]));
    }
    env->DeleteLocalRef(arr);
    return true;
}

std::string Sha256Hex(JNIEnv* env, const std::string& data) {
    std::string ignored;
    jclass cls = FindRepoHttp(env, ignored);
    if (cls == nullptr) return "";
    jmethodID mid =
        env->GetStaticMethodID(cls, "sha256Hex", "([B)Ljava/lang/String;");
    jbyteArray arr = env->NewByteArray(static_cast<jsize>(data.size()));
    if (!data.empty()) {
        env->SetByteArrayRegion(arr, 0, static_cast<jsize>(data.size()),
                                reinterpret_cast<const jbyte*>(&data[0]));
    }
    jstring hex =
        static_cast<jstring>(env->CallStaticObjectMethod(cls, mid, arr));
    std::string out;
    std::string ex = TakeException(env);
    if (!ex.empty()) MOSREPO_LOGE("sha256Hex 失败: %s", ex.c_str());
    if (hex != nullptr) {
        const char* c = env->GetStringUTFChars(hex, nullptr);
        if (c != nullptr) out = c;
        env->ReleaseStringUTFChars(hex, c);
        env->DeleteLocalRef(hex);
    }
    env->DeleteLocalRef(arr);
    env->DeleteLocalRef(cls);
    return out;
}

bool WriteFile(JNIEnv* env, const std::string& path, const std::string& data,
               std::string& err) {
    jclass cls = FindRepoHttp(env, err);
    if (cls == nullptr) return false;
    jmethodID mid = env->GetStaticMethodID(
        cls, "writeFile", "(Ljava/lang/String;[B)V");
    if (mid == nullptr) {
        err = "RepoHttp.writeFile 方法签名缺失: " + TakeException(env);
        env->DeleteLocalRef(cls);
        return false;
    }
    jstring jpath = env->NewStringUTF(path.c_str());
    jbyteArray arr = env->NewByteArray(static_cast<jsize>(data.size()));
    if (!data.empty()) {
        env->SetByteArrayRegion(arr, 0, static_cast<jsize>(data.size()),
                               reinterpret_cast<const jbyte*>(&data[0]));
    }
    env->CallStaticVoidMethod(cls, mid, jpath, arr);
    std::string ex = TakeException(env);
    env->DeleteLocalRef(arr);
    env->DeleteLocalRef(jpath);
    env->DeleteLocalRef(cls);
    if (!ex.empty()) {
        err = ex;
        return false;
    }
    return true;
}

// 白名单净化文件名，防止路径穿越/异常字符；非白名单字符替换为 '_'
std::string SafeName(const std::string& name) {
    std::string out;
    for (char c : name) {
        out.push_back((std::isalnum(static_cast<unsigned char>(c)) || c == '.' ||
                       c == '_' || c == '-')
                          ? c
                          : '_');
    }
    while (!out.empty() && out[0] == '.') out.erase(out.begin());
    if (out.size() > 120) out.resize(120);
    if (out.empty()) out = "download.bin";
    return out;
}

void SplitTabs(const std::string& line, std::vector<std::string>& out) {
    size_t pos = 0;
    while (true) {
        size_t tab = line.find('\t', pos);
        out.push_back(line.substr(
            pos, tab == std::string::npos ? std::string::npos : tab - pos));
        if (tab == std::string::npos) break;
        pos = tab + 1;
    }
}

}  // namespace

std::string TmpDir(struct android_app* app) {
    if (app == nullptr || app->activity == nullptr ||
        app->activity->internalDataPath == nullptr) {
        return "";
    }
    return std::string(app->activity->internalDataPath) + "/user/tmp";
}

bool LoadIndex(struct android_app* app, std::vector<Item>& items,
               std::string& err) {
    items.clear();
    JNIEnv* env = GetJNIEnv(app);
    if (env == nullptr) {
        err = "JNI attach 失败";
        return false;
    }
    std::string body;
    bool ok = HttpGet(env, IndexRawUrl(), kIndexTimeoutMs, body, err);
    if (!ok) {
        std::string err2;
        ok = HttpGet(env, IndexCdnUrl(), kIndexTimeoutMs, body, err2);
        if (!ok) err = err + " | 镜像也失败: " + err2;
    }
    if (!ok) return false;

    size_t pos = 0;
    while (pos < body.size()) {
        size_t nl = body.find('\n', pos);
        std::string line = body.substr(
            pos, nl == std::string::npos ? std::string::npos : nl - pos);
        pos = (nl == std::string::npos) ? body.size() : nl + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        std::vector<std::string> f;
        SplitTabs(line, f);
        if (f.size() < 7) {
            MOSREPO_LOGE("index.tsv 行格式异常，已跳过: %s", line.c_str());
            continue;
        }
        Item it;
        it.category = f[0];
        it.name = f[1];
        it.path = f[2];
        it.size = std::strtoll(f[3].c_str(), nullptr, 10);
        it.sha256 = f[4];
        it.ext = f[5];
        it.url = f[6];
        items.push_back(it);
    }
    MOSREPO_LOGI("索引加载完成: %zu 个文件", items.size());
    return true;
}

bool Download(struct android_app* app, const Item& item, std::string& destPath,
              std::string& err) {
    destPath.clear();
    std::string dir = TmpDir(app);
    if (dir.empty()) {
        err = "无法确定应用私有目录";
        return false;
    }
    JNIEnv* env = GetJNIEnv(app);
    if (env == nullptr) {
        err = "JNI attach 失败";
        return false;
    }

    std::string body;
    bool ok = HttpGet(env, item.url, kDownloadTimeoutMs, body, err);
    if (!ok) {
        std::string err2;
        ok = HttpGet(env, ToCdn(item.url), kDownloadTimeoutMs, body, err2);
        if (!ok) err = err + " | 镜像也失败: " + err2;
    }
    if (!ok) return false;

    if (item.size > 0 && static_cast<long long>(body.size()) != item.size) {
        MOSREPO_LOGE("大小与索引不符: %s 期望 %lld 实际 %zu", item.name.c_str(),
                     item.size, body.size());
    }

    // 数据仅在内存中校验，通过后才落盘（不存在半成品残留）
    std::string hex = Sha256Hex(env, body);
    if (hex.empty() || hex != item.sha256) {
        err = "SHA256 校验失败，未写入: " + item.name;
        MOSREPO_LOGE("%s", err.c_str());
        return false;
    }

    destPath = dir + "/" + SafeName(item.name);
    if (!WriteFile(env, destPath, body, err)) {
        err = "写入失败: " + err;
        return false;
    }
    MOSREPO_LOGI("下载成功: %s (%zu 字节, sha256=%s)", destPath.c_str(),
                 body.size(), hex.c_str());
    return true;
}

}  // namespace mosrepo

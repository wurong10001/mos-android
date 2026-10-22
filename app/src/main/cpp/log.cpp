#include "log.h"

#include <android/log.h>
#include <pthread.h>
#include <unistd.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

namespace moslog {
namespace {

pthread_mutex_t g_mutex = PTHREAD_MUTEX_INITIALIZER;
FILE* g_file = nullptr;
char g_path[600] = {0};
const int kKeepLines = 300;
std::vector<std::string> g_lines;

int priorityOf(const char* level)
{
    if (level[0] == 'W') return ANDROID_LOG_WARN;
    if (level[0] == 'E') return ANDROID_LOG_ERROR;
    return ANDROID_LOG_INFO;
}

}  // namespace

bool canWrite(const char* dir)
{
    if (dir == nullptr || dir[0] == '\0') return false;
    char path[600];
    snprintf(path, sizeof(path), "%s/.mos_write_test", dir);
    FILE* f = fopen(path, "w");
    if (f == nullptr) return false;
    fclose(f);
    unlink(path);
    return true;
}

void init(const char* filesDir)
{
    pthread_mutex_lock(&g_mutex);
    if (filesDir != nullptr && filesDir[0] != '\0') {
        snprintf(g_path, sizeof(g_path), "%s/mos.log", filesDir);
        if (g_file == nullptr) {
            g_file = fopen(g_path, "a");
        }
    }
    pthread_mutex_unlock(&g_mutex);
    LOGI("logger initialized (file: %s)", g_path[0] ? g_path : "<none>");
}

void write(const char* level, const char* fmt, ...)
{
    char msg[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);

    __android_log_print(priorityOf(level), "mos", "%s", msg);

    pthread_mutex_lock(&g_mutex);

    time_t t = time(nullptr);
    struct tm tmv;
    localtime_r(&t, &tmv);
    char line[1200];
    snprintf(line, sizeof(line), "%02d:%02d:%02d %s %s",
             tmv.tm_hour, tmv.tm_min, tmv.tm_sec, level, msg);

    if (g_file != nullptr) {
        fprintf(g_file, "%s\n", line);
        fflush(g_file);
    }

    g_lines.push_back(line);
    if (static_cast<int>(g_lines.size()) > kKeepLines) {
        g_lines.erase(g_lines.begin(), g_lines.end() - kKeepLines);
    }
    pthread_mutex_unlock(&g_mutex);
}

void recentLines(int maxLines, const char** out, int* outCount)
{
    pthread_mutex_lock(&g_mutex);
    static std::vector<std::string> snapshot;  // 静态缓冲, 保证指针存活
    snapshot.assign(g_lines.begin(), g_lines.end());
    pthread_mutex_unlock(&g_mutex);

    int total = static_cast<int>(snapshot.size());
    int start = total > maxLines ? total - maxLines : 0;
    int n = 0;
    for (int i = start; i < total; ++i) {
        out[n++] = snapshot[i].c_str();
    }
    *outCount = n;
}

const char* filePath()
{
    return g_path;
}

}  // namespace moslog

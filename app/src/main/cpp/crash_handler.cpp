#include "crash_handler.h"

#include <android/log.h>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <unistd.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <ucontext.h>
#include <sys/stat.h>
#include <sys/types.h>

#define LOG_TAG "crash"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

namespace crash {
namespace {

char g_reportPath[600] = {0};

const char* signalName(int sig)
{
    switch (sig) {
        case SIGSEGV: return "SIGSEGV (段错误: 空指针/野指针/栈溢出)";
        case SIGABRT: return "SIGABRT (断言失败或 abort)";
        case SIGBUS:  return "SIGBUS (总线错误)";
        case SIGFPE:  return "SIGFPE (算术错误, 如除零)";
        case SIGILL:  return "SIGILL (非法指令)";
        case SIGTRAP: return "SIGTRAP (断点/陷阱)";
        default:      return "UNKNOWN";
    }
}

// 信号处理里只能用 async-signal-safe 的调用, 这里直接用 open/write
void rawWrite(int fd, const char* s)
{
    if (s != nullptr) {
        size_t len = strlen(s);
        ssize_t ignored = write(fd, s, len);
        (void)ignored;
    }
}

// 从 ucontext 取故障 PC (Android bionic 没有 backtrace(), 用 dladdr 解析 PC)
uintptr_t faultPC(void* ctx)
{
    if (ctx == nullptr) return 0;
    const ucontext_t* uc = static_cast<const ucontext_t*>(ctx);
#if defined(__aarch64__)
    return uc->uc_mcontext.pc;
#elif defined(__arm__)
    return uc->uc_mcontext.arm_pc;
#elif defined(__x86_64__)
    return uc->uc_mcontext.gregs[REG_RIP];
#else
    return 0;
#endif
}

void writeReportRaw(int sig, void* addr, void* ctx)
{
    if (g_reportPath[0] == '\0') return;

    int fd = open(g_reportPath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return;

    char buf[512];
    rawWrite(fd, "==== mos-android 崩溃报告 ====\n");
    snprintf(buf, sizeof(buf), "信号: %s (%d)\n", signalName(sig), sig);
    rawWrite(fd, buf);
    if (addr != nullptr) {
        snprintf(buf, sizeof(buf), "故障地址: %p\n", addr);
        rawWrite(fd, buf);
    }

    // 尽量定位崩溃代码位置 (dladdr 不需要分配, 信号处理里可用)
    const uintptr_t pc = faultPC(ctx);
    if (pc != 0) {
        snprintf(buf, sizeof(buf), "崩溃 PC: 0x%llx\n",
                 static_cast<unsigned long long>(pc));
        rawWrite(fd, buf);
        Dl_info info;
        memset(&info, 0, sizeof(info));
        if (dladdr(reinterpret_cast<void*>(pc), &info) != 0 && info.dli_fname) {
            snprintf(buf, sizeof(buf), "所在库: %s\n", info.dli_fname);
            rawWrite(fd, buf);
            if (info.dli_sname != nullptr) {
                snprintf(buf, sizeof(buf), "最近符号: %s+0x%llx\n", info.dli_sname,
                         static_cast<unsigned long long>(
                             pc - reinterpret_cast<uintptr_t>(info.dli_fbase)));
                rawWrite(fd, buf);
            }
        }
    }

    rawWrite(fd, "完整调用栈请查看系统 tombstone 或 logcat\n");
    rawWrite(fd, "===================\n");
    close(fd);
}

void signalHandler(int sig, siginfo_t* info, void* ctx)
{
    writeReportRaw(sig, info ? info->si_addr : nullptr, ctx);
    // 恢复默认处理并重新触发, 让系统照常生成 tombstone
    signal(sig, SIG_DFL);
    raise(sig);
}

void terminateHandler()
{
    writeReportRaw(SIGABRT, nullptr, nullptr);
    abort();
}

}  // namespace

void install(const char* filesDir)
{
    if (filesDir != nullptr && filesDir[0] != '\0') {
        snprintf(g_reportPath, sizeof(g_reportPath), "%s/crash.log", filesDir);
    }

    // 信号处理在独立栈上执行, 避免栈溢出时无法处理
    static char altstack[SIGSTKSZ * 4];
    stack_t ss;
    ss.ss_sp = altstack;
    ss.ss_size = sizeof(altstack);
    ss.ss_flags = 0;
    sigaltstack(&ss, nullptr);

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = signalHandler;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&sa.sa_mask);

    const int sigs[] = {SIGSEGV, SIGABRT, SIGBUS, SIGFPE, SIGILL, SIGTRAP};
    for (int s : sigs) {
        sigaction(s, &sa, nullptr);
    }

    std::set_terminate(terminateHandler);
    LOGI("crash handler installed");
}

bool hasPendingReport(const char* filesDir)
{
    if (filesDir == nullptr) return false;
    char path[600];
    snprintf(path, sizeof(path), "%s/crash.log", filesDir);
    struct stat st;
    return stat(path, &st) == 0 && st.st_size > 0;
}

std::string takeReport(const char* filesDir)
{
    std::string content;
    if (filesDir == nullptr) return content;

    char path[600];
    snprintf(path, sizeof(path), "%s/crash.log", filesDir);
    FILE* f = fopen(path, "r");
    if (f != nullptr) {
        char buf[512];
        size_t n;
        while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
            content.append(buf, n);
        }
        fclose(f);
    }
    clearReport(filesDir);
    return content;
}

void clearReport(const char* filesDir)
{
    if (filesDir == nullptr) return;
    char path[600];
    snprintf(path, sizeof(path), "%s/crash.log", filesDir);
    unlink(path);
}

void writeReport(const char* reason, const char* detail)
{
    if (g_reportPath[0] == '\0') return;
    FILE* f = fopen(g_reportPath, "w");
    if (f == nullptr) return;
    fprintf(f, "==== mos-android 错误报告 ====\n%s\n", reason);
    if (detail != nullptr) {
        fprintf(f, "%s\n", detail);
    }
    fprintf(f, "===================\n");
    fclose(f);
}

}  // namespace crash

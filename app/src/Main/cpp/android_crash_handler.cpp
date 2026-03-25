#include <android/log.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <unwind.h>

namespace {
constexpr const char* kCrashTag = "xemu-android";
constexpr size_t kPathMax = 512;

static char g_inline_aio_flag_path[kPathMax];

static int GetTid() {
  return static_cast<int>(syscall(SYS_gettid));
}

struct BacktraceState {
  void** addrs;
  int count;
  int max;
};

static _Unwind_Reason_Code UnwindCallback(struct _Unwind_Context* ctx, void* arg) {
  BacktraceState* state = static_cast<BacktraceState*>(arg);
  if (state->count >= state->max) {
    return _URC_END_OF_STACK;
  }
  uintptr_t pc = _Unwind_GetIP(ctx);
  if (pc != 0) {
    state->addrs[state->count++] = reinterpret_cast<void*>(pc);
  }
  return _URC_NO_REASON;
}

static void LogBacktrace() {
  void* addrs[64];
  BacktraceState state{addrs, 0, static_cast<int>(sizeof(addrs) / sizeof(addrs[0]))};
  _Unwind_Backtrace(UnwindCallback, &state);
  for (int i = 0; i < state.count; ++i) {
    Dl_info info;
    if (dladdr(addrs[i], &info) && info.dli_fname) {
      uintptr_t base = reinterpret_cast<uintptr_t>(info.dli_fbase);
      uintptr_t pc = reinterpret_cast<uintptr_t>(addrs[i]);
      uintptr_t rel = (base != 0 && pc >= base) ? (pc - base) : 0;
      const char* sym = info.dli_sname ? info.dli_sname : "?";
      __android_log_print(ANDROID_LOG_ERROR, kCrashTag,
                          "  #%02d pc %p %s (%s+0x%zx)",
                          i, addrs[i], info.dli_fname, sym,
                          static_cast<size_t>(rel));
    } else {
      __android_log_print(ANDROID_LOG_ERROR, kCrashTag, "  #%02d pc %p", i, addrs[i]);
    }
  }
}

static void MarkInlineAioRequired() {
  if (g_inline_aio_flag_path[0] == '\0') {
    return;
  }

  int fd = open(g_inline_aio_flag_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (fd < 0) {
    return;
  }

  static const char kValue[] = "1\n";
  ssize_t ignored = write(fd, kValue, sizeof(kValue) - 1);
  (void)ignored;
  close(fd);
}

static void CrashHandler(int sig, siginfo_t* info, void* ucontext) {
  (void)info;
  (void)ucontext;
  if (sig == SIGILL) {
    MarkInlineAioRequired();
  }
  __android_log_print(ANDROID_LOG_ERROR, kCrashTag,
                      "Caught signal %d in tid %d", sig, GetTid());
  LogBacktrace();
  signal(sig, SIG_DFL);
  raise(sig);
}

static void InstallCrashHandlers() {
  struct sigaction sa;
  memset(&sa, 0, sizeof(sa));
  sa.sa_sigaction = CrashHandler;
  sa.sa_flags = SA_SIGINFO | SA_RESETHAND;
  sigaction(SIGABRT, &sa, nullptr);
  sigaction(SIGILL, &sa, nullptr);
  sigaction(SIGSEGV, &sa, nullptr);
}
}  // namespace

extern "C" void xemu_android_set_inline_aio_crash_flag_path(const char* path) {
  if (!path) {
    g_inline_aio_flag_path[0] = '\0';
    return;
  }

  size_t len = strnlen(path, sizeof(g_inline_aio_flag_path) - 1);
  memcpy(g_inline_aio_flag_path, path, len);
  g_inline_aio_flag_path[len] = '\0';
}

__attribute__((constructor)) static void InstallCrashHandlersOnLoad() {
  InstallCrashHandlers();
}

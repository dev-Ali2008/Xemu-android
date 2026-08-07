#include <SDL.h>
#include <SDL_main.h>
#include <SDL_system.h>

#include <GLES3/gl3.h>
#include <toml++/toml.h>

#include <android/log.h>
#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>
#include <jni.h>

#include <atomic>
#include <climits>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <unwind.h>
#include <sys/types.h>

#include "xemu-settings.h"
#include "hw/xbox/nv2a/debug.h"

extern "C" void xemu_set_fp_safe(bool enable);
extern "C" bool xemu_get_fp_safe(void);
extern "C" void xemu_set_fast_fences(bool enable);
extern "C" bool xemu_get_fast_fences(void);
extern "C" void xemu_set_fp_jit(bool enable);
extern "C" bool xemu_get_fp_jit(void);
extern "C" void xemu_set_draw_reorder(bool enable);
extern "C" bool xemu_get_draw_reorder(void);
extern "C" void xemu_set_draw_merge(bool enable);
extern "C" bool xemu_get_draw_merge(void);
extern "C" void xemu_set_bindless_textures(bool enable);
extern "C" bool xemu_get_bindless_textures(void);
extern "C" void xemu_set_async_compile(bool enable);
extern "C" bool xemu_get_async_compile(void);
extern "C" void xemu_set_frame_skip(bool enable);
extern "C" bool xemu_get_frame_skip(void);
extern "C" void xemu_set_submit_frames(int count);
extern "C" int xemu_get_submit_frames(void);
extern "C" void xemu_set_tier1_threshold(int value);
extern "C" int xemu_get_tier1_threshold(void);
extern "C" void xemu_set_tier1_budget(int value);
extern "C" int xemu_get_tier1_budget(void);
extern "C" void xemu_set_tier1_opt_mask(int value);
extern "C" int xemu_get_tier1_opt_mask(void);
extern "C" void xemu_set_tier1_enable(int value);
extern "C" int xemu_get_tier1_enable(void);
extern "C" bool runstate_is_running(void);
extern "C" void xemu_android_pause_emulation(void);
extern "C" void xemu_android_resume_emulation(void);
extern "C" void xemu_android_request_exit(void);
extern "C" void xemu_android_set_qemu_thread_finished(bool finished);
extern "C" void xemu_android_set_display_mode_setting(int mode);

extern "C" bool xemu_android_is_debug_logging_enabled(void)
{
    // Gates the GL error probes in the pgraph GL backend. Those call
    // glGetError(), a synchronisation point that makes the driver flush and can
    // stall the CPU on the GPU; they sit in per-draw paths, so always-on they
    // cost thousands of forced syncs per frame.
    //
    // XEMU_GL_DEBUG is set by MainActivity from the debug-logging preference.
    // Read once: getenv() in a per-draw path would be overhead of its own.
    static int cached = -1;

    if (cached < 0) {
        const char *value = getenv("XEMU_GL_DEBUG");
        cached = (value != NULL && value[0] == '1') ? 1 : 0;
    }
    return cached == 1;
}

/* Stub: old GL display readback — replaced by texture-based path in new GL display.c.
 * ui/xemu.c still calls this; returning false makes it use the fallback. */
extern "C" bool nv2a_android_copy_readback(uint8_t **buffer, size_t *buffer_size,
                                            int *width, int *height)
{
    (void)buffer; (void)buffer_size; (void)width; (void)height;
    return false;
}

#ifdef CONFIG_VULKAN
#include <adrenotools/driver.h>
#include <dlfcn.h>
#include <volk.h>

static void* g_custom_vulkan_library = nullptr;
/* True only when a user-installed GPU driver ZIP was loaded (e.g. Turnip). */
static bool g_vulkan_custom_driver_zip_loaded = false;

extern "C" PFN_vkGetInstanceProcAddr xemu_android_get_vk_proc_addr(void)
{
    if (!g_custom_vulkan_library) {
        return nullptr;
    }
    return reinterpret_cast<PFN_vkGetInstanceProcAddr>(
        dlsym(g_custom_vulkan_library, "vkGetInstanceProcAddr"));
}

extern "C" bool xemu_android_vulkan_custom_driver_zip_loaded(void)
{
    return g_vulkan_custom_driver_zip_loaded;
}
#else
extern "C" bool xemu_android_vulkan_custom_driver_zip_loaded(void)
{
    return false;
}
#endif

static int g_dvd_fd = -1;
static std::atomic_bool g_return_to_library_on_exit{false};

namespace {
constexpr const char* kLogTag = "xemu-android";
// Keep this in sync with the Android UI preference file. MainActivity runs in
// a separate process, but SharedPreferences remain app-wide as long as both
// processes open the same named file.
constexpr const char* kPrefsName = "xaniteog_prefs";

static JNIEnv* GetEnv();
static jobject GetActivity(JNIEnv* env);
static bool HasException(JNIEnv* env, const char* context);

static void LogInfo(const char* msg) {
  __android_log_print(ANDROID_LOG_INFO, kLogTag, "%s", msg);
}

static void LogInfoFmt(const char* fmt, const char* detail) {
  __android_log_print(ANDROID_LOG_INFO, kLogTag, fmt, detail);
}

static void LogInfoInt(const char* fmt, int value) {
  __android_log_print(ANDROID_LOG_INFO, kLogTag, fmt, value);
}

static void LogError(const char* msg) {
  __android_log_print(ANDROID_LOG_ERROR, kLogTag, "%s", msg);
}

static void LogErrorInt(const char* fmt, int value) {
  __android_log_print(ANDROID_LOG_ERROR, kLogTag, fmt, value);
}

static void LogErrorFmt(const char* fmt, const char* detail) {
  __android_log_print(ANDROID_LOG_ERROR, kLogTag, fmt, detail);
}

// --- stdout/stderr -> logcat bridge -----------------------------------
// A significant amount of engine-level diagnostic output — Vulkan physical
// device enumeration, required-extension checks in instance.c
// ("Available physical devices:", "required device extension not found:"),
// and renderer fallback decisions in pgraph.c — is written with plain
// fprintf(stderr, ...) / fprintf(stdout, ...) rather than
// __android_log_print(). On Android, a process's stdout/stderr file
// descriptors are NOT connected to logcat by default, so all of that
// output was previously discarded entirely: it never reached `adb logcat`
// on device, even though it's often the single most useful signal for
// diagnosing exactly why a renderer failed to initialize (e.g. a required
// Vulkan extension missing on one GPU but not another).
//
// This installs a pipe-based bridge: stdout/stderr are redirected into a
// pipe, and a background thread reads lines from that pipe and re-emits
// them via __android_log_print under the "xemu-native-stdio" tag. Must be
// called as early as possible (top of SDL_main) so nothing is lost.
static int g_stdio_pipe_read_fd = -1;

static void* StdioLogcatPumpThreadMain(void* /*arg*/) {
  char buf[1024];
  std::string line;
  ssize_t n;
  while ((n = read(g_stdio_pipe_read_fd, buf, sizeof(buf))) > 0) {
    for (ssize_t i = 0; i < n; i++) {
      if (buf[i] == '\n') {
        __android_log_print(ANDROID_LOG_INFO, "xemu-native-stdio", "%s", line.c_str());
        line.clear();
      } else if (buf[i] != '\r') {
        line.push_back(buf[i]);
      }
    }
  }
  if (!line.empty()) {
    __android_log_print(ANDROID_LOG_INFO, "xemu-native-stdio", "%s", line.c_str());
  }
  return nullptr;
}

static void RedirectStdioToLogcat() {
  int pipe_fds[2];
  if (pipe(pipe_fds) != 0) {
    __android_log_print(ANDROID_LOG_ERROR, kLogTag,
                        "RedirectStdioToLogcat: pipe() failed: %s", strerror(errno));
    return;
  }
  g_stdio_pipe_read_fd = pipe_fds[0];
  int write_fd = pipe_fds[1];

  // Line-buffer stdout (it's fully-buffered by default when not a tty,
  // which would delay/batch messages); stderr is already unbuffered.
  setvbuf(stdout, nullptr, _IOLBF, 0);

  dup2(write_fd, STDOUT_FILENO);
  dup2(write_fd, STDERR_FILENO);
  close(write_fd);

  pthread_t thread;
  if (pthread_create(&thread, nullptr, StdioLogcatPumpThreadMain, nullptr) == 0) {
    pthread_detach(thread);
    __android_log_print(ANDROID_LOG_INFO, kLogTag,
        "RedirectStdioToLogcat: stdout/stderr now mirrored to logcat, tag=xemu-native-stdio");
  } else {
    __android_log_print(ANDROID_LOG_ERROR, kLogTag,
        "RedirectStdioToLogcat: pthread_create failed: %s", strerror(errno));
  }
}

// --- native (signal-level) crash handler --------------------------------
// The JVM-level crash handler in XaniteApplication.kt only sees Kotlin/Java
// exceptions. It never sees a SIGSEGV/SIGABRT from this native library
// itself — which, for an emulator whose entire rendering pipeline is C/C++
// and Vulkan, is the far more likely crash type. This installs a signal
// handler that writes a crash report to the exact same directory
// CrashReportManager.kt scans on the next app launch (files/x1box/debug-logs,
// filename prefixed "native-crash-") so the "copy the crash to clipboard"
// feature covers native crashes too, not just Kotlin ones.
//
// Everything inside the handler itself is restricted to async-signal-safe
// operations only (no malloc, no JNI, no snprintf/std::string) since the
// process can be in an arbitrary, possibly-corrupted state when a signal
// fires. The handler always finishes by restoring whatever handler was
// previously installed (usually the OS's own debuggerd/tombstone hook) and
// re-raising the signal, so this only ever adds information — it never
// suppresses or changes how the process actually dies.
namespace {

char g_native_crash_dir[512] = {0};
constexpr size_t kAltStackSize = 32 * 1024;
uint8_t g_alt_stack[kAltStackSize];

struct sigaction g_prev_sigsegv {};
struct sigaction g_prev_sigabrt {};
struct sigaction g_prev_sigbus {};
struct sigaction g_prev_sigill {};
struct sigaction g_prev_sigfpe {};

// Async-signal-safe unsigned-integer-to-decimal-string (no snprintf).
int SafeUIntToStr(unsigned long value, char* out, int out_size) {
  char tmp[24];
  int n = 0;
  if (value == 0) {
    tmp[n++] = '0';
  } else {
    while (value > 0 && n < static_cast<int>(sizeof(tmp))) {
      tmp[n++] = static_cast<char>('0' + (value % 10));
      value /= 10;
    }
  }
  int written = 0;
  for (int i = n - 1; i >= 0 && written < out_size; i--) {
    out[written++] = tmp[i];
  }
  return written;
}

const char* SignalName(int sig) {
  switch (sig) {
    case SIGSEGV: return "SIGSEGV";
    case SIGABRT: return "SIGABRT";
    case SIGBUS:  return "SIGBUS";
    case SIGILL:  return "SIGILL";
    case SIGFPE:  return "SIGFPE";
    default:      return "UNKNOWN";
  }
}

struct sigaction* PreviousHandlerFor(int sig) {
  switch (sig) {
    case SIGSEGV: return &g_prev_sigsegv;
    case SIGABRT: return &g_prev_sigabrt;
    case SIGBUS:  return &g_prev_sigbus;
    case SIGILL:  return &g_prev_sigill;
    case SIGFPE:  return &g_prev_sigfpe;
    default:      return nullptr;
  }
}

struct BacktraceState {
  void** current;
  void** end;
};

_Unwind_Reason_Code UnwindBacktraceCallback(struct _Unwind_Context* context, void* arg) {
  auto* state = static_cast<BacktraceState*>(arg);
  uintptr_t pc = _Unwind_GetIP(context);
  if (pc) {
    if (state->current == state->end) {
      return _URC_END_OF_STACK;
    }
    *state->current++ = reinterpret_cast<void*>(pc);
  }
  return _URC_NO_REASON;
}

// Portable replacement for glibc's backtrace(): Android's bionic libc does
// not reliably expose <execinfo.h>'s backtrace()/backtrace_symbols_fd()
// (confirmed by a real NDK r26 build failure — "use of undeclared
// identifier 'backtrace'"). _Unwind_Backtrace from <unwind.h> is the actual
// portable primitive available across all supported NDK/API levels — it's
// the same mechanism libc++abi itself uses for exception unwinding, is a
// pure stack-walk with no heap allocation, and is safe to call from a
// signal handler. It only yields raw PC addresses, not symbol names — the
// crash file notes how to symbolicate them afterward with ndk-stack.
int CaptureBacktrace(void** out_frames, int max_frames) {
  BacktraceState state{out_frames, out_frames + max_frames};
  _Unwind_Backtrace(UnwindBacktraceCallback, &state);
  return static_cast<int>(state.current - out_frames);
}

// Async-signal-safe pointer-to-hex-string (mirrors SafeUIntToStr above,
// separate since a full 64-bit address is wider than SafeUIntToStr's use).
int SafePtrToHex(void* ptr, char* out, int out_size) {
  static const char kHexDigits[] = "0123456789abcdef";
  auto addr = reinterpret_cast<uintptr_t>(ptr);
  char tmp[2 * sizeof(uintptr_t)];
  int n = 0;
  bool started = false;
  for (int shift = static_cast<int>(sizeof(uintptr_t) * 8 - 4); shift >= 0; shift -= 4) {
    int digit = (addr >> shift) & 0xF;
    if (digit != 0) started = true;
    if (started || shift == 0) {
      tmp[n++] = kHexDigits[digit];
    }
  }
  int written = 0;
  for (int i = 0; i < n && written < out_size; i++) {
    out[written++] = tmp[i];
  }
  return written;
}

void NativeCrashSignalHandler(int sig, siginfo_t* info, void* ucontext) {
  (void)ucontext;

  if (g_native_crash_dir[0] != '\0') {
    char path[600];
    int p = 0;
    for (int i = 0; g_native_crash_dir[i] != '\0' && p < static_cast<int>(sizeof(path)) - 1; i++) {
      path[p++] = g_native_crash_dir[i];
    }
    static const char kPrefix[] = "/native-crash-";
    for (int i = 0; kPrefix[i] != '\0' && p < static_cast<int>(sizeof(path)) - 1; i++) {
      path[p++] = kPrefix[i];
    }
    p += SafeUIntToStr(static_cast<unsigned long>(getpid()), path + p,
                        static_cast<int>(sizeof(path)) - p - 1);
    static const char kSuffix[] = ".log";
    for (int i = 0; kSuffix[i] != '\0' && p < static_cast<int>(sizeof(path)) - 1; i++) {
      path[p++] = kSuffix[i];
    }
    path[p] = '\0';

    int fd = open(path, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (fd >= 0) {
      char numbuf[24];
      int n;

      static const char kHeader[] = "=== Xanite NATIVE crash report ===\nSignal: ";
      write(fd, kHeader, sizeof(kHeader) - 1);
      const char* sigName = SignalName(sig);
      write(fd, sigName, strlen(sigName));

      static const char kPidLabel[] = "\nPID: ";
      write(fd, kPidLabel, sizeof(kPidLabel) - 1);
      n = SafeUIntToStr(static_cast<unsigned long>(getpid()), numbuf, sizeof(numbuf));
      write(fd, numbuf, n);

      static const char kTidLabel[] = "\nTID: ";
      write(fd, kTidLabel, sizeof(kTidLabel) - 1);
      n = SafeUIntToStr(static_cast<unsigned long>(gettid()), numbuf, sizeof(numbuf));
      write(fd, numbuf, n);

      if (sig == SIGSEGV || sig == SIGBUS) {
        static const char kAddrLabel[] = "\nFault address: 0x";
        write(fd, kAddrLabel, sizeof(kAddrLabel) - 1);
        auto addr = reinterpret_cast<uintptr_t>(info->si_addr);
        static const char kHexDigits[] = "0123456789abcdef";
        char hexbuf[2 * sizeof(uintptr_t)];
        int hn = 0;
        bool started = false;
        for (int shift = static_cast<int>(sizeof(uintptr_t) * 8 - 4); shift >= 0; shift -= 4) {
          int digit = (addr >> shift) & 0xF;
          if (digit != 0) started = true;
          if (started || shift == 0) {
            hexbuf[hn++] = kHexDigits[digit];
          }
        }
        write(fd, hexbuf, hn);
      } else {
        // SIGABRT/SIGILL/SIGFPE are software-raised, not hardware memory
        // faults — info->si_addr isn't a meaningful address for these and
        // printing it as one is misleading (it's often just wherever
        // abort()/raise() happened to be, not anything you can act on).
        static const char kNoAddr[] =
            "\n(no fault address - not a memory fault; likely abort()/assert, "
            "an unhandled C++ exception, or an NDK FORTIFY check)";
        write(fd, kNoAddr, sizeof(kNoAddr) - 1);
      }

      static const char kBtLabel[] =
          "\n\nBacktrace (raw addresses — symbolicate with "
          "`ndk-stack -sym <path-to-unstripped-libxemu.so>` "
          "or addr2line):\n";
      write(fd, kBtLabel, sizeof(kBtLabel) - 1);

      void* frames[64];
      int frame_count = CaptureBacktrace(frames, 64);
      if (frame_count == 0) {
        static const char kNoFrames[] =
            "  (unwind produced no frames - this can happen for SIGABRT since "
            "_Unwind_Backtrace needs call-frame info for every frame it walks, "
            "and the abort() call path doesn't always have it. Check the real "
            "system tombstone instead: adb logcat -b crash right after this "
            "happens, or pull it via ApplicationExitInfo.)\n";
        write(fd, kNoFrames, sizeof(kNoFrames) - 1);
      }
      for (int i = 0; i < frame_count; i++) {
        static const char kFrameLabel[] = "  #";
        write(fd, kFrameLabel, sizeof(kFrameLabel) - 1);
        n = SafeUIntToStr(static_cast<unsigned long>(i), numbuf, sizeof(numbuf));
        write(fd, numbuf, n);

        static const char kPcLabel[] = " pc 0x";
        write(fd, kPcLabel, sizeof(kPcLabel) - 1);
        char hexbuf[2 * sizeof(uintptr_t)];
        int hn = SafePtrToHex(frames[i], hexbuf, sizeof(hexbuf));
        write(fd, hexbuf, hn);
        write(fd, "\n", 1);
      }

      close(fd);
    }
  }

  // Restore whatever was previously installed (normally the OS's own
  // debuggerd hook, set up before this library ever loaded) and re-raise,
  // so the process terminates exactly as it would have without this
  // handler — a tombstone/ANR dialog still happens as usual.
  struct sigaction* prev = PreviousHandlerFor(sig);
  if (prev) {
    sigaction(sig, prev, nullptr);
  }
  raise(sig);
}

void InstallNativeCrashHandler(const char* files_dir) {
  if (files_dir && files_dir[0] != '\0') {
    static const char kSubdir[] = "/x1box/debug-logs";
    size_t len = strlen(files_dir);
    size_t max_len = sizeof(g_native_crash_dir) - 1 - (sizeof(kSubdir) - 1);
    if (len > max_len) len = max_len;
    memcpy(g_native_crash_dir, files_dir, len);
    g_native_crash_dir[len] = '\0';
    strcat(g_native_crash_dir, kSubdir);
    mkdir(g_native_crash_dir, 0755);  // best-effort; Kotlin side also mkdirs()
  }

  // Run the handler on its own stack: if the crash is a stack overflow, the
  // thread's normal stack is already exhausted and a handler running on it
  // would immediately fault again before writing anything.
  static stack_t alt_stack{};
  alt_stack.ss_sp = g_alt_stack;
  alt_stack.ss_size = kAltStackSize;
  alt_stack.ss_flags = 0;
  sigaltstack(&alt_stack, nullptr);

  struct sigaction sa {};
  sa.sa_sigaction = NativeCrashSignalHandler;
  sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
  sigemptyset(&sa.sa_mask);

  sigaction(SIGSEGV, &sa, &g_prev_sigsegv);
  sigaction(SIGABRT, &sa, &g_prev_sigabrt);
  sigaction(SIGBUS, &sa, &g_prev_sigbus);
  sigaction(SIGILL, &sa, &g_prev_sigill);
  sigaction(SIGFPE, &sa, &g_prev_sigfpe);

  __android_log_print(ANDROID_LOG_INFO, kLogTag,
      "InstallNativeCrashHandler: installed for SIGSEGV/SIGABRT/SIGBUS/SIGILL/SIGFPE, dir=%s",
      g_native_crash_dir);
}

}  // namespace

static bool EnsureDirExists(const std::string& path) {
  if (path.empty()) return false;
  if (mkdir(path.c_str(), 0755) == 0) return true;
  return errno == EEXIST;
}

static bool FileExists(const std::string& path) {
  if (path.empty()) return false;
  struct stat st {};
  return stat(path.c_str(), &st) == 0;
}

static int64_t FileSize(const std::string& path) {
  if (path.empty()) return -1;
  struct stat st {};
  if (stat(path.c_str(), &st) != 0) return -1;
  return static_cast<int64_t>(st.st_size);
}

static void LogQcow2Info(const std::string& path) {
  FILE* f = fopen(path.c_str(), "rb");
  if (!f) return;

  uint8_t hdr[72];
  if (fread(hdr, 1, sizeof(hdr), f) < sizeof(hdr)) {
    fclose(f);
    return;
  }

  uint32_t magic = (uint32_t)hdr[0] << 24 | hdr[1] << 16 | hdr[2] << 8 | hdr[3];
  if (magic != 0x514649fbu) {
    __android_log_print(ANDROID_LOG_INFO, kLogTag,
                        "QCOW2 check: %s is raw (magic=0x%08x)", path.c_str(), magic);
    fclose(f);
    return;
  }

  uint64_t backing_offset = 0;
  uint32_t backing_size = 0;
  for (int i = 0; i < 8; i++) backing_offset = (backing_offset << 8) | hdr[8 + i];
  for (int i = 0; i < 4; i++) backing_size   = (backing_size   << 8) | hdr[16 + i];

  uint64_t virtual_size = 0;
  for (int i = 0; i < 8; i++) virtual_size = (virtual_size << 8) | hdr[24 + i];

  __android_log_print(ANDROID_LOG_INFO, kLogTag,
                      "QCOW2: %s  virtual_size=%" PRIu64 " (%.1f GB)  file_size=%" PRId64,
                      path.c_str(), virtual_size,
                      (double)virtual_size / (1024.0 * 1024.0 * 1024.0),
                      FileSize(path));

  if (backing_offset != 0 && backing_size != 0 && backing_size < 4096) {
    std::vector<char> backing(backing_size + 1, '\0');
    fseek(f, (long)backing_offset, SEEK_SET);
    fread(backing.data(), 1, backing_size, f);
    __android_log_print(ANDROID_LOG_WARN, kLogTag,
                        "QCOW2 WARNING: backing file = '%s'  -- reads of unmodified "
                        "sectors will FAIL if this file is missing!",
                        backing.data());
  }
  fclose(f);
}

static bool IsTcgTuningEnabled() {
  const char* value = SDL_getenv("XEMU_ANDROID_TCG_TUNING");
  return !(value && value[0] == '0');
}

// Called once per session from ui/xemu.c's sdl2_gl_refresh(), the instant
// nv2a produces its first real frame — lets MainActivity know it's safe to
// remove its boot-cover overlay instead of showing whatever garbage/static
// content the display surface has before the guest has actually drawn
// anything. Every JNI call below is exception-checked individually since a
// NoSuchMethodError (e.g. if MainActivity's Kotlin side doesn't define the
// method) leaves a pending exception that would otherwise corrupt the next
// unrelated JNI call on this thread.
extern "C" void xemu_android_notify_first_hw_frame(void) {
  JNIEnv* env = GetEnv();
  jobject activity = GetActivity(env);
  if (!env || !activity) {
    return;
  }

  jclass activityClass = env->GetObjectClass(activity);
  if (HasException(env, "notify_first_hw_frame: GetObjectClass") || !activityClass) {
    return;
  }

  jmethodID method = env->GetMethodID(activityClass, "onFirstHardwareFrameRendered", "()V");
  if (HasException(env, "notify_first_hw_frame: GetMethodID") || !method) {
    return;
  }

  env->CallVoidMethod(activity, method);
  HasException(env, "notify_first_hw_frame: CallVoidMethod");
}

static void LoadGameControllerMappingsFromAssets() {
  constexpr const char* kDbAssetName = "gamecontrollerdb.txt";

  JNIEnv* env = GetEnv();
  jobject activity = GetActivity(env);
  if (!env || !activity) {
    LogInfo("Controller mappings: JNI unavailable");
    return;
  }

  jclass activityClass = env->GetObjectClass(activity);
  jmethodID getAssets = env->GetMethodID(
      activityClass, "getAssets", "()Landroid/content/res/AssetManager;");
  if (!getAssets) {
    LogInfo("Controller mappings: Activity.getAssets() not found");
    return;
  }

  jobject assetManagerObj = env->CallObjectMethod(activity, getAssets);
  if (HasException(env, "Activity.getAssets") || !assetManagerObj) {
    LogInfo("Controller mappings: could not access AssetManager");
    return;
  }

  AAssetManager* assetManager = AAssetManager_fromJava(env, assetManagerObj);
  env->DeleteLocalRef(assetManagerObj);
  if (!assetManager) {
    LogInfo("Controller mappings: AssetManager bridge failed");
    return;
  }

  AAsset* asset = AAssetManager_open(assetManager, kDbAssetName, AASSET_MODE_STREAMING);
  if (!asset) {
    LogInfo("Controller mappings: no custom gamecontrollerdb.txt in assets");
    return;
  }

  const off_t length = AAsset_getLength(asset);
  if (length <= 0 || length > INT_MAX) {
    AAsset_close(asset);
    LogError("Controller mappings: invalid gamecontrollerdb.txt size");
    return;
  }

  std::vector<char> data(static_cast<size_t>(length));
  size_t total = 0;
  while (total < data.size()) {
    const int read = AAsset_read(asset, data.data() + total,
                                 static_cast<size_t>(data.size() - total));
    if (read <= 0) {
      break;
    }
    total += static_cast<size_t>(read);
  }
  AAsset_close(asset);

  if (total == 0) {
    LogError("Controller mappings: gamecontrollerdb.txt is empty");
    return;
  }
  data.resize(total);

  SDL_RWops* rw = SDL_RWFromConstMem(data.data(), static_cast<int>(data.size()));
  if (!rw) {
    LogErrorFmt("Controller mappings: SDL_RWFromConstMem failed: %s", SDL_GetError());
    return;
  }

  const int added = SDL_GameControllerAddMappingsFromRW(rw, 1);
  if (added < 0) {
    LogErrorFmt("Controller mappings: failed to parse gamecontrollerdb.txt: %s", SDL_GetError());
    return;
  }

  LogInfoInt("Controller mappings loaded from assets: %d", added);
}

static const char* GetTcgThreadFromEnv() {
  const char* value = SDL_getenv("XEMU_ANDROID_TCG_THREAD");
  if (value && strcmp(value, "single") == 0) {
    return "single";
  }
  return "multi";
}

static int GetTcgTbSizeFromEnv() {
  constexpr int kDefaultTbSize = 256;
  constexpr int kMinTbSize = 32;
  constexpr int kMaxTbSize = 512;

  const char* value = SDL_getenv("XEMU_ANDROID_TCG_TB_SIZE");
  if (!value || value[0] == '\0') {
    return kDefaultTbSize;
  }

  char* end = nullptr;
  long parsed = strtol(value, &end, 10);
  if (end == value || (end && *end != '\0')) {
    return kDefaultTbSize;
  }
  if (parsed < kMinTbSize) {
    parsed = kMinTbSize;
  } else if (parsed > kMaxTbSize) {
    parsed = kMaxTbSize;
  }
  return static_cast<int>(parsed);
}

static JNIEnv* GetEnv() {
  return static_cast<JNIEnv*>(SDL_AndroidGetJNIEnv());
}

static jobject GetActivity(JNIEnv* env) {
  (void)env;
  return reinterpret_cast<jobject>(SDL_AndroidGetActivity());
}

static bool HasException(JNIEnv* env, const char* context) {
  if (!env->ExceptionCheck()) return false;
  env->ExceptionDescribe();
  env->ExceptionClear();
  LogErrorFmt("JNI exception in %s", context);
  return true;
}

static std::string JStringToString(JNIEnv* env, jstring value) {
  if (!value) return {};
  const char* utf = env->GetStringUTFChars(value, nullptr);
  if (!utf) return {};
  std::string out(utf);
  env->ReleaseStringUTFChars(value, utf);
  return out;
}

static bool HasInlineAioCrashFlag(const std::string& flag_path) {
  if (flag_path.empty()) {
    return false;
  }
  struct stat st {};
  return stat(flag_path.c_str(), &st) == 0;
}

static bool ShouldEnableInlineAioWorkaround(const std::string& crash_flag_path) {
  const char* forced = SDL_getenv("XEMU_ANDROID_INLINE_AIO");
  if (forced) {
    return forced[0] != '\0' && forced[0] != '0';
  }

  if (HasInlineAioCrashFlag(crash_flag_path)) {
    LogInfoFmt("Inline AIO enabled from crash marker: %s",
               crash_flag_path.c_str());
  }

  return true;
}

// Cached handle to the app's SharedPreferences plus the accessor method IDs.
// Resolving these is relatively expensive (class lookup + method lookup +
// a JNI call into Java), so SyncSetupFiles builds this once and reuses it for
// its ~45 per-setting reads instead of re-resolving on every GetPref* call.
struct PrefsCtx {
  JNIEnv* env = nullptr;
  jobject prefs = nullptr;        // SharedPreferences instance (local ref, caller-owned)
  jmethodID getString = nullptr;  // also used for runtime_override_ string lookups
  jmethodID getInt = nullptr;
  jmethodID getBoolean = nullptr;
};

// Resolve the SharedPreferences object and cache its accessor method IDs.
// On failure the returned ctx has prefs == nullptr; all GetPref* helpers then
// fall back to their default values (matching the previous per-call behavior).
// The caller owns ctx.prefs and must DeleteLocalRef it when done.
static PrefsCtx OpenPrefs(JNIEnv* env, jobject activity) {
  PrefsCtx ctx;
  ctx.env = env;
  if (!env || !activity) return ctx;

  jclass activityClass = env->GetObjectClass(activity);
  jmethodID getPrefs = env->GetMethodID(activityClass, "getSharedPreferences",
                                        "(Ljava/lang/String;I)Landroid/content/SharedPreferences;");
  env->DeleteLocalRef(activityClass);
  if (!getPrefs) return ctx;

  jstring prefsName = env->NewStringUTF(kPrefsName);
  jobject prefs = env->CallObjectMethod(activity, getPrefs, prefsName, 0);
  env->DeleteLocalRef(prefsName);
  if (HasException(env, "getSharedPreferences") || !prefs) return ctx;
  ctx.prefs = prefs;

  jclass prefsClass = env->GetObjectClass(prefs);
  ctx.getString = env->GetMethodID(
      prefsClass, "getString", "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;");
  ctx.getInt = env->GetMethodID(prefsClass, "getInt", "(Ljava/lang/String;I)I");
  ctx.getBoolean = env->GetMethodID(prefsClass, "getBoolean", "(Ljava/lang/String;Z)Z");
  env->DeleteLocalRef(prefsClass);
  return ctx;
}

static std::string GetPrefString(const PrefsCtx& p, const char* key) {
  JNIEnv* env = p.env;
  if (!p.prefs || !p.getString) return {};

  std::string runtimeKeyStr = std::string("runtime_override_") + key;
  jstring jRuntimeKey = env->NewStringUTF(runtimeKeyStr.c_str());
  jstring runtimeValue = static_cast<jstring>(
      env->CallObjectMethod(p.prefs, p.getString, jRuntimeKey, nullptr));
  env->DeleteLocalRef(jRuntimeKey);
  if (!HasException(env, "SharedPreferences.getString") && runtimeValue) {
    std::string override = JStringToString(env, runtimeValue);
    env->DeleteLocalRef(runtimeValue);
    if (!override.empty()) {
      return override;
    }
  }

  jstring jkey = env->NewStringUTF(key);
  jstring value = static_cast<jstring>(
      env->CallObjectMethod(p.prefs, p.getString, jkey, nullptr));
  env->DeleteLocalRef(jkey);
  if (HasException(env, "SharedPreferences.getString")) return {};

  std::string out = JStringToString(env, value);
  if (value) env->DeleteLocalRef(value);
  return out;
}

static int GetPrefInt(const PrefsCtx& p, const char* key, int defaultValue) {
  JNIEnv* env = p.env;
  if (!p.prefs) return defaultValue;

  if (p.getString) {
    std::string runtimeKeyStr = std::string("runtime_override_") + key;
    jstring jRuntimeKey = env->NewStringUTF(runtimeKeyStr.c_str());
    jstring runtimeValue = static_cast<jstring>(
        env->CallObjectMethod(p.prefs, p.getString, jRuntimeKey, nullptr));
    env->DeleteLocalRef(jRuntimeKey);
    if (!HasException(env, "SharedPreferences.getString") && runtimeValue) {
      std::string override = JStringToString(env, runtimeValue);
      env->DeleteLocalRef(runtimeValue);
      if (!override.empty()) {
        char* end = nullptr;
        long parsed = strtol(override.c_str(), &end, 10);
        if (end && *end == '\0') {
          return static_cast<int>(parsed);
        }
      }
    }
  }

  if (!p.getInt) return defaultValue;

  jstring jkey = env->NewStringUTF(key);
  jint result = env->CallIntMethod(p.prefs, p.getInt, jkey, (jint)defaultValue);
  env->DeleteLocalRef(jkey);
  if (HasException(env, "SharedPreferences.getInt")) return defaultValue;

  return result;
}

static bool GetPrefBool(const PrefsCtx& p, const char* key, bool defaultValue) {
  JNIEnv* env = p.env;
  if (!p.prefs) return defaultValue;

  // Check for per-game runtime override (stored as string "true"/"false")
  if (p.getString) {
    std::string runtimeKeyStr = std::string("runtime_override_") + key;
    jstring jRuntimeKey = env->NewStringUTF(runtimeKeyStr.c_str());
    jstring override = static_cast<jstring>(
        env->CallObjectMethod(p.prefs, p.getString, jRuntimeKey, nullptr));
    env->DeleteLocalRef(jRuntimeKey);
    if (!HasException(env, "SharedPreferences.getString") && override) {
      std::string overrideStr = JStringToString(env, override);
      env->DeleteLocalRef(override);
      if (overrideStr == "true") return true;
      if (overrideStr == "false") return false;
    }
  }

  if (!p.getBoolean) return defaultValue;

  jstring jkey = env->NewStringUTF(key);
  jboolean result = env->CallBooleanMethod(p.prefs, p.getBoolean, jkey, (jboolean)defaultValue);
  env->DeleteLocalRef(jkey);
  if (HasException(env, "SharedPreferences.getBoolean")) return defaultValue;

  return result;
}

static int OpenUriAsNativeFd(JNIEnv* env, jobject activity, const std::string& uriString) {
  if (uriString.empty()) return -1;

  jclass activityClass = env->GetObjectClass(activity);
  jmethodID getContentResolver = env->GetMethodID(activityClass, "getContentResolver",
                                                   "()Landroid/content/ContentResolver;");
  if (!getContentResolver) return -1;
  jobject resolver = env->CallObjectMethod(activity, getContentResolver);
  if (HasException(env, "getContentResolver") || !resolver) return -1;

  jclass uriClass = env->FindClass("android/net/Uri");
  jmethodID parse = env->GetStaticMethodID(uriClass, "parse", "(Ljava/lang/String;)Landroid/net/Uri;");
  jstring juri = env->NewStringUTF(uriString.c_str());
  jobject uri = env->CallStaticObjectMethod(uriClass, parse, juri);
  env->DeleteLocalRef(juri);
  if (HasException(env, "Uri.parse") || !uri) return -1;

  jclass resolverClass = env->GetObjectClass(resolver);
  jmethodID openFd = env->GetMethodID(resolverClass, "openFileDescriptor",
      "(Landroid/net/Uri;Ljava/lang/String;)Landroid/os/ParcelFileDescriptor;");
  if (!openFd) return -1;

  jstring mode = env->NewStringUTF("r");
  jobject pfd = env->CallObjectMethod(resolver, openFd, uri, mode);
  env->DeleteLocalRef(mode);
  if (HasException(env, "openFileDescriptor") || !pfd) return -1;

  jclass pfdClass = env->GetObjectClass(pfd);
  jmethodID detach = env->GetMethodID(pfdClass, "detachFd", "()I");
  if (!detach) {
    jmethodID closePfd = env->GetMethodID(pfdClass, "close", "()V");
    if (closePfd) env->CallVoidMethod(pfd, closePfd);
    return -1;
  }

  jint fd = env->CallIntMethod(pfd, detach);
  if (HasException(env, "ParcelFileDescriptor.detachFd")) return -1;

  return static_cast<int>(fd);
}

static bool CopyUriToPath(JNIEnv* env, jobject activity, const std::string& uriString, const std::string& path) {
  if (uriString.empty() || path.empty()) return false;

  jclass activityClass = env->GetObjectClass(activity);
  jmethodID getContentResolver = env->GetMethodID(activityClass, "getContentResolver",
                                                 "()Landroid/content/ContentResolver;");
  if (!getContentResolver) return false;
  jobject resolver = env->CallObjectMethod(activity, getContentResolver);
  if (HasException(env, "getContentResolver") || !resolver) return false;

  jclass uriClass = env->FindClass("android/net/Uri");
  jmethodID parse = env->GetStaticMethodID(uriClass, "parse", "(Ljava/lang/String;)Landroid/net/Uri;");
  jstring juri = env->NewStringUTF(uriString.c_str());
  jobject uri = env->CallStaticObjectMethod(uriClass, parse, juri);
  env->DeleteLocalRef(juri);
  if (HasException(env, "Uri.parse") || !uri) return false;

  jclass resolverClass = env->GetObjectClass(resolver);
  jmethodID openInputStream = env->GetMethodID(
      resolverClass, "openInputStream", "(Landroid/net/Uri;)Ljava/io/InputStream;");
  jobject inputStream = env->CallObjectMethod(resolver, openInputStream, uri);
  if (HasException(env, "openInputStream") || !inputStream) return false;

  jclass fosClass = env->FindClass("java/io/FileOutputStream");
  jmethodID fosCtor = env->GetMethodID(fosClass, "<init>", "(Ljava/lang/String;)V");
  jstring jpath = env->NewStringUTF(path.c_str());
  jobject outputStream = env->NewObject(fosClass, fosCtor, jpath);
  env->DeleteLocalRef(jpath);
  if (HasException(env, "FileOutputStream.<init>") || !outputStream) return false;

  jclass inputClass = env->GetObjectClass(inputStream);
  jclass outputClass = env->GetObjectClass(outputStream);
  jmethodID readMethod = env->GetMethodID(inputClass, "read", "([B)I");
  jmethodID closeInput = env->GetMethodID(inputClass, "close", "()V");
  jmethodID writeMethod = env->GetMethodID(outputClass, "write", "([BII)V");
  jmethodID closeOutput = env->GetMethodID(outputClass, "close", "()V");
  if (!readMethod || !writeMethod) return false;

  bool ok = true;
  int64_t totalBytes = 0;
  const int kBufferSize = 64 * 1024;
  jbyteArray buffer = env->NewByteArray(kBufferSize);
  while (true) {
    jint read = env->CallIntMethod(inputStream, readMethod, buffer);
    if (HasException(env, "InputStream.read")) { ok = false; break; }
    if (read <= 0) break;
    env->CallVoidMethod(outputStream, writeMethod, buffer, 0, read);
    if (HasException(env, "OutputStream.write")) { ok = false; break; }
    totalBytes += read;
  }
  env->DeleteLocalRef(buffer);
  env->CallVoidMethod(inputStream, closeInput);
  env->CallVoidMethod(outputStream, closeOutput);
  HasException(env, "close streams");

  __android_log_print(ANDROID_LOG_INFO, kLogTag,
                      "CopyUriToPath: %s -> %s  bytes=%" PRId64 " ok=%d",
                      uriString.c_str(), path.c_str(), totalBytes, ok);
  if (ok && totalBytes == 0) {
    LogError("CopyUriToPath: source was empty or unreadable");
    ok = false;
  }
  return ok;
}

struct SetupFiles {
  std::string mcpx;
  std::string flash;
  std::string hdd;
  std::string dvd;
  std::string eeprom;
  std::string config_path;
  std::string inline_aio_flag_path;
  std::string audio_driver;  // SDL audio driver hint ("aaudio", "android", "dummy")
};

struct DisplaySettings {
  float surface_scale = 1.0f;
  int mem_limit_mib = 64;
  bool vsync = true;
  bool unlock_framerate = false;
  bool validation_layers = false;
  bool skip_boot_anim = true;
  bool fp_jit = true;
  bool use_dsp = false;
  bool use_dsp_jit = true;
  bool hrtf = false;
  bool cache_shaders = true;
  bool net_enable = false;
  std::string renderer = "vulkan";
  std::string filtering = "nearest";
  std::string aspect_ratio = "fit";
  std::string tcg_thread = "multi";
  std::string audio_driver = "aaudio";
};

static bool WriteConfigToml(const std::string& config_path,
                            const std::string& mcpx,
                            const std::string& flash,
                            const std::string& hdd,
                            const std::string& dvd,
                            const std::string& eeprom,
                            int tcg_tb_size = 128,
                            const DisplaySettings& ds = {}) {
  if (config_path.empty()) return false;
  toml::table tbl;

  if (FileExists(config_path)) {
    try {
      tbl = toml::parse_file(config_path);
    } catch (const toml::parse_error&) {
      // Ignore parse errors; we'll rewrite a clean config.
    }
  }

  auto EnsureTable = [](toml::table& parent, std::string_view key) -> toml::table* {
    if (auto* node = parent.get(key)) {
      if (auto* existing = node->as_table()) {
        return existing;
      }
    }
    parent.insert_or_assign(key, toml::table{});
    return parent.get(key)->as_table();
  };

  toml::table* general = EnsureTable(tbl, "general");
  toml::table* display = EnsureTable(tbl, "display");
  toml::table* display_window = EnsureTable(*display, "window");
  toml::table* audio = EnsureTable(tbl, "audio");
  toml::table* audio_vp = EnsureTable(*audio, "vp");
  toml::table* android = EnsureTable(tbl, "android");
  toml::table* sys = EnsureTable(tbl, "sys");
  toml::table* files = EnsureTable(*sys, "files");
  if (!general || !display || !display_window || !audio || !audio_vp ||
      !android || !sys || !files) {
    LogErrorFmt("Failed to build config tables at %s", config_path.c_str());
    return false;
  }

  general->insert_or_assign("show_welcome", false);
  general->insert_or_assign("skip_boot_anim", ds.skip_boot_anim);
  display->insert_or_assign("renderer", ds.renderer);
  display->insert_or_assign("filtering", ds.filtering);
  display_window->insert_or_assign("vsync", ds.vsync);

  toml::table* display_quality = EnsureTable(*display, "quality");
  if (display_quality) {
    display_quality->insert_or_assign("surface_scale", ds.surface_scale);
  }

  toml::table* display_ui = EnsureTable(*display, "ui");
  if (display_ui) {
    display_ui->insert_or_assign("aspect_ratio", ds.aspect_ratio);
  }
  toml::table* display_vulkan = EnsureTable(*display, "vulkan");
  if (display_vulkan) {
    display_vulkan->insert_or_assign("validation_layers", ds.validation_layers);
  }
  if (!audio_vp->contains("num_workers")) {
    audio_vp->insert_or_assign("num_workers", 0);
  }
  audio->insert_or_assign("hrtf", ds.hrtf);
  audio->insert_or_assign("use_dsp", ds.use_dsp);
  audio->insert_or_assign("use_dsp_jit", ds.use_dsp_jit);
  if (!audio->contains("volume_limit")) {
    audio->insert_or_assign("volume_limit", 1.0);
  }
  if (!android->contains("force_cpu_blit")) {
    android->insert_or_assign("force_cpu_blit", false);
  }
  if (!android->contains("tcg_tuning")) {
    android->insert_or_assign("tcg_tuning", true);
  }
  android->insert_or_assign("tcg_thread",
                            ds.tcg_thread == "single" ? "single" : "multi");
  android->insert_or_assign("tcg_tb_size", tcg_tb_size);
  android->insert_or_assign("audio_driver", ds.audio_driver);

  toml::table* perf = EnsureTable(tbl, "perf");
  if (perf) {
    perf->insert_or_assign("unlock_framerate", ds.unlock_framerate);
    perf->insert_or_assign("fp_jit", ds.fp_jit);
    perf->insert_or_assign("cache_shaders", ds.cache_shaders);
  }

  toml::table* net = EnsureTable(tbl, "net");
  if (net) {
    net->insert_or_assign("enable", ds.net_enable);
    net->insert_or_assign("backend", "nat");
  }

  sys->insert_or_assign("mem_limit", ds.mem_limit_mib == 128 ? "128" : "64");

  files->insert_or_assign("bootrom_path", mcpx);
  files->insert_or_assign("flashrom_path", flash);
  files->insert_or_assign("eeprom_path", eeprom);
  files->insert_or_assign("hdd_path", hdd);
  files->insert_or_assign("dvd_path", dvd);

  std::ofstream out(config_path, std::ios::binary | std::ios::trunc);
  if (!out.is_open()) {
    LogErrorFmt("Failed to write config at %s", config_path.c_str());
    return false;
  }
  out << tbl;
  out.close();
  return true;
}

static SetupFiles SyncSetupFiles() {
  SetupFiles out{};
  JNIEnv* env = GetEnv();
  jobject activity = GetActivity(env);
  if (!env || !activity) {
    LogError("JNI environment not ready for setup sync");
    return out;
  }

  LogInfo("SyncSetupFiles: start");

  const char* basePath = SDL_AndroidGetInternalStoragePath();
  int extState = SDL_AndroidGetExternalStorageState();
  if (extState & SDL_ANDROID_EXTERNAL_STORAGE_WRITE) {
    const char* external = SDL_AndroidGetExternalStoragePath();
    if (external && external[0] != '\0') {
      basePath = external;
    }
  }
  if (!basePath || basePath[0] == '\0') {
    LogError("Storage path not available");
    return out;
  }
  LogInfoFmt("SyncSetupFiles: base path %s", basePath);

  std::string base = std::string(basePath) + "/XaniteOG";
  EnsureDirExists(base);
  out.eeprom = base + "/eeprom.bin";
  out.inline_aio_flag_path = base + "/inline_aio_required.flag";

  // Resolve SharedPreferences and its accessor method IDs once; every GetPref*
  // call below reuses this instead of re-resolving the prefs object per read.
  // Placed after the early storage-path return above so that path can't leak
  // the prefs local ref (freed at the single cleanup point before return).
  PrefsCtx prefs = OpenPrefs(env, activity);

  std::string envVars = GetPrefString(prefs, "env_vars");
  if (!envVars.empty()) {
    std::istringstream stream(envVars);
    std::string line;
    while (std::getline(stream, line)) {
      if (line.empty()) continue;
      auto eq = line.find('=');
      if (eq == std::string::npos || eq == 0) continue;
      std::string key = line.substr(0, eq);
      std::string val = line.substr(eq + 1);
      setenv(key.c_str(), val.c_str(), 1);
      __android_log_print(ANDROID_LOG_INFO, kLogTag,
                          "env: %s=%s", key.c_str(), val.c_str());
    }
  }

  const std::string mcpxPath = GetPrefString(prefs, "mcpxPath");
  const std::string flashPath = GetPrefString(prefs, "flashPath");
  const std::string hddPath = GetPrefString(prefs, "hddPath");
  const std::string dvdPath = GetPrefString(prefs, "dvdPath");
  const std::string mcpxUri = GetPrefString(prefs, "mcpxUri");
  const std::string flashUri = GetPrefString(prefs, "flashUri");
  const std::string hddUri = GetPrefString(prefs, "hddUri");
  const std::string dvdUri = GetPrefString(prefs, "dvdUri");

  LogInfoFmt("Prefs mcpxPath=%s", mcpxPath.c_str());
  LogInfoFmt("Prefs flashPath=%s", flashPath.c_str());
  LogInfoFmt("Prefs hddPath=%s", hddPath.c_str());
  LogInfoFmt("Prefs dvdPath=%s", dvdPath.c_str());
  LogInfoFmt("Prefs mcpxUri=%s", mcpxUri.c_str());
  LogInfoFmt("Prefs flashUri=%s", flashUri.c_str());
  LogInfoFmt("Prefs hddUri=%s", hddUri.c_str());
  LogInfoFmt("Prefs dvdUri=%s", dvdUri.c_str());

  if (!mcpxPath.empty() && FileExists(mcpxPath)) {
    out.mcpx = mcpxPath;
  }
  if (out.mcpx.empty() && !mcpxUri.empty()) {
    out.mcpx = base + "/mcpx.bin";
    if (FileExists(out.mcpx)) {
      LogInfo("MCPX ROM already in app storage, skipping copy");
    } else if (CopyUriToPath(env, activity, mcpxUri, out.mcpx)) {
      LogInfo("MCPX ROM synced to app storage");
    } else {
      LogError("Failed to sync MCPX ROM");
    }
  }
  if (!flashPath.empty() && FileExists(flashPath)) {
    out.flash = flashPath;
  }
  if (out.flash.empty() && !flashUri.empty()) {
    out.flash = base + "/flash.bin";
    if (FileExists(out.flash)) {
      LogInfo("Flash ROM already in app storage, skipping copy");
    } else if (CopyUriToPath(env, activity, flashUri, out.flash)) {
      LogInfo("Flash ROM synced to app storage");
    } else {
      LogError("Failed to sync flash ROM");
    }
  }
  if (!hddPath.empty() && FileExists(hddPath)) {
    out.hdd = hddPath;
    __android_log_print(ANDROID_LOG_INFO, kLogTag,
                        "HDD from pref: %s  size=%" PRId64, hddPath.c_str(), FileSize(hddPath));
  }
  if (out.hdd.empty() && !hddUri.empty()) {
    out.hdd = base + "/hdd.img";
    if (FileExists(out.hdd)) {
      LogInfo("HDD image already in app storage, skipping copy");
    } else if (CopyUriToPath(env, activity, hddUri, out.hdd)) {
      LogInfo("HDD image synced to app storage");
    } else {
      LogError("Failed to sync HDD image");
      unlink(out.hdd.c_str());
      out.hdd.clear();
    }
  }
  if (!out.hdd.empty()) {
    __android_log_print(ANDROID_LOG_INFO, kLogTag,
                        "HDD resolved: %s  size=%" PRId64, out.hdd.c_str(), FileSize(out.hdd));
    LogQcow2Info(out.hdd);
  }

  if (!dvdPath.empty() && FileExists(dvdPath)) {
    out.dvd = dvdPath;
  }
  if (out.dvd.empty() && !dvdUri.empty()) {
    if (g_dvd_fd >= 0) {
      close(g_dvd_fd);
      g_dvd_fd = -1;
    }
    int fd = OpenUriAsNativeFd(env, activity, dvdUri);
    if (fd >= 0) {
      g_dvd_fd = fd;
      out.dvd = "/dev/fdset/0";
      LogInfoInt("DVD image opened via fd %d (zero-copy, fdset)", fd);
    } else {
      LogError("Failed to open DVD URI as fd, falling back to copy");
      std::string copy_dst = base + "/dvd.iso";
      if (CopyUriToPath(env, activity, dvdUri, copy_dst)) {
        out.dvd = copy_dst;
        LogInfo("DVD image synced to app storage (fallback copy)");
      } else {
        LogError("Failed to sync DVD image");
      }
    }
  }

  out.config_path = base + "/xemu.toml";
  int tbSize = GetPrefInt(prefs, "tcg_tb_size", 256);

  DisplaySettings ds;
  int scale_val = GetPrefInt(
      prefs, "setting_surface_scale",
      GetPrefInt(prefs, "surface_scale", 1));
  ds.surface_scale = scale_val == 0 ? 0.5f : (float)scale_val;
  if (ds.surface_scale < 0.5f) ds.surface_scale = 0.5f;
  if (ds.surface_scale > 10.0f) ds.surface_scale = 10.0f;
  ds.vsync = GetPrefBool(
      prefs, "setting_vsync",
      GetPrefBool(prefs, "vsync", true));
  ds.unlock_framerate = GetPrefBool(prefs, "unlock_framerate", false);
  ds.validation_layers = GetPrefBool(prefs, "validation_layers", false);
  ds.skip_boot_anim = GetPrefBool(prefs, "setting_skip_boot_anim", true);
  ds.use_dsp = GetPrefBool(prefs, "setting_use_dsp", false);
  ds.use_dsp_jit = GetPrefBool(prefs, "setting_use_dsp_jit", true);
  ds.hrtf = GetPrefBool(prefs, "setting_hrtf",
                         GetPrefBool(prefs, "hrtf", false));
  ds.cache_shaders = GetPrefBool(prefs, "setting_cache_shaders", true);
  ds.net_enable = GetPrefBool(prefs, "setting_network_enable", false);
  ds.mem_limit_mib = GetPrefInt(prefs, "setting_system_memory_mib",
                                 GetPrefInt(prefs, "sys_mem_mib", 64));
  {
    std::string thread = GetPrefString(prefs, "setting_tcg_thread");
    if (thread.empty()) {
      thread = GetPrefString(prefs, "tcg_thread");
    }
    ds.tcg_thread = thread == "single" ? "single" : "multi";
  }
  {
    std::string drv = GetPrefString(prefs, "setting_audio_driver");
    if (drv == "dummy") ds.audio_driver = "dummy";
    else if (drv == "openslES") ds.audio_driver = "android";
    else ds.audio_driver = "aaudio";  // default and explicit aaudio
  }

  bool fp_safe = GetPrefBool(prefs, "fp_safe", true);
  xemu_set_fp_safe(fp_safe);
  __android_log_print(ANDROID_LOG_INFO, "xemu-android",
                      "FP safe (native arithmetic): %s", fp_safe ? "ON" : "OFF");

  bool fp_jit = GetPrefBool(prefs, "setting_hard_fpu",
                             GetPrefBool(prefs, "fp_jit", true));
  ds.fp_jit = fp_jit;
  xemu_set_fp_jit(fp_jit);
  __android_log_print(ANDROID_LOG_INFO, "xemu-android",
                      "FP JIT (native storage + inline ops): %s", fp_jit ? "ON" : "OFF");

  bool fast_fences = GetPrefBool(prefs, "fast_fences", false);
  xemu_set_fast_fences(fast_fences);
  __android_log_print(ANDROID_LOG_INFO, "xemu-android",
                      "fast fences: %s", fast_fences ? "ON" : "OFF");

  bool draw_reorder = GetPrefBool(prefs, "draw_reorder", true);
  xemu_set_draw_reorder(draw_reorder);
  __android_log_print(ANDROID_LOG_INFO, "xemu-android",
                      "draw reorder: %s", draw_reorder ? "ON" : "OFF");

  bool draw_merge = GetPrefBool(prefs, "draw_merge", true);
  xemu_set_draw_merge(draw_merge);
  __android_log_print(ANDROID_LOG_INFO, "xemu-android",
                      "draw merge: %s", draw_merge ? "ON" : "OFF");

  bool bindless_tex = GetPrefBool(prefs, "bindless_textures", false);
  xemu_set_bindless_textures(bindless_tex);
  __android_log_print(ANDROID_LOG_INFO, "xemu-android",
                      "bindless textures: %s", bindless_tex ? "ON" : "OFF");

  bool async_compile = GetPrefBool(prefs, "async_compile", false);
  xemu_set_async_compile(async_compile);
  __android_log_print(ANDROID_LOG_INFO, "xemu-android",
                      "async compile: %s", async_compile ? "ON" : "OFF");

  bool frame_skip = GetPrefBool(prefs, "frame_skip", false);
  xemu_set_frame_skip(frame_skip);
  __android_log_print(ANDROID_LOG_INFO, "xemu-android",
                      "frame skip: %s", frame_skip ? "ON" : "OFF");

  int submit_frames = GetPrefInt(prefs, "submit_frames", 2);
  xemu_set_submit_frames(submit_frames);
  __android_log_print(ANDROID_LOG_INFO, "xemu-android",
                      "submit frames: %d", submit_frames);

  int tier1_threshold = GetPrefInt(prefs, "tier1_threshold", 64);
  xemu_set_tier1_threshold(tier1_threshold);
  // Defaults to the historical 16 so every title behaves exactly as before
  // unless a per-game profile raises it.
  int tier1_budget = GetPrefInt(prefs, "tier1_budget", 16);
  xemu_set_tier1_budget(tier1_budget);
  // Bit 0 = intra-TB dead flag elimination, bit 1 = cross-TB DFE.
  // Both default off: neither pass ran until the 2026-07-29 promotion fix, and
  // neither has been validated in gameplay yet.  See tier1-opt.c.
  int tier1_opt_mask = GetPrefInt(prefs, "tier1_opt_mask", 0);
  xemu_set_tier1_opt_mask(tier1_opt_mask);
  // Master switch; 0 stops promotion outright. No per-game override writes this
  // key, unlike tier1_budget, so it survives PerGameSettingsManager.
  // Defaults off: promotion currently hangs Forza, see cpu-exec.c.
  int tier1_enable = GetPrefInt(prefs, "tier1_enable", 0);
  xemu_set_tier1_enable(tier1_enable);
  __android_log_print(ANDROID_LOG_INFO, "xemu-android",
                      "tier1 budget: %d", tier1_budget);
  __android_log_print(ANDROID_LOG_INFO, "xemu-android",
                      "tier1 threshold: %d", tier1_threshold);
  __android_log_print(ANDROID_LOG_INFO, "xemu-android",
                      "tier1 opt mask: 0x%x", tier1_opt_mask);
  __android_log_print(ANDROID_LOG_INFO, "xemu-android",
                      "tier1 enable: %d", tier1_enable);

  std::string rendererPref = GetPrefString(prefs, "setting_renderer");
  if (rendererPref.empty()) {
    rendererPref = GetPrefString(prefs, "renderer");
  }
  if (rendererPref == "opengl") {
    ds.renderer = "opengl";
  } else if (rendererPref == "vulkan") {
    ds.renderer = "vulkan";
  }

  std::string filterPref = GetPrefString(prefs, "setting_filtering");
  if (filterPref.empty()) {
    filterPref = GetPrefString(prefs, "filtering");
  }
  if (!filterPref.empty()) ds.filtering = filterPref;
  int displayMode = GetPrefInt(prefs, "setting_display_mode", -1);
  if (displayMode == 1) {
    ds.aspect_ratio = "4:3";
    xemu_android_set_display_mode_setting(1);
  } else if (displayMode == 2) {
    ds.aspect_ratio = "16:9";
    xemu_android_set_display_mode_setting(2);
  } else if (displayMode == 0) {
    ds.aspect_ratio = "fit";
    xemu_android_set_display_mode_setting(0);
  } else {
    std::string arPref = GetPrefString(prefs, "aspect_ratio");
    if (!arPref.empty()) ds.aspect_ratio = arPref;
  }

  WriteConfigToml(out.config_path, out.mcpx, out.flash, out.hdd, out.dvd, out.eeprom, tbSize, ds);
  LogInfoFmt("SyncSetupFiles: config %s", out.config_path.c_str());
  LogInfoFmt("Resolved mcpx=%s", out.mcpx.c_str());
  LogInfoFmt("Resolved flash=%s", out.flash.c_str());
  LogInfoFmt("Resolved hdd=%s", out.hdd.c_str());
  LogInfoFmt("Resolved dvd=%s", out.dvd.c_str());
  LogInfoFmt("Resolved eeprom=%s", out.eeprom.c_str());

  out.audio_driver = ds.audio_driver;

  if (prefs.prefs) {
    env->DeleteLocalRef(prefs.prefs);
    prefs.prefs = nullptr;
  }
  return out;
}
}

extern "C" int xemu_android_main(int argc, char** argv);
extern "C" void qemu_init(int argc, char** argv);
extern "C" int (*qemu_main)(void);
extern "C" void xemu_android_display_preinit(void);
extern "C" void xemu_android_display_wait_ready(void);
extern "C" void xemu_android_display_loop(void);
extern "C" void xemu_android_set_inline_aio_crash_flag_path(const char* path);
extern "C" void xemu_android_session_log_open(const char* files_dir);
extern "C" void xemu_android_session_log_close(void);

#ifndef XEMU_OPT_THREAD_AFFINITY
#define XEMU_OPT_THREAD_AFFINITY 0
#endif

#if XEMU_OPT_THREAD_AFFINITY
#include <sys/syscall.h>
#include <sys/resource.h>

static void xemu_pin_to_big_cores_cpp(const char *label) {
  int ncpus = sysconf(_SC_NPROCESSORS_CONF);
  if (ncpus <= 0 || ncpus > 64) return;

  unsigned long max_freq = 0;
  unsigned long freqs[64];
  for (int i = 0; i < ncpus; i++) {
    char path[128];
    snprintf(path, sizeof(path),
             "/sys/devices/system/cpu/cpu%d/cpufreq/cpuinfo_max_freq", i);
    FILE *f = fopen(path, "r");
    if (f) {
      if (fscanf(f, "%lu", &freqs[i]) != 1) freqs[i] = 0;
      fclose(f);
    } else {
      freqs[i] = 0;
    }
    if (freqs[i] > max_freq) max_freq = freqs[i];
  }
  if (max_freq == 0) return;

  unsigned long threshold = max_freq * 9 / 10;
  unsigned long mask = 0;
  int big_count = 0;
  for (int i = 0; i < ncpus && i < (int)(sizeof(mask) * 8); i++) {
    if (freqs[i] >= threshold) {
      mask |= (1UL << i);
      big_count++;
    }
  }
  if (big_count > 0 && big_count < ncpus) {
    if (syscall(__NR_sched_setaffinity, 0, sizeof(mask), &mask) == 0) {
      __android_log_print(ANDROID_LOG_INFO, "xemu-android",
                          "%s: pinned to %d big cores (max_freq=%lu)",
                          label, big_count, max_freq);
    }
  }
  setpriority(PRIO_PROCESS, 0, -10);
}
#endif

struct QemuLaunchContext {
  int argc;
  char** argv;
};

static int SDLCALL QemuThreadMain(void* data) {
#if XEMU_OPT_THREAD_AFFINITY
  xemu_pin_to_big_cores_cpp("qemu_cpu_thread");
#endif
  auto* ctx = static_cast<QemuLaunchContext*>(data);
  xemu_android_set_qemu_thread_finished(false);
  LogInfoInt("QemuThreadMain: show_welcome=%d", g_config.general.show_welcome ? 1 : 0);
  LogInfoFmt("QemuThreadMain: bootrom=%s", g_config.sys.files.bootrom_path ? g_config.sys.files.bootrom_path : "(null)");
  LogInfo("QemuThreadMain: starting");
  const int rc = xemu_android_main(ctx->argc, ctx->argv);
  xemu_android_set_qemu_thread_finished(true);
  return rc;
}

#ifndef XEMU_OPT_TB_CACHE_HINTS
#define XEMU_OPT_TB_CACHE_HINTS 1
#endif

#if XEMU_OPT_TB_CACHE_HINTS
extern "C" void tb_cache_save(const char *path, uint32_t game_hash);
extern "C" int  tb_cache_load(const char *path, uint32_t game_hash);
extern "C" uint32_t tb_cache_compute_game_hash(const char *bootrom_path,
                                               const char *flashrom_path);
extern "C" void tb_cache_cleanup(void);
#endif

extern "C" int xemu_android_main(int argc, char** argv) {
  if (!qemu_main) {
    LogError("xemu core not linked; qemu_main missing");
    return 1;
  }
  LogInfo("xemu_android_main: qemu_init");
  auto t_init_start = SDL_GetTicks();
  qemu_init(argc, argv);
  auto t_init_end = SDL_GetTicks();
  __android_log_print(ANDROID_LOG_INFO, "xemu-android",
                      "qemu_init took %u ms", t_init_end - t_init_start);

  /* qemu_init's cleanup_add_fd already closed the original fd */
  g_dvd_fd = -1;

#if XEMU_OPT_TB_CACHE_HINTS
  /* Load translation block cache hints for pre-warming */
  const char *storage_load = SDL_AndroidGetInternalStoragePath();

  const char *dump_storage = NULL;
  if (SDL_AndroidGetExternalStorageState() & SDL_ANDROID_EXTERNAL_STORAGE_WRITE) {
    dump_storage = SDL_AndroidGetExternalStoragePath();
  }
  if (!dump_storage || !dump_storage[0]) {
    dump_storage = storage_load;
  }
  if (dump_storage) {
    char dump_dir[PATH_MAX];
    snprintf(dump_dir, sizeof(dump_dir), "%s/rt_dumps", dump_storage);
    nv2a_dbg_set_rt_dump_path(dump_dir);
  }

  /* Load translation block cache hints for pre-warming */
  if (storage_load) {
    char cache_path[PATH_MAX];
    snprintf(cache_path, sizeof(cache_path), "%s/XaniteOG/tb_cache.bin", storage_load);
    uint32_t game_hash = tb_cache_compute_game_hash(
        g_config.sys.files.bootrom_path, g_config.sys.files.flashrom_path);
    /* Fold code-generation-affecting settings into the hash so that a
     * cache saved with different FP modes is automatically rejected. */
    game_hash ^= (xemu_get_fp_safe() ? 0x1u : 0) | (xemu_get_fp_jit() ? 0x2u : 0);
    int nhints = tb_cache_load(cache_path, game_hash);
    __android_log_print(ANDROID_LOG_INFO, "xemu-android",
                        "TB cache: loaded %d hints from %s", nhints, cache_path);
  }
#endif

  LogInfo("xemu_android_main: qemu_main");
  int rc = qemu_main();
  LogErrorInt("xemu_android_main: qemu_main returned %d", rc);

#if XEMU_OPT_TB_CACHE_HINTS
  /* Save translation block cache hints for next launch */
  const char *storage = SDL_AndroidGetInternalStoragePath();
  if (storage) {
    char dir_path[PATH_MAX];
    snprintf(dir_path, sizeof(dir_path), "%s/XaniteOG", storage);
    mkdir(dir_path, 0755);
    char cache_path[PATH_MAX];
    snprintf(cache_path, sizeof(cache_path), "%s/tb_cache.bin", dir_path);
    uint32_t game_hash = tb_cache_compute_game_hash(
        g_config.sys.files.bootrom_path, g_config.sys.files.flashrom_path);
    game_hash ^= (xemu_get_fp_safe() ? 0x1u : 0) | (xemu_get_fp_jit() ? 0x2u : 0);
    tb_cache_save(cache_path, game_hash);
  }
  tb_cache_cleanup();
#endif

  if (g_dvd_fd >= 0) {
    close(g_dvd_fd);
    g_dvd_fd = -1;
  }

  return rc;
}

extern "C" int SDL_main(int argc, char* argv[]) {
  (void)argc;
  (void)argv;

  // Mirroring every stdout/stderr line through logcat is useful for a targeted
  // developer capture, but it is far too expensive for normal gameplay. Some
  // games update vertex shader state thousands of times per second.
  const char* active_dev_logs = getenv("XEMU_ANDROID_DEV_LOGS");
  if (active_dev_logs && strcmp(active_dev_logs, "1") == 0) {
    RedirectStdioToLogcat();
  }

  LogInfo("SDL_main: start");
  // Prefer AAudio on Android, but keep Android AudioTrack as fallback.
  SDL_SetHintWithPriority(SDL_HINT_AUDIODRIVER, "aaudio,android",
                          SDL_HINT_OVERRIDE);
  SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
  SDL_DisableScreenSaver();

  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) {
    __android_log_print(ANDROID_LOG_ERROR, kLogTag, "SDL_Init failed: %s", SDL_GetError());
    return 1;
  }

  // Deliberately using the raw internal storage path here, NOT the
  // external-storage-preferring one SyncSetupFiles() computes below — this
  // one must exactly match Kotlin's context.filesDir, since that's what
  // CrashReportManager.kt scans on the next launch for a native-crash-*.log.
  InstallNativeCrashHandler(SDL_AndroidGetInternalStoragePath());
  xemu_android_session_log_open(SDL_AndroidGetInternalStoragePath());

  SDL_GameControllerEventState(SDL_ENABLE);
  LoadGameControllerMappingsFromAssets();

  auto t_sync_start = SDL_GetTicks();
  SetupFiles setup = SyncSetupFiles();
  auto t_sync_end = SDL_GetTicks();
  __android_log_print(ANDROID_LOG_INFO, "xemu-android",
                      "SyncSetupFiles took %u ms", t_sync_end - t_sync_start);

  // Apply user's audio driver preference (overrides the default set above)
  if (!setup.audio_driver.empty()) {
    std::string hint = setup.audio_driver;
    if (hint == "aaudio") hint = "aaudio,android";
    SDL_SetHintWithPriority(SDL_HINT_AUDIODRIVER, hint.c_str(), SDL_HINT_OVERRIDE);
    __android_log_print(ANDROID_LOG_INFO, "xemu-android",
                        "audio driver hint: %s", hint.c_str());
  }

  xemu_android_set_inline_aio_crash_flag_path(setup.inline_aio_flag_path.empty()
                                                   ? nullptr
                                                   : setup.inline_aio_flag_path.c_str());

  if (!SDL_getenv("XEMU_ANDROID_INLINE_AIO")) {
    const bool use_inline_aio =
        ShouldEnableInlineAioWorkaround(setup.inline_aio_flag_path);
    setenv("XEMU_ANDROID_INLINE_AIO", use_inline_aio ? "1" : "0", 1);
    LogInfoFmt("XEMU_ANDROID_INLINE_AIO=%s", use_inline_aio ? "1" : "0");
  }

  if (!setup.config_path.empty()) {
    LogInfo("SDL_main: loading config");
    xemu_settings_set_path(setup.config_path.c_str());
    if (!xemu_settings_load()) {
      const char* err = xemu_settings_get_error_message();
      if (!err) {
        err = "Failed to load config file";
      }
      LogError(err);
      SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,
                               "Failed to load xemu config file",
                               err,
                               nullptr);
      SDL_Quit();
      return 1;
    }
    LogInfo("SDL_main: config loaded");
    LogInfoInt("Config show_welcome=%d", g_config.general.show_welcome ? 1 : 0);
    LogInfoFmt("Config bootrom=%s", g_config.sys.files.bootrom_path ? g_config.sys.files.bootrom_path : "(null)");
    LogInfoFmt("Config flashrom=%s", g_config.sys.files.flashrom_path ? g_config.sys.files.flashrom_path : "(null)");
    LogInfoFmt("Config hdd=%s", g_config.sys.files.hdd_path ? g_config.sys.files.hdd_path : "(null)");
    LogInfoFmt("Config dvd=%s", g_config.sys.files.dvd_path ? g_config.sys.files.dvd_path : "(null)");
    LogInfoFmt("Config eeprom=%s", g_config.sys.files.eeprom_path ? g_config.sys.files.eeprom_path : "(null)");

    // Ensure config strings are non-null and aligned with Android setup paths.
    if (!setup.mcpx.empty()) {
      xemu_settings_set_string(&g_config.sys.files.bootrom_path, setup.mcpx.c_str());
    } else if (!g_config.sys.files.bootrom_path) {
      xemu_settings_set_string(&g_config.sys.files.bootrom_path, "");
    }
    if (!setup.flash.empty()) {
      xemu_settings_set_string(&g_config.sys.files.flashrom_path, setup.flash.c_str());
    } else if (!g_config.sys.files.flashrom_path) {
      xemu_settings_set_string(&g_config.sys.files.flashrom_path, "");
    }
    if (!setup.hdd.empty()) {
      xemu_settings_set_string(&g_config.sys.files.hdd_path, setup.hdd.c_str());
    } else if (!g_config.sys.files.hdd_path) {
      xemu_settings_set_string(&g_config.sys.files.hdd_path, "");
    }
    if (!setup.dvd.empty()) {
      xemu_settings_set_string(&g_config.sys.files.dvd_path, setup.dvd.c_str());
    } else {
      xemu_settings_set_string(&g_config.sys.files.dvd_path, "");
    }
    if (!setup.eeprom.empty()) {
      xemu_settings_set_string(&g_config.sys.files.eeprom_path, setup.eeprom.c_str());
    } else if (!g_config.sys.files.eeprom_path) {
      xemu_settings_set_string(&g_config.sys.files.eeprom_path, "");
    }
    setenv("XEMU_ANDROID_FORCE_CPU_BLIT", "0", 1);
    g_config.general.show_welcome = false;

    // Apply the early renderer selection from activity prefs/runtime overrides.
    {
      const char *renderer_pref = SDL_getenv("XEMU_RENDERER");
      if (renderer_pref && strcmp(renderer_pref, "opengl") == 0) {
        g_config.display.renderer = CONFIG_DISPLAY_RENDERER_OPENGL;
        LogInfo("Renderer override: OpenGL ES");
      } else if (renderer_pref && strcmp(renderer_pref, "vulkan") == 0) {
        g_config.display.renderer = CONFIG_DISPLAY_RENDERER_VULKAN;
        LogInfo("Renderer override: Vulkan");
      }
    }

    LogInfoInt("Config final show_welcome=%d", g_config.general.show_welcome ? 1 : 0);
    LogInfoInt("Config final skip_boot_anim=%d", g_config.general.skip_boot_anim ? 1 : 0);
    LogInfoInt("Config final cache_shaders=%d", g_config.perf.cache_shaders ? 1 : 0);
    LogInfoInt("Config final fp_jit=%d", g_config.perf.fp_jit ? 1 : 0);
    LogInfoInt("Config final renderer=%d", (int)g_config.display.renderer);
    LogInfoFmt("Config final bootrom=%s", g_config.sys.files.bootrom_path ? g_config.sys.files.bootrom_path : "(null)");
    LogInfoFmt("Config final flashrom=%s", g_config.sys.files.flashrom_path ? g_config.sys.files.flashrom_path : "(null)");
    LogInfoFmt("Config final hdd=%s", g_config.sys.files.hdd_path ? g_config.sys.files.hdd_path : "(null)");
    LogInfoFmt("Config final dvd=%s", g_config.sys.files.dvd_path ? g_config.sys.files.dvd_path : "(null)");
    LogInfoFmt("Config final eeprom=%s", g_config.sys.files.eeprom_path ? g_config.sys.files.eeprom_path : "(null)");

    std::vector<std::string> arg_storage;
    arg_storage.emplace_back("xemu");
    if (IsTcgTuningEnabled()) {
      const char* tcg_thread = GetTcgThreadFromEnv();
      int tcg_tb_size = GetTcgTbSizeFromEnv();
      char accel_opts[64];
      snprintf(accel_opts, sizeof(accel_opts), "tcg,thread=%s,tb-size=%d",
               tcg_thread, tcg_tb_size);
      arg_storage.emplace_back("-accel");
      arg_storage.emplace_back(accel_opts);
      LogInfoFmt("SDL_main: using accel %s", accel_opts);
    } else {
      LogInfo("SDL_main: TCG tuning disabled");
    }

    if (g_dvd_fd >= 0) {
      int flags = fcntl(g_dvd_fd, F_GETFD);
      if (flags != -1 && (flags & FD_CLOEXEC)) {
        fcntl(g_dvd_fd, F_SETFD, flags & ~FD_CLOEXEC);
      }
      char add_fd_arg[64];
      snprintf(add_fd_arg, sizeof(add_fd_arg), "fd=%d,set=0", g_dvd_fd);
      arg_storage.emplace_back("-add-fd");
      arg_storage.emplace_back(add_fd_arg);
      LogInfoInt("SDL_main: passing DVD fd %d via -add-fd", g_dvd_fd);
    }

    std::vector<char*> xemu_argv;
    xemu_argv.reserve(arg_storage.size() + 1);
    for (auto& arg : arg_storage) {
      xemu_argv.push_back(const_cast<char*>(arg.c_str()));
    }
    xemu_argv.push_back(nullptr);
    LogInfo("SDL_main: launching xemu core");
    xemu_android_display_preinit();

    QemuLaunchContext launch_ctx{
      static_cast<int>(arg_storage.size()),
      xemu_argv.data(),
    };
    SDL_Thread* qemu_thread = SDL_CreateThread(QemuThreadMain, "qemu_main", &launch_ctx);
    if (!qemu_thread) {
      LogErrorFmt("Failed to start xemu thread: %s", SDL_GetError());
      return 1;
    }
    LogInfo("SDL_main: qemu thread started");
    xemu_android_set_qemu_thread_finished(false);
    xemu_android_display_wait_ready();
    LogInfo("SDL_main: display ready, entering render loop");
    xemu_android_display_loop();

    LogInfo("SDL_main: display loop exited, waiting for QEMU thread");
    xemu_android_session_log_close();
    int qemu_rc = 0;
    SDL_WaitThread(qemu_thread, &qemu_rc);
    LogInfoInt("SDL_main: QEMU thread exited with %d", qemu_rc);

    /* Always terminate the process on exit. Returning from SDL_main and
     * letting the JVM/SDL lifecycle tear down in-process is racy on Android:
     * window focus / surface callbacks can fire after SDL's event-queue
     * mutex has been destroyed, which aborts with
     * "pthread_mutex_lock called on a destroyed mutex". For the
     * "return to game library" path, MainActivity launches
     * GameLibraryActivity in a new task *before* calling into native, so
     * Android respawns the library UI once this process dies. */
    (void)g_return_to_library_on_exit.exchange(false);
    LogInfo("SDL_main: QEMU cleanup complete, terminating process");
    _exit(qemu_rc);
  }

  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
  SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
  SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

  SDL_Window* window = SDL_CreateWindow(
    "xemu (Android bootstrap)",
    SDL_WINDOWPOS_CENTERED,
    SDL_WINDOWPOS_CENTERED,
    1280,
    720,
    SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_SHOWN
  );

  if (!window) {
    __android_log_print(ANDROID_LOG_ERROR, kLogTag, "SDL_CreateWindow failed: %s", SDL_GetError());
    SDL_Quit();
    return 1;
  }

  SDL_GLContext gl = SDL_GL_CreateContext(window);
  if (!gl) {
    __android_log_print(ANDROID_LOG_ERROR, kLogTag, "SDL_GL_CreateContext failed: %s", SDL_GetError());
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 1;
  }

  SDL_GL_MakeCurrent(window, gl);
  SDL_GL_SetSwapInterval(1);

  LogInfo("xemu Android bootstrap running (core not wired yet)");

  bool running = true;
  while (running) {
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
      if (ev.type == SDL_QUIT) {
        running = false;
      } else if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_AC_BACK) {
        running = false;
      }
    }

    int w = 0;
    int h = 0;
    SDL_GL_GetDrawableSize(window, &w, &h);
    if (w <= 0) w = 1;
    if (h <= 0) h = 1;

    glViewport(0, 0, w, h);
    glClearColor(0.05f, 0.07f, 0.09f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

    SDL_GL_SwapWindow(window);
  }

  SDL_GL_DeleteContext(gl);
  SDL_DestroyWindow(window);
  SDL_Quit();
  return 0;
}

extern "C" JNIEXPORT jint JNICALL
Java_Ali_Xanite_MainActivity_nativeGetFps(JNIEnv *, jobject)
{
    return static_cast<jint>(g_nv2a_stats.increment_fps);
}

extern "C" JNIEXPORT jstring JNICALL
Java_Ali_Xanite_MainActivity_nativeGetFramePacing(JNIEnv *env, jobject)
{
    char buf[256];
    nv2a_profile_get_pacing_str(buf, sizeof(buf));
    return env->NewStringUTF(buf);
}

extern "C" JNIEXPORT jstring JNICALL
Java_Ali_Xanite_MainActivity_nativeGetShaderStats(JNIEnv *env, jobject)
{
    char buf[256];
    nv2a_profile_get_shader_stats_str(buf, sizeof(buf));
    return env->NewStringUTF(buf);
}

extern "C" JNIEXPORT jboolean JNICALL
Java_Ali_Xanite_MainActivity_nativeCaptureFrame(JNIEnv *, jobject)
{
#ifdef CONFIG_RENDERDOC
    if (nv2a_dbg_renderdoc_available()) {
        nv2a_dbg_renderdoc_capture_frames(1, false);
        return JNI_TRUE;
    }
#endif
    return JNI_FALSE;
}

extern "C" JNIEXPORT void JNICALL
Java_Ali_Xanite_MainActivity_nativeDumpDiagFrames(JNIEnv *, jobject, jint numFrames)
{
    nv2a_dbg_trigger_diag_frames((int)numFrames);
}

extern "C" char g_vulkan_driver_info[256];

extern "C" JNIEXPORT jstring JNICALL
Java_Ali_Xanite_MainActivity_nativeGetDriverInfo(JNIEnv *env, jobject)
{
    return env->NewStringUTF(g_vulkan_driver_info);
}

extern "C" JNIEXPORT jboolean JNICALL
Java_Ali_Xanite_SettingsActivity_nativeGetFpSafe(JNIEnv *, jobject)
{
    return xemu_get_fp_safe() ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT void JNICALL
Java_Ali_Xanite_SettingsActivity_nativeSetFpSafe(JNIEnv *, jobject, jboolean enable)
{
    xemu_set_fp_safe(enable == JNI_TRUE);
    const char *storage = SDL_AndroidGetInternalStoragePath();
    if (storage) {
        char path[PATH_MAX];
        snprintf(path, sizeof(path), "%s/XaniteOG/tb_cache.bin", storage);
        remove(path);
    }
}

extern "C" JNIEXPORT jboolean JNICALL
Java_Ali_Xanite_SettingsActivity_nativeGetFastFences(JNIEnv *, jobject)
{
    return xemu_get_fast_fences() ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT void JNICALL
Java_Ali_Xanite_SettingsActivity_nativeSetFastFences(JNIEnv *, jobject, jboolean enable)
{
    xemu_set_fast_fences(enable == JNI_TRUE);
}

extern "C" JNIEXPORT jboolean JNICALL
Java_Ali_Xanite_SettingsActivity_nativeGetDrawReorder(JNIEnv *, jobject)
{
    return xemu_get_draw_reorder() ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT void JNICALL
Java_Ali_Xanite_SettingsActivity_nativeSetDrawReorder(JNIEnv *, jobject, jboolean enable)
{
    xemu_set_draw_reorder(enable == JNI_TRUE);
}

extern "C" JNIEXPORT jboolean JNICALL
Java_Ali_Xanite_SettingsActivity_nativeGetDrawMerge(JNIEnv *, jobject)
{
    return xemu_get_draw_merge() ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT void JNICALL
Java_Ali_Xanite_SettingsActivity_nativeSetDrawMerge(JNIEnv *, jobject, jboolean enable)
{
    xemu_set_draw_merge(enable == JNI_TRUE);
}

extern "C" JNIEXPORT jboolean JNICALL
Java_Ali_Xanite_SettingsActivity_nativeGetBindlessTextures(JNIEnv *, jobject)
{
    return xemu_get_bindless_textures() ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT void JNICALL
Java_Ali_Xanite_SettingsActivity_nativeSetBindlessTextures(JNIEnv *, jobject, jboolean enable)
{
    xemu_set_bindless_textures(enable == JNI_TRUE);
}

extern "C" JNIEXPORT jboolean JNICALL
Java_Ali_Xanite_SettingsActivity_nativeGetAsyncCompile(JNIEnv *, jobject)
{
    return xemu_get_async_compile() ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT void JNICALL
Java_Ali_Xanite_SettingsActivity_nativeSetAsyncCompile(JNIEnv *, jobject, jboolean enable)
{
    xemu_set_async_compile(enable == JNI_TRUE);
}

extern "C" JNIEXPORT jboolean JNICALL
Java_Ali_Xanite_SettingsActivity_nativeGetFrameSkip(JNIEnv *, jobject)
{
    return xemu_get_frame_skip() ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT void JNICALL
Java_Ali_Xanite_SettingsActivity_nativeSetFrameSkip(JNIEnv *, jobject, jboolean enable)
{
    xemu_set_frame_skip(enable == JNI_TRUE);
}

extern "C" JNIEXPORT jint JNICALL
Java_Ali_Xanite_SettingsActivity_nativeGetSubmitFrames(JNIEnv *, jobject)
{
    return static_cast<jint>(xemu_get_submit_frames());
}

extern "C" JNIEXPORT void JNICALL
Java_Ali_Xanite_SettingsActivity_nativeSetSubmitFrames(JNIEnv *, jobject, jint count)
{
    xemu_set_submit_frames(static_cast<int>(count));
}

extern "C" JNIEXPORT jint JNICALL
Java_Ali_Xanite_SettingsActivity_nativeGetTier1Threshold(JNIEnv *, jobject)
{
    return static_cast<jint>(xemu_get_tier1_threshold());
}

extern "C" JNIEXPORT void JNICALL
Java_Ali_Xanite_SettingsActivity_nativeSetTier1Threshold(JNIEnv *, jobject, jint value)
{
    xemu_set_tier1_threshold(static_cast<int>(value));
}

extern "C" JNIEXPORT jboolean JNICALL
Java_Ali_Xanite_SettingsActivity_nativeGetFpJit(JNIEnv *, jobject)
{
    return xemu_get_fp_jit() ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT void JNICALL
Java_Ali_Xanite_SettingsActivity_nativeSetFpJit(JNIEnv *, jobject, jboolean enable)
{
    xemu_set_fp_jit(enable == JNI_TRUE);
    const char *storage = SDL_AndroidGetInternalStoragePath();
    if (storage) {
        char path[PATH_MAX];
        snprintf(path, sizeof(path), "%s/XaniteOG/tb_cache.bin", storage);
        remove(path);
    }
}

extern "C" JNIEXPORT void JNICALL
Java_Ali_Xanite_MainActivity_nativePauseEmulation(JNIEnv *, jobject)
{
    xemu_android_pause_emulation();
}

extern "C" JNIEXPORT void JNICALL
Java_Ali_Xanite_MainActivity_nativeResumeEmulation(JNIEnv *, jobject)
{
    xemu_android_resume_emulation();
}

extern "C" JNIEXPORT void JNICALL
Java_Ali_Xanite_MainActivity_nativeSetReturnToLibraryOnExit(
    JNIEnv *, jobject, jboolean enable)
{
    g_return_to_library_on_exit.store(enable == JNI_TRUE);
}

extern "C" JNIEXPORT void JNICALL
Java_Ali_Xanite_MainActivity_nativeExitEmulation(JNIEnv *, jobject)
{
    xemu_android_request_exit();
}

#ifdef CONFIG_VULKAN
extern "C" JNIEXPORT jboolean JNICALL
Java_Ali_Xanite_GpuDriverHelper_nativeSupportsCustomDriverLoading(JNIEnv *, jclass)
{
    return access("/dev/kgsl-3d0", F_OK) == 0 ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT void JNICALL
Java_Ali_Xanite_GpuDriverHelper_nativeInitializeDriver(
    JNIEnv *env, jclass,
    jstring hookLibDir, jstring customDriverDir,
    jstring customDriverName)
{
    const char *hook_dir = hookLibDir ? env->GetStringUTFChars(hookLibDir, nullptr) : nullptr;
    const char *driver_dir = customDriverDir ? env->GetStringUTFChars(customDriverDir, nullptr) : nullptr;
    const char *driver_name = customDriverName ? env->GetStringUTFChars(customDriverName, nullptr) : nullptr;

    void *handle = nullptr;
    g_vulkan_custom_driver_zip_loaded = false;

    if (driver_name && driver_name[0] != '\0') {
        __android_log_print(ANDROID_LOG_INFO, "xemu-android",
                            "Loading custom Vulkan driver: %s from %s",
                            driver_name, driver_dir ? driver_dir : "(null)");
        handle = adrenotools_open_libvulkan(
            RTLD_NOW,
            ADRENOTOOLS_DRIVER_CUSTOM,
            nullptr,
            hook_dir,
            driver_dir,
            driver_name,
            nullptr,
            nullptr);

        if (handle) {
            g_custom_vulkan_library = handle;
            g_vulkan_custom_driver_zip_loaded = true;
            __android_log_print(ANDROID_LOG_INFO, "xemu-android",
                                "Custom Vulkan driver loaded successfully via adrenotools");
        } else {
            __android_log_print(ANDROID_LOG_WARN, "xemu-android",
                                "adrenotools failed to load custom driver, will fall back to system default");
        }
    } else {
        /*
         * No Turnip/custom ZIP: still dlopen libvulkan through adrenotools with
         * featureFlags=0 (stock driver). That matches the volk dispatch path used
         * when a custom .so is loaded and applies adrenotools hooks from
         * nativeLibraryDir — avoiding OEM-specific quirks from mixing linker-
         * resolved Vulkan symbols with this code path.
         */
        if (hook_dir && hook_dir[0] != '\0') {
            handle = adrenotools_open_libvulkan(
                RTLD_NOW,
                0,
                (driver_dir && driver_dir[0] != '\0') ? driver_dir : nullptr,
                hook_dir,
                nullptr,
                nullptr,
                nullptr,
                nullptr);
        }
        if (handle) {
            g_custom_vulkan_library = handle;
            __android_log_print(ANDROID_LOG_INFO, "xemu-android",
                                "System Vulkan driver opened via adrenotools (hooked dispatch)");
        } else {
            __android_log_print(ANDROID_LOG_INFO, "xemu-android",
                                "Vulkan: default linkage (adrenotools system open not used)");
        }
    }

    if (driver_name) env->ReleaseStringUTFChars(customDriverName, driver_name);
    if (driver_dir) env->ReleaseStringUTFChars(customDriverDir, driver_dir);
    if (hook_dir) env->ReleaseStringUTFChars(hookLibDir, hook_dir);
}
#endif

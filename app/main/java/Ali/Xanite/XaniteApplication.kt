package Ali.Xanite

import android.app.Application
import android.content.Context
import android.os.Build
import android.util.Log
import java.io.File
import java.io.FileOutputStream
import java.io.PrintWriter
import java.io.StringWriter
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

/**
 * Custom Application whose sole job (for now) is to install a crash handler
 * as early as physically possible in the process lifecycle — earlier than
 * any Activity.onCreate(), and earlier than Application.onCreate() itself.
 *
 * Why attachBaseContext() and not onCreate(): Android creates ContentProviders
 * (including ones injected by AndroidX libraries via manifest merging, e.g.
 * androidx.startup.InitializationProvider used by profileinstaller) AFTER
 * attachBaseContext() but BEFORE Application.onCreate(). If a crash is
 * happening that early, a handler installed in onCreate() would never see it.
 *
 * This writes a full stack trace to app-private storage so it can be pulled
 * even when the crash happens before any UI/log stream was attached:
 *   adb shell run-as Ali.Xanite ls files/x1box/debug-logs/
 *   adb shell run-as Ali.Xanite cat files/x1box/debug-logs/crash-<timestamp>.log
 * (requires a debuggable build, which this project's debug variant is)
 */
class XaniteApplication : Application() {

  companion object {
    private const val TAG = "XaniteApplication"
    private const val PREFS_NAME = "xaniteog_prefs"
    private const val PREF_JSRF_PERFORMANCE_MIGRATED = "jsrf_performance_defaults_migrated_v1"
  }

  override fun attachBaseContext(base: Context) {
    super.attachBaseContext(LocaleHelper.applyLocale(base))
    applyJsrfPerformanceDefaults(base)
    // Bring debug logging up before the crash handler is installed, so a
    // crash this early still has somewhere to write to and logcat capture is
    // already running. initialize() is hardened to never throw.
    DebugLog.initialize(base)
    installCrashHandler(base)
  }

  private fun applyJsrfPerformanceDefaults(context: Context) {
    val prefs = context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
    if (prefs.getBoolean(PREF_JSRF_PERFORMANCE_MIGRATED, false)) return

    // A previous build briefly made asynchronous compilation the default.
    // Roll that experiment back once so existing installs receive the same
    // conservative JSRF-friendly default as clean installs.
    prefs.edit()
      .putBoolean("async_compile", false)
      .putBoolean(PREF_JSRF_PERFORMANCE_MIGRATED, true)
      .apply()
  }

  private fun installCrashHandler(context: Context) {
    val previousHandler = Thread.getDefaultUncaughtExceptionHandler()
    Thread.setDefaultUncaughtExceptionHandler { thread, throwable ->
      try {
        writeCrashReport(context, thread, throwable)
      } catch (t: Throwable) {
        // The crash handler must never itself throw during a crash.
        Log.e(TAG, "Failed to write crash report", t)
      }
      try {
        // Put the reason and stack trace into the debug log itself, not just
        // the separate crash file. DebugLog.e() writes error entries
        // synchronously, so this is on disk before we return.
        DebugLog.e(TAG, throwable) { "Uncaught exception on thread ${thread.name}" }
        // Then push everything still buffered (routine log lines and the
        // logcat capture tail) out to disk. Without this the log ends a beat
        // before the crash, which is the whole bug being fixed here.
        DebugLog.flushBlocking()
      } catch (t: Throwable) {
        Log.e(TAG, "Failed to flush debug logs during crash", t)
      }
      // Preserve default behavior (system crash dialog, process death, etc.)
      previousHandler?.uncaughtException(thread, throwable)
    }
  }

  private fun writeCrashReport(context: Context, thread: Thread, throwable: Throwable) {
    val dir = CrashReportManager.crashDir(context)
    dir.mkdirs()

    val stamp = SimpleDateFormat("yyyyMMdd-HHmmss", Locale.US).format(Date())
    val processName = runCatching {
      File("/proc/self/cmdline").readText().trim('\u0000', ' ', '\n')
    }.getOrDefault("unknown")

    val file = File(dir, "crash-$stamp.log")
    val writer = StringWriter()
    PrintWriter(writer).use { pw ->
      pw.println("=== Xanite crash report ===")
      pw.println("Time: $stamp")
      pw.println("Process: $processName")
      pw.println("Thread: ${thread.name}")
      pw.println("Device: ${Build.MANUFACTURER} ${Build.MODEL}")
      pw.println("Android: ${Build.VERSION.RELEASE} (SDK ${Build.VERSION.SDK_INT})")
      pw.println("ABI: ${Build.SUPPORTED_ABIS.joinToString()}")
      pw.println()
      throwable.printStackTrace(pw)
    }

    val reportText = writer.toString()
    // fsync rather than writeText(): the process is about to be torn down and
    // this file is the only record of why.
    FileOutputStream(file).use { stream ->
      stream.write(reportText.toByteArray(Charsets.UTF_8))
      stream.flush()
      try {
        stream.fd.sync()
      } catch (t: Throwable) {
        Log.w(TAG, "Could not fsync crash report", t)
      }
    }
    // Also to logcat in case a session happens to be attached.
    Log.e(TAG, "Uncaught exception written to ${file.absolutePath}", throwable)

    // Copy straight to the clipboard right here, best-effort. This is the
    // "instant" path for JVM-level crashes: by the time GameLibraryActivity's
    // onResume() runs its own CrashReportManager check, this has usually
    // already happened. That later check still runs regardless — it's what
    // catches native (signal-level) crashes, which never reach this handler
    // at all since they don't go through the JVM.
    CrashReportManager.copyTextToClipboard(context, "XaniteOG Gold crash log", reportText)
  }
}

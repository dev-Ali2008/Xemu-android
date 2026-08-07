package Ali.Xanite

import android.content.Context
import android.os.SystemClock
import android.util.Log
import java.io.File
import java.io.FileOutputStream
import java.io.OutputStream
import java.io.PrintWriter
import java.io.RandomAccessFile
import java.io.StringWriter
import java.io.Writer
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import java.util.concurrent.CountDownLatch
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit

object DebugLog {
  const val PREF_ENABLED = "setting_debug_logs_enabled"

  private const val TAG = "xaniteog-android"
  // Same directory CrashReportManager, the native signal handler
  // (xemu_android.cpp) and the session health monitor (ui/xemu.c) already
  // write into. These used to be two different directories after a rename,
  // which is why an exported "debug log" never actually contained the
  // crash-*.log / native-crash-*.log that explained the crash.
  private const val LOG_DIR = CrashReportManager.CRASH_LOG_DIR
  private const val UI_LOG_FILE_NAME = "ui-debug.log"
  private const val NATIVE_LOG_FILE_NAME = "xaniteog-debug.log"
  private const val UI_LOGCAT_FILE_NAME = "ui-logcat.log"
  private const val XANITEOG_LOGCAT_FILE_NAME = "xaniteog-logcat.log"
  private const val MAX_LOG_BYTES = 16L * 1024L * 1024L
  private const val TRIM_KEEP_BYTES = 4L * 1024L * 1024L

  /**
   * How long a crash handler is willing to block waiting for queued log lines
   * to reach disk. Android gives an uncaught-exception handler a limited
   * budget before the system kills the process outright, so this stays short.
   */
  private const val FLUSH_TIMEOUT_MS = 1_500L

  /** Logcat lines worth flushing to disk immediately instead of buffering. */
  private val FATAL_LINE_MARKERS = arrayOf(
    "FATAL EXCEPTION",
    "Fatal signal",
    "AndroidRuntime: FATAL",
    "abort_message",
    "signal 11 (SIGSEGV)",
    "signal 6 (SIGABRT)",
    "signal 7 (SIGBUS)",
    "signal 4 (SIGILL)",
    "signal 8 (SIGFPE)",
    "=== Xanite NATIVE crash report ===",
  )

  @Volatile private var appContext: Context? = null
  @PublishedApi
  @Volatile
  internal var enabled = false
  @Volatile private var logcatProcess: java.lang.Process? = null
  @Volatile private var logcatThread: Thread? = null
  @Volatile private var activeLogcatPath: String? = null
  @Volatile private var logcatWriter: Writer? = null

  /**
   * Serializes appends to the UI log. The crash path writes on the crashing
   * thread while the background writer may still be draining queued lines,
   * and those two must never interleave a half-written entry.
   */
  private val uiLogLock = Any()

  /** Guards [logcatWriter] between the capture thread and [flushBlocking]. */
  private val logcatWriterLock = Any()

  private val writerExecutor = Executors.newSingleThreadExecutor { runnable ->
    Thread(runnable, "xaniteog-debug-log-writer").apply {
      isDaemon = true
    }
  }

  fun initialize(context: Context) {
    // Hardened on purpose: this runs first thing in onCreate() on every
    // Activity, before any try/catch in caller code. It must never throw,
    // even if prefs were restored from another device/OS via Auto Backup
    // and are in an unexpected state.
    try {
      // applicationContext can still be null this early when called from
      // Application.attachBaseContext(); the base context works just as well
      // for prefs and filesDir, so fall back to it rather than losing logging
      // for the rest of the process lifetime.
      val applicationContext = context.applicationContext ?: context
      appContext = applicationContext
      enabled = applicationContext
        .getSharedPreferences("xaniteog_prefs", Context.MODE_PRIVATE)
        .getBoolean(PREF_ENABLED, false)
      ensureLogcatCaptureState(applicationContext)
    } catch (t: Throwable) {
      Log.e(TAG, "DebugLog.initialize failed, continuing without debug logging", t)
    }
  }

  fun setEnabled(context: Context, value: Boolean, resetLogs: Boolean = false) {
    initialize(context)
    if (!value) {
      stopLogcatCapture()
    }
    if (resetLogs) {
      clearLogs(context)
    }
    enabled = value
    if (value) {
      ensureLogcatCaptureState(context.applicationContext)
    }
    if (value) {
      i(TAG) { "Debug logging enabled" }
    } else {
      Log.i(TAG, "Debug logging disabled")
    }
  }

  fun hasAnyLog(context: Context): Boolean {
    return uiLogFile(context).isFile ||
      nativeLogFile(context).isFile ||
      uiLogcatFile(context).isFile ||
      xaniteogLogcatFile(context).isFile ||
      // A crash with debug logging switched off still leaves a crash report,
      // and that is the single most useful thing to be able to export.
      CrashReportManager.latestCrashFile(context) != null ||
      CrashReportManager.latestSessionLogFile(context) != null
  }

  fun exportDefaultFileName(): String {
    val stamp = SimpleDateFormat("yyyyMMdd-HHmmss", Locale.US).format(Date())
    return "xaniteog-debug-$stamp.log"
  }

  @Throws(Exception::class)
  fun exportCombined(context: Context, outputStream: OutputStream) {
    val text = buildCombinedLogText(context)
      ?: throw IllegalStateException("No debug log captured yet.")
    outputStream.bufferedWriter().use { it.write(text) }
  }

  /**
   * Copies the full combined debug log (UI log + native log + both logcat
   * captures) straight to the clipboard. Returns false if there's nothing
   * captured yet, e.g. debug logging was never turned on.
   */
  fun copyToClipboard(context: Context): Boolean {
    val text = buildCombinedLogText(context) ?: return false
    CrashReportManager.copyTextToClipboard(context, "XaniteOG Gold debug log", text)
    return true
  }

  /**
   * Builds the full report the user actually shares with us. The crash and
   * session sections come first: when someone exports a log it is almost
   * always *because* something crashed, and burying the reason under
   * megabytes of routine logcat made it easy to miss. Returns null only when
   * nothing at all has been captured.
   *
   * Anything still buffered is flushed first, so exporting right after a
   * recovered error does not hand back a log that stops one line short.
   */
  private fun buildCombinedLogText(context: Context): String? {
    // Export/copy is triggered from a button tap on the main thread, so cap
    // the wait well below the crash handler's budget.
    flushBlocking(500L)

    val sections = mutableListOf<Pair<String, File>>()

    // The crash section is emitted separately below so it can carry the
    // system tombstone alongside our own report.
    val crashFile = CrashReportManager.latestCrashFile(context)?.takeIf { it.isFile }

    CrashReportManager.latestSessionLogFile(context)
      ?.takeIf { it.isFile }
      ?.let { sections.add("Last Session Health Log (${it.name})" to it) }

    uiLogFile(context).takeIf { it.isFile }?.let { sections.add("UI Debug Log" to it) }
    nativeLogFile(context).takeIf { it.isFile }
      ?.let { sections.add("XaniteOG Gold Native Debug Log" to it) }
    uiLogcatFile(context).takeIf { it.isFile }?.let { sections.add("UI Logcat Capture" to it) }
    xaniteogLogcatFile(context).takeIf { it.isFile }
      ?.let { sections.add("XaniteOG Gold Logcat Capture" to it) }

    if (sections.isEmpty() && crashFile == null) {
      return null
    }

    return buildString {
      if (crashFile != null) {
        appendLine("=== Last Crash Report (${crashFile.name}) ===")
        // Not the raw file: buildEnrichedCrashText also attaches the OS's own
        // debuggerd tombstone for the same crash when one is available. Our
        // handler can only record raw PC addresses, which cannot be resolved
        // to functions without the process memory map; the tombstone carries
        // module names and offsets, so this is the difference between "it
        // crashed somewhere" and a symbolicated stack.
        appendLine(
          try {
            CrashReportManager.buildEnrichedCrashText(context, crashFile)
          } catch (error: Exception) {
            "(failed to read ${crashFile.name}: $error)"
          }
        )
      }

      sections.forEachIndexed { index, (title, file) ->
        if (index > 0 || crashFile != null) {
          appendLine()
        }
        appendLine("=== $title ===")
        try {
          file.bufferedReader().useLines { lines ->
            lines.forEach(::appendLine)
          }
        } catch (error: Exception) {
          appendLine("(failed to read ${file.name}: $error)")
        }
      }
    }
  }

  fun clearLogs(context: Context? = appContext) {
    context ?: return
    stopLogcatCapture()
    uiLogFile(context).delete()
    nativeLogFile(context).delete()
    uiLogcatFile(context).delete()
    xaniteogLogcatFile(context).delete()
  }

  fun resetLogs(context: Context? = appContext) {
    context ?: return
    val shouldResumeCapture = enabled
    clearLogs(context)
    if (shouldResumeCapture) {
      ensureLogcatCaptureState(context.applicationContext)
    }
  }

  inline fun d(tag: String, message: () -> String) {
    if (!enabled) {
      return
    }
    val text = message()
    Log.d(tag, text)
    appendUiLine("D", tag, text)
  }

  inline fun i(tag: String, message: () -> String) {
    if (!enabled) {
      return
    }
    val text = message()
    Log.i(tag, text)
    appendUiLine("I", tag, text)
  }

  inline fun w(tag: String, message: () -> String) {
    if (!enabled) {
      return
    }
    val text = message()
    Log.w(tag, text)
    appendUiLine("W", tag, text)
  }

  inline fun e(tag: String, throwable: Throwable? = null, message: () -> String) {
    val text = message()
    if (throwable != null) {
      Log.e(tag, text, throwable)
    } else {
      Log.e(tag, text)
    }
    if (enabled) {
      appendUiLine("E", tag, text, throwable)
    }
  }

  fun nativeLogFile(context: Context): File {
    return File(logDir(context), NATIVE_LOG_FILE_NAME)
  }

  @PublishedApi
  internal fun appendUiLine(
    level: String,
    tag: String,
    message: String,
    throwable: Throwable? = null,
  ) {
    val context = appContext ?: return
    val text = buildString {
      append(timestamp())
      append(' ')
      append(level)
      append('/')
      append(tag)
      append(": ")
      appendLine(message)
      if (throwable != null) {
        appendLine(stackTraceFor(throwable))
      }
    }

    // Error entries are the ones that carry the crash reason and the stack
    // trace. Handing those to the background writer is exactly how they got
    // lost before: a crashing process dies before that thread is scheduled
    // again, so the log ended right *before* the interesting part. Write them
    // on the calling thread and fsync so they survive process death.
    if (level == "E") {
      try {
        writeUiLine(context, text, sync = true)
      } catch (_: Exception) {
      }
      return
    }

    // Everything else stays asynchronous so ordinary logging never adds
    // latency to the emulator or UI threads.
    try {
      writerExecutor.execute {
        try {
          writeUiLine(context, text, sync = false)
        } catch (_: Exception) {
        }
      }
    } catch (_: Exception) {
    }
  }

  private fun writeUiLine(context: Context, text: String, sync: Boolean) {
    synchronized(uiLogLock) {
      val file = uiLogFile(context)
      file.parentFile?.mkdirs()
      trimFileIfNeeded(file)
      FileOutputStream(file, true).use { stream ->
        stream.write(text.toByteArray(Charsets.UTF_8))
        stream.flush()
        if (sync) {
          try {
            stream.fd.sync()
          } catch (_: Exception) {
          }
        }
      }
    }
  }

  /**
   * Drains queued log lines and force-flushes the logcat capture buffer to
   * disk, blocking up to [timeoutMs]. Call this from a crash handler before
   * letting the process die — without it the tail of the log (the part that
   * explains the crash) is still sitting in memory when the process is torn
   * down. Safe to call from anywhere: it never throws.
   */
  fun flushBlocking(timeoutMs: Long = FLUSH_TIMEOUT_MS) {
    flushLogcatWriter()
    try {
      // A single-threaded executor runs tasks in order, so once this one
      // completes every line queued before it has already been written.
      val drained = CountDownLatch(1)
      writerExecutor.execute { drained.countDown() }
      drained.await(timeoutMs, TimeUnit.MILLISECONDS)
    } catch (_: Exception) {
    }
    flushLogcatWriter()
  }

  private fun flushLogcatWriter() {
    try {
      synchronized(logcatWriterLock) {
        logcatWriter?.flush()
      }
    } catch (_: Exception) {
    }
  }

  private fun uiLogFile(context: Context): File {
    return File(logDir(context), UI_LOG_FILE_NAME)
  }

  private fun uiLogcatFile(context: Context): File {
    return File(logDir(context), UI_LOGCAT_FILE_NAME)
  }

  private fun xaniteogLogcatFile(context: Context): File {
    return File(logDir(context), XANITEOG_LOGCAT_FILE_NAME)
  }

  private fun currentProcessLogcatFile(context: Context): File {
    val processName = runCatching {
      File("/proc/self/cmdline").readText().trim('\u0000', ' ', '\n')
    }.getOrDefault("")
    return if (processName.endsWith(":xaniteog")) {
      xaniteogLogcatFile(context)
    } else {
      uiLogcatFile(context)
    }
  }

  private fun logDir(context: Context): File {
    return File(context.filesDir, LOG_DIR)
  }

  /**
   * Caps the log size while keeping the most recent [TRIM_KEEP_BYTES].
   * This used to blank the file outright, which meant a long session that
   * crossed the size cap threw away the newest entries too — the very ones
   * describing what the app was doing when it crashed.
   */
  private fun trimFileIfNeeded(file: File) {
    if (!file.isFile || file.length() <= MAX_LOG_BYTES) {
      return
    }
    try {
      val keep = TRIM_KEEP_BYTES.toInt()
      val tail = ByteArray(keep)
      RandomAccessFile(file, "r").use { raf ->
        raf.seek(raf.length() - keep)
        raf.readFully(tail)
      }
      // Drop the leading partial line so the file always starts on a boundary.
      var start = 0
      while (start < tail.size && tail[start] != '\n'.code.toByte()) {
        start++
      }
      if (start < tail.size) {
        start++
      }
      FileOutputStream(file, false).use { stream ->
        stream.write("=== log trimmed - older entries dropped ===\n".toByteArray(Charsets.UTF_8))
        stream.write(tail, start, tail.size - start)
        stream.flush()
      }
    } catch (_: Exception) {
      // Last resort: still enforce the cap rather than growing without bound.
      try {
        file.writeText("")
      } catch (_: Exception) {
      }
    }
  }

  private fun timestamp(): String {
    return SimpleDateFormat("yyyy-MM-dd HH:mm:ss.SSS", Locale.US).format(Date())
  }

  private fun stackTraceFor(throwable: Throwable): String {
    return StringWriter().also { writer ->
      PrintWriter(writer).use { printer ->
        throwable.printStackTrace(printer)
      }
    }.toString().trimEnd()
  }

  private fun ensureLogcatCaptureState(context: Context) {
    if (!enabled) {
      stopLogcatCapture()
      return
    }

    val targetFile = currentProcessLogcatFile(context)
    if (activeLogcatPath == targetFile.absolutePath && logcatProcess != null) {
      return
    }

    stopLogcatCapture()
    startLogcatCapture(targetFile)
  }

  private fun startLogcatCapture(targetFile: File) {
    try {
      targetFile.parentFile?.mkdirs()
      trimFileIfNeeded(targetFile)
      val process = ProcessBuilder(
        "logcat",
        "-T",
        "1",
        "-v",
        "threadtime",
        "--pid=${android.os.Process.myPid()}",
      )
        .redirectErrorStream(true)
        .start()

      val thread = Thread({
        try {
          process.inputStream.bufferedReader().use { reader ->
            FileOutputStream(targetFile, true).bufferedWriter(Charsets.UTF_8).use { writer ->
              synchronized(logcatWriterLock) { logcatWriter = writer }
              try {
                var pendingLines = 0
                var lastFlushMs = SystemClock.elapsedRealtime()
                while (true) {
                  val line = reader.readLine() ?: break
                  synchronized(logcatWriterLock) {
                    writer.appendLine(line)
                    pendingLines++
                    val nowMs = SystemClock.elapsedRealtime()
                    // A crash line is the last thing this process will ever
                    // log, so it can never be left sitting in the buffer —
                    // that is precisely how the capture used to stop just
                    // short of the crash. Flush it the moment it appears.
                    if (isFatalLine(line)) {
                      writer.flush()
                      pendingLines = 0
                      lastFlushMs = nowMs
                    } else if (pendingLines >= 64 || nowMs - lastFlushMs >= 250L) {
                      writer.flush()
                      pendingLines = 0
                      lastFlushMs = nowMs
                    }
                  }
                }
              } finally {
                synchronized(logcatWriterLock) {
                  try {
                    writer.flush()
                  } catch (_: Exception) {
                  }
                  if (logcatWriter === writer) {
                    logcatWriter = null
                  }
                }
              }
            }
          }
        } catch (_: Exception) {
        }
      }, "xaniteog-logcat-capture").apply {
        isDaemon = true
        start()
      }

      logcatProcess = process
      logcatThread = thread
      activeLogcatPath = targetFile.absolutePath
    } catch (error: Exception) {
      Log.w(TAG, "Failed to start logcat capture", error)
    }
  }

  private fun isFatalLine(line: String): Boolean {
    for (marker in FATAL_LINE_MARKERS) {
      if (line.contains(marker)) {
        return true
      }
    }
    return false
  }

  private fun stopLogcatCapture() {
    flushLogcatWriter()
    logcatProcess?.destroy()
    logcatProcess = null
    logcatThread?.interrupt()
    logcatThread = null
    activeLogcatPath = null
  }
}

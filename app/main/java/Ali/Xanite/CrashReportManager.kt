package Ali.Xanite

import android.app.ActivityManager
import android.app.ApplicationExitInfo
import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import android.os.Build
import android.util.Log
import java.io.File
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

/**
 * Single home for "what happened in the last crash" — written to by both the
 * JVM crash handler (XaniteApplication) and the native signal handler
 * (xemu_android.cpp, via nativeReportCrash JNI callback), and read by
 * GameLibraryActivity (to auto-copy + notify right after a crash) and
 * SettingsActivity (to show/copy/dismiss the last crash on demand).
 *
 * Also covers session health logs: xemu_android.cpp writes a
 * session-<pid>.log for every game session (open to close, independent of
 * the Debug Logging toggle) with periodic frame/FPS/GL-error summaries, and
 * flags specific lines with WARN markers when it detects the game never
 * booted, went unstable, or hit repeated rendering errors. This class is
 * what recognizes those markers and surfaces them the same way as a crash —
 * copy to clipboard + notify — without spamming the user after every normal,
 * healthy play session (which also produces a session log, just with no
 * WARN markers in it).
 *
 * All of the above write into the SAME directory DebugLog uses
 * (files/x1box/debug-logs) so "Export Debug Log" in Settings naturally picks
 * up crash and session files too, in addition to the dedicated actions here.
 * DebugLog.LOG_DIR is defined from CRASH_LOG_DIR below precisely so these two
 * can never drift apart again.
 */
object CrashReportManager {
  private const val TAG = "CrashReportManager"
  const val CRASH_LOG_DIR = "x1box/debug-logs"
  private const val PREF_LAST_SEEN_CRASH = "last_seen_crash_path"
  private const val PREF_LAST_SEEN_SESSION_ISSUE = "last_seen_session_issue_path"
  // Must match the store every other component uses (SettingsActivity,
  // XaniteApplication, LocaleHelper, OrientationPreferences, and native code
  // via kPrefsName in xemu_android.cpp). This object was left pointing at an
  // old name after a rename, so SettingsActivity wrote PREF_ACTIVE_DEV_LOGS
  // into xaniteog_prefs while isActiveDevLogsEnabled() read x1box_prefs and
  // always saw false - which meant XEMU_ANDROID_DEV_LOGS was permanently "0"
  // and the native session health log was never written at all.
  private const val PREFS_NAME = "xaniteog_prefs"

  /**
   * Off by default. Gates the session health monitor in xemu_android.cpp/
   * ui/xemu.c (boot-stall/instability/rendering-error detection) — when
   * off, no session-*.log is written at all, not just hidden from the UI.
   * MainActivity reads this and passes it to native code as the
   * XEMU_ANDROID_DEV_LOGS env var via nativeSetenv() before launch.
   */
  const val PREF_ACTIVE_DEV_LOGS = "setting_active_dev_logs_enabled"

  fun isActiveDevLogsEnabled(context: Context): Boolean =
    prefs(context).getBoolean(PREF_ACTIVE_DEV_LOGS, false)

  private val JAVA_CRASH_PREFIX = "crash-"
  private val NATIVE_CRASH_PREFIX = "native-crash-"
  private val SESSION_LOG_PREFIX = "session-"
  private val MAPS_LOG_PREFIX = "maps-"

  /** Markers xemu_android.cpp writes into a session log when something's wrong. */
  private val SESSION_ANOMALY_MARKERS = listOf("BOOT STALL DETECTED", "SESSION UNSTABLE")

  fun crashDir(context: Context): File = File(context.filesDir, CRASH_LOG_DIR)

  /** Most recent crash file (Java or native), or null if none exist. */
  fun latestCrashFile(context: Context): File? {
    val dir = crashDir(context)
    if (!dir.isDirectory) return null
    return dir.listFiles { f ->
      f.isFile && (f.name.startsWith(JAVA_CRASH_PREFIX) || f.name.startsWith(NATIVE_CRASH_PREFIX))
    }?.maxByOrNull { it.lastModified() }
  }

  /** True if there's a crash file the user hasn't already been shown. */
  fun hasUnseenCrash(context: Context): Boolean {
    val latest = latestCrashFile(context) ?: return false
    val lastSeen = prefs(context).getString(PREF_LAST_SEEN_CRASH, null)
    return latest.absolutePath != lastSeen
  }

  /** Marks the current latest crash as having been shown to the user. */
  fun markCrashSeen(context: Context) {
    val latest = latestCrashFile(context) ?: return
    prefs(context).edit().putString(PREF_LAST_SEEN_CRASH, latest.absolutePath).apply()
  }

  /**
   * Copies the latest crash file's content to the clipboard.
   * Returns true if a crash file existed and was copied.
   */
  fun copyLatestCrashToClipboard(context: Context): Boolean {
    val latest = latestCrashFile(context) ?: return false
    return try {
      copyTextToClipboard(context, "XaniteOG crash log", buildEnrichedCrashText(context, latest))
      true
    } catch (t: Throwable) {
      Log.e(TAG, "Failed to copy crash log to clipboard", t)
      false
    }
  }

  /**
   * Our own crash file plus, when available, the real system tombstone for
   * that same crash pulled via ApplicationExitInfo (Android 11+). This
   * matters most for the native handler's own crash-time backtrace attempt
   * (via _Unwind_Backtrace in xemu_android.cpp) which can legitimately
   * produce zero usable frames for some SIGABRT cases — the OS's own
   * debuggerd does not have that limitation and is the actual source of
   * truth for "what was on the stack".
   */
  internal fun buildEnrichedCrashText(context: Context, crashFile: File): String {
    // Resolve the raw PCs to module+offset first - that is the part that
    // makes the report actionable, and unlike the tombstone it always works.
    val ownReport = annotateBacktrace(context, crashFile)
    val tombstone = readMatchingSystemTombstone(context, crashFile.lastModified())
    return if (tombstone != null) {
      "$ownReport\n\n=== System tombstone (via ApplicationExitInfo) ===\n$tombstone"
    } else {
      ownReport
    }
  }

  /**
   * One executable mapping out of /proc/self/maps.
   *
   * [fileOffset] matters: a single .so is mapped in several segments and only
   * the first starts at file offset 0, so `pc - start` alone would give the
   * wrong answer for any frame in a later segment.
   */
  private data class MappedRegion(
    val start: Long,
    val end: Long,
    val fileOffset: Long,
    val path: String,
  ) {
    val name: String get() = path.substringAfterLast('/')
    fun offsetFor(pc: Long): Long = pc - start + fileOffset
  }

  /**
   * Records where each native library happens to be loaded in this process.
   *
   * The native signal handler can only record raw absolute PC addresses -
   * resolving them needs the process memory map, which dies with the process.
   * Capturing it up front is what turns "#0 pc 0x7dddf7fa84" (meaningless on
   * its own, since ASLR moves it every launch) into "libxemu.so+0x3a84",
   * which ndk-stack and addr2line can resolve directly against the unstripped
   * library.
   *
   * Call once per process after the native libraries are loaded. Cheap: one
   * small sequential read of a kernel-generated file.
   */
  fun captureMemoryMapSnapshot(context: Context) {
    try {
      val dir = crashDir(context)
      dir.mkdirs()
      val pid = android.os.Process.myPid()
      val target = File(dir, "$MAPS_LOG_PREFIX$pid.log")
      val executableMappings = File("/proc/self/maps").useLines { lines ->
        lines.filter { line ->
          // Executable, file-backed mappings only. Anonymous mappings and
          // non-executable data segments can never contain a crash PC.
          val perms = line.substringAfter(' ', "").substringBefore(' ')
          perms.length >= 3 && perms[2] == 'x' && line.contains('/')
        }.joinToString("\n")
      }
      if (executableMappings.isNotEmpty()) {
        target.writeText(executableMappings)
      }
    } catch (t: Throwable) {
      Log.w(TAG, "Could not capture memory map snapshot", t)
    }
  }

  /**
   * Re-takes the snapshot once the session is running.
   *
   * The GPU driver and other Vulkan libraries are dlopen'd when the renderer
   * starts, which is after [captureMemoryMapSnapshot] runs at launch - frames
   * inside them resolve to "not in any mapped library" against a launch-time
   * snapshot alone. Mappings are only ever added, so the later snapshot is
   * strictly better and simply replaces the earlier one.
   *
   * Runs on a short-lived daemon thread: file I/O, so never on the UI thread,
   * and it must not keep the process alive on its own.
   */
  fun scheduleMemoryMapRefresh(context: Context, delayMs: Long = 20_000L) {
    val appContext = context.applicationContext ?: context
    Thread({
      try {
        Thread.sleep(delayMs)
      } catch (_: InterruptedException) {
        return@Thread
      }
      captureMemoryMapSnapshot(appContext)
    }, "xanite-maps-refresh").apply {
      isDaemon = true
      start()
    }
  }

  private fun parseMapsFile(file: File): List<MappedRegion> {
    return try {
      file.useLines { lines ->
        lines.mapNotNull { line ->
          // 7ddc9a0000-7ddd12c000 r-xp 00000000 fd:03 12345 /path/lib.so
          val parts = line.trim().split(Regex("\\s+"))
          if (parts.size < 6) return@mapNotNull null
          val range = parts[0].split('-')
          if (range.size != 2) return@mapNotNull null
          val path = parts.subList(5, parts.size).joinToString(" ")
          if (!path.startsWith("/")) return@mapNotNull null
          MappedRegion(
            start = range[0].toLongOrNull(16) ?: return@mapNotNull null,
            end = range[1].toLongOrNull(16) ?: return@mapNotNull null,
            fileOffset = parts[2].toLongOrNull(16) ?: 0L,
            path = path,
          )
        }.toList()
      }
    } catch (t: Throwable) {
      Log.e(TAG, "Failed to parse maps snapshot ${file.name}", t)
      emptyList()
    }
  }

  /**
   * Rewrites each "#N pc 0x..." line in a native crash report to also name the
   * library and the offset inside it, using the map snapshot captured for the
   * same pid. Returns the report unchanged if no snapshot is available.
   */
  private fun annotateBacktrace(context: Context, crashFile: File): String {
    val report = crashFile.readText()
    // native-crash-<pid>.log pairs with maps-<pid>.log.
    val pid = crashFile.name
      .removePrefix(NATIVE_CRASH_PREFIX)
      .removeSuffix(".log")
      .toIntOrNull() ?: return report
    val mapsFile = File(crashDir(context), "$MAPS_LOG_PREFIX$pid.log")
    if (!mapsFile.isFile) return report
    val regions = parseMapsFile(mapsFile)
    if (regions.isEmpty()) return report

    val frameRegex = Regex("""^(\s*#\d+ pc )0x([0-9a-fA-F]+)\s*$""")
    val annotated = report.lines().joinToString("\n") { line ->
      val match = frameRegex.find(line) ?: return@joinToString line
      val pc = match.groupValues[2].toLongOrNull(16) ?: return@joinToString line
      val region = regions.firstOrNull { pc >= it.start && pc < it.end }
        ?: return@joinToString "$line  (address not in any mapped library)"
      "$line  ${region.name}+0x${java.lang.Long.toHexString(region.offsetFor(pc))}"
    }

    return buildString {
      appendLine(annotated.trimEnd())
      appendLine()
      appendLine("--- Symbolication ---")
      appendLine("Offsets above are resolved against the memory map captured at launch.")
      appendLine("To get function names and line numbers, run against the UNSTRIPPED library:")
      appendLine("  addr2line -Cfie <obj>/arm64-v8a/libxemu.so <offset> [<offset> ...]")
    }
  }

  /**
   * Looks up the most recent crash-reason process exit whose timestamp is
   * close to [aroundTimeMs] and returns its full tombstone text, or null if
   * unavailable (pre-Android 11, no matching exit found, or it's too old to
   * plausibly be the same crash).
   */
  private fun readMatchingSystemTombstone(context: Context, aroundTimeMs: Long): String? {
    if (Build.VERSION.SDK_INT < Build.VERSION_CODES.R) return null
    return try {
      val am = context.getSystemService(Context.ACTIVITY_SERVICE) as? ActivityManager ?: return null
      val infos = am.getHistoricalProcessExitReasons(context.packageName, 0, 10)
      val match = infos
        .filter {
          it.reason == ApplicationExitInfo.REASON_CRASH_NATIVE ||
            it.reason == ApplicationExitInfo.REASON_CRASH
        }
        .minByOrNull { kotlin.math.abs(it.timestamp - aroundTimeMs) }
        ?: return null
      // Only attach it if it's plausibly the same crash - otherwise this
      // could be pulling in an unrelated exit from earlier history.
      if (kotlin.math.abs(match.timestamp - aroundTimeMs) > 60_000) return null
      match.traceInputStream?.bufferedReader()?.use { it.readText() }
    } catch (t: Throwable) {
      Log.e(TAG, "Failed to read system tombstone via ApplicationExitInfo", t)
      null
    }
  }

  /** Most recent session health log, or null if no game has been launched yet. */
  fun latestSessionLogFile(context: Context): File? {
    val dir = crashDir(context)
    if (!dir.isDirectory) return null
    return dir.listFiles { f ->
      f.isFile && f.name.startsWith(SESSION_LOG_PREFIX)
    }?.maxByOrNull { it.lastModified() }
  }

  /** True if the given session log contains a boot-stall/instability marker. */
  private fun sessionLogHasAnomaly(file: File): Boolean = try {
    file.useLines { lines -> lines.any { line -> SESSION_ANOMALY_MARKERS.any { line.contains(it) } } }
  } catch (t: Throwable) {
    Log.e(TAG, "Failed to scan session log for anomalies", t)
    false
  }

  /**
   * True only if the latest session actually hit a boot stall / instability
   * / rendering-error condition AND the user hasn't already been shown it —
   * a normal, healthy session also produces a log file, but this stays
   * false for those so the user isn't notified after every ordinary session.
   */
  fun hasUnseenSessionIssue(context: Context): Boolean {
    val latest = latestSessionLogFile(context) ?: return false
    if (!sessionLogHasAnomaly(latest)) return false
    val lastSeen = prefs(context).getString(PREF_LAST_SEEN_SESSION_ISSUE, null)
    return latest.absolutePath != lastSeen
  }

  /** Marks the current latest session issue as having been shown to the user. */
  fun markSessionIssueSeen(context: Context) {
    val latest = latestSessionLogFile(context) ?: return
    prefs(context).edit().putString(PREF_LAST_SEEN_SESSION_ISSUE, latest.absolutePath).apply()
  }

  /** Copies the latest session log's full content to the clipboard. */
  fun copyLatestSessionLogToClipboard(context: Context): Boolean {
    val latest = latestSessionLogFile(context) ?: return false
    return try {
      copyTextToClipboard(context, "XaniteOG session log", latest.readText())
      true
    } catch (t: Throwable) {
      Log.e(TAG, "Failed to copy session log to clipboard", t)
      false
    }
  }

  /** Short human-readable status for the Settings "Last Session" row. */
  fun latestSessionStatusLabel(context: Context): String? {
    val latest = latestSessionLogFile(context) ?: return null
    val timestamp = SimpleDateFormat("yyyy-MM-dd HH:mm:ss", Locale.US).format(Date(latest.lastModified()))
    return if (sessionLogHasAnomaly(latest)) {
      "Issue detected: $timestamp"
    } else {
      "No issues: $timestamp"
    }
  }

  /**
   * Deletes every session log except the most recent one. Called on app
   * resume so a long history of past sessions doesn't accumulate on disk —
   * only "how did the last session go" is ever actually needed.
   */
  fun pruneOldSessionLogs(context: Context) {
    val dir = crashDir(context)
    pruneAllButNewest(dir, SESSION_LOG_PREFIX)
    // Map snapshots accumulate one per launch and are only useful for as long
    // as a crash report referencing that pid still exists. Keep a few so the
    // snapshot for the crash being investigated is not pruned out from under
    // it, but do not let them pile up.
    pruneAllButNewest(dir, MAPS_LOG_PREFIX, keep = 3)
  }

  private fun pruneAllButNewest(dir: File, prefix: String, keep: Int = 1) {
    val files = dir.listFiles { f -> f.isFile && f.name.startsWith(prefix) } ?: return
    files.sortedByDescending { it.lastModified() }
      .drop(keep)
      .forEach { it.delete() }
  }

  /**
   * Copies [text] directly to the clipboard without touching disk again —
   * used by the JVM crash handler right as it's building the report, so we
   * don't need a second file read in the same breath as the crash.
   * Safe to call from a crash handler: any failure here is swallowed so it
   * can never prevent the real crash from propagating to the OS.
   */
  fun copyTextToClipboard(context: Context, label: String, text: String) {
    try {
      val clipboard = context.getSystemService(Context.CLIPBOARD_SERVICE) as? ClipboardManager
        ?: return
      clipboard.setPrimaryClip(ClipData.newPlainText(label, text))
    } catch (t: Throwable) {
      Log.e(TAG, "Failed to set clipboard", t)
    }
  }

  /** Human-readable "how long ago" string for the last-crash label in Settings. */
  fun latestCrashTimestampLabel(context: Context): String? {
    val latest = latestCrashFile(context) ?: return null
    return SimpleDateFormat("yyyy-MM-dd HH:mm:ss", Locale.US).format(Date(latest.lastModified()))
  }

  /** Deletes every crash file (both Java and native). Used by "Dismiss"/"Clear" in Settings. */
  fun clearAll(context: Context) {
    crashDir(context).listFiles { f ->
      f.isFile && (f.name.startsWith(JAVA_CRASH_PREFIX) || f.name.startsWith(NATIVE_CRASH_PREFIX))
    }?.forEach { it.delete() }
    prefs(context).edit().remove(PREF_LAST_SEEN_CRASH).apply()
  }

  /** Deletes every session log. Used by "Dismiss" for the Last Session row. */
  fun clearSessionLogs(context: Context) {
    crashDir(context).listFiles { f ->
      f.isFile && f.name.startsWith(SESSION_LOG_PREFIX)
    }?.forEach { it.delete() }
    prefs(context).edit().remove(PREF_LAST_SEEN_SESSION_ISSUE).apply()
  }

  private fun prefs(context: Context) =
    context.applicationContext.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
}

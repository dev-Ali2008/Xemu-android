package Ali.Xanite

import android.content.Context
import android.util.Log

/**
 * Thin binding over libxanite_license.so. Every decision is made in native
 * code; this object only forwards a Context and maps status codes.
 */
object LicenseNative {
  private const val TAG = "LicenseNative"

  const val VALID = 0
  const val MISSING = 1
  const val MALFORMED = 2
  const val BAD_SIGNATURE = 3
  const val HWID_MISMATCH = 4
  const val INTERNAL_ERROR = 5

  private val available: Boolean = try {
    System.loadLibrary("xanite_license")
    true
  } catch (t: Throwable) {
    // A stripped or corrupted install. Treated as "not activated" rather than
    // "activated", so a missing .so cannot be used to bypass the gate.
    Log.e(TAG, "libxanite_license.so unavailable", t)
    false
  }

  fun verify(context: Context): Int =
    if (!available) INTERNAL_ERROR else runCatching {
      nativeVerifyLicense(context.applicationContext)
    }.getOrElse {
      Log.e(TAG, "verify failed", it)
      INTERNAL_ERROR
    }

  fun hwid(context: Context): String? =
    if (!available) null else runCatching {
      nativeDeriveHwid(context.applicationContext)
    }.getOrNull()

  /** `patreonId`, `slotsUsed`, `maxSlots`, `createdMs` — or null if unlicensed. */
  fun info(context: Context): LicenseInfo? {
    if (!available) return null
    val raw = runCatching { nativeGetLicenseInfo(context.applicationContext) }.getOrNull()
      ?: return null
    val parts = raw.split('\n')
    if (parts.size != 4) return null
    return LicenseInfo(
      patreonId = parts[0],
      slotsUsed = parts[1].toIntOrNull() ?: 0,
      maxSlots = parts[2].toIntOrNull() ?: BuildConfig.MAX_DEVICE_SLOTS,
      createdMs = parts[3].toLongOrNull() ?: 0L,
    )
  }

  /** No-op in debug builds; aborts the process in release if the APK was re-signed. */
  fun enforceApkSignature(context: Context) {
    if (!available) return
    runCatching { nativeEnforceApkSignature(context.applicationContext) }
  }

  data class LicenseInfo(
    val patreonId: String,
    val slotsUsed: Int,
    val maxSlots: Int,
    val createdMs: Long,
  )

  @JvmStatic private external fun nativeVerifyLicense(context: Context): Int
  @JvmStatic private external fun nativeDeriveHwid(context: Context): String?
  @JvmStatic private external fun nativeGetLicenseInfo(context: Context): String?
  @JvmStatic private external fun nativeEnforceApkSignature(context: Context)
}

package Ali.Xanite

import android.content.Context
import java.io.File

/**
 * Boot-time gate for the Gold skin.
 *
 * Enforcement is a compile-time property of the flavor: green ships with
 * [BuildConfig.GOLD_LICENSING_REQUIRED] false and never sees the gateway.
 */
object LicenseGate {

  const val LICENSE_FILE = "license.bin"

  fun isEnforced(context: Context): Boolean {
    if (!BuildConfig.GOLD_LICENSING_REQUIRED) return false
    // Release builds abort here if the APK carries the wrong certificate.
    LicenseNative.enforceApkSignature(context)
    return true
  }

  fun isActivated(context: Context): Boolean =
    LicenseNative.verify(context) == LicenseNative.VALID

  fun status(context: Context): Int = LicenseNative.verify(context)

  fun info(context: Context): LicenseNative.LicenseInfo? = LicenseNative.info(context)

  fun hwid(context: Context): String? = LicenseNative.hwid(context)

  fun licenseFile(context: Context): File = File(context.filesDir, LICENSE_FILE)

  /** Persists a license blob issued by the activation backend. */
  fun write(context: Context, blob: ByteArray): Boolean = runCatching {
    val target = licenseFile(context)
    val tmp = File(context.filesDir, "$LICENSE_FILE.tmp")
    tmp.writeBytes(blob)
    // Rename rather than write in place: a half-written license.bin would
    // otherwise fail signature verification and lock the user out.
    if (target.exists()) target.delete()
    tmp.renameTo(target)
  }.getOrDefault(false)

  fun clear(context: Context) {
    runCatching { licenseFile(context).delete() }
    runCatching { File(context.filesDir, "$LICENSE_FILE.tmp").delete() }
  }
}

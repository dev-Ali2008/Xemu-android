package Ali.Xanite

import java.io.File
import java.io.IOException

internal object XboxInsigniaHelper {
  const val PRIMARY_DNS = "46.101.64.175"
  const val SECONDARY_DNS = "8.8.8.8"

  private const val FLAG_XBOXDASH_XBE = 1 shl 0
  private const val FLAG_XODASH_DIR = 1 shl 1
  private const val FLAG_XBOX_XTF = 1 shl 2
  private const val FLAG_MSDASH_XBE = 1 shl 3
  private const val FLAG_AUDIO_DIR = 1 shl 4
  private const val FLAG_FONTS_DIR = 1 shl 5
  private const val FLAG_XBOXDASHDATA_DIR = 1 shl 6

  data class DashboardStatus(
    val flags: Int,
  ) {
    val hasBootXbe: Boolean
      get() = (flags and FLAG_XBOXDASH_XBE) != 0

    val hasXodashAssets: Boolean
      get() = (flags and FLAG_XODASH_DIR) != 0

    val hasXboxFont: Boolean
      get() = (flags and FLAG_XBOX_XTF) != 0

    val hasMsdashXbe: Boolean
      get() = (flags and FLAG_MSDASH_XBE) != 0

    val hasAudioDir: Boolean
      get() = (flags and FLAG_AUDIO_DIR) != 0

    val hasFontsDir: Boolean
      get() = (flags and FLAG_FONTS_DIR) != 0

    val hasXboxdashdataDir: Boolean
      get() = (flags and FLAG_XBOXDASHDATA_DIR) != 0

    val looksRetailDashboardInstalled: Boolean
      get() = hasBootXbe && (
        hasXodashAssets ||
          hasXboxFont ||
          hasMsdashXbe ||
          hasAudioDir ||
          hasFontsDir ||
          hasXboxdashdataDir
      )

    val hasAnyRetailDashboardFiles: Boolean
      get() = flags != 0
  }

  private val primaryDnsBytes = parseIpv4(PRIMARY_DNS)
  private val secondaryDnsBytes = parseIpv4(SECONDARY_DNS)

  @Throws(IOException::class, IllegalArgumentException::class)
  fun inspectDashboard(hddFile: File): DashboardStatus {
    require(hddFile.isFile) { "No local HDD image is configured." }
    check(NativeBridge.available) { "Native dashboard tools are not available on this device" }
    return DashboardStatus(NativeBridge.nativeInspectDashboardFlags(hddFile.absolutePath))
  }

  @Throws(IOException::class, IllegalArgumentException::class)
  fun applyConfigSectorDns(hddFile: File) {
    require(hddFile.isFile) { "No local HDD image is configured." }
    check(NativeBridge.available) { "Native dashboard tools are not available on this device" }
    NativeBridge.nativeApplyConfigSectorDns(
      hddFile.absolutePath,
      primaryDnsBytes,
      secondaryDnsBytes,
    )
  }

  fun primaryDnsBytes(): ByteArray = primaryDnsBytes.copyOf()

  private fun parseIpv4(value: String): ByteArray {
    val octets = value.split('.')
    require(octets.size == 4) { "Invalid IPv4 address: $value" }
    return ByteArray(4) { index ->
      val octet = octets[index].toIntOrNull()
        ?: throw IllegalArgumentException("Invalid IPv4 address: $value")
      require(octet in 0..255) { "Invalid IPv4 address: $value" }
      octet.toByte()
    }
  }

  private object NativeBridge {
    val available: Boolean = try {
      System.loadLibrary("SDL2")
      System.loadLibrary("xemu")
      true
    } catch (_: UnsatisfiedLinkError) {
      false
    }

    external fun nativeInspectDashboardFlags(hddPath: String): Int

    external fun nativeApplyConfigSectorDns(
      hddPath: String,
      primaryDns: ByteArray,
      secondaryDns: ByteArray,
    )
  }
}

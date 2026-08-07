package Ali.Xanite

import java.io.File
import java.io.IOException

internal object XboxEepromEditor {
  private const val EEPROM_SIZE = 256

  private const val FACTORY_CHECKSUM_OFFSET = 0x30
  private const val FACTORY_CHECKSUM_START  = 0x34
  private const val FACTORY_CHECKSUM_LENGTH = 0x2C

  private const val USER_CHECKSUM_OFFSET = 0x60
  private const val USER_CHECKSUM_START  = 0x64
  private const val USER_CHECKSUM_LENGTH = 0x5C

  private const val XBOX_LIVE_DNS_OFFSET = 0xAC

  private const val VIDEO_STANDARD_OFFSET = 0x58
  private const val LANGUAGE_OFFSET       = 0x90
  private const val VIDEO_SETTINGS_OFFSET = 0x94

  private const val VIDEO_SETTINGS_WIDESCREEN = 0x00010000u
  private const val VIDEO_SETTINGS_720P       = 0x00020000u
  private const val VIDEO_SETTINGS_1080I      = 0x00040000u
  private const val VIDEO_SETTINGS_480P       = 0x00080000u
  private const val VIDEO_SETTINGS_LETTERBOX  = 0x00100000u
  private const val VIDEO_SETTINGS_60HZ       = 0x00400000u
  private const val VIDEO_SETTINGS_50HZ       = 0x00800000u

  private val MANAGED_VIDEO_SETTINGS_MASK =
    VIDEO_SETTINGS_WIDESCREEN or
      VIDEO_SETTINGS_720P or
      VIDEO_SETTINGS_1080I or
      VIDEO_SETTINGS_480P or
      VIDEO_SETTINGS_LETTERBOX or
      VIDEO_SETTINGS_60HZ or
      VIDEO_SETTINGS_50HZ

  enum class Language(val id: UInt) {
    ENGLISH(0x00000001u),
    JAPANESE(0x00000002u),
    GERMAN(0x00000003u),
    FRENCH(0x00000004u),
    SPANISH(0x00000005u),
    ITALIAN(0x00000006u),
    KOREAN(0x00000007u),
    CHINESE(0x00000008u),
    PORTUGUESE(0x00000009u);

    companion object {
      fun fromId(id: UInt): Language? = entries.firstOrNull { it.id == id }
    }
  }

  enum class VideoStandard(val id: UInt) {
    NTSC_M(0x00400100u),
    NTSC_J(0x00400200u),
    PAL_I(0x00800300u),
    PAL_M(0x00400400u);

    companion object {
      fun fromId(id: UInt): VideoStandard? = entries.firstOrNull { it.id == id }
    }
  }

  enum class AspectRatio(val bits: UInt) {
    NORMAL(0u),
    WIDESCREEN(VIDEO_SETTINGS_WIDESCREEN),
    LETTERBOX(VIDEO_SETTINGS_LETTERBOX);

    companion object {
      fun fromBits(bits: UInt): AspectRatio? = when (bits and
        (VIDEO_SETTINGS_WIDESCREEN or VIDEO_SETTINGS_LETTERBOX)) {
        0u -> NORMAL
        VIDEO_SETTINGS_WIDESCREEN -> WIDESCREEN
        VIDEO_SETTINGS_LETTERBOX -> LETTERBOX
        else -> null
      }
    }
  }

  enum class RefreshRate(val bits: UInt) {
    DEFAULT(0u),
    HZ_60(VIDEO_SETTINGS_60HZ),
    HZ_50(VIDEO_SETTINGS_50HZ);

    companion object {
      fun fromBits(bits: UInt): RefreshRate? = when (bits and
        (VIDEO_SETTINGS_60HZ or VIDEO_SETTINGS_50HZ)) {
        0u -> DEFAULT
        VIDEO_SETTINGS_60HZ -> HZ_60
        VIDEO_SETTINGS_50HZ -> HZ_50
        else -> null
      }
    }
  }

  data class VideoSettings(
    val allow480p: Boolean,
    val allow720p: Boolean,
    val allow1080i: Boolean,
    val aspectRatio: AspectRatio,
    val refreshRate: RefreshRate,
  ) {
    fun toManagedBits(): UInt {
      var bits = aspectRatio.bits or refreshRate.bits
      if (allow480p) {
        bits = bits or VIDEO_SETTINGS_480P
      }
      if (allow720p) {
        bits = bits or VIDEO_SETTINGS_720P
      }
      if (allow1080i) {
        bits = bits or VIDEO_SETTINGS_1080I
      }
      return bits
    }
  }

  data class Snapshot(
    val language: Language,
    val videoStandard: VideoStandard,
    val videoSettings: VideoSettings,
    val rawLanguage: UInt,
    val rawVideoStandard: UInt,
    val rawVideoSettings: UInt,
  ) {
    val hasManagedVideoSettingsMismatch: Boolean
      get() = (rawVideoSettings and MANAGED_VIDEO_SETTINGS_MASK) != videoSettings.toManagedBits()
  }

  @Throws(IOException::class, IllegalArgumentException::class)
  fun load(file: File): Snapshot {
    val data = file.readBytes()
    ensureValidSize(file, data)

    val rawLanguage = readLeUInt32(data, LANGUAGE_OFFSET)
    val rawVideoStandard = readLeUInt32(data, VIDEO_STANDARD_OFFSET)
    val rawVideoSettings = readLeUInt32(data, VIDEO_SETTINGS_OFFSET)

    return Snapshot(
      language = Language.fromId(rawLanguage) ?: Language.ENGLISH,
      videoStandard = VideoStandard.fromId(rawVideoStandard) ?: VideoStandard.NTSC_M,
      videoSettings = VideoSettings(
        allow480p = (rawVideoSettings and VIDEO_SETTINGS_480P) != 0u,
        allow720p = (rawVideoSettings and VIDEO_SETTINGS_720P) != 0u,
        allow1080i = (rawVideoSettings and VIDEO_SETTINGS_1080I) != 0u,
        aspectRatio = AspectRatio.fromBits(rawVideoSettings) ?: AspectRatio.NORMAL,
        refreshRate = RefreshRate.fromBits(rawVideoSettings) ?: RefreshRate.DEFAULT,
      ),
      rawLanguage = rawLanguage,
      rawVideoStandard = rawVideoStandard,
      rawVideoSettings = rawVideoSettings,
    )
  }

  @Throws(IOException::class, IllegalArgumentException::class)
  fun apply(
    file: File,
    language: Language,
    videoStandard: VideoStandard,
    videoSettings: VideoSettings,
  ): Boolean {
    val data = file.readBytes()
    ensureValidSize(file, data)

    val currentLanguage = readLeUInt32(data, LANGUAGE_OFFSET)
    val currentVideoStandard = readLeUInt32(data, VIDEO_STANDARD_OFFSET)
    val currentVideoSettings = readLeUInt32(data, VIDEO_SETTINGS_OFFSET)
    val nextVideoSettings =
      (currentVideoSettings and MANAGED_VIDEO_SETTINGS_MASK.inv()) or videoSettings.toManagedBits()
    if (currentLanguage == language.id &&
      currentVideoStandard == videoStandard.id &&
      currentVideoSettings == nextVideoSettings
    ) {
      return false
    }

    writeLeUInt32(data, LANGUAGE_OFFSET, language.id)
    writeLeUInt32(data, VIDEO_STANDARD_OFFSET, videoStandard.id)
    writeLeUInt32(data, VIDEO_SETTINGS_OFFSET, nextVideoSettings)

    val factoryChecksum = xboxChecksum(data, FACTORY_CHECKSUM_START, FACTORY_CHECKSUM_LENGTH)
    writeLeUInt32(data, FACTORY_CHECKSUM_OFFSET, factoryChecksum)

    val userChecksum = xboxChecksum(data, USER_CHECKSUM_START, USER_CHECKSUM_LENGTH)
    writeLeUInt32(data, USER_CHECKSUM_OFFSET, userChecksum)

    file.writeBytes(data)
    return true
  }

  /**
   * Current Xbox Live DNS as dotted-quad, or null if the EEPROM is missing or
   * malformed. Used to tell whether Insignia's DNS has already been written,
   * so setup can show a step as done instead of asking again.
   */
  fun readXboxLiveDns(file: File): String? {
    return try {
      if (!file.isFile) return null
      val data = file.readBytes()
      if (data.size != EEPROM_SIZE) return null
      (0 until 4).joinToString(".") { (data[XBOX_LIVE_DNS_OFFSET + it].toInt() and 0xFF).toString() }
    } catch (_: IOException) {
      null
    }
  }

  @Throws(IOException::class, IllegalArgumentException::class)
  fun applyXboxLiveDns(
    file: File,
    dnsServer: ByteArray,
  ): Boolean {
    require(dnsServer.size == 4) { "Xbox Live DNS must be a 4-byte IPv4 address." }

    val data = file.readBytes()
    ensureValidSize(file, data)

    var changed = false
    for (index in dnsServer.indices) {
      if (data[XBOX_LIVE_DNS_OFFSET + index] != dnsServer[index]) {
        data[XBOX_LIVE_DNS_OFFSET + index] = dnsServer[index]
        changed = true
      }
    }
    if (!changed) {
      return false
    }

    val userChecksum = xboxChecksum(data, USER_CHECKSUM_START, USER_CHECKSUM_LENGTH)
    writeLeUInt32(data, USER_CHECKSUM_OFFSET, userChecksum)

    file.writeBytes(data)
    return true
  }

  private fun ensureValidSize(file: File, data: ByteArray) {
    if (data.size != EEPROM_SIZE) {
      throw IllegalArgumentException(
        "Invalid EEPROM size ${data.size}, expected $EEPROM_SIZE (${file.absolutePath})"
      )
    }
  }

  private fun readLeUInt32(data: ByteArray, offset: Int): UInt {
    return ((data[offset].toUInt() and 0xFFu) or
      ((data[offset + 1].toUInt() and 0xFFu) shl 8) or
      ((data[offset + 2].toUInt() and 0xFFu) shl 16) or
      ((data[offset + 3].toUInt() and 0xFFu) shl 24))
  }

  private fun writeLeUInt32(data: ByteArray, offset: Int, value: UInt) {
    data[offset]     = (value and 0xFFu).toByte()
    data[offset + 1] = ((value shr 8) and 0xFFu).toByte()
    data[offset + 2] = ((value shr 16) and 0xFFu).toByte()
    data[offset + 3] = ((value shr 24) and 0xFFu).toByte()
  }

  private fun xboxChecksum(data: ByteArray, offset: Int, length: Int): UInt {
    require(length % 4 == 0) { "Checksum length must be 32-bit aligned" }

    var high = 0u
    var low  = 0u
    var pos  = offset
    val end  = offset + length

    while (pos < end) {
      val value = readLeUInt32(data, pos)
      val sum = (high.toULong() shl 32) or low.toULong()
      val next = sum + value.toULong()

      high = ((next shr 32) and 0xFFFF_FFFFuL).toUInt()
      low += value
      pos += 4
    }

    return (high + low).inv()
  }
}

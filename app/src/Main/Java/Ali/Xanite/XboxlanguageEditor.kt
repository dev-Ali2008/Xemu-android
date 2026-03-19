package Ali.Xanite

import java.io.File
import java.io.IOException
import java.nio.ByteBuffer
import java.nio.ByteOrder

object XboxlanguageEditor {

    private const val EEPROM_SIZE = 256
    private const val EEPROM_MAX_SIZE = 1024 

    private object Offsets {

        const val FACTORY_CHECKSUM = 0x30
        const val FACTORY_CHECKSUM_START = 0x34
        const val FACTORY_CHECKSUM_LENGTH = 0x2C

        const val USER_CHECKSUM = 0x60
        const val USER_CHECKSUM_START = 0x64
        const val USER_CHECKSUM_LENGTH = 0x5C


        const val VIDEO_STANDARD = 0x58
        const val LANGUAGE = 0x90


        const val REFRESH_RATE = 0x94    
        const val ASPECT_RATIO = 0x98    
        const val RESOLUTION = 0x9C      
    }

    enum class Language(val id: UInt, val displayName: String, val code: String) {
        ENGLISH(0x00000001u, "English", "en"),
        JAPANESE(0x00000002u, "Japanese", "ja"),
        GERMAN(0x00000003u, "German", "de"),
        FRENCH(0x00000004u, "French", "fr"),
        SPANISH(0x00000005u, "Spanish", "es"),
        ITALIAN(0x00000006u, "Italian", "it"),
        KOREAN(0x00000007u, "Korean", "ko"),
        CHINESE(0x00000008u, "Chinese", "zh"),
        PORTUGUESE(0x00000009u, "Portuguese", "pt");

        companion object {
            private val mapById = entries.associateBy { it.id }
            private val mapByCode = entries.associateBy { it.code }

            fun fromId(id: UInt): Language? = mapById[id]
            fun fromCode(code: String): Language? = mapByCode[code]

            fun fromIdOrDefault(id: UInt, default: Language = ENGLISH): Language = 
                mapById[id] ?: default
        }
    }

    enum class VideoStandard(val id: UInt, val displayName: String, val isNtsc: Boolean) {
        NTSC_M(0x00400100u, "NTSC-M", true),
        NTSC_J(0x00400200u, "NTSC-J", true),
        PAL_I(0x00800300u, "PAL-I", false),
        PAL_M(0x00400400u, "PAL-M", false); 

        companion object {
            private val mapById = entries.associateBy { it.id }

            fun fromId(id: UInt): VideoStandard? = mapById[id]

            fun fromIdOrDefault(id: UInt, default: VideoStandard = NTSC_M): VideoStandard = 
                mapById[id] ?: default
        }
    }
    
    enum class RefreshRate(val value: Int, val displayName: String) {
        AUTO(0, "Auto"),
        HZ_50(50, "50Hz"),
        HZ_60(60, "60Hz");

        companion object {
            private val mapByValue = entries.associateBy { it.value }

            fun fromValue(value: Int): RefreshRate = mapByValue[value] ?: AUTO

            fun toEepromValue(rate: RefreshRate): UInt {
                return when (rate) {
                    AUTO -> 0x00000000u
                    HZ_50 -> 0x00000050u
                    HZ_60 -> 0x00000060u
                }
            }

            fun fromEepromValue(value: UInt): RefreshRate {
                return when (value) {
                    0x00000050u -> HZ_50
                    0x00000060u -> HZ_60
                    else -> AUTO
                }
            }
        }
    }

    enum class AspectRatio(val value: Int, val displayName: String) {
        AUTO(0, "Auto"),
        RATIO_4_3(1, "4:3"),
        RATIO_16_9(2, "16:9");

        companion object {
            private val mapByValue = entries.associateBy { it.value }

            fun fromValue(value: Int): AspectRatio = mapByValue[value] ?: AUTO

            fun toEepromValue(ratio: AspectRatio): UInt {
                return when (ratio) {
                    AUTO -> 0x00000000u
                    RATIO_4_3 -> 0x00000001u
                    RATIO_16_9 -> 0x00000002u
                }
            }

            fun fromEepromValue(value: UInt): AspectRatio {
                return when (value) {
                    0x00000001u -> RATIO_4_3
                    0x00000002u -> RATIO_16_9
                    else -> AUTO
                }
            }
        }
    }

    enum class Resolution(val value: Int, val displayName: String, val width: Int, val height: Int) {
        AUTO(0, "Auto", 0, 0),
        RES_480I(1, "480i", 720, 480),
        RES_480P(2, "480p", 720, 480),
        RES_720P(3, "720p", 1280, 720),
        RES_1080I(4, "1080i", 1920, 1080);

        val isProgressive: Boolean
            get() = this == RES_480P || this == RES_720P

        companion object {
            private val mapByValue = entries.associateBy { it.value }

            fun fromValue(value: Int): Resolution = mapByValue[value] ?: AUTO

            fun toEepromValue(res: Resolution): UInt {
                return when (res) {
                    AUTO -> 0x00000000u
                    RES_480I -> 0x00000001u
                    RES_480P -> 0x00000002u
                    RES_720P -> 0x00000003u
                    RES_1080I -> 0x00000004u
                }
            }

            fun fromEepromValue(value: UInt): Resolution {
                return when (value) {
                    0x00000001u -> RES_480I
                    0x00000002u -> RES_480P
                    0x00000003u -> RES_720P
                    0x00000004u -> RES_1080I
                    else -> AUTO
                }
            }
        }
    }

    data class Snapshot(
        val language: Language,
        val videoStandard: VideoStandard,
        val refreshRate: RefreshRate,
        val aspectRatio: AspectRatio,
        val resolution: Resolution,
        val rawLanguage: UInt,
        val rawVideoStandard: UInt,
        val rawRefreshRate: UInt,
        val rawAspectRatio: UInt,
        val rawResolution: UInt,
        val isValid: Boolean = true,
        val errorMessage: String? = null,
        val factoryChecksumValid: Boolean = true,
        val userChecksumValid: Boolean = true
    ) {
        val hasUnknownValues: Boolean
            get() = rawLanguage != language.id || 
                    rawVideoStandard != videoStandard.id ||
                    rawRefreshRate != RefreshRate.toEepromValue(refreshRate) ||
                    rawAspectRatio != AspectRatio.toEepromValue(aspectRatio) ||
                    rawResolution != Resolution.toEepromValue(resolution)

        val allChecksumsValid: Boolean
            get() = factoryChecksumValid && userChecksumValid

        companion object {
            fun invalid(error: String) = Snapshot(
                language = Language.ENGLISH,
                videoStandard = VideoStandard.NTSC_M,
                refreshRate = RefreshRate.AUTO,
                aspectRatio = AspectRatio.AUTO,
                resolution = Resolution.AUTO,
                rawLanguage = 0u,
                rawVideoStandard = 0u,
                rawRefreshRate = 0u,
                rawAspectRatio = 0u,
                rawResolution = 0u,
                isValid = false,
                errorMessage = error,
                factoryChecksumValid = false,
                userChecksumValid = false
            )
        }
    }

class EepromException(message: String, val file: File? = null, cause: Throwable? = null) : 
    Exception(message, cause)

    @Throws(IOException::class, EepromException::class)
    fun load(file: File): Snapshot {
        return try {
            val data = file.readBytes()
            validateEepromSize(file, data)


            val buffer = ByteBuffer.wrap(data).order(ByteOrder.LITTLE_ENDIAN)


            val rawLanguage = buffer.getInt(Offsets.LANGUAGE).toUInt()
            val rawVideoStandard = buffer.getInt(Offsets.VIDEO_STANDARD).toUInt()
            val rawRefreshRate = buffer.getInt(Offsets.REFRESH_RATE).toUInt()
            val rawAspectRatio = buffer.getInt(Offsets.ASPECT_RATIO).toUInt()
            val rawResolution = buffer.getInt(Offsets.RESOLUTION).toUInt()


            val factoryChecksum = readUInt32(data, Offsets.FACTORY_CHECKSUM)
            val userChecksum = readUInt32(data, Offsets.USER_CHECKSUM)

            val calculatedFactory = calculateChecksum(data, Offsets.FACTORY_CHECKSUM_START, Offsets.FACTORY_CHECKSUM_LENGTH)
            val calculatedUser = calculateChecksum(data, Offsets.USER_CHECKSUM_START, Offsets.USER_CHECKSUM_LENGTH)

            validateValues(rawLanguage, rawVideoStandard, rawRefreshRate, rawAspectRatio, rawResolution)

            Snapshot(
                language = Language.fromIdOrDefault(rawLanguage),
                videoStandard = VideoStandard.fromIdOrDefault(rawVideoStandard),
                refreshRate = RefreshRate.fromEepromValue(rawRefreshRate),
                aspectRatio = AspectRatio.fromEepromValue(rawAspectRatio),
                resolution = Resolution.fromEepromValue(rawResolution),
                rawLanguage = rawLanguage,
                rawVideoStandard = rawVideoStandard,
                rawRefreshRate = rawRefreshRate,
                rawAspectRatio = rawAspectRatio,
                rawResolution = rawResolution,
                factoryChecksumValid = factoryChecksum == calculatedFactory,
                userChecksumValid = userChecksum == calculatedUser
            )
        } catch (e: IOException) {
            throw IOException("Failed to read EEPROM file: ${file.name}", e)
        } catch (e: Exception) {
            throw EepromException("Invalid EEPROM data: ${e.message}", file, e)
        }
    }

    @Throws(IOException::class, EepromException::class)
    fun loadWithChecksum(file: File): Snapshot {
        val snapshot = load(file)
        if (!snapshot.isValid) return snapshot

        return snapshot
    }

    @Throws(IOException::class, EepromException::class)
    fun apply(
        file: File, 
        language: Language, 
        videoStandard: VideoStandard,
        refreshRate: RefreshRate = RefreshRate.AUTO,
        aspectRatio: AspectRatio = AspectRatio.AUTO,
        resolution: Resolution = Resolution.AUTO
    ): Boolean {
        val data = file.readBytes()
        validateEepromSize(file, data)


        val buffer = ByteBuffer.wrap(data).order(ByteOrder.LITTLE_ENDIAN)
        val currentLanguage = buffer.getInt(Offsets.LANGUAGE).toUInt()
        val currentVideo = buffer.getInt(Offsets.VIDEO_STANDARD).toUInt()
        val currentRefresh = buffer.getInt(Offsets.REFRESH_RATE).toUInt()
        val currentAspect = buffer.getInt(Offsets.ASPECT_RATIO).toUInt()
        val currentResolution = buffer.getInt(Offsets.RESOLUTION).toUInt()


        if (currentLanguage == language.id && 
            currentVideo == videoStandard.id &&
            currentRefresh == RefreshRate.toEepromValue(refreshRate) &&
            currentAspect == AspectRatio.toEepromValue(aspectRatio) &&
            currentResolution == Resolution.toEepromValue(resolution)) {
            return false
        }

        writeUInt32(data, Offsets.LANGUAGE, language.id)
        writeUInt32(data, Offsets.VIDEO_STANDARD, videoStandard.id)
        writeUInt32(data, Offsets.REFRESH_RATE, RefreshRate.toEepromValue(refreshRate))
        writeUInt32(data, Offsets.ASPECT_RATIO, AspectRatio.toEepromValue(aspectRatio))
        writeUInt32(data, Offsets.RESOLUTION, Resolution.toEepromValue(resolution))

        updateChecksums(data)

        createBackup(file)
        file.writeBytes(data)

        return true
    }

    @Throws(IOException::class, EepromException::class)
    fun applyAndVerify(
        file: File,
        language: Language,
        videoStandard: VideoStandard,
        refreshRate: RefreshRate = RefreshRate.AUTO,
        aspectRatio: AspectRatio = AspectRatio.AUTO,
        resolution: Resolution = Resolution.AUTO
    ): Boolean {
        val changed = apply(file, language, videoStandard, refreshRate, aspectRatio, resolution)

        if (changed) {

            val snapshot = load(file)
            if (snapshot.language != language || 
                snapshot.videoStandard != videoStandard ||
                snapshot.refreshRate != refreshRate ||
                snapshot.aspectRatio != aspectRatio ||
                snapshot.resolution != resolution) {
                throw EepromException("Verification failed after write", file)
            }
        }
        return changed
    }

    private fun validateEepromSize(file: File, data: ByteArray) {
        when {
            data.size < EEPROM_SIZE -> 
                throw EepromException("EEPROM file too small: ${data.size} bytes (expected $EEPROM_SIZE)", file)
            data.size > EEPROM_MAX_SIZE -> 
                throw EepromException("EEPROM file too large: ${data.size} bytes (max $EEPROM_MAX_SIZE)", file)
            data.size != EEPROM_SIZE -> 
                System.err.println("Warning: EEPROM size ${data.size} differs from standard $EEPROM_SIZE")
        }
    }

    private fun validateValues(
        language: UInt, 
        video: UInt,
        refreshRate: UInt,
        aspectRatio: UInt,
        resolution: UInt
    ) {
        if (language == 0u) {
            System.err.println("Warning: Language value is 0")
        }
        if (video == 0u) {
            System.err.println("Warning: Video standard value is 0")
        }
        if (refreshRate == 0u) {
            System.err.println("Warning: Refresh rate value is 0")
        }
        if (aspectRatio == 0u) {
            System.err.println("Warning: Aspect ratio value is 0")
        }
        if (resolution == 0u) {
            System.err.println("Warning: Resolution value is 0")
        }
    }

    private fun createBackup(file: File) {
        try {
            val backupDir = File(file.parentFile, "backups")
            if (!backupDir.exists()) {
                backupDir.mkdirs()
            }

            val timestamp = System.currentTimeMillis()
            val backup = File(backupDir, "${file.nameWithoutExtension}_$timestamp.bak")

            if (!backup.exists()) {
                file.copyTo(backup, overwrite = false)
            }
        } catch (e: Exception) {
            System.err.println("Warning: Failed to create backup: ${e.message}")
        }
    }

    private fun updateChecksums(data: ByteArray) {
        val factoryChecksum = calculateChecksum(data, Offsets.FACTORY_CHECKSUM_START, Offsets.FACTORY_CHECKSUM_LENGTH)
        writeUInt32(data, Offsets.FACTORY_CHECKSUM, factoryChecksum)

        val userChecksum = calculateChecksum(data, Offsets.USER_CHECKSUM_START, Offsets.USER_CHECKSUM_LENGTH)
        writeUInt32(data, Offsets.USER_CHECKSUM, userChecksum)
    }

    private fun readUInt32(data: ByteArray, offset: Int): UInt {
        return ((data[offset].toUInt() and 0xFFu) or
                ((data[offset + 1].toUInt() and 0xFFu) shl 8) or
                ((data[offset + 2].toUInt() and 0xFFu) shl 16) or
                ((data[offset + 3].toUInt() and 0xFFu) shl 24))
    }

    private fun writeUInt32(data: ByteArray, offset: Int, value: UInt) {
        data[offset] = (value and 0xFFu).toByte()
        data[offset + 1] = ((value shr 8) and 0xFFu).toByte()
        data[offset + 2] = ((value shr 16) and 0xFFu).toByte()
        data[offset + 3] = ((value shr 24) and 0xFFu).toByte()
    }

    private fun calculateChecksum(data: ByteArray, offset: Int, length: Int): UInt {
        require(length % 4 == 0) { "Checksum length must be 32-bit aligned" }

        var sum = 0uL
        var pos = offset
        val end = offset + length

        while (pos < end) {
            val value = readUInt32(data, pos).toULong()
            sum += value
            pos += 4
        }

        return (sum and 0xFFFF_FFFFuL).toUInt().inv()
    }

    fun getSupportedLanguages(): List<Language> = Language.entries

    fun getSupportedVideoStandards(): List<VideoStandard> = VideoStandard.entries

    fun getSupportedRefreshRates(): List<RefreshRate> = RefreshRate.entries
    
    fun getSupportedAspectRatios(): List<AspectRatio> = AspectRatio.entries

    fun getSupportedResolutions(): List<Resolution> = Resolution.entries

    fun isValidEeprom(file: File): Boolean {
        return try {
            val data = file.readBytes()
            data.size == EEPROM_SIZE || data.size == 1024 
        } catch (e: Exception) {
            false
        }
    }

    @Throws(IOException::class, EepromException::class)
    fun fixChecksums(file: File): Boolean {
        val data = file.readBytes()
        validateEepromSize(file, data)

        val currentFactory = readUInt32(data, Offsets.FACTORY_CHECKSUM)
        val currentUser = readUInt32(data, Offsets.USER_CHECKSUM)

        val calculatedFactory = calculateChecksum(data, Offsets.FACTORY_CHECKSUM_START, Offsets.FACTORY_CHECKSUM_LENGTH)
        val calculatedUser = calculateChecksum(data, Offsets.USER_CHECKSUM_START, Offsets.USER_CHECKSUM_LENGTH)

        if (currentFactory == calculatedFactory && currentUser == calculatedUser) {
            return false 
        }

        writeUInt32(data, Offsets.FACTORY_CHECKSUM, calculatedFactory)
        writeUInt32(data, Offsets.USER_CHECKSUM, calculatedUser)

        createBackup(file)
        file.writeBytes(data)
        return true
    }

    fun exportSettings(file: File): String {
        return try {
            val snapshot = load(file)
            buildString {
                appendLine("=== Xbox EEPROM Settings ===")
                appendLine("File: ${file.absolutePath}")
                appendLine("")
                appendLine("Language: ${snapshot.language.displayName} (0x${snapshot.rawLanguage.toString(16).padStart(8, '0')})")
                appendLine("Video Standard: ${snapshot.videoStandard.displayName} (0x${snapshot.rawVideoStandard.toString(16).padStart(8, '0')})")
                appendLine("Refresh Rate: ${snapshot.refreshRate.displayName}")
                appendLine("Aspect Ratio: ${snapshot.aspectRatio.displayName}")
                appendLine("Resolution: ${snapshot.resolution.displayName}")
                appendLine("")
                appendLine("Checksums:")
                appendLine("  Factory: ${if (snapshot.factoryChecksumValid) "✓ Valid" else "✗ Invalid"}")
                appendLine("  User: ${if (snapshot.userChecksumValid) "✓ Valid" else "✗ Invalid"}")
                appendLine("")
                appendLine("Status: ${if (snapshot.isValid) "Valid" else "Invalid: ${snapshot.errorMessage}"}")
            }
        } catch (e: Exception) {
            "Error reading EEPROM: ${e.message}"
        }
    }

    fun createDefaultEeprom(file: File): Boolean {
        return try {
            val data = ByteArray(EEPROM_SIZE)
            
            writeUInt32(data, Offsets.LANGUAGE, Language.ENGLISH.id)
            writeUInt32(data, Offsets.VIDEO_STANDARD, VideoStandard.NTSC_M.id)
            writeUInt32(data, Offsets.REFRESH_RATE, RefreshRate.toEepromValue(RefreshRate.AUTO))
            writeUInt32(data, Offsets.ASPECT_RATIO, AspectRatio.toEepromValue(AspectRatio.AUTO))
            writeUInt32(data, Offsets.RESOLUTION, Resolution.toEepromValue(Resolution.AUTO))

            val factoryChecksum = calculateChecksum(data, Offsets.FACTORY_CHECKSUM_START, Offsets.FACTORY_CHECKSUM_LENGTH)
            val userChecksum = calculateChecksum(data, Offsets.USER_CHECKSUM_START, Offsets.USER_CHECKSUM_LENGTH)

            writeUInt32(data, Offsets.FACTORY_CHECKSUM, factoryChecksum)
            writeUInt32(data, Offsets.USER_CHECKSUM, userChecksum)

            file.writeBytes(data)
            true
        } catch (e: Exception) {
            false
        }
    }
}

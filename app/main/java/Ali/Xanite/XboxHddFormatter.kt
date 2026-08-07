package Ali.Xanite

import java.io.File
import java.io.IOException

internal object XboxHddFormatter {
  const val MINIMUM_RETAIL_DISK_BYTES = 0x1DD156000L

  private const val SECTOR_SIZE_BYTES = 512L
  private const val FATX_SUPERBLOCK_SIZE = 4096
  private const val FATX_FAT_OFFSET = FATX_SUPERBLOCK_SIZE.toLong()
  private const val FATX_RESERVED_ENTRY_COUNT = 1L
  private const val FATX_NON_RETAIL_SECTORS_PER_CLUSTER = 128

  private const val EXTENDED_PARTITION_OFFSET = 0x1DD156000L
  private const val G_PARTITION_OFFSET = 0x0FFFFFFFL * SECTOR_SIZE_BYTES

  enum class Layout {
    RETAIL,
    RETAIL_PLUS_F,
    RETAIL_PLUS_F_G,
  }

  enum class LayoutAvailability {
    AVAILABLE,
    NO_EXTENDED_SPACE,
    NEEDS_STANDARD_G_BOUNDARY,
    NOT_ENOUGH_SPACE,
  }

  enum class ImageFormat {
    RAW,
    QCOW2,
  }

  data class Inspection(
    val format: ImageFormat,
    val totalBytes: Long,
  ) {
    val supportsRetailFormat: Boolean
      get() = totalBytes >= MINIMUM_RETAIL_DISK_BYTES

    val extendedBytes: Long
      get() = alignedExtendedBytes(totalBytes)
  }

  @Throws(IOException::class)
  fun inspect(file: File): Inspection {
    require(file.isFile) { "No local HDD image is configured." }
    check(NativeBridge.available) { "Native HDD tools are not available on this device" }

    val details = NativeBridge.nativeInspectHdd(file.absolutePath)
    require(details.size >= 2) { "Failed to inspect the current HDD image." }

    val format = ImageFormat.values().getOrNull(details[0].toInt())
      ?: throw IOException("Unsupported HDD image format.")
    return Inspection(
      format = format,
      totalBytes = details[1],
    )
  }

  fun supportedLayouts(inspection: Inspection): List<Layout> {
    if (!inspection.supportsRetailFormat) {
      return emptyList()
    }

    return Layout.entries.filter { availabilityFor(inspection, it) == LayoutAvailability.AVAILABLE }
  }

  @Throws(IOException::class, IllegalArgumentException::class)
  fun initialize(file: File, layout: Layout) {
    require(file.isFile) { "No local HDD image is configured." }
    check(NativeBridge.available) { "Native HDD tools are not available on this device" }
    NativeBridge.nativeInitializeHdd(file.absolutePath, layout.ordinal)
  }

  @Throws(IOException::class, IllegalArgumentException::class)
  fun initializeRetail(file: File) {
    initialize(file, Layout.RETAIL)
  }

  fun availabilityFor(inspection: Inspection, layout: Layout): LayoutAvailability {
    if (!inspection.supportsRetailFormat) {
      return LayoutAvailability.NOT_ENOUGH_SPACE
    }

    return when (layout) {
      Layout.RETAIL -> LayoutAvailability.AVAILABLE
      Layout.RETAIL_PLUS_F -> {
        if (inspection.extendedBytes <= 0L) {
          LayoutAvailability.NO_EXTENDED_SPACE
        } else if (canFormatExtendedPartition(inspection.extendedBytes)) {
          LayoutAvailability.AVAILABLE
        } else {
          LayoutAvailability.NOT_ENOUGH_SPACE
        }
      }
      Layout.RETAIL_PLUS_F_G -> {
        val split = standardFgPartitionSizes(inspection.totalBytes)
          ?: return LayoutAvailability.NEEDS_STANDARD_G_BOUNDARY
        if (canFormatExtendedPartition(split.first) &&
          canFormatExtendedPartition(split.second)
        ) {
          LayoutAvailability.AVAILABLE
        } else {
          LayoutAvailability.NOT_ENOUGH_SPACE
        }
      }
    }
  }

  private fun canBuildPartition(size: Long, sectorsPerCluster: Int): Boolean {
    val bytesPerCluster = sectorsPerCluster * SECTOR_SIZE_BYTES
    if (bytesPerCluster <= 0L || size <= 0L) {
      return false
    }

    var fatEntries = size / bytesPerCluster
    fatEntries += FATX_RESERVED_ENTRY_COUNT

    var fatLengthBytes = fatEntries * if (fatEntries < 0xFFF0) 2L else 4L
    if (fatLengthBytes % FATX_SUPERBLOCK_SIZE != 0L) {
      fatLengthBytes += FATX_SUPERBLOCK_SIZE - (fatLengthBytes % FATX_SUPERBLOCK_SIZE)
    }
    return FATX_FAT_OFFSET + fatLengthBytes + bytesPerCluster <= size
  }

  private fun canFormatExtendedPartition(size: Long): Boolean {
    return canBuildPartition(size, FATX_NON_RETAIL_SECTORS_PER_CLUSTER)
  }

  private fun standardFgPartitionSizes(totalBytes: Long): Pair<Long, Long>? {
    val alignedTotalBytes = (totalBytes / SECTOR_SIZE_BYTES) * SECTOR_SIZE_BYTES
    if (alignedTotalBytes <= G_PARTITION_OFFSET || G_PARTITION_OFFSET <= EXTENDED_PARTITION_OFFSET) {
      return null
    }

    val fSize = G_PARTITION_OFFSET - EXTENDED_PARTITION_OFFSET
    val gSize = alignedTotalBytes - G_PARTITION_OFFSET
    if (gSize <= 0L) {
      return null
    }

    return Pair(fSize, gSize)
  }

  private fun alignedExtendedBytes(totalBytes: Long): Long {
    if (totalBytes <= EXTENDED_PARTITION_OFFSET) {
      return 0L
    }
    return ((totalBytes - EXTENDED_PARTITION_OFFSET) / SECTOR_SIZE_BYTES) * SECTOR_SIZE_BYTES
  }

  private object NativeBridge {
    val available: Boolean = try {
      System.loadLibrary("SDL2")
      System.loadLibrary("xemu")
      true
    } catch (_: UnsatisfiedLinkError) {
      false
    }

    external fun nativeInspectHdd(path: String): LongArray
    external fun nativeInitializeHdd(path: String, layoutOrdinal: Int)
  }
}

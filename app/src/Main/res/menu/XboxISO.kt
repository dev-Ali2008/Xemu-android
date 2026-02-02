package og.xaniteog

import android.util.Log
import java.io.File
import java.io.RandomAccessFile
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.nio.channels.FileChannel
import kotlin.experimental.and
import kotlin.experimental.or

class XboxISO(
    private val memory: XboxMemory,
    private val mmio: XboxMMIO
) {

    companion object {
        private const val TAG = "XboxISO"
        
        // ISO Constants
        const val SECTOR_SIZE = 2048
        const val XGD_SECTOR_SIZE = 2048
        const val XGD_PFI_OFFSET = 0xFD90000L
        const val XGD_DMI_OFFSET = 0xFD91000L
        const val XGD_SS_OFFSET = 0xFD92000L
        const val XGD_VIDEO_OFFSET = 0xFD93000L
        const val XGD_GAME_OFFSET = 0xFD94000L
        
        // Xbox Game Disc (XGD) Layout
        const val XGD_LAYER_BREAK = 0x0FD90000L
        const val XGD_TOTAL_SECTORS = 0x7853B0L
        
        // File System Types
        const val FS_XISO = 0
        const val FS_REDUMP = 1
        const val FS_XGD = 2
        
        // Region Codes
        const val REGION_NA = 0x01000000
        const val REGION_JAPAN = 0x02000000
        const val REGION_EUROPE = 0x04000000
        const val REGION_ALL = 0x07000000
        
        // Media Types
        const val MEDIA_DVD_5 = 0
        const val MEDIA_DVD_9 = 1
        const val MEDIA_CD = 2
        
        // File Attributes
        const val ATTR_READONLY = 0x01
        const val ATTR_HIDDEN = 0x02
        const val ATTR_SYSTEM = 0x04
        const val ATTR_DIRECTORY = 0x10
        const val ATTR_ARCHIVE = 0x20
        const val ATTR_EXECUTABLE = 0x40
        const val ATTR_NORMAL = 0x80
    }

    // ===== ISO Structures =====
    
    data class VolumeDescriptor(
        val type: Int,
        val identifier: String,
        val version: Int,
        val systemIdentifier: String,
        val volumeIdentifier: String,
        val volumeSpaceSize: Long,
        val volumeSetSize: Int,
        val volumeSequenceNumber: Int,
        val logicalBlockSize: Int,
        val pathTableSize: Long,
        val locationOfPathTable: Long,
        val locationOfOptionalPathTable: Long,
        val rootDirectoryEntry: DirectoryEntry,
        val volumeSetIdentifier: String,
        val publisherIdentifier: String,
        val dataPreparerIdentifier: String,
        val applicationIdentifier: String,
        val copyrightFileIdentifier: String,
        val abstractFileIdentifier: String,
        val bibliographicFileIdentifier: String,
        val volumeCreationDate: String,
        val volumeModificationDate: String,
        val volumeExpirationDate: String,
        val volumeEffectiveDate: String
    )
    
    data class DirectoryEntry(
        val length: Int,
        val extendedAttributeRecordLength: Int,
        val locationOfExtent: Long,
        val dataLength: Long,
        val recordingDateAndTime: String,
        val fileFlags: Int,
        val fileUnitSize: Int,
        val interleaveGapSize: Int,
        val volumeSequenceNumber: Int,
        val fileIdentifier: String,
        val systemUse: ByteArray,
        val isDirectory: Boolean,
        val isHidden: Boolean,
        val isSystem: Boolean,
        val children: List<DirectoryEntry> = emptyList()
    )
    
    data class XGDInfo(
        val mediaType: Int,
        val layerBreak: Long,
        val totalSectors: Long,
        val securitySector: SecuritySector?,
        val pfi: PFI?,
        val dmi: DMI?,
        val videoPartition: VideoPartition?
    )
    
    data class SecuritySector(
        val magic: Int,
        val rootHash: ByteArray,
        val headerHash: ByteArray,
        val imageHash: ByteArray,
        val regionCode: Int,
        val numRootTableEntries: Int,
        val rootTableOffset: Int,
        val numCertEntries: Int,
        val certTableOffset: Int
    )
    
    data class PFI(
        val magic: Int,
        val dataStructureLength: Int,
        val reserved1: Int,
        val version: Int,
        val discStructure: Int,
        val sessionNumber: Int,
        val sessionStartAddress: Int,
        val layer0StartAddress: Int,
        val layer0EndAddress: Int,
        val layer1StartAddress: Int,
        val layer1EndAddress: Int
    )
    
    data class DMI(
        val magic: Int,
        val dataStructureLength: Int,
        val reserved1: Int,
        val version: Int,
        val discStructure: Int,
        val sessionNumber: Int,
        val sessionStartAddress: Int,
        val bcaFlag: Int,
        val bcaData: ByteArray
    )
    
    data class VideoPartition(
        val startSector: Long,
        val size: Long,
        val files: List<DirectoryEntry>
    )
    
    data class GamePartition(
        val startSector: Long,
        val size: Long,
        val xbeLocation: Long,
        val defaultXbe: XbeLoader.XbeImage?,
        val files: List<DirectoryEntry>
    )
    
    data class XboxDisc(
        val file: File,
        val fileSystemType: Int,
        val volumeDescriptor: VolumeDescriptor,
        val xgdInfo: XGDInfo?,
        val gamePartition: GamePartition?,
        val videoPartition: VideoPartition?,
        val allFiles: List<DirectoryEntry>,
        val totalSize: Long,
        val isXGD: Boolean,
        val region: Int,
        val title: String,
        val titleId: String,
        val mediaId: String
    )

    // ===== ISO State =====
    private var loadedDisc: XboxDisc? = null
    private var isMounted = false
    private var fileChannel: FileChannel? = null
    
    // ===== Cache =====
    private val sectorCache = mutableMapOf<Long, ByteArray>()
    private val fileCache = mutableMapOf<String, ByteArray>()
    private val CACHE_SIZE = 100

    /* ===============================
       Main Mount Function
       =============================== */

    fun mount(isoFile: File): XboxDisc {
        Log.d(TAG, "Mounting ISO: ${isoFile.name}")
        
        if (!isoFile.exists()) {
            throw IllegalStateException("ISO file not found: ${isoFile.absolutePath}")
        }
        
        try {
            val raf = RandomAccessFile(isoFile, "r")
            fileChannel = raf.channel
            
            // Detect file system type
            val fsType = detectFileSystemType(raf)
            Log.d(TAG, "Detected file system type: $fsType")
            
            var volumeDescriptor: VolumeDescriptor
            var xgdInfo: XGDInfo? = null
            var gamePartition: GamePartition? = null
            var videoPartition: VideoPartition? = null
            var allFiles = emptyList<DirectoryEntry>()
            
            if (fsType == FS_XGD) {
                // Xbox Game Disc format
                xgdInfo = parseXGDInfo(raf)
                volumeDescriptor = parseVolumeDescriptor(raf, XGD_GAME_OFFSET)
                
                // Parse game partition
                gamePartition = parseGamePartition(raf, XGD_GAME_OFFSET)
                
                // Parse video partition if exists
                if (xgdInfo.videoPartition != null) {
                    videoPartition = xgdInfo.videoPartition
                }
                
            } else {
                // Standard ISO/XISO format
                volumeDescriptor = parseVolumeDescriptor(raf, 0)
                allFiles = parseDirectoryTree(raf, volumeDescriptor.rootDirectoryEntry)
                
                // Try to find XBE for game partition
                gamePartition = findGamePartition(raf, allFiles)
            }
            
            // Extract disc information
            val (title, titleId, mediaId, region) = extractDiscInfo(raf, fsType)
            
            val disc = XboxDisc(
                file = isoFile,
                fileSystemType = fsType,
                volumeDescriptor = volumeDescriptor,
                xgdInfo = xgdInfo,
                gamePartition = gamePartition,
                videoPartition = videoPartition,
                allFiles = allFiles,
                totalSize = isoFile.length(),
                isXGD = fsType == FS_XGD,
                region = region,
                title = title,
                titleId = titleId,
                mediaId = mediaId
            )
            
            loadedDisc = disc
            isMounted = true
            
            Log.d(TAG, "ISO mounted successfully")
            Log.d(TAG, "Title: $title")
            Log.d(TAG, "Title ID: $titleId")
            Log.d(TAG, "Media ID: $mediaId")
            Log.d(TAG, "Region: 0x${region.toString(16)}")
            Log.d(TAG, "Size: ${isoFile.length()} bytes")
            Log.d(TAG, "XGD: ${disc.isXGD}")
            
            return disc
            
        } catch (e: Exception) {
            Log.e(TAG, "Failed to mount ISO: ${e.message}")
            fileChannel?.close()
            throw IllegalStateException("ISO mount failed: ${e.message}", e)
        }
    }

    /* ===============================
       File System Detection
       =============================== */

    private fun detectFileSystemType(raf: RandomAccessFile): Int {
        // Check for XGD security sector
        try {
            raf.seek(XGD_SS_OFFSET)
            val buffer = ByteArray(4)
            raf.read(buffer)
            val magic = ByteBuffer.wrap(buffer).order(ByteOrder.LITTLE_ENDIAN).int
            
            if (magic == 0x53534543) { // "CESS"
                return FS_XGD
            }
        } catch (e: Exception) {
            // Not XGD
        }
        
        // Check for ISO volume descriptor at sector 16
        try {
            raf.seek(16L * SECTOR_SIZE)
            val buffer = ByteArray(6)
            raf.read(buffer)
            
            val type = buffer[0].toInt() and 0xFF
            val identifier = String(buffer, 1, 5, Charsets.US_ASCII)
            
            if (type == 1 && identifier == "CD001") {
                return FS_XISO
            }
        } catch (e: Exception) {
            // Not standard ISO
        }
        
        // Try Redump format
        try {
            // Redump often has specific patterns
            raf.seek(0)
            val buffer = ByteArray(16)
            raf.read(buffer)
            
            // Check for common Redump header patterns
            val header = String(buffer, Charsets.ISO_8859_1)
            if (header.contains("PLAYSTATION") || header.contains("SEGA") || header.contains("XBOX")) {
                return FS_REDUMP
            }
        } catch (e: Exception) {
            // Not Redump
        }
        
        // Default to XISO
        return FS_XISO
    }

    /* ===============================
       Volume Descriptor Parsing
       =============================== */

    private fun parseVolumeDescriptor(raf: RandomAccessFile, baseOffset: Long): VolumeDescriptor {
        var sector = 16L
        
        while (true) {
            raf.seek(baseOffset + (sector * SECTOR_SIZE))
            val buffer = ByteArray(SECTOR_SIZE)
            raf.readFully(buffer)
            
            val bb = ByteBuffer.wrap(buffer).order(ByteOrder.LITTLE_ENDIAN)
            val type = bb.get().toInt() and 0xFF
            
            if (type == 0xFF) {
                throw IllegalStateException("Volume descriptor not found")
            }
            
            val identifier = ByteArray(5)
            bb.get(identifier)
            val idString = String(identifier, Charsets.US_ASCII)
            
            if (type == 1 && idString == "CD001") {
                return parsePrimaryVolumeDescriptor(bb, baseOffset)
            }
            
            sector++
        }
    }

    private fun parsePrimaryVolumeDescriptor(bb: ByteBuffer, baseOffset: Long): VolumeDescriptor {
        bb.position(0)
        
        val type = bb.get().toInt() and 0xFF
        val identifier = String(ByteArray(5).also { bb.get(it) }, Charsets.US_ASCII)
        val version = bb.get().toInt() and 0xFF
        
        // Skip unused byte
        bb.get()
        
        // System Identifier (32 bytes)
        val systemIdentifierBytes = ByteArray(32)
        bb.get(systemIdentifierBytes)
        val systemIdentifier = String(systemIdentifierBytes, Charsets.US_ASCII).trimEnd()
        
        // Volume Identifier (32 bytes)
        val volumeIdentifierBytes = ByteArray(32)
        bb.get(volumeIdentifierBytes)
        val volumeIdentifier = String(volumeIdentifierBytes, Charsets.US_ASCII).trimEnd()
        
        // Skip 8 bytes
        bb.position(bb.position() + 8)
        
        val volumeSpaceSize = bb.long
        val volumeSetSize = bb.getShort().toInt() and 0xFFFF
        val volumeSequenceNumber = bb.getShort().toInt() and 0xFFFF
        val logicalBlockSize = bb.getShort().toInt() and 0xFFFF
        val pathTableSize = bb.long
        val locationOfPathTable = bb.getInt().toLong() and 0xFFFFFFFFL
        val locationOfOptionalPathTable = bb.getInt().toLong() and 0xFFFFFFFFL
        
        // Parse root directory entry
        val rootDirectoryEntry = parseDirectoryEntry(bb)
        
        // Volume Set Identifier (128 bytes)
        val volumeSetIdentifierBytes = ByteArray(128)
        bb.get(volumeSetIdentifierBytes)
        val volumeSetIdentifier = String(volumeSetIdentifierBytes, Charsets.US_ASCII).trimEnd()
        
        // Publisher Identifier (128 bytes)
        val publisherIdentifierBytes = ByteArray(128)
        bb.get(publisherIdentifierBytes)
        val publisherIdentifier = String(publisherIdentifierBytes, Charsets.US_ASCII).trimEnd()
        
        // Data Preparer Identifier (128 bytes)
        val dataPreparerIdentifierBytes = ByteArray(128)
        bb.get(dataPreparerIdentifierBytes)
        val dataPreparerIdentifier = String(dataPreparerIdentifierBytes, Charsets.US_ASCII).trimEnd()
        
        // Application Identifier (128 bytes)
        val applicationIdentifierBytes = ByteArray(128)
        bb.get(applicationIdentifierBytes)
        val applicationIdentifier = String(applicationIdentifierBytes, Charsets.US_ASCII).trimEnd()
        
        // Copyright File Identifier (38 bytes)
        val copyrightFileIdentifierBytes = ByteArray(38)
        bb.get(copyrightFileIdentifierBytes)
        val copyrightFileIdentifier = String(copyrightFileIdentifierBytes, Charsets.US_ASCII).trimEnd()
        
        // Abstract File Identifier (36 bytes)
        val abstractFileIdentifierBytes = ByteArray(36)
        bb.get(abstractFileIdentifierBytes)
        val abstractFileIdentifier = String(abstractFileIdentifierBytes, Charsets.US_ASCII).trimEnd()
        
        // Bibliographic File Identifier (37 bytes)
        val bibliographicFileIdentifierBytes = ByteArray(37)
        bb.get(bibliographicFileIdentifierBytes)
        val bibliographicFileIdentifier = String(bibliographicFileIdentifierBytes, Charsets.US_ASCII).trimEnd()
        
        // Dates (17 bytes each)
        val volumeCreationDate = parseDateTime(ByteArray(17).also { bb.get(it) })
        val volumeModificationDate = parseDateTime(ByteArray(17).also { bb.get(it) })
        val volumeExpirationDate = parseDateTime(ByteArray(17).also { bb.get(it) })
        val volumeEffectiveDate = parseDateTime(ByteArray(17).also { bb.get(it) })
        
        return VolumeDescriptor(
            type = type,
            identifier = identifier,
            version = version,
            systemIdentifier = systemIdentifier,
            volumeIdentifier = volumeIdentifier,
            volumeSpaceSize = volumeSpaceSize,
            volumeSetSize = volumeSetSize,
            volumeSequenceNumber = volumeSequenceNumber,
            logicalBlockSize = logicalBlockSize,
            pathTableSize = pathTableSize,
            locationOfPathTable = locationOfPathTable,
            locationOfOptionalPathTable = locationOfOptionalPathTable,
            rootDirectoryEntry = rootDirectoryEntry,
            volumeSetIdentifier = volumeSetIdentifier,
            publisherIdentifier = publisherIdentifier,
            dataPreparerIdentifier = dataPreparerIdentifier,
            applicationIdentifier = applicationIdentifier,
            copyrightFileIdentifier = copyrightFileIdentifier,
            abstractFileIdentifier = abstractFileIdentifier,
            bibliographicFileIdentifier = bibliographicFileIdentifier,
            volumeCreationDate = volumeCreationDate,
            volumeModificationDate = volumeModificationDate,
            volumeExpirationDate = volumeExpirationDate,
            volumeEffectiveDate = volumeEffectiveDate
        )
    }

    /* ===============================
       Directory Entry Parsing
       =============================== */

    private fun parseDirectoryEntry(bb: ByteBuffer): DirectoryEntry {
        val length = bb.get().toInt() and 0xFF
        if (length == 0) {
            throw IllegalStateException("Invalid directory entry length")
        }
        
        val extendedAttributeRecordLength = bb.get().toInt() and 0xFF
        val locationOfExtent = bb.getInt().toLong() and 0xFFFFFFFFL
        val dataLength = bb.getInt().toLong() and 0xFFFFFFFFL
        
        // Recording Date and Time (7 bytes)
        val recordingDateAndTime = parseDateTime(ByteArray(7).also { bb.get(it) })
        
        val fileFlags = bb.get().toInt() and 0xFF
        val fileUnitSize = bb.get().toInt() and 0xFF
        val interleaveGapSize = bb.get().toInt() and 0xFF
        val volumeSequenceNumber = bb.getShort().toInt() and 0xFFFF
        
        val fileIdentifierLength = bb.get().toInt() and 0xFF
        val fileIdentifier = if (fileIdentifierLength > 0) {
            val idBytes = ByteArray(fileIdentifierLength)
            bb.get(idBytes)
            String(idBytes, Charsets.US_ASCII)
        } else {
            ""
        }
        
        // Padding to even length
        if (fileIdentifierLength % 2 == 0) {
            bb.get() // Padding byte
        }
        
        // System Use (variable length)
        val systemUseLength = length - (33 + fileIdentifierLength + (if (fileIdentifierLength % 2 == 0) 1 else 0))
        val systemUse = ByteArray(systemUseLength)
        if (systemUseLength > 0) {
            bb.get(systemUse)
        }
        
        val isDirectory = (fileFlags and ATTR_DIRECTORY) != 0
        val isHidden = (fileFlags and ATTR_HIDDEN) != 0
        val isSystem = (fileFlags and ATTR_SYSTEM) != 0
        
        return DirectoryEntry(
            length = length,
            extendedAttributeRecordLength = extendedAttributeRecordLength,
            locationOfExtent = locationOfExtent,
            dataLength = dataLength,
            recordingDateAndTime = recordingDateAndTime,
            fileFlags = fileFlags,
            fileUnitSize = fileUnitSize,
            interleaveGapSize = interleaveGapSize,
            volumeSequenceNumber = volumeSequenceNumber,
            fileIdentifier = fileIdentifier,
            systemUse = systemUse,
            isDirectory = isDirectory,
            isHidden = isHidden,
            isSystem = isSystem
        )
    }

    private fun parseDirectoryTree(raf: RandomAccessFile, rootEntry: DirectoryEntry): List<DirectoryEntry> {
        val entries = mutableListOf<DirectoryEntry>()
        parseDirectoryRecursive(raf, rootEntry, entries)
        return entries
    }

    private fun parseDirectoryRecursive(raf: RandomAccessFile, dirEntry: DirectoryEntry, entries: MutableList<DirectoryEntry>) {
        if (!dirEntry.isDirectory) return
        
        val sector = dirEntry.locationOfExtent
        val size = dirEntry.dataLength
        var offset = 0L
        
        raf.seek(sector * SECTOR_SIZE)
        val buffer = ByteArray(size.toInt())
        raf.readFully(buffer)
        
        val bb = ByteBuffer.wrap(buffer).order(ByteOrder.LITTLE_ENDIAN)
        
        while (offset < size) {
            bb.position(offset.toInt())
            
            val length = bb.get().toInt() and 0xFF
            if (length == 0) {
                // Skip to next sector
                offset = ((offset / SECTOR_SIZE) + 1) * SECTOR_SIZE
                continue
            }
            
            val entry = parseDirectoryEntry(bb)
            
            // Skip . and .. entries
            if (entry.fileIdentifier != "." && entry.fileIdentifier != "..") {
                entries.add(entry)
                
                // Recurse into subdirectories
                if (entry.isDirectory) {
                    parseDirectoryRecursive(raf, entry, entries)
                }
            }
            
            offset += length
        }
    }

    /* ===============================
       XGD Format Parsing
       =============================== */

    private fun parseXGDInfo(raf: RandomAccessFile): XGDInfo {
        // Parse Security Sector
        val securitySector = parseSecuritySector(raf)
        
        // Parse PFI
        val pfi = parsePFI(raf)
        
        // Parse DMI
        val dmi = parseDMI(raf)
        
        // Parse Video Partition
        val videoPartition = parseVideoPartition(raf)
        
        // Determine media type
        val mediaType = determineMediaType(pfi)
        
        return XGDInfo(
            mediaType = mediaType,
            layerBreak = XGD_LAYER_BREAK,
            totalSectors = XGD_TOTAL_SECTORS,
            securitySector = securitySector,
            pfi = pfi,
            dmi = dmi,
            videoPartition = videoPartition
        )
    }

    private fun parseSecuritySector(raf: RandomAccessFile): SecuritySector {
        raf.seek(XGD_SS_OFFSET)
        val buffer = ByteArray(0x1000)
        raf.readFully(buffer)
        
        val bb = ByteBuffer.wrap(buffer).order(ByteOrder.LITTLE_ENDIAN)
        
        val magic = bb.int
        if (magic != 0x53534543) { // "CESS"
            throw IllegalStateException("Invalid security sector magic")
        }
        
        val rootHash = ByteArray(20)
        bb.get(rootHash)
        
        val headerHash = ByteArray(20)
        bb.get(headerHash)
        
        val imageHash = ByteArray(20)
        bb.get(imageHash)
        
        val regionCode = bb.int
        
        bb.position(0x7C)
        val numRootTableEntries = bb.int
        val rootTableOffset = bb.int
        val numCertEntries = bb.int
        val certTableOffset = bb.int
        
        return SecuritySector(
            magic = magic,
            rootHash = rootHash,
            headerHash = headerHash,
            imageHash = imageHash,
            regionCode = regionCode,
            numRootTableEntries = numRootTableEntries,
            rootTableOffset = rootTableOffset,
            numCertEntries = numCertEntries,
            certTableOffset = certTableOffset
        )
    }

    private fun parsePFI(raf: RandomAccessFile): PFI {
        raf.seek(XGD_PFI_OFFSET)
        val buffer = ByteArray(0x800)
        raf.readFully(buffer)
        
        val bb = ByteBuffer.wrap(buffer).order(ByteOrder.BIG_ENDIAN) // PFI is big endian
        
        val magic = bb.int
        if (magic != 0x50464900) { // "PFI\0"
            throw IllegalStateException("Invalid PFI magic")
        }
        
        val dataStructureLength = bb.short.toInt() and 0xFFFF
        val reserved1 = bb.short.toInt() and 0xFFFF
        val version = bb.short.toInt() and 0xFFFF
        
        val discStructure = bb.get().toInt() and 0xFF
        val sessionNumber = bb.get().toInt() and 0xFF
        val sessionStartAddress = bb.int
        val layer0StartAddress = bb.int
        val layer0EndAddress = bb.int
        val layer1StartAddress = bb.int
        val layer1EndAddress = bb.int
        
        return PFI(
            magic = magic,
            dataStructureLength = dataStructureLength,
            reserved1 = reserved1,
            version = version,
            discStructure = discStructure,
            sessionNumber = sessionNumber,
            sessionStartAddress = sessionStartAddress,
            layer0StartAddress = layer0StartAddress,
            layer0EndAddress = layer0EndAddress,
            layer1StartAddress = layer1StartAddress,
            layer1EndAddress = layer1EndAddress
        )
    }

    private fun parseDMI(raf: RandomAccessFile): DMI {
        raf.seek(XGD_DMI_OFFSET)
        val buffer = ByteArray(0x800)
        raf.readFully(buffer)
        
        val bb = ByteBuffer.wrap(buffer).order(ByteOrder.BIG_ENDIAN) // DMI is big endian
        
        val magic = bb.int
        if (magic != 0x444D4900) { // "DMI\0"
            throw IllegalStateException("Invalid DMI magic")
        }
        
        val dataStructureLength = bb.short.toInt() and 0xFFFF
        val reserved1 = bb.short.toInt() and 0xFFFF
        val version = bb.short.toInt() and 0xFFFF
        
        val discStructure = bb.get().toInt() and 0xFF
        val sessionNumber = bb.get().toInt() and 0xFF
        val sessionStartAddress = bb.int
        
        val bcaFlag = bb.get().toInt() and 0xFF
        val bcaData = ByteArray(40)
        bb.get(bcaData)
        
        return DMI(
            magic = magic,
            dataStructureLength = dataStructureLength,
            reserved1 = reserved1,
            version = version,
            discStructure = discStructure,
            sessionNumber = sessionNumber,
            sessionStartAddress = sessionStartAddress,
            bcaFlag = bcaFlag,
            bcaData = bcaData
        )
    }

    private fun parseVideoPartition(raf: RandomAccessFile): VideoPartition? {
        try {
            raf.seek(XGD_VIDEO_OFFSET)
            val buffer = ByteArray(SECTOR_SIZE)
            raf.readFully(buffer)
            
            // Check if video partition exists
            val bb = ByteBuffer.wrap(buffer).order(ByteOrder.LITTLE_ENDIAN)
            val type = bb.get().toInt() and 0xFF
            
            if (type != 1) {
                return null // No video partition
            }
            
            // Parse video partition directory
            bb.position(0)
            val rootEntry = parseDirectoryEntry(bb)
            val files = parseDirectoryTree(raf, rootEntry)
            
            return VideoPartition(
                startSector = XGD_VIDEO_OFFSET / SECTOR_SIZE,
                size = files.sumOf { it.dataLength },
                files = files
            )
            
        } catch (e: Exception) {
            Log.w(TAG, "Failed to parse video partition: ${e.message}")
            return null
        }
    }

    private fun parseGamePartition(raf: RandomAccessFile, baseOffset: Long): GamePartition {
        raf.seek(baseOffset)
        val buffer = ByteArray(SECTOR_SIZE)
        raf.readFully(buffer)
        
        val bb = ByteBuffer.wrap(buffer).order(ByteOrder.LITTLE_ENDIAN)
        val rootEntry = parseDirectoryEntry(bb)
        val files = parseDirectoryTree(raf, rootEntry)
        
  val xbeEntry = files.find { it.fileIdentifier.equals("default.xbe", true) }
var xbeImage: XbeLoader.XbeImage? = null
var xbeLocation = 0L

if (xbeEntry != null) {
    xbeLocation = xbeEntry.locationOfExtent * SECTOR_SIZE
    try {
        val xbeLoader = XbeLoader(XboxMemory(XboxMMIO()))
        val xbeData = readFile(xbeEntry)
        xbeImage = xbeLoader.getLoadedImage() // ✅ استخدم xbeLoader
    } catch (e: Exception) {
        Log.w(TAG, "Failed to parse XBE: ${e.message}")
    }
}
        
        return GamePartition(
            startSector = baseOffset / SECTOR_SIZE,
            size = files.sumOf { it.dataLength },
            xbeLocation = xbeLocation,
            defaultXbe = xbeImage,
            files = files
        )
    }

    private fun findGamePartition(raf: RandomAccessFile, files: List<DirectoryEntry>): GamePartition? {
        // Look for default.xbe
        val xbeEntries = files.filter { it.fileIdentifier.equals("default.xbe", true) }
        
        if (xbeEntries.isEmpty()) {
            return null
        }
        
        // Use the first XBE found
        val xbeEntry = xbeEntries.first()
        
        // Find all files in the same directory
        val gameFiles = files.filter { 
            it.locationOfExtent >= xbeEntry.locationOfExtent &&
            it.locationOfExtent < xbeEntry.locationOfExtent + 1000 // Arbitrary limit
        }
        
        return GamePartition(
            startSector = xbeEntry.locationOfExtent,
            size = gameFiles.sumOf { it.dataLength },
            xbeLocation = xbeEntry.locationOfExtent * SECTOR_SIZE,
            defaultXbe = null,
            files = gameFiles
        )
    }

    /* ===============================
       Disc Information Extraction
       =============================== */

    private fun extractDiscInfo(raf: RandomAccessFile, fsType: Int): Quadruple<String, String, String, Int> {
        var title = "Unknown"
        var titleId = "00000000"
        var mediaId = "00000000"
        var region = REGION_ALL
        
        try {
            if (fsType == FS_XGD) {
                // Extract from XGD security sector
                raf.seek(XGD_SS_OFFSET)
                val buffer = ByteArray(0x100)
                raf.readFully(buffer)
                
                val bb = ByteBuffer.wrap(buffer).order(ByteOrder.LITTLE_ENDIAN)
                bb.position(0x0C)
                region = bb.int
                
                // Try to find title from XBE if available
                title = "Xbox Game Disc"
                
            } else {
                // Try to find default.xbe and parse its certificate
                raf.seek(16L * SECTOR_SIZE + 0x9C) // Volume identifier might have title
                val titleBuffer = ByteArray(32)
                raf.readFully(titleBuffer)
                title = String(titleBuffer, Charsets.US_ASCII).trimEnd()
                
                if (title.isEmpty() || title == " ".repeat(32)) {
                    title = "Xbox Game"
                }
            }
            
            // Generate media ID from file hash
            val fileHash = calculateFileHash(raf)
            mediaId = fileHash.substring(0, 8).uppercase()
            
        } catch (e: Exception) {
            Log.w(TAG, "Failed to extract disc info: ${e.message}")
        }
        
        return Quadruple(title, titleId, mediaId, region)
    }

    /* ===============================
       File Operations
       =============================== */

    fun readFile(fileEntry: DirectoryEntry): ByteArray {
        val disc = loadedDisc ?: throw IllegalStateException("No disc mounted")
        
        val cacheKey = "${fileEntry.locationOfExtent}:${fileEntry.dataLength}"
        fileCache[cacheKey]?.let {
            return it
        }
        
        val buffer = ByteArray(fileEntry.dataLength.toInt())
        
        fileChannel?.let { fc ->
            val position = fileEntry.locationOfExtent * SECTOR_SIZE
            fc.position(position)
            
            val bb = ByteBuffer.wrap(buffer)
            var remaining = fileEntry.dataLength
            var offset = 0L
            
            while (remaining > 0) {
                val sector = (position + offset) / SECTOR_SIZE
                sectorCache[sector]?.let { cachedSector ->
                    val toCopy = minOf(cachedSector.size.toLong(), remaining).toInt()
                    System.arraycopy(cachedSector, 0, buffer, offset.toInt(), toCopy)
                    offset += toCopy
                    remaining -= toCopy
                } ?: run {
                    // Read sector
                    val sectorBuffer = ByteArray(SECTOR_SIZE)
                    bb.position(offset.toInt())
                    fc.read(bb)
                    
                    // Cache sector
                    if (sectorCache.size >= CACHE_SIZE) {
                        sectorCache.remove(sectorCache.keys.first())
                    }
                    sectorCache[sector] = sectorBuffer
                    
                    val toCopy = minOf(SECTOR_SIZE.toLong(), remaining).toInt()
                    System.arraycopy(sectorBuffer, 0, buffer, offset.toInt(), toCopy)
                    offset += toCopy
                    remaining -= toCopy
                }
            }
        }
        
        // Cache file
        if (fileCache.size >= CACHE_SIZE) {
            fileCache.remove(fileCache.keys.first())
        }
        fileCache[cacheKey] = buffer
        
        return buffer
    }

    fun findFile(path: String): DirectoryEntry? {
        val disc = loadedDisc ?: return null
        
        val parts = path.split("/").filter { it.isNotEmpty() }
        var currentEntries = disc.allFiles
        
        for (part in parts) {
            val entry = currentEntries.find { 
                it.fileIdentifier.equals(part, true) 
            } ?: return null
            
            if (part == parts.last()) {
                return entry
            }
            
            if (!entry.isDirectory) {
                return null
            }
            
            // Read directory contents
            currentEntries = parseDirectoryTree(
                RandomAccessFile(disc.file, "r"),
                entry
            )
        }
        
        return null
    }

    fun listDirectory(path: String): List<DirectoryEntry> {
        val disc = loadedDisc ?: return emptyList()
        
        if (path.isEmpty() || path == "/") {
            return disc.allFiles
        }
        
        val entry = findFile(path) ?: return emptyList()
        if (!entry.isDirectory) return emptyList()
        
        return parseDirectoryTree(
            RandomAccessFile(disc.file, "r"),
            entry
        )
    }

    /* ===============================
       Sector Operations
       =============================== */

    fun readSector(sector: Long): ByteArray {
        val disc = loadedDisc ?: throw IllegalStateException("No disc mounted")
        
        sectorCache[sector]?.let {
            return it
        }
        
        val buffer = ByteArray(SECTOR_SIZE)
        fileChannel?.let { fc ->
            val position = sector * SECTOR_SIZE
            fc.position(position)
            
            val bb = ByteBuffer.wrap(buffer)
            fc.read(bb)
            
            // Cache sector
            if (sectorCache.size >= CACHE_SIZE) {
                sectorCache.remove(sectorCache.keys.first())
            }
            sectorCache[sector] = buffer
        }
        
        return buffer
    }

    fun readSectors(startSector: Long, count: Int): ByteArray {
        val buffer = ByteArray(count * SECTOR_SIZE)
        
        for (i in 0 until count) {
            val sectorData = readSector(startSector + i)
            System.arraycopy(sectorData, 0, buffer, i * SECTOR_SIZE, SECTOR_SIZE)
        }
        
        return buffer
    }

    /* ===============================
       Utility Functions
       =============================== */

    private fun parseDateTime(bytes: ByteArray): String {
        if (bytes.size == 7) {
            // 7-byte format
            val year = String(bytes, 0, 4, Charsets.US_ASCII)
            val month = String(bytes, 4, 2, Charsets.US_ASCII)
            val day = String(bytes, 6, 2, Charsets.US_ASCII)
            val hour = String(bytes, 8, 2, Charsets.US_ASCII)
            val minute = String(bytes, 10, 2, Charsets.US_ASCII)
            val second = String(bytes, 12, 2, Charsets.US_ASCII)
            val hundredths = String(bytes, 14, 2, Charsets.US_ASCII)
            val offset = bytes[16].toInt()
            
            return "$year-$month-${day}T$hour:$minute:$second.$hundredths"
        } else if (bytes.size == 17) {
            // 17-byte format with timezone
            val datetime = String(bytes, 0, 16, Charsets.US_ASCII).trimEnd()
            return datetime
        }
        
        return "Unknown"
    }

    private fun determineMediaType(pfi: PFI?): Int {
        pfi ?: return MEDIA_DVD_5
        
        return when (pfi.discStructure) {
            0x01 -> MEDIA_DVD_5
            0x02 -> MEDIA_DVD_9
            0x10 -> MEDIA_CD
            else -> MEDIA_DVD_5
        }
    }

    private fun calculateFileHash(raf: RandomAccessFile): String {
        raf.seek(0)
        
        val buffer = ByteArray(8192)
        val digest = java.security.MessageDigest.getInstance("SHA-256")
        
        var bytesRead: Int
        do {
            bytesRead = raf.read(buffer)
            if (bytesRead > 0) {
                digest.update(buffer, 0, bytesRead)
            }
        } while (bytesRead != -1)
        
        val hash = digest.digest()
        return hash.joinToString("") { "%02x".format(it) }
    }

    /* ===============================
       Public API
       =============================== */

    fun isMounted(): Boolean = isMounted

    fun getMountedDisc(): XboxDisc? = loadedDisc

    fun getDiscInfo(): Map<String, String> {
        val disc = loadedDisc ?: return emptyMap()
        
        return mapOf(
            "Title" to disc.title,
            "Title ID" to disc.titleId,
            "Media ID" to disc.mediaId,
            "Region" to when (disc.region) {
                REGION_NA -> "North America"
                REGION_JAPAN -> "Japan"
                REGION_EUROPE -> "Europe"
                REGION_ALL -> "All Regions"
                else -> "Unknown (0x${disc.region.toString(16)})"
            },
            "File System" to when (disc.fileSystemType) {
                FS_XISO -> "XISO"
                FS_REDUMP -> "Redump"
                FS_XGD -> "XGD"
                else -> "Unknown"
            },
            "Size" to "${disc.totalSize} bytes",
            "XGD" to disc.isXGD.toString(),
            "Media Type" to when (disc.xgdInfo?.mediaType) {
                MEDIA_DVD_5 -> "DVD-5"
                MEDIA_DVD_9 -> "DVD-9"
                MEDIA_CD -> "CD"
                else -> "Unknown"
            },
            "Files" to disc.allFiles.size.toString(),
            "Has Video" to (disc.videoPartition != null).toString()
        )
    }

    fun extractXbe(): ByteArray? {
        val disc = loadedDisc ?: return null
        
        val xbeEntry = disc.allFiles.find { 
            it.fileIdentifier.equals("default.xbe", true) 
        } ?: return null
        
        return readFile(xbeEntry)
    }

    fun extractFile(path: String): ByteArray? {
        val entry = findFile(path) ?: return null
        return readFile(entry)
    }

    fun getFileList(): List<String> {
        val disc = loadedDisc ?: return emptyList()
        return disc.allFiles.map { it.fileIdentifier }
    }

    fun unmount() {
        fileCache.clear()
        sectorCache.clear()
        
        fileChannel?.close()
        fileChannel = null
        
        loadedDisc = null
        isMounted = false
        
        Log.d(TAG, "ISO unmounted")
    }

    /* ===============================
       Verification
       =============================== */

    fun verifyDisc(): Boolean {
        val disc = loadedDisc ?: return false
        
        try {
            // Check file size
            if (disc.totalSize == 0L) {
                Log.e(TAG, "Disc size is zero")
                return false
            }
            
            // Check for essential files
            val hasXbe = disc.allFiles.any { 
                it.fileIdentifier.equals("default.xbe", true) 
            }
            
            if (!hasXbe) {
                Log.w(TAG, "No default.xbe found")
                // Some discs might not have XBE (video discs)
            }
            
            // Verify XGD structure if applicable
            if (disc.isXGD) {
                val xgdInfo = disc.xgdInfo
                if (xgdInfo == null) {
                    Log.e(TAG, "XGD info missing")
                    return false
                }
                
                // Check security sector
                if (xgdInfo.securitySector?.magic != 0x53534543) {
                    Log.e(TAG, "Invalid security sector")
                    return false
                }
            }
            
            return true
            
        } catch (e: Exception) {
            Log.e(TAG, "Disc verification failed: ${e.message}")
            return false
        }
    }

    /* ===============================
       Data Classes for Return Types
       =============================== */

    data class Quadruple<A, B, C, D>(
        val first: A,
        val second: B,
        val third: C,
        val fourth: D
    )
}
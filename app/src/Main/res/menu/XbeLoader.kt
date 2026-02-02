package og.xaniteog

import java.io.File
import java.io.RandomAccessFile
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.security.MessageDigest
import android.util.Log
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import java.io.ByteArrayOutputStream

class XbeLoader(
    private val memory: XboxMemory
) {

    companion object {
        private const val TAG = "XbeLoader"
        
        // XBE Magic Numbers
        const val XBE_MAGIC = 0x48454258  // "XBEH"
        const val XBE_VERSION = 0x00000002
        
        // Section Flags
        const val XBE_SECTION_WRITABLE = 0x00000001
        const val XBE_SECTION_EXECUTABLE = 0x00000002
        const val XBE_SECTION_PRELOAD = 0x00000004
        const val XBE_SECTION_INSERTED = 0x00000008
        
        // Debug Flags
        const val XBE_DEBUG_UNKNOWN = 0x00000001
        const val XBE_DEBUG_DBGSUPPORTED = 0x00000002
        const val XBE_DEBUG_DEBUGGABLE = 0x00000004
        
        // Media Types
        const val XBE_MEDIA_TYPE_HD = 0x00000001
        const val XBE_MEDIA_TYPE_DVD_X2 = 0x00000002
        const val XBE_MEDIA_TYPE_DVD_CD = 0x00000004
        const val XBE_MEDIA_TYPE_CD = 0x00000008
        const val XBE_MEDIA_TYPE_DVD_5_RO = 0x00000010
        const val XBE_MEDIA_TYPE_DVD_9_RO = 0x00000020
        const val XBE_MEDIA_TYPE_DVD_5_RW = 0x00000040
        const val XBE_MEDIA_TYPE_DVD_9_RW = 0x00000080
        const val XBE_MEDIA_TYPE_DONGLE = 0x00000100
        const val XBE_MEDIA_TYPE_MEDIA_MASK = 0x00FFFFFF
        const val XBE_MEDIA_TYPE_NONSECURE_HD = 0x80000000.toInt()
        const val XBE_MEDIA_TYPE_NONSECURE_MODE = 0x40000000
        
        // Certificate Flags
        const val XBE_CERT_FLAG_MANUFACTURING = 0x00000001
        const val XBE_CERT_FLAG_LIMITED = 0x00000002
        const val XBE_CERT_FLAG_DEBUG = 0x00000004
        const val XBE_CERT_FLAG_TEST = 0x00000008
        
        // Game Regions
        const val XBE_REGION_NA = 0x00000001
        const val XBE_REGION_JAPAN = 0x00000002
        const val XBE_REGION_RESTOFWORLD = 0x00000004
        const val XBE_REGION_MANUFACTURING = 0x80000000.toInt()
        
        // Library Flags
        const val XBE_LIBRARY_FLAG_QFE_VERSION = 0x0000FFFF
        const val XBE_LIBRARY_FLAG_DEBUG_BUILD = 0x00010000
        
        // Logo Bitmap Format
        const val XBE_LOGO_WIDTH = 100
        const val XBE_LOGO_HEIGHT = 42
        const val XBE_LOGO_SIZE = XBE_LOGO_WIDTH * XBE_LOGO_HEIGHT * 4 // ARGB
    }

    // ===== XBE Structures =====
    
    data class XbeHeader(
        val magic: Int,
        val digitalSignature: ByteArray,
        val baseAddress: Int,
        val sizeOfHeaders: Int,
        val sizeOfImage: Int,
        val sizeOfImageHeader: Int,
        val timeDateStamp: Int,
        val certificateAddress: Int,
        val numberOfSections: Int,
        val sectionHeadersAddress: Int,
        val initFlags: Int,
        val entryPoint: Int,
        val tlsAddress: Int,
        val stackSize: Int,
        val peHeapReserve: Int,
        val peHeapCommit: Int,
        val peBaseAddress: Int,
        val peSizeOfImage: Int,
        val peChecksum: Int,
        val peTimeDateStamp: Int,
        val debugPathnameAddress: Int,
        val debugFilenameAddress: Int,
        val debugUnicodeFilenameAddress: Int,
        val kernelImageThunkAddress: Int,
        val nonKernelImportDirectoryAddress: Int,
        val numberOfLibraryVersions: Int,
        val libraryVersionsAddress: Int,
        val kernelLibraryVersionAddress: Int,
        val xapiLibraryVersionAddress: Int,
        val logoBitmapAddress: Int,
        val logoBitmapSize: Int,
        val debugPathname: String,
        val debugFilename: String
    ) {
        override fun equals(other: Any?): Boolean {
            if (this === other) return true
            if (javaClass != other?.javaClass) return false
            
            other as XbeHeader
            
            if (magic != other.magic) return false
            if (!digitalSignature.contentEquals(other.digitalSignature)) return false
            if (baseAddress != other.baseAddress) return false
            if (sizeOfHeaders != other.sizeOfHeaders) return false
            if (sizeOfImage != other.sizeOfImage) return false
            if (sizeOfImageHeader != other.sizeOfImageHeader) return false
            if (timeDateStamp != other.timeDateStamp) return false
            if (certificateAddress != other.certificateAddress) return false
            if (numberOfSections != other.numberOfSections) return false
            if (sectionHeadersAddress != other.sectionHeadersAddress) return false
            if (initFlags != other.initFlags) return false
            if (entryPoint != other.entryPoint) return false
            if (tlsAddress != other.tlsAddress) return false
            if (stackSize != other.stackSize) return false
            if (peHeapReserve != other.peHeapReserve) return false
            if (peHeapCommit != other.peHeapCommit) return false
            if (peBaseAddress != other.peBaseAddress) return false
            if (peSizeOfImage != other.peSizeOfImage) return false
            if (peChecksum != other.peChecksum) return false
            if (peTimeDateStamp != other.peTimeDateStamp) return false
            if (debugPathnameAddress != other.debugPathnameAddress) return false
            if (debugFilenameAddress != other.debugFilenameAddress) return false
            if (debugUnicodeFilenameAddress != other.debugUnicodeFilenameAddress) return false
            if (kernelImageThunkAddress != other.kernelImageThunkAddress) return false
            if (nonKernelImportDirectoryAddress != other.nonKernelImportDirectoryAddress) return false
            if (numberOfLibraryVersions != other.numberOfLibraryVersions) return false
            if (libraryVersionsAddress != other.libraryVersionsAddress) return false
            if (kernelLibraryVersionAddress != other.kernelLibraryVersionAddress) return false
            if (xapiLibraryVersionAddress != other.xapiLibraryVersionAddress) return false
            if (logoBitmapAddress != other.logoBitmapAddress) return false
            if (logoBitmapSize != other.logoBitmapSize) return false
            if (debugPathname != other.debugPathname) return false
            if (debugFilename != other.debugFilename) return false
            
            return true
        }
        
        override fun hashCode(): Int {
            var result = magic
            result = 31 * result + digitalSignature.contentHashCode()
            result = 31 * result + baseAddress
            result = 31 * result + sizeOfHeaders
            result = 31 * result + sizeOfImage
            result = 31 * result + sizeOfImageHeader
            result = 31 * result + timeDateStamp
            result = 31 * result + certificateAddress
            result = 31 * result + numberOfSections
            result = 31 * result + sectionHeadersAddress
            result = 31 * result + initFlags
            result = 31 * result + entryPoint
            result = 31 * result + tlsAddress
            result = 31 * result + stackSize
            result = 31 * result + peHeapReserve
            result = 31 * result + peHeapCommit
            result = 31 * result + peBaseAddress
            result = 31 * result + peSizeOfImage
            result = 31 * result + peChecksum
            result = 31 * result + peTimeDateStamp
            result = 31 * result + debugPathnameAddress
            result = 31 * result + debugFilenameAddress
            result = 31 * result + debugUnicodeFilenameAddress
            result = 31 * result + kernelImageThunkAddress
            result = 31 * result + nonKernelImportDirectoryAddress
            result = 31 * result + numberOfLibraryVersions
            result = 31 * result + libraryVersionsAddress
            result = 31 * result + kernelLibraryVersionAddress
            result = 31 * result + xapiLibraryVersionAddress
            result = 31 * result + logoBitmapAddress
            result = 31 * result + logoBitmapSize
            result = 31 * result + debugPathname.hashCode()
            result = 31 * result + debugFilename.hashCode()
            return result
        }
    }
    
    data class XbeSectionHeader(
        val flags: Int,
        val virtualAddress: Int,
        val virtualSize: Int,
        val rawAddress: Int,
        val rawSize: Int,
        val sectionNameAddress: Int,
        val sectionNameReferenceCount: Int,
        val headSharedPageReferenceCountAddress: Int,
        val tailSharedPageReferenceCountAddress: Int,
        val sectionDigest: ByteArray,
        val sectionName: String
    ) {
        override fun equals(other: Any?): Boolean {
            if (this === other) return true
            if (javaClass != other?.javaClass) return false
            
            other as XbeSectionHeader
            
            if (flags != other.flags) return false
            if (virtualAddress != other.virtualAddress) return false
            if (virtualSize != other.virtualSize) return false
            if (rawAddress != other.rawAddress) return false
            if (rawSize != other.rawSize) return false
            if (sectionNameAddress != other.sectionNameAddress) return false
            if (sectionNameReferenceCount != other.sectionNameReferenceCount) return false
            if (headSharedPageReferenceCountAddress != other.headSharedPageReferenceCountAddress) return false
            if (tailSharedPageReferenceCountAddress != other.tailSharedPageReferenceCountAddress) return false
            if (!sectionDigest.contentEquals(other.sectionDigest)) return false
            if (sectionName != other.sectionName) return false
            
            return true
        }
        
        override fun hashCode(): Int {
            var result = flags
            result = 31 * result + virtualAddress
            result = 31 * result + virtualSize
            result = 31 * result + rawAddress
            result = 31 * result + rawSize
            result = 31 * result + sectionNameAddress
            result = 31 * result + sectionNameReferenceCount
            result = 31 * result + headSharedPageReferenceCountAddress
            result = 31 * result + tailSharedPageReferenceCountAddress
            result = 31 * result + sectionDigest.contentHashCode()
            result = 31 * result + sectionName.hashCode()
            return result
        }
    }
    
    data class XbeCertificate(
        val size: Int,
        val timeDateStamp: Int,
        val titleId: Int,
        val titleName: String,
        val alternateTitleIds: IntArray,
        val allowedMedia: Int,
        val gameRegion: Int,
        val gameRatings: Int,
        val diskNumber: Int,
        val version: Int,
        val lanKey: ByteArray,
        val signatureKey: ByteArray,
        val alternateSignatureKeys: Array<ByteArray>,
        val originalCertificateSize: Int,
        val onlineServiceId: Int,
        val securityFlags: Int,
        val executionId: ByteArray
    ) {
        override fun equals(other: Any?): Boolean {
            if (this === other) return true
            if (javaClass != other?.javaClass) return false
            
            other as XbeCertificate
            
            if (size != other.size) return false
            if (timeDateStamp != other.timeDateStamp) return false
            if (titleId != other.titleId) return false
            if (titleName != other.titleName) return false
            if (!alternateTitleIds.contentEquals(other.alternateTitleIds)) return false
            if (allowedMedia != other.allowedMedia) return false
            if (gameRegion != other.gameRegion) return false
            if (gameRatings != other.gameRatings) return false
            if (diskNumber != other.diskNumber) return false
            if (version != other.version) return false
            if (!lanKey.contentEquals(other.lanKey)) return false
            if (!signatureKey.contentEquals(other.signatureKey)) return false
            if (!alternateSignatureKeys.contentDeepEquals(other.alternateSignatureKeys)) return false
            if (originalCertificateSize != other.originalCertificateSize) return false
            if (onlineServiceId != other.onlineServiceId) return false
            if (securityFlags != other.securityFlags) return false
            if (!executionId.contentEquals(other.executionId)) return false
            
            return true
        }
        
        override fun hashCode(): Int {
            var result = size
            result = 31 * result + timeDateStamp
            result = 31 * result + titleId
            result = 31 * result + titleName.hashCode()
            result = 31 * result + alternateTitleIds.contentHashCode()
            result = 31 * result + allowedMedia
            result = 31 * result + gameRegion
            result = 31 * result + gameRatings
            result = 31 * result + diskNumber
            result = 31 * result + version
            result = 31 * result + lanKey.contentHashCode()
            result = 31 * result + signatureKey.contentHashCode()
            result = 31 * result + alternateSignatureKeys.contentDeepHashCode()
            result = 31 * result + originalCertificateSize
            result = 31 * result + onlineServiceId
            result = 31 * result + securityFlags
            result = 31 * result + executionId.contentHashCode()
            return result
        }
    }
    
    data class XbeLibraryVersion(
        val libraryName: String,
        val majorVersion: Int,
        val minorVersion: Int,
        val buildVersion: Int,
        val flags: Int
    )
    
    data class XbeSection(
        val header: XbeSectionHeader,
        val data: ByteArray
    ) {
        override fun equals(other: Any?): Boolean {
            if (this === other) return true
            if (javaClass != other?.javaClass) return false
            
            other as XbeSection
            
            if (header != other.header) return false
            if (!data.contentEquals(other.data)) return false
            
            return true
        }
        
        override fun hashCode(): Int {
            var result = header.hashCode()
            result = 31 * result + data.contentHashCode()
            return result
        }
    }
    
    data class XbeImage(
        val header: XbeHeader,
        val certificate: XbeCertificate,
        val sections: List<XbeSection>,
        val libraryVersions: List<XbeLibraryVersion>,
        val kernelImports: List<ImportEntry>,
        val userImports: List<ImportEntry>,
        val tlsData: ByteArray?,
        val logoBitmap: ByteArray?,
        val debugInfo: DebugInfo?,
        val rawData: ByteArray // إضافة لحفظ البيانات الأولية
    )
    
    data class MemoryRegion(
        val baseAddress: Long,
        val size: Long,
        var protection: Int,
        val allocationType: Int,
        var isFree: Boolean,
        var isCommitted: Boolean
    )
    
    data class ImportEntry(
        val functionName: String,
        val ordinal: Int,
        val address: Int,
        val thunkAddress: Int
    )
    
    data class DebugInfo(
        val pathname: String,
        val filename: String,
        val unicodeFilename: String?,
        val timestamp: String
    )
    
    data class LogoBitmap(
        val width: Int,
        val height: Int,
        val format: Int,
        val pixels: IntArray,
        val rawData: ByteArray
    )

    // ===== Loader State =====
    private var loadedImage: XbeImage? = null
    private var isLoaded = false
    private var entryPoint = 0

    /* ===============================
       Main Load Function
       =============================== */

    fun load(xbeFile: File): Int {
        Log.d(TAG, "Loading XBE: ${xbeFile.name}")
        
        if (!xbeFile.exists()) {
            throw IllegalStateException("XBE file not found: ${xbeFile.absolutePath}")
        }
        
        try {
            // Read entire file
            val data = RandomAccessFile(xbeFile, "r").use { raf ->
                ByteArray(raf.length().toInt()).also {
                    raf.readFully(it)
                }
            }
            
            val buffer = ByteBuffer.wrap(data).order(ByteOrder.LITTLE_ENDIAN)
            
            // Parse XBE image
            val image = parseXbeImage(buffer, data)
            loadedImage = image
            isLoaded = true
            
            // Calculate actual entry point
            entryPoint = image.header.entryPoint
            
            // Map image to memory
            mapImageToMemory(image)
            
            // Setup imports
            setupImports(image)
            
            // Setup TLS if present
            setupTLS(image)
            
            Log.d(TAG, "XBE loaded successfully")
            Log.d(TAG, "Title: ${image.certificate.titleName}")
            Log.d(TAG, "Entry Point: 0x${entryPoint.toString(16)}")
            Log.d(TAG, "Sections: ${image.sections.size}")
            Log.d(TAG, "Image Size: 0x${image.header.sizeOfImage.toString(16)}")
            
            return entryPoint
            
        } catch (e: Exception) {
            Log.e(TAG, "Failed to load XBE: ${e.message}")
            throw IllegalStateException("XBE load failed: ${e.message}", e)
        }
    }

    /* ===============================
       XBE Image Parsing
       =============================== */

    private fun parseXbeImage(buffer: ByteBuffer, rawData: ByteArray): XbeImage {
        buffer.position(0)
        
        // Parse header
        val header = parseHeader(buffer)
        
        // Parse certificate
        val certificate = parseCertificate(buffer, header.certificateAddress - header.baseAddress)
        
        // Parse sections
        val sections = parseSections(buffer, header)
        
        // Parse library versions
        val libraryVersions = parseLibraryVersions(buffer, header)
        
        // Parse imports
        val (kernelImports, userImports) = parseImports(buffer, header)
        
        // Parse TLS data
        val tlsData = parseTLSData(buffer, header)
        
        // Parse logo bitmap
        val logoBitmap = parseLogoBitmap(buffer, header)
        
        // Parse debug info
        val debugInfo = parseDebugInfo(buffer, header)
        
        return XbeImage(
            header = header,
            certificate = certificate,
            sections = sections,
            libraryVersions = libraryVersions,
            kernelImports = kernelImports,
            userImports = userImports,
            tlsData = tlsData,
            logoBitmap = logoBitmap,
            debugInfo = debugInfo,
            rawData = rawData
        )
    }

    /* ===============================
       Header Parsing
       =============================== */

    private fun parseHeader(buffer: ByteBuffer): XbeHeader {
        buffer.position(0)
        
        val magic = buffer.int
        if (magic != XBE_MAGIC) {
            throw IllegalStateException("Invalid XBE magic: 0x${magic.toString(16)}")
        }
        
        // Read digital signature (256 bytes)
        val digitalSignature = ByteArray(256)
        buffer.get(digitalSignature)
        
        val baseAddress = buffer.int
        val sizeOfHeaders = buffer.int
        val sizeOfImage = buffer.int
        val sizeOfImageHeader = buffer.int
        val timeDateStamp = buffer.int
        val certificateAddress = buffer.int
        val numberOfSections = buffer.int
        val sectionHeadersAddress = buffer.int
        val initFlags = buffer.int
        val entryPoint = buffer.int
        val tlsAddress = buffer.int
        val stackSize = buffer.int
        val peHeapReserve = buffer.int
        val peHeapCommit = buffer.int
        val peBaseAddress = buffer.int
        val peSizeOfImage = buffer.int
        val peChecksum = buffer.int
        val peTimeDateStamp = buffer.int
        val debugPathnameAddress = buffer.int
        val debugFilenameAddress = buffer.int
        val debugUnicodeFilenameAddress = buffer.int
        val kernelImageThunkAddress = buffer.int
        val nonKernelImportDirectoryAddress = buffer.int
        val numberOfLibraryVersions = buffer.int
        val libraryVersionsAddress = buffer.int
        val kernelLibraryVersionAddress = buffer.int
        val xapiLibraryVersionAddress = buffer.int
        val logoBitmapAddress = buffer.int
        val logoBitmapSize = buffer.int
        
        // Read debug strings
        val debugPathname = if (debugPathnameAddress != 0) {
            readCString(buffer, debugPathnameAddress - baseAddress)
        } else ""
        
        val debugFilename = if (debugFilenameAddress != 0) {
            readCString(buffer, debugFilenameAddress - baseAddress)
        } else ""
        
        return XbeHeader(
            magic = magic,
            digitalSignature = digitalSignature,
            baseAddress = baseAddress,
            sizeOfHeaders = sizeOfHeaders,
            sizeOfImage = sizeOfImage,
            sizeOfImageHeader = sizeOfImageHeader,
            timeDateStamp = timeDateStamp,
            certificateAddress = certificateAddress,
            numberOfSections = numberOfSections,
            sectionHeadersAddress = sectionHeadersAddress,
            initFlags = initFlags,
            entryPoint = entryPoint,
            tlsAddress = tlsAddress,
            stackSize = stackSize,
            peHeapReserve = peHeapReserve,
            peHeapCommit = peHeapCommit,
            peBaseAddress = peBaseAddress,
            peSizeOfImage = peSizeOfImage,
            peChecksum = peChecksum,
            peTimeDateStamp = peTimeDateStamp,
            debugPathnameAddress = debugPathnameAddress,
            debugFilenameAddress = debugFilenameAddress,
            debugUnicodeFilenameAddress = debugUnicodeFilenameAddress,
            kernelImageThunkAddress = kernelImageThunkAddress,
            nonKernelImportDirectoryAddress = nonKernelImportDirectoryAddress,
            numberOfLibraryVersions = numberOfLibraryVersions,
            libraryVersionsAddress = libraryVersionsAddress,
            kernelLibraryVersionAddress = kernelLibraryVersionAddress,
            xapiLibraryVersionAddress = xapiLibraryVersionAddress,
            logoBitmapAddress = logoBitmapAddress,
            logoBitmapSize = logoBitmapSize,
            debugPathname = debugPathname,
            debugFilename = debugFilename
        )
    }

    /* ===============================
       Certificate Parsing
       =============================== */

    private fun parseCertificate(buffer: ByteBuffer, offset: Int): XbeCertificate {
        buffer.position(offset)
        
        val size = buffer.int
        val timeDateStamp = buffer.int
        val titleId = buffer.int
        
        // Title name (40 Unicode characters)
        val titleNameBytes = ByteArray(80)
        buffer.get(titleNameBytes)
        val titleName = decodeUnicode(titleNameBytes).trimEnd { it == 0.toChar() }
        
        // Alternate title IDs (16 entries)
        val alternateTitleIds = IntArray(16)
        for (i in 0 until 16) {
            alternateTitleIds[i] = buffer.int
        }
        
        val allowedMedia = buffer.int
        val gameRegion = buffer.int
        val gameRatings = buffer.int
        val diskNumber = buffer.int
        val version = buffer.int
        
        // LAN key (16 bytes)
        val lanKey = ByteArray(16)
        buffer.get(lanKey)
        
        // Signature key (16 bytes)
        val signatureKey = ByteArray(16)
        buffer.get(signatureKey)
        
        // Alternate signature keys (16 entries, 16 bytes each)
        val alternateSignatureKeys = Array(16) { ByteArray(16) }
        for (i in 0 until 16) {
            buffer.get(alternateSignatureKeys[i])
        }
        
        val originalCertificateSize = buffer.int
        val onlineServiceId = buffer.int
        
        // Skip reserved area (28 bytes)
        buffer.position(buffer.position() + 28)
        
        val securityFlags = buffer.int
        
        // Execution ID (20 bytes)
        val executionId = ByteArray(20)
        buffer.get(executionId)
        
        return XbeCertificate(
            size = size,
            timeDateStamp = timeDateStamp,
            titleId = titleId,
            titleName = titleName,
            alternateTitleIds = alternateTitleIds,
            allowedMedia = allowedMedia,
            gameRegion = gameRegion,
            gameRatings = gameRatings,
            diskNumber = diskNumber,
            version = version,
            lanKey = lanKey,
            signatureKey = signatureKey,
            alternateSignatureKeys = alternateSignatureKeys,
            originalCertificateSize = originalCertificateSize,
            onlineServiceId = onlineServiceId,
            securityFlags = securityFlags,
            executionId = executionId
        )
    }

    /* ===============================
       Section Parsing
       =============================== */

    private fun parseSections(buffer: ByteBuffer, header: XbeHeader): List<XbeSection> {
        val sections = mutableListOf<XbeSection>()
        
        val sectionOffset = header.sectionHeadersAddress - header.baseAddress
        
        for (i in 0 until header.numberOfSections) {
            buffer.position(sectionOffset + (i * 56)) // 56 bytes per section header
            
            val flags = buffer.int
            val virtualAddress = buffer.int
            val virtualSize = buffer.int
            val rawAddress = buffer.int
            val rawSize = buffer.int
            val sectionNameAddress = buffer.int
            val sectionNameReferenceCount = buffer.int
            val headSharedPageReferenceCountAddress = buffer.int
            val tailSharedPageReferenceCountAddress = buffer.int
            
            // Section digest (20 bytes)
            val sectionDigest = ByteArray(20)
            buffer.get(sectionDigest)
            
            // Read section name
            val sectionName = if (sectionNameAddress != 0) {
                readCString(buffer, sectionNameAddress - header.baseAddress)
            } else "UNKNOWN_$i"
            
            // Read section data
            val data = if (rawSize > 0 && rawAddress > 0) {
                val savedPosition = buffer.position()
                buffer.position(rawAddress)
                val sectionData = ByteArray(rawSize)
                buffer.get(sectionData)
                buffer.position(savedPosition)
                sectionData
            } else {
                ByteArray(0)
            }
            
            val sectionHeader = XbeSectionHeader(
                flags = flags,
                virtualAddress = virtualAddress,
                virtualSize = virtualSize,
                rawAddress = rawAddress,
                rawSize = rawSize,
                sectionNameAddress = sectionNameAddress,
                sectionNameReferenceCount = sectionNameReferenceCount,
                headSharedPageReferenceCountAddress = headSharedPageReferenceCountAddress,
                tailSharedPageReferenceCountAddress = tailSharedPageReferenceCountAddress,
                sectionDigest = sectionDigest,
                sectionName = sectionName
            )
            
            sections.add(XbeSection(sectionHeader, data))
            
            Log.d(TAG, "Section $i: $sectionName (VA: 0x${virtualAddress.toString(16)}, Size: 0x${rawSize.toString(16)}, Flags: 0x${flags.toString(16)})")
        }
        
        return sections
    }

    /* ===============================
       Library Versions Parsing
       =============================== */

    private fun parseLibraryVersions(buffer: ByteBuffer, header: XbeHeader): List<XbeLibraryVersion> {
        val versions = mutableListOf<XbeLibraryVersion>()
        
        if (header.numberOfLibraryVersions == 0 || header.libraryVersionsAddress == 0) {
            return versions
        }
        
        val offset = header.libraryVersionsAddress - header.baseAddress
        
        for (i in 0 until header.numberOfLibraryVersions) {
            buffer.position(offset + (i * 36)) // 36 bytes per library version
            
            // Library name (8 bytes)
            val nameBytes = ByteArray(8)
            buffer.get(nameBytes)
            val libraryName = String(nameBytes).trimEnd { it == 0.toChar() }
            
            val majorVersion = buffer.getShort().toInt() and 0xFFFF
            val minorVersion = buffer.getShort().toInt() and 0xFFFF
            val buildVersion = buffer.getShort().toInt() and 0xFFFF
             buffer.getShort() // Skip padding
            
            val flags = buffer.int
            
            versions.add(XbeLibraryVersion(
                libraryName = libraryName,
                majorVersion = majorVersion,
                minorVersion = minorVersion,
                buildVersion = buildVersion,
                flags = flags
            ))
            
            Log.d(TAG, "Library: $libraryName v$majorVersion.$minorVersion.$buildVersion (Flags: 0x${flags.toString(16)})")
        }
        
        return versions
    }

    /* ===============================
       Import Parsing
       =============================== */

    private fun parseImports(buffer: ByteBuffer, header: XbeHeader): Pair<List<ImportEntry>, List<ImportEntry>> {
        val kernelImports = mutableListOf<ImportEntry>()
        val userImports = mutableListOf<ImportEntry>()
        
        // Parse kernel imports
        if (header.kernelImageThunkAddress != 0) {
            parseImportTable(buffer, header, header.kernelImageThunkAddress, kernelImports, true)
        }
        
        // Parse user imports
        if (header.nonKernelImportDirectoryAddress != 0) {
            parseImportTable(buffer, header, header.nonKernelImportDirectoryAddress, userImports, false)
        }
        
        return Pair(kernelImports, userImports)
    }
    
    private fun parseImportTable(
        buffer: ByteBuffer,
        header: XbeHeader,
        tableAddress: Int,
        imports: MutableList<ImportEntry>,
        isKernel: Boolean
    ) {
        var offset = tableAddress - header.baseAddress
        
        while (true) {
            buffer.position(offset)
            val thunk = buffer.int
            
            if (thunk == 0) break // End of table
            
            val isOrdinal = (thunk and 0x80000000.toInt()) != 0
            val address = thunk and 0x7FFFFFFF
            
            if (isOrdinal) {
                // Import by ordinal
                val ordinal = address
                imports.add(ImportEntry(
                    functionName = "Ordinal_$ordinal",
                    ordinal = ordinal,
                    address = 0,
                    thunkAddress = thunk
                ))
            } else {
                // Import by name
                val nameOffset = address - header.baseAddress
                val savedPosition = buffer.position()
                buffer.position(nameOffset)
                
                val hint = buffer.short.toInt() and 0xFFFF
                val name = readCString(buffer, buffer.position())
                buffer.position(savedPosition)
                
                imports.add(ImportEntry(
                    functionName = name,
                    ordinal = hint,
                    address = address,
                    thunkAddress = thunk
                ))
            }
            
            offset += 4
        }
    }

    /* ===============================
       TLS Data Parsing
       =============================== */

    private fun parseTLSData(buffer: ByteBuffer, header: XbeHeader): ByteArray? {
        if (header.tlsAddress == 0) return null
        
        val tlsOffset = header.tlsAddress - header.baseAddress
        val savedPosition = buffer.position()
        buffer.position(tlsOffset)
        
        val startAddress = buffer.int
        val endAddress = buffer.int
        val indexAddress = buffer.int
        val callbackAddress = buffer.int
        val zeroFillSize = buffer.int
        val characteristics = buffer.int
        
        val dataSize = endAddress - startAddress
        if (dataSize <= 0) {
            buffer.position(savedPosition)
            return null
        }
        
        // Read TLS data
        buffer.position(startAddress - header.baseAddress)
        val tlsData = ByteArray(dataSize)
        buffer.get(tlsData)
        buffer.position(savedPosition)
        
        Log.d(TAG, "TLS Data found: ${tlsData.size} bytes at 0x${startAddress.toString(16)}")
        return tlsData
    }

    /* ===============================
       Logo Bitmap Parsing
       =============================== */

    private fun parseLogoBitmap(buffer: ByteBuffer, header: XbeHeader): ByteArray? {
        if (header.logoBitmapAddress == 0 || header.logoBitmapSize == 0) {
            Log.d(TAG, "No logo bitmap found in XBE")
            return null
        }
        
        val savedPosition = buffer.position()
        buffer.position(header.logoBitmapAddress - header.baseAddress)
        
        // XBE logo is typically 100x42 ARGB
        val expectedSize = XBE_LOGO_SIZE
        val actualSize = min(header.logoBitmapSize, expectedSize)
        
        val bitmap = ByteArray(actualSize)
        buffer.get(bitmap)
        buffer.position(savedPosition)
        
        Log.d(TAG, "Logo bitmap found: ${bitmap.size} bytes")
        return bitmap
    }

    /* ===============================
       Debug Info Parsing
       =============================== */

    private fun parseDebugInfo(buffer: ByteBuffer, header: XbeHeader): DebugInfo? {
        if (header.debugPathnameAddress == 0 && header.debugFilenameAddress == 0) {
            return null
        }
        
        val pathname = if (header.debugPathnameAddress != 0) {
            readCString(buffer, header.debugPathnameAddress - header.baseAddress)
        } else ""
        
        val filename = if (header.debugFilenameAddress != 0) {
            readCString(buffer, header.debugFilenameAddress - header.baseAddress)
        } else ""
        
        val unicodeFilename = if (header.debugUnicodeFilenameAddress != 0) {
            val offset = header.debugUnicodeFilenameAddress - header.baseAddress
            val savedPosition = buffer.position()
            buffer.position(offset)
            val unicodeBytes = ByteArray(520) // Max 260 Unicode chars
            buffer.get(unicodeBytes)
            buffer.position(savedPosition)
            decodeUnicode(unicodeBytes).trimEnd { it == 0.toChar() }
        } else null
        
        val timestamp = java.text.SimpleDateFormat("yyyy-MM-dd HH:mm:ss")
            .format(java.util.Date(header.timeDateStamp * 1000L))
        
        return DebugInfo(pathname, filename, unicodeFilename, timestamp)
    }

    /* ===============================
       Memory Mapping
       =============================== */

    private fun mapImageToMemory(image: XbeImage) {
        Log.d(TAG, "Mapping XBE image to memory...")
        
        // Clear memory
        memory.clearRAM()
        
        // Map each section
        for (section in image.sections) {
            val va = section.header.virtualAddress
            val data = section.data
            
            if (data.isNotEmpty()) {
                Log.d(TAG, "Mapping section ${section.header.sectionName} to 0x${va.toString(16)} (${data.size} bytes)")
                
                // Write section data
                for ((index, byte) in data.withIndex()) {
                    memory.write8(va + index, byte.toInt() and 0xFF)
                }
                
                // Zero-fill remaining virtual space
                for (i in data.size until section.header.virtualSize) {
                    memory.write8(va + i, 0)
                }
                
                // Mark section permissions
                markSectionPermissions(section)
            }
        }
        
        // Map certificate to memory (optional)
        mapCertificateToMemory(image.certificate, image.header.baseAddress)
        
        Log.d(TAG, "Image mapping complete")
    }

    private fun markSectionPermissions(section: XbeSection) {
        val flags = section.header.flags
        
        // In a real emulator, you would set page permissions here
        // For now, just log the permissions
        
        val permissions = StringBuilder()
        if (flags and XBE_SECTION_WRITABLE != 0) permissions.append("W")
        if (flags and XBE_SECTION_EXECUTABLE != 0) permissions.append("X")
        if (flags and XBE_SECTION_PRELOAD != 0) permissions.append("P")
        if (flags and XBE_SECTION_INSERTED != 0) permissions.append("I")
        
        Log.d(TAG, "Section ${section.header.sectionName} permissions: $permissions (0x${flags.toString(16)})")
    }

    private fun mapCertificateToMemory(cert: XbeCertificate, baseAddress: Int) {
        // Certificate is usually at offset 0x1000
        val certAddress = baseAddress + 0x1000
        
        // In a real emulator, you would write the certificate data here
        // For now, just mark the area as reserved
        
        Log.d(TAG, "Certificate mapped at 0x${certAddress.toString(16)}")
    }

    /* ===============================
       Import Setup
       =============================== */

    private fun setupImports(image: XbeImage) {
        Log.d(TAG, "Setting up imports...")
        
        // Setup kernel imports
        for (import in image.kernelImports) {
            val thunkAddress = import.thunkAddress and 0x7FFFFFFF
            
            // In a real emulator, you would resolve the import here
            // For now, we'll set up placeholder addresses
            
            memory.write32(thunkAddress, 0xDEADBEEF.toInt()) // Placeholder
            
            Log.v(TAG, "Kernel import: ${import.functionName} -> 0x${thunkAddress.toString(16)}")
        }
        
        // Setup user imports
        for (import in image.userImports) {
            val thunkAddress = import.thunkAddress and 0x7FFFFFFF
            
            memory.write32(thunkAddress, 0xF00DBABE.toInt()) // Placeholder
            
            Log.v(TAG, "User import: ${import.functionName} -> 0x${thunkAddress.toString(16)}")
        }
        
        Log.d(TAG, "Import setup complete: ${image.kernelImports.size} kernel, ${image.userImports.size} user imports")
    }

    /* ===============================
       TLS Setup
       =============================== */

    private fun setupTLS(image: XbeImage) {
        if (image.tlsData == null) return
        
        Log.d(TAG, "Setting up TLS data...")
        
        // Allocate TLS area
        val tlsSize = image.tlsData.size
        val tlsAddress = memory.allocate(tlsSize)
        
        // Copy TLS data
        for ((index, byte) in image.tlsData.withIndex()) {
            memory.write8(tlsAddress + index, byte.toInt() and 0xFF)
        }
        
        // Setup TLS index (simplified)
        memory.write32(0x003FF000, tlsAddress) // TLS index pointer
        
        Log.d(TAG, "TLS setup complete at 0x${tlsAddress.toString(16)}")
    }

    /* ===============================
       Utility Functions
       =============================== */

    private fun readCString(buffer: ByteBuffer, offset: Int): String {
        val savedPosition = buffer.position()
        buffer.position(offset)
        
        val bytes = mutableListOf<Byte>()
        while (true) {
            val b = buffer.get()
            if (b == 0.toByte()) break
            bytes.add(b)
        }
        
        buffer.position(savedPosition)
        return String(bytes.toByteArray(), Charsets.UTF_8)
    }

    private fun decodeUnicode(bytes: ByteArray): String {
        val chars = mutableListOf<Char>()
        var i = 0
        
        while (i < bytes.size - 1) {
            val ch = ((bytes[i + 1].toInt() and 0xFF) shl 8) or (bytes[i].toInt() and 0xFF)
            if (ch == 0) break
            chars.add(ch.toChar())
            i += 2
        }
        
        return String(chars.toCharArray())
    }

    private fun calculateDigest(data: ByteArray): ByteArray {
        return try {
            MessageDigest.getInstance("SHA-1").digest(data)
        } catch (e: Exception) {
            ByteArray(20) // Return zero digest on error
        }
    }

    /* ===============================
       Logo Bitmap Extraction
       =============================== */

    /**
     * استخراج اللوجو من XBE وتحويله إلى Bitmap
     */
    fun extractLogoBitmap(): Bitmap? {
        val image = loadedImage ?: return null
        val logoData = image.logoBitmap ?: return null
        
        return try {
            // XBE logo is typically 100x42 ARGB
            val width = XBE_LOGO_WIDTH
            val height = XBE_LOGO_HEIGHT
            
            // Create ARGB bitmap
            val pixels = IntArray(width * height)
            
            // Convert raw ARGB data to pixels
            for (i in 0 until min(pixels.size, logoData.size / 4)) {
                val offset = i * 4
                val a = logoData[offset + 3].toInt() and 0xFF
                val r = logoData[offset + 2].toInt() and 0xFF
                val g = logoData[offset + 1].toInt() and 0xFF
                val b = logoData[offset].toInt() and 0xFF
                
                pixels[i] = (a shl 24) or (r shl 16) or (g shl 8) or b
            }
            
            // Create bitmap
            Bitmap.createBitmap(pixels, width, height, Bitmap.Config.ARGB_8888)
            
        } catch (e: Exception) {
            Log.e(TAG, "Failed to create logo bitmap: ${e.message}")
            null
        }
    }

    /**
     * حفظ اللوجو إلى ملف
     */
    fun saveLogoToFile(file: File): Boolean {
        return try {
            val bitmap = extractLogoBitmap() ?: return false
            
            val stream = ByteArrayOutputStream()
            bitmap.compress(Bitmap.CompressFormat.PNG, 100, stream)
            
            file.writeBytes(stream.toByteArray())
            Log.d(TAG, "Logo saved to: ${file.absolutePath}")
            true
        } catch (e: Exception) {
            Log.e(TAG, "Failed to save logo: ${e.message}")
            false
        }
    }

    /**
     * الحصول على معلومات اللوجو
     */
    fun getLogoInfo(): Map<String, Any> {
        val image = loadedImage ?: return emptyMap()
        val logoData = image.logoBitmap ?: return emptyMap()
        
        return mapOf(
            "size" to logoData.size,
            "width" to XBE_LOGO_WIDTH,
            "height" to XBE_LOGO_HEIGHT,
            "format" to "ARGB",
            "has_logo" to true
        )
    }

    /* ===============================
       إضافة وظائف جديدة لاستخراج الملفات
       =============================== */

    /**
     * استخراج قسم معين من XBE
     */
    fun extractSection(sectionName: String): ByteArray? {
        val image = loadedImage ?: return null
        
        return image.sections.find { it.header.sectionName == sectionName }?.data
    }

    /**
     * استخراج جميع الأقسام
     */
    fun extractAllSections(): Map<String, ByteArray> {
        val image = loadedImage ?: return emptyMap()
        
        return image.sections.associate { it.header.sectionName to it.data }
    }

    /**
     * استخراج شهادة التوقيع
     */
    fun extractCertificate(): ByteArray? {
        val image = loadedImage ?: return null
        
        // Reconstruct certificate from parsed data
        val buffer = ByteBuffer.allocate(0x1000).order(ByteOrder.LITTLE_ENDIAN)
        
        val cert = image.certificate
        
        buffer.putInt(cert.size)
        buffer.putInt(cert.timeDateStamp)
        buffer.putInt(cert.titleId)
        
        // Title name
        val titleNameBytes = cert.titleName.toByteArray(Charsets.UTF_16LE)
        buffer.put(titleNameBytes)
        buffer.put(ByteArray(80 - titleNameBytes.size)) // Pad
        
        // Alternate title IDs
        for (id in cert.alternateTitleIds) {
            buffer.putInt(id)
        }
        
        buffer.putInt(cert.allowedMedia)
        buffer.putInt(cert.gameRegion)
        buffer.putInt(cert.gameRatings)
        buffer.putInt(cert.diskNumber)
        buffer.putInt(cert.version)
        
        buffer.put(cert.lanKey)
        buffer.put(cert.signatureKey)
        
        for (key in cert.alternateSignatureKeys) {
            buffer.put(key)
        }
        
        buffer.putInt(cert.originalCertificateSize)
        buffer.putInt(cert.onlineServiceId)
        
        buffer.put(ByteArray(28)) // Reserved
        
        buffer.putInt(cert.securityFlags)
        buffer.put(cert.executionId)
        
        return buffer.array()
    }

    /**
     * استخراج معلومات التصحيح
     */
    fun extractDebugInfo(): Map<String, String> {
        val image = loadedImage ?: return emptyMap()
        val debugInfo = image.debugInfo ?: return emptyMap()
        
        return mapOf(
            "pathname" to debugInfo.pathname,
            "filename" to debugInfo.filename,
            "unicode_filename" to (debugInfo.unicodeFilename ?: ""),
            "timestamp" to debugInfo.timestamp
        )
    }

    /* ===============================
       Public API
       =============================== */

    fun isLoaded(): Boolean = isLoaded

    fun getLoadedImage(): XbeImage? = loadedImage

    fun getEntryPoint(): Int = entryPoint

    fun getImageInfo(): Map<String, String> {
        val image = loadedImage ?: return emptyMap()
        
        val info = mutableMapOf<String, String>()
        
        info["Title"] = image.certificate.titleName
        info["Title ID"] = "0x${image.certificate.titleId.toString(16)}"
        info["Entry Point"] = "0x${image.header.entryPoint.toString(16)}"
        info["Image Size"] = "${image.header.sizeOfImage} bytes (0x${image.header.sizeOfImage.toString(16)})"
        info["Base Address"] = "0x${image.header.baseAddress.toString(16)}"
        info["Sections"] = image.sections.size.toString()
        info["Timestamp"] = image.debugInfo?.timestamp ?: "Unknown"
        info["Media"] = getMediaTypeString(image.certificate.allowedMedia)
        info["Region"] = getRegionString(image.certificate.gameRegion)
        info["Version"] = image.certificate.version.toString()
        info["Disk Number"] = image.certificate.diskNumber.toString()
        info["Security Flags"] = "0x${image.certificate.securityFlags.toString(16)}"
        info["Stack Size"] = "${image.header.stackSize} bytes"
        info["Has Logo"] = if (image.logoBitmap != null) "Yes (${image.logoBitmap.size} bytes)" else "No"
        info["Has TLS"] = if (image.tlsData != null) "Yes (${image.tlsData.size} bytes)" else "No"
        
        return info
    }

    private fun getMediaTypeString(media: Int): String {
        val types = mutableListOf<String>()
        
        if (media and XBE_MEDIA_TYPE_HD != 0) types.add("HD")
        if (media and XBE_MEDIA_TYPE_DVD_X2 != 0) types.add("DVD-X2")
        if (media and XBE_MEDIA_TYPE_DVD_CD != 0) types.add("DVD-CD")
        if (media and XBE_MEDIA_TYPE_CD != 0) types.add("CD")
        if (media and XBE_MEDIA_TYPE_DVD_5_RO != 0) types.add("DVD-5-RO")
        if (media and XBE_MEDIA_TYPE_DVD_9_RO != 0) types.add("DVD-9-RO")
        if (media and XBE_MEDIA_TYPE_DVD_5_RW != 0) types.add("DVD-5-RW")
        if (media and XBE_MEDIA_TYPE_DVD_9_RW != 0) types.add("DVD-9-RW")
        if (media and XBE_MEDIA_TYPE_DONGLE != 0) types.add("Dongle")
        if (media and XBE_MEDIA_TYPE_NONSECURE_HD != 0) types.add("NonSecure-HD")
        if (media and XBE_MEDIA_TYPE_NONSECURE_MODE != 0) types.add("NonSecure-Mode")
        
        return if (types.isEmpty()) "Unknown" else types.joinToString(", ")
    }

    private fun getRegionString(region: Int): String {
        val regions = mutableListOf<String>()
        
        if (region and XBE_REGION_NA != 0) regions.add("North America")
        if (region and XBE_REGION_JAPAN != 0) regions.add("Japan")
        if (region and XBE_REGION_RESTOFWORLD != 0) regions.add("Rest of World")
        if (region and XBE_REGION_MANUFACTURING != 0) regions.add("Manufacturing")
        
        return if (regions.isEmpty()) "Unknown" else regions.joinToString(", ")
    }

    fun unload() {
        loadedImage = null
        isLoaded = false
        entryPoint = 0
        Log.d(TAG, "XBE unloaded")
    }

    /* ===============================
       Validation
       =============================== */

    fun validateImage(): Boolean {
        val image = loadedImage ?: return false
        
        try {
            // Check magic
            if (image.header.magic != XBE_MAGIC) {
                Log.e(TAG, "Invalid XBE magic")
                return false
            }
            
            // Check entry point
            if (image.header.entryPoint == 0) {
                Log.e(TAG, "Invalid entry point")
                return false
            }
            
            // Check base address
            if (image.header.baseAddress == 0) {
                Log.e(TAG, "Invalid base address")
                return false
            }
            
            // Check section consistency
            for (section in image.sections) {
                if (section.header.virtualAddress == 0 && section.header.rawSize > 0) {
                    Log.e(TAG, "Section ${section.header.sectionName} has no virtual address")
                    return false
                }
                
                // Verify section size
                if (section.data.size != section.header.rawSize) {
                    Log.e(TAG, "Section ${section.header.sectionName} size mismatch")
                    return false
                }
            }
            
            // Check certificate
            if (image.certificate.size < 0x100) {
                Log.e(TAG, "Certificate too small")
                return false
            }
            
            return true
            
        } catch (e: Exception) {
            Log.e(TAG, "Validation failed: ${e.message}")
            return false
        }
    }

    /* ===============================
       Patch Support
       =============================== */

    fun applyPatch(address: Int, data: ByteArray) {
        if (!isLoaded) return
        
        for ((index, byte) in data.withIndex()) {
            memory.write8(address + index, byte.toInt() and 0xFF)
        }
        
        Log.d(TAG, "Applied patch at 0x${address.toString(16)}, size: ${data.size}")
    }

    fun applyStringPatch(address: Int, oldString: String, newString: String): Boolean {
        if (!isLoaded) return false
        
        // Read current string
        val current = StringBuilder()
        var offset = 0
        while (true) {
            val ch = memory.read8(address + offset).toChar()
            if (ch == '\u0000') break
            current.append(ch)
            offset++
        }
        
        if (current.toString() != oldString) {
            Log.w(TAG, "String mismatch at 0x${address.toString(16)}")
            return false
        }
        
        // Write new string
        for ((index, ch) in newString.withIndex()) {
            memory.write8(address + index, ch.code)
        }
        memory.write8(address + newString.length, 0)
        
        Log.d(TAG, "String patched at 0x${address.toString(16)}: '$oldString' -> '$newString'")
        return true
    }

    /* ===============================
       إضافة وظائف جديدة للمساعدة في التصحيح
       =============================== */

    /**
     * طباعة معلومات مفصلة عن XBE
     */
    fun dumpDetailedInfo(): String {
        val image = loadedImage ?: return "No image loaded"
        
        val sb = StringBuilder()
        sb.appendLine("=== XBE Detailed Information ===")
        sb.appendLine("Title: ${image.certificate.titleName}")
        sb.appendLine("Title ID: 0x${image.certificate.titleId.toString(16)}")
        sb.appendLine("Entry Point: 0x${image.header.entryPoint.toString(16)}")
        sb.appendLine("Base Address: 0x${image.header.baseAddress.toString(16)}")
        sb.appendLine("Image Size: ${image.header.sizeOfImage} bytes")
        sb.appendLine("Header Size: ${image.header.sizeOfHeaders} bytes")
        sb.appendLine("Timestamp: ${image.debugInfo?.timestamp ?: "Unknown"}")
        sb.appendLine("")
        
        sb.appendLine("=== Sections (${image.sections.size}) ===")
        for ((index, section) in image.sections.withIndex()) {
            sb.appendLine("  [$index] ${section.header.sectionName}")
            sb.appendLine("    Virtual Address: 0x${section.header.virtualAddress.toString(16)}")
            sb.appendLine("    Virtual Size: ${section.header.virtualSize} bytes")
            sb.appendLine("    Raw Size: ${section.header.rawSize} bytes")
            sb.appendLine("    Flags: 0x${section.header.flags.toString(16)}")
            sb.appendLine("")
        }
        
        sb.appendLine("=== Certificate ===")
        sb.appendLine("Size: ${image.certificate.size} bytes")
        sb.appendLine("Media: ${getMediaTypeString(image.certificate.allowedMedia)}")
        sb.appendLine("Region: ${getRegionString(image.certificate.gameRegion)}")
        sb.appendLine("Version: ${image.certificate.version}")
        sb.appendLine("Security Flags: 0x${image.certificate.securityFlags.toString(16)}")
        sb.appendLine("")
        
        sb.appendLine("=== Libraries (${image.libraryVersions.size}) ===")
        for (lib in image.libraryVersions) {
            sb.appendLine("  ${lib.libraryName}: v${lib.majorVersion}.${lib.minorVersion}.${lib.buildVersion}")
        }
        
        return sb.toString()
    }

    /**
     * الحصول على قائمة بأسماء الأقسام
     */
    fun getSectionNames(): List<String> {
        return loadedImage?.sections?.map { it.header.sectionName } ?: emptyList()
    }

    /**
     * البحث في البيانات الخام
     */
    fun searchInRawData(pattern: ByteArray): List<Int> {
        val image = loadedImage ?: return emptyList()
        
        val matches = mutableListOf<Int>()
        val data = image.rawData
        
        for (i in 0..data.size - pattern.size) {
            var match = true
            for (j in pattern.indices) {
                if (data[i + j] != pattern[j]) {
                    match = false
                    break
                }
            }
            if (match) {
                matches.add(i)
            }
        }
        
        return matches
    }
}
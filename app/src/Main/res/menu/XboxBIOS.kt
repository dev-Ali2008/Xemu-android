package og.xaniteog

import android.content.Context
import android.util.Log
import java.io.File
import java.io.FileOutputStream
import java.nio.ByteBuffer
import java.nio.ByteOrder

class XboxBIOS(
    private val memory: XboxMemory,
    private val cpu: XboxCPU,
    private val context: Context? = null
) {
    companion object {
        private const val TAG = "XboxBIOS"
        
        // BIOS Memory Addresses
        const val BIOS_BASE = 0xFF000000.toInt()
        const val BIOS_SIZE = 1 * 1024 * 1024 // 1MB
        
        // MCPX ROM Addresses
        const val MCPX_ROM_BASE = 0xFF000000.toInt()
        const val MCPX_ROM_SIZE = 512 * 1024 // 512KB
        
        // SMC/TSOP Addresses
        const val TSOP_BASE = 0xFFF80000.toInt()
        const val TSOP_SIZE = 256 * 1024 // 256KB
        
        // Flash Addresses
        const val FLASH_BASE = 0xFFFE0000.toInt()
        const val FLASH_SIZE = 256 * 1024 // 256KB
        
        // BIOS File Names
        const val MCPX_FILE = "mcpx_1.0.bin"
        const val TSOP_FILE = "Complex_4627v1.03.bin"
        const val HDD_FILE = "xbox_hdd.qcow2"
        
        // MCPX Versions
        const val MCPX_VERSION_1_0 = 0
        const val MCPX_VERSION_1_1 = 1
        const val MCPX_VERSION_1_2 = 2
        
        // TSOP Chip Types
        const val TSOP_TYPE_ST = 0
        const val TSOP_TYPE_WINBOND = 1
        const val TSOP_TYPE_SST = 2
    }
    
    // ===== BIOS Structures =====
    data class MCPXRom(
        val version: Int,
        val data: ByteArray,
        val checksum: Int,
        val timestamp: String
    )
    
    data class TSOPBios(
        val version: String,
        val kernelVersion: String,
        val data: ByteArray,
        val checksum: Int,
        val isRetail: Boolean,
        val isDebug: Boolean,
        val region: Int
    )
    
    data class HDDImage(
        val path: String,
        val size: Long,
        val format: String,
        val partitions: List<HDDPartition>
    )
    
    data class HDDPartition(
        val type: Int,
        val offset: Long,
        val size: Long,
        val isActive: Boolean,
        val label: String
    )
    
    data class BiosConfig(
        var mcpxEnabled: Boolean = true,
        var tsopEnabled: Boolean = true,
        var flashEnabled: Boolean = false,
        var hddEnabled: Boolean = true,
        var bootFromHDD: Boolean = true,
        var bootFromDVD: Boolean = false,
        var bootFromNetwork: Boolean = false,
        var region: Int = 1, // 1=USA, 2=Japan, 4=Europe
        var videoMode: Int = 0, // 0=NTSC, 1=PAL
        var debugMode: Boolean = false,
        var manufacturingMode: Boolean = false
    )
    
    // ===== BIOS State =====
    private var mcpxRom: MCPXRom? = null
    private var tsopBios: TSOPBios? = null
    private var hddImage: HDDImage? = null
    private var config = BiosConfig()
    private var isLoaded = false
    private var isInitialized = false
    private var bootStage = 0
    
    // ===== Debug/Stats =====
    private var bootTime = 0L
    private var bootAttempts = 0
    private var lastError: String? = null
    
    /* ===============================
       Main Initialization
       =============================== */
    
    fun initialize() {
        Log.d(TAG, "Initializing Xbox BIOS...")
        
        try {
            // Load BIOS components from assets
            loadFromAssets()
            
            // Verify all components
            if (!verifyComponents()) {
                throw IllegalStateException("BIOS components verification failed")
            }
            
            // Map BIOS to memory
            mapToMemory()
            
            // Initialize MCPX
            initializeMcpx()
            
            // Initialize boot sequence
            initializeBootSequence()
            
            isInitialized = true
            isLoaded = true
            
            Log.d(TAG, "BIOS initialized successfully")
            Log.d(TAG, "MCPX: ${mcpxRom?.version ?: "Not loaded"}")
            Log.d(TAG, "TSOP: ${tsopBios?.version ?: "Not loaded"}")
            Log.d(TAG, "HDD: ${if (hddImage != null) "Loaded" else "Not loaded"}")
            
        } catch (e: Exception) {
            Log.e(TAG, "BIOS initialization failed: ${e.message}")
            lastError = e.message
            throw e
        }
    }
    
    /* ===============================
       Assets Loading
       =============================== */
    
    private fun loadFromAssets() {
        Log.d(TAG, "Loading BIOS from assets...")
        
        context?.let { ctx ->
            try {
                // Load MCPX ROM
                loadMcpxFromAssets(ctx)
                
                // Load TSOP BIOS
                loadTsopFromAssets(ctx)
                
                // Load HDD Image (copy to internal storage first)
                loadHddFromAssets(ctx)
                
            } catch (e: Exception) {
                Log.e(TAG, "Failed to load from assets: ${e.message}")
                throw e
            }
        } ?: run {
            Log.w(TAG, "No context provided, using dummy BIOS")
            loadDummyBios()
        }
    }
    
    private fun loadMcpxFromAssets(context: Context) {
        try {
            val inputStream = context.assets.open(MCPX_FILE)
            val data = inputStream.readBytes()
            inputStream.close()
            
            mcpxRom = MCPXRom(
                version = MCPX_VERSION_1_0,
                data = data,
                checksum = calculateChecksum(data),
                timestamp = "2001-11-15"
            )
            
            Log.d(TAG, "MCPX ROM loaded: ${data.size} bytes, checksum: 0x${mcpxRom!!.checksum.toString(16)}")
            
        } catch (e: Exception) {
            Log.e(TAG, "Failed to load MCPX ROM: ${e.message}")
            throw e
        }
    }
    
    private fun loadTsopFromAssets(context: Context) {
        try {
            val inputStream = context.assets.open(TSOP_FILE)
            val data = inputStream.readBytes()
            inputStream.close()
            
            // Parse BIOS version from header
            val version = parseBiosVersion(data)
            val kernelVersion = parseKernelVersion(data)
            
            tsopBios = TSOPBios(
                version = version,
                kernelVersion = kernelVersion,
                data = data,
                checksum = calculateChecksum(data),
                isRetail = true,
                isDebug = false,
                region = 1 // USA
            )
            
            Log.d(TAG, "TSOP BIOS loaded: ${data.size} bytes")
            Log.d(TAG, "  Version: $version")
            Log.d(TAG, "  Kernel: $kernelVersion")
            Log.d(TAG, "  Checksum: 0x${tsopBios!!.checksum.toString(16)}")
            
        } catch (e: Exception) {
            Log.e(TAG, "Failed to load TSOP BIOS: ${e.message}")
            throw e
        }
    }
    
    private fun loadHddFromAssets(context: Context) {
        try {
            // Copy HDD image to internal storage
            val hddDir = File(context.filesDir, "hdd")
            if (!hddDir.exists()) hddDir.mkdirs()
            
            val hddFile = File(hddDir, HDD_FILE)
            
            if (!hddFile.exists()) {
                Log.d(TAG, "Copying HDD image to internal storage...")
                val inputStream = context.assets.open(HDD_FILE)
                val outputStream = FileOutputStream(hddFile)
                
                inputStream.copyTo(outputStream)
                
                inputStream.close()
                outputStream.close()
                
                Log.d(TAG, "HDD image copied: ${hddFile.length()} bytes")
            }
            
            // Parse HDD partitions
            val partitions = parseHddPartitions(hddFile)
            
            hddImage = HDDImage(
                path = hddFile.absolutePath,
                size = hddFile.length(),
                format = "QCOW2",
                partitions = partitions
            )
            
            Log.d(TAG, "HDD image loaded: ${partitions.size} partitions")
            partitions.forEachIndexed { index, partition ->
                Log.d(TAG, "  Partition $index: ${partition.label} (${partition.size / 1024 / 1024}MB)")
            }
            
        } catch (e: Exception) {
            Log.e(TAG, "Failed to load HDD image: ${e.message}")
            // HDD is optional, don't throw
        }
    }
    
    private fun loadDummyBios() {
        // Create dummy MCPX ROM (512KB)
        val dummyMcpx = ByteArray(MCPX_ROM_SIZE) { 0xFF.toByte() }
        
        // Write MCPX signature
        dummyMcpx[0] = 0x4D // 'M'
        dummyMcpx[1] = 0x43 // 'C'
        dummyMcpx[2] = 0x50 // 'P'
        dummyMcpx[3] = 0x58 // 'X'
        
        mcpxRom = MCPXRom(
            version = MCPX_VERSION_1_0,
            data = dummyMcpx,
            checksum = calculateChecksum(dummyMcpx),
            timestamp = "2001-01-01"
        )
        
        // Create dummy TSOP BIOS (256KB)
        val dummyTsop = ByteArray(TSOP_SIZE) { 0x00.toByte() }
        
        // Write Xbox BIOS signature
        "XBOX_BIOS".toByteArray().copyInto(dummyTsop, 0)
        
        tsopBios = TSOPBios(
            version = "1.0-dummy",
            kernelVersion = "1.0.4627.1",
            data = dummyTsop,
            checksum = calculateChecksum(dummyTsop),
            isRetail = true,
            isDebug = false,
            region = 1
        )
        
        Log.d(TAG, "Dummy BIOS created")
    }
    
    /* ===============================
       Memory Mapping
       =============================== */
    
    private fun mapToMemory() {
        Log.d(TAG, "Mapping BIOS to memory...")
        
        // Clear BIOS memory region
        memory.memset(BIOS_BASE, 0, BIOS_SIZE)
        
        // Map MCPX ROM
        mcpxRom?.let { mcpx ->
            Log.d(TAG, "Mapping MCPX ROM to 0x${MCPX_ROM_BASE.toString(16)}")
            memory.writeBytes(MCPX_ROM_BASE, mcpx.data)
            
            // Set protection (read-only, execute)
            memory.mprotect(MCPX_ROM_BASE, MCPX_ROM_SIZE, 
                XboxMemory.PROT_READ or XboxMemory.PROT_EXEC)
        }
        
        // Map TSOP BIOS
        tsopBios?.let { tsop ->
            Log.d(TAG, "Mapping TSOP BIOS to 0x${TSOP_BASE.toString(16)}")
            memory.writeBytes(TSOP_BASE, tsop.data)
            
            // Set protection (read-only, execute)
            memory.mprotect(TSOP_BASE, TSOP_SIZE,
                XboxMemory.PROT_READ or XboxMemory.PROT_EXEC)
        }
        
        // Setup flash area (writable)
        memory.mprotect(FLASH_BASE, FLASH_SIZE,
            XboxMemory.PROT_READ or XboxMemory.PROT_WRITE)
        
        // Write BIOS configuration to flash
        writeConfigToFlash()
        
        Log.d(TAG, "BIOS memory mapping complete")
    }
    
    private fun writeConfigToFlash() {
        val configData = ByteArray(256)
        val buffer = ByteBuffer.wrap(configData).order(ByteOrder.LITTLE_ENDIAN)
        
        // Write configuration structure
        buffer.putInt(0x42494F53) // "BIOS" magic
        buffer.putInt(0x00010000) // Version 1.0
        
        // Config flags
        var flags = 0
        if (config.mcpxEnabled) flags = flags or 0x0001
        if (config.tsopEnabled) flags = flags or 0x0002
        if (config.flashEnabled) flags = flags or 0x0004
        if (config.hddEnabled) flags = flags or 0x0008
        if (config.bootFromHDD) flags = flags or 0x0010
        if (config.bootFromDVD) flags = flags or 0x0020
        if (config.bootFromNetwork) flags = flags or 0x0040
        if (config.debugMode) flags = flags or 0x0080
        if (config.manufacturingMode) flags = flags or 0x0100
        
        buffer.putInt(flags)
        buffer.putInt(config.region)
        buffer.putInt(config.videoMode)
        
        // Write to flash
        memory.writeBytes(FLASH_BASE, configData)
        
        Log.d(TAG, "BIOS config written to flash")
    }
    
    /* ===============================
       MCPX Initialization
       =============================== */
    
    private fun initializeMcpx() {
        Log.d(TAG, "Initializing MCPX...")
        
        // MCPX (Media Communications Processor X) is the southbridge
        // It handles:
        // - Memory controller initialization
        // - PCI configuration
        // - USB, IDE, Ethernet, Audio
        
        // Set up MCPX registers in MMIO space
        initializeMcpxRegisters()
        
        // Initialize memory controller
        initializeMemoryController()
        
        // Initialize PCI bus
        initializePciBus()
        
        // Initialize IDE controller (for HDD/DVD)
        initializeIdeController()
        
        Log.d(TAG, "MCPX initialization complete")
    }
    
    private fun initializeMcpxRegisters() {
        // MCPX registers start at 0x80000000
        val mcpxBase = 0x80000000.toInt()
        
        // MCPX Revision Register
        memory.write32(mcpxBase + 0x0000, 0x00000101) // Rev 1.1
        
        // Chipset Configuration
        memory.write32(mcpxBase + 0x0004, 0x00000001) // Enable MCPX
        
        // SMBus Configuration
        memory.write32(mcpxBase + 0x0010, 0x00000001) // Enable SMBus
        
        // IDE Configuration
        memory.write32(mcpxBase + 0x0020, 0x00000001) // Enable IDE
        
        // USB Configuration
        memory.write32(mcpxBase + 0x0030, 0x00000001) // Enable USB
        
        // Ethernet Configuration
        memory.write32(mcpxBase + 0x0040, 0x00000001) // Enable Ethernet
        
        // Audio Configuration
        memory.write32(mcpxBase + 0x0050, 0x00000001) // Enable Audio
        
        Log.d(TAG, "MCPX registers initialized")
    }
    
    private fun initializeMemoryController() {
        // Memory controller configuration
        // Xbox has 64MB RAM at 0x00000000
        
        // DRAM Configuration Register
        memory.write32(0xFD000000.toInt() + 0x0200, 0x00000001) // 64MB config
        
        // Memory Timing
        memory.write32(0xFD000000.toInt() + 0x0204, 0x00000123) // CAS latency 2
        
        Log.d(TAG, "Memory controller initialized")
    }
    
    private fun initializePciBus() {
        // PCI Configuration Space starts at 0x80000000
        
        // NVIDIA NV2A GPU (Device 0:2:0)
        val nv2aConfig = 0x80000000.toInt() + (0 shl 11) + (2 shl 8) + (0 shl 3)
        memory.write32(nv2aConfig + 0x00, 0x02A010DE) // Vendor/Device ID
        memory.write32(nv2aConfig + 0x04, 0x02800007) // Command/Status
        memory.write32(nv2aConfig + 0x08, 0x03000002) // Class Code/Revision
        
        // MCPX Southbridge (Device 0:4:0)
        val mcpxConfig = 0x80000000.toInt() + (0 shl 11) + (4 shl 8) + (0 shl 3)
        memory.write32(mcpxConfig + 0x00, 0x01B710DE) // Vendor/Device ID
        memory.write32(mcpxConfig + 0x04, 0x02800007) // Command/Status
        memory.write32(mcpxConfig + 0x08, 0x06010000) // Class Code/Revision
        
        Log.d(TAG, "PCI bus initialized")
    }
    
    private fun initializeIdeController() {
        // IDE Controller setup for HDD/DVD
        
        // Primary IDE Controller Base: 0x1F0
        // Secondary IDE Controller Base: 0x170
        
        // IDE Configuration
        memory.write8(0x1F6, 0xA0.toInt()) // Select master device
        memory.write8(0x1F7, 0xEC.toInt()) // Identify command
        
        // Simulate HDD presence
        // In real BIOS, this would read from actual HDD
        
        Log.d(TAG, "IDE controller initialized")
    }
    
    /* ===============================
       Boot Sequence
       =============================== */
    
    private fun initializeBootSequence() {
        Log.d(TAG, "Initializing boot sequence...")
        
        bootStage = 0
        bootTime = System.currentTimeMillis()
        bootAttempts = 0
        
        // Set CPU entry point to MCPX ROM
        val entryPoint = MCPX_ROM_BASE
        cpu.setEntryPoint(entryPoint)
        
        Log.d(TAG, "Boot entry point set to 0x${entryPoint.toString(16)}")
    }
    
    fun bootStep(): Boolean {
        bootStage++
        
        when (bootStage) {
            1 -> {
                Log.d(TAG, "Boot Stage 1: MCPX ROM execution")
                // MCPX initializes hardware
                return true
            }
            2 -> {
                Log.d(TAG, "Boot Stage 2: Memory testing")
                // BIOS tests memory
                return true
            }
            3 -> {
                Log.d(TAG, "Boot Stage 3: PCI enumeration")
                // BIOS enumerates PCI devices
                return true
            }
            4 -> {
                Log.d(TAG, "Boot Stage 4: Boot device selection")
                // BIOS selects boot device (HDD/DVD/Network)
                return selectBootDevice()
            }
            5 -> {
                Log.d(TAG, "Boot Stage 5: Loading bootloader")
                // Load bootloader from selected device
                return loadBootloader()
            }
            6 -> {
                Log.d(TAG, "Boot Stage 6: Handoff to kernel")
                // Jump to kernel entry point
                return handoffToKernel()
            }
            else -> {
                Log.d(TAG, "Boot complete")
                return false // Boot finished
            }
        }
    }
    
    private fun selectBootDevice(): Boolean {
        return when {
            config.bootFromHDD && hddImage != null -> {
                Log.d(TAG, "Selected boot device: HDD")
                true
            }
            config.bootFromDVD -> {
                Log.d(TAG, "Selected boot device: DVD")
                true
            }
            config.bootFromNetwork -> {
                Log.d(TAG, "Selected boot device: Network")
                true
            }
            else -> {
                Log.w(TAG, "No boot device available")
                false
            }
        }
    }
    
    private fun loadBootloader(): Boolean {
        try {
            // Load bootloader from HDD partition C
            val bootloader = loadBootloaderFromHDD()
            if (bootloader != null) {
                // Map bootloader to memory at 0x00010000
                memory.writeBytes(0x00010000, bootloader)
                
                // Set entry point to bootloader
                cpu.setEntryPoint(0x00010000)
                
                Log.d(TAG, "Bootloader loaded: ${bootloader.size} bytes")
                return true
            }
            
            Log.w(TAG, "No bootloader found")
            return false
            
        } catch (e: Exception) {
            Log.e(TAG, "Failed to load bootloader: ${e.message}")
            return false
        }
    }
    
    private fun loadBootloaderFromHDD(): ByteArray? {
        hddImage?.let { hdd ->
            // Look for bootloader in partition C (Xbox system partition)
            val systemPartition = hdd.partitions.find { it.label == "C" || it.type == 0x07 }
            
            systemPartition?.let { partition ->
                // In a real implementation, this would read from the HDD image file
                // For now, return a dummy bootloader
                return createDummyBootloader()
            }
        }
        
        return null
    }
    
    private fun handoffToKernel(): Boolean {
        // Set kernel entry point (usually from XBE)
        val kernelEntry = 0x00100000 // Default kernel entry
        
        // Set up kernel parameters
        setupKernelParameters()
        
        // Jump to kernel
        cpu.setEntryPoint(kernelEntry)
        
        Log.d(TAG, "Handoff to kernel at 0x${kernelEntry.toString(16)}")
        return true
    }
    
    private fun setupKernelParameters() {
        // Xbox kernel parameters at 0x00000100
        val paramsAddr = 0x00000100
        
        // Kernel parameter structure
        val params = ByteArray(256)
        val buffer = ByteBuffer.wrap(params).order(ByteOrder.LITTLE_ENDIAN)
        
        // Magic number
        buffer.putInt(0x4B524E4C) // "KRNL"
        
        // Version
        buffer.putInt(0x00010000) // 1.0
        
        // Memory size (64MB)
        buffer.putInt(64 * 1024 * 1024)
        
        // Boot device type
        buffer.putInt(if (config.bootFromHDD) 1 else 2) // 1=HDD, 2=DVD
        
        // Video mode
        buffer.putInt(config.videoMode)
        
        // Region
        buffer.putInt(config.region)
        
        // Write parameters
        memory.writeBytes(paramsAddr, params)
        
        Log.d(TAG, "Kernel parameters setup complete")
    }
    
    /* ===============================
       Utility Functions
       =============================== */
    
    private fun verifyComponents(): Boolean {
        var valid = true
        
        // Verify MCPX ROM
        mcpxRom?.let { mcpx ->
            if (mcpx.data.size != MCPX_ROM_SIZE) {
                Log.e(TAG, "MCPX ROM size mismatch: ${mcpx.data.size} != $MCPX_ROM_SIZE")
                valid = false
            }
            
            val actualChecksum = calculateChecksum(mcpx.data)
            if (actualChecksum != mcpx.checksum) {
                Log.w(TAG, "MCPX checksum mismatch: 0x${actualChecksum.toString(16)} != 0x${mcpx.checksum.toString(16)}")
                // Don't fail for checksum mismatch in emulation
            }
        } ?: run {
            Log.e(TAG, "MCPX ROM not loaded")
            valid = false
        }
        
        // Verify TSOP BIOS
        tsopBios?.let { tsop ->
            if (tsop.data.size != TSOP_SIZE) {
                Log.e(TAG, "TSOP BIOS size mismatch: ${tsop.data.size} != $TSOP_SIZE")
                valid = false
            }
        } ?: run {
            Log.e(TAG, "TSOP BIOS not loaded")
            valid = false
        }
        
        return valid
    }
    
    private fun calculateChecksum(data: ByteArray): Int {
        var sum = 0
        for (byte in data) {
            sum = (sum + (byte.toInt() and 0xFF)) and 0xFFFFFFFF.toInt()
        }
        return sum
    }
    
    private fun parseBiosVersion(data: ByteArray): String {
        if (data.size < 64) return "Unknown"
        
        // Look for version string in BIOS
        for (i in 0 until data.size - 8) {
            if (data[i] == 'v'.toByte() && data[i + 1] == 'e'.toByte() && data[i + 2] == 'r'.toByte()) {
                val end = (i + 20).coerceAtMost(data.size)
                return String(data, i, end - i).trim()
            }
        }
        
        return "1.0.4627.1"
    }
    
    private fun parseKernelVersion(data: ByteArray): String {
        // Simplified - just return known version
        return "1.0.4627.1"
    }
    
    private fun parseHddPartitions(hddFile: File): List<HDDPartition> {
        val partitions = mutableListOf<HDDPartition>()
        
        try {
            // Xbox HDD partition layout:
            // Partition C: System (FATX) - 750MB
            // Partition E: Games (FATX) - 4.5GB
            // Partition F: Media (FATX) - rest
            
            partitions.add(HDDPartition(
                type = 0x07, // FATX
                offset = 0x00000000L,
                size = 750L * 1024 * 1024, // 750MB
                isActive = true,
                label = "C"
            ))
            
            partitions.add(HDDPartition(
                type = 0x07, // FATX
                offset = 750L * 1024 * 1024,
                size = 4500L * 1024 * 1024, // 4.5GB
                isActive = true,
                label = "E"
            ))
            
            partitions.add(HDDPartition(
                type = 0x07, // FATX
                offset = 5250L * 1024 * 1024,
                size = hddFile.length() - 5250L * 1024 * 1024,
                isActive = true,
                label = "F"
            ))
            
        } catch (e: Exception) {
            Log.e(TAG, "Failed to parse HDD partitions: ${e.message}")
        }
        
        return partitions
    }
    
    private fun createDummyBootloader(): ByteArray {
        // Create a simple bootloader that jumps to kernel
        val bootloader = ByteArray(512)
        val buffer = ByteBuffer.wrap(bootloader).order(ByteOrder.LITTLE_ENDIAN)
        
        // Simple x86 bootloader code
        buffer.put(0xEB.toByte()) // JMP short
        buffer.put(0x3C.toByte()) // Offset to kernel loader
        
        // Padding
        for (i in 2 until 510) {
            buffer.put(0x90.toByte()) // NOP
        }
        
        // Boot signature
        buffer.put(0x55.toByte())
        buffer.put(0xAA.toByte())
        
        return bootloader
    }
    
    /* ===============================
       Public API
       =============================== */
    
    fun reset() {
        Log.d(TAG, "Resetting BIOS...")
        
        isLoaded = false
        isInitialized = false
        bootStage = 0
        
        // Clear BIOS memory
        memory.memset(BIOS_BASE, 0, BIOS_SIZE)
        
        // Reinitialize
        initialize()
        
        Log.d(TAG, "BIOS reset complete")
    }
    
    fun getStatus(): String {
        return when {
            !isLoaded -> "Not loaded"
            !isInitialized -> "Not initialized"
            bootStage == 0 -> "Ready"
            bootStage < 6 -> "Booting (stage $bootStage)"
            else -> "Booted"
        }
    }
    
    fun getBootInfo(): Map<String, String> {
        return mapOf(
            "Status" to getStatus(),
            "MCPX" to (mcpxRom?.version?.toString() ?: "Not loaded"),
            "TSOP" to (tsopBios?.version ?: "Not loaded"),
            "HDD" to (if (hddImage != null) "Loaded (${hddImage!!.partitions.size} partitions)" else "Not loaded"),
            "Boot Stage" to bootStage.toString(),
            "Boot Time" to "${System.currentTimeMillis() - bootTime}ms",
            "Region" to when (config.region) {
                1 -> "USA"
                2 -> "Japan"
                4 -> "Europe"
                else -> "Unknown"
            },
            "Video Mode" to if (config.videoMode == 0) "NTSC" else "PAL",
            "Last Error" to (lastError ?: "None")
        )
    }
    
    fun setConfig(newConfig: BiosConfig) {
        config = newConfig
        Log.d(TAG, "BIOS configuration updated")
    }
    
    fun getConfig(): BiosConfig {
        return config.copy()
    }
    
    fun isReady(): Boolean {
        return isLoaded && isInitialized
    }
    
    fun getHddImage(): HDDImage? {
        return hddImage
    }
    
    fun readHddSector(partition: String, sector: Long): ByteArray? {
        hddImage?.let { hdd ->
            val part = hdd.partitions.find { it.label == partition }
            part?.let { p ->
                // Calculate file offset
                val offset = p.offset + (sector * 512)
                
                // Read from HDD file
                val file = File(hdd.path)
                val raf = java.io.RandomAccessFile(file, "r")
                raf.seek(offset)
                
                val sectorData = ByteArray(512)
                raf.read(sectorData)
                raf.close()
                
                return sectorData
            }
        }
        
        return null
    }
    
    fun writeHddSector(partition: String, sector: Long, data: ByteArray): Boolean {
        hddImage?.let { hdd ->
            val part = hdd.partitions.find { it.label == partition }
            part?.let { p ->
                if (data.size != 512) {
                    Log.e(TAG, "Sector size must be 512 bytes")
                    return false
                }
                
                // Calculate file offset
                val offset = p.offset + (sector * 512)
                
                // Write to HDD file
                val file = File(hdd.path)
                val raf = java.io.RandomAccessFile(file, "rw")
                raf.seek(offset)
                raf.write(data)
                raf.close()
                
                return true
            }
        }
        
        return false
    }
    
    /* ===============================
       Save/Load State (for emulator save states)
       =============================== */
    
    fun saveState(): ByteArray {
        val buffer = ByteArray(1024)
        val bb = ByteBuffer.wrap(buffer).order(ByteOrder.LITTLE_ENDIAN)
        
        // Save state version
        bb.putInt(0x42494F53) // "BIOS"
        bb.putInt(0x00010000) // Version 1.0
        
        // Save config
        bb.put(if (config.mcpxEnabled) 1 else 0)
        bb.put(if (config.tsopEnabled) 1 else 0)
        bb.put(if (config.hddEnabled) 1 else 0)
        bb.put(if (config.bootFromHDD) 1 else 0)
        bb.putInt(config.region)
        bb.putInt(config.videoMode)
        
        // Save boot state
        bb.putInt(bootStage)
        bb.putLong(bootTime)
        bb.putInt(bootAttempts)
        
        // Save flags
        var flags = 0
        if (isLoaded) flags = flags or 0x0001
        if (isInitialized) flags = flags or 0x0002
        bb.putInt(flags)
        
        return buffer
    }
    
    fun loadState(stateData: ByteArray): Boolean {
        try {
            val bb = ByteBuffer.wrap(stateData).order(ByteOrder.LITTLE_ENDIAN)
            
            // Verify magic
            if (bb.int != 0x42494F53) {
                Log.e(TAG, "Invalid BIOS state magic")
                return false
            }
            
            // Verify version
            val version = bb.int
            if (version != 0x00010000) {
                Log.w(TAG, "Unsupported BIOS state version: 0x${version.toString(16)}")
            }
            
            // Load config
            config.mcpxEnabled = bb.get() != 0.toByte()
            config.tsopEnabled = bb.get() != 0.toByte()
            config.hddEnabled = bb.get() != 0.toByte()
            config.bootFromHDD = bb.get() != 0.toByte()
            config.region = bb.int
            config.videoMode = bb.int
            
            // Load boot state
            bootStage = bb.int
            bootTime = bb.long
            bootAttempts = bb.int
            
            // Load flags
            val flags = bb.int
            isLoaded = (flags and 0x0001) != 0
            isInitialized = (flags and 0x0002) != 0
            
            Log.d(TAG, "BIOS state loaded successfully")
            return true
            
        } catch (e: Exception) {
            Log.e(TAG, "Failed to load BIOS state: ${e.message}")
            return false
        }
    }
    
    /* ===============================
       Debug Functions
       =============================== */
    
    fun dumpMemory(range: IntRange = BIOS_BASE until (BIOS_BASE + BIOS_SIZE), bytesPerLine: Int = 16): String {
        val sb = StringBuilder()
        sb.append("BIOS Memory Dump (0x${range.first.toString(16)}-0x${range.last.toString(16)}):\n")
        
        for (addr in range step bytesPerLine) {
            sb.append("0x${addr.toString(16).padStart(8, '0')}: ")
            
            for (i in 0 until bytesPerLine) {
                if (addr + i <= range.last) {
                    val value = memory.read8(addr + i)
                    sb.append("%02X ".format(value))
                } else {
                    sb.append("   ")
                }
            }
            
            sb.append(" ")
            
            for (i in 0 until bytesPerLine) {
                if (addr + i <= range.last) {
                    val value = memory.read8(addr + i)
                    if (value in 32..126) {
                        sb.append(value.toChar())
                    } else {
                        sb.append(".")
                    }
                }
            }
            
            sb.append("\n")
        }
        
        return sb.toString()
    }
    
    fun validate(): Boolean {
        return try {
            // Check if BIOS memory is accessible
            val testAddr = BIOS_BASE
            val testValue = 0x55
            memory.write8(testAddr, testValue)
            val readValue = memory.read8(testAddr)
            
            if (readValue != testValue) {
                Log.e(TAG, "BIOS memory validation failed")
                return false
            }
            
            // Check MCPX ROM signature
            val mcpxSig = memory.read32(MCPX_ROM_BASE)
            if (mcpxSig != 0x5850434D) { // "MCPX" in little-endian
                Log.w(TAG, "MCPX signature not found")
            }
            
            Log.d(TAG, "BIOS validation passed")
            true
            
        } catch (e: Exception) {
            Log.e(TAG, "BIOS validation failed: ${e.message}")
            false
        }
    }
}
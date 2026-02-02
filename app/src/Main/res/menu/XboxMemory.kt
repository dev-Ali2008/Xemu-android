package og.xaniteog

import android.util.Log
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.util.*
import kotlin.math.pow

class XboxMemory(
    val mmio: XboxMMIO,
) {

    companion object {
        private const val TAG = "XboxMemory"

        // Memory Layout Constants
        const val RAM_BASE = 0x00000000
        const val RAM_SIZE = 64 * 1024 * 1024  // 64MB

        const val VRAM_BASE = 0xF0000000.toInt()
        const val VRAM_SIZE = 64 * 1024 * 1024  // 64MB

        const val MMIO_BASE = 0xFD000000.toInt()
        const val MMIO_SIZE = 16 * 1024 * 1024  // 16MB

        const val BIOS_BASE = 0xFF000000.toInt()
        const val BIOS_SIZE = 1 * 1024 * 1024   // 1MB

        const val PCI_BASE = 0x80000000.toInt()
        const val PCI_SIZE = 128 * 1024 * 1024  // 128MB

        const val APU_BASE = 0xFE800000.toInt()
        const val APU_SIZE = 8 * 1024 * 1024    // 8MB

        // Page Table Constants
        const val PAGE_SIZE = 4096
        const val PAGE_MASK = PAGE_SIZE - 1
        const val PAGE_SHIFT = 12

        // Memory Protection Flags
        const val PROT_NONE = 0x00
        const val PROT_READ = 0x01
        const val PROT_WRITE = 0x02
        const val PROT_EXEC = 0x04
        const val PROT_ALL = PROT_READ or PROT_WRITE or PROT_EXEC

        // Memory Type Flags
        const val MEM_TYPE_RAM = 0x01
        const val MEM_TYPE_VRAM = 0x02
        const val MEM_TYPE_MMIO = 0x04
        const val MEM_TYPE_BIOS = 0x08
        const val MEM_TYPE_PCI = 0x10
        const val MEM_TYPE_APU = 0x20
        const val MEM_TYPE_UNMAPPED = 0x40
        const val MEM_TYPE_GUARD = 0x80

        // Cache Control Flags
        const val CACHE_WRITEBACK = 0x00
        const val CACHE_WRITETHROUGH = 0x01
        const val CACHE_UNCACHED = 0x02
        const val CACHE_WRITECOMBINE = 0x04

        // TLB Entry Flags
        const val TLB_VALID = 0x01
        const val TLB_DIRTY = 0x02
        const val TLB_ACCESSED = 0x04
        const val TLB_GLOBAL = 0x08
        const val TLB_USER = 0x10

        // ✅ FIX HERE
        private const val CACHE_SIZE = 16384
    }

    // ===== Memory Structures =====

    data class MemoryRegion(
        val name: String,
        val base: Int,
        val size: Int,
        val type: Int,
        var protection: Int,
        var cacheMode: Int,
        val buffer: ByteBuffer,
        var isMapped: Boolean = true,
        var isDirty: Boolean = false,
        var accessCount: Long = 0
    )

    data class PageTableEntry(
        val virtualPage: Int,
        var physicalPage: Int,
        var protection: Int,
        var type: Int,
        var cacheMode: Int,
        var accessed: Boolean,
        var dirty: Boolean,
        var valid: Boolean,
        var timestamp: Long
    )

    data class TLBEntry(
        var virtualAddress: Int,
        var physicalAddress: Int,
        var protection: Int,
        var type: Int,
        var cacheMode: Int,
        var accessed: Boolean,
        var dirty: Boolean,
        var valid: Boolean,
        var timestamp: Long,
        var lruCounter: Long
    )

    data class HeapBlock(
        val address: Int,
        var size: Int,
        var isFree: Boolean,
        var magic: Int = 0x48454150,
        var next: HeapBlock? = null,
        var prev: HeapBlock? = null
    )

    data class MemoryStatistics(
        var totalRamUsage: Long = 0,
        var totalVramUsage: Long = 0,
        var heapUsage: Long = 0,
        var heapFragmentation: Double = 0.0,
        var tlbHits: Long = 0,
        var tlbMisses: Long = 0,
        var pageFaults: Long = 0,
        var readCount: Long = 0,
        var writeCount: Long = 0,
        var cacheHits: Long = 0,
        var cacheMisses: Long = 0,
        var lastUpdateTime: Long = 0
    )

    // ===== Memory State =====

    private val ram = ByteBuffer.allocateDirect(RAM_SIZE).order(ByteOrder.LITTLE_ENDIAN)
    private val vram = ByteBuffer.allocateDirect(VRAM_SIZE).order(ByteOrder.LITTLE_ENDIAN)
    private val bios = ByteBuffer.allocateDirect(BIOS_SIZE).order(ByteOrder.LITTLE_ENDIAN)

    private val regions = mutableMapOf<Int, MemoryRegion>()
    private val regionList = mutableListOf<MemoryRegion>()

    private val pageTable = mutableMapOf<Int, PageTableEntry>()
    private val reversePageTable = mutableMapOf<Int, Int>()

    private val tlb = Array(128) { TLBEntry(0, 0, 0, 0, 0, false, false, false, 0L, 0L) }
    private var tlbPointer = 0
    private var tlbClock = 0L

    private var heapBase = 0x10000000
    private var heapSize = 32 * 1024 * 1024
    private var heapHead: HeapBlock? = null
    private val heapBlocks = mutableMapOf<Int, HeapBlock>()

    private val stats = MemoryStatistics()
    private var statsUpdateTime = System.currentTimeMillis()

    private val readCache = mutableMapOf<Int, Int>()
    private val writeCache = mutableMapOf<Int, Int>()

    private var mmioHandler: XboxMMIO? = null
    private val mmioRegions = mutableMapOf<IntRange, (Int, Int) -> Unit>()

    private val watchpoints = mutableMapOf<Int, (Int, Int, Boolean) -> Unit>()
    private val breakpoints = mutableSetOf<Int>()

    private val memoryMappings = mutableMapOf<Int, Int>()
    private val dirtyPages = BitSet()

    private val floatBuffer = ByteBuffer.allocate(4).order(ByteOrder.LITTLE_ENDIAN)
    private val doubleBuffer = ByteBuffer.allocate(8).order(ByteOrder.LITTLE_ENDIAN)

    // ===== Initialization =====
    
    init {
        Log.d(TAG, "Initializing Xbox Memory System...")
        
        // Initialize RAM with zeros
        clearRAM()
        
        // Initialize VRAM with zeros
        clearVRAM()
        
        // Initialize BIOS with zeros
        clearBIOS()
        
        // Setup memory regions
        setupMemoryRegions()
        
        // Initialize heap
        initializeHeap()
        
        // Initialize TLB
        initializeTLB()
        
        // Setup MMIO regions
        setupMMIORegions()
        
        Log.d(TAG, "Memory system initialized")
        Log.d(TAG, "RAM: ${RAM_SIZE / (1024 * 1024)}MB")
        Log.d(TAG, "VRAM: ${VRAM_SIZE / (1024 * 1024)}MB")
        Log.d(TAG, "BIOS: ${BIOS_SIZE / 1024}KB")
        Log.d(TAG, "Heap: ${heapSize / (1024 * 1024)}MB")
    }

    private fun setupMemoryRegions() {
        // RAM Region (0x00000000 - 0x03FFFFFF)
        val ramRegion = MemoryRegion(
            name = "RAM",
            base = RAM_BASE,
            size = RAM_SIZE,
            type = MEM_TYPE_RAM,
            protection = PROT_ALL,
            cacheMode = CACHE_WRITEBACK,
            buffer = ram
        )
        addRegion(ramRegion)
        
        // VRAM Region (0xF0000000 - 0xF3FFFFFF)
        val vramRegion = MemoryRegion(
            name = "VRAM",
            base = VRAM_BASE,
            size = VRAM_SIZE,
            type = MEM_TYPE_VRAM,
            protection = PROT_READ or PROT_WRITE,
            cacheMode = CACHE_WRITECOMBINE,
            buffer = vram
        )
        addRegion(vramRegion)
        
        // MMIO Region (0xFD000000 - 0xFDFFFFFF)
        val mmioRegion = MemoryRegion(
            name = "MMIO",
            base = MMIO_BASE,
            size = MMIO_SIZE,
            type = MEM_TYPE_MMIO,
            protection = PROT_READ or PROT_WRITE,
            cacheMode = CACHE_UNCACHED,
            buffer = ByteBuffer.allocateDirect(MMIO_SIZE).order(ByteOrder.LITTLE_ENDIAN)
        )
        addRegion(mmioRegion)
        
        // BIOS Region (0xFF000000 - 0xFF0FFFFF)
        val biosRegion = MemoryRegion(
            name = "BIOS",
            base = BIOS_BASE,
            size = BIOS_SIZE,
            type = MEM_TYPE_BIOS,
            protection = PROT_READ or PROT_EXEC,
            cacheMode = CACHE_WRITETHROUGH,
            buffer = bios
        )
        addRegion(biosRegion)
        
        // PCI Region (0x80000000 - 0x87FFFFFF)
        val pciRegion = MemoryRegion(
            name = "PCI",
            base = PCI_BASE,
            size = PCI_SIZE,
            type = MEM_TYPE_PCI,
            protection = PROT_READ or PROT_WRITE,
            cacheMode = CACHE_UNCACHED,
            buffer = ByteBuffer.allocateDirect(PCI_SIZE).order(ByteOrder.LITTLE_ENDIAN)
        )
        addRegion(pciRegion)
        
        // APU Region (0xFE800000 - 0xFEFFFFFF)
        val apuRegion = MemoryRegion(
            name = "APU",
            base = APU_BASE,
            size = APU_SIZE,
            type = MEM_TYPE_APU,
            protection = PROT_READ or PROT_WRITE,
            cacheMode = CACHE_UNCACHED,
            buffer = ByteBuffer.allocateDirect(APU_SIZE).order(ByteOrder.LITTLE_ENDIAN)
        )
        addRegion(apuRegion)
        
        Log.d(TAG, "Memory regions configured: ${regions.size} regions")
    }

    private fun addRegion(region: MemoryRegion) {
        regions[region.base] = region
        regionList.add(region)
        
        // Update page table for this region
        val pageCount = region.size / PAGE_SIZE
        for (i in 0 until pageCount) {
            val virtualPage = (region.base / PAGE_SIZE) + i
            val physicalPage = getPhysicalPage(region.base + (i * PAGE_SIZE))
            
            val entry = PageTableEntry(
                virtualPage = virtualPage,
                physicalPage = physicalPage,
                protection = region.protection,
                type = region.type,
                cacheMode = region.cacheMode,
                accessed = false,
                dirty = false,
                valid = true,
                timestamp = System.currentTimeMillis()
            )
            
            pageTable[virtualPage] = entry
            reversePageTable[physicalPage] = virtualPage
        }
    }

    private fun initializeHeap() {
        // Create initial heap block covering entire heap
        val initialBlock = HeapBlock(
            address = heapBase,
            size = heapSize,
            isFree = true
        )
        
        heapHead = initialBlock
        heapBlocks[heapBase] = initialBlock
        
        Log.d(TAG, "Heap initialized at 0x${heapBase.toString(16)} (${heapSize / (1024 * 1024)}MB)")
    }

    private fun initializeTLB() {
        for (i in tlb.indices) {
            tlb[i] = TLBEntry(
                virtualAddress = 0,
                physicalAddress = 0,
                protection = 0,
                type = 0,
                cacheMode = 0,
                accessed = false,
                dirty = false,
                valid = false,
                timestamp = 0L,
                lruCounter = 0L
            )
        }
        
        Log.d(TAG, "TLB initialized with ${tlb.size} entries")
    }

    private fun setupMMIORegions() {
        // NV2A GPU Registers (0xFD000000 - 0xFD0FFFFF)
        mmioRegions[0xFD000000.toInt()..0xFD0FFFFF.toInt()] = { addr, value ->
            mmioHandler?.write32(addr, value)
        }
        
        // APU Registers (0xFE800000 - 0xFE8FFFFF)
        mmioRegions[0xFE800000.toInt()..0xFE8FFFFF.toInt()] = { addr, value ->
            // APU register writes
            Log.v(TAG, "APU write: 0x${addr.toString(16)} = 0x${value.toString(16)}")
        }
        
        // PCI Configuration Space (0x80000000 - 0x80000FFF)
        mmioRegions[0x80000000.toInt()..0x80000FFF.toInt()] = { addr, value ->
            // PCI configuration writes
            Log.v(TAG, "PCI write: 0x${addr.toString(16)} = 0x${value.toString(16)}")
        }
        
        Log.d(TAG, "MMIO regions configured: ${mmioRegions.size} regions")
    }

    /* ===============================
       Memory Access Methods
       =============================== */

    fun read8(address: Int): Int {
        stats.readCount++
        
        // Check breakpoints
        if (breakpoints.contains(address)) {
            Log.w(TAG, "Breakpoint hit at 0x${address.toString(16)}")
        }
        
        // Check watchpoints
        watchpoints[address]?.invoke(address, 0, false)
        
        // Check cache first
        val cached = readCache[address]
        if (cached != null) {
            stats.cacheHits++
            return cached
        }
        
        stats.cacheMisses++
        
        // Check TLB
        val tlbEntry = lookupTLB(address)
        if (tlbEntry != null && tlbEntry.valid) {
            stats.tlbHits++
            
            if ((tlbEntry.protection and PROT_READ) == 0) {
                throw MemoryProtectionException("Read access violation at 0x${address.toString(16)}")
            }
            
            tlbEntry.accessed = true
            tlbEntry.timestamp = System.currentTimeMillis()
            tlbEntry.lruCounter = ++tlbClock
            
            val physicalAddr = tlbEntry.physicalAddress + (address and PAGE_MASK)
            val region = findRegion(physicalAddr)
            
            if (region == null) {
                throw MemoryAccessException("Unmapped memory read at 0x${address.toString(16)}")
            }
            
            region.accessCount++
            val value = region.buffer.get(physicalAddr - region.base).toInt() and 0xFF
            
            // Update cache
            updateReadCache(address, value)
            
            return value
        }
        
        // TLB miss - walk page table
        stats.tlbMisses++
        val pageEntry = walkPageTable(address)
        
        if (pageEntry == null || !pageEntry.valid) {
            stats.pageFaults++
            throw MemoryAccessException("Page fault at 0x${address.toString(16)}")
        }
        
        if ((pageEntry.protection and PROT_READ) == 0) {
            throw MemoryProtectionException("Read access violation at 0x${address.toString(16)}")
        }
        
        pageEntry.accessed = true
        pageEntry.timestamp = System.currentTimeMillis()
        
        // Update TLB
        updateTLB(address, pageEntry)
        
        val physicalAddr = getPhysicalAddress(address, pageEntry)
        val region = findRegion(physicalAddr)
        
        if (region == null) {
            throw MemoryAccessException("Unmapped memory read at 0x${address.toString(16)}")
        }
        
        region.accessCount++
        val value = region.buffer.get(physicalAddr - region.base).toInt() and 0xFF
        
        // Update cache
        updateReadCache(address, value)
        
        return value
    }

    fun read16(address: Int): Int {
        val low = read8(address)
        val high = read8(address + 1)
        return (high shl 8) or low
    }

    fun read32(address: Int): Int {
        // Check for MMIO access
        if (isMMIO(address)) {
            return mmioHandler?.read32(address) ?: 0
        }
        
        // Check alignment
        if (address and 0x3 != 0) {
            Log.w(TAG, "Unaligned 32-bit read at 0x${address.toString(16)}")
        }
        
        val b0 = read8(address)
        val b1 = read8(address + 1)
        val b2 = read8(address + 2)
        val b3 = read8(address + 3)
        
        return (b3 shl 24) or (b2 shl 16) or (b1 shl 8) or b0
    }

    fun read64(address: Int): Long {
        val low = read32(address).toLong() and 0xFFFFFFFFL
        val high = read32(address + 4).toLong() and 0xFFFFFFFFL
        return (high shl 32) or low
    }

    fun readFloat(address: Int): Float {
        val bits = read32(address)
        return Float.fromBits(bits)
    }

    fun readDouble(address: Int): Double {
        val bits = read64(address)
        return Double.fromBits(bits)
    }

    fun readBytes(address: Int, size: Int): ByteArray {
        val result = ByteArray(size)
        for (i in 0 until size) {
            result[i] = read8(address + i).toByte()
        }
        return result
    }

    fun write8(address: Int, value: Int) {
        val byteValue = value and 0xFF
        stats.writeCount++
        
        // Check breakpoints
        if (breakpoints.contains(address)) {
            Log.w(TAG, "Breakpoint hit at 0x${address.toString(16)}")
        }
        
        // Check watchpoints
        watchpoints[address]?.invoke(address, byteValue, true)
        
        // Check cache first
        writeCache[address] = byteValue
        
        // Check TLB
        val tlbEntry = lookupTLB(address)
        if (tlbEntry != null && tlbEntry.valid) {
            stats.tlbHits++
            
            if ((tlbEntry.protection and PROT_WRITE) == 0) {
                throw MemoryProtectionException("Write access violation at 0x${address.toString(16)}")
            }
            
            tlbEntry.accessed = true
            tlbEntry.dirty = true
            tlbEntry.timestamp = System.currentTimeMillis()
            tlbEntry.lruCounter = ++tlbClock
            
            val physicalAddr = tlbEntry.physicalAddress + (address and PAGE_MASK)
            val region = findRegion(physicalAddr)
            
            if (region == null) {
                throw MemoryAccessException("Unmapped memory write at 0x${address.toString(16)}")
            }
            
            region.accessCount++
            region.buffer.put(physicalAddr - region.base, byteValue.toByte())
            region.isDirty = true
            
            // Mark page as dirty
            val pageNumber = address / PAGE_SIZE
            dirtyPages.set(pageNumber, true)
            
            return
        }
        
        // TLB miss - walk page table
        stats.tlbMisses++
        val pageEntry = walkPageTable(address)
        
        if (pageEntry == null || !pageEntry.valid) {
            stats.pageFaults++
            throw MemoryAccessException("Page fault at 0x${address.toString(16)}")
        }
        
        if ((pageEntry.protection and PROT_WRITE) == 0) {
            throw MemoryProtectionException("Write access violation at 0x${address.toString(16)}")
        }
        
        pageEntry.accessed = true
        pageEntry.dirty = true
        pageEntry.timestamp = System.currentTimeMillis()
        
        // Update TLB
        updateTLB(address, pageEntry)
        
        val physicalAddr = getPhysicalAddress(address, pageEntry)
        val region = findRegion(physicalAddr)
        
        if (region == null) {
            throw MemoryAccessException("Unmapped memory write at 0x${address.toString(16)}")
        }
        
        region.accessCount++
        region.buffer.put(physicalAddr - region.base, byteValue.toByte())
        region.isDirty = true
        
        // Mark page as dirty
        val pageNumber = address / PAGE_SIZE
        dirtyPages.set(pageNumber, true)
    }

    fun write16(address: Int, value: Int) {
        val shortValue = value and 0xFFFF
        write8(address, shortValue and 0xFF)
        write8(address + 1, (shortValue shr 8) and 0xFF)
    }

    fun write32(address: Int, value: Int) {
        // Check for MMIO access
        if (isMMIO(address)) {
            mmioHandler?.write32(address, value)
            return
        }
        
        // Check alignment
        if (address and 0x3 != 0) {
            Log.w(TAG, "Unaligned 32-bit write at 0x${address.toString(16)}")
        }
        
        write8(address, value and 0xFF)
        write8(address + 1, (value shr 8) and 0xFF)
        write8(address + 2, (value shr 16) and 0xFF)
        write8(address + 3, (value shr 24) and 0xFF)
    }

    fun write64(address: Int, value: Long) {
        write32(address, value.toInt())
        write32(address + 4, (value shr 32).toInt())
    }

    fun writeFloat(address: Int, value: Float) {
        write32(address, value.toBits())
    }

    fun writeDouble(address: Int, value: Double) {
        write64(address, value.toBits())
    }

    fun writeBytes(address: Int, data: ByteArray) {
        for (i in data.indices) {
            write8(address + i, data[i].toInt() and 0xFF)
        }
    }

    fun writeString(address: Int, str: String, includeNull: Boolean = true) {
        for (i in str.indices) {
            write8(address + i, str[i].code)
        }
        if (includeNull) {
            write8(address + str.length, 0)
        }
    }

    fun readFloatFast(address: Int): Float {
        // طريقة أسرع لقراءة Float مباشرة من الـ buffer
        val region = findRegion(address)
        if (region != null && (region.protection and PROT_READ) != 0) {
            synchronized(floatBuffer) {
                floatBuffer.clear()
                for (i in 0 until 4) {
                    floatBuffer.put(region.buffer.get(address - region.base + i))
                }
                floatBuffer.flip()
                return floatBuffer.float
            }
        }
        return readFloat(address)
    }

    fun writeFloatFast(address: Int, value: Float) {
        // طريقة أسرع لكتابة Float مباشرة إلى الـ buffer
        val region = findRegion(address)
        if (region != null && (region.protection and PROT_WRITE) != 0) {
            synchronized(floatBuffer) {
                floatBuffer.clear()
                floatBuffer.putFloat(value)
                floatBuffer.flip()
                for (i in 0 until 4) {
                    region.buffer.put(address - region.base + i, floatBuffer.get())
                }
                region.isDirty = true
            }
            // Mark page as dirty
            val pageNumber = address / PAGE_SIZE
            dirtyPages.set(pageNumber, true)
        } else {
            writeFloat(address, value)
        }
    }

    fun readSigned32(address: Int): Int {
        return read32(address)
    }

    fun readSigned8(address: Int): Int {
        val value = read8(address)
        return if (value and 0x80 != 0) value or 0xFFFFFF00.toInt() else value
    }

    fun readSigned16(address: Int): Int {
        val value = read16(address)
        return if (value and 0x8000 != 0) value or 0xFFFF0000.toInt() else value
    }

    fun write32Fast(address: Int, value: Int) {
        // طريقة أسرع لكتابة 32-bit مباشرة إلى الـ buffer
        val region = findRegion(address)
        if (region != null && (region.protection and PROT_WRITE) != 0) {
            region.buffer.putInt(address - region.base, value)
            region.isDirty = true
            
            // Mark page as dirty
            val pageNumber = address / PAGE_SIZE
            dirtyPages.set(pageNumber, true)
        } else {
            write32(address, value)
        }
    }

    /* ===============================
       String Operations
       =============================== */

    fun readCString(address: Int): String {
        val sb = StringBuilder()
        var currentAddr = address
        
        while (true) {
            val ch = read8(currentAddr)
            if (ch == 0) break
            sb.append(ch.toChar())
            currentAddr++
        }
        
        return sb.toString()
    }

    fun readUnicodeString(address: Int): String {
        val sb = StringBuilder()
        var currentAddr = address
        
        while (true) {
            val ch = read16(currentAddr)
            if (ch == 0) break
            sb.append(ch.toChar())
            currentAddr += 2
        }
        
        return sb.toString()
    }

    fun readPString(address: Int): String {
        val length = read8(address)
        return readCString(address + 1).take(length)
    }

    /* ===============================
       Memory Management
       =============================== */

    fun allocate(size: Int): Int {
        if (size <= 0) {
            return 0
        }
        
        // Align size to 16 bytes
        val alignedSize = (size + 15) and 0xFFFFFFF0.toInt()
        
        var current = heapHead
        while (current != null) {
            if (current.isFree && current.size >= alignedSize) {
                return splitBlock(current, alignedSize)
            }
            current = current.next
        }
        
        // No suitable block found
        Log.w(TAG, "Heap allocation failed: size=0x${alignedSize.toString(16)}")
        return 0
    }

    private fun splitBlock(block: HeapBlock, size: Int): Int {
        if (block.size < size + 32) {
            // Block is just large enough, use it entirely
            block.isFree = false
            block.magic = 0x414C4C4F  // "ALLO"
            return block.address
        }
        
        // Split block
        val newBlock = HeapBlock(
            address = block.address + size,
            size = block.size - size,
            isFree = true,
            next = block.next,
            prev = block
        )
        
        block.size = size
        block.isFree = false
        block.magic = 0x414C4C4F  // "ALLO"
        block.next = newBlock
        
        newBlock.prev?.next = newBlock
        newBlock.next?.prev = newBlock
        
        heapBlocks[newBlock.address] = newBlock
        
        return block.address
    }

    fun free(address: Int): Boolean {
        val block = heapBlocks[address]
        if (block == null || block.isFree || block.magic != 0x414C4C4F) {
            Log.w(TAG, "Invalid free at 0x${address.toString(16)}")
            return false
        }
        
        block.isFree = true
        block.magic = 0x46524545  // "FREE"
        
        // Try to coalesce with next block
        block.next?.let { nextBlock ->
            if (nextBlock.isFree) {
                block.size += nextBlock.size
                block.next = nextBlock.next
                block.next?.prev = block
                heapBlocks.remove(nextBlock.address)
            }
        }
        
        // Try to coalesce with previous block
        block.prev?.let { prevBlock ->
            if (prevBlock.isFree) {
                prevBlock.size += block.size
                prevBlock.next = block.next
                prevBlock.next?.prev = prevBlock
                heapBlocks.remove(block.address)
            }
        }
        
        return true
    }

    fun reallocate(address: Int, newSize: Int): Int {
        if (address == 0) {
            return allocate(newSize)
        }
        
        val block = heapBlocks[address]
        if (block == null || block.isFree || block.magic != 0x414C4C4F) {
            Log.w(TAG, "Invalid realloc at 0x${address.toString(16)}")
            return 0
        }
        
        if (newSize <= block.size) {
            // Can shrink in place
            if (block.size - newSize >= 32) {
                splitBlock(block, newSize)
            }
            return address
        }
        
        // Need to allocate new block
        val newAddress = allocate(newSize)
        if (newAddress == 0) {
            return 0
        }
        
        // Copy data
        val oldData = readBytes(address, block.size)
        writeBytes(newAddress, oldData)
        
        // Free old block
        free(address)
        
        return newAddress
    }

    /* ===============================
       Memory Mapping
       =============================== */

    fun mmap(address: Int, size: Int, protection: Int, cacheMode: Int = CACHE_WRITEBACK): Int {
        // Align to page boundary
        val alignedAddress = address and 0xFFFFF000.toInt()
        val alignedSize = ((size + PAGE_SIZE - 1) and 0xFFFFF000.toInt())
        
        // Check if region is available
        for (region in regionList) {
            if (region.base <= alignedAddress && alignedAddress < region.base + region.size) {
                if (!region.isMapped) {
                    region.isMapped = true
                    region.protection = protection
                    region.cacheMode = cacheMode
                    
                    // Update page table
                    val pageCount = alignedSize / PAGE_SIZE
                    for (i in 0 until pageCount) {
                        val virtualPage = (alignedAddress / PAGE_SIZE) + i
                        val physicalPage = getPhysicalPage(alignedAddress + (i * PAGE_SIZE))
                        
                        val entry = PageTableEntry(
                            virtualPage = virtualPage,
                            physicalPage = physicalPage,
                            protection = protection,
                            type = region.type,
                            cacheMode = cacheMode,
                            accessed = false,
                            dirty = false,
                            valid = true,
                            timestamp = System.currentTimeMillis()
                        )
                        
                        pageTable[virtualPage] = entry
                        reversePageTable[physicalPage] = virtualPage
                        
                        // Invalidate TLB entries for this page
                        invalidateTLB(virtualPage * PAGE_SIZE)
                    }
                    
                    Log.d(TAG, "Mapped ${alignedSize / 1024}KB at 0x${alignedAddress.toString(16)}")
                    return alignedAddress
                }
                break
            }
        }
        
        Log.w(TAG, "mmap failed at 0x${alignedAddress.toString(16)}, size=0x${alignedSize.toString(16)}")
        return 0
    }

    fun munmap(address: Int, size: Int): Boolean {
        val alignedAddress = address and 0xFFFFF000.toInt()
        val alignedSize = ((size + PAGE_SIZE - 1) and 0xFFFFF000.toInt())
        
        val region = findRegion(alignedAddress)
        if (region == null || !region.isMapped) {
            return false
        }
        
        region.isMapped = false
        
        // Clear page table entries
        val pageCount = alignedSize / PAGE_SIZE
        for (i in 0 until pageCount) {
            val virtualPage = (alignedAddress / PAGE_SIZE) + i
            pageTable.remove(virtualPage)
            
            // Invalidate TLB entries
            invalidateTLB(virtualPage * PAGE_SIZE)
        }
        
        // Clear memory
        for (i in 0 until alignedSize step 4) {
            region.buffer.putInt((alignedAddress - region.base) + i, 0)
        }
        
        Log.d(TAG, "Unmapped ${alignedSize / 1024}KB at 0x${alignedAddress.toString(16)}")
        return true
    }

    fun mprotect(address: Int, size: Int, protection: Int): Boolean {
        val alignedAddress = address and 0xFFFFF000.toInt()
        val alignedSize = ((size + PAGE_SIZE - 1) and 0xFFFFF000.toInt())
        
        val region = findRegion(alignedAddress)
        if (region == null) {
            return false
        }
        
        region.protection = protection
        
        // Update page table entries
        val pageCount = alignedSize / PAGE_SIZE
        for (i in 0 until pageCount) {
            val virtualPage = (alignedAddress / PAGE_SIZE) + i
            val entry = pageTable[virtualPage]
            if (entry != null) {
                entry.protection = protection
                
                // Invalidate TLB entries for this page
                invalidateTLB(virtualPage * PAGE_SIZE)
            }
        }
        
        Log.d(TAG, "Changed protection to 0x${protection.toString(16)} for ${alignedSize / 1024}KB at 0x${alignedAddress.toString(16)}")
        return true
    }

    /* ===============================
       Cache Management
       =============================== */

    private fun updateReadCache(address: Int, value: Int) {
        if (readCache.size >= CACHE_SIZE) {
            // Remove oldest entry (simple FIFO)
            val oldestKey = readCache.keys.firstOrNull()
            if (oldestKey != null) {
                readCache.remove(oldestKey)
            }
        }
        readCache[address] = value
    }

    fun invalidateCache() {
        readCache.clear()
        writeCache.clear()
        Log.d(TAG, "Cache invalidated")
    }

    fun flushWriteCache() {
        for ((address, value) in writeCache) {
            // Actually write to memory
            val tlbEntry = lookupTLB(address)
            if (tlbEntry != null && tlbEntry.valid) {
                val physicalAddr = tlbEntry.physicalAddress + (address and PAGE_MASK)
                val region = findRegion(physicalAddr)
                region?.buffer?.put(physicalAddr - region.base, value.toByte())
            }
        }
        writeCache.clear()
        Log.d(TAG, "Write cache flushed")
    }

    fun clearCachesForCPU() {
        readCache.clear()
        for (i in tlb.indices) {
            tlb[i].valid = false
        }
    }

    /* ===============================
       TLB Management
       =============================== */

    private fun lookupTLB(virtualAddress: Int): TLBEntry? {
        val index = (virtualAddress shr PAGE_SHIFT) % tlb.size
        val entry = tlb[index]
        
        if (entry.valid && entry.virtualAddress == (virtualAddress and 0xFFFFF000.toInt())) {
            return entry
        }
        
        return null
    }

    private fun updateTLB(virtualAddress: Int, pageEntry: PageTableEntry) {
        val index = (virtualAddress shr PAGE_SHIFT) % tlb.size
        val entry = tlb[index]
        
        // If entry is valid and dirty, write back
        if (entry.valid && entry.dirty) {
            // Write back dirty page
            val physicalAddr = entry.physicalAddress
            val region = findRegion(physicalAddr)
            // Note: In a real system, would write back to page table
        }
        
        // Update TLB entry
        entry.virtualAddress = virtualAddress and 0xFFFFF000.toInt()
        entry.physicalAddress = pageEntry.physicalPage * PAGE_SIZE
        entry.protection = pageEntry.protection
        entry.type = pageEntry.type
        entry.cacheMode = pageEntry.cacheMode
        entry.accessed = pageEntry.accessed
        entry.dirty = pageEntry.dirty
        entry.valid = true
        entry.timestamp = System.currentTimeMillis()
        entry.lruCounter = ++tlbClock
    }

    fun invalidateTLB(virtualAddress: Int) {
        val page = virtualAddress and 0xFFFFF000.toInt()
        for (i in tlb.indices) {
            if (tlb[i].virtualAddress == page) {
                tlb[i].valid = false
            }
        }
    }

    fun invalidateTLBAll() {
        for (i in tlb.indices) {
            tlb[i].valid = false
        }
        Log.d(TAG, "TLB invalidated")
    }

    /* ===============================
       Page Table Management
       =============================== */

    private fun walkPageTable(virtualAddress: Int): PageTableEntry? {
        val virtualPage = virtualAddress / PAGE_SIZE
        return pageTable[virtualPage]
    }

    private fun getPhysicalAddress(virtualAddress: Int, pageEntry: PageTableEntry): Int {
        val pageOffset = virtualAddress and PAGE_MASK
        return (pageEntry.physicalPage * PAGE_SIZE) + pageOffset
    }

    private fun getPhysicalPage(address: Int): Int {
        // Simplified physical page calculation
        return address / PAGE_SIZE
    }

    fun getPageTableEntry(virtualAddress: Int): PageTableEntry? {
        return pageTable[virtualAddress / PAGE_SIZE]
    }

    fun setPageTableEntry(virtualAddress: Int, entry: PageTableEntry) {
        val virtualPage = virtualAddress / PAGE_SIZE
        pageTable[virtualPage] = entry
        reversePageTable[entry.physicalPage] = virtualPage
        
        // Invalidate TLB for this page
        invalidateTLB(virtualAddress)
    }

    fun translateAddress(virtualAddress: Int): Int {
        val tlbEntry = lookupTLB(virtualAddress)
        if (tlbEntry != null && tlbEntry.valid) {
            return tlbEntry.physicalAddress + (virtualAddress and PAGE_MASK)
        }
        
        val pageEntry = walkPageTable(virtualAddress)
        if (pageEntry != null && pageEntry.valid) {
            return getPhysicalAddress(virtualAddress, pageEntry)
        }
        
        throw MemoryAccessException("Cannot translate address 0x${virtualAddress.toString(16)}")
    }

    /* ===============================
       Region Management
       =============================== */

    private fun findRegion(address: Int): MemoryRegion? {
        // Check for exact region base first
        regions[address]?.let { return it }
        
        // Linear search through regions (small number of regions)
        for (region in regionList) {
            if (region.base <= address && address < region.base + region.size) {
                return region
            }
        }
        
        return null
    }

    fun getRegion(address: Int): MemoryRegion? {
        return findRegion(address)
    }

    fun getRegionByName(name: String): MemoryRegion? {
        return regionList.find { it.name == name }
    }

    fun getAllRegions(): List<MemoryRegion> {
        return regionList.toList()
    }

    fun isAddressInRegion(address: Int, regionName: String): Boolean {
        val region = getRegionByName(regionName)
        return region?.let { address in it.base until (it.base + it.size) } ?: false
    }

    /* ===============================
       MMIO Handling
       =============================== */

    fun isMMIO(address: Int): Boolean {
        for ((range, _) in mmioRegions) {
            if (address in range) {
                return true
            }
        }
        return false
    }

    fun setMMIOHandler(handler: XboxMMIO) {
        mmioHandler = handler
    }

    fun registerMMIOHandler(range: IntRange, handler: (Int, Int) -> Unit) {
        mmioRegions[range] = handler
        Log.d(TAG, "Registered MMIO handler for range 0x${range.first.toString(16)}-0x${range.last.toString(16)}")
    }

    /* ===============================
       Debugging & Monitoring
       =============================== */

    fun addWatchpoint(address: Int, callback: (Int, Int, Boolean) -> Unit) {
        watchpoints[address] = callback
        Log.d(TAG, "Watchpoint added at 0x${address.toString(16)}")
    }

    fun removeWatchpoint(address: Int) {
        watchpoints.remove(address)
        Log.d(TAG, "Watchpoint removed at 0x${address.toString(16)}")
    }

    fun addBreakpoint(address: Int) {
        breakpoints.add(address)
        Log.d(TAG, "Breakpoint added at 0x${address.toString(16)}")
    }

    fun removeBreakpoint(address: Int) {
        breakpoints.remove(address)
        Log.d(TAG, "Breakpoint removed at 0x${address.toString(16)}")
    }

    fun clearBreakpoints() {
        breakpoints.clear()
        Log.d(TAG, "All breakpoints cleared")
    }

    /* ===============================
       Memory Operations
       =============================== */

    fun memset(address: Int, value: Int, size: Int) {
        for (i in 0 until size) {
            write8(address + i, value)
        }
    }

    fun memcpy(dest: Int, src: Int, size: Int) {
        // Handle overlapping regions
        if (dest < src) {
            for (i in 0 until size) {
                val byte = read8(src + i)
                write8(dest + i, byte)
            }
        } else {
            for (i in size - 1 downTo 0) {
                val byte = read8(src + i)
                write8(dest + i, byte)
            }
        }
    }

    fun memmove(dest: Int, src: Int, size: Int) {
        memcpy(dest, src, size) // memcpy already handles overlap
    }

    fun memcmp(ptr1: Int, ptr2: Int, size: Int): Int {
        for (i in 0 until size) {
            val b1 = read8(ptr1 + i)
            val b2 = read8(ptr2 + i)
            if (b1 != b2) {
                return b1 - b2
            }
        }
        return 0
    }

    fun loadData(address: Int, data: ByteArray) {
        for (i in data.indices) {
            write8(address + i, data[i].toInt() and 0xFF)
        }
    }

    fun loadProgram(entryPoint: Int, code: ByteArray): Int {
        val codeSize = code.size
        val alignedSize = ((codeSize + 15) and 0xFFFFFFF0.toInt())
        
        // Allocate memory for code
        val codeAddress = allocate(alignedSize)
        if (codeAddress == 0) {
            Log.e(TAG, "Failed to allocate memory for program")
            return 0
        }
        
        // Load code
        loadData(codeAddress, code)
        
        // Set protection to read/execute
        mprotect(codeAddress, alignedSize, PROT_READ or PROT_EXEC)
        
        Log.d(TAG, "Program loaded at 0x${codeAddress.toString(16)}, entry point: 0x${entryPoint.toString(16)}")
        return codeAddress
    }

    /* ===============================
       Clear Operations
       =============================== */

    fun clearRAM() {
        for (i in 0 until RAM_SIZE step 4) {
            ram.putInt(i, 0)
        }
        ram.position(0)
        Log.d(TAG, "RAM cleared")
    }

    fun clearVRAM() {
        for (i in 0 until VRAM_SIZE step 4) {
            vram.putInt(i, 0)
        }
        vram.position(0)
        Log.d(TAG, "VRAM cleared")
    }

    fun clearBIOS() {
        for (i in 0 until BIOS_SIZE step 4) {
            bios.putInt(i, 0)
        }
        bios.position(0)
        Log.d(TAG, "BIOS cleared")
    }

    fun clearAll() {
        clearRAM()
        clearVRAM()
        clearBIOS()
        
        // Clear heap
        initializeHeap()
        
        // Clear page table
        pageTable.clear()
        reversePageTable.clear()
        
        // Clear TLB
        invalidateTLBAll()
        
        // Clear caches
        invalidateCache()
        
        // Clear watchpoints and breakpoints
        watchpoints.clear()
        breakpoints.clear()
        
        Log.d(TAG, "All memory cleared")
    }

    /* ===============================
       Statistics & Information
       =============================== */

    fun getStatistics(): MemoryStatistics {
        val currentTime = System.currentTimeMillis()
        
        // Update statistics periodically
        if (currentTime - statsUpdateTime > 1000) {
            updateStatistics()
            statsUpdateTime = currentTime
        }
        
        return stats.copy()
    }

    private fun updateStatistics() {
        // Calculate heap usage
        var usedHeap = 0
        var totalHeap = 0
        var freeBlocks = 0
        
        var current = heapHead
        while (current != null) {
            totalHeap += current.size
            if (!current.isFree) {
                usedHeap += current.size
            } else {
                freeBlocks++
            }
            current = current.next
        }
        
        stats.heapUsage = usedHeap.toLong()
        stats.heapFragmentation = if (freeBlocks > 0) {
            freeBlocks.toDouble() / heapBlocks.size
        } else {
            0.0
        }
        
        // Calculate RAM/VRAM usage (simplified)
        stats.totalRamUsage = RAM_SIZE.toLong()
        stats.totalVramUsage = 0
        
        stats.lastUpdateTime = System.currentTimeMillis()
    }

    fun getHeapInfo(): Map<String, String> {
        val info = mutableMapOf<String, String>()
        
        var totalBlocks = 0
        var usedBlocks = 0
        var freeBlocks = 0
        var usedMemory = 0
        var freeMemory = 0
        var largestFree = 0
        
        var current = heapHead
        while (current != null) {
            totalBlocks++
            
            if (current.isFree) {
                freeBlocks++
                freeMemory += current.size
                if (current.size > largestFree) {
                    largestFree = current.size
                }
            } else {
                usedBlocks++
                usedMemory += current.size
            }
            
            current = current.next
        }
        
        info["Total Blocks"] = totalBlocks.toString()
        info["Used Blocks"] = usedBlocks.toString()
        info["Free Blocks"] = freeBlocks.toString()
        info["Used Memory"] = "${usedMemory / 1024}KB"
        info["Free Memory"] = "${freeMemory / 1024}KB"
        info["Largest Free"] = "${largestFree / 1024}KB"
        info["Fragmentation"] = "%.2f%%".format((freeBlocks.toDouble() / totalBlocks) * 100)
        
        return info
    }

    fun getTLBInfo(): Map<String, String> {
        val info = mutableMapOf<String, String>()
        
        var validEntries = 0
        var dirtyEntries = 0
        var accessedEntries = 0
        
        for (entry in tlb) {
            if (entry.valid) {
                validEntries++
                if (entry.dirty) dirtyEntries++
                if (entry.accessed) accessedEntries++
            }
        }
        
        info["Total Entries"] = tlb.size.toString()
        info["Valid Entries"] = validEntries.toString()
        info["Dirty Entries"] = dirtyEntries.toString()
        info["Accessed Entries"] = accessedEntries.toString()
        info["Hit Rate"] = if (stats.tlbHits + stats.tlbMisses > 0) {
            "%.2f%%".format(stats.tlbHits.toDouble() / (stats.tlbHits + stats.tlbMisses) * 100)
        } else {
            "0%"
        }
        
        return info
    }

    fun getCacheInfo(): Map<String, String> {
        val info = mutableMapOf<String, String>()
        
        info["Read Cache Size"] = "${readCache.size}/$CACHE_SIZE"
        info["Write Cache Size"] = "${writeCache.size}/$CACHE_SIZE"
        info["Read Hit Rate"] = if (stats.cacheHits + stats.cacheMisses > 0) {
            "%.2f%%".format(stats.cacheHits.toDouble() / (stats.cacheHits + stats.cacheMisses) * 100)
        } else {
            "0%"
        }
        
        return info
    }

    fun dumpMemory(start: Int, size: Int): String {
        val sb = StringBuilder()
        sb.append("Memory dump at 0x${start.toString(16)} (${size} bytes):\n")
        
        for (i in 0 until size step 16) {
            sb.append("0x${(start + i).toString(16).padStart(8, '0')}: ")
            
            // Hex dump
            for (j in 0 until 16) {
                if (i + j < size) {
                    val value = read8(start + i + j)
                    sb.append("%02X ".format(value))
                } else {
                    sb.append("   ")
                }
            }
            
            sb.append(" ")
            
            // ASCII dump
            for (j in 0 until 16) {
                if (i + j < size) {
                    val value = read8(start + i + j)
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

    fun validateHeap(): Boolean {
        var current = heapHead
        var prev: HeapBlock? = null
        
        while (current != null) {
            // Check magic number
            if (!current.isFree && current.magic != 0x414C4C4F) {
                Log.e(TAG, "Heap corruption at block 0x${current.address.toString(16)}")
                return false
            }
            
            // Check pointers
            if (current.prev != prev) {
                Log.e(TAG, "Heap pointer corruption at block 0x${current.address.toString(16)}")
                return false
            }
            
            // Check block size
            if (current.size <= 0) {
                Log.e(TAG, "Invalid block size at 0x${current.address.toString(16)}")
                return false
            }
            
            prev = current
            current = current.next
        }
        
        return true
    }

    fun memoryCheck(address: Int, size: Int): Boolean {
        try {
            // Try to read and write to the memory region
            for (i in 0 until size step 4) {
                val testValue = 0xDEADBEEF.toInt()
                write32(address + i, testValue)
                val readValue = read32(address + i)
                if (readValue != testValue) {
                    Log.e(TAG, "Memory check failed at 0x${(address + i).toString(16)}")
                    return false
                }
            }
            return true
        } catch (e: Exception) {
            Log.e(TAG, "Memory check exception: ${e.message}")
            return false
        }
    }

    /* ===============================
       Direct Buffer Access (for performance)
       =============================== */

    fun getRAMBuffer(): ByteBuffer {
        ram.position(0)
        return ram.slice().order(ByteOrder.LITTLE_ENDIAN)
    }

    fun getVRAMBuffer(): ByteBuffer {
        vram.position(0)
        return vram.slice().order(ByteOrder.LITTLE_ENDIAN)
    }

    fun getBIOSBuffer(): ByteBuffer {
        bios.position(0)
        return bios.slice().order(ByteOrder.LITTLE_ENDIAN)
    }

    fun getDirectBuffer(address: Int, size: Int): ByteBuffer? {
        val region = findRegion(address)
        if (region == null || address + size > region.base + region.size) {
            return null
        }
        
        val buffer = region.buffer
        buffer.position(address - region.base)
        val slice = buffer.slice()
        slice.limit(size)
        return slice.order(ByteOrder.LITTLE_ENDIAN)
    }

    fun getFastBuffer(address: Int, size: Int): ByteBuffer? {
        return getDirectBuffer(address, size)
    }

    /* ===============================
       Exception Classes
       =============================== */

    class MemoryAccessException(message: String) : Exception(message)
    class MemoryProtectionException(message: String) : Exception(message)
    class HeapCorruptionException(message: String) : Exception(message)

    /* ===============================
       Shutdown
       =============================== */

    fun shutdown() {
        // Flush any pending writes
        flushWriteCache()
        
        // Clear all memory
        clearAll()
        
        // Clear data structures
        regions.clear()
        regionList.clear()
        pageTable.clear()
        reversePageTable.clear()
        heapBlocks.clear()
        mmioRegions.clear()
        watchpoints.clear()
        breakpoints.clear()
        memoryMappings.clear()
        
        heapHead = null
        
        Log.d(TAG, "Memory system shutdown complete")
    }

    /* ===============================
       Compatibility Methods for XboxCPU
       =============================== */

    fun read8Direct(address: Int): Int {
        val region = findRegion(address)
        return if (region != null && (region.protection and PROT_READ) != 0) {
            region.buffer.get(address - region.base).toInt() and 0xFF
        } else {
            read8(address)
        }
    }

    fun write8Direct(address: Int, value: Int) {
        val region = findRegion(address)
        if (region != null && (region.protection and PROT_WRITE) != 0) {
            region.buffer.put(address - region.base, value.toByte())
            region.isDirty = true
        } else {
            write8(address, value)
        }
    }

    fun read32Direct(address: Int): Int {
        val region = findRegion(address)
        return if (region != null && (region.protection and PROT_READ) != 0 && address and 0x3 == 0) {
            region.buffer.getInt(address - region.base)
        } else {
            read32(address)
        }
    }

    fun write32Direct(address: Int, value: Int) {
        val region = findRegion(address)
        if (region != null && (region.protection and PROT_WRITE) != 0 && address and 0x3 == 0) {
            region.buffer.putInt(address - region.base, value)
            region.isDirty = true
        } else {
            write32(address, value)
        }
    }

    fun bulkRead(start: Int, data: ByteArray, offset: Int, length: Int) {
        for (i in 0 until length) {
            data[offset + i] = read8(start + i).toByte()
        }
    }

    fun bulkWrite(start: Int, data: ByteArray, offset: Int, length: Int) {
        for (i in 0 until length) {
            write8(start + i, data[offset + i].toInt() and 0xFF)
        }
    }

    fun bulkCopy(src: Int, dest: Int, length: Int) {
        for (i in 0 until length step 4) {
            val remaining = length - i
            val chunk = if (remaining >= 4) 4 else remaining
            
            when (chunk) {
                4 -> {
                    val value = read32(src + i)
                    write32(dest + i, value)
                }
                else -> {
                    for (j in 0 until chunk) {
                        val value = read8(src + i + j)
                        write8(dest + i + j, value)
                    }
                }
            }
        }
    }

    fun readU64(address: Int): ULong {
        val low = read32(address).toULong()
        val high = read32(address + 4).toULong()
        return (high shl 32) or low
    }

    fun writeU64(address: Int, value: ULong) {
        write32(address, (value and 0xFFFFFFFFUL).toInt())
        write32(address + 4, ((value shr 32) and 0xFFFFFFFFUL).toInt())
    }

    fun isAddressValid(address: Int): Boolean {
        return try {
            // Try to read one byte
            read8(address)
            true
        } catch (e: Exception) {
            false
        }
    }

    fun getAddressRange(regionName: String): IntRange? {
        val region = getRegionByName(regionName)
        return region?.let { it.base until (it.base + it.size) }
    }

    fun dumpRegion(regionName: String, maxBytes: Int = 256): String {
        val region = getRegionByName(regionName)
        return if (region != null) {
            dumpMemory(region.base, minOf(region.size, maxBytes))
        } else {
            "Region '$regionName' not found"
        }
    }
}



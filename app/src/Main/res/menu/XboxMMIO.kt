package og.xaniteog

import android.util.Log
import java.nio.ByteBuffer
import java.nio.ByteOrder

class XboxMMIO(private var gpu: XboxGPU? = null) {

    companion object {
        private const val TAG = "XboxMMIO"
        
        // ===== Memory Mapped I/O Regions =====
        
        // NV2A (Graphics & Media) - 0xFD000000 - 0xFDFFFFFF
        const val NV2A_BASE = 0xFD000000.toInt()
        const val NV2A_SIZE = 0x01000000
        
        // APU (Audio) - 0xFE800000 - 0xFEFFFFFF
        const val APU_BASE = 0xFE800000.toInt()
        const val APU_SIZE = 0x00800000
        
        // PCI Configuration - 0x80000000 - 0x87FFFFFF
        const val PCI_BASE = 0x80000000.toInt()
        const val PCI_SIZE = 0x08000000
        
        // ACPI - 0xFEC00000 - 0xFEC00FFF
        const val ACPI_BASE = 0xFEC00000.toInt()
        const val ACPI_SIZE = 0x00001000
        
        // SMBus - 0xF2000000 - 0xF2000FFF
        const val SMBUS_BASE = 0xF2000000.toInt()
        const val SMBUS_SIZE = 0x00001000
        
        // IDE Controller - 0xF3000000 - 0xF3000FFF
        const val IDE_BASE = 0xF3000000.toInt()
        const val IDE_SIZE = 0x00001000
        
        // USB Controller - 0xF4000000 - 0xF4000FFF
        const val USB_BASE = 0xF4000000.toInt()
        const val USB_SIZE = 0x00001000
        
        // Ethernet (LAN) - 0xF5000000 - 0xF5000FFF
        const val LAN_BASE = 0xF5000000.toInt()
        const val LAN_SIZE = 0x00001000
        
        // Flash ROM - 0xFF000000 - 0xFF0FFFFF
        const val FLASH_BASE = 0xFF000000.toInt()
        const val FLASH_SIZE = 0x00100000
        
        // ===== NV2A Register Offsets =====
        
        // PFB (Frame Buffer)
        const val NV_PFB = 0x00100000
        const val NV_PFB_CSTATUS = NV_PFB + 0x00000100
        const val NV_PFB_CONFIG = NV_PFB + 0x00000200
        
        // PRMA (Memory Controller)
        const val NV_PRAM = 0x00180000
        const val NV_PRAM_DATA = NV_PRAM + 0x00000000
        
        // PCRTC (CRTC Controller)
        const val NV_PCRTC = 0x00600000
        const val NV_PCRTC_INTR_0 = NV_PCRTC + 0x00000100
        const val NV_PCRTC_INTR_EN_0 = NV_PCRTC + 0x00000140
        const val NV_PCRTC_START = NV_PCRTC + 0x00000800
        const val NV_PCRTC_RASTER = NV_PCRTC + 0x00000828
        
        // PRAMDAC (RAMDAC)
        const val NV_PRAMDAC = 0x00680000
        const val NV_PRAMDAC_GENERAL_CONTROL = NV_PRAMDAC + 0x00000600
        const val NV_PRAMDAC_VPLL_COEFF = NV_PRAMDAC + 0x00000508
        const val NV_PRAMDAC_PLL_COEFF_SELECT = NV_PRAMDAC + 0x0000050C
        
        // PFIFO (Command FIFO)
        const val NV_PFIFO = 0x00200000
        const val NV_PFIFO_CACHE1_PUSH = NV_PFIFO + 0x00000320
        const val NV_PFIFO_CACHE1_PULL = NV_PFIFO + 0x00000324
        const val NV_PFIFO_CACHE1_DMA_PUSH = NV_PFIFO + 0x00000328
        const val NV_PFIFO_CACHE1_DMA_PULL = NV_PFIFO + 0x0000032C
        const val NV_PFIFO_CACHE1_PUT = NV_PFIFO + 0x00000300
        const val NV_PFIFO_CACHE1_GET = NV_PFIFO + 0x00000304
        
        // PGRAPH (3D Graphics Engine)
        const val NV_PGRAPH = 0x00400000
        const val NV_PGRAPH_CTX_CONTROL = NV_PGRAPH + 0x00000100
        const val NV_PGRAPH_CTX_SWITCH = NV_PGRAPH + 0x00000104
        const val NV_PGRAPH_BUFFER_NOTIFY = NV_PGRAPH + 0x00000180
        const val NV_PGRAPH_INTR = NV_PGRAPH + 0x00000140
        const val NV_PGRAPH_INTR_EN = NV_PGRAPH + 0x00000144
        
        // PVIDEO (Video Decoder)
        const val NV_PVIDEO = 0x00620000
        const val NV_PVIDEO_INTR = NV_PVIDEO + 0x00000100
        const val NV_PVIDEO_INTR_EN = NV_PVIDEO + 0x00000104
        
        // PTIMER (Timer)
        const val NV_PTIMER = 0x00090000
        const val NV_PTIMER_INTR_0 = NV_PTIMER + 0x00000100
        const val NV_PTIMER_INTR_EN_0 = NV_PTIMER + 0x00000140
        const val NV_PTIMER_NUMERATOR = NV_PTIMER + 0x00000200
        const val NV_PTIMER_DENOMINATOR = NV_PTIMER + 0x00000210
        const val NV_PTIMER_TIME_0 = NV_PTIMER + 0x00000300
        
        // PMC (Microcontroller)
        const val NV_PMC = 0x00000000
        const val NV_PMC_BOOT_0 = NV_PMC + 0x00000000
        const val NV_PMC_INTR_0 = NV_PMC + 0x00000100
        const val NV_PMC_INTR_EN_0 = NV_PMC + 0x00000140
        
        // ===== APU Register Offsets =====
        
        // Audio Processor
        const val APU_CTRL = 0x00
        const val APU_STATUS = 0x04
        const val APU_INTR_CTRL = 0x08
        const val APU_INTR_STATUS = 0x0C
        const val APU_DMA_ADDR = 0x10
        const val APU_DMA_SIZE = 0x14
        const val APU_DMA_CTRL = 0x18
        const val APU_VOLUME = 0x1C
        const val APU_SAMPLE_RATE = 0x20
        const val APU_FORMAT = 0x24
        
        // ===== PCI Configuration Offsets =====
        
        // Vendor and Device IDs
        const val PCI_VENDOR_ID = 0x00
        const val PCI_DEVICE_ID = 0x02
        const val PCI_COMMAND = 0x04
        const val PCI_STATUS = 0x06
        const val PCI_REVISION = 0x08
        const val PCI_CLASS_CODE = 0x0B
        const val PCI_HEADER_TYPE = 0x0E
        const val PCI_BAR0 = 0x10
        const val PCI_BAR1 = 0x14
        const val PCI_BAR2 = 0x18
        const val PCI_BAR3 = 0x1C
        const val PCI_BAR4 = 0x20
        const val PCI_BAR5 = 0x24
        const val PCI_INTERRUPT_LINE = 0x3C
        const val PCI_INTERRUPT_PIN = 0x3D
        
        // ===== SMBus Register Offsets =====
        
        const val SMBUS_STATUS = 0x00
        const val SMBUS_CONTROL = 0x02
        const val SMBUS_ADDRESS = 0x04
        const val SMBUS_DATA = 0x06
        const val SMBUS_COMMAND = 0x08
        
        // ===== IDE Controller Offsets =====
        
        const val IDE_DATA = 0x00
        const val IDE_ERROR = 0x01
        const val IDE_SECTOR_COUNT = 0x02
        const val IDE_SECTOR_NUMBER = 0x03
        const val IDE_CYLINDER_LOW = 0x04
        const val IDE_CYLINDER_HIGH = 0x05
        const val IDE_DRIVE_HEAD = 0x06
        const val IDE_STATUS = 0x07
        const val IDE_COMMAND = 0x07
        const val IDE_ALT_STATUS = 0x206
        const val IDE_DEVICE_CONTROL = 0x206
        
        // ===== USB Controller Offsets =====
        
        const val USB_HCCR = 0x00  // Host Controller Control Register
        const val USB_HCCPARAMS = 0x08
        const val USB_USBCMD = 0x10
        const val USB_USBSTS = 0x14
        const val USB_USBINTR = 0x18
        const val USB_FRINDEX = 0x1C
        const val USB_PERIODICLISTBASE = 0x20
        const val USB_ASYNCLISTADDR = 0x28
        
        // ===== Ethernet Controller Offsets =====
        
        const val LAN_CTRL = 0x00
        const val LAN_STATUS = 0x04
        const val LAN_MAC_LOW = 0x08
        const val LAN_MAC_HIGH = 0x0C
        const val LAN_TX_DESC = 0x10
        const val LAN_RX_DESC = 0x14
        const val LAN_INTR = 0x18
        const val LAN_INTR_MASK = 0x1C
        
        // ===== ACPI Register Offsets =====
        
        const val ACPI_PM1_EVT_BLK = 0x00
        const val ACPI_PM1_CNT_BLK = 0x04
        const val ACPI_PM_TMR_BLK = 0x08
        const val ACPI_GP0_BLK = 0x0C
        const val ACPI_GP1_BLK = 0x10
        const val ACPI_SMI_CMD = 0x14
        const val ACPI_PM1a_EVT_BLK = 0x18
        const val ACPI_PM1b_EVT_BLK = 0x1C
        
        // ===== Flash ROM Offsets =====
        
        const val FLASH_CMD = 0x5555
        const val FLASH_ALT_CMD = 0x2AAA
        const val FLASH_DATA = 0x0000
        
        // ===== Interrupt Numbers =====
        
        const val IRQ_NV2A = 0x20
        const val IRQ_APU = 0x21
        const val IRQ_IDE = 0x22
        const val IRQ_USB = 0x23
        const val IRQ_LAN = 0x24
        const val IRQ_SMBUS = 0x25
        const val IRQ_TIMER = 0x26
        
        // ===== Device IDs =====
        
        const val DEVICE_ID_NV2A = 0x02A0
        const val DEVICE_ID_MCPX = 0x01B4
        const val DEVICE_ID_APU = 0x01B1
        const val DEVICE_ID_IDE = 0x01B2
        const val DEVICE_ID_USB = 0x01B3
        const val DEVICE_ID_LAN = 0x01B5
        const val DEVICE_ID_SMBUS = 0x01B6
        
        // ===== Vendor IDs =====
        
        const val VENDOR_ID_NVIDIA = 0x10DE
        const val VENDOR_ID_MICROSOFT = 0x1414
        
        // ===== Constants =====
        
        const val PCI_CONFIG_SIZE = 0x100
        const val SECTOR_SIZE = 512
        const val FLASH_PAGE_SIZE = 256
    }

    // ===== MMIO State =====
    
    data class MMIORegion(
        val name: String,
        val base: Int,
        val size: Int,
        val buffer: ByteBuffer,
        var isEnabled: Boolean = true,
        var isReadOnly: Boolean = false
    )
    
    data class PCIDevice(
        val vendorId: Int,
        val deviceId: Int,
        val className: Int,
        val subclass: Int,
        val progIf: Int,
        val headerType: Int,
        val bars: IntArray,
        val interruptLine: Int,
        val interruptPin: Int
    )
   
    
    
    data class IDEChannel(
        var data: Int = 0,
        var error: Int = 0,
        var sectorCount: Int = 0,
        var sectorNumber: Int = 0,
        var cylinderLow: Int = 0,
        var cylinderHigh: Int = 0,
        var driveHead: Int = 0,
        var status: Int = 0x40, // Ready
        var command: Int = 0,
        var control: Int = 0,
        var lba: Boolean = false,
        var drive: Int = 0
    )
    
    data class USBPort(
        var connected: Boolean = false,
        var speed: Int = 0, // 0 = low, 1 = full, 2 = high
        var deviceAddress: Int = 0,
        var endpoint: Int = 0
    )
    
    data class NetworkPacket(
        val data: ByteArray,
        val timestamp: Long
    )

    // MMIO Regions
    private val regions = mutableMapOf<Int, MMIORegion>()
    private val regionRanges = mutableMapOf<Int, IntRange>()
    private val pciDevices = mutableMapOf<Int, PCIDevice>()
    private val pciConfigSpaces = mutableMapOf<Int, ByteBuffer>()
    
    // Device States
    private val nv2aRegisters = mutableMapOf<Int, Int>()
    private val apuRegisters = mutableMapOf<Int, Int>()
    private val smbusRegisters = mutableMapOf<Int, Int>()
    private val ideChannels = arrayOf(IDEChannel(), IDEChannel()) // Primary & Secondary
    private val usbPorts = Array(4) { USBPort() }
    private val lanRegisters = mutableMapOf<Int, Int>()
    private val acpiRegisters = mutableMapOf<Int, Int>()
    private val flashMemory = ByteBuffer.allocateDirect(FLASH_SIZE).order(ByteOrder.LITTLE_ENDIAN)
    
    // Interrupt State
    private val pendingInterrupts = mutableSetOf<Int>()
    private val enabledInterrupts = mutableSetOf<Int>()
    
    // Device States
    private var nv2aEnabled = true
    private var apuEnabled = false
    private var ideEnabled = false
    private var usbEnabled = false
    private var lanEnabled = false
    private var smbusEnabled = false
    
    // Timing
    private var timerValue = 0L
    private var lastTimerUpdate = System.currentTimeMillis()
    
    // DMA
    private val dmaChannels = Array(8) { DMAChannel() }
    
    // Network
    private val txQueue = mutableListOf<NetworkPacket>()
    private val rxQueue = mutableListOf<NetworkPacket>()
    
    // Debug
    private var debugLogging = false
    
    data class DMAChannel(
        var source: Int = 0,
        var destination: Int = 0,
        var count: Int = 0,
        var control: Int = 0,
        var status: Int = 0,
        var enabled: Boolean = false
    )

    // ===== Initialization =====
    
    init {
        Log.d(TAG, "Initializing Xbox MMIO System...")
        
        // Initialize MMIO regions
        initializeRegions()
        
        // Initialize PCI devices
        initializePCIDevices()
        
        // Initialize device registers
        initializeDeviceRegisters()
        
        // Initialize flash memory
        initializeFlash()
        
        Log.d(TAG, "MMIO system initialized")
    }

    private fun initializeRegions() {
        // NV2A Region
        val nv2aRegion = MMIORegion(
            name = "NV2A",
            base = NV2A_BASE,
            size = NV2A_SIZE,
            buffer = ByteBuffer.allocateDirect(NV2A_SIZE).order(ByteOrder.LITTLE_ENDIAN),
            isEnabled = true,
            isReadOnly = false
        )
        regions[NV2A_BASE] = nv2aRegion
        regionRanges[NV2A_BASE] = NV2A_BASE..(NV2A_BASE + NV2A_SIZE - 1)
        
        // APU Region
        val apuRegion = MMIORegion(
            name = "APU",
            base = APU_BASE,
            size = APU_SIZE,
            buffer = ByteBuffer.allocateDirect(APU_SIZE).order(ByteOrder.LITTLE_ENDIAN),
            isEnabled = true,
            isReadOnly = false
        )
        regions[APU_BASE] = apuRegion
        regionRanges[APU_BASE] = APU_BASE..(APU_BASE + APU_SIZE - 1)
        
        // PCI Configuration Region
        val pciRegion = MMIORegion(
            name = "PCI",
            base = PCI_BASE,
            size = PCI_SIZE,
            buffer = ByteBuffer.allocateDirect(PCI_SIZE).order(ByteOrder.LITTLE_ENDIAN),
            isEnabled = true,
            isReadOnly = false
        )
        regions[PCI_BASE] = pciRegion
        regionRanges[PCI_BASE] = PCI_BASE..(PCI_BASE + PCI_SIZE - 1)
        
        // ACPI Region
        val acpiRegion = MMIORegion(
            name = "ACPI",
            base = ACPI_BASE,
            size = ACPI_SIZE,
            buffer = ByteBuffer.allocateDirect(ACPI_SIZE).order(ByteOrder.LITTLE_ENDIAN),
            isEnabled = true,
            isReadOnly = false
        )
        regions[ACPI_BASE] = acpiRegion
        regionRanges[ACPI_BASE] = ACPI_BASE..(ACPI_BASE + ACPI_SIZE - 1)
        
        // SMBus Region
        val smbusRegion = MMIORegion(
            name = "SMBus",
            base = SMBUS_BASE,
            size = SMBUS_SIZE,
            buffer = ByteBuffer.allocateDirect(SMBUS_SIZE).order(ByteOrder.LITTLE_ENDIAN),
            isEnabled = true,
            isReadOnly = false
        )
        regions[SMBUS_BASE] = smbusRegion
        regionRanges[SMBUS_BASE] = SMBUS_BASE..(SMBUS_BASE + SMBUS_SIZE - 1)
        
        // IDE Region
        val ideRegion = MMIORegion(
            name = "IDE",
            base = IDE_BASE,
            size = IDE_SIZE,
            buffer = ByteBuffer.allocateDirect(IDE_SIZE).order(ByteOrder.LITTLE_ENDIAN),
            isEnabled = true,
            isReadOnly = false
        )
        regions[IDE_BASE] = ideRegion
        regionRanges[IDE_BASE] = IDE_BASE..(IDE_BASE + IDE_SIZE - 1)
        
        // USB Region
        val usbRegion = MMIORegion(
            name = "USB",
            base = USB_BASE,
            size = USB_SIZE,
            buffer = ByteBuffer.allocateDirect(USB_SIZE).order(ByteOrder.LITTLE_ENDIAN),
            isEnabled = true,
            isReadOnly = false
        )
        regions[USB_BASE] = usbRegion
        regionRanges[USB_BASE] = USB_BASE..(USB_BASE + USB_SIZE - 1)
        
        // LAN Region
        val lanRegion = MMIORegion(
            name = "LAN",
            base = LAN_BASE,
            size = LAN_SIZE,
            buffer = ByteBuffer.allocateDirect(LAN_SIZE).order(ByteOrder.LITTLE_ENDIAN),
            isEnabled = true,
            isReadOnly = false
        )
        regions[LAN_BASE] = lanRegion
        regionRanges[LAN_BASE] = LAN_BASE..(LAN_BASE + LAN_SIZE - 1)
        
        // Flash Region
        val flashRegion = MMIORegion(
            name = "FLASH",
            base = FLASH_BASE,
            size = FLASH_SIZE,
            buffer = flashMemory,
            isEnabled = true,
            isReadOnly = false
        )
        regions[FLASH_BASE] = flashRegion
        regionRanges[FLASH_BASE] = FLASH_BASE..(FLASH_BASE + FLASH_SIZE - 1)
        
        Log.d(TAG, "MMIO regions initialized: ${regions.size} regions")
    }
    
    

    private fun initializePCIDevices() {
        // NV2A Device (Bus 0, Device 0, Function 0)
        val nv2aDevice = PCIDevice(
            vendorId = VENDOR_ID_NVIDIA,
            deviceId = DEVICE_ID_NV2A,
            className = 0x03, // Display controller
            subclass = 0x00, // VGA compatible
            progIf = 0x00,
            headerType = 0x00,
            bars = intArrayOf(NV2A_BASE, 0, 0, 0, 0, 0),
            interruptLine = IRQ_NV2A,
            interruptPin = 0x01
        )
        pciDevices[0x0000] = nv2aDevice
        
        // MCPX Device (Bus 0, Device 1, Function 0)
        val mcpxDevice = PCIDevice(
            vendorId = VENDOR_ID_MICROSOFT,
            deviceId = DEVICE_ID_MCPX,
            className = 0x06, // Bridge
            subclass = 0x80, // Other
            progIf = 0x00,
            headerType = 0x00,
            bars = intArrayOf(0, 0, 0, 0, 0, 0),
            interruptLine = 0,
            interruptPin = 0x00
        )
        pciDevices[0x0100] = mcpxDevice
        
        // APU Device (Bus 0, Device 2, Function 0)
        val apuDevice = PCIDevice(
            vendorId = VENDOR_ID_MICROSOFT,
            deviceId = DEVICE_ID_APU,
            className = 0x04, // Multimedia
            subclass = 0x01, // Audio
            progIf = 0x00,
            headerType = 0x00,
            bars = intArrayOf(APU_BASE, 0, 0, 0, 0, 0),
            interruptLine = IRQ_APU,
            interruptPin = 0x01
        )
        pciDevices[0x0200] = apuDevice
        
        // IDE Device (Bus 0, Device 3, Function 0)
        val ideDevice = PCIDevice(
            vendorId = VENDOR_ID_MICROSOFT,
            deviceId = DEVICE_ID_IDE,
            className = 0x01, // Mass storage
            subclass = 0x01, // IDE
            progIf = 0x8A, // PCI native mode
            headerType = 0x00,
            bars = intArrayOf(IDE_BASE, 0, 0, 0, 0, 0),
            interruptLine = IRQ_IDE,
            interruptPin = 0x01
        )
        pciDevices[0x0300] = ideDevice
        
        // USB Device (Bus 0, Device 4, Function 0)
        val usbDevice = PCIDevice(
            vendorId = VENDOR_ID_MICROSOFT,
            deviceId = DEVICE_ID_USB,
            className = 0x0C, // Serial bus
            subclass = 0x03, // USB
            progIf = 0x10, // UHCI
            headerType = 0x00,
            bars = intArrayOf(USB_BASE, 0, 0, 0, 0, 0),
            interruptLine = IRQ_USB,
            interruptPin = 0x01
        )
        pciDevices[0x0400] = usbDevice
        
        // LAN Device (Bus 0, Device 5, Function 0)
        val lanDevice = PCIDevice(
            vendorId = VENDOR_ID_MICROSOFT,
            deviceId = DEVICE_ID_LAN,
            className = 0x02, // Network
            subclass = 0x00, // Ethernet
            progIf = 0x00,
            headerType = 0x00,
            bars = intArrayOf(LAN_BASE, 0, 0, 0, 0, 0),
            interruptLine = IRQ_LAN,
            interruptPin = 0x01
        )
        pciDevices[0x0500] = lanDevice
        
        // SMBus Device (Bus 0, Device 6, Function 0)
        val smbusDevice = PCIDevice(
            vendorId = VENDOR_ID_MICROSOFT,
            deviceId = DEVICE_ID_SMBUS,
            className = 0x0C, // Serial bus
            subclass = 0x05, // SMBus
            progIf = 0x00,
            headerType = 0x00,
            bars = intArrayOf(SMBUS_BASE, 0, 0, 0, 0, 0),
            interruptLine = IRQ_SMBUS,
            interruptPin = 0x01
        )
        pciDevices[0x0600] = smbusDevice
        
        // Initialize PCI configuration spaces
        for ((address, device) in pciDevices) {
            val configSpace = ByteBuffer.allocateDirect(PCI_CONFIG_SIZE).order(ByteOrder.LITTLE_ENDIAN)
            
            // Write standard header
            configSpace.putShort(PCI_VENDOR_ID, device.vendorId.toShort())
            configSpace.putShort(PCI_DEVICE_ID, device.deviceId.toShort())
            configSpace.putShort(PCI_COMMAND, 0x0007.toShort()) // Enable I/O, Memory, Bus Master
            configSpace.putShort(PCI_STATUS, 0x0200.toShort()) // Fast back-to-back capable
            configSpace.put(PCI_REVISION, 0x00.toByte())
            configSpace.put(PCI_CLASS_CODE, device.className.toByte())
            configSpace.put(PCI_CLASS_CODE + 1, device.subclass.toByte())
            configSpace.put(PCI_CLASS_CODE + 2, device.progIf.toByte())
            configSpace.put(PCI_HEADER_TYPE, device.headerType.toByte())
            
            // Write BARs
            for (i in device.bars.indices) {
                configSpace.putInt(PCI_BAR0 + (i * 4), device.bars[i])
            }
            
            // Write interrupt information
            configSpace.put(PCI_INTERRUPT_LINE, device.interruptLine.toByte())
            configSpace.put(PCI_INTERRUPT_PIN, device.interruptPin.toByte())
            
            pciConfigSpaces[address] = configSpace
        }
        
        Log.d(TAG, "PCI devices initialized: ${pciDevices.size} devices")
    }

    private fun initializeDeviceRegisters() {
        // Initialize NV2A registers
        nv2aRegisters[NV_PMC_BOOT_0] = 0x02A010DE  // NV2A signature
        nv2aRegisters[NV_PMC_INTR_0] = 0x00000000
        nv2aRegisters[NV_PMC_INTR_EN_0] = 0x00000000
        
        nv2aRegisters[NV_PTIMER_TIME_0] = 0x00000000
        nv2aRegisters[NV_PTIMER_NUMERATOR] = 0x0000003C  // 60Hz
        nv2aRegisters[NV_PTIMER_DENOMINATOR] = 0x00000001
        
        nv2aRegisters[NV_PCRTC_START] = 0xFD000000.toInt()  // Framebuffer address
        nv2aRegisters[NV_PCRTC_INTR_0] = 0x00000000
        nv2aRegisters[NV_PCRTC_INTR_EN_0] = 0x00000001  // Enable VBlank interrupt
        
        nv2aRegisters[NV_PGRAPH_INTR] = 0x00000000
        nv2aRegisters[NV_PGRAPH_INTR_EN] = 0x00000000
        
        nv2aRegisters[NV_PFIFO_CACHE1_PUT] = 0x00000000
        nv2aRegisters[NV_PFIFO_CACHE1_GET] = 0x00000000
        
        // Initialize APU registers
        apuRegisters[APU_CTRL] = 0x00000000
        apuRegisters[APU_STATUS] = 0x00000000
        apuRegisters[APU_INTR_CTRL] = 0x00000000
        apuRegisters[APU_INTR_STATUS] = 0x00000000
        apuRegisters[APU_VOLUME] = 0x00007F7F  // Left/Right volume at max
        apuRegisters[APU_SAMPLE_RATE] = 0x0000AC44  // 44100 Hz
        apuRegisters[APU_FORMAT] = 0x00000000  // 16-bit stereo
        
        // Initialize SMBus registers
        smbusRegisters[SMBUS_STATUS] = 0x0000
        smbusRegisters[SMBUS_CONTROL] = 0x0000
        smbusRegisters[SMBUS_ADDRESS] = 0x0000
        smbusRegisters[SMBUS_DATA] = 0x0000
        smbusRegisters[SMBUS_COMMAND] = 0x0000
        
        // Initialize LAN registers
        lanRegisters[LAN_CTRL] = 0x00000000
        lanRegisters[LAN_STATUS] = 0x00000000
        lanRegisters[LAN_MAC_LOW] = 0x00112233
        lanRegisters[LAN_MAC_HIGH] = 0x44550000
        lanRegisters[LAN_INTR] = 0x00000000
        lanRegisters[LAN_INTR_MASK] = 0x00000000
        
        // Initialize ACPI registers
        acpiRegisters[ACPI_PM1_CNT_BLK] = 0x00000000
        acpiRegisters[ACPI_PM_TMR_BLK] = 0x00000000
        acpiRegisters[ACPI_SMI_CMD] = 0x00000000
        
        // Initialize IDE channels
        ideChannels[0].status = 0x40  // Drive ready
        ideChannels[1].status = 0x40
        
        // Initialize USB ports
        for (port in usbPorts) {
            port.connected = false
            port.speed = 1  // Full speed
        }
        
        // Initialize DMA channels
        for (channel in dmaChannels) {
            channel.enabled = false
            channel.status = 0x00
        }
        
        Log.d(TAG, "Device registers initialized")
    }

    private fun initializeFlash() {
        // Clear flash memory
        for (i in 0 until FLASH_SIZE) {
            flashMemory.put(i, 0xFF.toByte())
        }
        
        // Write Xbox bootloader signature
        flashMemory.putInt(0x00, 0x534C4244)  // "DBLS"
        flashMemory.putInt(0x04, 0x01000001)  // Version 1.0
        
        // Write Xbox ROM signature
        flashMemory.position(0x1FC)
        flashMemory.put("XBOX".toByteArray())
        
        flashMemory.position(0)
        Log.d(TAG, "Flash memory initialized (${FLASH_SIZE / 1024}KB)")
    }

    /* ===============================
       MMIO Access Methods
       =============================== */

    fun read8(address: Int): Int {
        // Find region
        val region = findRegion(address)
        if (region == null || !region.isEnabled) {
            Log.w(TAG, "Read from unmapped MMIO address: 0x${address.toString(16)}")
            return 0xFF
        }
        
        val offset = address - region.base
        
        return when (region.name) {
            "NV2A" -> readNV2A(offset, 1)
            "APU" -> readAPU(offset, 1)
            "PCI" -> readPCI(offset, 1)
            "ACPI" -> readACPI(offset, 1)
            "SMBus" -> readSMBus(offset, 1)
            "IDE" -> readIDE(offset, 1)
            "USB" -> readUSB(offset, 1)
            "LAN" -> readLAN(offset, 1)
            "FLASH" -> readFlash(offset, 1)
            else -> {
                if (offset >= 0 && offset < region.buffer.capacity()) {
                    region.buffer.get(offset).toInt() and 0xFF
                } else {
                    0xFF
                }
            }
        }
    }

    fun read16(address: Int): Int {
        // Check alignment
        if (address and 0x1 != 0) {
            Log.w(TAG, "Unaligned 16-bit MMIO read at 0x${address.toString(16)}")
        }
        
        val low = read8(address)
        val high = read8(address + 1)
        return (high shl 8) or low
    }

    fun read32(address: Int): Int {
        // Check alignment
        if (address and 0x3 != 0) {
            Log.w(TAG, "Unaligned 32-bit MMIO read at 0x${address.toString(16)}")
        }
        
        val b0 = read8(address)
        val b1 = read8(address + 1)
        val b2 = read8(address + 2)
        val b3 = read8(address + 3)
        
        return (b3 shl 24) or (b2 shl 16) or (b1 shl 8) or b0
    }

    fun write8(address: Int, value: Int) {
        val byteValue = value and 0xFF
        
        // Find region
        val region = findRegion(address)
        if (region == null || !region.isEnabled) {
            Log.w(TAG, "Write to unmapped MMIO address: 0x${address.toString(16)} = 0x${byteValue.toString(16)}")
            return
        }
        
        if (region.isReadOnly) {
            Log.w(TAG, "Write to read-only MMIO address: 0x${address.toString(16)}")
            return
        }
        
        val offset = address - region.base
        
        when (region.name) {
            "NV2A" -> writeNV2A(offset, byteValue, 1)
            "APU" -> writeAPU(offset, byteValue, 1)
            "PCI" -> writePCI(offset, byteValue, 1)
            "ACPI" -> writeACPI(offset, byteValue, 1)
            "SMBus" -> writeSMBus(offset, byteValue, 1)
            "IDE" -> writeIDE(offset, byteValue, 1)
            "USB" -> writeUSB(offset, byteValue, 1)
            "LAN" -> writeLAN(offset, byteValue, 1)
            "FLASH" -> writeFlash(offset, byteValue, 1)
            else -> {
                if (offset >= 0 && offset < region.buffer.capacity()) {
                    region.buffer.put(offset, byteValue.toByte())
                }
            }
        }
        
        if (debugLogging) {
            Log.v(TAG, "MMIO write: 0x${address.toString(16)} = 0x${byteValue.toString(16)} [${region.name}]")
        }
    }

    fun write16(address: Int, value: Int) {
        // Check alignment
        if (address and 0x1 != 0) {
            Log.w(TAG, "Unaligned 16-bit MMIO write at 0x${address.toString(16)}")
        }
        
        val shortValue = value and 0xFFFF
        write8(address, shortValue and 0xFF)
        write8(address + 1, (shortValue shr 8) and 0xFF)
    }

    fun write32(address: Int, value: Int) {
        // Check alignment
        if (address and 0x3 != 0) {
            Log.w(TAG, "Unaligned 32-bit MMIO write at 0x${address.toString(16)}")
        }
        
        write8(address, value and 0xFF)
        write8(address + 1, (value shr 8) and 0xFF)
        write8(address + 2, (value shr 16) and 0xFF)
        write8(address + 3, (value shr 24) and 0xFF)
    }

    /* ===============================
       Device-Specific Read/Write
       =============================== */

    private fun readNV2A(offset: Int, size: Int): Int {
        // Convert offset to register address
        val regAddress = offset
        
        // Handle special registers
        when (regAddress) {
            NV_PMC_BOOT_0 -> return nv2aRegisters[regAddress] ?: 0x02A010DE
            NV_PMC_INTR_0 -> return nv2aRegisters[regAddress] ?: 0x00000000
            NV_PMC_INTR_EN_0 -> return nv2aRegisters[regAddress] ?: 0x00000000
            
            NV_PTIMER_TIME_0 -> {
                // Update timer value
                val currentTime = System.currentTimeMillis()
                val delta = currentTime - lastTimerUpdate
                lastTimerUpdate = currentTime
                timerValue += (delta * 1000L).toLong() // Convert to microseconds
                return (timerValue and 0xFFFFFFFF).toInt()
            }
            NV_PTIMER_NUMERATOR -> return nv2aRegisters[regAddress] ?: 0x0000003C
            NV_PTIMER_DENOMINATOR -> return nv2aRegisters[regAddress] ?: 0x00000001
            
            NV_PCRTC_START -> return nv2aRegisters[regAddress] ?: 0xFD000000.toInt()
            NV_PCRTC_RASTER -> {
                // Return current scanline (simulated)
                val time = System.currentTimeMillis() % 16666 // 60Hz frame = 16.666ms
                val scanline = (time * 480 / 16666).toInt() // 480 lines
                return scanline
            }
            NV_PCRTC_INTR_0 -> {
                val value = nv2aRegisters[regAddress] ?: 0x00000000
                // Clear interrupt on read
                nv2aRegisters[regAddress] = 0x00000000
                return value
            }
            NV_PCRTC_INTR_EN_0 -> return nv2aRegisters[regAddress] ?: 0x00000001
            
            NV_PGRAPH_INTR -> {
                val value = nv2aRegisters[regAddress] ?: 0x00000000
                // Clear interrupt on read
                nv2aRegisters[regAddress] = 0x00000000
                return value
            }
            NV_PGRAPH_INTR_EN -> return nv2aRegisters[regAddress] ?: 0x00000000
            
            NV_PFIFO_CACHE1_PUT -> return nv2aRegisters[regAddress] ?: 0x00000000
            NV_PFIFO_CACHE1_GET -> return nv2aRegisters[regAddress] ?: 0x00000000
            NV_PFIFO_CACHE1_PUSH -> return 0x00000001 // Always ready to push
            NV_PFIFO_CACHE1_PULL -> return 0x00000001 // Always ready to pull
            
            NV_PVIDEO_INTR -> return nv2aRegisters[regAddress] ?: 0x00000000
            NV_PVIDEO_INTR_EN -> return nv2aRegisters[regAddress] ?: 0x00000000
            
            else -> {
                // Handle GPU-specific registers through GPU object
                if (gpu != null && regAddress >= 0x08000000 && regAddress < 0x08001000) {
                    return gpu.readRegister(regAddress)
                }
                
                // Default: return from register map or 0
                return nv2aRegisters[regAddress] ?: 0x00000000
            }
        }
    }

    private fun writeNV2A(offset: Int, value: Int, size: Int) {
        val regAddress = offset
        
        when (regAddress) {
            NV_PMC_INTR_0 -> {
                // Write 1 to clear interrupt bits
                val current = nv2aRegisters[regAddress] ?: 0x00000000
                nv2aRegisters[regAddress] = current and value.inv()
            }
            NV_PMC_INTR_EN_0 -> {
                nv2aRegisters[regAddress] = value
            }
            
            NV_PCRTC_INTR_0 -> {
                // Write 1 to clear interrupt bits
                val current = nv2aRegisters[regAddress] ?: 0x00000000
                nv2aRegisters[regAddress] = current and value.inv()
            }
            NV_PCRTC_INTR_EN_0 -> {
                nv2aRegisters[regAddress] = value
            }
            
            NV_PGRAPH_INTR -> {
                // Write 1 to clear interrupt bits
                val current = nv2aRegisters[regAddress] ?: 0x00000000
                nv2aRegisters[regAddress] = current and value.inv()
            }
            NV_PGRAPH_INTR_EN -> {
                nv2aRegisters[regAddress] = value
            }
            
            NV_PGRAPH_CTX_CONTROL -> {
                // Context control
                nv2aRegisters[regAddress] = value
            }
            
            NV_PFIFO_CACHE1_PUSH -> {
                // Push command to FIFO
                if (value != 0) {
                    // In real NV2A, this would push a command
                    Log.v(TAG, "NV2A FIFO push command")
                }
            }
            NV_PFIFO_CACHE1_PULL -> {
                // Pull command from FIFO
                if (value != 0) {
                    // In real NV2A, this would pull a command
                    Log.v(TAG, "NV2A FIFO pull command")
                }
            }
            NV_PFIFO_CACHE1_PUT -> {
                nv2aRegisters[NV_PFIFO_CACHE1_PUT] = value
            }
            NV_PFIFO_CACHE1_GET -> {
                nv2aRegisters[NV_PFIFO_CACHE1_GET] = value
            }
            
            NV_PCRTC_START -> {
                nv2aRegisters[regAddress] = value
                Log.d(TAG, "Framebuffer address set to 0x${value.toString(16)}")
            }
            
            NV_PRAMDAC_GENERAL_CONTROL -> {
                // RAMDAC control
                nv2aRegisters[regAddress] = value
            }
            
            NV_PRAMDAC_VPLL_COEFF -> {
                // Video PLL coefficients
                nv2aRegisters[regAddress] = value
            }
            
            else -> {
                // Handle GPU-specific registers through GPU object
                if (gpu != null && regAddress >= 0x08000000 && regAddress < 0x08001000) {
                gpu!!.writeRegister(regAddress, value)
                } else {
                    // Store in register map
                    nv2aRegisters[regAddress] = value
                }
                
                if (debugLogging && size == 4) {
                    Log.v(TAG, "NV2A write: 0x${regAddress.toString(16)} = 0x${value.toString(16)}")
                }
            }
        }
    }

    private fun readAPU(offset: Int, size: Int): Int {
        val regAddress = offset
        
        return when (regAddress) {
            APU_CTRL -> apuRegisters[regAddress] ?: 0x00000000
            APU_STATUS -> {
                val status = apuRegisters[regAddress] ?: 0x00000000
                // Always report ready
                status or 0x00000001
            }
            APU_INTR_CTRL -> apuRegisters[regAddress] ?: 0x00000000
            APU_INTR_STATUS -> {
                val status = apuRegisters[regAddress] ?: 0x00000000
                // Clear on read
                apuRegisters[regAddress] = 0x00000000
                status
            }
            APU_DMA_ADDR -> apuRegisters[regAddress] ?: 0x00000000
            APU_DMA_SIZE -> apuRegisters[regAddress] ?: 0x00000000
            APU_DMA_CTRL -> apuRegisters[regAddress] ?: 0x00000000
            APU_VOLUME -> apuRegisters[regAddress] ?: 0x00007F7F
            APU_SAMPLE_RATE -> apuRegisters[regAddress] ?: 0x0000AC44
            APU_FORMAT -> apuRegisters[regAddress] ?: 0x00000000
            else -> 0x00000000
        }
    }

    private fun writeAPU(offset: Int, value: Int, size: Int) {
        val regAddress = offset
        
        when (regAddress) {
            APU_CTRL -> {
                apuRegisters[regAddress] = value
                apuEnabled = (value and 0x1) != 0
                Log.d(TAG, "APU ${if (apuEnabled) "enabled" else "disabled"}")
            }
            APU_INTR_CTRL -> {
                apuRegisters[regAddress] = value
                if (value and 0x1 != 0) {
                    // Enable APU interrupt
                    enabledInterrupts.add(IRQ_APU)
                } else {
                    enabledInterrupts.remove(IRQ_APU)
                }
            }
            APU_INTR_STATUS -> {
                // Write 1 to clear interrupt bits
                val current = apuRegisters[regAddress] ?: 0x00000000
                apuRegisters[regAddress] = current and value.inv()
            }
            APU_DMA_ADDR -> apuRegisters[regAddress] = value
            APU_DMA_SIZE -> apuRegisters[regAddress] = value
            APU_DMA_CTRL -> {
                apuRegisters[regAddress] = value
                if (value and 0x1 != 0) {
                    // Start DMA transfer
                    startAPUDMA()
                }
            }
            APU_VOLUME -> apuRegisters[regAddress] = value
            APU_SAMPLE_RATE -> apuRegisters[regAddress] = value
            APU_FORMAT -> apuRegisters[regAddress] = value
            else -> apuRegisters[regAddress] = value
        }
    }

    private fun readPCI(offset: Int, size: Int): Int {
        // PCI configuration space access
        if (offset < PCI_SIZE) {
            val deviceOffset = offset % PCI_CONFIG_SIZE
            val deviceIndex = offset / PCI_CONFIG_SIZE
            
            val configSpace = pciConfigSpaces[deviceIndex * 0x100]
            if (configSpace != null && deviceOffset < configSpace.capacity()) {
                return when (size) {
                    1 -> configSpace.get(deviceOffset).toInt() and 0xFF
                    2 -> configSpace.getShort(deviceOffset).toInt() and 0xFFFF
                    4 -> configSpace.getInt(deviceOffset)
                    else -> 0
                }
            }
        }
        
        return 0
    }

    private fun writePCI(offset: Int, value: Int, size: Int) {
        // PCI configuration space access
        if (offset < PCI_SIZE) {
            val deviceOffset = offset % PCI_CONFIG_SIZE
            val deviceIndex = offset / PCI_CONFIG_SIZE
            
            val configSpace = pciConfigSpaces[deviceIndex * 0x100]
            if (configSpace != null && deviceOffset < configSpace.capacity()) {
                when (size) {
                    1 -> configSpace.put(deviceOffset, value.toByte())
                    2 -> configSpace.putShort(deviceOffset, value.toShort())
                    4 -> configSpace.putInt(deviceOffset, value)
                }
                
                // Handle BAR writes
                if (deviceOffset >= PCI_BAR0 && deviceOffset < PCI_BAR0 + 24) {
                    val barIndex = (deviceOffset - PCI_BAR0) / 4
                    val device = pciDevices[deviceIndex * 0x100]
                    if (device != null && barIndex < device.bars.size) {
                        // Update BAR value
                        // Note: In real PCI, BAR writes have special handling
                        Log.v(TAG, "PCI BAR${barIndex} write: 0x${value.toString(16)}")
                    }
                }
            }
        }
    }

    private fun readACPI(offset: Int, size: Int): Int {
        return acpiRegisters[offset] ?: 0x00000000
    }

    private fun writeACPI(offset: Int, value: Int, size: Int) {
        acpiRegisters[offset] = value
        
        // Handle ACPI commands
        when (offset) {
            ACPI_PM1_CNT_BLK -> {
                if (value and 0x1000 != 0) { // SLP_EN
                    if (value and 0x4000 != 0) { // SLP_TYP
                        Log.d(TAG, "ACPI: System entering S3 (Suspend to RAM)")
                    }
                }
            }
            ACPI_SMI_CMD -> {
                // System Management Interrupt command
                Log.d(TAG, "ACPI SMI command: 0x${value.toString(16)}")
            }
        }
    }

    private fun readSMBus(offset: Int, size: Int): Int {
        return when (offset) {
            SMBUS_STATUS -> smbusRegisters[offset] ?: 0x0000
            SMBUS_CONTROL -> smbusRegisters[offset] ?: 0x0000
            SMBUS_ADDRESS -> smbusRegisters[offset] ?: 0x0000
            SMBUS_DATA -> {
                // Read data from SMBus device (simulated)
                val data = smbusRegisters[offset] ?: 0x0000
                smbusRegisters[offset] = 0x0000 // Clear after read
                data
            }
            SMBUS_COMMAND -> smbusRegisters[offset] ?: 0x0000
            else -> 0x0000
        }
    }

    private fun writeSMBus(offset: Int, value: Int, size: Int) {
        when (offset) {
            SMBUS_STATUS -> {
                // Write 1 to clear status bits
                val current = smbusRegisters[offset] ?: 0x0000
                smbusRegisters[offset] = current and value.inv()
            }
            SMBUS_CONTROL -> {
                smbusRegisters[offset] = value
                if (value and 0x0400 != 0) { // START
                    processSMBusTransaction()
                }
            }
            SMBUS_ADDRESS -> smbusRegisters[offset] = value
            SMBUS_DATA -> smbusRegisters[offset] = value
            SMBUS_COMMAND -> smbusRegisters[offset] = value
            else -> smbusRegisters[offset] = value
        }
    }

    private fun readIDE(offset: Int, size: Int): Int {
        val channelIndex = if (offset < 0x100) 0 else 1
        val channelOffset = offset % 0x100
        val channel = ideChannels[channelIndex]
        
        return when (channelOffset) {
            IDE_DATA -> {
                // Read data from simulated drive
                0x0000
            }
            IDE_ERROR -> channel.error
            IDE_SECTOR_COUNT -> channel.sectorCount
            IDE_SECTOR_NUMBER -> channel.sectorNumber
            IDE_CYLINDER_LOW -> channel.cylinderLow
            IDE_CYLINDER_HIGH -> channel.cylinderHigh
            IDE_DRIVE_HEAD -> channel.driveHead
            IDE_STATUS -> {
                // Update status before read
                channel.status = channel.status or 0x40 // Always ready
                channel.status
            }
            IDE_COMMAND -> channel.command
            IDE_ALT_STATUS -> channel.control
            IDE_DEVICE_CONTROL -> channel.control
            else -> 0x00
        }
    }

    private fun writeIDE(offset: Int, value: Int, size: Int) {
        val channelIndex = if (offset < 0x100) 0 else 1
        val channelOffset = offset % 0x100
        val channel = ideChannels[channelIndex]
        
        when (channelOffset) {
            IDE_DATA -> {
                // Write data to simulated drive
            }
            IDE_ERROR -> channel.error = value
            IDE_SECTOR_COUNT -> channel.sectorCount = value
            IDE_SECTOR_NUMBER -> channel.sectorNumber = value
            IDE_CYLINDER_LOW -> channel.cylinderLow = value
            IDE_CYLINDER_HIGH -> channel.cylinderHigh = value
            IDE_DRIVE_HEAD -> {
                channel.driveHead = value
                channel.drive = (value shr 4) and 0x1
                channel.lba = (value and 0x40) != 0
            }
            IDE_COMMAND -> {
                channel.command = value
                handleIDECommand(channelIndex, value)
            }
            IDE_DEVICE_CONTROL -> {
                channel.control = value
                if (value and 0x02 != 0) { // SRST
                    // Software reset
                    resetIDEChannel(channelIndex)
                }
            }
        }
    }

    private fun readUSB(offset: Int, size: Int): Int {
        // Simulated USB controller
        return when (offset) {
            USB_HCCR -> 0x0110 // Version 1.1
            USB_HCCPARAMS -> 0x00000000
            USB_USBCMD -> 0x00000000
            USB_USBSTS -> 0x00000000
            USB_USBINTR -> 0x00000000
            USB_FRINDEX -> (System.currentTimeMillis() % 1000).toInt() // Frame index
            USB_PERIODICLISTBASE -> 0x00000000
            USB_ASYNCLISTADDR -> 0x00000000
            else -> 0x00000000
        }
    }

    private fun writeUSB(offset: Int, value: Int, size: Int) {
        when (offset) {
            USB_USBCMD -> {
                if (value and 0x1 != 0) {
                    usbEnabled = true
                    Log.d(TAG, "USB controller enabled")
                } else {
                    usbEnabled = false
                    Log.d(TAG, "USB controller disabled")
                }
            }
            USB_USBINTR -> {
                if (value != 0) {
                    enabledInterrupts.add(IRQ_USB)
                } else {
                    enabledInterrupts.remove(IRQ_USB)
                }
            }
        }
    }

    private fun readLAN(offset: Int, size: Int): Int {
        return lanRegisters[offset] ?: 0x00000000
    }

    private fun writeLAN(offset: Int, value: Int, size: Int) {
        when (offset) {
            LAN_CTRL -> {
                lanRegisters[offset] = value
                lanEnabled = (value and 0x1) != 0
                Log.d(TAG, "LAN controller ${if (lanEnabled) "enabled" else "disabled"}")
            }
            LAN_INTR_MASK -> {
                lanRegisters[offset] = value
                if (value != 0) {
                    enabledInterrupts.add(IRQ_LAN)
                } else {
                    enabledInterrupts.remove(IRQ_LAN)
                }
            }
            else -> lanRegisters[offset] = value
        }
    }

    private fun readFlash(offset: Int, size: Int): Int {
        if (offset < flashMemory.capacity()) {
            return when (size) {
                1 -> flashMemory.get(offset).toInt() and 0xFF
                2 -> flashMemory.getShort(offset).toInt() and 0xFFFF
                4 -> flashMemory.getInt(offset)
                else -> 0
            }
        }
        return 0xFF
    }

    private fun writeFlash(offset: Int, value: Int, size: Int) {
        // Flash memory programming (simplified)
        if (offset < flashMemory.capacity()) {
            // Check for flash command sequences
            when (offset) {
                FLASH_CMD -> {
                    // Command phase 1
                    if (value == 0xAA) {
                        Log.v(TAG, "Flash command sequence started")
                    }
                }
                FLASH_ALT_CMD -> {
                    // Command phase 2
                    if (value == 0x55) {
                        Log.v(TAG, "Flash command phase 2")
                    }
                }
                else -> {
                    // Normal write
                    when (size) {
                        1 -> flashMemory.put(offset, value.toByte())
                        2 -> flashMemory.putShort(offset, value.toShort())
                        4 -> flashMemory.putInt(offset, value)
                    }
                }
            }
        }
    }

    /* ===============================
       Device Command Handling
       =============================== */

    private fun handleIDECommand(channel: Int, command: Int) {
        val ideChannel = ideChannels[channel]
        
        when (command) {
            0x20, 0x21 -> { // READ SECTOR(S)
                ideChannel.status = 0x40 // Ready, no error
                // In real implementation, would read from disk image
                Log.d(TAG, "IDE READ SECTOR command")
            }
            0x30, 0x31 -> { // WRITE SECTOR(S)
                ideChannel.status = 0x40 // Ready, no error
                // In real implementation, would write to disk image
                Log.d(TAG, "IDE WRITE SECTOR command")
            }
            0x90 -> { // EXECUTE DEVICE DIAGNOSTIC
                ideChannel.status = 0x40 // Ready, no error
                ideChannel.error = 0x01 // Diagnostic passed
                Log.d(TAG, "IDE DIAGNOSTIC command")
            }
            0xEC -> { // IDENTIFY DEVICE
                ideChannel.status = 0x40 // Ready, no error
                // In real implementation, would fill identify buffer
                Log.d(TAG, "IDE IDENTIFY DEVICE command")
            }
            0xEF -> { // SET FEATURES
                ideChannel.status = 0x40 // Ready, no error
                Log.d(TAG, "IDE SET FEATURES command")
            }
            else -> {
                Log.w(TAG, "Unhandled IDE command: 0x${command.toString(16)}")
                ideChannel.status = 0x41 // Ready with error
                ideChannel.error = 0x04 // Command aborted
            }
        }
    }

    private fun resetIDEChannel(channel: Int) {
        val ideChannel = ideChannels[channel]
        
        ideChannel.data = 0
        ideChannel.error = 0x01 // Diagnostic passed
        ideChannel.sectorCount = 0x01
        ideChannel.sectorNumber = 0x01
        ideChannel.cylinderLow = 0x00
        ideChannel.cylinderHigh = 0x00
        ideChannel.driveHead = 0x00
        ideChannel.status = 0x40 // Ready
        ideChannel.command = 0x00
        ideChannel.control = 0x00
        ideChannel.lba = false
        ideChannel.drive = 0
        
        Log.d(TAG, "IDE channel $channel reset")
    }

    private fun processSMBusTransaction() {
        val address = smbusRegisters[SMBUS_ADDRESS] ?: 0x0000
        val command = smbusRegisters[SMBUS_COMMAND] ?: 0x0000
        val data = smbusRegisters[SMBUS_DATA] ?: 0x0000
        
        // Simulate SMBus transactions
        when (address shr 1) { // 7-bit address
            0x10 -> { // Temperature sensor
                smbusRegisters[SMBUS_DATA] = 0x25 // 37°C
            }
            0x20 -> { // EEPROM
                // Read from simulated EEPROM
                smbusRegisters[SMBUS_DATA] = 0x00
            }
            else -> {
                smbusRegisters[SMBUS_DATA] = 0x00
            }
        }
        
        // Set status to completed
        smbusRegisters[SMBUS_STATUS] = 0x0080 // COMPLETED flag
    }

    private fun startAPUDMA() {
        val dmaAddr = apuRegisters[APU_DMA_ADDR] ?: 0x00000000
        val dmaSize = apuRegisters[APU_DMA_SIZE] ?: 0x00000000
        
        Log.d(TAG, "APU DMA started: addr=0x${dmaAddr.toString(16)}, size=$dmaSize")
        
        // In real implementation, would transfer audio data
        // For now, just simulate completion
        
        // Set interrupt
        apuRegisters[APU_INTR_STATUS] = 0x00000001
        if (enabledInterrupts.contains(IRQ_APU)) {
            pendingInterrupts.add(IRQ_APU)
        }
    }

    /* ===============================
       Interrupt Handling
       =============================== */

    fun hasPendingInterrupts(): Boolean {
        return pendingInterrupts.isNotEmpty()
    }

    fun getPendingInterrupt(): Int? {
        if (pendingInterrupts.isEmpty()) return null
        
        // Get highest priority interrupt
        val interrupt = pendingInterrupts.minOrNull()
        pendingInterrupts.remove(interrupt)
        return interrupt
    }

    fun triggerInterrupt(interrupt: Int) {
        if (enabledInterrupts.contains(interrupt)) {
            pendingInterrupts.add(interrupt)
            Log.v(TAG, "Interrupt $interrupt triggered")
        }
    }

    fun enableInterrupt(interrupt: Int) {
        enabledInterrupts.add(interrupt)
    }

    fun disableInterrupt(interrupt: Int) {
        enabledInterrupts.remove(interrupt)
    }

    fun clearInterrupt(interrupt: Int) {
        pendingInterrupts.remove(interrupt)
    }
    
    fun setGPU(gpu: XboxGPU?) {
    this.gpu = gpu  // gpu موجود بالفعل كخاصية
    Log.d(TAG, "GPU set in MMIO")
}

    /* ===============================
       Device Control
       =============================== */

    fun reset() {
        Log.d(TAG, "Resetting MMIO system...")
        
        // Clear all registers
        nv2aRegisters.clear()
        apuRegisters.clear()
        smbusRegisters.clear()
        lanRegisters.clear()
        acpiRegisters.clear()
        
        // Reset IDE channels
        resetIDEChannel(0)
        resetIDEChannel(1)
        
        // Reset USB ports
        for (port in usbPorts) {
            port.connected = false
            port.speed = 1
        }
        
        // Reset DMA channels
        for (channel in dmaChannels) {
            channel.enabled = false
            channel.status = 0x00
        }
        
        // Clear interrupts
        pendingInterrupts.clear()
        enabledInterrupts.clear()
        
        // Reset device states
        nv2aEnabled = true
        apuEnabled = false
        ideEnabled = false
        usbEnabled = false
        lanEnabled = false
        smbusEnabled = false
        
        // Reinitialize registers
        initializeDeviceRegisters()
        
        Log.d(TAG, "MMIO system reset complete")
    }

    fun setDebugLogging(enabled: Boolean) {
        debugLogging = enabled
        Log.d(TAG, "MMIO debug logging ${if (enabled) "enabled" else "disabled"}")
    }

    /* ===============================
       Utility Methods
       =============================== */

    private fun findRegion(address: Int): MMIORegion? {
        for ((base, region) in regions) {
            val range = regionRanges[base]
            if (range != null && address in range) {
                return region
            }
        }
        return null
    }

    fun isMMIO(address: Int): Boolean {
        return findRegion(address) != null
    }

    fun getRegionInfo(): Map<String, String> {
        val info = mutableMapOf<String, String>()
        
        for ((base, region) in regions) {
            val range = regionRanges[base]
            if (range != null) {
                info[region.name] = "0x${range.first.toString(16)}-0x${range.last.toString(16)} " +
                                   "(${region.size / 1024}KB) " +
                                   "${if (region.isEnabled) "ENABLED" else "DISABLED"}"
            }
        }
        
        return info
    }

    fun getDeviceInfo(): Map<String, String> {
        val info = mutableMapOf<String, String>()
        
        info["NV2A"] = if (nv2aEnabled) "Enabled" else "Disabled"
        info["APU"] = if (apuEnabled) "Enabled" else "Disabled"
        info["IDE"] = if (ideEnabled) "Enabled" else "Disabled"
        info["USB"] = if (usbEnabled) "Enabled" else "Disabled"
        info["LAN"] = if (lanEnabled) "Enabled" else "Disabled"
        info["SMBus"] = if (smbusEnabled) "Enabled" else "Disabled"
        
        info["Pending Interrupts"] = pendingInterrupts.joinToString(", ")
        info["Enabled Interrupts"] = enabledInterrupts.joinToString(", ")
        
        info["PCI Devices"] = pciDevices.size.toString()
        
        return info
    }

    fun getRegisterDump(device: String): Map<String, String> {
        val dump = mutableMapOf<String, String>()
        
        when (device) {
            "NV2A" -> {
                for ((reg, value) in nv2aRegisters) {
                    dump["0x${reg.toString(16)}"] = "0x${value.toString(16)}"
                }
            }
            "APU" -> {
                for ((reg, value) in apuRegisters) {
                    dump["0x${reg.toString(16)}"] = "0x${value.toString(16)}"
                }
            }
            "SMBus" -> {
                for ((reg, value) in smbusRegisters) {
                    dump["0x${reg.toString(16)}"] = "0x${value.toString(16)}"
                }
            }
            "LAN" -> {
                for ((reg, value) in lanRegisters) {
                    dump["0x${reg.toString(16)}"] = "0x${value.toString(16)}"
                }
            }
        }
        
        return dump
    }

    /* ===============================
       Network Simulation
       =============================== */

    fun sendNetworkPacket(packet: ByteArray): Boolean {
        if (!lanEnabled) return false
        
        val networkPacket = NetworkPacket(packet, System.currentTimeMillis())
        txQueue.add(networkPacket)
        
        // Trigger interrupt
        lanRegisters[LAN_INTR] = lanRegisters[LAN_INTR]?.or(0x00000001) ?: 0x00000001
        if (enabledInterrupts.contains(IRQ_LAN)) {
            pendingInterrupts.add(IRQ_LAN)
        }
        
        Log.d(TAG, "Network packet sent: ${packet.size} bytes")
        return true
    }

    fun receiveNetworkPacket(): ByteArray? {
        if (!lanEnabled) return null
        
        if (rxQueue.isNotEmpty()) {
            val packet = rxQueue.removeAt(0)
            
            // Trigger interrupt
            lanRegisters[LAN_INTR] = lanRegisters[LAN_INTR]?.or(0x00000002) ?: 0x00000002
            if (enabledInterrupts.contains(IRQ_LAN)) {
                pendingInterrupts.add(IRQ_LAN)
            }
            
            return packet.data
        }
        
        return null
    }

    /* ===============================
       Timer Updates
       =============================== */

    fun update() {
        // Update timer
        val currentTime = System.currentTimeMillis()
        val delta = currentTime - lastTimerUpdate
        
        if (delta >= 16) { // ~60Hz
            lastTimerUpdate = currentTime
            
            // Trigger VBlank interrupt
            nv2aRegisters[NV_PCRTC_INTR_0] = nv2aRegisters[NV_PCRTC_INTR_0]?.or(0x00000001) ?: 0x00000001
            if (enabledInterrupts.contains(IRQ_NV2A)) {
                pendingInterrupts.add(IRQ_NV2A)
            }
            
            // Update timer value
            timerValue += 16666 // 16.666ms in microseconds
        }
    }

    /* ===============================
       DMA Operations
       =============================== */

    fun doDMATransfer(channel: Int): Boolean {
        if (channel < 0 || channel >= dmaChannels.size) return false
        
        val dmaChannel = dmaChannels[channel]
        if (!dmaChannel.enabled) return false
        
        // Simulate DMA transfer
        // In real implementation, would transfer data between memory and device
        
        dmaChannel.status = 0x00000001 // Transfer complete
        
        // Trigger interrupt based on channel
        when (channel) {
            0 -> triggerInterrupt(IRQ_NV2A) // Graphics DMA
            1 -> triggerInterrupt(IRQ_APU)  // Audio DMA
            2 -> triggerInterrupt(IRQ_IDE)  // IDE DMA
            3 -> triggerInterrupt(IRQ_USB)  // USB DMA
            4 -> triggerInterrupt(IRQ_LAN)  // Network DMA
        }
        
        Log.d(TAG, "DMA channel $channel transfer complete")
        return true
    }

    fun setDMAChannel(channel: Int, source: Int, dest: Int, count: Int, control: Int) {
        if (channel < 0 || channel >= dmaChannels.size) return
        
        val dmaChannel = dmaChannels[channel]
        dmaChannel.source = source
        dmaChannel.destination = dest
        dmaChannel.count = count
        dmaChannel.control = control
        dmaChannel.enabled = true
        dmaChannel.status = 0x00000000
        
        Log.d(TAG, "DMA channel $channel configured: src=0x${source.toString(16)}, " +
                   "dst=0x${dest.toString(16)}, count=$count")
    }

    fun getDMAChannelStatus(channel: Int): Int {
        if (channel < 0 || channel >= dmaChannels.size) return 0
        return dmaChannels[channel].status
    }

    /* ===============================
       Flash Operations
       =============================== */

    fun flashEraseSector(sector: Int): Boolean {
        if (sector * FLASH_PAGE_SIZE >= FLASH_SIZE) return false
        
        val start = sector * FLASH_PAGE_SIZE
        val end = start + FLASH_PAGE_SIZE
        
        for (i in start until end) {
            flashMemory.put(i, 0xFF.toByte())
        }
        
        Log.d(TAG, "Flash sector $sector erased (${FLASH_PAGE_SIZE} bytes)")
        return true
    }

    fun flashProgramPage(page: Int, data: ByteArray): Boolean {
        if (page * FLASH_PAGE_SIZE + data.size >= FLASH_SIZE) return false
        
        val offset = page * FLASH_PAGE_SIZE
        flashMemory.position(offset)
        flashMemory.put(data)
        
        Log.d(TAG, "Flash page $page programmed (${data.size} bytes)")
        return true
    }

    fun flashReadPage(page: Int): ByteArray {
        if (page * FLASH_PAGE_SIZE >= FLASH_SIZE) return ByteArray(0)
        
        val offset = page * FLASH_PAGE_SIZE
        val data = ByteArray(FLASH_PAGE_SIZE)
        
        flashMemory.position(offset)
        flashMemory.get(data)
        
        return data
    }

    /* ===============================
       Shutdown
       =============================== */

    fun shutdown() {
        Log.d(TAG, "Shutting down MMIO system...")
        
        // Disable all devices
        nv2aEnabled = false
        apuEnabled = false
        ideEnabled = false
        usbEnabled = false
        lanEnabled = false
        smbusEnabled = false
        
        // Clear all data structures
        regions.clear()
        regionRanges.clear()
        pciDevices.clear()
        pciConfigSpaces.clear()
        nv2aRegisters.clear()
        apuRegisters.clear()
        smbusRegisters.clear()
        lanRegisters.clear()
        acpiRegisters.clear()
        pendingInterrupts.clear()
        enabledInterrupts.clear()
        txQueue.clear()
        rxQueue.clear()
        
        Log.d(TAG, "MMIO system shutdown complete")
    }
}
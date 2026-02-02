package og.xaniteog

import android.util.Log
import kotlin.math.floor
import kotlin.math.pow
import kotlin.math.floor

class XboxCPU(
    val memory: XboxMemory,
    var kernel: XboxKernel? = null
) {

    companion object {
        private const val TAG = "XboxCPU"
        
        // CPU Flags
        const val CF = 1 shl 0    // Carry Flag
        const val PF = 1 shl 2    // Parity Flag
        const val AF = 1 shl 4    // Adjust Flag
        const val ZF = 1 shl 6    // Zero Flag
        const val SF = 1 shl 7    // Sign Flag
        const val TF = 1 shl 8    // Trap Flag
        const val IF = 1 shl 9    // Interrupt Enable
        const val DF = 1 shl 10   // Direction Flag
        const val OF = 1 shl 11   // Overflow Flag
        const val IOPL = 3 shl 12 // I/O Privilege Level
        const val NT = 1 shl 14   // Nested Task
        const val RF = 1 shl 16   // Resume Flag
        const val VM = 1 shl 17   // Virtual 8086 Mode
        const val AC = 1 shl 18   // Alignment Check
        const val VIF = 1 shl 19  // Virtual Interrupt Flag
        const val VIP = 1 shl 20  // Virtual Interrupt Pending
        const val ID = 1 shl 21   // ID Flag
        
        // Opcode constants
        private const val PREFIX_LOCK = 0xF0
        private const val PREFIX_REP = 0xF3
        private const val PREFIX_REPNE = 0xF2
        private const val PREFIX_SEG_CS = 0x2E
        private const val PREFIX_SEG_DS = 0x3E
        private const val PREFIX_SEG_ES = 0x26
        private const val PREFIX_SEG_FS = 0x64
        private const val PREFIX_SEG_GS = 0x65
        private const val PREFIX_SEG_SS = 0x36
        private const val PREFIX_OP_SIZE = 0x66
        private const val PREFIX_ADDR_SIZE = 0x67
        
        private const val TLB_SIZE = 64
    }

    /* ===============================
       General Purpose Registers
       =============================== */
    var eax = 0
    var ebx = 0
    var ecx = 0
    var edx = 0
    var esi = 0
    var edi = 0
    var ebp = 0
    var esp = 0

    /* ===============================
       Segment Registers (Xbox uses flat memory)
       =============================== */
    var cs = 0x08   // Code Segment
    var ds = 0x10   // Data Segment
    var es = 0x10   // Extra Segment
    var fs = 0x10   // Extra Segment
    var gs = 0x10   // Extra Segment
    var ss = 0x10   // Stack Segment

    /* ===============================
       Instruction Pointer & Flags
       =============================== */
    var eip = 0
    var eflags = 0x00000002  // Default flags (bit 1 always 1)

    /* ===============================
       Control Registers (Xbox specific)
       =============================== */
    var cr0 = 0x80000001  // Protected mode enabled
    var cr2 = 0
    var cr3 = 0
    var cr4 = 0

    /* ===============================
       Debug Registers
       =============================== */
    var dr0 = 0
    var dr1 = 0
    var dr2 = 0
    var dr3 = 0
    var dr6 = 0
    var dr7 = 0

    /* ===============================
       x87 FPU Registers
       =============================== */
    private val fpuRegs = DoubleArray(8)
    private var fpuTop = 0 // ST(0) is at this index
    private var fpuControlWord = 0x037F
    private var fpuStatusWord = 0
    private var fpuTagWord = 0xFFFF
    
    // MMX Registers (alias to FPU registers)
    private val mmxRegs = Array(8) { LongArray(1) }
    private var mmxState = false // True when MMX instructions are in use

    /* ===============================
       SSE Registers (Xbox supports SSE)
       =============================== */
    private val xmmRegs = Array(8) { FloatArray(4) }  // XMM0-XMM7
    private var mxcsr = 0x1F80  // Default MXCSR value

    /* ===============================
       CPU State
       =============================== */
    var running = false
    var halted = false
    var cycles = 0L
    var instructionsExecuted = 0L
    var lastOpcode = 0
    var lastPrefixes = mutableListOf<Int>()
    var lockPrefix = false
    var repPrefix = false
    var repnePrefix = false
    
    // Cache for performance
    private var currentSegment = 0x10  // Default DS
    private var operandSizeOverride = false
    private var addressSizeOverride = false

    /* ===============================
       Performance Counters
       =============================== */
    var timerTicks = 0L
    var branchCount = 0L
    var interruptCount = 0L
    var syscallCount = 0L
    var fpuInstructions = 0L
    var mmxInstructions = 0L
    var sseInstructions = 0L
    var cacheHits = 0L
    var cacheMisses = 0L
    var pageFaults = 0L

  /* ===============================
   TLB (Translation Lookaside Buffer)
   =============================== */
private data class TLBEntry(
    val virtual: Int,
    val physical: Int,
    val accessed: Boolean,
    val dirty: Boolean,
    var lastAccess: Long = System.currentTimeMillis()
)

private val tlb = mutableMapOf<Int, TLBEntry>()


/* ===============================
   Instruction Cache
   =============================== */
private data class CacheLine(
    val tag: Int,
    val data: ByteArray,
    var valid: Boolean,   // ✅ صارت var
    var lastAccess: Long
)

private val instructionCache = Array(256) { CacheLine(0, ByteArray(32), false, 0L) }    


    /* ===============================
       Fetch Operations (مُحسنة)
       =============================== */

    private fun fetch8(): Int {
        val virtualAddr = eip
        val physicalAddr = translateAddress(virtualAddr)
        val value = memory.read8(physicalAddr)
        eip = (eip + 1) and 0xFFFFFFFF.toInt()
        return value
    }

    private fun fetch16(): Int {
        val virtualAddr = eip
        val physicalAddr = translateAddress(virtualAddr)
        val value = memory.read16(physicalAddr)
        eip = (eip + 2) and 0xFFFFFFFF.toInt()
        return value
    }

    private fun fetch32(): Int {
        val virtualAddr = eip
        val physicalAddr = translateAddress(virtualAddr)
        val value = memory.read32(physicalAddr)
        eip = (eip + 4) and 0xFFFFFFFF.toInt()
        return value
    }

    private fun fetchSigned8(): Int {
        val value = fetch8()
        return if (value and 0x80 != 0) value or 0xFFFFFF00.toInt() else value
    }

    private fun fetchSigned16(): Int {
        val value = fetch16()
        return if (value and 0x8000 != 0) value or 0xFFFF0000.toInt() else value
    }

    private fun fetchSigned32(): Int {
        return fetch32()
    }

    private fun fetch64(): Long {
        val low = fetch32().toLong() and 0xFFFFFFFFL
        val high = fetch32().toLong() and 0xFFFFFFFFL
        return (high shl 32) or low
    }

    /* ===============================
       Address Translation (TLB)
       =============================== */

    private fun translateAddress(virtual: Int): Int {
        // Check TLB first
        val tlbKey = virtual shr 12  // Page number
        val tlbEntry = tlb[tlbKey]
        
        if (tlbEntry != null && tlbEntry.accessed) {
            cacheHits++
            return (tlbEntry.physical and 0xFFFFF000.toInt()) or (virtual and 0xFFF)
        }
        
        cacheMisses++
        
        // Page table lookup (simplified)
        val pdeAddr = (cr3 and 0xFFFFF000.toInt()) or ((virtual shr 22) and 0x3FC)
        val pde = memory.read32(pdeAddr)
        
        if (pde and 1 == 0) {
            pageFaults++
            handlePageFault(virtual)
            return translateAddress(virtual) // Retry after handling
        }
        
        val pteAddr = (pde and 0xFFFFF000.toInt()) or ((virtual shr 12) and 0x3FC)
        val pte = memory.read32(pteAddr)
        
        if (pte and 1 == 0) {
            pageFaults++
            handlePageFault(virtual)
            return translateAddress(virtual)
        }
        
        val physical = (pte and 0xFFFFF000.toInt()) or (virtual and 0xFFF)
        
        // Update TLB
        val newEntry = TLBEntry(
            virtual = tlbKey,
            physical = physical,
            accessed = true,
            dirty = (pte and 0x40) != 0
        )
        
        if (tlb.size >= TLB_SIZE) {
            // Remove oldest entry
            val oldest = tlb.minByOrNull { it.value.lastAccess }
            if (oldest != null) {
                tlb.remove(oldest.key)
            }
        }
        
        tlb[tlbKey] = newEntry
        
        return physical
    }

    private fun handlePageFault(virtual: Int) {
        cr2 = virtual
        // In a real CPU, this would raise exception 14
        Log.w(TAG, "Page fault at virtual address 0x${virtual.toString(16)}")
    }

    /* ===============================
       Instruction Cache Operations
       =============================== */

    private fun fetchFromCache(address: Int): ByteArray? {
        val cacheIndex = (address shr 5) and 0xFF  // 256 lines
        val tag = address and 0xFFFFFE00.toInt()   // Tag
        
        val line = instructionCache[cacheIndex]
        if (line.valid && line.tag == tag) {
            cacheHits++
            line.lastAccess = cycles
            return line.data.copyOf()
        }
        
        cacheMisses++
        return null
    }

    private fun fillCache(address: Int, data: ByteArray) {
        val cacheIndex = (address shr 5) and 0xFF
        val tag = address and 0xFFFFFE00.toInt()
        
        instructionCache[cacheIndex] = CacheLine(
            tag = tag,
            data = data.copyOf(),
            valid = true,
            lastAccess = cycles
        )
    }

 /* ===============================
   Flag Operations (مُحسنة)
   =============================== */

fun setFlag(flag: Int, value: Boolean) {
    if (value) {
        eflags = eflags or flag
    } else {
        eflags = eflags and flag.inv()
    }
}

fun execute(cycles: Int) {
    // تنفيذ تعليمات CPU
    for (i in 0 until cycles) {
        // fetch, decode, execute...
    }
}

fun run(cycles: Int) {
    // كود الدالة run
    execute(cycles)  // أو تنفيذ مختلف
}

fun getFlag(flag: Int): Boolean {
    return (eflags and flag) != 0
}

fun updateZeroFlag(result: Int) {
    setFlag(ZF, result == 0)
}

fun updateSignFlag32(result: Int) {
    setFlag(SF, (result and 0x80000000.toInt()) != 0)
}

fun updateSignFlag16(result: Int) {
    setFlag(SF, (result and 0x8000) != 0)
}

fun updateSignFlag8(result: Int) {
    setFlag(SF, (result and 0x80) != 0)
}

fun updateParityFlag(value: Int) {
    var bits = value and 0xFF
    var count = 0
    for (i in 0 until 8) {
        if ((bits and 1) != 0) count++
        bits = bits shr 1
    }
    setFlag(PF, (count and 1) == 0)
}

fun updateCarryFlagAdd(op1: Int, op2: Int, result: Long) {
    setFlag(CF, (result and 0x100000000L) != 0L)
}

fun updateCarryFlagSub(op1: Int, op2: Int, result: Long) {
    setFlag(CF, (op1.toLong() and 0xFFFFFFFFL) < (op2.toLong() and 0xFFFFFFFFL))
}

fun updateOverflowFlagAdd(op1: Int, op2: Int, result: Long) {
    val sign1 = op1 and 0x80000000.toInt()
    val sign2 = op2 and 0x80000000.toInt()
    val signr = (result and 0x80000000L).toInt()
    setFlag(OF, (sign1 == sign2) && (sign1 != signr))
}

fun updateOverflowFlagSub(op1: Int, op2: Int, result: Long) {
    val sign1 = op1 and 0x80000000.toInt()
    val sign2 = op2 and 0x80000000.toInt()
    val signr = (result and 0x80000000L).toInt()
    setFlag(OF, (sign1 != sign2) && (sign1 != signr))
}

fun updateAuxiliaryFlagAdd(op1: Int, op2: Int) {
    setFlag(AF, ((op1 xor op2 xor (op1 + op2)) and 0x10) != 0)
}

fun updateAuxiliaryFlagSub(op1: Int, op2: Int) {
    setFlag(AF, ((op1 xor op2 xor (op1 - op2)) and 0x10) != 0)
}

fun updateFlagsLogic(result: Int) {
    setFlag(CF, false)
    setFlag(OF, false)
    updateZeroFlag(result)
    updateSignFlag32(result)
    updateParityFlag(result)
}

    /* ===============================
       Stack Operations (مُحسنة)
       =============================== */

    private fun push8(value: Int) {
        esp = (esp - 1) and 0xFFFFFFFF.toInt()
        memory.write8(esp, value)
    }

    private fun push16(value: Int) {
        esp = (esp - 2) and 0xFFFFFFFF.toInt()
        memory.write16(esp, value)
    }

    private fun push32(value: Int) {
        esp = (esp - 4) and 0xFFFFFFFF.toInt()
        memory.write32(esp, value)
    }

    private fun push64(value: Long) {
        push32((value shr 32).toInt())
        push32(value.toInt())
    }

    private fun pop8(): Int {
        val value = memory.read8(esp)
        esp = (esp + 1) and 0xFFFFFFFF.toInt()
        return value
    }

    private fun pop16(): Int {
        val value = memory.read16(esp)
        esp = (esp + 2) and 0xFFFFFFFF.toInt()
        return value
    }

    private fun pop32(): Int {
        val value = memory.read32(esp)
        esp = (esp + 4) and 0xFFFFFFFF.toInt()
        return value
    }

    private fun pop64(): Long {
        val low = pop32().toLong() and 0xFFFFFFFFL
        val high = pop32().toLong() and 0xFFFFFFFFL
        return (high shl 32) or low
    }

    /* ===============================
       x87 FPU Operations (مُحسنة)
       =============================== */

    private fun fpuGetReg(reg: Int): Double {
        return fpuRegs[(fpuTop + reg) and 7]
    }

    private fun fpuSetReg(reg: Int, value: Double) {
        fpuRegs[(fpuTop + reg) and 7] = value
        // Mark register as valid (tag = 00)
        val bitPos = ((fpuTop + reg) and 7) * 2
        fpuTagWord = fpuTagWord and (3 shl bitPos).inv()
    }

    private fun fpuPush(value: Double) {
        fpuTop = (fpuTop - 1) and 7
        fpuSetReg(0, value)
    }

    private fun fpuPop() {
        // Mark register as empty (tag = 11)
        val bitPos = fpuTop * 2
        fpuTagWord = fpuTagWord or (3 shl bitPos)
        fpuTop = (fpuTop + 1) and 7
    }

    private fun updateFpuStatus() {
        // Simplified status update
        fpuStatusWord = (fpuTop shl 11)
    }

    private fun handleFld(operand: Int) {
        val modRM = operand
        val ea = calculateEA(modRM)
        val value = memory.readFloat(ea)
        fpuPush(value.toDouble())
        fpuInstructions++
        cycles += 3
    }

    private fun handleFst(operand: Int) {
        val modRM = operand
        val ea = calculateEA(modRM)
        val value = fpuGetReg(0)
        memory.writeFloat(ea, value.toFloat())
        fpuInstructions++
        cycles += 3
    }

    private fun handleFstp(operand: Int) {
        handleFst(operand)
        fpuPop()
    }

    private fun handleFadd(operand: Int) {
        when (operand) {
            0xC0, 0xC1 -> { // FADD ST(0), ST(i)
                val i = operand and 7
                val result = fpuGetReg(0) + fpuGetReg(i)
                fpuSetReg(0, result)
            }
            0xC8, 0xC9, 0xCA, 0xCB, 0xCC, 0xCD, 0xCE, 0xCF -> { // FADD ST(i), ST(0)
                val i = operand and 7
                val result = fpuGetReg(i) + fpuGetReg(0)
                fpuSetReg(i, result)
            }
        }
        fpuInstructions++
        cycles += 5
    }

    private fun handleFaddp(operand: Int) {
        handleFadd(operand)
        fpuPop()
    }

    private fun handleFsub(operand: Int) {
        when (operand) {
            0xE8, 0xE9, 0xEA, 0xEB, 0xEC, 0xED, 0xEE, 0xEF -> { // FSUB ST(i), ST(0)
                val i = operand and 7
                val result = fpuGetReg(i) - fpuGetReg(0)
                fpuSetReg(i, result)
            }
        }
        fpuInstructions++
        cycles += 5
    }

    private fun handleFsubp(operand: Int) {
        handleFsub(operand)
        fpuPop()
    }

    private fun handleFmul(operand: Int) {
        when (operand) {
            0xC8, 0xC9, 0xCA, 0xCB, 0xCC, 0xCD, 0xCE, 0xCF -> { // FMUL ST(i), ST(0)
                val i = operand and 7
                val result = fpuGetReg(i) * fpuGetReg(0)
                fpuSetReg(i, result)
            }
        }
        fpuInstructions++
        cycles += 5
    }

    private fun handleFmulp(operand: Int) {
        handleFmul(operand)
        fpuPop()
    }

    private fun handleFdiv(operand: Int) {
        when (operand) {
            0xF8, 0xF9, 0xFA, 0xFB, 0xFC, 0xFD, 0xFE, 0xFF -> { // FDIV ST(i), ST(0)
                val i = operand and 7
                val divisor = fpuGetReg(0)
                if (divisor == 0.0) {
                    // Handle divide by zero
                    fpuStatusWord = fpuStatusWord or 0x0100
                    fpuSetReg(i, Double.POSITIVE_INFINITY)
                } else {
                    val result = fpuGetReg(i) / divisor
                    fpuSetReg(i, result)
                }
            }
        }
        fpuInstructions++
        cycles += 73  // Division is slow
    }

    private fun handleFdivp(operand: Int) {
        handleFdiv(operand)
        fpuPop()
    }

    private fun handleFcom(operand: Int) {
        val value = fpuGetReg(0)
        val compareWith = fpuGetReg(operand and 7)
        
        // Clear comparison flags in status word
        fpuStatusWord = fpuStatusWord and 0xFCFF
        
        when {
            value.isNaN() || compareWith.isNaN() -> {
                fpuStatusWord = fpuStatusWord or 0x4500 // Invalid operation
            }
            value == compareWith -> {
                fpuStatusWord = fpuStatusWord or 0x4000 // Equal
            }
            value < compareWith -> {
                fpuStatusWord = fpuStatusWord or 0x0100 // Less than
            }
            value > compareWith -> {
                fpuStatusWord = fpuStatusWord or 0x0000 // Greater than
            }
        }
        fpuInstructions++
        cycles += 4
    }

    private fun handleFcomp(operand: Int) {
        handleFcom(operand)
        fpuPop()
    }

    private fun handleFcompp() {
        handleFcom(1)
        fpuPop()
        fpuPop()
    }

    private fun handleFstsw() {
        eax = fpuStatusWord and 0xFFFF
        fpuInstructions++
        cycles += 3
    }

    private fun handleFldcw(operand: Int) {
        val modRM = operand
        val ea = calculateEA(modRM)
        fpuControlWord = memory.read16(ea).toInt()
        fpuInstructions++
        cycles += 4
    }

    private fun handleFstcw(operand: Int) {
        val modRM = operand
        val ea = calculateEA(modRM)
        memory.write16(ea, fpuControlWord)
        fpuInstructions++
        cycles += 4
    }

    private fun handleFinit() {
        fpuRegs.fill(0.0)
        fpuTop = 0
        fpuControlWord = 0x037F
        fpuStatusWord = 0
        fpuTagWord = 0xFFFF
        fpuInstructions++
        cycles += 17
    }

    /* ===============================
       MMX Operations (مُحسنة)
       =============================== */

    private fun getMmxReg(reg: Int): LongArray {
        return mmxRegs[reg]
    }

    private fun setMmxReg(reg: Int, value: LongArray) {
        mmxRegs[reg] = value.copyOf()
        // When MMX instruction is used, mark that MMX state is active
        mmxState = true
    }

    private fun handleMovq(operand: Int) {
        val modRM = operand
        if ((modRM shr 6) and 0x03 == 3) {
            // Register to register
            val dstReg = (modRM shr 3) and 0x07
            val srcReg = modRM and 0x07
            setMmxReg(dstReg, getMmxReg(srcReg))
        } else {
            // Memory to register
            val dstReg = (modRM shr 3) and 0x07
            val ea = calculateEA(modRM)
            val low = memory.read32(ea).toLong() and 0xFFFFFFFFL
            val high = memory.read32(ea + 4).toLong() and 0xFFFFFFFFL
            val value = longArrayOf((high shl 32) or low)
            setMmxReg(dstReg, value)
        }
        mmxInstructions++
        cycles += 2
    }

    private fun handleMovd(operand: Int) {
        val modRM = operand
        if ((modRM shr 6) and 0x03 == 3) {
            // Register to register
            val dstReg = (modRM shr 3) and 0x07
            val srcReg = modRM and 0x07
            val srcValue = getReg32(srcReg).toLong() and 0xFFFFFFFFL
            setMmxReg(dstReg, longArrayOf(srcValue))
        } else {
            // Memory to register
            val dstReg = (modRM shr 3) and 0x07
            val ea = calculateEA(modRM)
            val value = memory.read32(ea).toLong() and 0xFFFFFFFFL
            setMmxReg(dstReg, longArrayOf(value))
        }
        mmxInstructions++
        cycles += 2
    }

    private fun handlePaddb(operand: Int) {
        val modRM = operand
        val dstReg = (modRM shr 3) and 0x07
        val srcReg = modRM and 0x07
        val dst = getMmxReg(dstReg)[0]
        val src = getMmxReg(srcReg)[0]
        
        var result = 0L
        for (i in 0 until 8) {
            val byteMask = 0xFFL shl (i * 8)
            val dstByte = (dst and byteMask) shr (i * 8)
            val srcByte = (src and byteMask) shr (i * 8)
            val sum = (dstByte + srcByte) and 0xFFL
            result = result or (sum shl (i * 8))
        }
        
        setMmxReg(dstReg, longArrayOf(result))
        mmxInstructions++
        cycles += 2
    }

    private fun handlePaddw(operand: Int) {
        val modRM = operand
        val dstReg = (modRM shr 3) and 0x07
        val srcReg = modRM and 0x07
        val dst = getMmxReg(dstReg)[0]
        val src = getMmxReg(srcReg)[0]
        
        var result = 0L
        for (i in 0 until 4) {
            val wordMask = 0xFFFFL shl (i * 16)
            val dstWord = (dst and wordMask) shr (i * 16)
            val srcWord = (src and wordMask) shr (i * 16)
            val sum = (dstWord + srcWord) and 0xFFFFL
            result = result or (sum shl (i * 16))
        }
        
        setMmxReg(dstReg, longArrayOf(result))
        mmxInstructions++
        cycles += 2
    }

    private fun handlePaddd(operand: Int) {
        val modRM = operand
        val dstReg = (modRM shr 3) and 0x07
        val srcReg = modRM and 0x07
        val dst = getMmxReg(dstReg)[0]
        val src = getMmxReg(srcReg)[0]
        
        var result = 0L
        for (i in 0 until 2) {
            val dwordMask = 0xFFFFFFFFL shl (i * 32)
            val dstDword = (dst and dwordMask) shr (i * 32)
            val srcDword = (src and dwordMask) shr (i * 32)
            val sum = (dstDword + srcDword) and 0xFFFFFFFFL
            result = result or (sum shl (i * 32))
        }
        
        setMmxReg(dstReg, longArrayOf(result))
        mmxInstructions++
        cycles += 2
    }

    private fun handlePsubb(operand: Int) {
        val modRM = operand
        val dstReg = (modRM shr 3) and 0x07
        val srcReg = modRM and 0x07
        val dst = getMmxReg(dstReg)[0]
        val src = getMmxReg(srcReg)[0]
        
        var result = 0L
        for (i in 0 until 8) {
            val byteMask = 0xFFL shl (i * 8)
            val dstByte = (dst and byteMask) shr (i * 8)
            val srcByte = (src and byteMask) shr (i * 8)
            val diff = (dstByte - srcByte) and 0xFFL
            result = result or (diff shl (i * 8))
        }
        
        setMmxReg(dstReg, longArrayOf(result))
        mmxInstructions++
        cycles += 2
    }

    private fun handlePsubw(operand: Int) {
        val modRM = operand
        val dstReg = (modRM shr 3) and 0x07
        val srcReg = modRM and 0x07
        val dst = getMmxReg(dstReg)[0]
        val src = getMmxReg(srcReg)[0]
        
        var result = 0L
        for (i in 0 until 4) {
            val wordMask = 0xFFFFL shl (i * 16)
            val dstWord = (dst and wordMask) shr (i * 16)
            val srcWord = (src and wordMask) shr (i * 16)
            val diff = (dstWord - srcWord) and 0xFFFFL
            result = result or (diff shl (i * 16))
        }
        
        setMmxReg(dstReg, longArrayOf(result))
        mmxInstructions++
        cycles += 2
    }

    private fun handlePsubd(operand: Int) {
        val modRM = operand
        val dstReg = (modRM shr 3) and 0x07
        val srcReg = modRM and 0x07
        val dst = getMmxReg(dstReg)[0]
        val src = getMmxReg(srcReg)[0]
        
        var result = 0L
        for (i in 0 until 2) {
            val dwordMask = 0xFFFFFFFFL shl (i * 32)
            val dstDword = (dst and dwordMask) shr (i * 32)
            val srcDword = (src and dwordMask) shr (i * 32)
            val diff = (dstDword - srcDword) and 0xFFFFFFFFL
            result = result or (diff shl (i * 32))
        }
        
        setMmxReg(dstReg, longArrayOf(result))
        mmxInstructions++
        cycles += 2
    }

    private fun handlePand(operand: Int) {
        val modRM = operand
        val dstReg = (modRM shr 3) and 0x07
        val srcReg = modRM and 0x07
        val dst = getMmxReg(dstReg)[0]
        val src = getMmxReg(srcReg)[0]
        val result = dst and src
        setMmxReg(dstReg, longArrayOf(result))
        mmxInstructions++
        cycles += 2
    }

    private fun handlePor(operand: Int) {
        val modRM = operand
        val dstReg = (modRM shr 3) and 0x07
        val srcReg = modRM and 0x07
        val dst = getMmxReg(dstReg)[0]
        val src = getMmxReg(srcReg)[0]
        val result = dst or src
        setMmxReg(dstReg, longArrayOf(result))
        mmxInstructions++
        cycles += 2
    }

    private fun handlePxor(operand: Int) {
        val modRM = operand
        val dstReg = (modRM shr 3) and 0x07
        val srcReg = modRM and 0x07
        val dst = getMmxReg(dstReg)[0]
        val src = getMmxReg(srcReg)[0]
        val result = dst xor src
        setMmxReg(dstReg, longArrayOf(result))
        mmxInstructions++
        cycles += 2
    }

    private fun handlePcmp(operand: Int)  {
        val modRM = operand
        val dstReg = (modRM shr 3) and 0x07
        val srcReg = modRM and 0x07
        val dst = getMmxReg(dstReg)[0]
        val src = getMmxReg(srcReg)[0]
        
        var result = 0L
        for (i in 0 until 8) {
            val byteMask = 0xFFL shl (i * 8)
            val dstByte = (dst and byteMask) shr (i * 8)
            val srcByte = (src and byteMask) shr (i * 8)
            val mask = if (dstByte == srcByte) 0xFFL else 0x00L
            result = result or (mask shl (i * 8))
        }
        
        setMmxReg(dstReg, longArrayOf(result))
        mmxInstructions++
        cycles += 2
    }

    private fun handlePacksswb(operand: Int) {
        val modRM = operand
        val dstReg = (modRM shr 3) and 0x07
        val srcReg = modRM and 0x07
        val dst = getMmxReg(dstReg)[0]
        val src = getMmxReg(srcReg)[0]
        
        var result = 0L
        for (i in 0 until 8) {
            val wordMask = 0xFFFFL shl (i * 16)
            val word = (src and wordMask) shr (i * 16)
            val saturated = when {
                word > 127 -> 127L
                word < -128 -> -128L
                else -> word and 0xFFL
            }
            result = result or (saturated shl (i * 8))
        }
        
        setMmxReg(dstReg, longArrayOf(result))
        mmxInstructions++
        cycles += 3
    }

    private fun handleEmms() {
        // Exit MMX state
        mmxState = false
        mmxInstructions++
        cycles += 2
    }

    /* ===============================
       SSE Operations (لـ Xbox)
       =============================== */

    private fun getXmmReg(reg: Int): FloatArray {
        return xmmRegs[reg]
    }

    private fun setXmmReg(reg: Int, value: FloatArray) {
        xmmRegs[reg] = value.copyOf()
    }

    private fun handleMovaps(operand: Int) {
        val modRM = operand
        if ((modRM shr 6) and 0x03 == 3) {
            // Register to register
            val dstReg = (modRM shr 3) and 0x07
            val srcReg = modRM and 0x07
            setXmmReg(dstReg, getXmmReg(srcReg))
        } else {
            // Memory to register
            val dstReg = (modRM shr 3) and 0x07
            val ea = calculateEA(modRM)
            val value = FloatArray(4)
            for (i in 0 until 4) {
                value[i] = memory.readFloat(ea + i * 4)
            }
            setXmmReg(dstReg, value)
        }
        sseInstructions++
        cycles += 3
    }

    private fun handleAddps(operand: Int) {
        val modRM = operand
        val dstReg = (modRM shr 3) and 0x07
        val srcReg = modRM and 0x07
        val dst = getXmmReg(dstReg)
        val src = getXmmReg(srcReg)
        val result = FloatArray(4)
        
        for (i in 0 until 4) {
            result[i] = dst[i] + src[i]
        }
        
        setXmmReg(dstReg, result)
        sseInstructions++
        cycles += 4
    }

    private fun handleMulps(operand: Int) {
        val modRM = operand
        val dstReg = (modRM shr 3) and 0x07
        val srcReg = modRM and 0x07
        val dst = getXmmReg(dstReg)
        val src = getXmmReg(srcReg)
        val result = FloatArray(4)
        
        for (i in 0 until 4) {
            result[i] = dst[i] * src[i]
        }
        
        setXmmReg(dstReg, result)
        sseInstructions++
        cycles += 5
    }

    private fun handleLdmxcsr(operand: Int) {
        val modRM = operand
        val ea = calculateEA(modRM)
        mxcsr = memory.read32(ea)
        sseInstructions++
        cycles += 3
    }

    private fun handleStmxcsr(operand: Int) {
        val modRM = operand
        val ea = calculateEA(modRM)
        memory.write32(ea, mxcsr)
        sseInstructions++
        cycles += 3
    }

    /* ===============================
       Execution Core (محسّنة جداً)
       =============================== */

    fun step(): Int {
        if (!running || halted) return 0
        
        val startEip = eip
        
        // Handle prefixes
        lastPrefixes.clear()
        lockPrefix = false
        repPrefix = false
        repnePrefix = false
        operandSizeOverride = false
        addressSizeOverride = false
        currentSegment = ds
        
        var prefix: Int
        do {
            prefix = fetch8()
            when (prefix) {
                PREFIX_LOCK -> {
                    lockPrefix = true
                    lastPrefixes.add(prefix)
                }
                PREFIX_REP -> {
                    repPrefix = true
                    lastPrefixes.add(prefix)
                }
                PREFIX_REPNE -> {
                    repnePrefix = true
                    lastPrefixes.add(prefix)
                }
                PREFIX_OP_SIZE -> {
                    operandSizeOverride = true
                    lastPrefixes.add(prefix)
                }
                PREFIX_ADDR_SIZE -> {
                    addressSizeOverride = true
                    lastPrefixes.add(prefix)
                }
                PREFIX_SEG_CS -> { currentSegment = cs; lastPrefixes.add(prefix) }
                PREFIX_SEG_DS -> { currentSegment = ds; lastPrefixes.add(prefix) }
                PREFIX_SEG_ES -> { currentSegment = es; lastPrefixes.add(prefix) }
                PREFIX_SEG_FS -> { currentSegment = fs; lastPrefixes.add(prefix) }
                PREFIX_SEG_GS -> { currentSegment = gs; lastPrefixes.add(prefix) }
                PREFIX_SEG_SS -> { currentSegment = ss; lastPrefixes.add(prefix) }
                else -> {
                    // Not a prefix, put it back
                    eip = (eip - 1) and 0xFFFFFFFF.toInt()
                }
            }
        } while (prefix in listOf(
            PREFIX_LOCK, PREFIX_REP, PREFIX_REPNE,
            PREFIX_OP_SIZE, PREFIX_ADDR_SIZE,
            PREFIX_SEG_CS, PREFIX_SEG_DS, PREFIX_SEG_ES,
            PREFIX_SEG_FS, PREFIX_SEG_GS, PREFIX_SEG_SS
        ))
        
        lastOpcode = fetch8()
        var cyclesUsed = 1
        
        try {
            when (lastOpcode) {
            
                0x90 -> {
                    cyclesUsed = 1
                }

                /* ---------- MOV reg8, imm8, HDD32 ---------- */
                0xB0, 0xB1, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6, 0xB7 -> {
                    val imm = fetch8()
                    when (lastOpcode - 0xB0) {
                        0 -> eax = (eax and 0xFFFFFF00.toInt()) or (imm and 0xFF)
                        1 -> ecx = (ecx and 0xFFFFFF00.toInt()) or (imm and 0xFF)
                        2 -> edx = (edx and 0xFFFFFF00.toInt()) or (imm and 0xFF)
                        3 -> ebx = (ebx and 0xFFFFFF00.toInt()) or (imm and 0xFF)
                        4 -> eax = (eax and 0xFFFF00FF.toInt()) or ((imm and 0xFF) shl 8)
                        5 -> ecx = (ecx and 0xFFFF00FF.toInt()) or ((imm and 0xFF) shl 8)
                        6 -> edx = (edx and 0xFFFF00FF.toInt()) or ((imm and 0xFF) shl 8)
                        7 -> ebx = (ebx and 0xFFFF00FF.toInt()) or ((imm and 0xFF) shl 8)
                    }
                    cyclesUsed = 5
                }

                /* ---------- MOV reg32, imm32, rsg32 ---------- */
                0xB8, 0xB9, 0xBA, 0xBB, 0xBC, 0xBD, 0xBE, 0xBF -> {
                    val imm = fetch32()
                    when (lastOpcode - 0xB8) {
                        0 -> eax = imm
                        1 -> ecx = imm
                        2 -> edx = imm
                        3 -> ebx = imm
                        4 -> esp = imm
                        5 -> ebp = imm
                        6 -> esi = imm
                        7 -> edi = imm
                    }
                    cyclesUsed = 5
                }

                /* ---------- MOV r/m8, r8 ---------- */
                0x88 -> {
                    val modRM = fetch8()
                    handleMov8(modRM)
                    cyclesUsed = 2
                }

                /* ---------- MOV r/m32, r32 ---------- */
                0x89 -> {
                    val modRM = fetch8()
                    handleMov32(modRM)
                    cyclesUsed = 2
                }

                /* ---------- MOV r8, r/m8 ---------- */
                0x8A -> {
                    val modRM = fetch8()
                    handleMovR8(modRM)
                    cyclesUsed = 2
                }

                /* ---------- MOV r32, r/m32 ---------- */
                0x8B -> {
                    val modRM = fetch8()
                    handleMovR32(modRM)
                    cyclesUsed = 2
                }

                /* ---------- MOV r/m32, imm32 ---------- */
                0xC7 -> {
                    val modRM = fetch8()
                    val imm = fetch32()
                    writeOperand32(modRM, imm)
                    cyclesUsed = 6
                }

                /* ---------- ADD ---------- */
                0x01 -> {
                    val modRM = fetch8()
                    handleAdd32(modRM)
                    cyclesUsed = 2
                }
                
                0x03 -> {
                    val modRM = fetch8()
                    handleAddR32(modRM)
                    cyclesUsed = 2
                }
                
                0x04 -> {
                    val imm = fetch8()
                    handleAddALImm8(imm)
                    cyclesUsed = 2
                }
                
                0x05 -> {
                    val imm = fetch32()
                    handleAddEAXImm32(imm)
                    cyclesUsed = 2
                }

                /* ---------- SUB ---------- */
                0x29 -> {
                    val modRM = fetch8()
                    handleSub32(modRM)
                    cyclesUsed = 2
                }
                
                0x2B -> {
                    val modRM = fetch8()
                    handleSubR32(modRM)
                    cyclesUsed = 2
                }
                
                0x2C -> {
                    val imm = fetch8()
                    handleSubALImm8(imm)
                    cyclesUsed = 2
                }
                
                0x2D -> {
                    val imm = fetch32()
                    handleSubEAXImm32(imm)
                    cyclesUsed = 2
                }

                /* ---------- CMP ---------- */
                0x39 -> {
                    val modRM = fetch8()
                    handleCmp32(modRM)
                    cyclesUsed = 2
                }
                
                0x3B -> {
                    val modRM = fetch8()
                    handleCmpR32(modRM)
                    cyclesUsed = 2
                }
                
                0x3C -> {
                    val imm = fetch8()
                    handleCmpALImm8(imm)
                    cyclesUsed = 2
                }
                
                0x3D -> {
                    val imm = fetch32()
                    handleCmpEAXImm32(imm)
                    cyclesUsed = 2
                }

                /* ---------- INC reg32 ---------- */
                0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47 -> {
                    val reg = lastOpcode - 0x40
                    val oldValue = getReg32(reg)
                    val result = oldValue.toLong() + 1L
                    setReg32(reg, result.toInt())
                    
                    updateZeroFlag(result.toInt())
                    updateSignFlag32(result.toInt())
                    updateOverflowFlagAdd(oldValue, 1, result)
                    updateAuxiliaryFlagAdd(oldValue, 1)
                    cyclesUsed = 1
                }

                /* ---------- DEC reg32 ---------- */
                0x48, 0x49, 0x4A, 0x4B, 0x4C, 0x4D, 0x4E, 0x4F -> {
                    val reg = lastOpcode - 0x48
                    val oldValue = getReg32(reg)
                    val result = oldValue.toLong() - 1L
                    setReg32(reg, result.toInt())
                    
                    updateZeroFlag(result.toInt())
                    updateSignFlag32(result.toInt())
                    updateOverflowFlagSub(oldValue, 1, result)
                    updateAuxiliaryFlagSub(oldValue, 1)
                    cyclesUsed = 1
                }

                /* ---------- PUSH reg32 ---------- */
                0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57 -> {
                    val reg = lastOpcode - 0x50
                    push32(getReg32(reg))
                    cyclesUsed = 1
                }

                /* ---------- POP reg32 ---------- */
                0x58, 0x59, 0x5A, 0x5B, 0x5C, 0x5D, 0x5E, 0x5F -> {
                    val reg = lastOpcode - 0x58
                    val value = pop32()
                    setReg32(reg, value)
                    cyclesUsed = 1
                }

                /* ---------- PUSH imm8 ---------- */
                0x6A -> {
                    val imm = fetchSigned8()
                    push32(imm)
                    cyclesUsed = 1
                }

                /* ---------- PUSH imm32 ---------- */
                0x68 -> {
                    val imm = fetch32()
                    push32(imm)
                    cyclesUsed = 1
                }

                /* ---------- JMP rel8 ---------- */
                0xEB -> {
                    val offset = fetchSigned8()
                    eip = (eip + offset) and 0xFFFFFFFF.toInt()
                    branchCount++
                    cyclesUsed = 5
                }

                /* ---------- JMP rel32 ---------- */
                0xE9 -> {
                    val offset = fetchSigned32()
                    eip = (eip + offset) and 0xFFFFFFFF.toInt()
                    branchCount++
                    cyclesUsed = 5
                }

                /* ---------- JMP r/m32 ---------- */
                0xFF -> {
                    val modRM = fetch8()
                    if ((modRM shr 3) and 0x07 == 4) { // JMP r/m32
                        val target = readOperand32(modRM)
                        eip = target
                        branchCount++
                        cyclesUsed = 5
                    } else {
                        handleUnknownOpcode(lastOpcode)
                    }
                }

                /* ---------- CALL rel32 ---------- */
                0xE8 -> {
                    val offset = fetchSigned32()
                    push32(eip)
                    eip = (eip + offset) and 0xFFFFFFFF.toInt()
                    branchCount++
                    cyclesUsed = 7
                }

                /* ---------- CALL r/m32 ---------- */
                0xFF -> {
                    val modRM = fetch8()
                    if ((modRM shr 3) and 0x07 == 2) { // CALL r/m32
                        val target = readOperand32(modRM)
                        push32(eip)
                        eip = target
                        branchCount++
                        cyclesUsed = 7
                    }
                }

                /* ---------- RET ---------- */
                0xC3 -> {
                    eip = pop32()
                    branchCount++
                    cyclesUsed = 6
                }

                /* ---------- RET imm16 ---------- */
                0xC2 -> {
                    val imm = fetch16()
                    eip = pop32()
                    esp = (esp + imm) and 0xFFFFFFFF.toInt()
                    branchCount++
                    cyclesUsed = 6
                }

                /* ---------- INT ---------- */
                0xCD -> {
                    val vector = fetch8()
                    handleInterrupt(vector)
                    interruptCount++
                    cyclesUsed = 20
                }

                /* ---------- SYSCALL ---------- */
                0x0F -> {
                    val next = fetch8()
                    when (next) {
                        0x05 -> {
                            handleSyscall()
                            syscallCount++
                            cyclesUsed = 30
                        }
                        0x34 -> {
                            // SYSENTER (Pentium II+)
                            handleSysenter()
                            syscallCount++
                            cyclesUsed = 25
                        }
                        0x35 -> {
                            // SYSEXIT
                            handleSysexit()
                            syscallCount++
                            cyclesUsed = 25
                        }
                        else -> {
                            handleUnknownOpcode((lastOpcode shl 8) or next)
                        }
                    }
                }

                /* ---------- HLT ---------- */
                0xF4 -> {
                    halted = true
                    cyclesUsed = 1
                }

                /* ---------- CLI ---------- */
                0xFA -> {
                    setFlag(IF, false)
                    cyclesUsed = 2
                }

                /* ---------- STI ---------- */
                0xFB -> {
                    setFlag(IF, true)
                    cyclesUsed = 2
                }

                /* ---------- CLC ---------- */
                0xF8 -> {
                    setFlag(CF, false)
                    cyclesUsed = 2
                }

                /* ---------- STC ---------- */
                0xF9 -> {
                    setFlag(CF, true)
                    cyclesUsed = 2
                }

                /* ---------- CMC ---------- */
                0xF5 -> {
                    setFlag(CF, !getFlag(CF))
                    cyclesUsed = 2
                }

                /* ---------- TEST ---------- */
                0x85 -> {
                    val modRM = fetch8()
                    handleTest32(modRM)
                    cyclesUsed = 2
                }
                
                0xA8 -> {
                    val imm = fetch8()
                    handleTestALImm8(imm)
                    cyclesUsed = 2
                }
                
                0xA9 -> {
                    val imm = fetch32()
                    handleTestEAXImm32(imm)
                    cyclesUsed = 2
                }

                /* ---------- AND ---------- */
                0x21 -> {
                    val modRM = fetch8()
                    handleAnd32(modRM)
                    cyclesUsed = 2
                }
                
                0x23 -> {
                    val modRM = fetch8()
                    handleAndR32(modRM)
                    cyclesUsed = 2
                }
                
                0x24 -> {
                    val imm = fetch8()
                    handleAndALImm8(imm)
                    cyclesUsed = 2
                }
                
                0x25 -> {
                    val imm = fetch32()
                    handleAndEAXImm32(imm)
                    cyclesUsed = 2
                }

                /* ---------- OR ---------- */
                0x09 -> {
                    val modRM = fetch8()
                    handleOr32(modRM)
                    cyclesUsed = 2
                }
                
                0x0B -> {
                    val modRM = fetch8()
                    handleOrR32(modRM)
                    cyclesUsed = 2
                }
                
                0x0C -> {
                    val imm = fetch8()
                    handleOrALImm8(imm)
                    cyclesUsed = 2
                }
                
                0x0D -> {
                    val imm = fetch32()
                    handleOrEAXImm32(imm)
                    cyclesUsed = 2
                }

                /* ---------- XOR ---------- */
                0x31 -> {
                    val modRM = fetch8()
                    handleXor32(modRM)
                    cyclesUsed = 2
                }
                
                0x33 -> {
                    val modRM = fetch8()
                    handleXorR32(modRM)
                    cyclesUsed = 2
                }
                
                0x34 -> {
                    val imm = fetch8()
                    handleXorALImm8(imm)
                    cyclesUsed = 2
                }
                
                0x35 -> {
                    val imm = fetch32()
                    handleXorEAXImm32(imm)
                    cyclesUsed = 2
                }

                /* ---------- SHL/SHR/SAR ---------- */
                0xD0 -> {
                    val modRM = fetch8()
                    val shiftType = (modRM shr 3) and 0x07
                    when (shiftType) {
                        4 -> handleShl8(modRM, 1)
                        5 -> handleShr8(modRM, 1)
                        7 -> handleSar8(modRM, 1)
                        else -> handleUnknownOpcode(lastOpcode)
                    }
                    cyclesUsed = 2
                }
                
                0xD1 -> {
                    val modRM = fetch8()
                    val shiftType = (modRM shr 3) and 0x07
                    when (shiftType) {
                        4 -> handleShl32(modRM, 1)
                        5 -> handleShr32(modRM, 1)
                        7 -> handleSar32(modRM, 1)
                        else -> handleUnknownOpcode(lastOpcode)
                    }
                    cyclesUsed = 2
                }
                
                0xD2 -> {
                    val modRM = fetch8()
                    val shiftType = (modRM shr 3) and 0x07
                    when (shiftType) {
                        4 -> handleShl8(modRM, (ecx and 0x1F))
                        5 -> handleShr8(modRM, (ecx and 0x1F))
                        7 -> handleSar8(modRM, (ecx and 0x1F))
                        else -> handleUnknownOpcode(lastOpcode)
                    }
                    cyclesUsed = 2
                }
                
                0xD3 -> {
                    val modRM = fetch8()
                    val shiftType = (modRM shr 3) and 0x07
                    when (shiftType) {
                        4 -> handleShl32(modRM, (ecx and 0x1F))
                        5 -> handleShr32(modRM, (ecx and 0x1F))
                        7 -> handleSar32(modRM, (ecx and 0x1F))
                        else -> handleUnknownOpcode(lastOpcode)
                    }
                    cyclesUsed = 2
                }
                
                0xC0 -> {
                    val modRM = fetch8()
                    val shiftType = (modRM shr 3) and 0x07
                    val imm = fetch8() and 0x1F
                    when (shiftType) {
                        4 -> handleShl8(modRM, imm)
                        5 -> handleShr8(modRM, imm)
                        7 -> handleSar8(modRM, imm)
                        else -> handleUnknownOpcode(lastOpcode)
                    }
                    cyclesUsed = 2
                }
                
                0xC1 -> {
                    val modRM = fetch8()
                    val shiftType = (modRM shr 3) and 0x07
                    val imm = fetch8() and 0x1F
                    when (shiftType) {
                        4 -> handleShl32(modRM, imm)
                        5 -> handleShr32(modRM, imm)
                        7 -> handleSar32(modRM, imm)
                        else -> handleUnknownOpcode(lastOpcode)
                    }
                    cyclesUsed = 2
                }

                /* ---------- ROL/ROR/RCL/RCR ---------- */
                0xD0 -> {
                    val modRM = fetch8()
                    val rotateType = (modRM shr 3) and 0x07
                    when (rotateType) {
                        0 -> handleRol8(modRM, 1)
                        1 -> handleRor8(modRM, 1)
                        2 -> handleRcl8(modRM, 1)
                        3 -> handleRcr8(modRM, 1)
                        else -> {} // Handled above
                    }
                }
                
                0xD1 -> {
                    val modRM = fetch8()
                    val rotateType = (modRM shr 3) and 0x07
                    when (rotateType) {
                        0 -> handleRol32(modRM, 1)
                        1 -> handleRor32(modRM, 1)
                        2 -> handleRcl32(modRM, 1)
                        3 -> handleRcr32(modRM, 1)
                        else -> {} // Handled above
                    }
                }

                /* ---------- PUSHF ---------- */
                0x9C -> {
                    push32(eflags)
                    cyclesUsed = 4
                }

                /* ---------- POPF ---------- */
                0x9D -> {
                    eflags = pop32()
                    cyclesUsed = 4
                }

                /* ---------- LEAVE ---------- */
                0xC9 -> {
                    esp = ebp
                    ebp = pop32()
                    cyclesUsed = 3
                }

                /* ---------- NEG ---------- */
                0xF6 -> {
                    val modRM = fetch8()
                    if ((modRM shr 3) and 0x07 == 3) {
                        handleNeg8(modRM)
                        cyclesUsed = 2
                    }
                }
                
                0xF7 -> {
                    val modRM = fetch8()
                    if ((modRM shr 3) and 0x07 == 3) {
                        handleNeg32(modRM)
                        cyclesUsed = 2
                    }
                }

                /* ---------- NOT ---------- */
                0xF6 -> {
                    val modRM = fetch8()
                    if ((modRM shr 3) and 0x07 == 2) {
                        handleNot8(modRM)
                        cyclesUsed = 2
                    }
                }
                
                0xF7 -> {
                    val modRM = fetch8()
                    if ((modRM shr 3) and 0x07 == 2) {
                        handleNot32(modRM)
                        cyclesUsed = 2
                    }
                }

                /* ---------- MUL ---------- */
                0xF6 -> {
                    val modRM = fetch8()
                    if ((modRM shr 3) and 0x07 == 4) {
                        handleMul8(modRM)
                        cyclesUsed = 11
                    }
                }
                
                0xF7 -> {
                    val modRM = fetch8()
                    if ((modRM shr 3) and 0x07 == 4) {
                        handleMul32(modRM)
                        cyclesUsed = 11
                    }
                }

                /* ---------- IMUL ---------- */
                0xF6 -> {
                    val modRM = fetch8()
                    if ((modRM shr 3) and 0x07 == 5) {
                        handleIMul8(modRM)
                        cyclesUsed = 11
                    }
                }
                
                0xF7 -> {
                    val modRM = fetch8()
                    if ((modRM shr 3) and 0x07 == 5) {
                        handleIMul32(modRM)
                        cyclesUsed = 11
                    }
                }
                
                0x6B -> {
                    val modRM = fetch8()
                    val imm = fetchSigned8()
                    handleIMul32Imm8(modRM, imm)
                    cyclesUsed = 11
                }
                
                0x69 -> {
                    val modRM = fetch8()
                    val imm = fetch32()
                    handleIMul32Imm32(modRM, imm)
                    cyclesUsed = 11
                }
                
                0x0F -> {
                    val next = fetch8()
                    if (next == 0xAF) {
                        val modRM = fetch8()
                        handleIMul32Reg(modRM)
                        cyclesUsed = 11
                    }
                }

                /* ---------- DIV ---------- */
                0xF6 -> {
                    val modRM = fetch8()
                    if ((modRM shr 3) and 0x07 == 6) {
                        handleDiv8(modRM)
                        cyclesUsed = 17
                    }
                }
                
                0xF7 -> {
                    val modRM = fetch8()
                    if ((modRM shr 3) and 0x07 == 6) {
                        handleDiv32(modRM)
                        cyclesUsed = 17
                    }
                }

                /* ---------- IDIV ---------- */
                0xF6 -> {
                    val modRM = fetch8()
                    if ((modRM shr 3) and 0x07 == 7) {
                        handleIDiv8(modRM)
                        cyclesUsed = 22
                    }
                }
                
                0xF7 -> {
                    val modRM = fetch8()
                    if ((modRM shr 3) and 0x07 == 7) {
                        handleIDiv32(modRM)
                        cyclesUsed = 22
                    }
                }

                /* ---------- LEA ---------- */
                0x8D -> {
                    val modRM = fetch8()
                    handleLea32(modRM)
                    cyclesUsed = 2
                }

                /* ---------- XCHG ---------- */
                0x86 -> {
                    val modRM = fetch8()
                    handleXchg8(modRM)
                    cyclesUsed = 3
                }
                
                0x87 -> {
                    val modRM = fetch8()
                    handleXchg32(modRM)
                    cyclesUsed = 3
                }
                
                0x90 -> {
                    // NOP (also XCHG EAX, EAX)
                    cyclesUsed = 1
                }
                
                0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97 -> {
                    val reg = lastOpcode - 0x90
                    handleXchgEAX(reg)
                    cyclesUsed = 3
                }

                /* ---------- BSWAP ---------- */
                0x0F -> {
                    val next = fetch8()
                    if (next in 0xC8..0xCF) {
                        val reg = next - 0xC8
                        handleBswap(reg)
                        cyclesUsed = 1
                    }
                }

                /* ---------- CMOVcc ---------- */
                0x0F -> {
                    val next = fetch8()
                    if (next in 0x40..0x4F) {
                        val modRM = fetch8()
                        val condition = next - 0x40
                        handleCmovcc(condition, modRM)
                        cyclesUsed = 2
                    }
                }

                /* ---------- SETcc ---------- */
                0x0F -> {
                    val next = fetch8()
                    if (next in 0x90..0x9F) {
                        val modRM = fetch8()
                        val condition = next - 0x90
                        handleSetcc(condition, modRM)
                        cyclesUsed = 2
                    }
                }

                /* ---------- x87 FPU Instructions ---------- */
                0xD9 -> {
                    val next = fetch8()
                    when {
                        next in 0xC0..0xC7 -> handleFld(next)
                        next in 0xD0..0xD7 -> handleFst(next)
                        next in 0xD8..0xDF -> handleFstp(next)
                        next == 0xE0 -> handleFchs()
                        next == 0xE1 -> handleFabs()
                        next == 0xE4 -> handleFtest()
                        next == 0xE8 -> handleFld1()
                        next == 0xE9 -> handleFldl2t()
                        next == 0xEA -> handleFldl2e()
                        next == 0xEB -> handleFldpi()
                        next == 0xEC -> handleFldlg2()
                        next == 0xED -> handleFldln2()
                        next == 0xEE -> handleFldz()
                        next in 0xF0..0xF7 -> handleF2xm1()
                        next in 0xF8..0xFF -> handleFscale()
                        else -> handleUnknownOpcode((lastOpcode shl 8) or next)
                    }
                    cyclesUsed = 3
                }

                0xDA -> {
                    val next = fetch8()
                    when {
                        next in 0xC0..0xC7 -> handleFcmovb(next)
                        next in 0xC8..0xCF -> handleFcmove(next)
                        next in 0xD0..0xD7 -> handleFcmovbe(next)
                        next in 0xD8..0xDF -> handleFcmovu(next)
                        else -> handleUnknownOpcode((lastOpcode shl 8) or next)
                    }
                    cyclesUsed = 4
                }

                0xDB -> {
                    val next = fetch8()
                    when {
                        next in 0xC0..0xC7 -> handleFcmovnb(next)
                        next in 0xC8..0xCF -> handleFcmovne(next)
                        next in 0xD0..0xD7 -> handleFcmovnbe(next)
                        next in 0xD8..0xDF -> handleFcmovnu(next)
                        next == 0xE2 -> handleFclex()
                        next == 0xE3 -> handleFinit()
                        else -> handleUnknownOpcode((lastOpcode shl 8) or next)
                    }
                    cyclesUsed = 4
                }

                0xDC -> {
                    val next = fetch8()
                    when {
                        next in 0xC0..0xC7 -> handleFadd(next)
                        next in 0xC8..0xCF -> handleFmul(next)
                        next in 0xE0..0xE7 -> handleFsub(next)
                        next in 0xE8..0xEF -> handleFsubr(next)
                        next in 0xF0..0xF7 -> handleFdiv(next)
                        next in 0xF8..0xFF -> handleFdivr(next)
                        else -> handleUnknownOpcode((lastOpcode shl 8) or next)
                    }
                    cyclesUsed = 5
                }

                0xDD -> {
                    val next = fetch8()
                    when {
                        next in 0xC0..0xC7 -> handleFfree(next)
                        next in 0xD0..0xD7 -> handleFst(next)
                        next in 0xD8..0xDF -> handleFstp(next)
                        next in 0xE0..0xE7 -> handleFucom(next)
                        next in 0xE8..0xEF -> handleFucomp(next)
                        else -> handleUnknownOpcode((lastOpcode shl 8) or next)
                    }
                    cyclesUsed = 3
                }

                0xDE -> {
                    val next = fetch8()
                    when {
                        next in 0xC0..0xC7 -> handleFaddp(next)
                        next in 0xC8..0xCF -> handleFmulp(next)
                        next in 0xE0..0xE7 -> handleFsubrp(next)
                        next in 0xE8..0xEF -> handleFsubp(next)
                        next in 0xF0..0xF7 -> handleFdivrp(next)
                        next in 0xF8..0xFF -> handleFdivp(next)
                        else -> handleUnknownOpcode((lastOpcode shl 8) or next)
                    }
                    cyclesUsed = 5
                }

                0xDF -> {
                    val next = fetch8()
                    when (next) {
                        0xE0 -> handleFstsw()
                        in 0xD0..0xD7 -> handleFcom(next)
                        in 0xD8..0xDF -> handleFcomp(next)
                        in 0xE8..0xEF -> handleFucomip(next)
                        in 0xF0..0xF7 -> handleFcomip(next)
                        else -> handleUnknownOpcode((lastOpcode shl 8) or next)
                    }
                    cyclesUsed = 3
                }

                /* ---------- MMX Instructions ---------- */
                0x0F -> {
                    val next = fetch8()
                    when (next) {
                        0x6F -> handleMovq(fetch8())
                        0x7F -> handleMovqStore(fetch8())
                        0x6E -> handleMovd(fetch8())
                        0x7E -> handleMovdStore(fetch8())
                        0xFC -> handlePaddb(fetch8())
                        0xFD -> handlePaddw(fetch8())
                        0xFE -> handlePaddd(fetch8())
                        0xF8 -> handlePsubb(fetch8())
                        0xF9 -> handlePsubw(fetch8())
                        0xFA -> handlePsubd(fetch8())
                        0xDB -> handlePand(fetch8())
                        0xEB -> handlePor(fetch8())
                        0xEF -> handlePxor(fetch8())
                        0x74 -> handlePcmpeqb(fetch8())
                        0x75 -> handlePcmpeqw(fetch8())
                        0x76 -> handlePcmpeqd(fetch8())
                        0x63 -> handlePacksswb(fetch8())
                        0x0F -> handleEmms()
                        else -> throw IllegalStateException("Unknown MMX opcode: 0x0F 0x${next.toString(16)}")
                    }
                    cyclesUsed = 2
                }

                /* ---------- SSE Instructions ---------- */
                0x0F -> {
                    val next = fetch8()
                    when (next) {
                        0x28 -> handleMovaps(fetch8())
                        0x29 -> handleMovapsStore(fetch8())
                        0x58 -> handleAddps(fetch8())
                        0x59 -> handleMulps(fetch8())
                        0x5C -> handleSubps(fetch8())
                        0x5E -> handleDivps(fetch8())
                        0x10 -> handleMovups(fetch8())
                        0x11 -> handleMovupsStore(fetch8())
                        0x2E -> handleUcomiss(fetch8())
                        0x2F -> handleComiss(fetch8())
                        0xAE -> {
                            val modRM = fetch8()
                            when ((modRM shr 3) and 0x07) {
                                2 -> handleLdmxcsr(modRM)
                                3 -> handleStmxcsr(modRM)
                            }
                        }
                    }
                }

                /* ---------- Conditional Jumps ---------- */
                0x70 -> handleJumpCC(0)  // JO
                0x71 -> handleJumpCC(1)  // JNO
                0x72 -> handleJumpCC(2)  // JB/JNAE/JC
                0x73 -> handleJumpCC(3)  // JNB/JAE/JNC
                0x74 -> handleJumpCC(4)  // JZ/JE
                0x75 -> handleJumpCC(5)  // JNZ/JNE
                0x76 -> handleJumpCC(6)  // JBE/JNA
                0x77 -> handleJumpCC(7)  // JNBE/JA
                0x78 -> handleJumpCC(8)  // JS
                0x79 -> handleJumpCC(9)  // JNS
                0x7A -> handleJumpCC(10) // JP/JPE
                0x7B -> handleJumpCC(11) // JNP/JPO
                0x7C -> handleJumpCC(12) // JL/JNGE
                0x7D -> handleJumpCC(13) // JNL/JGE
                0x7E -> handleJumpCC(14) // JLE/JNG
                0x7F -> handleJumpCC(15) // JNLE/JG

                /* ---------- LOOP instructions ---------- */
                0xE0 -> handleLoopnz()
                0xE1 -> handleLoopz()
                0xE2 -> handleLoop()
                0xE3 -> handleJecxz()

                /* ---------- String Instructions ---------- */
                0xA4 -> handleMovsb()
                0xA5 -> handleMovsd()
                0xA6 -> handleCmpsb()
                0xA7 -> handleCmpsd()
                0xAA -> handleStosb()
                0xAB -> handleStosd()
                0xAC -> handleLodsb()
                0xAD -> handleLodsd()
                0xAE -> handleScasb()
                0xAF -> handleScasd()

                /* ---------- Bit Instructions ---------- */
                0x0F -> {
                    val next = fetch8()
                    when (next) {
                        0xA3 -> handleBt(fetch8())
                        0xAB -> handleBts(fetch8())
                        0xB3 -> handleBtr(fetch8())
                        0xBB -> handleBtc(fetch8())
                        0xBC -> handleBsf(fetch8())
                        0xBD -> handleBsr(fetch8())
                    }
                }

                /* ---------- Cache Control ---------- */
                0x0F -> {
                    val next = fetch8()
                    when (next) {
                        0xAE -> {
                            val modRM = fetch8()
                            when ((modRM shr 3) and 0x07) {
                                5 -> handleMfence()
                                6 -> handleSfence()
                                7 -> handleLfence()
                            }
                        }
                        0x18 -> {
                            val modRM = fetch8()
                            when ((modRM shr 3) and 0x07) {
                                1 -> handlePrefetchT0(modRM)
                                2 -> handlePrefetchT1(modRM)
                                3 -> handlePrefetchT2(modRM)
                                0 -> handlePrefetchNta(modRM)
                            }
                        }
                    }
                }

                /* ---------- CPUID ---------- */
                0x0F -> {
                    val next = fetch8()
                    if (next == 0xA2) {
                        handleCpuid()
                        cyclesUsed = 15
                    }
                }

                /* ---------- RDTSC ---------- */
                0x0F -> {
                    val next = fetch8()
                    if (next == 0x31) {
                        handleRdtsc()
                        cyclesUsed = 15
                    }
                }

                /* ---------- PREFETCH ---------- */
                0x0F -> {
                    val next = fetch8()
                    when (next) {
                        0x0D -> {
                            val modRM = fetch8()
                            handlePrefetch(modRM)
                            cyclesUsed = 1
                        }
                        0x18 -> {
                            val modRM = fetch8()
                            handlePrefetchw(modRM)
                            cyclesUsed = 1
                        }
                    }
                }

                else -> {
                    handleUnknownOpcode(lastOpcode)
                }
            }
            
            cycles += cyclesUsed
            instructionsExecuted++
            
            // Update timer every 1000 cycles
            if (cycles % 1000 == 0L) {
                timerTicks++
            }
            
            // Clear prefixes for next instruction
            lastPrefixes.clear()
            
            return cyclesUsed
            
        } catch (e: Exception) {
            Log.e(TAG, "CPU Exception at EIP=0x${startEip.toString(16)} opcode=0x${lastOpcode.toString(16)}: ${e.message}")
            Log.e(TAG, "Registers: EAX=0x${eax.toString(16)} EBX=0x${ebx.toString(16)} ECX=0x${ecx.toString(16)} EDX=0x${edx.toString(16)}")
            Log.e(TAG, "           ESI=0x${esi.toString(16)} EDI=0x${edi.toString(16)} EBP=0x${ebp.toString(16)} ESP=0x${esp.toString(16)}")
            Log.e(TAG, "           EIP=0x${eip.toString(16)} EFLAGS=0x${eflags.toString(16)}")
            running = false
            return 0
        }
    }

private fun handlePcmpeqb(modrm: Int) {
    val dstReg = (modrm shr 3) and 0x07
    val srcReg = modrm and 0x07
    val dst = getMmxReg(dstReg)[0]
    val src = getMmxReg(srcReg)[0]
    
    var result = 0L
    for (i in 0 until 8) {
        val byteMask = 0xFFL shl (i * 8)
        val dstByte = (dst and byteMask) shr (i * 8)
        val srcByte = (src and byteMask) shr (i * 8)
        val mask = if (dstByte == srcByte) 0xFFL else 0x00L
        result = result or (mask shl (i * 8))
    }
    
    setMmxReg(dstReg, longArrayOf(result))
    mmxInstructions++
    cycles += 2
}

private fun handlePcmpeqw(modrm: Int) {
    val dstReg = (modrm shr 3) and 0x07
    val srcReg = modrm and 0x07
    val dst = getMmxReg(dstReg)[0]
    val src = getMmxReg(srcReg)[0]
    
    var result = 0L
    for (i in 0 until 4) {
        val wordMask = 0xFFFFL shl (i * 16)
        val dstWord = (dst and wordMask) shr (i * 16)
        val srcWord = (src and wordMask) shr (i * 16)
        val mask = if (dstWord == srcWord) 0xFFFFL else 0x0000L
        result = result or (mask shl (i * 16))
    }
    
    setMmxReg(dstReg, longArrayOf(result))
    mmxInstructions++
    cycles += 2
}

private fun handlePcmpeqd(modrm: Int) {
    val dstReg = (modrm shr 3) and 0x07
    val srcReg = modrm and 0x07
    val dst = getMmxReg(dstReg)[0]
    val src = getMmxReg(srcReg)[0]
    
    var result = 0L
    for (i in 0 until 2) {
        val dwordMask = 0xFFFFFFFFL shl (i * 32)
        val dstDword = (dst and dwordMask) shr (i * 32)
        val srcDword = (src and dwordMask) shr (i * 32)
        val mask = if (dstDword == srcDword) 0xFFFFFFFFL else 0x00000000L
        result = result or (mask shl (i * 32))
    }
    
    setMmxReg(dstReg, longArrayOf(result))
    mmxInstructions++
    cycles += 2
}


    /* ===============================
       Register Access (مُحسنة)
       =============================== */

    private fun getReg32(reg: Int): Int {
        return when (reg and 0x07) {
            0 -> eax
            1 -> ecx
            2 -> edx
            3 -> ebx
            4 -> esp
            5 -> ebp
            6 -> esi
            7 -> edi
            else -> 0
        }
    }

    private fun setReg32(reg: Int, value: Int) {
        when (reg and 0x07) {
            0 -> eax = value
            1 -> ecx = value
            2 -> edx = value
            3 -> ebx = value
            4 -> esp = value
            5 -> ebp = value
            6 -> esi = value
            7 -> edi = value
        }
    }

    private fun getReg16(reg: Int): Int {
        return when (reg and 0x07) {
            0 -> eax and 0xFFFF
            1 -> ecx and 0xFFFF
            2 -> edx and 0xFFFF
            3 -> ebx and 0xFFFF
            4 -> esp and 0xFFFF
            5 -> ebp and 0xFFFF
            6 -> esi and 0xFFFF
            7 -> edi and 0xFFFF
            else -> 0
        }
    }

    private fun setReg16(reg: Int, value: Int) {
        val v = value and 0xFFFF
        when (reg and 0x07) {
            0 -> eax = (eax and 0xFFFF0000.toInt()) or v
            1 -> ecx = (ecx and 0xFFFF0000.toInt()) or v
            2 -> edx = (edx and 0xFFFF0000.toInt()) or v
            3 -> ebx = (ebx and 0xFFFF0000.toInt()) or v
            4 -> esp = (esp and 0xFFFF0000.toInt()) or v
            5 -> ebp = (ebp and 0xFFFF0000.toInt()) or v
            6 -> esi = (esi and 0xFFFF0000.toInt()) or v
            7 -> edi = (edi and 0xFFFF0000.toInt()) or v
        }
    }

    private fun getReg8(reg: Int): Int {
        return when (reg and 0x07) {
            0 -> eax and 0xFF
            1 -> ecx and 0xFF
            2 -> edx and 0xFF
            3 -> ebx and 0xFF
            4 -> (eax shr 8) and 0xFF
            5 -> (ecx shr 8) and 0xFF
            6 -> (edx shr 8) and 0xFF
            7 -> (ebx shr 8) and 0xFF
            else -> 0
        }
    }

    private fun setReg8(reg: Int, value: Int) {
        val v = value and 0xFF
        when (reg and 0x07) {
            0 -> eax = (eax and 0xFFFFFF00.toInt()) or v
            1 -> ecx = (ecx and 0xFFFFFF00.toInt()) or v
            2 -> edx = (edx and 0xFFFFFF00.toInt()) or v
            3 -> ebx = (ebx and 0xFFFFFF00.toInt()) or v
            4 -> eax = (eax and 0xFFFF00FF.toInt()) or (v shl 8)
            5 -> ecx = (ecx and 0xFFFF00FF.toInt()) or (v shl 8)
            6 -> edx = (edx and 0xFFFF00FF.toInt()) or (v shl 8)
            7 -> ebx = (ebx and 0xFFFF00FF.toInt()) or (v shl 8)
        }
    }

    /* ===============================
       ModR/M Operations (مُحسنة)
       =============================== */

    private fun calculateEA(modRM: Int): Int {
        val mod = (modRM shr 6) and 0x03
        val rm = modRM and 0x07
        
        return when (mod) {
            0 -> {
                when (rm) {
                    0 -> eax
                    1 -> ecx
                    2 -> edx
                    3 -> ebx
                    4 -> {
                        val sib = fetch8()
                        handleSIB(sib, 0)
                    }
                    5 -> fetch32()
                    6 -> esi
                    7 -> edi
                    else -> 0
                }
            }
            1 -> {
                val disp8 = fetchSigned8()
                when (rm) {
                    0 -> eax + disp8
                    1 -> ecx + disp8
                    2 -> edx + disp8
                    3 -> ebx + disp8
                    4 -> {
                        val sib = fetch8()
                        handleSIB(sib, disp8)
                    }
                    5 -> ebp + disp8
                    6 -> esi + disp8
                    7 -> edi + disp8
                    else -> 0
                }
            }
            2 -> {
                val disp32 = fetchSigned32()
                when (rm) {
                    0 -> eax + disp32
                    1 -> ecx + disp32
                    2 -> edx + disp32
                    3 -> ebx + disp32
                    4 -> {
                        val sib = fetch8()
                        handleSIB(sib, disp32)
                    }
                    5 -> ebp + disp32
                    6 -> esi + disp32
                    7 -> edi + disp32
                    else -> 0
                }
            }
            3 -> {
                // Register addressing - no memory access
                return -1
            }
            else -> 0
        }
    }

    private fun handleSIB(sib: Int, baseDisp: Int): Int {
        val scale = 1 shl ((sib shr 6) and 0x03)
        val index = (sib shr 3) and 0x07
        val base = sib and 0x07
        
        var address = baseDisp
        
        // Add base register
        when (base) {
            0 -> address += eax
            1 -> address += ecx
            2 -> address += edx
            3 -> address += ebx
            4 -> address += esp
            5 -> if ((sib and 0x07) == 5) address += fetchSigned32() else address += ebp
            6 -> address += esi
            7 -> address += edi
        }
        
        // Add scaled index
        when (index) {
            0 -> address += eax * scale
            1 -> address += ecx * scale
            2 -> address += edx * scale
            3 -> address += ebx * scale
            4 -> {} // No index
            5 -> address += ebp * scale
            6 -> address += esi * scale
            7 -> address += edi * scale
        }
        
        return address
    }

    private fun readOperand8(modRM: Int): Int {
        val mod = (modRM shr 6) and 0x03
        if (mod == 3) {
            // Register
            val reg = modRM and 0x07
            return getReg8(reg)
        } else {
            // Memory
            val ea = calculateEA(modRM)
            return memory.read8(ea)
        }
    }

    private fun writeOperand8(modRM: Int, value: Int) {
        val mod = (modRM shr 6) and 0x03
        if (mod == 3) {
            // Register
            val reg = modRM and 0x07
            setReg8(reg, value)
        } else {
            // Memory
            val ea = calculateEA(modRM)
            memory.write8(ea, value)
        }
    }

    private fun readOperand16(modRM: Int): Int {
        val mod = (modRM shr 6) and 0x03
        if (mod == 3) {
            // Register
            val reg = modRM and 0x07
            return getReg16(reg)
        } else {
            // Memory
            val ea = calculateEA(modRM)
            return memory.read16(ea)
        }
    }

    private fun writeOperand16(modRM: Int, value: Int) {
        val mod = (modRM shr 6) and 0x03
        if (mod == 3) {
            // Register
            val reg = modRM and 0x07
            setReg16(reg, value)
        } else {
            // Memory
            val ea = calculateEA(modRM)
            memory.write16(ea, value)
        }
    }

    private fun readOperand32(modRM: Int): Int {
        val mod = (modRM shr 6) and 0x03
        if (mod == 3) {
            // Register
            val reg = modRM and 0x07
            return getReg32(reg)
        } else {
            // Memory
            val ea = calculateEA(modRM)
            return memory.read32(ea)
        }
    }

    private fun writeOperand32(modRM: Int, value: Int) {
        val mod = (modRM shr 6) and 0x03
        if (mod == 3) {
            // Register
            val reg = modRM and 0x07
            setReg32(reg, value)
        } else {
            // Memory
            val ea = calculateEA(modRM)
            memory.write32(ea, value)
        }
    }

    /* ===============================
       Instruction Handlers (مُحسنة)
       =============================== */

    private fun handleMov8(modRM: Int) {
        val srcReg = (modRM shr 3) and 0x07
        val srcValue = getReg8(srcReg)
        writeOperand8(modRM, srcValue)
    }

    private fun handleMov16(modRM: Int) {
        val srcReg = (modRM shr 3) and 0x07
        val srcValue = getReg16(srcReg)
        writeOperand16(modRM, srcValue)
    }

    private fun handleMov32(modRM: Int) {
        val srcReg = (modRM shr 3) and 0x07
        val srcValue = getReg32(srcReg)
        writeOperand32(modRM, srcValue)
    }

    private fun handleMovR8(modRM: Int) {
        val dstReg = (modRM shr 3) and 0x07
        val srcValue = readOperand8(modRM)
        setReg8(dstReg, srcValue)
    }

    private fun handleMovR16(modRM: Int) {
        val dstReg = (modRM shr 3) and 0x07
        val srcValue = readOperand16(modRM)
        setReg16(dstReg, srcValue)
    }

    private fun handleMovR32(modRM: Int) {
        val dstReg = (modRM shr 3) and 0x07
        val srcValue = readOperand32(modRM)
        setReg32(dstReg, srcValue)
    }

    private fun handleAdd8(modRM: Int) {
        val srcReg = (modRM shr 3) and 0x07
        val srcValue = getReg8(srcReg)
        val dstValue = readOperand8(modRM)
        val result = dstValue.toLong() + srcValue.toLong()
        
        updateCarryFlagAdd(dstValue, srcValue, result)
        updateOverflowFlagAdd(dstValue, srcValue, result)
        updateAuxiliaryFlagAdd(dstValue, srcValue)
        updateZeroFlag(result.toInt())
        updateSignFlag8(result.toInt())
        updateParityFlag(result.toInt())
        
        writeOperand8(modRM, result.toInt())
    }

    private fun handleAdd16(modRM: Int) {
        val srcReg = (modRM shr 3) and 0x07
        val srcValue = getReg16(srcReg)
        val dstValue = readOperand16(modRM)
        val result = dstValue.toLong() + srcValue.toLong()
        
        updateCarryFlagAdd(dstValue, srcValue, result)
        updateOverflowFlagAdd(dstValue, srcValue, result)
        updateAuxiliaryFlagAdd(dstValue, srcValue)
        updateZeroFlag(result.toInt())
        updateSignFlag16(result.toInt())
        updateParityFlag(result.toInt())
        
        writeOperand16(modRM, result.toInt())
    }

    private fun handleAdd32(modRM: Int) {
        val srcReg = (modRM shr 3) and 0x07
        val srcValue = getReg32(srcReg)
        val dstValue = readOperand32(modRM)
        val result = dstValue.toLong() + srcValue.toLong()
        
        updateCarryFlagAdd(dstValue, srcValue, result)
        updateOverflowFlagAdd(dstValue, srcValue, result)
        updateAuxiliaryFlagAdd(dstValue, srcValue)
        updateZeroFlag(result.toInt())
        updateSignFlag32(result.toInt())
        updateParityFlag(result.toInt())
        
        writeOperand32(modRM, result.toInt())
    }

    private fun handleAddR8(modRM: Int) {
        val dstReg = (modRM shr 3) and 0x07
        val srcValue = readOperand8(modRM)
        val dstValue = getReg8(dstReg)
        val result = dstValue.toLong() + srcValue.toLong()
        
        updateCarryFlagAdd(dstValue, srcValue, result)
        updateOverflowFlagAdd(dstValue, srcValue, result)
        updateAuxiliaryFlagAdd(dstValue, srcValue)
        updateZeroFlag(result.toInt())
        updateSignFlag8(result.toInt())
        updateParityFlag(result.toInt())
        
        setReg8(dstReg, result.toInt())
    }

    private fun handleAddR16(modRM: Int) {
        val dstReg = (modRM shr 3) and 0x07
        val srcValue = readOperand16(modRM)
        val dstValue = getReg16(dstReg)
        val result = dstValue.toLong() + srcValue.toLong()
        
        updateCarryFlagAdd(dstValue, srcValue, result)
        updateOverflowFlagAdd(dstValue, srcValue, result)
        updateAuxiliaryFlagAdd(dstValue, srcValue)
        updateZeroFlag(result.toInt())
        updateSignFlag16(result.toInt())
        updateParityFlag(result.toInt())
        
        setReg16(dstReg, result.toInt())
    }

    private fun handleAddR32(modRM: Int) {
        val dstReg = (modRM shr 3) and 0x07
        val srcValue = readOperand32(modRM)
        val dstValue = getReg32(dstReg)
        val result = dstValue.toLong() + srcValue.toLong()
        
        updateCarryFlagAdd(dstValue, srcValue, result)
        updateOverflowFlagAdd(dstValue, srcValue, result)
        updateAuxiliaryFlagAdd(dstValue, srcValue)
        updateZeroFlag(result.toInt())
        updateSignFlag32(result.toInt())
        updateParityFlag(result.toInt())
        
        setReg32(dstReg, result.toInt())
    }

    private fun handleAddALImm8(imm: Int) {
        val dstValue = eax and 0xFF
        val result = dstValue.toLong() + imm.toLong()
        
        updateCarryFlagAdd(dstValue, imm, result)
        updateOverflowFlagAdd(dstValue, imm, result)
        updateAuxiliaryFlagAdd(dstValue, imm)
        updateZeroFlag(result.toInt())
        updateSignFlag8(result.toInt())
        updateParityFlag(result.toInt())
        
        eax = (eax and 0xFFFFFF00.toInt()) or (result.toInt() and 0xFF)
    }

    private fun handleAddEAXImm32(imm: Int) {
        val dstValue = eax
        val result = dstValue.toLong() + imm.toLong()
        
        updateCarryFlagAdd(dstValue, imm, result)
        updateOverflowFlagAdd(dstValue, imm, result)
        updateAuxiliaryFlagAdd(dstValue, imm)
        updateZeroFlag(result.toInt())
        updateSignFlag32(result.toInt())
        updateParityFlag(result.toInt())
        
        eax = result.toInt()
    }

    private fun handleSub8(modRM: Int) {
        val srcReg = (modRM shr 3) and 0x07
        val srcValue = getReg8(srcReg)
        val dstValue = readOperand8(modRM)
        val result = dstValue.toLong() - srcValue.toLong()
        
        updateCarryFlagSub(dstValue, srcValue, result)
        updateOverflowFlagSub(dstValue, srcValue, result)
        updateAuxiliaryFlagSub(dstValue, srcValue)
        updateZeroFlag(result.toInt())
        updateSignFlag8(result.toInt())
        updateParityFlag(result.toInt())
        
        writeOperand8(modRM, result.toInt())
    }

    private fun handleSub16(modRM: Int) {
        val srcReg = (modRM shr 3) and 0x07
        val srcValue = getReg16(srcReg)
        val dstValue = readOperand16(modRM)
        val result = dstValue.toLong() - srcValue.toLong()
        
        updateCarryFlagSub(dstValue, srcValue, result)
        updateOverflowFlagSub(dstValue, srcValue, result)
        updateAuxiliaryFlagSub(dstValue, srcValue)
        updateZeroFlag(result.toInt())
        updateSignFlag16(result.toInt())
        updateParityFlag(result.toInt())
        
        writeOperand16(modRM, result.toInt())
    }

    private fun handleSub32(modRM: Int) {
        val srcReg = (modRM shr 3) and 0x07
        val srcValue = getReg32(srcReg)
        val dstValue = readOperand32(modRM)
        val result = dstValue.toLong() - srcValue.toLong()
        
        updateCarryFlagSub(dstValue, srcValue, result)
        updateOverflowFlagSub(dstValue, srcValue, result)
        updateAuxiliaryFlagSub(dstValue, srcValue)
        updateZeroFlag(result.toInt())
        updateSignFlag32(result.toInt())
        updateParityFlag(result.toInt())
        
        writeOperand32(modRM, result.toInt())
    }

    private fun handleSubR8(modRM: Int) {
        val dstReg = (modRM shr 3) and 0x07
        val srcValue = readOperand8(modRM)
        val dstValue = getReg8(dstReg)
        val result = dstValue.toLong() - srcValue.toLong()
        
        updateCarryFlagSub(dstValue, srcValue, result)
        updateOverflowFlagSub(dstValue, srcValue, result)
        updateAuxiliaryFlagSub(dstValue, srcValue)
        updateZeroFlag(result.toInt())
        updateSignFlag8(result.toInt())
        updateParityFlag(result.toInt())
        
        setReg8(dstReg, result.toInt())
    }

    private fun handleSubR16(modRM: Int) {
        val dstReg = (modRM shr 3) and 0x07
        val srcValue = readOperand16(modRM)
        val dstValue = getReg16(dstReg)
        val result = dstValue.toLong() - srcValue.toLong()
        
        updateCarryFlagSub(dstValue, srcValue, result)
        updateOverflowFlagSub(dstValue, srcValue, result)
        updateAuxiliaryFlagSub(dstValue, srcValue)
        updateZeroFlag(result.toInt())
        updateSignFlag16(result.toInt())
        updateParityFlag(result.toInt())
        
        setReg16(dstReg, result.toInt())
    }

    private fun handleSubR32(modRM: Int) {
        val dstReg = (modRM shr 3) and 0x07
        val srcValue = readOperand32(modRM)
        val dstValue = getReg32(dstReg)
        val result = dstValue.toLong() - srcValue.toLong()
        
        updateCarryFlagSub(dstValue, srcValue, result)
        updateOverflowFlagSub(dstValue, srcValue, result)
        updateAuxiliaryFlagSub(dstValue, srcValue)
        updateZeroFlag(result.toInt())
        updateSignFlag32(result.toInt())
        updateParityFlag(result.toInt())
        
        setReg32(dstReg, result.toInt())
    }

    private fun handleSubALImm8(imm: Int) {
        val dstValue = eax and 0xFF
        val result = dstValue.toLong() - imm.toLong()
        
        updateCarryFlagSub(dstValue, imm, result)
        updateOverflowFlagSub(dstValue, imm, result)
        updateAuxiliaryFlagSub(dstValue, imm)
        updateZeroFlag(result.toInt())
        updateSignFlag8(result.toInt())
        updateParityFlag(result.toInt())
        
        eax = (eax and 0xFFFFFF00.toInt()) or (result.toInt() and 0xFF)
    }

    private fun handleSubEAXImm32(imm: Int) {
        val dstValue = eax
        val result = dstValue.toLong() - imm.toLong()
        
        updateCarryFlagSub(dstValue, imm, result)
        updateOverflowFlagSub(dstValue, imm, result)
        updateAuxiliaryFlagSub(dstValue, imm)
        updateZeroFlag(result.toInt())
        updateSignFlag32(result.toInt())
        updateParityFlag(result.toInt())
        
        eax = result.toInt()
    }

    private fun handleCmp8(modRM: Int) {
        val srcReg = (modRM shr 3) and 0x07
        val srcValue = getReg8(srcReg)
        val dstValue = readOperand8(modRM)
        val result = dstValue.toLong() - srcValue.toLong()
        
        updateCarryFlagSub(dstValue, srcValue, result)
        updateOverflowFlagSub(dstValue, srcValue, result)
        updateAuxiliaryFlagSub(dstValue, srcValue)
        updateZeroFlag(result.toInt())
        updateSignFlag8(result.toInt())
        updateParityFlag(result.toInt())
    }

    private fun handleCmp16(modRM: Int) {
        val srcReg = (modRM shr 3) and 0x07
        val srcValue = getReg16(srcReg)
        val dstValue = readOperand16(modRM)
        val result = dstValue.toLong() - srcValue.toLong()
        
        updateCarryFlagSub(dstValue, srcValue, result)
        updateOverflowFlagSub(dstValue, srcValue, result)
        updateAuxiliaryFlagSub(dstValue, srcValue)
        updateZeroFlag(result.toInt())
        updateSignFlag16(result.toInt())
        updateParityFlag(result.toInt())
    }

    private fun handleCmp32(modRM: Int) {
        val srcReg = (modRM shr 3) and 0x07
        val srcValue = getReg32(srcReg)
        val dstValue = readOperand32(modRM)
        val result = dstValue.toLong() - srcValue.toLong()
        
        updateCarryFlagSub(dstValue, srcValue, result)
        updateOverflowFlagSub(dstValue, srcValue, result)
        updateAuxiliaryFlagSub(dstValue, srcValue)
        updateZeroFlag(result.toInt())
        updateSignFlag32(result.toInt())
        updateParityFlag(result.toInt())
    }

    private fun handleCmpR8(modRM: Int) {
        val dstReg = (modRM shr 3) and 0x07
        val srcValue = readOperand8(modRM)
        val dstValue = getReg8(dstReg)
        val result = dstValue.toLong() - srcValue.toLong()
        
        updateCarryFlagSub(dstValue, srcValue, result)
        updateOverflowFlagSub(dstValue, srcValue, result)
        updateAuxiliaryFlagSub(dstValue, srcValue)
        updateZeroFlag(result.toInt())
        updateSignFlag8(result.toInt())
        updateParityFlag(result.toInt())
    }

    private fun handleCmpR16(modRM: Int) {
        val dstReg = (modRM shr 3) and 0x07
        val srcValue = readOperand16(modRM)
        val dstValue = getReg16(dstReg)
        val result = dstValue.toLong() - srcValue.toLong()
        
        updateCarryFlagSub(dstValue, srcValue, result)
        updateOverflowFlagSub(dstValue, srcValue, result)
        updateAuxiliaryFlagSub(dstValue, srcValue)
        updateZeroFlag(result.toInt())
        updateSignFlag16(result.toInt())
        updateParityFlag(result.toInt())
    }

    private fun handleCmpR32(modRM: Int) {
        val dstReg = (modRM shr 3) and 0x07
        val srcValue = readOperand32(modRM)
        val dstValue = getReg32(dstReg)
        val result = dstValue.toLong() - srcValue.toLong()
        
        updateCarryFlagSub(dstValue, srcValue, result)
        updateOverflowFlagSub(dstValue, srcValue, result)
        updateAuxiliaryFlagSub(dstValue, srcValue)
        updateZeroFlag(result.toInt())
        updateSignFlag32(result.toInt())
        updateParityFlag(result.toInt())
    }

    private fun handleCmpALImm8(imm: Int) {
        val dstValue = eax and 0xFF
        val result = dstValue.toLong() - imm.toLong()
        
        updateCarryFlagSub(dstValue, imm, result)
        updateOverflowFlagSub(dstValue, imm, result)
        updateAuxiliaryFlagSub(dstValue, imm)
        updateZeroFlag(result.toInt())
        updateSignFlag8(result.toInt())
        updateParityFlag(result.toInt())
    }

    private fun handleCmpEAXImm32(imm: Int) {
        val dstValue = eax
        val result = dstValue.toLong() - imm.toLong()
        
        updateCarryFlagSub(dstValue, imm, result)
        updateOverflowFlagSub(dstValue, imm, result)
        updateAuxiliaryFlagSub(dstValue, imm)
        updateZeroFlag(result.toInt())
        updateSignFlag32(result.toInt())
        updateParityFlag(result.toInt())
    }

    private fun handleTest8(modRM: Int) {
        val srcReg = (modRM shr 3) and 0x07
        val srcValue = getReg8(srcReg)
        val dstValue = readOperand8(modRM)
        val result = dstValue and srcValue
        
        setFlag(CF, false)
        setFlag(OF, false)
        updateZeroFlag(result)
        updateSignFlag8(result)
        updateParityFlag(result)
    }

    private fun handleTest16(modRM: Int) {
        val srcReg = (modRM shr 3) and 0x07
        val srcValue = getReg16(srcReg)
        val dstValue = readOperand16(modRM)
        val result = dstValue and srcValue
        
        setFlag(CF, false)
        setFlag(OF, false)
        updateZeroFlag(result)
        updateSignFlag16(result)
        updateParityFlag(result)
    }

    private fun handleTest32(modRM: Int) {
        val srcReg = (modRM shr 3) and 0x07
        val srcValue = getReg32(srcReg)
        val dstValue = readOperand32(modRM)
        val result = dstValue and srcValue
        
        setFlag(CF, false)
        setFlag(OF, false)
        updateZeroFlag(result)
        updateSignFlag32(result)
        updateParityFlag(result)
    }

    private fun handleTestALImm8(imm: Int) {
        val dstValue = eax and 0xFF
        val result = dstValue and imm
        
        setFlag(CF, false)
        setFlag(OF, false)
        updateZeroFlag(result)
        updateSignFlag8(result)
        updateParityFlag(result)
    }

    private fun handleTestAXImm16(imm: Int) {
        val dstValue = eax and 0xFFFF
        val result = dstValue and imm
        
        setFlag(CF, false)
        setFlag(OF, false)
        updateZeroFlag(result)
        updateSignFlag16(result)
        updateParityFlag(result)
    }

    private fun handleTestEAXImm32(imm: Int) {
        val dstValue = eax
        val result = dstValue and imm
        
        setFlag(CF, false)
        setFlag(OF, false)
        updateZeroFlag(result)
        updateSignFlag32(result)
        updateParityFlag(result)
    }

    private fun handleAnd8(modRM: Int) {
        val srcReg = (modRM shr 3) and 0x07
        val srcValue = getReg8(srcReg)
        val dstValue = readOperand8(modRM)
        val result = dstValue and srcValue
        
        setFlag(CF, false)
        setFlag(OF, false)
        updateZeroFlag(result)
        updateSignFlag8(result)
        updateParityFlag(result)
        
        writeOperand8(modRM, result)
    }

    private fun handleAnd16(modRM: Int) {
        val srcReg = (modRM shr 3) and 0x07
        val srcValue = getReg16(srcReg)
        val dstValue = readOperand16(modRM)
        val result = dstValue and srcValue
        
        setFlag(CF, false)
        setFlag(OF, false)
        updateZeroFlag(result)
        updateSignFlag16(result)
        updateParityFlag(result)
        
        writeOperand16(modRM, result)
    }

    private fun handleAnd32(modRM: Int) {
        val srcReg = (modRM shr 3) and 0x07
        val srcValue = getReg32(srcReg)
        val dstValue = readOperand32(modRM)
        val result = dstValue and srcValue
        
        setFlag(CF, false)
        setFlag(OF, false)
        updateZeroFlag(result)
        updateSignFlag32(result)
        updateParityFlag(result)
        
        writeOperand32(modRM, result)
    }

    private fun handleAndR8(modRM: Int) {
        val dstReg = (modRM shr 3) and 0x07
        val srcValue = readOperand8(modRM)
        val dstValue = getReg8(dstReg)
        val result = dstValue and srcValue
        
        setFlag(CF, false)
        setFlag(OF, false)
        updateZeroFlag(result)
        updateSignFlag8(result)
        updateParityFlag(result)
        
        setReg8(dstReg, result)
    }

    private fun handleAndR16(modRM: Int) {
        val dstReg = (modRM shr 3) and 0x07
        val srcValue = readOperand16(modRM)
        val dstValue = getReg16(dstReg)
        val result = dstValue and srcValue
        
        setFlag(CF, false)
        setFlag(OF, false)
        updateZeroFlag(result)
        updateSignFlag16(result)
        updateParityFlag(result)
        
        setReg16(dstReg, result)
    }

    private fun handleAndR32(modRM: Int) {
        val dstReg = (modRM shr 3) and 0x07
        val srcValue = readOperand32(modRM)
        val dstValue = getReg32(dstReg)
        val result = dstValue and srcValue
        
        setFlag(CF, false)
        setFlag(OF, false)
        updateZeroFlag(result)
        updateSignFlag32(result)
        updateParityFlag(result)
        
        setReg32(dstReg, result)
    }

    private fun handleAndALImm8(imm: Int) {
        val dstValue = eax and 0xFF
        val result = dstValue and imm
        
        setFlag(CF, false)
        setFlag(OF, false)
        updateZeroFlag(result)
        updateSignFlag8(result)
        updateParityFlag(result)
        
        eax = (eax and 0xFFFFFF00.toInt()) or (result and 0xFF)
    }

    private fun handleAndAXImm16(imm: Int) {
        val dstValue = eax and 0xFFFF
        val result = dstValue and imm
        
        setFlag(CF, false)
        setFlag(OF, false)
        updateZeroFlag(result)
        updateSignFlag16(result)
        updateParityFlag(result)
        
        eax = (eax and 0xFFFF0000.toInt()) or (result and 0xFFFF)
    }

    private fun handleAndEAXImm32(imm: Int) {
        val dstValue = eax
        val result = dstValue and imm
        
        setFlag(CF, false)
        setFlag(OF, false)
        updateZeroFlag(result)
        updateSignFlag32(result)
        updateParityFlag(result)
        
        eax = result
    }

    private fun handleOr8(modRM: Int) {
        val srcReg = (modRM shr 3) and 0x07
        val srcValue = getReg8(srcReg)
        val dstValue = readOperand8(modRM)
        val result = dstValue or srcValue
        
        setFlag(CF, false)
        setFlag(OF, false)
        updateZeroFlag(result)
        updateSignFlag8(result)
        updateParityFlag(result)
        
        writeOperand8(modRM, result)
    }

    private fun handleOr16(modRM: Int) {
        val srcReg = (modRM shr 3) and 0x07
        val srcValue = getReg16(srcReg)
        val dstValue = readOperand16(modRM)
        val result = dstValue or srcValue
        
        setFlag(CF, false)
        setFlag(OF, false)
        updateZeroFlag(result)
        updateSignFlag16(result)
        updateParityFlag(result)
        
        writeOperand16(modRM, result)
    }

    private fun handleOr32(modRM: Int) {
        val srcReg = (modRM shr 3) and 0x07
        val srcValue = getReg32(srcReg)
        val dstValue = readOperand32(modRM)
        val result = dstValue or srcValue
        
        setFlag(CF, false)
        setFlag(OF, false)
        updateZeroFlag(result)
        updateSignFlag32(result)
        updateParityFlag(result)
        
        writeOperand32(modRM, result)
    }

    private fun handleOrR8(modRM: Int) {
        val dstReg = (modRM shr 3) and 0x07
        val srcValue = readOperand8(modRM)
        val dstValue = getReg8(dstReg)
        val result = dstValue or srcValue
        
        setFlag(CF, false)
        setFlag(OF, false)
        updateZeroFlag(result)
        updateSignFlag8(result)
        updateParityFlag(result)
        
        setReg8(dstReg, result)
    }

    private fun handleOrR16(modRM: Int) {
        val dstReg = (modRM shr 3) and 0x07
        val srcValue = readOperand16(modRM)
        val dstValue = getReg16(dstReg)
        val result = dstValue or srcValue
        
        setFlag(CF, false)
        setFlag(OF, false)
        updateZeroFlag(result)
        updateSignFlag16(result)
        updateParityFlag(result)
        
        setReg16(dstReg, result)
    }

    private fun handleOrR32(modRM: Int) {
        val dstReg = (modRM shr 3) and 0x07
        val srcValue = readOperand32(modRM)
        val dstValue = getReg32(dstReg)
        val result = dstValue or srcValue
        
        setFlag(CF, false)
        setFlag(OF, false)
        updateZeroFlag(result)
        updateSignFlag32(result)
        updateParityFlag(result)
        
        setReg32(dstReg, result)
    }

    private fun handleOrALImm8(imm: Int) {
        val dstValue = eax and 0xFF
        val result = dstValue or imm
        
        setFlag(CF, false)
        setFlag(OF, false)
        updateZeroFlag(result)
        updateSignFlag8(result)
        updateParityFlag(result)
        
        eax = (eax and 0xFFFFFF00.toInt()) or (result and 0xFF)
    }

    private fun handleOrAXImm16(imm: Int) {
        val dstValue = eax and 0xFFFF
        val result = dstValue or imm
        
        setFlag(CF, false)
        setFlag(OF, false)
        updateZeroFlag(result)
        updateSignFlag16(result)
        updateParityFlag(result)
        
        eax = (eax and 0xFFFF0000.toInt()) or (result and 0xFFFF)
    }

    private fun handleOrEAXImm32(imm: Int) {
        val dstValue = eax
        val result = dstValue or imm
        
        setFlag(CF, false)
        setFlag(OF, false)
        updateZeroFlag(result)
        updateSignFlag32(result)
        updateParityFlag(result)
        
        eax = result
    }

    private fun handleXor8(modRM: Int) {
        val srcReg = (modRM shr 3) and 0x07
        val srcValue = getReg8(srcReg)
        val dstValue = readOperand8(modRM)
        val result = dstValue xor srcValue
        
        setFlag(CF, false)
        setFlag(OF, false)
        updateZeroFlag(result)
        updateSignFlag8(result)
        updateParityFlag(result)
        
        writeOperand8(modRM, result)
    }

    private fun handleXor16(modRM: Int) {
        val srcReg = (modRM shr 3) and 0x07
        val srcValue = getReg16(srcReg)
        val dstValue = readOperand16(modRM)
        val result = dstValue xor srcValue
        
        setFlag(CF, false)
        setFlag(OF, false)
        updateZeroFlag(result)
        updateSignFlag16(result)
        updateParityFlag(result)
        
        writeOperand16(modRM, result)
    }

    private fun handleXor32(modRM: Int) {
        val srcReg = (modRM shr 3) and 0x07
        val srcValue = getReg32(srcReg)
        val dstValue = readOperand32(modRM)
        val result = dstValue xor srcValue
        
        setFlag(CF, false)
        setFlag(OF, false)
        updateZeroFlag(result)
        updateSignFlag32(result)
        updateParityFlag(result)
        
        writeOperand32(modRM, result)
    }

    private fun handleXorR8(modRM: Int) {
        val dstReg = (modRM shr 3) and 0x07
        val srcValue = readOperand8(modRM)
        val dstValue = getReg8(dstReg)
        val result = dstValue xor srcValue
        
        setFlag(CF, false)
        setFlag(OF, false)
        updateZeroFlag(result)
        updateSignFlag8(result)
        updateParityFlag(result)
        
        setReg8(dstReg, result)
    }

    private fun handleXorR16(modRM: Int) {
        val dstReg = (modRM shr 3) and 0x07
        val srcValue = readOperand16(modRM)
        val dstValue = getReg16(dstReg)
        val result = dstValue xor srcValue
        
        setFlag(CF, false)
        setFlag(OF, false)
        updateZeroFlag(result)
        updateSignFlag16(result)
        updateParityFlag(result)
        
        setReg16(dstReg, result)
    }

    private fun handleXorR32(modRM: Int) {
        val dstReg = (modRM shr 3) and 0x07
        val srcValue = readOperand32(modRM)
        val dstValue = getReg32(dstReg)
        val result = dstValue xor srcValue
        
        setFlag(CF, false)
        setFlag(OF, false)
        updateZeroFlag(result)
        updateSignFlag32(result)
        updateParityFlag(result)
        
        setReg32(dstReg, result)
    }

    private fun handleXorALImm8(imm: Int) {
        val dstValue = eax and 0xFF
        val result = dstValue xor imm
        
        setFlag(CF, false)
        setFlag(OF, false)
        updateZeroFlag(result)
        updateSignFlag8(result)
        updateParityFlag(result)
        
        eax = (eax and 0xFFFFFF00.toInt()) or (result and 0xFF)
    }

    private fun handleXorAXImm16(imm: Int) {
        val dstValue = eax and 0xFFFF
        val result = dstValue xor imm
        
        setFlag(CF, false)
        setFlag(OF, false)
        updateZeroFlag(result)
        updateSignFlag16(result)
        updateParityFlag(result)
        
        eax = (eax and 0xFFFF0000.toInt()) or (result and 0xFFFF)
    }

    private fun handleXorEAXImm32(imm: Int) {
        val dstValue = eax
        val result = dstValue xor imm
        
        setFlag(CF, false)
        setFlag(OF, false)
        updateZeroFlag(result)
        updateSignFlag32(result)
        updateParityFlag(result)
        
        eax = result
    }

    private fun handleShl8(modRM: Int, count: Int) {
        val value = readOperand8(modRM)
        val shiftCount = if (count == 1) 1 else (count and 0x1F)
        val result = value shl shiftCount
        
        // Update flags
        setFlag(CF, if (shiftCount > 0) ((value shr (8 - shiftCount)) and 1) != 0 else false)
        updateZeroFlag(result)
        updateSignFlag8(result)
        updateParityFlag(result)
        setFlag(OF, if (shiftCount == 1) ((result xor value) and 0x80) != 0 else false)
        
        writeOperand8(modRM, result and 0xFF)
    }

    private fun handleShl16(modRM: Int, count: Int) {
        val value = readOperand16(modRM)
        val shiftCount = if (count == 1) 1 else (count and 0x1F)
        val result = value shl shiftCount
        
        // Update flags
        setFlag(CF, if (shiftCount > 0) ((value shr (16 - shiftCount)) and 1) != 0 else false)
        updateZeroFlag(result)
        updateSignFlag16(result)
        updateParityFlag(result)
        setFlag(OF, if (shiftCount == 1) ((result xor value) and 0x8000) != 0 else false)
        
        writeOperand16(modRM, result and 0xFFFF)
    }

    private fun handleShl32(modRM: Int, count: Int) {
        val value = readOperand32(modRM)
        val shiftCount = if (count == 1) 1 else (count and 0x1F)
        val result = value shl shiftCount
        
        // Update flags
        setFlag(CF, if (shiftCount > 0) ((value shr (32 - shiftCount)) and 1) != 0 else false)
        updateZeroFlag(result)
        updateSignFlag32(result)
        updateParityFlag(result)
        setFlag(OF, if (shiftCount == 1) ((result xor value) and 0x80000000.toInt()) != 0 else false)
        
        writeOperand32(modRM, result)
    }

    private fun handleShr8(modRM: Int, count: Int) {
        val value = readOperand8(modRM)
        val shiftCount = if (count == 1) 1 else (count and 0x1F)
        val result = value ushr shiftCount
        
        // Update flags
        setFlag(CF, if (shiftCount > 0) ((value shr (shiftCount - 1)) and 1) != 0 else false)
        updateZeroFlag(result)
        updateSignFlag8(result)
        updateParityFlag(result)
        setFlag(OF, if (shiftCount == 1) (value and 0x80) != 0 else false)
        
        writeOperand8(modRM, result and 0xFF)
    }

    private fun handleShr16(modRM: Int, count: Int) {
        val value = readOperand16(modRM)
        val shiftCount = if (count == 1) 1 else (count and 0x1F)
        val result = value ushr shiftCount
        
        // Update flags
        setFlag(CF, if (shiftCount > 0) ((value shr (shiftCount - 1)) and 1) != 0 else false)
        updateZeroFlag(result)
        updateSignFlag16(result)
        updateParityFlag(result)
        setFlag(OF, if (shiftCount == 1) (value and 0x8000) != 0 else false)
        
        writeOperand16(modRM, result and 0xFFFF)
    }

    private fun handleShr32(modRM: Int, count: Int) {
        val value = readOperand32(modRM)
        val shiftCount = if (count == 1) 1 else (count and 0x1F)
        val result = value ushr shiftCount
        
        // Update flags
        setFlag(CF, if (shiftCount > 0) ((value shr (shiftCount - 1)) and 1) != 0 else false)
        updateZeroFlag(result)
        updateSignFlag32(result)
        updateParityFlag(result)
        setFlag(OF, if (shiftCount == 1) (value and 0x80000000.toInt()) != 0 else false)
        
        writeOperand32(modRM, result)
    }

    private fun handleSar8(modRM: Int, count: Int) {
        val value = readOperand8(modRM).toByte()
        val shiftCount = if (count == 1) 1 else (count and 0x1F)
        val result = value.toInt() shr shiftCount
        
        // Update flags
        setFlag(CF, if (shiftCount > 0) ((value.toInt() shr (shiftCount - 1)) and 1) != 0 else false)
        updateZeroFlag(result)
        updateSignFlag8(result)
        updateParityFlag(result)
        setFlag(OF, false)
        
        writeOperand8(modRM, result and 0xFF)
    }

    private fun handleSar16(modRM: Int, count: Int) {
        val value = readOperand16(modRM).toShort()
        val shiftCount = if (count == 1) 1 else (count and 0x1F)
        val result = value.toInt() shr shiftCount
        
        // Update flags
        setFlag(CF, if (shiftCount > 0) ((value.toInt() shr (shiftCount - 1)) and 1) != 0 else false)
        updateZeroFlag(result)
        updateSignFlag16(result)
        updateParityFlag(result)
        setFlag(OF, false)
        
        writeOperand16(modRM, result and 0xFFFF)
    }

    private fun handleSar32(modRM: Int, count: Int) {
        val value = readOperand32(modRM)
        val shiftCount = if (count == 1) 1 else (count and 0x1F)
        val result = value shr shiftCount
        
        // Update flags
        setFlag(CF, if (shiftCount > 0) ((value shr (shiftCount - 1)) and 1) != 0 else false)
        updateZeroFlag(result)
        updateSignFlag32(result)
        updateParityFlag(result)
        setFlag(OF, false)
        
        writeOperand32(modRM, result)
    }

    private fun handleRol8(modRM: Int, count: Int) {
        val value = readOperand8(modRM)
        val shiftCount = if (count == 1) 1 else (count and 0x1F)
        val result = ((value shl shiftCount) or (value ushr (8 - shiftCount))) and 0xFF
        
        // Update flags
        setFlag(CF, (result and 1) != 0)
        setFlag(OF, if (shiftCount == 1) ((result xor value) and 0x80) != 0 else false)
        
        writeOperand8(modRM, result)
    }

    private fun handleRol16(modRM: Int, count: Int) {
        val value = readOperand16(modRM)
        val shiftCount = if (count == 1) 1 else (count and 0x1F)
        val result = ((value shl shiftCount) or (value ushr (16 - shiftCount))) and 0xFFFF
        
        // Update flags
        setFlag(CF, (result and 1) != 0)
        setFlag(OF, if (shiftCount == 1) ((result xor value) and 0x8000) != 0 else false)
        
        writeOperand16(modRM, result)
    }

    private fun handleRol32(modRM: Int, count: Int) {
        val value = readOperand32(modRM)
        val shiftCount = if (count == 1) 1 else (count and 0x1F)
        val result = (value shl shiftCount) or (value ushr (32 - shiftCount))
        
        // Update flags
        setFlag(CF, (result and 1) != 0)
        setFlag(OF, if (shiftCount == 1) ((result xor value) and 0x80000000.toInt()) != 0 else false)
        
        writeOperand32(modRM, result)
    }

    private fun handleRor8(modRM: Int, count: Int) {
        val value = readOperand8(modRM)
        val shiftCount = if (count == 1) 1 else (count and 0x1F)
        val result = ((value ushr shiftCount) or (value shl (8 - shiftCount))) and 0xFF
        
        // Update flags
        setFlag(CF, ((value ushr (shiftCount - 1)) and 1) != 0)
        setFlag(OF, if (shiftCount == 1) ((result xor value) and 0x80) != 0 else false)
        
        writeOperand8(modRM, result)
    }

    private fun handleRor16(modRM: Int, count: Int) {
        val value = readOperand16(modRM)
        val shiftCount = if (count == 1) 1 else (count and 0x1F)
        val result = ((value ushr shiftCount) or (value shl (16 - shiftCount))) and 0xFFFF
        
        // Update flags
        setFlag(CF, ((value ushr (shiftCount - 1)) and 1) != 0)
        setFlag(OF, if (shiftCount == 1) ((result xor value) and 0x8000) != 0 else false)
        
        writeOperand16(modRM, result)
    }

    private fun handleRor32(modRM: Int, count: Int) {
        val value = readOperand32(modRM)
        val shiftCount = if (count == 1) 1 else (count and 0x1F)
        val result = (value ushr shiftCount) or (value shl (32 - shiftCount))
        
        // Update flags
        setFlag(CF, ((value ushr (shiftCount - 1)) and 1) != 0)
        setFlag(OF, if (shiftCount == 1) ((result xor value) and 0x80000000.toInt()) != 0 else false)
        
        writeOperand32(modRM, result)
    }

    private fun handleRcl8(modRM: Int, count: Int) {
        val value = readOperand8(modRM)
        val shiftCount = if (count == 1) 1 else (count and 0x1F)
        var result = value.toLong()
        
        for (i in 0 until shiftCount) {
            val oldCarry = if (getFlag(CF)) 1L else 0L
            val newCarry = ((result and 0x80) != 0L)
            result = ((result shl 1) or oldCarry) and 0xFFL
            setFlag(CF, newCarry)
        }
        
        // Update overflow flag for single shift
        if (shiftCount == 1) {
            setFlag(OF, ((result.toInt() xor value) and 0x80) != 0)
        }
        
        writeOperand8(modRM, result.toInt())
    }

    private fun handleRcl16(modRM: Int, count: Int) {
        val value = readOperand16(modRM)
        val shiftCount = if (count == 1) 1 else (count and 0x1F)
        var result = value.toLong()
        
        for (i in 0 until shiftCount) {
            val oldCarry = if (getFlag(CF)) 1L else 0L
            val newCarry = ((result and 0x8000) != 0L)
            result = ((result shl 1) or oldCarry) and 0xFFFFL
            setFlag(CF, newCarry)
        }
        
        // Update overflow flag for single shift
        if (shiftCount == 1) {
            setFlag(OF, ((result.toInt() xor value) and 0x8000) != 0)
        }
        
        writeOperand16(modRM, result.toInt())
    }

    private fun handleRcl32(modRM: Int, count: Int) {
        val value = readOperand32(modRM)
        val shiftCount = if (count == 1) 1 else (count and 0x1F)
        var result = value.toLong() and 0xFFFFFFFFL
        
        for (i in 0 until shiftCount) {
            val oldCarry = if (getFlag(CF)) 1L else 0L
            val newCarry = ((result and 0x80000000L) != 0L)
            result = ((result shl 1) or oldCarry) and 0xFFFFFFFFL
            setFlag(CF, newCarry)
        }
        
        // Update overflow flag for single shift
        if (shiftCount == 1) {
            setFlag(OF, ((result.toInt() xor value) and 0x80000000.toInt()) != 0)
        }
        
        writeOperand32(modRM, result.toInt())
    }

    private fun handleRcr8(modRM: Int, count: Int) {
        val value = readOperand8(modRM)
        val shiftCount = if (count == 1) 1 else (count and 0x1F)
        var result = value.toLong()
        
        for (i in 0 until shiftCount) {
            val oldCarry = if (getFlag(CF)) 0x80L else 0L
            val newCarry = ((result and 1) != 0L)
            result = ((result ushr 1) or oldCarry) and 0xFFL
            setFlag(CF, newCarry)
        }
        
        // Update overflow flag for single shift
        if (shiftCount == 1) {
            setFlag(OF, ((result.toInt() xor value) and 0x80) != 0)
        }
        
        writeOperand8(modRM, result.toInt())
    }

    private fun handleRcr16(modRM: Int, count: Int) {
        val value = readOperand16(modRM)
        val shiftCount = if (count == 1) 1 else (count and 0x1F)
        var result = value.toLong()
        
        for (i in 0 until shiftCount) {
            val oldCarry = if (getFlag(CF)) 0x8000L else 0L
            val newCarry = ((result and 1) != 0L)
            result = ((result ushr 1) or oldCarry) and 0xFFFFL
            setFlag(CF, newCarry)
        }
        
        // Update overflow flag for single shift
        if (shiftCount == 1) {
            setFlag(OF, ((result.toInt() xor value) and 0x8000) != 0)
        }
        
        writeOperand16(modRM, result.toInt())
    }

    private fun handleRcr32(modRM: Int, count: Int) {
        val value = readOperand32(modRM)
        val shiftCount = if (count == 1) 1 else (count and 0x1F)
        var result = value.toLong() and 0xFFFFFFFFL
        
        for (i in 0 until shiftCount) {
            val oldCarry = if (getFlag(CF)) 0x80000000L else 0L
            val newCarry = ((result and 1) != 0L)
            result = ((result ushr 1) or oldCarry) and 0xFFFFFFFFL
            setFlag(CF, newCarry)
        }
        
        // Update overflow flag for single shift
        if (shiftCount == 1) {
            setFlag(OF, ((result.toInt() xor value) and 0x80000000.toInt()) != 0)
        }
        
        writeOperand32(modRM, result.toInt())
    }

    private fun handleNeg8(modRM: Int) {
        val value = readOperand8(modRM)
        val result = -value
        
        updateCarryFlagSub(0, value, result.toLong())
        updateOverflowFlagSub(0, value, result.toLong())
        updateAuxiliaryFlagSub(0, value)
        updateZeroFlag(result)
        updateSignFlag8(result)
        updateParityFlag(result)
        
        writeOperand8(modRM, result and 0xFF)
    }

    private fun handleNeg16(modRM: Int) {
        val value = readOperand16(modRM)
        val result = -value
        
        updateCarryFlagSub(0, value, result.toLong())
        updateOverflowFlagSub(0, value, result.toLong())
        updateAuxiliaryFlagSub(0, value)
        updateZeroFlag(result)
        updateSignFlag16(result)
        updateParityFlag(result)
        
        writeOperand16(modRM, result and 0xFFFF)
    }

    private fun handleNeg32(modRM: Int) {
        val value = readOperand32(modRM)
        val result = -value
        
        updateCarryFlagSub(0, value, result.toLong())
        updateOverflowFlagSub(0, value, result.toLong())
        updateAuxiliaryFlagSub(0, value)
        updateZeroFlag(result)
        updateSignFlag32(result)
        updateParityFlag(result)
        
        writeOperand32(modRM, result)
    }

    private fun handleNot8(modRM: Int) {
        val value = readOperand8(modRM)
        val result = value.inv() and 0xFF
        writeOperand8(modRM, result)
    }

    private fun handleNot16(modRM: Int) {
        val value = readOperand16(modRM)
        val result = value.inv() and 0xFFFF
        writeOperand16(modRM, result)
    }

    private fun handleNot32(modRM: Int) {
        val value = readOperand32(modRM)
        val result = value.inv()
        writeOperand32(modRM, result)
    }

    private fun handleMul8(modRM: Int) {
        val value = readOperand8(modRM)
        val al = eax and 0xFF
        val result = al.toLong() * value.toLong()
        
        eax = ((result and 0xFF00L).toInt() shl 8) or (result.toInt() and 0xFF)
        
        // Update flags
        val highByte = (result ushr 8) and 0xFF
        setFlag(CF, highByte != 0L)
        setFlag(OF, highByte != 0L)
    }

    private fun handleMul16(modRM: Int) {
        val value = readOperand16(modRM)
        val ax = eax and 0xFFFF
        val result = ax.toLong() * value.toLong()
        
        eax = (result.toInt() and 0xFFFF)
        edx = ((result ushr 16) and 0xFFFF).toInt()
        
        // Update flags
        val highWord = (result ushr 16) and 0xFFFF
        setFlag(CF, highWord != 0L)
        setFlag(OF, highWord != 0L)
    }

    private fun handleMul32(modRM: Int) {
        val value = readOperand32(modRM)
        val eaxVal = eax.toLong() and 0xFFFFFFFFL
        val result = eaxVal * (value.toLong() and 0xFFFFFFFFL)
        
        eax = (result and 0xFFFFFFFFL).toInt()
        edx = ((result ushr 32) and 0xFFFFFFFFL).toInt()
        
        // Update flags
        val highDword = (result ushr 32) and 0xFFFFFFFFL
        setFlag(CF, highDword != 0L)
        setFlag(OF, highDword != 0L)
    }

    private fun handleIMul8(modRM: Int) {
        val value = readOperand8(modRM).toByte()
        val al = (eax and 0xFF).toByte()
        val result = al.toInt() * value.toInt()
        
        eax = ((result and 0xFF00) shl 8) or (result and 0xFF)
        
        // Update flags
        val highByte = (result ushr 8) and 0xFF
        setFlag(CF, highByte != 0)
        setFlag(OF, highByte != 0)
    }

    private fun handleIMul16(modRM: Int) {
        val value = readOperand16(modRM).toShort()
        val ax = (eax and 0xFFFF).toShort()
        val result = ax.toInt() * value.toInt()
        
        eax = (result and 0xFFFF)
        edx = ((result ushr 16) and 0xFFFF)
        
        // Update flags
        val highWord = (result ushr 16) and 0xFFFF
        setFlag(CF, highWord != 0)
        setFlag(OF, highWord != 0)
    }

    private fun handleIMul32(modRM: Int) {
        val value = readOperand32(modRM)
        val result = eax.toLong() * value.toLong()
        
        eax = (result and 0xFFFFFFFFL).toInt()
        edx = ((result ushr 32) and 0xFFFFFFFFL).toInt()
        
        // Update flags
        val highDword = (result ushr 32) and 0xFFFFFFFFL
        setFlag(CF, highDword != 0L)
        setFlag(OF, highDword != 0L)
    }

    private fun handleIMul32Imm8(modRM: Int, imm: Int) {
        val dstReg = (modRM shr 3) and 0x07
        val value = readOperand32(modRM)
        val result = value * imm
        
        setReg32(dstReg, result)
        
        // Update flags
        // Check for overflow (result doesn't fit in 32-bit signed)
        val longResult = value.toLong() * imm.toLong()
        setFlag(CF, longResult != result.toLong())
        setFlag(OF, longResult != result.toLong())
    }

    private fun handleIMul32Imm32(modRM: Int, imm: Int) {
        val dstReg = (modRM shr 3) and 0x07
        val value = readOperand32(modRM)
        val result = value * imm
        
        setReg32(dstReg, result)
        
        // Update flags
        val longResult = value.toLong() * imm.toLong()
        setFlag(CF, longResult != result.toLong())
        setFlag(OF, longResult != result.toLong())
    }

    private fun handleIMul32Reg(modRM: Int) {
        val dstReg = (modRM shr 3) and 0x07
        val srcValue = readOperand32(modRM)
        val dstValue = getReg32(dstReg)
        val result = dstValue * srcValue
        
        setReg32(dstReg, result)
        
        // Update flags
        val longResult = dstValue.toLong() * srcValue.toLong()
        setFlag(CF, longResult != result.toLong())
        setFlag(OF, longResult != result.toLong())
    }

    private fun handleDiv8(modRM: Int) {
        val divisor = readOperand8(modRM)
        if (divisor == 0) {
            handleDivideByZero()
            return
        }
        
        val dividend = eax and 0xFFFF
        val quotient = dividend / divisor
        val remainder = dividend % divisor
        
        if (quotient > 0xFF) {
            handleDivideOverflow()
            return
        }
        
        eax = ((remainder and 0xFF) shl 8) or (quotient and 0xFF)
    }

    private fun handleDiv16(modRM: Int) {
        val divisor = readOperand16(modRM)
        if (divisor == 0) {
            handleDivideByZero()
            return
        }
        
        val dividend = ((edx and 0xFFFF).toLong() shl 16) or (eax and 0xFFFF).toLong()
        val quotient = (dividend / divisor).toInt()
        val remainder = (dividend % divisor).toInt()
        
        if (quotient > 0xFFFF) {
            handleDivideOverflow()
            return
        }
        
        eax = quotient and 0xFFFF
        edx = remainder and 0xFFFF
    }

    private fun handleDiv32(modRM: Int) {
    val divisor = readOperand32(modRM).toLong() // استخدم Long
    if (divisor == 0L) {
        handleDivideByZero()
        return
    }

    // دمج EDX:EAX → dividend (64-bit)
    val dividend = (edx.toLong() shl 32) or (eax.toLong() and 0xFFFFFFFFL)

    val quotient = dividend / divisor
    val remainder = dividend % divisor

    // تحقق من overflow
    if (quotient < Int.MIN_VALUE.toLong() || quotient > Int.MAX_VALUE.toLong()) {
        handleDivideOverflow()
        return
    }

    // فقط هنا نحول للقيم 32-bit
    eax = quotient.toInt()
    edx = remainder.toInt()
}

fun setEntryPoint(entry: Int) {
        eip = entry
    }

    private fun handleIDiv8(modRM: Int) {
        val divisor = readOperand8(modRM).toByte()
        if (divisor == 0.toByte()) {
            handleDivideByZero()
            return
        }
        
        val dividend = (eax and 0xFFFF).toShort()
        val quotient = (dividend / divisor.toInt()).toByte()
        val remainder = (dividend % divisor.toInt()).toByte()
        
        if (quotient.toInt() < -128 || quotient.toInt() > 127) {
            handleDivideOverflow()
            return
        }
        
        eax = ((remainder.toInt() and 0xFF) shl 8) or (quotient.toInt() and 0xFF)
    }

   private fun handleIDiv16(modRM: Int) {
    val divisor = readOperand16(modRM).toInt()  // استخدم Int مباشرة
    if (divisor == 0) {
        handleDivideByZero()
        return
    }

    val dividend = ((edx and 0xFFFF).toLong() shl 16) or (eax and 0xFFFF).toLong()
    val quotient = dividend / divisor.toLong()
    val remainder = dividend % divisor.toLong()

    if (quotient < Short.MIN_VALUE || quotient > Short.MAX_VALUE) {
        handleDivideOverflow()
        return
    }

    eax = (quotient.toInt() and 0xFFFF)
    edx = (remainder.toInt() and 0xFFFF)
}

private fun handleIDiv32(modRM: Int) {
    val divisor = readOperand32(modRM).toLong()
    if (divisor == 0L) {
        handleDivideByZero()
        return
    }

    val dividend = (edx.toLong() shl 32) or (eax.toLong() and 0xFFFFFFFFL)
    val quotient = dividend / divisor
    val remainder = dividend % divisor

    if (quotient < Int.MIN_VALUE.toLong() || quotient > Int.MAX_VALUE.toLong()) {
        handleDivideOverflow()
        return
    }

    eax = (quotient.toInt())
    edx = (remainder.toInt())
}

    private fun handleDivideByZero() {
        // Raise exception 0 (Divide by Zero)
        handleInterrupt(0)
    }

    private fun handleDivideOverflow() {
        // Raise exception 0 (Divide by Zero) for overflow
        handleInterrupt(0)
    }

    private fun handleLea32(modRM: Int) {
        val dstReg = (modRM shr 3) and 0x07
        val ea = calculateEA(modRM)
        setReg32(dstReg, ea)
    }

    private fun handleXchg8(modRM: Int) {
        val srcReg = (modRM shr 3) and 0x07
        val srcValue = getReg8(srcReg)
        val dstValue = readOperand8(modRM)
        
        setReg8(srcReg, dstValue)
        writeOperand8(modRM, srcValue)
    }

    private fun handleXchg16(modRM: Int) {
        val srcReg = (modRM shr 3) and 0x07
        val srcValue = getReg16(srcReg)
        val dstValue = readOperand16(modRM)
        
        setReg16(srcReg, dstValue)
        writeOperand16(modRM, srcValue)
    }

    private fun handleXchg32(modRM: Int) {
        val srcReg = (modRM shr 3) and 0x07
        val srcValue = getReg32(srcReg)
        val dstValue = readOperand32(modRM)
        
        setReg32(srcReg, dstValue)
        writeOperand32(modRM, srcValue)
    }

    private fun handleXchgEAX(reg: Int) {
        val otherValue = getReg32(reg)
        val temp = eax
        eax = otherValue
        setReg32(reg, temp)
    }

    private fun handleBswap(reg: Int) {
        val value = getReg32(reg)
        val result = ((value and 0xFF) shl 24) or
                    ((value and 0xFF00) shl 8) or
                    ((value and 0xFF0000) ushr 8) or
                    ((value and 0xFF000000.toInt()) ushr 24)
        setReg32(reg, result)
    }

    private fun handleCmovcc(condition: Int, modRM: Int) {
        val dstReg = (modRM shr 3) and 0x07
        val srcValue = readOperand32(modRM)
        
        var takeMove = false
        
        when (condition) {
            0 -> takeMove = getFlag(OF)  // CMOVO
            1 -> takeMove = !getFlag(OF) // CMOVNO
            2 -> takeMove = getFlag(CF)  // CMOVB/CMOVNAE
            3 -> takeMove = !getFlag(CF) // CMOVNB/CMOVAE
            4 -> takeMove = getFlag(ZF)  // CMOVE/CMOVZ
            5 -> takeMove = !getFlag(ZF) // CMOVNE/CMOVNZ
            6 -> takeMove = getFlag(CF) || getFlag(ZF) // CMOVBE/CMOVNA
            7 -> takeMove = !(getFlag(CF) || getFlag(ZF)) // CMOVNBE/CMOVA
            8 -> takeMove = getFlag(SF)  // CMOVS
            9 -> takeMove = !getFlag(SF) // CMOVNS
            10 -> takeMove = getFlag(PF) // CMOVP/CMOVPE
            11 -> takeMove = !getFlag(PF) // CMOVNP/CMOVPO
            12 -> takeMove = getFlag(SF) != getFlag(OF) // CMOVL/CMOVNGE
            13 -> takeMove = getFlag(SF) == getFlag(OF) // CMOVNL/CMOVGE
            14 -> takeMove = getFlag(ZF) || (getFlag(SF) != getFlag(OF)) // CMOVLE/CMOVNG
            15 -> takeMove = !getFlag(ZF) && (getFlag(SF) == getFlag(OF)) // CMOVNLE/CMOVG
        }
        
        if (takeMove) {
            setReg32(dstReg, srcValue)
        }
    }

    private fun handleSetcc(condition: Int, modRM: Int) {
        var takeSet = false
        
        when (condition) {
            0 -> takeSet = getFlag(OF)  // SETO
            1 -> takeSet = !getFlag(OF) // SETNO
            2 -> takeSet = getFlag(CF)  // SETB/SETNAE
            3 -> takeSet = !getFlag(CF) // SETNB/SETAE
            4 -> takeSet = getFlag(ZF)  // SETE/SETZ
            5 -> takeSet = !getFlag(ZF) // SETNE/SETNZ
            6 -> takeSet = getFlag(CF) || getFlag(ZF) // SETBE/SETNA
            7 -> takeSet = !(getFlag(CF) || getFlag(ZF)) // SETNBE/SETA
            8 -> takeSet = getFlag(SF)  // SETS
            9 -> takeSet = !getFlag(SF) // SETNS
            10 -> takeSet = getFlag(PF) // SETP/SETPE
            11 -> takeSet = !getFlag(PF) // SETNP/SETPO
            12 -> takeSet = getFlag(SF) != getFlag(OF) // SETL/SETNGE
            13 -> takeSet = getFlag(SF) == getFlag(OF) // SETNL/SETGE
            14 -> takeSet = getFlag(ZF) || (getFlag(SF) != getFlag(OF)) // SETLE/SETNG
            15 -> takeSet = !getFlag(ZF) && (getFlag(SF) == getFlag(OF)) // SETNLE/SETG
        }
        
        writeOperand8(modRM, if (takeSet) 1 else 0)
    }

    private fun handleJumpCC(condition: Int) {
        val offset = fetchSigned8()
        var takeJump = false
        
        when (condition) {
            0 -> takeJump = getFlag(OF)  // JO
            1 -> takeJump = !getFlag(OF) // JNO
            2 -> takeJump = getFlag(CF)  // JB/JNAE/JC
            3 -> takeJump = !getFlag(CF) // JNB/JAE/JNC
            4 -> takeJump = getFlag(ZF)  // JZ/JE
            5 -> takeJump = !getFlag(ZF) // JNZ/JNE
            6 -> takeJump = getFlag(CF) || getFlag(ZF) // JBE/JNA
            7 -> takeJump = !(getFlag(CF) || getFlag(ZF)) // JNBE/JA
            8 -> takeJump = getFlag(SF)  // JS
            9 -> takeJump = !getFlag(SF) // JNS
            10 -> takeJump = getFlag(PF) // JP/JPE
            11 -> takeJump = !getFlag(PF) // JNP/JPO
            12 -> takeJump = getFlag(SF) != getFlag(OF) // JL/JNGE
            13 -> takeJump = getFlag(SF) == getFlag(OF) // JNL/JGE
            14 -> takeJump = getFlag(ZF) || (getFlag(SF) != getFlag(OF)) // JLE/JNG
            15 -> takeJump = !getFlag(ZF) && (getFlag(SF) == getFlag(OF)) // JNLE/JG
        }
        
        if (takeJump) {
            eip = (eip + offset) and 0xFFFFFFFF.toInt()
            branchCount++
        }
    }

    private fun handleLoopnz() {
        val offset = fetchSigned8()
        ecx = (ecx - 1) and 0xFFFFFFFF.toInt()
        
        if (ecx != 0 && !getFlag(ZF)) {
            eip = (eip + offset) and 0xFFFFFFFF.toInt()
        }
        branchCount++
    }

    private fun handleLoopz() {
        val offset = fetchSigned8()
        ecx = (ecx - 1) and 0xFFFFFFFF.toInt()
        
        if (ecx != 0 && getFlag(ZF)) {
            eip = (eip + offset) and 0xFFFFFFFF.toInt()
        }
        branchCount++
    }

    private fun handleLoop() {
        val offset = fetchSigned8()
        ecx = (ecx - 1) and 0xFFFFFFFF.toInt()
        
        if (ecx != 0) {
            eip = (eip + offset) and 0xFFFFFFFF.toInt()
        }
        branchCount++
    }

    private fun handleJecxz() {
        val offset = fetchSigned8()
        if (ecx == 0) {
            eip = (eip + offset) and 0xFFFFFFFF.toInt()
            branchCount++
        }
    }

    private fun handleMovsb() {
        val srcAddr = esi
        val dstAddr = edi
        val value = memory.read8(srcAddr)
        memory.write8(dstAddr, value)
        
        if (getFlag(DF)) {
            esi = (esi - 1) and 0xFFFFFFFF.toInt()
            edi = (edi - 1) and 0xFFFFFFFF.toInt()
        } else {
            esi = (esi + 1) and 0xFFFFFFFF.toInt()
            edi = (edi + 1) and 0xFFFFFFFF.toInt()
        }
        
        if (repPrefix) {
            ecx = (ecx - 1) and 0xFFFFFFFF.toInt()
            if (ecx != 0) {
                eip = (eip - 2) and 0xFFFFFFFF.toInt() // Repeat instruction
            } else {
                repPrefix = false
            }
        }
    }

    private fun handleMovsd() {
        val srcAddr = esi
        val dstAddr = edi
        val value = memory.read32(srcAddr)
        memory.write32(dstAddr, value)
        
        if (getFlag(DF)) {
            esi = (esi - 4) and 0xFFFFFFFF.toInt()
            edi = (edi - 4) and 0xFFFFFFFF.toInt()
        } else {
            esi = (esi + 4) and 0xFFFFFFFF.toInt()
            edi = (edi + 4) and 0xFFFFFFFF.toInt()
        }
        
        if (repPrefix) {
            ecx = (ecx - 1) and 0xFFFFFFFF.toInt()
            if (ecx != 0) {
                eip = (eip - 2) and 0xFFFFFFFF.toInt() // Repeat instruction
            } else {
                repPrefix = false
            }
        }
    }

    private fun handleCmpsb() {
        val srcAddr = esi
        val dstAddr = edi
        val srcValue = memory.read8(srcAddr)
        val dstValue = memory.read8(dstAddr)
        val result = dstValue.toLong() - srcValue.toLong()
        
        updateCarryFlagSub(dstValue, srcValue, result)
        updateOverflowFlagSub(dstValue, srcValue, result)
        updateAuxiliaryFlagSub(dstValue, srcValue)
        updateZeroFlag(result.toInt())
        updateSignFlag8(result.toInt())
        updateParityFlag(result.toInt())
        
        if (getFlag(DF)) {
            esi = (esi - 1) and 0xFFFFFFFF.toInt()
            edi = (edi - 1) and 0xFFFFFFFF.toInt()
        } else {
            esi = (esi + 1) and 0xFFFFFFFF.toInt()
            edi = (edi + 1) and 0xFFFFFFFF.toInt()
        }
        
        if (repPrefix || repnePrefix) {
            ecx = (ecx - 1) and 0xFFFFFFFF.toInt()
            val repeatCondition = if (repPrefix) getFlag(ZF) else !getFlag(ZF)
            if (ecx != 0 && repeatCondition) {
                eip = (eip - 2) and 0xFFFFFFFF.toInt() // Repeat instruction
            } else {
                repPrefix = false
                repnePrefix = false
            }
        }
    }

    private fun handleCmpsd() {
        val srcAddr = esi
        val dstAddr = edi
        val srcValue = memory.read32(srcAddr)
        val dstValue = memory.read32(dstAddr)
        val result = dstValue.toLong() - srcValue.toLong()
        
        updateCarryFlagSub(dstValue, srcValue, result)
        updateOverflowFlagSub(dstValue, srcValue, result)
        updateAuxiliaryFlagSub(dstValue, srcValue)
        updateZeroFlag(result.toInt())
        updateSignFlag32(result.toInt())
        updateParityFlag(result.toInt())
        
        if (getFlag(DF)) {
            esi = (esi - 4) and 0xFFFFFFFF.toInt()
            edi = (edi - 4) and 0xFFFFFFFF.toInt()
        } else {
            esi = (esi + 4) and 0xFFFFFFFF.toInt()
            edi = (edi + 4) and 0xFFFFFFFF.toInt()
        }
        
        if (repPrefix || repnePrefix) {
            ecx = (ecx - 1) and 0xFFFFFFFF.toInt()
            val repeatCondition = if (repPrefix) getFlag(ZF) else !getFlag(ZF)
            if (ecx != 0 && repeatCondition) {
                eip = (eip - 2) and 0xFFFFFFFF.toInt() // Repeat instruction
            } else {
                repPrefix = false
                repnePrefix = false
            }
        }
    }

    private fun handleStosb() {
        val dstAddr = edi
        val value = eax and 0xFF
        memory.write8(dstAddr, value)
        
        if (getFlag(DF)) {
            edi = (edi - 1) and 0xFFFFFFFF.toInt()
        } else {
            edi = (edi + 1) and 0xFFFFFFFF.toInt()
        }
        
        if (repPrefix) {
            ecx = (ecx - 1) and 0xFFFFFFFF.toInt()
            if (ecx != 0) {
                eip = (eip - 2) and 0xFFFFFFFF.toInt() // Repeat instruction
            } else {
                repPrefix = false
            }
        }
    }

    private fun handleStosd() {
        val dstAddr = edi
        val value = eax
        memory.write32(dstAddr, value)
        
        if (getFlag(DF)) {
            edi = (edi - 4) and 0xFFFFFFFF.toInt()
        } else {
            edi = (edi + 4) and 0xFFFFFFFF.toInt()
        }
        
        if (repPrefix) {
            ecx = (ecx - 1) and 0xFFFFFFFF.toInt()
            if (ecx != 0) {
                eip = (eip - 2) and 0xFFFFFFFF.toInt() // Repeat instruction
            } else {
                repPrefix = false
            }
        }
    }

    private fun handleLodsb() {
        val srcAddr = esi
        val value = memory.read8(srcAddr)
        eax = (eax and 0xFFFFFF00.toInt()) or (value and 0xFF)
        
        if (getFlag(DF)) {
            esi = (esi - 1) and 0xFFFFFFFF.toInt()
        } else {
            esi = (esi + 1) and 0xFFFFFFFF.toInt()
        }
        
        if (repPrefix) {
            ecx = (ecx - 1) and 0xFFFFFFFF.toInt()
            if (ecx != 0) {
                eip = (eip - 2) and 0xFFFFFFFF.toInt() // Repeat instruction
            } else {
                repPrefix = false
            }
        }
    }

    private fun handleLodsd() {
        val srcAddr = esi
        val value = memory.read32(srcAddr)
        eax = value
        
        if (getFlag(DF)) {
            esi = (esi - 4) and 0xFFFFFFFF.toInt()
        } else {
            esi = (esi + 4) and 0xFFFFFFFF.toInt()
        }
        
        if (repPrefix) {
            ecx = (ecx - 1) and 0xFFFFFFFF.toInt()
            if (ecx != 0) {
                eip = (eip - 2) and 0xFFFFFFFF.toInt() // Repeat instruction
            } else {
                repPrefix = false
            }
        }
    }

    private fun handleScasb() {
        val dstAddr = edi
        val dstValue = memory.read8(dstAddr)
        val srcValue = eax and 0xFF
        val result = dstValue.toLong() - srcValue.toLong()
        
        updateCarryFlagSub(dstValue, srcValue, result)
        updateOverflowFlagSub(dstValue, srcValue, result)
        updateAuxiliaryFlagSub(dstValue, srcValue)
        updateZeroFlag(result.toInt())
        updateSignFlag8(result.toInt())
        updateParityFlag(result.toInt())
        
        if (getFlag(DF)) {
            edi = (edi - 1) and 0xFFFFFFFF.toInt()
        } else {
            edi = (edi + 1) and 0xFFFFFFFF.toInt()
        }
        
        if (repPrefix || repnePrefix) {
            ecx = (ecx - 1) and 0xFFFFFFFF.toInt()
            val repeatCondition = if (repPrefix) getFlag(ZF) else !getFlag(ZF)
            if (ecx != 0 && repeatCondition) {
                eip = (eip - 2) and 0xFFFFFFFF.toInt() // Repeat instruction
            } else {
                repPrefix = false
                repnePrefix = false
            }
        }
    }

    private fun handleScasd() {
        val dstAddr = edi
        val dstValue = memory.read32(dstAddr)
        val srcValue = eax
        val result = dstValue.toLong() - srcValue.toLong()
        
        updateCarryFlagSub(dstValue, srcValue, result)
        updateOverflowFlagSub(dstValue, srcValue, result)
        updateAuxiliaryFlagSub(dstValue, srcValue)
        updateZeroFlag(result.toInt())
        updateSignFlag32(result.toInt())
        updateParityFlag(result.toInt())
        
        if (getFlag(DF)) {
            edi = (edi - 4) and 0xFFFFFFFF.toInt()
        } else {
            edi = (edi + 4) and 0xFFFFFFFF.toInt()
        }
        
        if (repPrefix || repnePrefix) {
            ecx = (ecx - 1) and 0xFFFFFFFF.toInt()
            val repeatCondition = if (repPrefix) getFlag(ZF) else !getFlag(ZF)
            if (ecx != 0 && repeatCondition) {
                eip = (eip - 2) and 0xFFFFFFFF.toInt() // Repeat instruction
            } else {
                repPrefix = false
                repnePrefix = false
            }
        }
    }

    private fun handleBt(modRM: Int) {
        val bitBase = readOperand32(modRM)
        val bitOffset = getReg32(modRM and 0x07)
        val bitPosition = bitOffset and 0x1F
        val bitMask = 1 shl bitPosition
        
        setFlag(CF, (bitBase and bitMask) != 0)
    }

    private fun handleBts(modRM: Int) {
        val bitBaseAddr = calculateEA(modRM)
        val bitOffset = getReg32(modRM and 0x07)
        val bitPosition = bitOffset and 0x1F
        val bitMask = 1 shl bitPosition
        
        val value = memory.read32(bitBaseAddr)
        setFlag(CF, (value and bitMask) != 0)
        memory.write32(bitBaseAddr, value or bitMask)
    }

    private fun handleBtr(modRM: Int) {
        val bitBaseAddr = calculateEA(modRM)
        val bitOffset = getReg32(modRM and 0x07)
        val bitPosition = bitOffset and 0x1F
        val bitMask = 1 shl bitPosition
        
        val value = memory.read32(bitBaseAddr)
        setFlag(CF, (value and bitMask) != 0)
        memory.write32(bitBaseAddr, value and bitMask.inv())
    }

    private fun handleBtc(modRM: Int) {
        val bitBaseAddr = calculateEA(modRM)
        val bitOffset = getReg32(modRM and 0x07)
        val bitPosition = bitOffset and 0x1F
        val bitMask = 1 shl bitPosition
        
        val value = memory.read32(bitBaseAddr)
        setFlag(CF, (value and bitMask) != 0)
        memory.write32(bitBaseAddr, value xor bitMask)
    }

    private fun handleBsf(modRM: Int) {
        val dstReg = (modRM shr 3) and 0x07
        val srcValue = readOperand32(modRM)
        
        if (srcValue == 0) {
            setFlag(ZF, true)
            setReg32(dstReg, 0)
        } else {
            setFlag(ZF, false)
            var bit = 0
            while ((srcValue and (1 shl bit)) == 0) {
                bit++
            }
            setReg32(dstReg, bit)
        }
    }

    private fun handleBsr(modRM: Int) {
        val dstReg = (modRM shr 3) and 0x07
        val srcValue = readOperand32(modRM)
        
        if (srcValue == 0) {
            setFlag(ZF, true)
            setReg32(dstReg, 0)
        } else {
            setFlag(ZF, false)
            var bit = 31
            while ((srcValue and (1 shl bit)) == 0) {
                bit--
            }
            setReg32(dstReg, bit)
        }
    }

    private fun handleMfence() {
        // Memory fence - ensure all memory operations complete
        // In emulation, we can just return immediately
        cycles += 5
    }

    private fun handleSfence() {
        // Store fence - ensure all stores complete
        cycles += 5
    }

    private fun handleLfence() {
        // Load fence - ensure all loads complete
        cycles += 5
    }

    private fun handlePrefetchT0(modRM: Int) {
        // Prefetch data into all cache levels
        val addr = calculateEA(modRM)
        // Just mark as accessed for TLB
        translateAddress(addr)
        cycles += 1
    }

    private fun handlePrefetchT1(modRM: Int) {
        // Prefetch data into all cache levels except L1
        val addr = calculateEA(modRM)
        translateAddress(addr)
        cycles += 1
    }

    private fun handlePrefetchT2(modRM: Int) {
        // Prefetch data into L2 cache only
        val addr = calculateEA(modRM)
        translateAddress(addr)
        cycles += 1
    }

    private fun handlePrefetchNta(modRM: Int) {
        // Prefetch data into non-temporal cache
        val addr = calculateEA(modRM)
        translateAddress(addr)
        cycles += 1
    }

    private fun handlePrefetch(modRM: Int) {
        // General prefetch
        val addr = calculateEA(modRM)
        translateAddress(addr)
        cycles += 1
    }

    private fun handlePrefetchw(modRM: Int) {
        // Prefetch for write
        val addr = calculateEA(modRM)
        translateAddress(addr)
        cycles += 1
    }

    private fun handleCpuid() {
        when (eax) {
            0 -> {
                // Get vendor string
                eax = 1  // Maximum supported CPUID level
                ebx = 0x756E6547  // "Genu"
                edx = 0x49656E69  // "ineI"
                ecx = 0x6C65746E  // "ntel"
            }
            1 -> {
                // Get processor info
                eax = 0x00000680  // Family 6, Model 8, Stepping 0
                ebx = 0x00000800  // Local APIC ID
                ecx = 0x00000000  // Feature flags (simplified)
                edx = 0x00000001  // Feature flags (FPU present)
            }
            0x80000000.toInt() -> {
                // Get extended CPUID max level
                eax = 0x80000001.toInt() and 0xFFFFFFFF.toInt()
            }
            0x80000001.toInt() -> {
                // Get extended processor info
                eax = 0x00000000
                ebx = 0x00000000
                ecx = 0x00000000
                edx = 0x00000000
            }
            else -> {
                // Unknown CPUID function
                eax = 0
                ebx = 0
                ecx = 0
                edx = 0
            }
        }
    }

    private fun handleRdtsc() {
        // Read Time Stamp Counter
        val timeStamp = cycles
        eax = (timeStamp and 0xFFFFFFFFL).toInt()
        edx = ((timeStamp ushr 32) and 0xFFFFFFFFL).toInt()
    }

    private fun handleFchs() {
        val value = fpuGetReg(0)
        fpuSetReg(0, -value)
        fpuInstructions++
        cycles += 2
    }

    private fun handleFabs() {
        val value = fpuGetReg(0)
        fpuSetReg(0, kotlin.math.abs(value))
        fpuInstructions++
        cycles += 2
    }

    private fun handleFtest() {
        // FTST compares ST(0) with 0.0
        val value = fpuGetReg(0)
        
        // Clear comparison flags in status word
        fpuStatusWord = fpuStatusWord and 0xFCFF
        
        when {
            value.isNaN() -> {
                fpuStatusWord = fpuStatusWord or 0x4500 // Invalid operation
            }
            value == 0.0 -> {
                fpuStatusWord = fpuStatusWord or 0x4000 // Equal
            }
            value < 0.0 -> {
                fpuStatusWord = fpuStatusWord or 0x0100 // Less than
            }
            value > 0.0 -> {
                fpuStatusWord = fpuStatusWord or 0x0000 // Greater than
            }
        }
        fpuInstructions++
        cycles += 4
    }

    private fun handleFld1() {
        fpuPush(1.0)
        fpuInstructions++
        cycles += 2
    }

    private fun handleFldl2t() {
        fpuPush(kotlin.math.ln(10.0) / kotlin.math.ln(2.0))
        fpuInstructions++
        cycles += 2
    }

    private fun handleFldl2e() {
        fpuPush(kotlin.math.ln(kotlin.math.E) / kotlin.math.ln(2.0))
        fpuInstructions++
        cycles += 2
    }

    private fun handleFldpi() {
        fpuPush(kotlin.math.PI)
        fpuInstructions++
        cycles += 2
    }

    private fun handleFldlg2() {
        fpuPush(kotlin.math.log10(2.0))
        fpuInstructions++
        cycles += 2
    }

    private fun handleFldln2() {
        fpuPush(kotlin.math.ln(2.0))
        fpuInstructions++
        cycles += 2
    }

    private fun handleFldz() {
        fpuPush(0.0)
        fpuInstructions++
        cycles += 2
    }

    private fun handleF2xm1() {
    val value = fpuGetReg(0)
    val result = 2.0.pow(value) - 1.0
    fpuSetReg(0, result)
    fpuInstructions++
    cycles += 30
}

private fun handleFscale() {
    val value = fpuGetReg(0)
    val scale = floor(fpuGetReg(1))
    val result = value * 2.0.pow(scale)
    fpuSetReg(0, result)
    fpuInstructions++
    cycles += 30
}

    

    private fun handleFcmovb(operand: Int) {
        if (getFlag(CF)) {
            val i = operand and 7
            fpuSetReg(0, fpuGetReg(i))
        }
        fpuInstructions++
        cycles += 4
    }

    private fun handleFcmove(operand: Int) {
        if (getFlag(ZF)) {
            val i = operand and 7
            fpuSetReg(0, fpuGetReg(i))
        }
        fpuInstructions++
        cycles += 4
    }

    private fun handleFcmovbe(operand: Int) {
        if (getFlag(CF) || getFlag(ZF)) {
            val i = operand and 7
            fpuSetReg(0, fpuGetReg(i))
        }
        fpuInstructions++
        cycles += 4
    }

    private fun handleFcmovu(operand: Int) {
        if (getFlag(PF)) {
            val i = operand and 7
            fpuSetReg(0, fpuGetReg(i))
        }
        fpuInstructions++
        cycles += 4
    }

    private fun handleFcmovnb(operand: Int) {
        if (!getFlag(CF)) {
            val i = operand and 7
            fpuSetReg(0, fpuGetReg(i))
        }
        fpuInstructions++
        cycles += 4
    }

    private fun handleFcmovne(operand: Int) {
        if (!getFlag(ZF)) {
            val i = operand and 7
            fpuSetReg(0, fpuGetReg(i))
        }
        fpuInstructions++
        cycles += 4
    }

    private fun handleFcmovnbe(operand: Int) {
        if (!getFlag(CF) && !getFlag(ZF)) {
            val i = operand and 7
            fpuSetReg(0, fpuGetReg(i))
        }
        fpuInstructions++
        cycles += 4
    }

    private fun handleFcmovnu(operand: Int) {
        if (!getFlag(PF)) {
            val i = operand and 7
            fpuSetReg(0, fpuGetReg(i))
        }
        fpuInstructions++
        cycles += 4
    }

    private fun handleFclex() {
        // Clear exceptions
        fpuStatusWord = fpuStatusWord and 0xFF00
        fpuInstructions++
        cycles += 3
    }

    private fun handleFfree(operand: Int) {
        val i = operand and 7
        val bitPos = ((fpuTop + i) and 7) * 2
        fpuTagWord = fpuTagWord or (3 shl bitPos) // Mark as empty
        fpuInstructions++
        cycles += 2
    }

    private fun handleFucom(operand: Int) {
        val value = fpuGetReg(0)
        val compareWith = fpuGetReg(operand and 7)
        
        // Clear comparison flags in status word
        fpuStatusWord = fpuStatusWord and 0xFCFF
        
        when {
            value.isNaN() || compareWith.isNaN() -> {
                // Unordered
                fpuStatusWord = fpuStatusWord or 0x4500
            }
            value == compareWith -> {
                fpuStatusWord = fpuStatusWord or 0x4000 // Equal
            }
            value < compareWith -> {
                fpuStatusWord = fpuStatusWord or 0x0100 // Less than
            }
            value > compareWith -> {
                fpuStatusWord = fpuStatusWord or 0x0000 // Greater than
            }
        }
        fpuInstructions++
        cycles += 4
    }

    private fun handleFucomp(operand: Int) {
        handleFucom(operand)
        fpuPop()
    }

    private fun handleFucomip(operand: Int) {
        handleFucom(operand)
        fpuPop()
        
        // Update CPU flags from FPU status
        setFlag(CF, (fpuStatusWord and 0x0100) != 0)
        setFlag(PF, (fpuStatusWord and 0x0400) != 0)
        setFlag(ZF, (fpuStatusWord and 0x4000) != 0)
    }

    private fun handleFcomip(operand: Int) {
        handleFcom(operand)
        fpuPop()
        
        // Update CPU flags from FPU status
        setFlag(CF, (fpuStatusWord and 0x0100) != 0)
        setFlag(PF, (fpuStatusWord and 0x0400) != 0)
        setFlag(ZF, (fpuStatusWord and 0x4000) != 0)
    }

    private fun handleFsubr(operand: Int) {
        when (operand) {
            0xE0, 0xE1, 0xE2, 0xE3, 0xE4, 0xE5, 0xE6, 0xE7 -> { // FSUBR ST(i), ST(0)
                val i = operand and 7
                val result = fpuGetReg(0) - fpuGetReg(i)  // Reversed subtraction
                fpuSetReg(i, result)
            }
        }
        fpuInstructions++
        cycles += 5
    }

    private fun handleFsubrp(operand: Int) {
        handleFsubr(operand)
        fpuPop()
    }

    private fun handleFdivr(operand: Int) {
        when (operand) {
            0xF0, 0xF1, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7 -> { // FDIVR ST(i), ST(0)
                val i = operand and 7
                val divisor = fpuGetReg(i)
                if (divisor == 0.0) {
                    // Handle divide by zero
                    fpuStatusWord = fpuStatusWord or 0x0100
                    fpuSetReg(0, Double.POSITIVE_INFINITY)
                } else {
                    val result = fpuGetReg(0) / divisor  // Reversed division
                    fpuSetReg(i, result)
                }
            }
        }
        fpuInstructions++
        cycles += 73
    }

    private fun handleFdivrp(operand: Int) {
        handleFdivr(operand)
        fpuPop()
    }

    private fun handleMovapsStore(operand: Int) {
        val modRM = operand
        if ((modRM shr 6) and 0x03 == 3) {
            // Register to register
            val dstReg = modRM and 0x07
            val srcReg = (modRM shr 3) and 0x07
            setXmmReg(dstReg, getXmmReg(srcReg))
        } else {
            // Register to memory
            val srcReg = (modRM shr 3) and 0x07
            val ea = calculateEA(modRM)
            val value = getXmmReg(srcReg)
            for (i in 0 until 4) {
                memory.writeFloat(ea + i * 4, value[i])
            }
        }
        sseInstructions++
        cycles += 3
    }

    private fun handleMovdStore(operand: Int) {
        val modRM = operand
        if ((modRM shr 6) and 0x03 == 3) {
            // Register to register
            val dstReg = modRM and 0x07
            val srcReg = (modRM shr 3) and 0x07
            val srcValue = getMmxReg(srcReg)[0]
            setReg32(dstReg, srcValue.toInt())
        } else {
            // Register to memory
            val srcReg = (modRM shr 3) and 0x07
            val ea = calculateEA(modRM)
            val value = getMmxReg(srcReg)[0].toInt()
            memory.write32(ea, value)
        }
        mmxInstructions++
        cycles += 2
    }

    private fun handleMovqStore(operand: Int) {
        val modRM = operand
        if ((modRM shr 6) and 0x03 == 3) {
            // Register to register
            val dstReg = modRM and 0x07
            val srcReg = (modRM shr 3) and 0x07
            setMmxReg(dstReg, getMmxReg(srcReg))
        } else {
            // Register to memory
            val srcReg = (modRM shr 3) and 0x07
            val ea = calculateEA(modRM)
            val value = getMmxReg(srcReg)[0]
            memory.write32(ea, (value and 0xFFFFFFFFL).toInt())
            memory.write32(ea + 4, ((value ushr 32) and 0xFFFFFFFFL).toInt())
        }
        mmxInstructions++
        cycles += 2
    }

    private fun handleMovups(operand: Int) {
        // Same as MOVAPS for now (aligned vs unaligned not emulated)
        handleMovaps(operand)
    }

    private fun handleMovupsStore(operand: Int) {
        // Same as MOVAPS for now
        handleMovapsStore(operand)
    }

    private fun handleSubps(operand: Int) {
        val modRM = operand
        val dstReg = (modRM shr 3) and 0x07
        val srcReg = modRM and 0x07
        val dst = getXmmReg(dstReg)
        val src = getXmmReg(srcReg)
        val result = FloatArray(4)
        
        for (i in 0 until 4) {
            result[i] = dst[i] - src[i]
        }
        
        setXmmReg(dstReg, result)
        sseInstructions++
        cycles += 4
    }

    private fun handleDivps(operand: Int) {
        val modRM = operand
        val dstReg = (modRM shr 3) and 0x07
        val srcReg = modRM and 0x07
        val dst = getXmmReg(dstReg)
        val src = getXmmReg(srcReg)
        val result = FloatArray(4)
        
        for (i in 0 until 4) {
            result[i] = dst[i] / src[i]
        }
        
        setXmmReg(dstReg, result)
        sseInstructions++
        cycles += 23
    }

    private fun handleUcomiss(operand: Int) {
        val modRM = operand
        val srcReg = modRM and 0x07
        val dstValue = getXmmReg(0)[0]
        val srcValue = getXmmReg(srcReg)[0]
        
        // Clear flags
        setFlag(CF, false)
        setFlag(PF, false)
        setFlag(ZF, false)
        setFlag(OF, false)
        setFlag(SF, false)
        setFlag(AF, false)
        
        when {
            dstValue.isNaN() || srcValue.isNaN() -> {
                // Unordered
                setFlag(CF, true)
                setFlag(PF, true)
                setFlag(ZF, true)
            }
            dstValue == srcValue -> {
                setFlag(ZF, true)
                setFlag(PF, false)
                setFlag(CF, false)
            }
            dstValue < srcValue -> {
                setFlag(CF, true)
            }
            // else: dstValue > srcValue, all flags remain false
        }
        sseInstructions++
        cycles += 4
    }

    private fun handleComiss(operand: Int) {
        // Same as UCOMISS for now
        handleUcomiss(operand)
    }

    /* ===============================
       System Operations (مُحسنة)
       =============================== */

    private fun handleInterrupt(vector: Int) {
        // Save flags and return address
        push32(eflags)
        push32(cs)
        push32(eip)
        
        // Clear interrupt flag
        setFlag(IF, false)
        setFlag(TF, false)
        
        // Jump to interrupt handler
        val idtEntry = vector * 8
        val handlerOffset = memory.read16(idtEntry)
        val handlerSegment = memory.read16(idtEntry + 2)
        
        cs = handlerSegment
        eip = handlerOffset.toInt()
    }

    private fun handleSyscall() {
        kernel?.handleSyscall()
    }

    private fun handleSysenter() {
        // Save return address
        ecx = eip
        edx = esp
        
        // Jump to sysenter handler
        eip = 0xC0000080.toInt()  // Default sysenter entry point
        esp = 0x03FFFFFC  // Kernel stack
        
        // Set privilege level to 0 (kernel mode)
        cs = 0x08
        ss = 0x10
    }

    private fun handleSysexit() {
        // Return from sysenter
        eip = ecx
        esp = edx
        
        // Set privilege level to 3 (user mode)
        cs = 0x1B
        ss = 0x23
    }

    /* ===============================
       Control (مُحسنة)
       =============================== */

    fun reset(entryPoint: Int = 0xFFF0) {
        eax = 0
        ebx = 0
        ecx = 0
        edx = 0
        esi = 0
        edi = 0
        ebp = 0
        esp = 0x03FF_FFFC.toInt()
        
        cs = 0x08
        ds = 0x10
        es = 0x10
        fs = 0x10
        gs = 0x10
        ss = 0x10
        
        eip = entryPoint
        eflags = 0x00000002
        
        var cr0 = 0x80000001
        cr2 = 0
        cr3 = 0
        cr4 = 0
        
        dr0 = 0
        dr1 = 0
        dr2 = 0
        dr3 = 0
        dr6 = 0
        dr7 = 0
        
        // Reset FPU
        fpuRegs.fill(0.0)
        fpuTop = 0
        fpuControlWord = 0x037F
        fpuStatusWord = 0
        fpuTagWord = 0xFFFF
        
        // Reset MMX
        mmxRegs.forEach { it[0] = 0L }
        mmxState = false
        
        // Reset SSE
        xmmRegs.forEach { it.fill(0.0f) }
        mxcsr = 0x1F80
        
        // Reset TLB and cache
        tlb.clear()
        instructionCache.forEach { 
            it.valid = false
            it.lastAccess = 0
        }
        
        cycles = 0L
        instructionsExecuted = 0L
        timerTicks = 0L
        branchCount = 0L
        interruptCount = 0L
        syscallCount = 0L
        fpuInstructions = 0L
        mmxInstructions = 0L
        sseInstructions = 0L
        cacheHits = 0L
        cacheMisses = 0L
        pageFaults = 0L
        
        running = true
        halted = false
        lastOpcode = 0
        lastPrefixes.clear()
        lockPrefix = false
        repPrefix = false
        repnePrefix = false
        currentSegment = ds
        operandSizeOverride = false
        addressSizeOverride = false
    }

    fun run(cycleLimit: Int = 10000): Long {
        var executed = 0
        while (running && !halted && executed < cycleLimit) {
            step()
            executed++
        }
        return cycles
    }

    /* ===============================
       Debug Information (مُحسنة)
       =============================== */

    fun getRegisters(): Map<String, String> {
        return mapOf(
            "EAX" to "0x${eax.toString(16).padStart(8, '0')}",
            "EBX" to "0x${ebx.toString(16).padStart(8, '0')}",
            "ECX" to "0x${ecx.toString(16).padStart(8, '0')}",
            "EDX" to "0x${edx.toString(16).padStart(8, '0')}",
            "ESI" to "0x${esi.toString(16).padStart(8, '0')}",
            "EDI" to "0x${edi.toString(16).padStart(8, '0')}",
            "EBP" to "0x${ebp.toString(16).padStart(8, '0')}",
            "ESP" to "0x${esp.toString(16).padStart(8, '0')}",
            "EIP" to "0x${eip.toString(16).padStart(8, '0')}",
            "EFLAGS" to "0x${eflags.toString(16).padStart(8, '0')}",
            "CS" to "0x${cs.toString(16).padStart(4, '0')}",
            "DS" to "0x${ds.toString(16).padStart(4, '0')}",
            "ES" to "0x${es.toString(16).padStart(4, '0')}",
            "FS" to "0x${fs.toString(16).padStart(4, '0')}",
            "GS" to "0x${gs.toString(16).padStart(4, '0')}",
            "SS" to "0x${ss.toString(16).padStart(4, '0')}",
            "CR0" to "0x${cr0.toString(16).padStart(8, '0')}",
            "CR2" to "0x${cr2.toString(16).padStart(8, '0')}",
            "CR3" to "0x${cr3.toString(16).padStart(8, '0')}",
            "CR4" to "0x${cr4.toString(16).padStart(8, '0')}",
            "FPU TOP" to fpuTop.toString(),
            "MMX State" to mmxState.toString(),
            "MXCSR" to "0x${mxcsr.toString(16).padStart(8, '0')}"
        )
    }

    fun getFlagsString(): String {
        val flags = listOf(
            if (getFlag(CF)) "CF" else "",
            if (getFlag(PF)) "PF" else "",
            if (getFlag(AF)) "AF" else "",
            if (getFlag(ZF)) "ZF" else "",
            if (getFlag(SF)) "SF" else "",
            if (getFlag(TF)) "TF" else "",
            if (getFlag(IF)) "IF" else "",
            if (getFlag(DF)) "DF" else "",
            if (getFlag(OF)) "OF" else "",
            if (getFlag(NT)) "NT" else "",
            if (getFlag(RF)) "RF" else "",
            if (getFlag(VM)) "VM" else "",
            if (getFlag(AC)) "AC" else "",
            if (getFlag(VIF)) "VIF" else "",
            if (getFlag(VIP)) "VIP" else "",
            if (getFlag(ID)) "ID" else ""
        )
        return flags.filter { it.isNotEmpty() }.joinToString(" ")
    }

    fun getPerformanceInfo(): Map<String, String> {
        return mapOf(
            "Cycles" to cycles.toString(),
            "Instructions" to instructionsExecuted.toString(),
            "IPC" to String.format("%.2f", instructionsExecuted.toDouble() / cycles.coerceAtLeast(1)),
            "Branches" to branchCount.toString(),
            "Interrupts" to interruptCount.toString(),
            "Syscalls" to syscallCount.toString(),
            "FPU Ops" to fpuInstructions.toString(),
            "MMX Ops" to mmxInstructions.toString(),
            "SSE Ops" to sseInstructions.toString(),
            "Cache Hits" to cacheHits.toString(),
            "Cache Misses" to cacheMisses.toString(),
            "Hit Rate" to String.format("%.1f%%", cacheHits.toDouble() / (cacheHits + cacheMisses).coerceAtLeast(1) * 100),
            "Page Faults" to pageFaults.toString(),
            "Timer Ticks" to timerTicks.toString(),
            "Last Opcode" to "0x${lastOpcode.toString(16)}",
            "Prefixes" to lastPrefixes.joinToString(" ") { "0x${it.toString(16)}" }
        )
    }

    fun getInstructionHistory(count: Int = 10): List<String> {
        // This would need a circular buffer to track recent instructions
        return listOf("Instruction history not implemented")
    }

    fun dumpState(): String {
        val sb = StringBuilder()
        sb.appendLine("=== CPU State ===")
        sb.appendLine("Registers:")
        getRegisters().forEach { (reg, value) ->
            sb.appendLine("  $reg: $value")
        }
        sb.appendLine("Flags: ${getFlagsString()}")
        sb.appendLine("Performance:")
        getPerformanceInfo().forEach { (metric, value) ->
            sb.appendLine("  $metric: $value")
        }
        sb.appendLine("Running: $running, Halted: $halted")
        return sb.toString()
    }

    /* ===============================
       Errors
       =============================== */

    private fun handleUnknownOpcode(opcode: Int) {
        val opcodeStr = if (opcode > 0xFF) {
            "0x${(opcode shr 8).toString(16).padStart(2, '0')} 0x${(opcode and 0xFF).toString(16).padStart(2, '0')}"
        } else {
            "0x${opcode.toString(16).padStart(2, '0')}"
        }
        
        throw IllegalStateException(
            "Unknown opcode $opcodeStr at EIP=0x${(eip - 1).toString(16).padStart(8, '0')}" +
            " prefixes=[${lastPrefixes.joinToString(" ") { "0x${it.toString(16)}" }}]"
        )
    }
}
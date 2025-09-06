#include "x86_core.h"
#include "xbox_memory.h"
#include <android/log.h>
#include <stdexcept>
#include <cstring>

#define LOG_TAG "X86CoreSimple"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

X86Core::X86Core(XboxMemory* memory) :
    xmmRegisters{},
    fpu{},
    memory(memory),
    state(CpuState::Running),
    kernel(nullptr),
    xboxRegisters{},
    regs{0, 0, 0, 0, 0, 0, 0x0003FFFC, 0x0003FFFC},
    eip(0x00000000),
    eflags(0x00000002),
    cs(0xF000), ds(0x0000), es(0x0000), fs(0x0000), gs(0x0000), ss(0x0000),
    cr0(0x60000011), cr2(0), cr3(0), cr4(0),
    breakpoints(),
    debugCallback(),
    jit_cache(),
    executionCounts(),
    jitCacheBase(nullptr),
    jitCacheUsed(0),
    jitEnabled(false), 
    jitThreshold(10),
    totalInstructionsExecuted(0),
    instructionsThisFrame(0),
    targetInstructionsPerFrame(1000),
    stackGuardStart(0x0003FFFC),
    stackGuardEnd(0x0003FFFC),
    fpuStack()
{

    eax = regs[0];
    ebx = regs[1];
    ecx = regs[2];
    edx = regs[3];
    esi = regs[4];
    edi = regs[5];
    esp = regs[6];
    ebp = regs[7];

    fpu.controlWord = 0x037F;
    fpu.statusWord = 0;
    fpu.tagWord = 0xFFFF;

    memset(&xboxRegisters, 0, sizeof(xboxRegisters));

    LOGI("X86Core (Simplified): Initialized successfully");
}

X86Core::~X86Core() {
    if (jitCacheBase) {
        free(jitCacheBase); 
    }
}


void X86Core::executeNextInstruction() {
    if (eip >= memory->getSize()) {
        LOGE("X86Core: EIP out of bounds: 0x%08X", eip);
        state = CpuState::Halted;
        return;
    }

    uint8_t opcode = memory->read8(eip++);
    LOGD("X86Core: Executing opcode: 0x%02X at EIP: 0x%08X", opcode, eip - 1);


    totalInstructionsExecuted++;
    instructionsThisFrame++;



}

void X86Core::reset() {
    eax = ebx = ecx = edx = 0;
    esi = edi = 0;
    esp = 0x0003FFFC;
    ebp = 0x0003FFFC;
    eip = 0x00100000; 
    eflags = 0x00000002;
    state = CpuState::Running;

    LOGI("X86Core: Reset completed");
}

void X86Core::handleInterrupt(uint8_t interrupt) {
    LOGD("X86Core: Handling interrupt: 0x%02X", interrupt);

}

bool X86Core::isInstructionComplete() const {
    return true; 
}


uint32_t X86Core::getRegister(int reg) const {
    switch (reg) {
        case 0: return eax;
        case 1: return ecx;
        case 2: return edx;
        case 3: return ebx;
        case 4: return esp;
        case 5: return ebp;
        case 6: return esi;
        case 7: return edi;
        default: return 0;
    }
}

void X86Core::setRegister(int reg, uint32_t value) {
    switch (reg) {
        case 0: eax = value; regs[0] = value; break;
        case 1: ecx = value; regs[1] = value; break;
        case 2: edx = value; regs[2] = value; break;
        case 3: ebx = value; regs[3] = value; break;
        case 4: esp = value; regs[6] = value; break;
        case 5: ebp = value; regs[7] = value; break;
        case 6: esi = value; regs[4] = value; break;
        case 7: edi = value; regs[5] = value; break;
    }
}




void X86Core::handleSyscall() {
    LOGD("X86Core: System call handled");
}

void X86Core::push32(uint32_t value) {
    esp -= 4;
    memory->write32(esp, value);
}

uint32_t X86Core::pop32() {
    uint32_t value = memory->read32(esp);
    esp += 4;
    return value;
}

uint32_t X86Core::readOperand(uint8_t modrm) {
    (void)modrm; 
    return 0; 
}

void X86Core::writeOperand(uint8_t modrm, uint32_t value) {
    (void)modrm; (void)value; 
}

void X86Core::updateFlags(uint32_t result, uint32_t a, uint32_t b, int operation) {
    (void)result; (void)a; (void)b; (void)operation; 
}

void X86Core::setFlags(uint32_t result) {
    (void)result; 
}


X86Core* createX86CoreInstance(XboxMemory* memory) {
    return new X86Core(memory);
}



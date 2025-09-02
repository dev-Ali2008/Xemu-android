#include "x86_core.h"
#include <android/log.h>

#define LOG_TAG "X86Instructions"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)


void X86Core::mov_r32_imm32() {
    uint8_t reg = memory->read8(eip++) & 7;
    uint32_t imm = memory->read32(eip);
    eip += 4;
    setRegister(reg, imm);
}



void X86Core::mov_eax_moffs32() {
    uint32_t offset = memory->read32(eip);
    eip += 4;
    eax = memory->read32(offset);
}

void X86Core::mov_moffs32_eax() {
    uint32_t offset = memory->read32(eip);
    eip += 4;
    memory->write32(offset, eax);
}

void X86Core::mov_r32_sreg() {
    uint8_t modrm = memory->read8(eip++);
    uint8_t sreg = (modrm >> 3) & 7;
    [[maybe_unused]] uint16_t sreg_value = 0;

    switch (sreg) {
        case 0: sreg_value = es; break;
        case 1: sreg_value = cs; break;
        case 2: sreg_value = ss; break;
        case 3: sreg_value = ds; break;
        case 4: sreg_value = fs; break;
        case 5: sreg_value = gs; break;
    }

    writeOperand(modrm, sreg_value);
}

void X86Core::mov_sreg_r32() {
    uint8_t modrm = memory->read8(eip++);
    uint8_t sreg = (modrm >> 3) & 7;
    uint16_t value = readOperand(modrm) & 0xFFFF;

    switch (sreg) {
        case 0: es = value; break;
        case 1: cs = value; break;
        case 2: ss = value; break;
        case 3: ds = value; break;
        case 4: fs = value; break;
        case 5: gs = value; break;
    }
}


void X86Core::add_eax_imm32() {
    uint32_t imm = memory->read32(eip);
    eip += 4;
    aluAdd(eax, imm);
}

void X86Core::add_rm32_imm32() {
    uint8_t modrm = memory->read8(eip++);
    uint32_t imm = memory->read32(eip);
    eip += 4;
    uint32_t value = readOperand(modrm);
    aluAdd(value, imm);
    writeOperand(modrm, value);
}

void X86Core::sub_r32_rm32() {
    uint8_t modrm = memory->read8(eip++);
    uint8_t reg = (modrm >> 3) & 7;
    uint32_t value = readOperand(modrm);
    uint32_t reg_value = getRegister(reg);
    aluSub(reg_value, value);
    setRegister(reg, reg_value);
}

void X86Core::sub_rm32_r32() {
    uint8_t modrm = memory->read8(eip++);
    uint8_t reg = (modrm >> 3) & 7;
    uint32_t value = getRegister(reg);
    uint32_t mem_value = readOperand(modrm);
    aluSub(mem_value, value);
    writeOperand(modrm, mem_value);
}

void X86Core::sub_r32_imm32() {
    uint8_t modrm = memory->read8(eip++);
    uint8_t reg = (modrm >> 3) & 7;
    uint32_t imm = memory->read32(eip);
    eip += 4;
    uint32_t reg_value = getRegister(reg);
    aluSub(reg_value, imm);
    setRegister(reg, reg_value);
}

void X86Core::sub_rm32_imm32() {
    uint8_t modrm = memory->read8(eip++);
    uint32_t imm = memory->read32(eip);
    eip += 4;
    uint32_t value = readOperand(modrm);
    aluSub(value, imm);
    writeOperand(modrm, value);
}

void X86Core::sub_eax_imm32() {
    uint32_t imm = memory->read32(eip);
    eip += 4;
    aluSub(eax, imm);
}


void X86Core::and_r32_rm32() {
    uint8_t modrm = memory->read8(eip++);
    uint8_t reg = (modrm >> 3) & 7;
    uint32_t value = readOperand(modrm);
    uint32_t reg_value = getRegister(reg);
    reg_value &= value;
    updateFlags(reg_value, reg_value, value, ALU_AND);
    setRegister(reg, reg_value);
}

void X86Core::and_rm32_r32() {
    uint8_t modrm = memory->read8(eip++);
    uint8_t reg = (modrm >> 3) & 7;
    uint32_t value = getRegister(reg);
    uint32_t mem_value = readOperand(modrm);
    mem_value &= value;
    updateFlags(mem_value, mem_value, value, ALU_AND);
    writeOperand(modrm, mem_value);
}

void X86Core::and_r32_imm32() {
    uint8_t modrm = memory->read8(eip++);
    uint8_t reg = (modrm >> 3) & 7;
    uint32_t imm = memory->read32(eip);
    eip += 4;
    uint32_t reg_value = getRegister(reg);
    reg_value &= imm;
    updateFlags(reg_value, reg_value, imm, ALU_AND);
    setRegister(reg, reg_value);
}

void X86Core::and_rm32_imm32() {
    uint8_t modrm = memory->read8(eip++);
    uint32_t imm = memory->read32(eip);
    eip += 4;
    uint32_t value = readOperand(modrm);
    value &= imm;
    updateFlags(value, value, imm, ALU_AND);
    writeOperand(modrm, value);
}

void X86Core::and_eax_imm32() {
    uint32_t imm = memory->read32(eip);
    eip += 4;
    eax &= imm;
    updateFlags(eax, eax, imm, ALU_AND);
}

void X86Core::or_r32_rm32() {
    uint8_t modrm = memory->read8(eip++);
    uint8_t reg = (modrm >> 3) & 7;
    uint32_t value = readOperand(modrm);
    uint32_t reg_value = getRegister(reg);
    reg_value |= value;
    updateFlags(reg_value, reg_value, value, ALU_OR);
    setRegister(reg, reg_value);
}

void X86Core::or_rm32_r32() {
    uint8_t modrm = memory->read8(eip++);
    uint8_t reg = (modrm >> 3) & 7;
    uint32_t value = getRegister(reg);
    uint32_t mem_value = readOperand(modrm);
    mem_value |= value;
    updateFlags(mem_value, mem_value, value, ALU_OR);
    writeOperand(modrm, mem_value);
}

void X86Core::or_r32_imm32() {
    uint8_t modrm = memory->read8(eip++);
    uint8_t reg = (modrm >> 3) & 7;
    uint32_t imm = memory->read32(eip);
    eip += 4;
    uint32_t reg_value = getRegister(reg);
    reg_value |= imm;
    updateFlags(reg_value, reg_value, imm, ALU_OR);
    setRegister(reg, reg_value);
}

void X86Core::or_rm32_imm32() {
    uint8_t modrm = memory->read8(eip++);
    uint32_t imm = memory->read32(eip);
    eip += 4;
    uint32_t value = readOperand(modrm);
    value |= imm;
    updateFlags(value, value, imm, ALU_OR);
    writeOperand(modrm, value);
}

void X86Core::or_eax_imm32() {
    uint32_t imm = memory->read32(eip);
    eip += 4;
    eax |= imm;
    updateFlags(eax, eax, imm, ALU_OR);
}

void X86Core::xor_r32_rm32() {
    uint8_t modrm = memory->read8(eip++);
    uint8_t reg = (modrm >> 3) & 7;
    uint32_t value = readOperand(modrm);
    uint32_t reg_value = getRegister(reg);
    reg_value ^= value;
    updateFlags(reg_value, reg_value, value, ALU_XOR);
    setRegister(reg, reg_value);
}

void X86Core::xor_rm32_r32() {
    uint8_t modrm = memory->read8(eip++);
    uint8_t reg = (modrm >> 3) & 7;
    uint32_t value = getRegister(reg);
    uint32_t mem_value = readOperand(modrm);
    mem_value ^= value;
    updateFlags(mem_value, mem_value, value, ALU_XOR);
    writeOperand(modrm, mem_value);
}

void X86Core::xor_r32_imm32() {
    uint8_t modrm = memory->read8(eip++);
    uint8_t reg = (modrm >> 3) & 7;
    uint32_t imm = memory->read32(eip);
    eip += 4;
    uint32_t reg_value = getRegister(reg);
    reg_value ^= imm;
    updateFlags(reg_value, reg_value, imm, ALU_XOR);
    setRegister(reg, reg_value);
}

void X86Core::xor_rm32_imm32() {
    uint8_t modrm = memory->read8(eip++);
    uint32_t imm = memory->read32(eip);
    eip += 4;
    uint32_t value = readOperand(modrm);
    value ^= imm;
    updateFlags(value, value, imm, ALU_XOR);
    writeOperand(modrm, value);
}

void X86Core::xor_eax_imm32() {
    uint32_t imm = memory->read32(eip);
    eip += 4;
    eax ^= imm;
    updateFlags(eax, eax, imm, ALU_XOR);
}


void X86Core::cmp_r32_rm32() {
    uint8_t modrm = memory->read8(eip++);
    uint8_t reg = (modrm >> 3) & 7;
    uint32_t value = readOperand(modrm);
    uint32_t reg_value = getRegister(reg);
    uint32_t result = reg_value - value;
    updateFlags(result, reg_value, value, ALU_SUB);
}

void X86Core::cmp_rm32_r32() {
    uint8_t modrm = memory->read8(eip++);
    uint8_t reg = (modrm >> 3) & 7;
    uint32_t value = getRegister(reg);
    uint32_t mem_value = readOperand(modrm);
    uint32_t result = mem_value - value;
    updateFlags(result, mem_value, value, ALU_SUB);
}

void X86Core::cmp_r32_imm32() {
    uint8_t modrm = memory->read8(eip++);
    uint8_t reg = (modrm >> 3) & 7;
    uint32_t imm = memory->read32(eip);
    eip += 4;
    uint32_t reg_value = getRegister(reg);
    uint32_t result = reg_value - imm;
    updateFlags(result, reg_value, imm, ALU_SUB);
}

void X86Core::cmp_rm32_imm32() {
    uint8_t modrm = memory->read8(eip++);
    uint32_t imm = memory->read32(eip);
    eip += 4;
    uint32_t value = readOperand(modrm);
    uint32_t result = value - imm;
    updateFlags(result, value, imm, ALU_SUB);
}

void X86Core::cmp_eax_imm32() {
    uint32_t imm = memory->read32(eip);
    eip += 4;
    uint32_t result = eax - imm;
    updateFlags(result, eax, imm, ALU_SUB);
}

void X86Core::test_rm32_r32() {
    uint8_t modrm = memory->read8(eip++);
    uint8_t reg = (modrm >> 3) & 7;
    uint32_t value = getRegister(reg);
    uint32_t mem_value = readOperand(modrm);
    uint32_t result = mem_value & value;
    updateFlags(result, mem_value, value, ALU_AND);
}

void X86Core::test_r32_imm32() {
    uint8_t modrm = memory->read8(eip++);
    uint8_t reg = (modrm >> 3) & 7;
    uint32_t imm = memory->read32(eip);
    eip += 4;
    uint32_t reg_value = getRegister(reg);
    uint32_t result = reg_value & imm;
    updateFlags(result, reg_value, imm, ALU_AND);
}

void X86Core::test_rm32_imm32() {
    uint8_t modrm = memory->read8(eip++);
    uint32_t imm = memory->read32(eip);
    eip += 4;
    uint32_t value = readOperand(modrm);
    uint32_t result = value & imm;
    updateFlags(result, value, imm, ALU_AND);
}

void X86Core::test_eax_imm32() {
    uint32_t imm = memory->read32(eip);
    eip += 4;
    uint32_t result = eax & imm;
    updateFlags(result, eax, imm, ALU_AND);
}


void X86Core::inc_r32() {
    uint8_t reg = memory->read8(eip - 1) & 7;
    uint32_t value = getRegister(reg);
    aluAdd(value, 1);
    setRegister(reg, value);
}

void X86Core::inc_rm32() {
    uint8_t modrm = memory->read8(eip++);
    uint32_t value = readOperand(modrm);
    aluAdd(value, 1);
    writeOperand(modrm, value);
}

void X86Core::dec_r32() {
    uint8_t reg = memory->read8(eip - 1) & 7;
    uint32_t value = getRegister(reg);
    aluSub(value, 1);
    setRegister(reg, value);
}

void X86Core::dec_rm32() {
    uint8_t modrm = memory->read8(eip++);
    uint32_t value = readOperand(modrm);
    aluSub(value, 1);
    writeOperand(modrm, value);
}


void X86Core::push_r32() {
    uint8_t reg = memory->read8(eip - 1) & 7;
    uint32_t value = getRegister(reg);
    esp -= 4;
    memory->write32(esp, value);
}

void X86Core::push_rm32() {
    uint8_t modrm = memory->read8(eip++);
    uint32_t value = readOperand(modrm);
    esp -= 4;
    memory->write32(esp, value);
}

void X86Core::push_imm32() {
    uint32_t imm = memory->read32(eip);
    eip += 4;
    esp -= 4;
    memory->write32(esp, imm);
}

void X86Core::push_imm8() {
    int8_t imm = memory->read8(eip++);
    esp -= 4;
    memory->write32(esp, (int32_t)imm);
}

void X86Core::pop_r32() {
    uint8_t reg = memory->read8(eip - 1) & 7;
    uint32_t value = memory->read32(esp);
    esp += 4;
    setRegister(reg, value);
}

void X86Core::pop_rm32() {
    uint8_t modrm = memory->read8(eip++);
    uint32_t value = memory->read32(esp);
    esp += 4;
    writeOperand(modrm, value);
}


void X86Core::push_seg() {
    uint8_t seg = (memory->read8(eip - 1) >> 3) & 7;
    [[maybe_unused]] uint16_t value = 0;

    switch (seg) {
        case 0: value = es; break;
        case 1: value = cs; break;
        case 2: value = ss; break;
        case 3: value = ds; break;
        case 4: value = fs; break;
        case 5: value = gs; break;
        default: value = 0; break;
    }

    esp -= 4;
    memory->write32(esp, value);

    if (traceEnabled) {
        char buf[128];
        snprintf(buf, sizeof(buf), "PUSH SEG%d = 0x%04X", seg, value);
        debugCallback(eip, buf);
    }
}

void X86Core::pop_seg() {
    uint8_t seg = (memory->read8(eip - 1) >> 3) & 7;
    uint16_t value = memory->read16(esp);
    esp += 4; 

    switch (seg) {
        case 0: es = value; break;
        case 1: cs = value; break;
        case 2: ss = value; break;
        case 3: ds = value; break;
        case 4: fs = value; break;
        case 5: gs = value; break;
        default: break;
    }

    if (traceEnabled) {
        char buf[128];
        snprintf(buf, sizeof(buf), "POP SEG%d = 0x%04X", seg, value);
        debugCallback(eip, buf);
    }
}




void X86Core::int_imm8() {
    uint8_t vector = memory->read8(eip++);


    esp -= 4;
    memory->write32(esp, eflags);
    esp -= 4;
    memory->write32(esp, cs);
    esp -= 4;
    memory->write32(esp, eip);


    eflags &= ~0x200; 



    if (vector == 0x80) { 
        handleSyscall();
    } else {

        eip = vector * 4; 
    }

    if (traceEnabled) {
        char buf[128];
        snprintf(buf, sizeof(buf), "INT 0x%02X", vector);
        debugCallback(eip, buf);
    }
}

void X86Core::iret() {

    eip = memory->read32(esp);
    esp += 4;
    validateAndFixESP(); 
    cs = memory->read16(esp);
    esp += 4;
    validateAndFixESP(); 
    eflags = memory->read32(esp);
    esp += 4;
    validateAndFixESP(); 

    if (traceEnabled) {
        char buf[128];
        snprintf(buf, sizeof(buf), "IRET: EIP=0x%08X, CS=0x%04X, EFLAGS=0x%08X", eip, cs, eflags);
        debugCallback(eip, buf);
    }
}


void X86Core::enter_imm16_imm8() {
    uint16_t frameSize = memory->read16(eip);
    eip += 2;
    uint8_t nestingLevel = memory->read8(eip++);


    esp -= 4;
    validateAndFixESP(); 
    memory->write32(esp, ebp);


    ebp = esp;


    esp -= frameSize;
    validateAndFixESP(); 

    if (traceEnabled) {
        char buf[128];
        snprintf(buf, sizeof(buf), "ENTER: frame_size=%d, nesting=%d", frameSize, nestingLevel);
        debugCallback(eip, buf);
    }
}

void X86Core::leave() {

    esp = ebp;


    validateAndFixESP();

    ebp = memory->read32(esp);
    esp += 4;
    validateAndFixESP(); 

    if (traceEnabled) {
        char buf[128];
        snprintf(buf, sizeof(buf), "LEAVE: EBP=0x%08X", ebp);
        debugCallback(eip, buf);
    }
}



void X86Core::call_rm32() {
    uint8_t modrm = memory->read8(eip++);
    uint32_t target = readOperand(modrm);
    esp -= 4;
    validateAndFixESP(); 
    memory->write32(esp, eip);
    eip = target;
}

void X86Core::ret_imm16() {
    uint16_t imm = memory->read16(eip);
    eip += 2;


    uint32_t return_addr = memory->read32(esp);
    LOGD("CPU: RET imm16 - ESP: 0x%08X, Return address: 0x%08X, Imm: 0x%04X", esp, return_addr, imm);

    esp += 4 + imm;
    validateAndFixESP(); 


    eip = return_addr;
    LOGD("CPU: RET imm16 - Jumping to return address: 0x%08X", eip);
}

void X86Core::jmp_rm32() {
    uint8_t modrm = memory->read8(eip++);
    uint32_t target = readOperand(modrm);
    eip = target;
}


void X86Core::jo_rel8() {
    int8_t offset = memory->read8(eip++);
    if (eflags & 0x800) eip += offset;
}

void X86Core::jno_rel8() {
    int8_t offset = memory->read8(eip++);
    if (!(eflags & 0x800)) eip += offset;
}

void X86Core::jb_rel8() {
    int8_t offset = memory->read8(eip++);
    if (eflags & 0x1) eip += offset;
}

void X86Core::jnb_rel8() {
    int8_t offset = memory->read8(eip++);
    if (!(eflags & 0x1)) eip += offset;
}

void X86Core::jz_rel8() {
    int8_t offset = memory->read8(eip++);
    if (eflags & 0x40) eip += offset;
}

void X86Core::jnz_rel8() {
    int8_t offset = memory->read8(eip++);
    if (!(eflags & 0x40)) eip += offset;
}

void X86Core::jbe_rel8() {
    int8_t offset = memory->read8(eip++);
    if ((eflags & 0x1) || (eflags & 0x40)) eip += offset;
}

void X86Core::jnbe_rel8() {
    int8_t offset = memory->read8(eip++);
    if (!((eflags & 0x1) || (eflags & 0x40))) eip += offset;
}

void X86Core::js_rel8() {
    int8_t offset = memory->read8(eip++);
    if (eflags & 0x80) eip += offset;
}

void X86Core::jns_rel8() {
    int8_t offset = memory->read8(eip++);
    if (!(eflags & 0x80)) eip += offset;
}

void X86Core::jp_rel8() {
    int8_t offset = memory->read8(eip++);
    if (eflags & 0x4) eip += offset;
}

void X86Core::jnp_rel8() {
    int8_t offset = memory->read8(eip++);
    if (!(eflags & 0x4)) eip += offset;
}

void X86Core::jl_rel8() {
    int8_t offset = memory->read8(eip++);
    if ((eflags & 0x80) != ((eflags & 0x800) >> 4)) eip += offset;
}

void X86Core::jnl_rel8() {
    int8_t offset = memory->read8(eip++);
    if ((eflags & 0x80) == ((eflags & 0x800) >> 4)) eip += offset;
}

void X86Core::jle_rel8() {
    int8_t offset = memory->read8(eip++);
    if ((eflags & 0x40) || ((eflags & 0x80) != ((eflags & 0x800) >> 4))) eip += offset;
}

void X86Core::jnle_rel8() {
    int8_t offset = memory->read8(eip++);
    if (!(eflags & 0x40) && ((eflags & 0x80) == ((eflags & 0x800) >> 4))) eip += offset;
}




void X86Core::loop_rel8() {
    int8_t offset = memory->read8(eip++);
    ecx--;
    if (ecx != 0) eip += offset;
}

void X86Core::loope_rel8() {
    int8_t offset = memory->read8(eip++);
    ecx--;
    if (ecx != 0 && (eflags & 0x40)) eip += offset;
}

void X86Core::loopne_rel8() {
    int8_t offset = memory->read8(eip++);
    ecx--;
    if (ecx != 0 && !(eflags & 0x40)) eip += offset;
}



void X86Core::hlt() {
    state = CpuState::Halted;
}

void X86Core::wait() {

}

void X86Core::lock() {

}

void X86Core::rep() {

}

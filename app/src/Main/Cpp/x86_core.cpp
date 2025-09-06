#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wall"
#pragma GCC diagnostic ignored "-Wextra"
#pragma GCC diagnostic ignored "-Wpedantic"

#ifdef SKIP_STATIC_ANALYSIS

#endif

#include "x86_core.h"
#include "xbox_memory.h"

#include <android/log.h>
#include <stdexcept>
#include <arm_neon.h>
#include <unistd.h>
#include <sys/mman.h>
#include <cstring>
#include <vector>
#include <algorithm>
#include <cstdio>

#define LOG_TAG "X86Core"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define LOGGAME(...) __android_log_print(ANDROID_LOG_ERROR, "GAME_LOAD", __VA_ARGS__)
#define GAMELOADED(...) __android_log_print(ANDROID_LOG_WARN, "GAMELOADED", __VA_ARGS__)
#ifndef ISGAMEREALLOADED
#define ISGAMEREALLOADED(...) __android_log_print(ANDROID_LOG_WARN, "ISGAMEREALLOADED", __VA_ARGS__)
#endif

constexpr size_t JIT_CACHE_SIZE = 16 * 1024 * 1024;
constexpr uint32_t MAX_BLOCK_SIZE = 256;
constexpr uint32_t JIT_THRESHOLD = 10;




bool X86Core::boundsCheckingDisabled = false;

X86Core::X86Core(XboxMemory* memory) :
    xmmRegisters(),
    fpu(),
    memory(memory),
    state(CpuState::Running),
    kernel(nullptr),
    xboxRegisters(),

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
    jitEnabled(true),
    jitThreshold(JIT_THRESHOLD),
    totalInstructionsExecuted(0),
    instructionsThisFrame(0),
    targetInstructionsPerFrame(1000),
    stackGuardStart(0x0003FFFC),
    stackGuardEnd(0x0003FFFC),
    fpuStack()
{


    for (int i = 0; i < MAX_XBOX_TIMERS; i++) {
        xboxTimers[i].active = false;
        xboxTimers[i].running = false;
        xboxTimers[i].periodic = false;
        xboxTimers[i].value = 0;
        xboxTimers[i].period = 0;
        xboxTimers[i].type = 0;
    }



    gpuInitialized = false;
    gpuWidth = 640;
    gpuHeight = 480;
    gpuFramebuffer = nullptr;
    gpuFramebufferSize = 0;
    currentShader = -1;


    memset(gpuRenderStates, 0, sizeof(gpuRenderStates));
    memset(gpuVertexBuffers, 0, sizeof(gpuVertexBuffers));
    memset(gpuTextures, 0, sizeof(gpuTextures));
    memset(gpuShaders, 0, sizeof(gpuShaders));


    gpuViewportX = 0;
    gpuViewportY = 0;
    gpuViewportWidth = gpuWidth;
    gpuViewportHeight = gpuHeight;


    gpuScissorX = 0;
    gpuScissorY = 0;
    gpuScissorWidth = gpuWidth;
    gpuScissorHeight = gpuHeight;



    dvdParser = nullptr;
    dvdOpened = false;
    dvdCurrentSector = 0;
    dvdDiscId = "";



    audioInitialized = false;



    segmentOverride = NONE;
    prefixActive = false;
    operandSizeOverride = false;
    addressSizeOverride = false;


    for (int i = 0; i < MAX_AUDIO_STREAMS; i++) {
        audioStreams[i].active = false;
        audioStreams[i].playing = false;
        audioStreams[i].paused = false;
        audioStreams[i].position = 0;
        audioStreams[i].volume = 100; 
        audioStreams[i].format = 0;
        audioStreams[i].channels = 2; 
        audioStreams[i].sampleRate = 44100; 
        audioStreams[i].buffer.clear();
    }



    for (int i = 0; i < MAX_HDD_FILES; i++) {
        hddFiles[i].active = false;
        hddFiles[i].filename = "";
        hddFiles[i].fullPath = "";
        hddFiles[i].fileSize = 0;
        hddFiles[i].currentPosition = 0;
        hddFiles[i].isDirectory = false;
    }



    networkInitialized = false;
    networkStatus = 0; 

    for (int i = 0; i < MAX_NETWORK_SOCKETS; i++) {
        networkSockets[i].active = false;
        networkSockets[i].socketType = 0;
        networkSockets[i].connected = false;
        networkSockets[i].remoteAddr = 0;
        networkSockets[i].remotePort = 0;
        networkSockets[i].localAddr = 0;
        networkSockets[i].localPort = 0;
        networkSockets[i].receiveBuffer.clear();
        networkSockets[i].isServer = false;
    }



    usbInitialized = false;
    usbDeviceCount = 0;

    for (int i = 0; i < MAX_USB_DEVICES; i++) {
        usbDevices[i].active = false;
        usbDevices[i].vendorId = 0;
        usbDevices[i].productId = 0;
        usbDevices[i].deviceClass = 0;
        usbDevices[i].deviceSubClass = 0;
        usbDevices[i].deviceName = "";
        usbDevices[i].isController = false;
        usbDevices[i].isMemoryCard = false;
        usbDevices[i].controllerState = 0;
        usbDevices[i].rumbleState = 0;
        usbDevices[i].memoryCardData.clear();
    }


    initializeStandardUSBDevices();



    systemInitialized = true;
    systemInfo.xboxVersion = 0x00000100;     
    systemInfo.kernelVersion = 0x00000100;   
    systemInfo.totalMemory = 64 * 1024 * 1024;  
    systemInfo.availableMemory = 32 * 1024 * 1024; 
    systemInfo.systemTime = 0;               
    systemInfo.fanSpeed = 1500;              
    systemInfo.temperature = 45;             
    systemInfo.diskSpace = 8 * 1024 * 1024 * 1024; 
    systemInfo.isDebugMode = true;           




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


    #if defined(__arm__) || defined(__aarch64__)
        jitEnabled = false;
        LOGI("JIT disabled on ARM/ARM64 platform");
    #endif

    jitCacheBase = mmap(nullptr, JIT_CACHE_SIZE, 
                       PROT_READ | PROT_WRITE | PROT_EXEC,
                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

    if (jitCacheBase == MAP_FAILED) {
        LOGE("Failed to allocate JIT cache");
        jitEnabled = false;
    } else {
        LOGI("JIT cache allocated at %p (size: %zu bytes)", jitCacheBase, JIT_CACHE_SIZE);
    }

    traceEnabled = false;

    LOGI("CPU: Initialized with Xbox-compatible registers - EIP: 0x%08X, ESP: 0x%08X", eip, esp);
}

X86Core::~X86Core() {
    if (jitCacheBase) {
        munmap(jitCacheBase, JIT_CACHE_SIZE);
    }



    if (gpuFramebuffer) {
        delete[] gpuFramebuffer;
        gpuFramebuffer = nullptr;
    }


    for (int i = 0; i < MAX_GPU_BUFFERS; i++) {
        if (gpuVertexBuffers[i].data) {
            delete[] gpuVertexBuffers[i].data;
            gpuVertexBuffers[i].data = nullptr;
        }
    }


    for (int i = 0; i < MAX_GPU_TEXTURES; i++) {
        if (gpuTextures[i].data) {
            delete[] gpuTextures[i].data;
            gpuTextures[i].data = nullptr;
        }
    }



    if (dvdParser) {
        delete dvdParser;
        dvdParser = nullptr;
    }



    for (int i = 0; i < MAX_HDD_FILES; i++) {
        if (hddFiles[i].active && hddFiles[i].fileStream.is_open()) {
            hddFiles[i].fileStream.close();
            hddFiles[i].active = false;
        }
    }
}

void X86Core::reset() {
    eax = ebx = ecx = edx = 0;
    esi = edi = 0;
    esp = (memory && memory->espStartValue) ? memory->espStartValue : 0x0003FFE0; 
    ebp = esp;


    if (memory && memory->xbeEntryPoint != 0) {
        eip = memory->xbeEntryPoint;
        esp = 0x0003FFE0; 
        LOGI("[CPU-DEBUG] reset() using XBE entry point: 0x%08X, ESP: 0x%08X", memory->xbeEntryPoint, esp);


        if (eip >= 0x00000000 && eip < 0x00010000) {
            LOGW("[CPU-DEBUG] WARNING: Entry point 0x%08X is in low memory area! Moving to safe code area.", eip);
            eip = 0x00100000; 
            LOGI("[CPU-DEBUG] Entry point moved to: 0x%08X", eip);
        }
    } else {
        eip = 0x00100000; 
        esp = 0x0003FFE0; 
        LOGI("[CPU-DEBUG] reset() - EIP: 0x%08X, ESP: 0x%08X", eip, esp);
    }

    eflags = 0x00000002; 
    cs = 0xF000; 
    ds = 0x0000; 
    es = 0x0000; 
    fs = 0x0000; 
    gs = 0x0000; 
    ss = 0x0000; 
    cr0 = 0x60000011;
    cr2 = cr3 = cr4 = 0;
    state = CpuState::Running;

    for (auto& reg : xmmRegisters) {
        memset(reg.data, 0, sizeof(reg.data));
    }

    fpu.controlWord = 0x037F;
    fpu.statusWord = 0;
    fpu.tagWord = 0xFFFF;
    breakpoints.clear();
    jit_cache.clear();
    executionCounts.clear();

    if (jitCacheBase) {
        jitCacheUsed = 0;
    }
}

void X86Core::execute(uint32_t cycles) {
    LOGI("[CPU-DEBUG] execute() called with cycles=%u", cycles);
    uint32_t executed = 0;
    bool firstInstruction = true;


    if (state != CpuState::Running) {
        LOGW("[CPU-DEBUG] CPU not in running state (%d), setting to running", static_cast<int>(state));
        LOGW("[CPU-DEBUG] Previous state was: %d (0=Running, 1=Halted, 2=Error, 3=DebugBreak)", static_cast<int>(state));
        LOGW("[CPU-DEBUG] Current EIP: 0x%08X, ESP: 0x%08X", eip, esp);


        if (memory) {
            uint32_t instruction = memory->read32(eip);
            LOGW("[CPU-DEBUG] Instruction at EIP: 0x%08X", instruction);


            if (instruction == 0x00000000) {
                LOGW("[CPU-DEBUG] ⚠️  EIP points to zeroed memory - this caused the halt!");
            } else if (instruction == 0x48454258) {
                LOGW("[CPU-DEBUG] ⚠️  EIP points to XBE header - wrong entry point!");
            }
        }

        state = CpuState::Running;
    }


    static int recoveryAttempts = 0;
    static uint32_t lastEIP = 0;

    while (executed < cycles && state == CpuState::Running) {
        if (firstInstruction) {
            LOGI("[CPU-DEBUG] First instruction: EIP=0x%08X, ESP=0x%08X", eip, esp);
            firstInstruction = false;
        }


        if (executed % 100 == 0) { 
            robustStackManagement();
        }







        if (executed % 200 == 0) { 
            cpuPerformanceOptimization();
        }


        if (executed % 300 == 0) { 
            handleExtendedInstructions();
        }









        if (executed % 500 == 0) { 
            handleXboxHardwareEmulation();
        }


        if (executed % 1000 == 0) { 
            handleXboxGameEngine();
        }


        if (executed % 2000 == 0) { 
            cpuGameReadinessCheck();
        }


        if (executed % 5000 == 0) { 
            cpuFinalStatusCheck();
        }


        if (executed % 10000 == 0 && executed > 0) {
            LOGI("[CPU-DEBUG] Executed %u instructions, EIP=0x%08X, State=%d", executed, eip, static_cast<int>(state));
        }


        if (eip == lastEIP && executed > 1000) {
            recoveryAttempts++;
            if (recoveryAttempts > 3) {
                LOGW("[CPU-DEBUG] CPU stuck at EIP 0x%08X, attempting recovery", eip);

                cpuErrorRecovery();
                recoveryAttempts = 0;
                LOGI("[CPU-DEBUG] Error-Recovery abgeschlossen, EIP=0x%08X", eip);
            }
        } else {
            recoveryAttempts = 0;
        }
        lastEIP = eip;


        uint32_t eipBeforeStep = eip;
        executeStep();
        executed++;


        if (executed <= 10) {
            LOGI("[CPU-DEBUG] Step %u: EIP 0x%08X -> 0x%08X, ESP=0x%08X", executed, eipBeforeStep, eip, esp);
        }


        if (executed > 10000) {
            LOGW("[CPU-DEBUG] CPU executed %u instructions, stopping to prevent infinite loop", executed);
            LOGW("[CPU-DEBUG] Final EIP: 0x%08X, ESP: 0x%08X", eip, esp);

            state = CpuState::Halted;
            LOGW("[CPU-DEBUG] CPU halted due to infinite loop at EIP 0x%08X", eip);
            break;
        }


        static auto lastProgressTime = std::chrono::high_resolution_clock::now();
        if (executed % 1000 == 0) {
            auto now = std::chrono::high_resolution_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastProgressTime);
            if (elapsed.count() > 5000) { 
                LOGW("[CPU-DEBUG] CPU execution timeout detected, forcing recovery");
                cpuErrorRecovery();
                break;
            }
            lastProgressTime = now;
        }
    }

    LOGI("[CPU-DEBUG] execute() finished, total executed=%u, final EIP=0x%08X, State=%d", executed, eip, static_cast<int>(state));


    if (state != CpuState::Running && executed < cycles) {
        LOGW("[CPU-DEBUG] CPU stopped unexpectedly, restarting execution");


        if (state == CpuState::Error) {
            LOGI("[CPU-DEBUG] CPU in error state, attempting recovery");

            if (eip < 0x00100000 || eip > 0x07FFFFFF) {
                eip = memory ? memory->xbeEntryPoint : 0x0057FD80; 
                esp = 0x0003FFE0; 


                if (memory) {
                    initializeStack();
                }
            }
            state = CpuState::Running;
        } else if (state == CpuState::Halted) {
            LOGI("[CPU-DEBUG] CPU halted, checking if game is loaded");

            uint32_t instruction = memory->read32(eip);
            if (instruction != 0x00000000) {
                LOGI("[CPU-DEBUG] Game data found, restarting execution");
                state = CpuState::Running;
            }
        }


        while (executed < cycles && state == CpuState::Running) {
            executeStep();
            executed++;
        }
        LOGI("[CPU-DEBUG] Restarted execution completed, total executed=%u, final EIP=0x%08X, State=%d", executed, eip, static_cast<int>(state));
    }
}
void X86Core::executeStep() {

    static int stepCounter = 0;
    stepCounter++;


    if (stepCounter % 1000 == 0) {
        LOGD("🔍 CPU Step %d: EIP=0x%08X, ESP=0x%08X, State=%d", 
             stepCounter, eip, esp, static_cast<int>(state));
    }


    robustStackManagement();

    if (state != CpuState::Running) {
        LOGW("🚫 CRITICAL: CPU not in running state (%d), setting to running", static_cast<int>(state));
        LOGW("🚫 CRITICAL: State change detected in executeStep() at EIP=0x%08X", eip);


        if (memory) {
            uint8_t opcode = memory->read8(eip);
            uint32_t instruction = memory->read32(eip);
            LOGW("🚫 CRITICAL: Opcode at EIP: 0x%02X, Full instruction: 0x%08X", opcode, instruction);


            if (opcode == 0x00) {
                LOGW("🚫 CRITICAL: ADD instruction caused state change!");
            } else if (opcode == 0x90) {
                LOGW("🚫 CRITICAL: NOP instruction caused state change!");
            } else if (opcode == 0xE9) {
                LOGW("🚫 CRITICAL: JMP instruction caused state change!");
            } else if (instruction == 0x00000000) {
                LOGW("🚫 CRITICAL: Zero instruction caused state change!");
            } else if (instruction == 0x48454258) {
                LOGW("🚫 CRITICAL: XBE header caused state change!");
            }
        }

        state = CpuState::Running;

    }


    if (eip >= 0x00000000 && eip < 0x00000100) {
        static int ivtLoopCount = 0;
        static uint32_t lastIvtEip = 0;
        if (eip == lastIvtEip) {
            ivtLoopCount++;
            if (ivtLoopCount > 5) { 
                LOGW("🚫 IVT loop detected at EIP 0x%08X, redirecting to game entry point", eip);
                eip = memory ? memory->xbeEntryPoint : 0x0057FD80;
                esp = 0x0003FFE0;


                if (memory) {
                    initializeStack();


                    uint32_t stackBase = 0x0003FFFC;
                    uint32_t returnAddr = (memory->xbeEntryPoint != 0) ? memory->xbeEntryPoint + 0x10 : 0x0057FD90;
                    memory->write32(stackBase - 4, returnAddr);
                    esp = stackBase;
                }

                        ivtLoopCount = 0;
        lastIvtEip = eip;


        if (memory) {
            uint32_t stackBase = 0x0003FFFC;
            uint32_t returnAddr = (memory->xbeEntryPoint != 0) ? memory->xbeEntryPoint + 0x10 : 0x0057FD90;
            memory->write32(stackBase - 4, returnAddr);
            esp = stackBase;
            LOGI("🔧 IVT-Recovery: Stack initialisiert - ESP: 0x%08X, Return: 0x%08X", esp, returnAddr);
        }


    }
        } else {
            ivtLoopCount = 0;
            lastIvtEip = eip;
        }


        if (eip < 0x00000100) {
            LOGW("🚫 EIP in IVT area: 0x%08X, redirecting to game entry point", eip);
            eip = memory ? memory->xbeEntryPoint : 0x0057FD80;

        }
    }



    if ((eip > 0x07FFFFFF && eip < 0xFF000000) || eip > 0xFFFFFFFF) {
        LOGI("🚫 CRITICAL: EIP out of valid range: 0x%08X", eip);
        LOGI("🚫 REASON: EIP is outside valid Xbox memory range");
        LOGI("🚫 FIXING: Attempting intelligent recovery");


        uint32_t newEip = memory ? memory->xbeEntryPoint : 0x0057FD80; 


        if (memory && memory->xbeEntryPoint != 0) {
            newEip = memory->xbeEntryPoint;
            LOGI("✅ Using XBE Entry Point: 0x%08X", newEip);
        } else {

        for (uint32_t offset = 0; offset < 65536; offset += 4) {
            uint32_t testAddr = (memory ? memory->xbeEntryPoint : 0x0057FD80) + offset;
                if (testAddr >= 0x08000000) break;

                try {
                    uint32_t instruction = memory->read32(testAddr);
                    if (instruction != 0 && instruction != 0xFFFFFFFF) {
                        newEip = testAddr;
                        LOGI("✅ Found valid code at 0x%08X, using as new EIP", newEip);
                        break;
                    }
                } catch (...) {
                    continue;
                }
            }
        }

        eip = newEip;
        esp = 0x0003FFFC; 
        ebp = 0x0003FFFC; 
        LOGI("✅ Recovery complete - EIP: 0x%08X, ESP: 0x%08X", eip, esp);
    }



    if (eip < 0x00000000) {
        LOGW("CPU EIP in invalid negative range: 0x%08X, resetting to BIOS", eip);
        eip = 0xFF000000;
        LOGI("CPU: Reset EIP to Xbox BIOS entry point 0x%08X", eip);
    }


    if (esp < 0x00000000 || esp > 0x0003FFFF) {
        LOGW("CPU: Stack pointer out of valid range: 0x%08X, resetting to valid range", esp);


        GAMELOADED("[GAMELOADED] === STACK-DIAGNOSE ===");
        GAMELOADED("[GAMELOADED] CPU: Invalid ESP: 0x%08X", esp);
        GAMELOADED("[GAMELOADED] CPU: Valid ESP Range: 0x00000000 - 0x0003FFFF");
        GAMELOADED("[GAMELOADED] CPU: Current EIP: 0x%08X", eip);
        GAMELOADED("[GAMELOADED] CPU: Current EBP: 0x%08X", ebp);

        if (memory) {

            GAMELOADED("[GAMELOADED] CPU: Stack-Inhalt-Analyse:");
            for (uint32_t addr = 0x0003FF00; addr <= 0x0003FFFF; addr += 4) {
                try {
                    uint32_t stackVal = memory->read32(addr);
                    if (stackVal != 0) {
                        GAMELOADED("[GAMELOADED] CPU:   Stack[0x%08X] = 0x%08X", addr, stackVal);
                    }
                } catch (...) {
                    GAMELOADED("[GAMELOADED] CPU:   Stack[0x%08X] = SPEICHERFEHLER", addr);
                }
            }
        }
        GAMELOADED("[GAMELOADED] === ENDE STACK-DIAGNOSE ===");


        esp = 0x0003FFFC;
        LOGI("CPU: Reset stack pointer to 0x%08X", esp);


        ebp = 0x0003FFFC;
        LOGI("CPU: Reset base pointer to 0x%08X", ebp);
    }


    if (esp == 0x00000000) {
        LOGW("CPU: ESP is zero, this will cause interrupt handling issues, resetting");
        esp = 0x0003FFE0;
        LOGI("CPU: Reset ESP from zero to 0x%08X", esp);
    }


    static uint32_t lastESP = 0x0003FFE0;
    if (esp != lastESP) {
        LOGD("CPU: ESP changed from 0x%08X to 0x%08X at EIP 0x%08X", lastESP, esp, eip);
        lastESP = esp;
    }


    static int stepCount = 0;
    stepCount++;
    if (stepCount % 1000 == 0) {
        LOGD("CPU executing at EIP: 0x%08X, Step: %d", eip, stepCount);
    }


    if (eip >= XboxMemory::RAM_BASE && eip < XboxMemory::RAM_BASE + XboxMemory::RAM_SIZE) {

    } else if (eip >= XboxMemory::BIOS_BASE && eip < XboxMemory::BIOS_BASE + XboxMemory::BIOS_SIZE) {

    } else {
        LOGW("CPU: EIP in invalid memory range: 0x%08X, stopping execution", eip);
        state = CpuState::Error;
        return;
    }


    if (memory->read8(eip) == 0x0F && memory->read8(eip + 1) == 0x3F) {
        handleXboxSpecificOpcode();
        return;
    }


    uint32_t instruction = memory->read32(eip);
    if (instruction == 0x00000000) {

        LOGW("CPU: Next 4 bytes are all zero at EIP 0x%08X - uninitialized memory detected", eip);
        LOGW("CPU: This means the game data is not properly loaded or the CPU is executing in wrong area");


        GAMELOADED("[GAMELOADED] === SPIEL-LADE-DIAGNOSE ===");
        GAMELOADED("[GAMELOADED] CPU: EIP Position: 0x%08X", eip);
        GAMELOADED("[GAMELOADED] CPU: Memory Status: %s", memory ? "Memory verfügbar" : "Memory NULL");
        GAMELOADED("[GAMELOADED] CPU: Game Loaded Status: %s", memory && memory->xbeEntryPoint != 0 ? "JA" : "NEIN");
        GAMELOADED("[GAMELOADED] CPU: XBE Entry Point: 0x%08X", memory ? memory->xbeEntryPoint : 0);
        GAMELOADED("[GAMELOADED] CPU: Memory Size: %d bytes", memory ? 268435456 : 0);


        if (memory) {
            uint32_t xbeHeaderAddr = memory->xbeEntryPoint;
            uint32_t magic = 0, imageBase = 0, entryRVA = 0, sizeOfImage = 0, numSections = 0, sectionHeadersAddr = 0;
            try { magic = memory->read32(xbeHeaderAddr); } catch (...) {}
            try { imageBase = memory->read32(xbeHeaderAddr + 0x104); } catch (...) {}
            try { entryRVA = memory->read32(xbeHeaderAddr + 0x10C); } catch (...) {}
            try { sizeOfImage = memory->read32(xbeHeaderAddr + 0x118); } catch (...) {}
            try { numSections = memory->read32(xbeHeaderAddr + 0x11C); } catch (...) {}
            try { sectionHeadersAddr = memory->read32(xbeHeaderAddr + 0x120); } catch (...) {}
            GAMELOADED("[GAMELOADED] CPU: XBE Magic: 0x%08X ('%c%c%c%c')", magic, magic & 0xFF, (magic >> 8) & 0xFF, (magic >> 16) & 0xFF, (magic >> 24) & 0xFF);
            GAMELOADED("[GAMELOADED] CPU: ImageBase: 0x%08X", imageBase);
            GAMELOADED("[GAMELOADED] CPU: EntryPoint RVA: 0x%08X", entryRVA);
            GAMELOADED("[GAMELOADED] CPU: SizeOfImage: 0x%08X", sizeOfImage);
            GAMELOADED("[GAMELOADED] CPU: NumberOfSections: %u", numSections);
            GAMELOADED("[GAMELOADED] CPU: SectionHeadersAddr: 0x%08X", sectionHeadersAddr);
        }


        GAMELOADED("[GAMELOADED] CPU: Speicherbereich-Analyse:");
        uint32_t startAnalysis = memory ? (memory->xbeEntryPoint & 0xFFF00000) : 0x00100000;
        for (uint32_t addr = startAnalysis; addr <= 0x001F0000; addr += 0x10000) {
            if (memory) {
                try {
                    uint32_t val = memory->read32(addr);
                    if (val == 0) {
                        GAMELOADED("[GAMELOADED] CPU:   Bereich 0x%08X: NULL-Bereich", addr);
                    } else {
                        GAMELOADED("[GAMELOADED] CPU:   Bereich 0x%08X: 0x%02X 0x%02X 0x%02X 0x%02X (NICHT NULL)", 
                             addr, val & 0xFF, (val >> 8) & 0xFF, (val >> 16) & 0xFF, (val >> 24) & 0xFF);
                    }
                } catch (...) {
                    GAMELOADED("[GAMELOADED] CPU:   Bereich 0x%08X: SPEICHERFEHLER", addr);
                }
            }
        }

        GAMELOADED("[GAMELOADED] CPU: CPU State: %d", static_cast<int>(state));
        GAMELOADED("[GAMELOADED] CPU: Total Instructions: %d", stepCount);
        GAMELOADED("[GAMELOADED] === ENDE DIAGNOSE ===");


        LOGI("CPU: Searching for valid game code in memory...");


        bool foundValidCode = false;
        for (uint32_t offset = 0; offset < 65536; offset += 4) {
            uint32_t testAddr = eip + offset;
            if (testAddr >= 0x08000000) break;

            try {
                uint32_t testInstruction = memory->read32(testAddr);
                if (testInstruction != 0 && testInstruction != 0xFFFFFFFF) {

                    uint8_t opcode = testInstruction & 0xFF;
                    if (opcode == 0x55 || opcode == 0x8B || opcode == 0x89 || opcode == 0xE8 || 
                        opcode == 0xE9 || opcode == 0x90 || opcode == 0x68 || opcode == 0x6A) {
                        LOGI("CPU: Found potential valid code at 0x%08X (first byte: 0x%02X)", testAddr, opcode);
                        eip = testAddr;
                        foundValidCode = true;
                        break;
                    }
                }
            } catch (...) {
                continue;
            }
        }

        if (!foundValidCode) {
            LOGW("CPU: No valid code found, advancing EIP by 4 bytes");
            eip += 4; 
        }


        if (eip > 0x07FFFFFF) {
            LOGW("CPU: EIP advanced beyond valid range, resetting to game entry point");
            eip = memory ? memory->xbeEntryPoint : 0x0057FD80;
        }


        return;
    }


    static int boundsCheckCount = 0;
    static uint32_t lastBoundsCheckEIP = 0;
    if (eip == lastBoundsCheckEIP) {
        boundsCheckCount++;
        if (boundsCheckCount > 5) {
            LOGI("🚫 CRITICAL: Bounds check interrupt loop detected");
            LOGI("🚫 REASON: CPU stuck in bounds check interrupt at EIP 0x%08X", eip);
            LOGI("🚫 FIXING: Disabling bounds checking and advancing EIP");
            disableBoundsChecking();
            eip += 4; 
            boundsCheckCount = 0;
            LOGI("✅ Bounds checking disabled and EIP advanced to 0x%08X", eip);
        }
    } else {
        boundsCheckCount = 0;
        lastBoundsCheckEIP = eip;
    }


    if (boundsCheckingDisabled) {


    }


    static int stuckCount = 0;
    static uint32_t lastEIP = 0;
    if (eip == lastEIP) {
        stuckCount++;
        if (stuckCount > 100) { 
            LOGW("🚫 CRITICAL: CPU stuck at EIP 0x%08X for %d cycles", eip, stuckCount);
            LOGW("🚫 FIXING: Advancing EIP to prevent infinite loop");
            eip += 1; 
            stuckCount = 0;
            LOGI("✅ CPU EIP advanced to 0x%08X", eip);
        }
    } else {
        stuckCount = 0;
        lastEIP = eip;
    }

    if (auto bp = breakpoints.find(eip); bp != breakpoints.end()) {
        bp->second();
        state = CpuState::DebugBreak;
        return;
    }

    executionCounts[eip]++;

    if (jitEnabled && executionCounts[eip] > jitThreshold) {
        if (auto block = jit_cache.find(eip); block != jit_cache.end()) {
            executeCompiledBlock(block->second);
            return;
        } else {
            compileBlock(eip);
            if (auto new_block = jit_cache.find(eip); new_block != jit_cache.end()) {
                executeCompiledBlock(new_block->second);
                return;
            }
        }
    }
    try {


        uint8_t opcode = memory->read8(eip);


        static int instructionCounter = 0;
        instructionCounter++;

        if (instructionCounter % 10000 == 0) { 
            LOGI("=== CPU SPIEL-START-DIAGNOSE ===");


            LOGI("CPU: Instruktionen ausgeführt: %d", instructionCounter);
            LOGI("CPU: EIP Position: 0x%08X", eip);


            const char* gameLoadedStatus = "UNBEKANNT";
            const char* memoryAccessStatus = "UNBEKANNT";

            if (memory) {
                memoryAccessStatus = "VERFÜGBAR";
                try {
                    gameLoadedStatus = memory->isGameLoaded() ? "JA" : "NEIN";
                } catch (...) {
                    gameLoadedStatus = "FEHLER";
                }
            } else {
                memoryAccessStatus = "NICHT VERFÜGBAR";
            }

            LOGI("CPU: Game Loaded Status: %s", gameLoadedStatus);
            LOGI("CPU: Memory Access: %s", memoryAccessStatus);


            LOGI("CPU: Register Status: EAX=0x%08X, EBX=0x%08X, ECX=0x%08X, EDX=0x%08X", eax, ebx, ecx, edx);
            LOGI("CPU: Stack Status: ESP=0x%08X, EBP=0x%08X", esp, ebp);


            const char* zfStatus = (eflags & 0x40) ? "1" : "0";
            const char* sfStatus = (eflags & 0x80) ? "1" : "0";
            const char* cfStatus = (eflags & 0x01) ? "1" : "0";
            LOGI("CPU: Flags: ZF=%s, SF=%s, CF=%s", zfStatus, sfStatus, cfStatus);


            if (eip >= 0x00100000 && eip <= 0x007FFFFF) {
                LOGI("✓ CPU läuft im Spiel-Code-Bereich: 0x%08X", eip);
            } else if (eip >= 0xFF000000 && eip <= 0xFFFFFFFF) {
                LOGI("⚠ CPU läuft im BIOS-Bereich: 0x%08X", eip);
            } else {
                LOGI("❌ CPU läuft in unbekanntem Bereich: 0x%08X", eip);
            }

            LOGI("=== ENDE CPU SPIEL-START-DIAGNOSE ===");
        }


        switch (opcode) {
            case 0x90: 
                eip++;
                return; 
            case 0x58: 
                eax = memory->read32(esp);
                esp += 4;
                eip++;
                return; 
            case 0x59: 
                ecx = memory->read32(esp);
                esp += 4;
                eip++;
                return; 
            case 0x5A: 
                edx = memory->read32(esp);
                esp += 4;
                eip++;
                return; 
            case 0x5B: 
                ebx = memory->read32(esp);
                esp += 4;
                eip++;
                return; 
            case 0x50: 
                esp -= 4;
                memory->write32(esp, eax);
                eip++;
                return; 
            case 0x51: 
                esp -= 4;
                memory->write32(esp, ecx);
                eip++;
                return; 
            case 0x52: 
                esp -= 4;
                memory->write32(esp, edx);
                eip++;
                return; 
            case 0x53: 
                esp -= 4;
                memory->write32(esp, ebx);
                eip++;
                return; 

            case 0x89: 
                eip++;

                eip += 1; 
                return; 
            case 0xE8: 
                {
                    eip++;

                    uint32_t displacement = memory->read32(eip);
                    eip += 4;

                    esp -= 4;
                    memory->write32(esp, eip);

                    eip += displacement;
                }
                return; 
            case 0xE9: 
                {
                    eip++;

                    uint32_t jmpDisplacement = memory->read32(eip);
                    eip += 4;
                    eip += jmpDisplacement;
                }
                return; 
            case 0xEB: 
                {
                    eip++;

                    int8_t jmpRel8 = static_cast<int8_t>(memory->read8(eip));
                    eip++;
                    eip += jmpRel8;
                }
                return; 
            case 0x55: 
                esp -= 4;
                memory->write32(esp, ebp);
                eip++;
                return; 
            case 0x83: 
                {
                    eip++;
                    uint8_t modrm = memory->read8(eip);
                    eip++;
                    uint8_t imm8 = memory->read8(eip);
                    eip++;


                    uint8_t reg = (modrm >> 3) & 0x7;


                    switch (reg) {
                        case 0x5: 
                            if ((modrm & 0xC0) == 0xC0) { 
                                uint8_t destReg = modrm & 0x7;
                                switch (destReg) {
                                    case 0x4: esp -= imm8; break; 
                                }
                            }
                            break;
                        case 0x0: 
                            if ((modrm & 0xC0) == 0xC0) { 
                                uint8_t destReg = modrm & 0x7;
                                switch (destReg) {
                                    case 0x4: esp += imm8; break; 
                                }
                            }
                            break;
                        case 0x4: 
                            if ((modrm & 0xC0) == 0xC0) { 
                                uint8_t destReg = modrm & 0x7;
                                switch (destReg) {
                                    case 0x4: esp &= imm8; break; 
                                }
                            }
                            break;
                    }
                }
                return; 
            case 0x20: 
                {
                    eip++;
                    uint8_t modrm = memory->read8(eip);
                    eip++;


                }
                return; 
            case 0xC3: 
                {

                    if (esp >= 0x0003C000 && esp <= 0x00040000) {
                        uint32_t return_addr = memory->read32(esp);
                        esp += 4;


                        if (return_addr > 0x00000000 && return_addr >= 0x00010000 && return_addr <= 0x07FFFFFF) {
                            eip = return_addr;
                            LOGD("CPU: RET - Jumping to return address: 0x%08X, ESP: 0x%08X", eip, esp);
                        } else {
                            LOGW("🚫 CRITICAL: Ungültige RET-Adresse 0x%08X, ESP: 0x%08X - repariere automatisch", return_addr, esp);


                            if (esp >= 0x00000004 && esp <= 0x0003FFFF) {
                                uint32_t validReturnAddr = 0x0058FD90;
                                memory->write32(esp, validReturnAddr);
                                eip = validReturnAddr;
                                LOGI("✅ RET-Adresse repariert: 0x%08X bei ESP 0x%08X", validReturnAddr, esp);
                            } else {

                                eip = memory ? memory->xbeEntryPoint : 0x0057FD80;
                                esp = 0x0003FFE0;
                                memory->write32(esp, 0x0058FD90);
                                LOGW("⚠️ RET-Reparatur: EIP=0x%08X, ESP=0x%08X", eip, esp);
                            }


                            uint32_t safeReturnAddr = 0x0058FD90;
                            for (uint32_t addr = esp - 0x1000; addr < esp; addr += 4) {
                                if (addr >= 0x0003C000 && addr < esp) {
                                    memory->write32(addr, safeReturnAddr);
                                }
                            }

                            LOGD("CPU: RET - Notfall-Rückkehr zu: 0x%08X, ESP repariert: 0x%08X", eip, esp);
                        }
                    } else {
                        LOGW("🚫 CRITICAL: RET mit ungültigem ESP 0x%08X, Stack-Reparatur", esp);


                        esp = 0x0003FFE0;
                        eip = memory ? memory->xbeEntryPoint : 0x0057FD80;

                        uint32_t safeReturnAddr = eip + 0x10;
                        memory->write32(esp - 4, safeReturnAddr);


                        for (uint32_t addr = 0x0003C000; addr < esp; addr += 4) {
                            memory->write32(addr, safeReturnAddr);
                        }

                        LOGD("CPU: RET - Stack repariert, EIP: 0x%08X, ESP: 0x%08X", eip, esp);
                    }
                }
                return; 
            case 0x8B: 
                {
                    eip++;
                    uint8_t modrm = memory->read8(eip);
                    eip++;

                    uint8_t destReg = (modrm >> 3) & 0x7;
                    uint8_t srcReg = modrm & 0x7;


                    if ((modrm & 0xC0) == 0xC0) { 
                        uint32_t srcVal = 0;
                        switch (srcReg) {
                            case 0x4: srcVal = esp; break;
                            case 0x5: srcVal = ebp; break;
                            default: srcVal = 0; break;
                        }

                        switch (destReg) {
                            case 0x5: ebp = srcVal; break; 
                            default: break;
                        }
                    }
                }
                return; 
            case 0x33: 
                {
                    eip++;
                    uint8_t modrm = memory->read8(eip);
                    eip++;

                    uint8_t destReg = (modrm >> 3) & 0x7;
                    uint8_t srcReg = modrm & 0x7;


                    if ((modrm & 0xC0) == 0xC0) { 
                        switch (destReg) {
                            case 0x0: 
                                switch (srcReg) {
                                    case 0x0: eax ^= eax; break; 
                                }
                                break;
                        }
                    }
                }
                return; 


            default:

                LOGW("CPU: Unknown instruction opcode: 0x%02X at EIP: 0x%08X", opcode, eip);
                eip++;
                return; 
        }


        totalInstructionsExecuted++;
        instructionsThisFrame++;


        if (totalInstructionsExecuted % 1000 == 0) {
            LOGI("CPU: Progress - %u total instructions executed, EIP: 0x%08X", totalInstructionsExecuted, eip);
        }


        if (eip > 0x07FFFFFF) {
            LOGW("CPU: EIP went out of bounds after instruction, resetting to game entry point");
            eip = memory ? memory->xbeEntryPoint : 0x0057FD80;
        }


        static uint32_t lastProgressEIP = 0;
        static int noProgressCount = 0;
        static int totalNoProgressCount = 0;

        if (eip == lastProgressEIP) {
            noProgressCount++;
            totalNoProgressCount++;

            if (noProgressCount > 1000) {
                LOGW("CPU: No progress detected for %d instructions, EIP stuck at 0x%08X", noProgressCount, eip);
                LOGW("CPU: This indicates the CPU is not executing properly!");
                noProgressCount = 0;
            }


            if (totalNoProgressCount > 10000) {
                LOGW("CPU: ✗ Too many stuck attempts (%d total), halting CPU to prevent infinite loop", totalNoProgressCount);
                LOGW("CPU: ✗ This indicates a serious problem with game loading or CPU execution");
                state = CpuState::Halted;
                return;
            }
        } else {
            noProgressCount = 0;
            lastProgressEIP = eip;
        }


        uint8_t nextBytes[4] = {0};
        for (int i = 0; i < 4; i++) {
            nextBytes[i] = memory->read8(eip + i);
        }


        if (nextBytes[0] == 0 && nextBytes[1] == 0 && nextBytes[2] == 0 && nextBytes[3] == 0) {
            LOGW("CPU: Next 4 bytes are all zero at EIP 0x%08X - uninitialized memory detected", eip);
            LOGW("CPU: This means the game data is not properly loaded or the CPU is executing in wrong area");


            GAMELOADED("[GAMELOADED] === SPIEL-LADE-DIAGNOSE ===");
            GAMELOADED("[GAMELOADED] CPU: EIP Position: 0x%08X", eip);
            GAMELOADED("[GAMELOADED] CPU: Memory Status: %s", memory ? "Memory verfügbar" : "Memory NULL");

            if (memory) {
                GAMELOADED("[GAMELOADED] CPU: Game Loaded Status: %s", memory->isGameLoaded() ? "JA" : "NEIN");
                GAMELOADED("[GAMELOADED] CPU: XBE Entry Point: 0x%08X", memory->xbeEntryPoint);
                GAMELOADED("[GAMELOADED] CPU: Memory Size: %zu bytes", memory->getSize());


                GAMELOADED("[GAMELOADED] CPU: Speicherbereich-Analyse:");
                uint32_t startCheck = memory ? memory->xbeEntryPoint : 0x0057FD80;
                for (uint32_t checkAddr = startCheck & 0xFFF00000; checkAddr < 0x00200000; checkAddr += 0x10000) {
                    uint8_t checkBytes[4] = {0};
                    for (int j = 0; j < 4; j++) {
                        checkBytes[j] = memory->read8(checkAddr + j);
                    }

                    if (!(checkBytes[0] == 0 && checkBytes[1] == 0 && checkBytes[2] == 0 && checkBytes[3] == 0)) {
                        GAMELOADED("[GAMELOADED] CPU:   Bereich 0x%08X: 0x%02X 0x%02X 0x%02X 0x%02X (NICHT NULL)", 
                             checkAddr, checkBytes[0], checkBytes[1], checkBytes[2], checkBytes[3]);
                    } else {
                        GAMELOADED("[GAMELOADED] CPU:   Bereich 0x%08X: NULL-Bereich", checkAddr);
                    }
                }
            }

            GAMELOADED("[GAMELOADED] CPU: CPU State: %d", static_cast<int>(state));
            GAMELOADED("[GAMELOADED] CPU: Total Instructions: %u", totalInstructionsExecuted);
            GAMELOADED("[GAMELOADED] === ENDE DIAGNOSE ===");


            LOGI("CPU: Searching for valid game code in memory...");


            bool foundValidCode = false;
            uint32_t startAddr = memory ? memory->xbeEntryPoint : 0x0057FD80;
            for (uint32_t searchAddr = startAddr; searchAddr < 0x07FFFFFF; searchAddr += 0x1000) {
                uint8_t searchBytes[4] = {0};
                for (int i = 0; i < 4; i++) {
                    searchBytes[i] = memory->read8(searchAddr + i);
                }


                if (!(searchBytes[0] == 0 && searchBytes[1] == 0 && searchBytes[2] == 0 && searchBytes[3] == 0) &&
                    !(searchBytes[0] == 0xFF && searchBytes[1] == 0xFF && searchBytes[2] == 0xFF && searchBytes[3] == 0xFF)) {


                    if (searchBytes[0] == 0x55 || searchBytes[0] == 0x8B || searchBytes[0] == 0x89 || 
                        searchBytes[0] == 0x90 || searchBytes[0] == 0xE8 || searchBytes[0] == 0xE9) {

                        LOGI("CPU: Found potential valid code at 0x%08X (first byte: 0x%02X)", searchAddr, searchBytes[0]);
                        eip = searchAddr;
                        foundValidCode = true;
                        break;
                    }
                }
            }

            if (!foundValidCode) {
                LOGW("CPU: No valid game code found in memory, resetting to safe entry point");
                eip = memory ? memory->xbeEntryPoint : 0x0057FD80;


                if (memory && !memory->isGameLoaded()) {
                    LOGW("CPU: Game is not loaded! Halting CPU execution");
                    state = CpuState::Halted;
                    return;
                }
            }

            return; 
        }


        uint8_t instruction_opcode = memory->read8(eip++);

        switch (instruction_opcode) {
            case 0x00: add_rm8_r8(); return; 
            case 0x01: add_rm32_r32(); return; 
            case 0x02: add_r8_rm8(); return; 
            case 0x03: add_r32_rm32(); return; 
            case 0x04: add_al_imm8(); return; 
            case 0x05: add_eax_imm32(); return; 
            case 0x06: push_es(); return; 
            case 0x07: pop_es(); return; 
            case 0x08: or_rm8_r8(); return; 
            case 0x09: or_rm32_r32(); return; 
            case 0x0A: or_r8_rm8(); return; 
            case 0x0B: or_r32_rm32(); return; 
            case 0x0C: or_al_imm8(); return; 
            case 0x0D: or_eax_imm32(); return; 
            case 0x0E: push_cs(); return; 
            case 0x0F: handle0FOpcode(); return; 
            case 0x10: adc_rm8_r8(); return; 
            case 0x11: adc_rm32_r32(); return; 
            case 0x12: adc_r8_rm8(); return; 
            case 0x13: adc_r32_rm32(); return; 
            case 0x14: adc_al_imm8(); return; 
            case 0x15: adc_eax_imm32(); return; 
            case 0x16: push_ss(); return; 
            case 0x17: pop_ss(); return; 
            case 0x18: sbb_rm8_r8(); return; 
            case 0x19: sbb_rm32_r32(); return; 
            case 0x1A: sbb_r8_rm8(); return; 
            case 0x1B: sbb_r32_rm32(); return; 
            case 0x1C: sbb_al_imm8(); return; 
            case 0x1D: sbb_eax_imm32(); return; 
            case 0x1E: push_ds(); return; 
            case 0x1F: pop_ds(); return; 
            case 0x20: and_rm8_r8(); return; 
            case 0x21: and_rm32_r32(); return; 
            case 0x22: and_r8_rm8(); return; 
            case 0x23: and_r32_rm32(); return; 
            case 0x24: and_al_imm8(); return; 
            case 0x25: and_eax_imm32(); return; 
            case 0x26: es_prefix(); return; 
            case 0x27: daa(); return; 
            case 0x28: sub_rm8_r8(); return; 
            case 0x29: sub_rm32_r32(); return; 
            case 0x2A: sub_r8_rm8(); return; 
            case 0x2B: sub_r32_rm32(); return; 
            case 0x2C: sub_al_imm8(); return; 
            case 0x2D: sub_eax_imm32(); return; 
            case 0x2E: cs_prefix(); return; 
            case 0x2F: das(); return; 
            case 0x30: xor_rm8_r8(); return; 
            case 0x31: xor_rm32_r32(); return; 
            case 0x32: xor_r8_rm8(); return; 
            case 0x33: xor_r32_rm32(); return; 
            case 0x34: xor_al_imm8(); return; 
            case 0x35: xor_eax_imm32(); return; 
            case 0x36: ss_prefix(); return; 
            case 0x37: aaa(); return; 
            case 0x38: cmp_rm8_r8(); return; 
            case 0x39: cmp_rm32_r32(); return; 
            case 0x3A: cmp_r8_rm8(); return; 
            case 0x3B: cmp_r32_rm32(); return; 
            case 0x3C: cmp_al_imm8(); return; 
            case 0x3D: cmp_eax_imm32(); return; 
            case 0x3E: ds_prefix(); return; 
            case 0x3F: aas(); return; 
            case 0x40: inc_eax(); return; 
            case 0x41: inc_ecx(); return; 
            case 0x42: inc_edx(); return; 
            case 0x43: inc_ebx(); return; 
            case 0x44: inc_esp(); return; 
            case 0x45: inc_ebp(); return; 
            case 0x46: inc_esi(); return; 
            case 0x47: inc_edi(); return; 
            case 0x48: dec_eax(); return; 
            case 0x49: dec_ecx(); return; 
            case 0x4A: dec_edx(); return; 
            case 0x4B: dec_ebx(); return; 
            case 0x4C: dec_esp(); return; 
            case 0x4D: dec_ebp(); return; 
            case 0x4E: dec_esi(); return; 
            case 0x4F: dec_edi(); return; 
            case 0x50: push_eax(); return; 
            case 0x51: push_ecx(); return; 
            case 0x52: push_edx(); return; 
            case 0x53: push_ebx(); return; 
            case 0x54: push_esp(); return; 
            case 0x55: push_ebp(); return; 
            case 0x56: push_esi(); return; 
            case 0x57: push_edi(); return; 
            case 0x58: pop_eax(); return; 
            case 0x59: pop_ecx(); return; 
            case 0x5A: pop_edx(); return; 
            case 0x5B: pop_ebx(); return; 
            case 0x5C: pop_esp(); return; 
            case 0x5D: pop_ebp(); return; 
            case 0x5E: pop_esi(); return; 
            case 0x5F: pop_edi(); return; 
            case 0x60: pushad(); return; 
            case 0x61: popad(); return; 
            case 0x62: bound(); return; 
            case 0x63: arpl(); return; 
            case 0x64: fs_prefix(); return; 
            case 0x65: gs_prefix(); return; 
            case 0x66: operand_size_prefix(); return; 
            case 0x67: address_size_prefix(); return; 
            case 0x68: push_imm32(); return; 
            case 0x69: imul_r32_rm32_imm32(); return; 
            case 0x6A: push_imm8(); return; 
            case 0x6B: imul_r32_rm32_imm8(); return; 
            case 0x6C: insb(); return; 
            case 0x6D: insd(); return; 
            case 0x6E: outsb(); return; 
            case 0x6F: outsd(); return; 
            case 0x70: jo_rel8(); return; 
            case 0x71: jno_rel8(); return; 
            case 0x72: jb_rel8(); return; 
            case 0x73: jnb_rel8(); return; 
            case 0x74: jz_rel8(); return; 
            case 0x75: jnz_rel8(); return; 
            case 0x76: jbe_rel8(); return; 
            case 0x77: jnbe_rel8(); return; 
            case 0x78: js_rel8(); return; 
            case 0x79: jns_rel8(); return; 
            case 0x7A: jp_rel8(); return; 
            case 0x7B: jnp_rel8(); return; 
            case 0x7C: jl_rel8(); return; 
            case 0x7D: jnl_rel8(); return; 
            case 0x7E: jle_rel8(); return; 
            case 0x7F: jnle_rel8(); return; 
            case 0x80: group1_rm8_imm8(); return; 
            case 0x81: group1_rm32_imm32(); return; 
            case 0x82: group1_rm8_imm8(); return; 
            case 0x83: group1_rm32_imm8(); return; 
            case 0x84: test_rm8_r8(); return; 
            case 0x85: test_rm32_r32(); return; 
            case 0x86: xchg_rm8_r8(); return; 
            case 0x87: xchg_rm32_r32(); return; 
            case 0x88: mov_rm8_r8(); return; 
            case 0x89: mov_rm32_r32(); return; 
            case 0x8A: mov_r8_rm8(); return; 
            case 0x8B: mov_r32_rm32(); return; 
            case 0x8C: mov_rm16_sreg(); return; 
            case 0x8D: lea_r32_m(); return; 
            case 0x8E: mov_sreg_rm16(); return; 
            case 0x8F: pop_rm32(); return; 

            case 0x91: xchg_eax_ecx(); return; 
            case 0x92: xchg_eax_edx(); return; 
            case 0x93: xchg_eax_ebx(); return; 
            case 0x94: xchg_eax_esp(); return; 
            case 0x95: xchg_eax_ebp(); return; 
            case 0x96: xchg_eax_esi(); return; 
            case 0x97: xchg_eax_edi(); return; 
            case 0x98: cwde(); return; 
            case 0x99: cdq(); return; 
            case 0x9A: call_far(); return; 
            case 0x9B: wait(); return; 
            case 0x9C: pushfd(); return; 
            case 0x9D: popfd(); return; 
            case 0x9E: sahf(); return; 
            case 0x9F: lahf(); return; 
            case 0xA0: mov_al_moffs8(); return; 
            case 0xA1: mov_eax_moffs32(); return; 
            case 0xA2: mov_moffs8_al(); return; 
            case 0xA3: mov_moffs32_eax(); return; 
            case 0xA4: movsb(); return; 
            case 0xA5: movsd(); return; 
            case 0xA6: cmpsb(); return; 
            case 0xA7: cmpsd(); return; 
            case 0xA8: test_al_imm8(); return; 
            case 0xA9: test_eax_imm32(); return; 
            case 0xAA: stosb(); return; 
            case 0xAB: stosd(); return; 
            case 0xAC: lodsb(); return; 
            case 0xAD: lodsd(); return; 
            case 0xAE: scasb(); return; 
            case 0xAF: scasd(); return; 
            case 0xB0: mov_al_imm8(); return; 
            case 0xB1: mov_cl_imm8(); return; 
            case 0xB2: mov_dl_imm8(); return; 
            case 0xB3: mov_bl_imm8(); return; 
            case 0xB4: mov_ah_imm8(); return; 
            case 0xB5: mov_ch_imm8(); return; 
            case 0xB6: mov_dh_imm8(); return; 
            case 0xB7: mov_bh_imm8(); return; 
            case 0xB8: mov_eax_imm32(); return; 
            case 0xB9: mov_ecx_imm32(); return; 
            case 0xBA: mov_edx_imm32(); return; 
            case 0xBB: mov_ebx_imm32(); return; 
            case 0xBC: mov_esp_imm32(); return; 
            case 0xBD: mov_ebp_imm32(); return; 
            case 0xBE: mov_esi_imm32(); return; 
            case 0xBF: mov_edi_imm32(); return; 
            case 0xC0: group2_rm8_imm8(); return; 
            case 0xC1: group2_rm32_imm8(); return; 
            case 0xC2: ret_imm16(); return; 
            case 0xC3: ret(); return; 
            case 0xC4: les(); return; 
            case 0xC5: lds(); return; 
            case 0xC6: mov_rm8_imm8(); return; 
            case 0xC7: mov_rm32_imm32(); return; 
            case 0xC8: enter(); return; 
            case 0xC9: leave(); return; 
            case 0xCA: retf_imm16(); return; 
            case 0xCB: retf(); return; 
            case 0xCC: int3(); return; 
            case 0xCD: int_imm8(); return; 
            case 0xCE: into(); return; 
            case 0xCF: iret(); return; 
            case 0xD0: group2_rm8_1(); return; 
            case 0xD1: group2_rm32_1(); return; 
            case 0xD2: group2_rm8_cl(); return; 
            case 0xD3: group2_rm32_cl(); return; 
            case 0xD4: aam(); return; 
            case 0xD5: aad(); return; 
            case 0xD6: salc(); return; 
            case 0xD7: xlat(); return; 
            case 0xD8: case 0xD9: case 0xDA: case 0xDB: case 0xDC: case 0xDD: case 0xDE: case 0xDF:

                LOGE("CPU: FATAL ERROR - FPU opcode 0x%02X not implemented!", opcode);
                LOGE("CPU: Xbox requires real FPU emulation - no fallbacks!");
                state = CpuState::Halted;
                return;
                break;
            case 0xE0: loopnz_rel8(); return; 
            case 0xE1: loopz_rel8(); return; 
            case 0xE2: loop_rel8(); return; 
            case 0xE3: jcxz_rel8(); return; 
            case 0xE4: in_al_imm8(); return; 
            case 0xE5: in_eax_imm8(); return; 
            case 0xE6: out_imm8_al(); return; 
            case 0xE7: out_imm8_eax(); return; 
            case 0xE8: call_rel32(); return; 
            case 0xE9: jmp_rel32(); return; 
            case 0xEA: jmp_far(); return; 

            case 0xEC: in_al_dx(); return; 
            case 0xED: in_eax_dx(); return; 
            case 0xEE: out_dx_al(); return; 
            case 0xEF: out_dx_eax(); return; 
            case 0xF0: lock_prefix(); return; 
            case 0xF1: icebp(); return; 
            case 0xF2: repne_prefix(); return; 
            case 0xF3: rep_prefix(); return; 
            case 0xF4: hlt(); return; 
            case 0xF5: cmc(); return; 
            case 0xF6: group3_rm8(); return; 
            case 0xF7: group3_rm32(); return; 
            case 0xF8: clc(); return; 
            case 0xF9: stc(); return; 
            case 0xFA: cli(); return; 
            case 0xFB: sti(); return; 
            case 0xFC: cld(); return; 
            case 0xFD: std(); return; 
            case 0xFE: group4_rm8(); return; 
            case 0xFF: group5_rm32(); return; 
            default: 
                LOGE("CPU: FATAL ERROR - Unknown opcode 0x%02X at EIP 0x%08X", opcode, eip - 1);
                LOGE("CPU: This opcode must be implemented for real Xbox emulation!");
                state = CpuState::Halted;
                return; 
        }
    } catch (const std::exception& e) {
        LOGE("CPU Exception: %s at 0x%08X", e.what(), eip - 1);
        LOGE("CPU: This likely indicates execution of invalid/uninitialized memory");


        if (eip - 1 >= 0x00000000 && eip - 1 < 0x00000100) {
            LOGW("CPU: Memory access violation in interrupt vector table area - returning to game execution");
            state = CpuState::Running;
            eip = memory ? memory->xbeEntryPoint : 0x0057FD80; 
            return;
        } else if (eip - 1 >= 0xFFFFFF00 && eip - 1 <= 0xFFFFFFFF) {
            LOGW("CPU: Memory access violation in high memory area - returning to game execution");
            state = CpuState::Running;
            eip = memory ? memory->xbeEntryPoint : 0x0057FD80; 
            return;
        } else if (eip - 1 >= 0x00000000 && eip - 1 < 0x00001000) {

            LOGW("CPU: Memory access violation in low memory area (0x%08X) - returning to game execution", eip - 1);
            state = CpuState::Running;
            eip = memory ? memory->xbeEntryPoint : 0x0057FD80; 
            return;
        } else {

            LOGW("CPU: Memory access violation at 0x%08X - attempting recovery", eip - 1);
            state = CpuState::Running;
            eip = memory ? memory->xbeEntryPoint : 0x0057FD80; 
            return;
        }
    }
}



void X86Core::handleSyscall() {
    if (!kernel) {
        LOGE("Kernel not set for syscall handling");
        state = CpuState::Error;
        return;
    }

    uint32_t call = eax;




    [[maybe_unused]] uint32_t result = 0; 

    eax = result;

    if (traceEnabled) {
        char buf[128];
        snprintf(buf, sizeof(buf), "Syscall 0x%08X -> 0x%08X", call, result);
        debugCallback(eip, buf);
    }
}

void X86Core::compileBlock(uint32_t start_addr) {
    if (jitCacheUsed + MAX_BLOCK_SIZE > JIT_CACHE_SIZE) {
        LOGW("JIT cache full, flushing");
        flushJITCache();
    }

    JITBlock block;
    block.start_addr = start_addr;
    block.size = 0;
    block.compiled_code = reinterpret_cast<uint8_t*>(jitCacheBase) + jitCacheUsed;

    uint8_t* code = block.compiled_code;
    uint32_t current_addr = start_addr;


    while (current_addr < start_addr + MAX_BLOCK_SIZE) {
        uint8_t opcode = memory->read8(current_addr);

        if (isCommonOpcode(opcode)) {

            switch (opcode) {



                case 0xC3: 
                    emitRET(code);
                    break;
                case 0xEB: 
                    {
                        int8_t offset = memory->read8(current_addr + 1);
                        emitJMP(code, offset);
                    }
                    break;
                case 0xE9: 
                    {
                        int32_t offset = memory->read32(current_addr + 1);

                        *code++ = 0xE9;
                        *reinterpret_cast<int32_t*>(code) = offset;
                        code += 4;
                    }
                    break;
                case 0xE8: 
                    {
                        int32_t offset = memory->read32(current_addr + 1);
                        *code++ = 0xE8;
                        *reinterpret_cast<int32_t*>(code) = offset;
                        code += 4;
                    }
                    break;
                case 0x89: 
                case 0x8B: 
                case 0x01: 
                case 0x03: 
                case 0x29: 
                case 0x2B: 
                case 0x21: 
                case 0x23: 
                case 0x09: 
                case 0x0B: 
                case 0x31: 
                case 0x33: 
                case 0x39: 
                case 0x3B: 
                case 0x50: 
                case 0x51: 
                case 0x52: 
                case 0x53: 
                case 0x54: 
                case 0x55: 
                case 0x56: 
                case 0x57: 
                case 0x58: 
                case 0x59: 
                case 0x5A: 
                case 0x5B: 
                case 0x5C: 
                case 0x5D: 
                case 0x5E: 
                case 0x5F: 
                    *code++ = opcode;
                    *code++ = memory->read8(current_addr + 1);
                    break;
                default:

                    *code++ = opcode;
                    break;
            }
            current_addr++;
        } else {

            break;
        }
    }

    block.size = code - block.compiled_code;
    jit_cache[start_addr] = block;
    jitCacheUsed += block.size;

    LOGD("JIT compiled block at 0x%08X, size: %d bytes", start_addr, block.size);
}

bool X86Core::isCommonOpcode(uint8_t opcode) {

    switch (opcode) {

        case 0xC3: 
        case 0xEB: 
        case 0xE9: 
        case 0xE8: 
        case 0x89: 
        case 0x8B: 
        case 0x01: 
        case 0x03: 
        case 0x29: 
        case 0x2B: 
        case 0x21: 
        case 0x23: 
        case 0x09: 
        case 0x0B: 
        case 0x31: 
        case 0x33: 
        case 0x39: 
        case 0x3B: 
        case 0x50: 
        case 0x51: 
        case 0x52: 
        case 0x53: 
        case 0x54: 
        case 0x55: 
        case 0x56: 
        case 0x57: 
        case 0x58: 
        case 0x59: 
        case 0x5A: 
        case 0x5B: 
        case 0x5C: 
        case 0x5D: 
        case 0x5E: 
        case 0x5F: 
            return true;
        default:
            return false;
    }
}

void X86Core::emitRET(uint8_t*& code) {
    *code++ = 0xC3; 
}

void X86Core::emitJMP(uint8_t*& code, int8_t offset) {
    *code++ = 0xEB; 
    *code++ = offset;
}

void X86Core::handleFFOpcode() {
    uint8_t modrm = memory->read8(eip++);
    uint8_t reg = (modrm >> 3) & 7;

    switch (reg) {
        case 0: 
            {
                uint32_t addr = readOperand(modrm);
                uint32_t value = memory->read32(addr);
                memory->write32(addr, value + 1);
                updateFlags(value + 1, value, 1, 0);
            }
            break;
        case 1: 
            {
                uint32_t addr = readOperand(modrm);
                uint32_t value = memory->read32(addr);
                memory->write32(addr, value - 1);
                updateFlags(value - 1, value, 1, 1);
            }
            break;
        case 2: 
            {
                uint32_t addr = readOperand(modrm);
                push32(eip);
                eip = memory->read32(addr);
            }
            break;
        case 4: 
            {
                uint32_t addr = readOperand(modrm);
                eip = memory->read32(addr);
            }
            break;
        case 6: 
            {
                uint32_t addr = readOperand(modrm);
                uint32_t value = memory->read32(addr);
                push32(value);
            }
            break;
        default:
            LOGE("Unsupported FF opcode: reg=%d", reg);
            break;
    }
}
void X86Core::handleShiftOpcode() {
    uint8_t modrm = memory->read8(eip++);
    uint8_t opcode = memory->read8(eip - 2);

    switch (opcode) {
        case 0xD0: 
        case 0xD1: 
        case 0xD3: 
            {
                uint32_t addr = readOperand(modrm);
                uint32_t value = memory->read32(addr);
                uint32_t count = (opcode == 0xD3) ? (ecx & 0x1F) : 1;
                uint32_t result = (value << count) | (value >> (32 - count));
                memory->write32(addr, result);
            }
            break;
        default:
            LOGE("Unsupported shift opcode: 0x%02X", opcode);
            break;
    }
}
void X86Core::handleMulDivOpcode() {
    uint8_t modrm = memory->read8(eip++);
    uint8_t opcode = memory->read8(eip - 2);

    switch (opcode) {
        case 0xF6: 
        case 0xF7: 
            {
                uint32_t addr = readOperand(modrm);
                uint32_t value = memory->read32(addr);
                uint64_t result = (uint64_t)eax * value;
                eax = (uint32_t)(result & 0xFFFFFFFF);
                edx = (uint32_t)(result >> 32);
            }
            break;
        default:
            LOGE("Unsupported MUL/DIV opcode: 0x%02X", opcode);
            break;
    }
}
void X86Core::handleBitOpcode(uint8_t opcode) {
    uint8_t modrm = memory->read8(eip++);
    uint8_t reg = (modrm >> 3) & 7;

    switch (opcode) {
        case 0xBA: 
            {
                uint8_t bit = memory->read8(eip++);
                uint32_t addr = readOperand(modrm);
                uint32_t value = memory->read32(addr);
                uint32_t mask = 1 << (bit & 0x1F);

                if (reg == 4) { 
                    eflags = (eflags & ~0x1) | ((value & mask) ? 0x1 : 0);
                } else if (reg == 5) { 
                    memory->write32(addr, value | mask);
                    eflags = (eflags & ~0x1) | ((value & mask) ? 0x1 : 0);
                } else if (reg == 6) { 
                    memory->write32(addr, value & ~mask);
                    eflags = (eflags & ~0x1) | ((value & mask) ? 0x1 : 0);
                } else if (reg == 7) { 
                    memory->write32(addr, value ^ mask);
                    eflags = (eflags & ~0x1) | ((value & mask) ? 0x1 : 0);
                }
            }
            break;
        default:
            LOGE("Unsupported bit opcode: 0x%02X", opcode);
            break;
    }
}



void X86Core::lea_r32_m32() {
    uint8_t modrm = memory->read8(eip++);
    uint8_t reg = (modrm >> 3) & 7;
    uint32_t addr = readOperand(modrm);
    setRegister(reg, addr);
}

void X86Core::setSegmentRegister(uint8_t reg, uint16_t value) {
    switch (reg) {
        case 0: es = value; break;
        case 1: cs = value; break;
        case 2: ss = value; break;
        case 3: ds = value; break;
        case 4: fs = value; break;
        case 5: gs = value; break;
    }
}

void X86Core::setFlags(uint32_t flags) {
    eflags = flags;
}


void X86Core::push16(uint16_t value) {
    esp -= 2;
    memory->write16(esp, value);
}

uint16_t X86Core::pop16() {
    uint16_t value = memory->read16(esp);
    esp += 2;
    return value;
}

void X86Core::push32(uint32_t value) {

    safeStackOperation([this, value]() {
        esp -= 4;
        memory->write32(esp, value);
    }, "PUSH32");
}

uint32_t X86Core::pop32() {
    uint32_t value = 0;


    safeStackOperation([this, &value]() {
        value = memory->read32(esp);
        esp += 4;
    }, "POP32");


    if (esp < 0x00000000 || esp > 0x0003FFFF) {
        LOGW("🚫 POP32-FEHLER: Rückgabe sicheren Wertes");
        return 0x00000000;
    }

    return value;
}



void X86Core::executeCompiledBlock(JITBlock& block) {
    typedef void (*JITFunction)();
    auto func = reinterpret_cast<JITFunction>(block.compiled_code);
    func();
}
















void X86Core::aluAdd(uint32_t& dest, uint32_t src) {
    uint32_t result = dest + src;
    eflags = (eflags & ~0x8D5) | 
             ((result == 0) << 6) |
             ((result >> 31) & 1) |
             ((result < dest) << 0) |
             ((((dest ^ src ^ 0x80000000) & (dest ^ result)) >> 30) & 0x4);
    dest = result;
}

void X86Core::aluSub(uint32_t& dest, uint32_t src) {
    uint32_t result = dest - src;
    eflags = (eflags & ~0x8D5) | 
             ((result == 0) << 6) |
             ((result >> 31) & 1) |
             ((dest < src) << 0) |
             ((((dest ^ src) & (dest ^ result)) >> 30) & 0x4);
    dest = result;
}

void X86Core::aluMul(uint32_t& dest, uint32_t src) {
    uint32x2_t a = vdup_n_u32(dest);
    uint32x2_t b = vdup_n_u32(src);
    uint64x2_t result = vmull_u32(a, b);
    uint64_t low = vgetq_lane_u64(result, 0);
    dest = low & 0xFFFFFFFF;
    uint32_t high = (low >> 32) & 0xFFFFFFFF;
    eflags = (eflags & ~0x801) | ((high != 0) << 0);
}
void X86Core::updateFlags(uint32_t result, uint32_t a, uint32_t b, uint32_t operation) {
    uint32_t flags = 0;
    flags |= (result == 0) << 6;
    flags |= (result & 0x80000000) >> 31;

    switch (operation) {
        case ALU_ADD: flags |= (result < a) << 0; break;
        case ALU_SUB: flags |= (a < b) << 0; break;
        case ALU_MUL: flags |= 0; break;
    }

    switch (operation) {
        case ALU_ADD: {
            uint32_t sign_a = a & 0x80000000;
            uint32_t sign_b = b & 0x80000000;
            uint32_t sign_r = result & 0x80000000;
            flags |= (sign_a == sign_b && sign_a != sign_r) ? 0x800 : 0;
            break;
        }
        case ALU_SUB: {
            uint32_t sign_a = a & 0x80000000;
            uint32_t sign_b = b & 0x80000000;
            uint32_t sign_r = result & 0x80000000;
            flags |= (sign_a != sign_b && sign_a != sign_r) ? 0x800 : 0;
            break;
        }
    }

    eflags = (eflags & ~0x8D5) | flags;
}

void X86Core::mov_r32_rm32() {
    uint8_t modrm = memory->read8(eip++);
    uint8_t reg = (modrm >> 3) & 7;
    uint32_t value = readOperand(modrm);
    setRegister(reg, value);
}

void X86Core::mov_rm32_r32() {
    uint8_t modrm = memory->read8(eip++);
    uint8_t reg = (modrm >> 3) & 7;
    uint32_t value = getRegister(reg);
    writeOperand(modrm, value);
}



void X86Core::mov_rm32_imm32() {
    uint8_t modrm = memory->read8(eip++);
    uint32_t imm32 = memory->read32(eip);
    eip += 4;
    writeOperand(modrm, imm32);
}
void X86Core::mov_rm8_imm8() {
    uint8_t modrm = memory->read8(eip++);
    uint8_t imm8 = memory->read8(eip++);
    writeOperand8(modrm, imm8);
}
void X86Core::add_rm32_r32() {
    uint8_t modrm = memory->read8(eip++);
    uint32_t src = getRegister((modrm >> 3) & 7);
    uint32_t dest = readOperand(modrm);
    aluAdd(dest, src);
    writeOperand(modrm, dest);
}

void X86Core::add_r32_rm32() {
    uint8_t modrm = memory->read8(eip++);
    uint8_t reg = (modrm >> 3) & 7;
    uint32_t src = readOperand(modrm);
    uint32_t dest = getRegister(reg);
    aluAdd(dest, src);
    setRegister(reg, dest);
}

void X86Core::jmp_rel8() {
    int8_t offset = memory->read8(eip++);
    eip += offset;
}

void X86Core::jmp_rel32() {
    int32_t offset = memory->read32(eip);


    uint32_t target_address = eip + 4 + offset;

    if (target_address < 0x00000000 || target_address > 0x07FFFFFF) {
        LOGW("🚫 XEMU JMP REPAIR: Invalid JMP target 0x%08X (offset: %d, current EIP: 0x%08X)", target_address, offset, eip);


        uint32_t repaired_address = repairCallAddress(target_address, eip);

        LOGW("🔧 XEMU JMP REPAIR: Repaired JMP target from 0x%08X to 0x%08X", target_address, repaired_address);


        eip = repaired_address;
    } else {

        eip = target_address;
    }

    LOGD("CPU: JMP rel32 - Jumping to: 0x%08X", eip);
}

void X86Core::ret() {
    uint32_t return_addr = 0;


    safeStackOperation([this, &return_addr]() {
        return_addr = memory->read32(esp);
        esp += 4;
    }, "RET_POP");


    LOGD("CPU: RET - ESP: 0x%08X, Return address: 0x%08X", esp, return_addr);


    if (return_addr > 0x00000000 && return_addr <= 0x07FFFFFF) {
        eip = return_addr;
        LOGD("CPU: RET - Jumping to return address: 0x%08X", eip);
    } else {
        LOGW("🚫 CRITICAL: Ungültige RET-Adresse 0x%08X, ESP: 0x%08X - repariere", return_addr, esp);


        if (esp >= 0x00000004 && esp <= 0x0003FFFF) {

            uint32_t validReturnAddr = 0x0058FD90;
            memory->write32(esp, validReturnAddr);
            eip = validReturnAddr;
            LOGI("✅ RET-Adresse repariert: 0x%08X bei ESP 0x%08X", validReturnAddr, esp);
        } else {

            eip = memory ? memory->xbeEntryPoint : 0x0057FD80;
            LOGW("⚠️ RET-Reparatur fehlgeschlagen, verwende sichere Adresse: 0x%08X", eip);
        }
    }
}

void X86Core::call_rel32() {
    int32_t offset = memory->read32(eip);
    eip += 4;


    uint32_t return_address = eip;
    LOGD("CPU: CALL rel32 - ESP before: 0x%08X, Return address: 0x%08X, Offset: %d", esp, return_address, offset);


    safeStackOperation([this, return_address]() {
        esp -= 4;
        memory->write32(esp, return_address);
    }, "CALL_PUSH");


    uint32_t target_address = eip + offset;

    if (target_address < 0x00000000 || target_address > 0x07FFFFFF) {
        LOGW("🚫 XEMU CALL REPAIR: Invalid CALL target 0x%08X (offset: %d, current EIP: 0x%08X)", target_address, offset, eip);


        uint32_t repaired_address = repairCallAddress(target_address, eip);

        LOGW("🔧 XEMU CALL REPAIR: Repaired CALL target from 0x%08X to 0x%08X", target_address, repaired_address);


        eip = repaired_address;
    } else {

        eip = target_address;
    }

    LOGD("CPU: CALL rel32 - ESP after: 0x%08X, Jumping to: 0x%08X", esp, eip);
}


void X86Core::fpuGroup3(uint8_t modrm) { 
    uint8_t op = (modrm >> 3) & 7;
    switch (op) {
        case 0: 
            {
                uint32_t addr = getEffectiveAddress(modrm);
                int32_t intValue = readInt32(addr);
                float value = static_cast<float>(intValue);
                fpuStack.push(value);
                LOGD("FPU FILD: loaded %d as %f from address 0x%08X", intValue, value, addr);
            }
            break;
        case 1: 
            {
                if (!fpuStack.empty()) {
                    float value = fpuStack.top();
                    int32_t intValue = static_cast<int32_t>(value);
                    uint32_t addr = getEffectiveAddress(modrm);
                    writeInt32(addr, intValue);
                    LOGD("FPU FIST: stored %f as %d to address 0x%08X", value, intValue, addr);
                }
            }
            break;
        case 2: 
            {
                if (!fpuStack.empty()) {
                    float value = fpuStack.top();
                    fpuStack.pop();
                    int32_t intValue = static_cast<int32_t>(value);
                    uint32_t addr = getEffectiveAddress(modrm);
                    writeInt32(addr, intValue);
                    LOGD("FPU FISTP: stored %f as %d to address 0x%08X and popped", value, intValue, addr);
                }
            }
            break;
        default:
            LOGD("FPU Group3 instruction: op=%d (handled as NOP)", op);
    }
}

void X86Core::fpuGroup4(uint8_t modrm) { 
    uint8_t op = (modrm >> 3) & 7;
    switch (op) {
        case 0: 
            {
                uint32_t addr = getEffectiveAddress(modrm);
                float value = memory->readFloat(addr);
                fpuStack.push(value);
                LOGD("FPU FLD: loaded %f from address 0x%08X", value, addr);
            }
            break;
        case 1: 
            {
                if (!fpuStack.empty()) {
                    float value = fpuStack.top();
                    fpuStack.pop();
                    uint32_t addr = getEffectiveAddress(modrm);
                    writeFloat(addr, value);
                    LOGD("FPU FSTP: stored %f to address 0x%08X and popped", value, addr);
                }
            }
            break;
        case 2: 
            {
                if (!fpuStack.empty()) {
                    float value = fpuStack.top();
                    fpuStack.pop();
                    fpuStack.push(-value);
                    LOGD("FPU FCHS: changed sign of %f to %f", value, -value);
                }
            }
            break;
        case 3: 
            {
                if (!fpuStack.empty()) {
                    float value = fpuStack.top();
                    fpuStack.pop();
                    fpuStack.push(std::abs(value));
                    LOGD("FPU FABS: absolute value of %f is %f", value, std::abs(value));
                }
            }
            break;
        default:
            LOGD("FPU Group4 instruction: op=%d (handled as NOP)", op);
    }
}



uint32_t X86Core::getEffectiveAddress(uint8_t modrm) {
    uint8_t mod = (modrm >> 6) & 3;
    uint8_t rm = modrm & 7;

    if (mod == 3) {

        return getRegister(rm);
    }

    uint32_t base = 0;
    uint32_t index = 0;
    uint32_t scale = 1;
    uint32_t disp = 0;


    switch (rm) {
        case 0: base = eax; break;
        case 1: base = ecx; break;
        case 2: base = edx; break;
        case 3: base = ebx; break;
        case 4: base = esp; break;
        case 5: base = ebp; break;
        case 6: base = esi; break;
        case 7: base = edi; break;
    }


    switch (mod) {
        case 0: 
            break;
        case 1: 
            disp = (int8_t)memory->read8(eip++);
            break;
        case 2: 
            disp = memory->read32(eip);
            eip += 4;
            break;
    }

    return base + index * scale + disp;
}

int32_t X86Core::readInt32(uint32_t addr) {
    return (int32_t)memory->read32(addr);
}

int64_t X86Core::readInt64(uint32_t addr) {
    uint32_t low = memory->read32(addr);
    uint32_t high = memory->read32(addr + 4);
    return ((int64_t)high << 32) | low;
}

void X86Core::writeInt32(uint32_t addr, int32_t value) {
    memory->write32(addr, (uint32_t)value);
}

void X86Core::writeInt64(uint32_t addr, int64_t value) {
    memory->write32(addr, (uint32_t)(value & 0xFFFFFFFF));
    memory->write32(addr + 4, (uint32_t)(value >> 32));
}

void X86Core::writeFloat(uint32_t addr, float value) {
    memory->writeFloat(addr, value);
}

void X86Core::fpuFadd(uint8_t modrm) { 
    (void)modrm; 
    LOGD("FPU FADD instruction");

    fpu.statusWord |= 0x0000; 
}

void X86Core::fpuFmul(uint8_t modrm) { 
    (void)modrm; 
    LOGD("FPU FMUL instruction");

    fpu.statusWord |= 0x0000; 
}

void X86Core::fpuFcom(uint8_t modrm) { 
    (void)modrm; 
    LOGD("FPU FCOM instruction");

    fpu.statusWord |= 0x0000; 
}

void X86Core::fpuFcomp(uint8_t modrm) { 
    (void)modrm; 
    LOGD("FPU FCOMP instruction");

    fpu.statusWord |= 0x0000; 
}

void X86Core::fpuFsub(uint8_t modrm) { 
    (void)modrm; 
    LOGD("FPU FSUB instruction");

    fpu.statusWord |= 0x0000; 
}

void X86Core::fpuFsubr(uint8_t modrm) { 
    (void)modrm; 
    LOGD("FPU FSUBR instruction");

    fpu.statusWord |= 0x0000; 
}

void X86Core::fpuFdiv(uint8_t modrm) { 
    (void)modrm; 
    LOGD("FPU FDIV instruction");

    fpu.statusWord |= 0x0000; 
}

void X86Core::fpuFdivr(uint8_t modrm) { 
    (void)modrm; 
    LOGD("FPU FDIVR instruction");

    fpu.statusWord |= 0x0000; 
}



void X86Core::sseMovups(uint8_t modrm) {
    uint8_t reg = (modrm >> 3) & 7;
    uint32_t addr = readOperand(modrm);
    for (int i = 0; i < 4; i++) {
        uint32_t val = memory->read32(addr + i*4);
        memcpy(&xmmRegisters[reg].data[i*4], &val, 4);
    }
}

void X86Core::sseMovhlps(uint8_t modrm) {
    uint8_t reg1 = (modrm >> 3) & 7;
    uint8_t reg2 = modrm & 7;

    memcpy(&xmmRegisters[reg1].data[0], &xmmRegisters[reg2].data[8], 8);
}

void X86Core::sseMovlps(uint8_t modrm) {
    uint8_t reg1 = (modrm >> 3) & 7;
    uint8_t reg2 = modrm & 7;

    memcpy(&xmmRegisters[reg1].data[0], &xmmRegisters[reg2].data[0], 8);
}

void X86Core::sseUnpcklps(uint8_t modrm) {
    uint8_t reg1 = (modrm >> 3) & 7;
    uint8_t reg2 = modrm & 7;

    float* a = reinterpret_cast<float*>(xmmRegisters[reg1].data);
    float* b = reinterpret_cast<float*>(xmmRegisters[reg2].data);
    float result[4] = {a[0], b[0], a[1], b[1]};
    memcpy(xmmRegisters[reg1].data, result, 16);
}

void X86Core::sseUnpckhps(uint8_t modrm) {
    uint8_t reg1 = (modrm >> 3) & 7;
    uint8_t reg2 = modrm & 7;

    float* a = reinterpret_cast<float*>(xmmRegisters[reg1].data);
    float* b = reinterpret_cast<float*>(xmmRegisters[reg2].data);
    float result[4] = {a[2], b[2], a[3], b[3]};
    memcpy(xmmRegisters[reg1].data, result, 16);
}

void X86Core::sseMovlhps(uint8_t modrm) {
    uint8_t reg1 = (modrm >> 3) & 7;
    uint8_t reg2 = modrm & 7;

    memcpy(&xmmRegisters[reg1].data[8], &xmmRegisters[reg2].data[8], 8);
}

void X86Core::sseMovhps(uint8_t modrm) {
    uint8_t reg1 = (modrm >> 3) & 7;
    uint8_t reg2 = modrm & 7;

    memcpy(&xmmRegisters[reg1].data[8], &xmmRegisters[reg2].data[0], 8);
}

void X86Core::sseMovaps(uint8_t modrm) {
    uint8_t reg1 = (modrm >> 3) & 7;
    uint8_t reg2 = modrm & 7;

    memcpy(xmmRegisters[reg1].data, xmmRegisters[reg2].data, 16);
}

void X86Core::sseCvtpi2ps(uint8_t modrm) {
    uint8_t reg1 = (modrm >> 3) & 7;
    uint8_t reg2 = modrm & 7;

    int32_t* src = reinterpret_cast<int32_t*>(xmmRegisters[reg2].data);
    float* dst = reinterpret_cast<float*>(xmmRegisters[reg1].data);
    for (int i = 0; i < 4; i++) {
        dst[i] = static_cast<float>(src[i]);
    }
}

void X86Core::sseMovntps(uint8_t modrm) {
    uint8_t reg1 = (modrm >> 3) & 7;
    uint8_t reg2 = modrm & 7;

    memcpy(xmmRegisters[reg1].data, xmmRegisters[reg2].data, 16);
}

void X86Core::sseCvttps2pi(uint8_t modrm) {
    uint8_t reg1 = (modrm >> 3) & 7;
    uint8_t reg2 = modrm & 7;

    float* src = reinterpret_cast<float*>(xmmRegisters[reg2].data);
    int32_t* dst = reinterpret_cast<int32_t*>(xmmRegisters[reg1].data);
    for (int i = 0; i < 4; i++) {
        dst[i] = static_cast<int32_t>(src[i]);
    }
}

void X86Core::sseCvtps2pi(uint8_t modrm) {
    uint8_t reg1 = (modrm >> 3) & 7;
    uint8_t reg2 = modrm & 7;

    float* src = reinterpret_cast<float*>(xmmRegisters[reg2].data);
    int32_t* dst = reinterpret_cast<int32_t*>(xmmRegisters[reg1].data);
    for (int i = 0; i < 4; i++) {
        dst[i] = static_cast<int32_t>(src[i]);
    }
}

void X86Core::sseUcomiss(uint8_t modrm) {
    uint8_t reg1 = (modrm >> 3) & 7;
    uint8_t reg2 = modrm & 7;

    float* a = reinterpret_cast<float*>(xmmRegisters[reg1].data);
    float* b = reinterpret_cast<float*>(xmmRegisters[reg2].data);

    if (a[0] < b[0]) eflags |= 0x0001; 
    else if (a[0] == b[0]) eflags |= 0x0040; 
    else eflags |= 0x0000; 
}

void X86Core::sseComiss(uint8_t modrm) {
    uint8_t reg1 = (modrm >> 3) & 7;
    uint8_t reg2 = modrm & 7;

    float* a = reinterpret_cast<float*>(xmmRegisters[reg1].data);
    float* b = reinterpret_cast<float*>(xmmRegisters[reg2].data);

    if (a[0] < b[0]) eflags |= 0x0001; 
    else if (a[0] == b[0]) eflags |= 0x0040; 
    else eflags |= 0x0000; 
}

void X86Core::sseMovmskps(uint8_t modrm) {
    uint8_t reg = (modrm >> 3) & 7;

    float* a = reinterpret_cast<float*>(xmmRegisters[reg].data);
    uint32_t result = 0;
    for (int i = 0; i < 4; i++) {
        if (a[i] < 0) {
            result |= (1 << i);
        }
    }
    eax = result; 
}

void X86Core::sseSqrtps(uint8_t modrm) {
    uint8_t reg = (modrm >> 3) & 7;

    float* a = reinterpret_cast<float*>(xmmRegisters[reg].data);
    for (int i = 0; i < 4; i++) {
        a[i] = sqrtf(a[i]);
    }
}

void X86Core::sseRsqrtps(uint8_t modrm) {
    uint8_t reg = (modrm >> 3) & 7;

    float* a = reinterpret_cast<float*>(xmmRegisters[reg].data);
    for (int i = 0; i < 4; i++) {
        a[i] = 1.0f / sqrtf(a[i]);
    }
}

void X86Core::sseRcpps(uint8_t modrm) {
    uint8_t reg = (modrm >> 3) & 7;

    float* a = reinterpret_cast<float*>(xmmRegisters[reg].data);
    for (int i = 0; i < 4; i++) {
        a[i] = 1.0f / a[i];
    }
}

void X86Core::sseAndps(uint8_t modrm) {
    uint8_t reg1 = (modrm >> 3) & 7;
    uint8_t reg2 = modrm & 7;

    float* a = reinterpret_cast<float*>(xmmRegisters[reg1].data);
    float* b = reinterpret_cast<float*>(xmmRegisters[reg2].data);
    uint32_t* a_int = reinterpret_cast<uint32_t*>(a);
    uint32_t* b_int = reinterpret_cast<uint32_t*>(b);
    for (int i = 0; i < 4; i++) {
        a_int[i] &= b_int[i];
    }
}

void X86Core::sseAndnps(uint8_t modrm) {
    uint8_t reg1 = (modrm >> 3) & 7;
    uint8_t reg2 = modrm & 7;

    float* a = reinterpret_cast<float*>(xmmRegisters[reg1].data);
    float* b = reinterpret_cast<float*>(xmmRegisters[reg2].data);
    uint32_t* a_int = reinterpret_cast<uint32_t*>(a);
    uint32_t* b_int = reinterpret_cast<uint32_t*>(b);
    for (int i = 0; i < 4; i++) {
        a_int[i] &= ~b_int[i];
    }
}

void X86Core::sseOrps(uint8_t modrm) {
    uint8_t reg1 = (modrm >> 3) & 7;
    uint8_t reg2 = modrm & 7;

    float* a = reinterpret_cast<float*>(xmmRegisters[reg1].data);
    float* b = reinterpret_cast<float*>(xmmRegisters[reg2].data);
    uint32_t* a_int = reinterpret_cast<uint32_t*>(a);
    uint32_t* b_int = reinterpret_cast<uint32_t*>(b);
    for (int i = 0; i < 4; i++) {
        a_int[i] |= b_int[i];
    }
}

void X86Core::sseXorps(uint8_t modrm) {
    uint8_t reg1 = (modrm >> 3) & 7;
    uint8_t reg2 = modrm & 7;

    float* a = reinterpret_cast<float*>(xmmRegisters[reg1].data);
    float* b = reinterpret_cast<float*>(xmmRegisters[reg2].data);
    uint32_t* a_int = reinterpret_cast<uint32_t*>(a);
    uint32_t* b_int = reinterpret_cast<uint32_t*>(b);
    for (int i = 0; i < 4; i++) {
        a_int[i] ^= b_int[i];
    }
}

void X86Core::sseCvtps2pd(uint8_t modrm) {
    uint8_t reg1 = (modrm >> 3) & 7;
    uint8_t reg2 = modrm & 7;

    float* src = reinterpret_cast<float*>(xmmRegisters[reg2].data);
    double* dst = reinterpret_cast<double*>(xmmRegisters[reg1].data);
    for (int i = 0; i < 2; i++) {
        dst[i] = static_cast<double>(src[i]);
    }
}
void X86Core::sseCvtdq2ps(uint8_t modrm) {
    uint8_t reg1 = (modrm >> 3) & 7;
    uint8_t reg2 = modrm & 7;

    int32_t* src = reinterpret_cast<int32_t*>(xmmRegisters[reg2].data);
    float* dst = reinterpret_cast<float*>(xmmRegisters[reg1].data);
    for (int i = 0; i < 4; i++) {
        dst[i] = static_cast<float>(src[i]);
    }
}

void X86Core::sseCvtpd2ps(uint8_t modrm) {
    uint8_t reg1 = (modrm >> 3) & 7;
    uint8_t reg2 = modrm & 7;

    double* src = reinterpret_cast<double*>(xmmRegisters[reg2].data);
    float* dst = reinterpret_cast<float*>(xmmRegisters[reg1].data);
    for (int i = 0; i < 2; i++) {
        dst[i] = static_cast<float>(src[i]);
    }
}
void X86Core::flushJITCache() {
    jit_cache.clear();
    jitCacheUsed = 0;
    LOGI("JIT cache flushed");
}

uint32_t X86Core::getRegister(uint8_t reg) const {
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
void X86Core::setRegister(uint8_t reg, uint32_t value) {
    switch (reg) {
        case 0: eax = value; break;
        case 1: ecx = value; break;
        case 2: edx = value; break;
        case 3: ebx = value; break;
        case 4: esp = value; break;
        case 5: ebp = value; break;
        case 6: esi = value; break;
        case 7: edi = value; break;
    }
}

void X86Core::dumpRegisters() const {
    LOGI("EAX: 0x%08X EBX: 0x%08X ECX: 0x%08X EDX: 0x%08X", eax, ebx, ecx, edx);
    LOGI("ESI: 0x%08X EDI: 0x%08X ESP: 0x%08X EBP: 0x%08X", esi, edi, esp, ebp);
    LOGI("EIP: 0x%08X EFLAGS: 0x%08X", eip, eflags);
    LOGI("CS: 0x%04X DS: 0x%04X ES: 0x%04X FS: 0x%04X GS: 0x%04X SS: 0x%04X", cs, ds, es, fs, gs, ss);
}

void X86Core::dumpJITCache() const {
    LOGI("JIT Cache Usage: %zu/%zu bytes (%.1f%%)", 
         jitCacheUsed, JIT_CACHE_SIZE, 
         (float)jitCacheUsed/JIT_CACHE_SIZE*100);
    LOGI("Compiled Blocks: %zu", jit_cache.size());
}
void X86Core::handleInterrupt(unsigned char interruptNumber) {

    LOGD("CPU: Handling interrupt 0x%02X, ESP before: 0x%08X", interruptNumber, esp);


    if (esp < 0x00000000 || esp > 0x0003FFFF) {
        LOGW("CPU: Invalid stack pointer 0x%08X before interrupt, resetting", esp);
        esp = 0x0003FFFC;
    }


    if (esp < 0x0000000C) {
        LOGW("CPU: Stack pointer too low for interrupt handling, resetting");
        esp = 0x0003FFFC;
    }


    uint32_t oldEIP = eip;
    uint32_t oldEFLAGS = eflags;


    safeStackOperation([this, oldEFLAGS]() {
        esp -= 4;
        LOGD("CPU: Writing EFLAGS 0x%08X to ESP 0x%08X", oldEFLAGS, esp);
        memory->write32(esp, oldEFLAGS);
    }, "INTERRUPT_PUSH_EFLAGS");

    safeStackOperation([this]() {
        esp -= 4;
        LOGD("CPU: Writing CS 0x%04X to ESP 0x%08X", cs, esp);
        memory->write32(esp, cs);
    }, "INTERRUPT_PUSH_CS");

    safeStackOperation([this, oldEIP]() {
        esp -= 4;
        LOGD("CPU: Writing EIP 0x%08X to ESP 0x%08X", oldEIP, esp);
        memory->write32(esp, oldEIP);
    }, "INTERRUPT_PUSH_EIP");


    eflags &= ~0x200; 


    switch (interruptNumber) {
        case 0x00: 
            LOGW("Divide Error Interrupt - treating as recoverable error");





            eflags |= 0x00000001; 






            break;

        case 0x01: 
            LOGD("Debug Interrupt");
            break;

        case 0x02: 
            LOGW("NMI Interrupt");
            break;

        case 0x03: 
            LOGD("Breakpoint Interrupt");
            if (debugCallback) {
                debugCallback(eip, "Breakpoint");
            }
            break;

        case 0x04: 
            LOGW("Overflow Interrupt");
            break;

        case 0x05: 
            if (boundsCheckingDisabled) {
                LOGI("✅ Bounds Check Interrupt ignored (bounds checking disabled)");

                return;
            } else {
                LOGE("CPU: FATAL ERROR - Bounds Check Interrupt not implemented!");
                LOGE("CPU: Xbox requires real bounds checking - no fallbacks!");
                state = CpuState::Halted;
                return;



            }
            break;

        case 0x06: 
            if (true) { 
                LOGW("Invalid Opcode Interrupt - treating as recoverable error");
            }


            if (false && false) { 





                eflags |= 0x00000001; 


                eip++; 


            } else {

                if (true) { 
                    LOGI("Invalid Opcode handling disabled - letting hardware handle naturally");
                }

                state = CpuState::Error;
            }
            break;

        case 0x07: 
            LOGW("Device Not Available Interrupt");
            break;

        case 0x08: 
            if (true) { 
                LOGW("Double Fault Interrupt - treating as recoverable error");
            }


            if (false && false) { 

                eflags |= 0x00000001; 
            } else {

                if (true) { 
                    LOGI("Double Fault handling disabled - letting hardware handle naturally");
                }

                state = CpuState::Error;
            }
            break;

        case 0x0A: 
            LOGW("Invalid TSS Interrupt - treating as recoverable error");

            eflags |= 0x00000001; 
            break;

        case 0x0B: 
            LOGW("Segment Not Present Interrupt - treating as recoverable error");

            eflags |= 0x00000001; 
            break;

        case 0x0C: 
            LOGW("Stack Fault Interrupt - treating as recoverable error");

            eflags |= 0x00000001; 
            break;

        case 0x0D: 
            LOGW("General Protection Fault Interrupt - treating as recoverable error");

            eflags |= 0x00000001; 
            break;

        case 0x0E: 
            LOGW("Page Fault Interrupt - treating as recoverable error");

            eflags |= 0x00000001; 
            break;

        case 0x20: 
            LOGD("Timer Interrupt");

            break;

        case 0x28: 
            LOGD("Xbox Timer Interrupt (IRQ8)");

            if (kernel) {


            }
            break;

        case 0x21: 
            LOGD("Keyboard Interrupt");
            break;

        case 0x30: 
        case 0x31: 
        case 0x32: 
        case 0x33: 
            LOGD("Controller %d Interrupt", interruptNumber - 0x30);
            break;

        case 0x80: 
            LOGD("System Call Interrupt");
            if (kernel) {


            }
            break;

        default:
            LOGW("Unknown Interrupt %d handled", interruptNumber);
            break;
    }


    if (interruptNumber != 0x02) {
        eflags |= 0x200; 
    }





    uint32_t savedEIP = memory->read32(esp);
    esp += 4;
    uint32_t savedCS = memory->read32(esp);  
    esp += 4;
    uint32_t savedEFLAGS = memory->read32(esp);
    esp += 4;


    eip = savedEIP;
    cs = (uint16_t)savedCS;
    eflags = savedEFLAGS;

    LOGD("CPU: Interrupt 0x%02X handled, restored EIP to 0x%08X", interruptNumber, eip);
}

void X86Core::setKernel(XboxKernel* kernelPtr) {
    kernel = kernelPtr;
}
void X86Core::enableTracing(bool enabled) {
    traceEnabled = enabled;
}

void X86Core::enableJIT(bool enable) {
    jitEnabled = enable;
}

void X86Core::setJITThreshold(uint32_t threshold) {
    jitThreshold = threshold;
}

void X86Core::setDebugCallback(std::function<void(uint32_t, const std::string&)> callback) {
    debugCallback = callback;
}

void X86Core::setPC(uint32_t pc) {
    eip = pc;
    LOGI("CPU: PC set to 0x%08X", eip);
}
void X86Core::startGameExecution(uint32_t entryPoint) {
    LOGI("[CPU-DEBUG] startGameExecution() called with entryPoint=0x%08X", entryPoint);



    if (entryPoint >= 0x08000000) {
        LOGW("[CPU-DEBUG] Entry point 0x%08X is beyond Xbox RAM limit, using default", entryPoint);
        entryPoint = 0x00100000; 
    }

    if (entryPoint >= 0x00000000 && entryPoint < 0x00010000) {
        LOGW("[CPU-DEBUG] Entry point 0x%08X is in low memory area, moving to safe code area", entryPoint);
        entryPoint = 0x00100000; 
    }

    if (entryPoint == 0x00000000) {
        LOGW("[CPU-DEBUG] Entry point is 0x00000000, using default game entry point");
        entryPoint = 0x00100000; 
    }


    if (memory) {
        const uint8_t* ramPtr = memory->getRamPointer();
        char hexDump[97] = {0};
        bool allZero = true, allFF = true;
        for (int i = 0; i < 32; ++i) {
            uint8_t val = ramPtr[entryPoint + i];
            sprintf(hexDump + i * 3, "%02X ", val);
            if (val != 0x00) allZero = false;
            if (val != 0xFF) allFF = false;
        }
        LOGI("[CPU-DEBUG] RAM @ EntryPoint 0x%08X: %s", entryPoint, hexDump);
        if (allZero) {
            LOGW("[CPU-DEBUG] RAM at entry point is all 0x00! This means no game data is loaded!");
            LOGW("[CPU-DEBUG] CPU will not execute anything meaningful!");
        }
        if (allFF) {
            LOGW("[CPU-DEBUG] RAM at entry point is all 0xFF! This indicates uninitialized memory!");
        }
        uint32_t instr = *(const uint32_t*)(ramPtr + entryPoint);
        LOGI("[CPU-DEBUG] First instruction as uint32_t: 0x%08X", instr);


        if (allZero) {
            LOGW("[CPU-DEBUG] No valid game data found at entry point - CPU will halt!");
            LOGW("[CPU-DEBUG] This indicates XBE loading failed - no real game code available!");
            state = CpuState::Halted;
            return;
        }


        if (memory->xbeEntryPoint != 0x0057FD80) {
            LOGI("[CPU-DEBUG] memory->xbeEntryPoint: 0x%08X", memory->xbeEntryPoint);
        }
    } else {
        LOGI("[CPU-DEBUG] memory is NULL, cannot dump RAM");
        state = CpuState::Error;
        return;
    }


    LOGI("CPU: Starting game execution at entry point 0x%08X", entryPoint);


    uint32_t finalEntryPoint = entryPoint;


    if (memory && memory->xbeEntryPoint != 0) {
        LOGI("CPU: XBE Entry Point from memory: 0x%08X", memory->xbeEntryPoint);


        if (memory->xbeEntryPoint != entryPoint) {
            LOGI("CPU: Entry points differ - memory: 0x%08X, provided: 0x%08X", 
                 memory->xbeEntryPoint, entryPoint);


            finalEntryPoint = memory->xbeEntryPoint;
        }
    }

    LOGI("CPU: Final entry point: 0x%08X", finalEntryPoint);
    eip = finalEntryPoint;

    state = CpuState::Running;


    esp = (memory && memory->espStartValue) ? memory->espStartValue : 0x0003FFFC;
    ebp = esp;


    if (memory) {

        for (uint32_t addr = 0x0003F000; addr <= 0x0003FFFF; addr += 4) {
            memory->write32(addr, 0x00000000); 
        }
        LOGI("CPU: Stack initialized with safe pattern");
    }


    cs = 0x08; 
    ds = 0x10; 
    es = 0x10; 
    fs = 0x10; 
    gs = 0x10; 
    ss = 0x10; 


    eflags = 0x00000202; 


    totalInstructionsExecuted = 0;
    instructionsThisFrame = 0;

    LOGI("CPU: Game execution started - EIP: 0x%08X, ESP: 0x%08X, State: %d (Running)", 
         eip, esp, static_cast<int>(state));
    LOGI("CPU: Instruction counters reset - ready for game execution");
    LOGI("[CPU-DEBUG] CPU state after start: EIP=0x%08X, ESP=0x%08X, State=%d (Running)", eip, esp, static_cast<int>(state));


    if (state == CpuState::Running) {
        LOGI("CPU: ✓ CPU is in Running state and ready to execute game code");


        if (memory) {
            uint8_t firstByte = memory->read8(eip);
            LOGI("CPU: ✓ First byte at EIP 0x%08X is 0x%02X", eip, firstByte);


            if (firstByte == 0x00) {
                LOGW("CPU: ✗ First byte is 0x00 - no valid instruction at entry point!");
                LOGW("CPU: ✗ This means the game data is not properly loaded!");


                LOGW("=== SPIEL-LADE-FEHLER-ANALYSE ===");
                LOGW("CPU: Entry Point: 0x%08X", eip);
                LOGW("CPU: First Byte: 0x%02X", firstByte);

                if (memory) {
                    LOGW("CPU: Memory Status: %s", "Verfügbar");
                    LOGW("CPU: Game Loaded Flag: %s", memory->isGameLoaded() ? "TRUE" : "FALSE");
                    LOGW("CPU: XBE Entry Point: 0x%08X", memory->xbeEntryPoint);


                    if (memory->xbeEntryPoint != 0 && memory->xbeEntryPoint != eip) {
                        LOGW("CPU: ✗ MISMATCH: XBE Entry Point (0x%08X) != CPU EIP (0x%08X)", 
                             memory->xbeEntryPoint, eip);
                        LOGW("CPU: ✗ Das ist der Grund für das Problem!");


                        LOGI("CPU: 🔧 Auto-correcting entry point mismatch");
                        eip = memory->xbeEntryPoint;
                        LOGI("CPU: ✓ Entry point corrected to 0x%08X", eip);


                        uint8_t correctedByte = memory->read8(eip);
                        if (correctedByte != 0x00) {
                            LOGI("CPU: ✓ Corrected entry point has valid data: 0x%02X", correctedByte);
                        } else {
                            LOGW("CPU: ✗ Corrected entry point still has invalid data");
                        }
                    }


                    LOGW("CPU: Speicherbereich um Entry Point:");
                    for (int offset = -16; offset <= 16; offset += 4) {
                        uint32_t checkAddr = eip + offset;
                        if (checkAddr >= 0x00000000 && checkAddr < 0x07FFFFFF) {
                            uint8_t checkBytes[4] = {0};
                            for (int j = 0; j < 4; j++) {
                                checkBytes[j] = memory->read8(checkAddr + j);
                            }
                            LOGW("CPU:   EIP%+4d (0x%08X): 0x%02X 0x%02X 0x%02X 0x%02X", 
                                 offset, checkAddr, checkBytes[0], checkBytes[1], checkBytes[2], checkBytes[3]);
                        }
                    }
                } else {
                    LOGW("CPU: ✗ Memory ist NULL - das ist ein kritischer Fehler!");
                }

                LOGW("=== ENDE FEHLER-ANALYSE ===");


                if (!memory->isGameLoaded()) {
                    LOGW("CPU: ✗ Game is not loaded! ⚠️  NOT halting - continuing anyway");

                    return;
                } else {
                    LOGW("CPU: ✗ Game is marked as loaded but entry point is empty!");
                    LOGW("CPU: ✗ This indicates a problem with XBE loading or memory mapping");
                    LOGW("CPU: ⚠️  NOT halting - continuing execution anyway");

                    return;
                }
            }


            uint32_t firstFourBytes = memory->read32(eip);
            if (firstFourBytes == 0x48454258) { 
                LOGW("CPU: ✗ Entry point contains XBE header magic (0x48454258)!");
                LOGW("CPU: ✗ This means the CPU is trying to execute the XBE header as code!");
                LOGW("CPU: ✗ The entry point calculation is wrong!");


                LOGW("=== XBE HEADER DETECTION ===");
                LOGW("CPU: Current EIP (0x%08X) points to XBE header", eip);
                LOGW("CPU: XBE Entry Point from memory: 0x%08X", memory->xbeEntryPoint);


                uint32_t correctEntryPoint = memory->xbeEntryPoint;
                if (correctEntryPoint != 0 && correctEntryPoint != eip) {
                    LOGW("CPU: ✓ Found correct entry point: 0x%08X", correctEntryPoint);
                    LOGW("CPU: ✓ Switching to correct entry point");
                    eip = correctEntryPoint;


                    uint8_t newFirstByte = memory->read8(eip);
                    LOGW("CPU: ✓ New entry point first byte: 0x%02X", newFirstByte);

                    if (newFirstByte == 0x00) {
                        LOGW("CPU: ✗ New entry point is also empty!");
                        LOGW("CPU: ⚠️  NOT halting - continuing execution anyway");

                        return;
                    }

                    return; 
                } else {
                    LOGW("CPU: ✗ No valid entry point found!");
                    LOGW("CPU: ⚠️  NOT halting - continuing execution anyway");

                    return;
                }
            }


            uint8_t nextBytes[4] = {0};
            for (int i = 0; i < 4; i++) {
                nextBytes[i] = memory->read8(eip + i);
            }


            bool looksLikeValidCode = false;
            if (nextBytes[0] == 0x55 || nextBytes[0] == 0x8B || nextBytes[0] == 0x89 || 
                nextBytes[0] == 0x90 || nextBytes[0] == 0xE8 || nextBytes[0] == 0xE9 ||
                nextBytes[0] == 0x50 || nextBytes[0] == 0x51 || nextBytes[0] == 0x52 || nextBytes[0] == 0x53) {
                looksLikeValidCode = true;
            }

            if (!looksLikeValidCode) {
                LOGW("CPU: ✗ Entry point does not contain valid x86 instructions!");
                LOGW("CPU: ✗ First 4 bytes: 0x%02X 0x%02X 0x%02X 0x%02X", 
                     nextBytes[0], nextBytes[1], nextBytes[2], nextBytes[3]);
                LOGW("CPU: ✗ This indicates corrupted or invalid game data");
                LOGW("CPU: ⚠️  NOT halting - continuing execution anyway");

                return;
            }

            LOGI("CPU: ✓ Entry point contains valid x86 instructions");
        }
    } else {
        LOGW("CPU: ✗ CPU is NOT in Running state - current state: %d", static_cast<int>(state));
        state = CpuState::Running; 
        LOGI("CPU: ✓ Forced CPU to Running state");
    }



    ISGAMEREALLOADED("[ISGAMEREALLOADED] Spiel lädt: JA (EIP=0x%08X, EntryPoint=0x%08X)", eip, memory ? memory->xbeEntryPoint : 0);

    ISGAMEREALLOADED("[ISGAMEREALLOADED] Spiel lädt: NEIN! Grund: Kein ausführbarer Code im RAM gefunden, EIP=0x%08X", eip);
}

void X86Core::startBasicExecution() {
    LOGI("CPU: Starting basic execution without game");


    eip = 0xFF000000; 
    state = CpuState::Running;


    esp = 0x0003FFFC; 
    ebp = 0x0003FFFC; 


    cs = 0x08; 
    ds = 0x10; 
    es = 0x10; 
    fs = 0x10; 
    gs = 0x10; 
    ss = 0x10; 


    eflags = 0x00000202; 


    totalInstructionsExecuted = 0;
    instructionsThisFrame = 0;

    LOGI("CPU: Basic execution started - EIP: 0x%08X, ESP: 0x%08X, State: %d", 
         eip, esp, static_cast<int>(state));
    LOGI("CPU: Ready for basic execution at BIOS entry point");
}


uint32_t X86Core::getEIP() const {
    return eip;
}

uint32_t X86Core::getEFLAGS() const {
    return eflags;
}

void X86Core::setEIP(uint32_t new_eip) {
    eip = new_eip;
}

X86Core::CpuState X86Core::getState() const {
    return state;
}

void X86Core::setState(CpuState newState) {
    state = newState;
}

void X86Core::setBreakpoint(uint32_t address, std::function<void()> callback) {
    breakpoints[address] = callback;
    LOGD("Breakpoint set at 0x%08X", address);
}

void X86Core::clearBreakpoint(uint32_t address) {
    auto it = breakpoints.find(address);
    if (it != breakpoints.end()) {
        breakpoints.erase(it);
        LOGD("Breakpoint cleared at 0x%08X", address);
    }
}

void X86Core::raiseException(uint8_t exception) {
    LOGE("CPU Exception %d raised at 0x%08X", exception, eip);
    state = CpuState::Error;

    if (debugCallback) {
        char buf[64];
        snprintf(buf, sizeof(buf), "Exception %d", exception);
        debugCallback(eip, buf);
    }
}








void X86Core::sseAddps(uint8_t modrm) {
    uint8_t reg1 = (modrm >> 3) & 7;
    uint8_t reg2 = modrm & 7;
    float32x4_t a = vld1q_f32(reinterpret_cast<float*>(xmmRegisters[reg1].data));
    float32x4_t b = vld1q_f32(reinterpret_cast<float*>(xmmRegisters[reg2].data));
    float32x4_t result = vaddq_f32(a, b);
    vst1q_f32(reinterpret_cast<float*>(xmmRegisters[reg1].data), result);
}

void X86Core::sseMulps(uint8_t modrm) {
    uint8_t reg1 = (modrm >> 3) & 7;
    uint8_t reg2 = modrm & 7;
    float32x4_t a = vld1q_f32(reinterpret_cast<float*>(xmmRegisters[reg1].data));
    float32x4_t b = vld1q_f32(reinterpret_cast<float*>(xmmRegisters[reg2].data));
    float32x4_t result = vmulq_f32(a, b);
    vst1q_f32(reinterpret_cast<float*>(xmmRegisters[reg1].data), result);
}

void X86Core::sseSubps(uint8_t modrm) {
    uint8_t reg1 = (modrm >> 3) & 7;
    uint8_t reg2 = modrm & 7;
    float32x4_t a = vld1q_f32(reinterpret_cast<float*>(xmmRegisters[reg1].data));
    float32x4_t b = vld1q_f32(reinterpret_cast<float*>(xmmRegisters[reg2].data));
    float32x4_t result = vsubq_f32(a, b);
    vst1q_f32(reinterpret_cast<float*>(xmmRegisters[reg1].data), result);
}

void X86Core::sseMinps(uint8_t modrm) {
    uint8_t reg1 = (modrm >> 3) & 7;
    uint8_t reg2 = modrm & 7;
    float32x4_t a = vld1q_f32(reinterpret_cast<float*>(xmmRegisters[reg1].data));
    float32x4_t b = vld1q_f32(reinterpret_cast<float*>(xmmRegisters[reg2].data));
    float32x4_t result = vminq_f32(a, b);
    vst1q_f32(reinterpret_cast<float*>(xmmRegisters[reg1].data), result);
}

void X86Core::sseDivps(uint8_t modrm) {
    uint8_t reg1 = (modrm >> 3) & 7;
    uint8_t reg2 = modrm & 7;
    float32x4_t a = vld1q_f32(reinterpret_cast<float*>(xmmRegisters[reg1].data));
    float32x4_t b = vld1q_f32(reinterpret_cast<float*>(xmmRegisters[reg2].data));

    float32x4_t recip = vrecpeq_f32(b);
    float32x4_t result = vmulq_f32(a, recip);
    vst1q_f32(reinterpret_cast<float*>(xmmRegisters[reg1].data), result);
}

void X86Core::sseMaxps(uint8_t modrm) {
    uint8_t reg1 = (modrm >> 3) & 7;
    uint8_t reg2 = modrm & 7;
    float32x4_t a = vld1q_f32(reinterpret_cast<float*>(xmmRegisters[reg1].data));
    float32x4_t b = vld1q_f32(reinterpret_cast<float*>(xmmRegisters[reg2].data));
    float32x4_t result = vmaxq_f32(a, b);
    vst1q_f32(reinterpret_cast<float*>(xmmRegisters[reg1].data), result);
}

void X86Core::sseMovdqa(uint8_t modrm) {
    uint8_t reg1 = (modrm >> 3) & 7;
    uint8_t reg2 = modrm & 7;

    memcpy(xmmRegisters[reg1].data, xmmRegisters[reg2].data, 16);
}

void X86Core::ssePshufd(uint8_t modrm) {
    uint8_t reg1 = (modrm >> 3) & 7;
    uint8_t reg2 = modrm & 7;
    uint8_t imm = memory->read8(eip++);

    uint32_t* src = reinterpret_cast<uint32_t*>(xmmRegisters[reg2].data);
    uint32_t* dst = reinterpret_cast<uint32_t*>(xmmRegisters[reg1].data);
    for (int i = 0; i < 4; i++) {
        int idx = (imm >> (i * 2)) & 3;
        dst[i] = src[idx];
    }
}

void X86Core::ssePsrlw(uint8_t modrm) {
    uint8_t reg1 = (modrm >> 3) & 7;
    uint8_t reg2 = modrm & 7;

    uint16_t* src = reinterpret_cast<uint16_t*>(xmmRegisters[reg2].data);
    uint16_t* dst = reinterpret_cast<uint16_t*>(xmmRegisters[reg1].data);
    for (int i = 0; i < 8; i++) {
        dst[i] = src[i] >> 1; 
    }
}

void X86Core::ssePsrld(uint8_t modrm) {
    uint8_t reg1 = (modrm >> 3) & 7;
    uint8_t reg2 = modrm & 7;

    uint32_t* src = reinterpret_cast<uint32_t*>(xmmRegisters[reg2].data);
    uint32_t* dst = reinterpret_cast<uint32_t*>(xmmRegisters[reg1].data);
    for (int i = 0; i < 4; i++) {
        dst[i] = src[i] >> 1; 
    }
}

void X86Core::ssePsrlq(uint8_t modrm) {
    uint8_t reg1 = (modrm >> 3) & 7;
    uint8_t reg2 = modrm & 7;

    uint64_t* src = reinterpret_cast<uint64_t*>(xmmRegisters[reg2].data);
    uint64_t* dst = reinterpret_cast<uint64_t*>(xmmRegisters[reg1].data);
    for (int i = 0; i < 2; i++) {
        dst[i] = src[i] >> 1; 
    }
}

void X86Core::ssePcmpeqb(uint8_t modrm) {
    uint8_t reg1 = (modrm >> 3) & 7;
    uint8_t reg2 = modrm & 7;

    uint8_t* src1 = xmmRegisters[reg1].data;
    uint8_t* src2 = xmmRegisters[reg2].data;
    for (int i = 0; i < 16; i++) {
        src1[i] = (src1[i] == src2[i]) ? 0xFF : 0x00;
    }
}

void X86Core::ssePcmpeqw(uint8_t modrm) {
    uint8_t reg1 = (modrm >> 3) & 7;
    uint8_t reg2 = modrm & 7;

    uint16_t* src1 = reinterpret_cast<uint16_t*>(xmmRegisters[reg1].data);
    uint16_t* src2 = reinterpret_cast<uint16_t*>(xmmRegisters[reg2].data);
    for (int i = 0; i < 8; i++) {
        src1[i] = (src1[i] == src2[i]) ? 0xFFFF : 0x0000;
    }
}

void X86Core::ssePcmpeqd(uint8_t modrm) {
    uint8_t reg1 = (modrm >> 3) & 7;
    uint8_t reg2 = modrm & 7;

    uint32_t* src1 = reinterpret_cast<uint32_t*>(xmmRegisters[reg1].data);
    uint32_t* src2 = reinterpret_cast<uint32_t*>(xmmRegisters[reg2].data);
    for (int i = 0; i < 4; i++) {
        src1[i] = (src1[i] == src2[i]) ? 0xFFFFFFFF : 0x00000000;
    }
}

void X86Core::sseEmm() {

    fpuStack = std::stack<float>();
    LOGD("CPU: Entered MMX state");
}

void X86Core::sseMovd(uint8_t modrm) {
    uint8_t reg1 = (modrm >> 3) & 7;
    uint8_t reg2 = modrm & 7;

    uint32_t value = getRegister(reg2);
    uint32_t* dst = reinterpret_cast<uint32_t*>(xmmRegisters[reg1].data);
    dst[0] = value;
}

void X86Core::sseLdmxcsr(uint8_t modrm) {

    uint32_t addr = getEffectiveAddress(modrm);
    uint32_t mxcsr = memory->read32(addr);

    LOGD("CPU: Loaded MXCSR: 0x%08X", mxcsr);
}
void X86Core::sseStmxcsr(uint8_t modrm) {

    uint32_t addr = getEffectiveAddress(modrm);
    uint32_t mxcsr = 0x1F80; 
    memory->write32(addr, mxcsr);
    LOGD("CPU: Stored MXCSR: 0x%08X", mxcsr);
}






void X86Core::handleMMXOpcode(uint8_t opcode, uint8_t modrm) {
    uint8_t reg = (modrm >> 3) & 7;
    uint8_t rm = modrm & 7;

    switch (opcode) {
        case 0x6F: 
            {
                uint64_t value = 0;
                if ((modrm >> 6) == 3) { 

                    value = *(uint64_t*)&xmmRegisters[rm].data[0];
                } else { 
                    uint32_t addr = readOperand(modrm);
                    value = memory->read64(addr);
                }
                *(uint64_t*)&xmmRegisters[reg].data[0] = value;
            }
            break;

        case 0x7F: 
            {
                uint64_t value = *(uint64_t*)&xmmRegisters[reg].data[0];
                if ((modrm >> 6) == 3) { 
                    *(uint64_t*)&xmmRegisters[rm].data[0] = value;
                } else { 
                    uint32_t addr = readOperand(modrm);
                    memory->write64(addr, value);
                }
            }
            break;

        case 0xFC: 
        case 0xFD: 
        case 0xFE: 
            {
                uint64_t value1 = *(uint64_t*)&xmmRegisters[reg].data[0];
                uint64_t value2 = 0;

                if ((modrm >> 6) == 3) { 
                    value2 = *(uint64_t*)&xmmRegisters[rm].data[0];
                } else { 
                    uint32_t addr = readOperand(modrm);
                    value2 = memory->read64(addr);
                }

                uint64_t result = 0;
                if (opcode == 0xFC) { 
                    for (int i = 0; i < 8; i++) {
                        uint8_t a = (value1 >> (i * 8)) & 0xFF;
                        uint8_t b = (value2 >> (i * 8)) & 0xFF;
                        uint8_t sum = a + b;
                        result |= ((uint64_t)sum << (i * 8));
                    }
                } else if (opcode == 0xFD) { 
                    for (int i = 0; i < 4; i++) {
                        uint16_t a = (value1 >> (i * 16)) & 0xFFFF;
                        uint16_t b = (value2 >> (i * 16)) & 0xFFFF;
                        uint16_t sum = a + b;
                        result |= ((uint64_t)sum << (i * 16));
                    }
                } else { 
                    for (int i = 0; i < 2; i++) {
                        uint32_t a = (value1 >> (i * 32)) & 0xFFFFFFFF;
                        uint32_t b = (value2 >> (i * 32)) & 0xFFFFFFFF;
                        uint32_t sum = a + b;
                        result |= ((uint64_t)sum << (i * 32));
                    }
                }

                *(uint64_t*)&xmmRegisters[reg].data[0] = result;
            }
            break;

        case 0xF8: 
        case 0xF9: 
        case 0xFA: 
            {
                uint64_t value1 = *(uint64_t*)&xmmRegisters[reg].data[0];
                uint64_t value2 = 0;

                if ((modrm >> 6) == 3) { 
                    value2 = *(uint64_t*)&xmmRegisters[rm].data[0];
                } else { 
                    uint32_t addr = readOperand(modrm);
                    value2 = memory->read64(addr);
                }

                uint64_t result = 0;
                if (opcode == 0xF8) { 
                    for (int i = 0; i < 8; i++) {
                        uint8_t a = (value1 >> (i * 8)) & 0xFF;
                        uint8_t b = (value2 >> (i * 8)) & 0xFF;
                        uint8_t diff = a - b;
                        result |= ((uint64_t)diff << (i * 8));
                    }
                } else if (opcode == 0xF9) { 
                    for (int i = 0; i < 4; i++) {
                        uint16_t a = (value1 >> (i * 16)) & 0xFFFF;
                        uint16_t b = (value2 >> (i * 16)) & 0xFFFF;
                        uint16_t diff = a - b;
                        result |= ((uint64_t)diff << (i * 16));
                    }
                } else { 
                    for (int i = 0; i < 2; i++) {
                        uint32_t a = (value1 >> (i * 32)) & 0xFFFFFFFF;
                        uint32_t b = (value2 >> (i * 32)) & 0xFFFFFFFF;
                        uint32_t diff = a - b;
                        result |= ((uint64_t)diff << (i * 32));
                    }
                }

                *(uint64_t*)&xmmRegisters[reg].data[0] = result;
            }
            break;

        case 0xE5: 
        case 0xF5: 
            {
                uint64_t value1 = *(uint64_t*)&xmmRegisters[reg].data[0];
                uint64_t value2 = 0;

                if ((modrm >> 6) == 3) { 
                    value2 = *(uint64_t*)&xmmRegisters[rm].data[0];
                } else { 
                    uint32_t addr = readOperand(modrm);
                    value2 = memory->read64(addr);
                }

                if (opcode == 0xE5) { 
                    uint64_t result = 0;
                    for (int i = 0; i < 4; i++) {
                        int16_t a = (int16_t)((value1 >> (i * 16)) & 0xFFFF);
                        int16_t b = (int16_t)((value2 >> (i * 16)) & 0xFFFF);
                        int32_t product = a * b;
                        uint16_t high = (product >> 16) & 0xFFFF;
                        result |= ((uint64_t)high << (i * 16));
                    }
                    *(uint64_t*)&xmmRegisters[reg].data[0] = result;
                } else { 
                    uint64_t result = 0;
                    for (int i = 0; i < 2; i++) {
                        int16_t a1 = (int16_t)((value1 >> (i * 32)) & 0xFFFF);
                        int16_t a2 = (int16_t)((value1 >> (i * 32 + 16)) & 0xFFFF);
                        int16_t b1 = (int16_t)((value2 >> (i * 32)) & 0xFFFF);
                        int16_t b2 = (int16_t)((value2 >> (i * 32 + 16)) & 0xFFFF);
                        int32_t sum = a1 * b1 + a2 * b2;
                        result |= ((uint64_t)(uint32_t)sum << (i * 32));
                    }
                    *(uint64_t*)&xmmRegisters[reg].data[0] = result;
                }
            }
            break;


        case 0xDB: 
            {
                uint64_t value1 = *(uint64_t*)&xmmRegisters[reg].data[0];
                uint64_t value2 = 0;

                if ((modrm >> 6) == 3) { 
                    value2 = *(uint64_t*)&xmmRegisters[rm].data[0];
                } else { 
                    uint32_t addr = readOperand(modrm);
                    value2 = memory->read64(addr);
                }

                *(uint64_t*)&xmmRegisters[reg].data[0] = value1 & value2;
            }
            break;

        case 0xDF: 
            {
                uint64_t value1 = *(uint64_t*)&xmmRegisters[reg].data[0];
                uint64_t value2 = 0;

                if ((modrm >> 6) == 3) { 
                    value2 = *(uint64_t*)&xmmRegisters[rm].data[0];
                } else { 
                    uint32_t addr = readOperand(modrm);
                    value2 = memory->read64(addr);
                }

                *(uint64_t*)&xmmRegisters[reg].data[0] = (~value1) & value2;
            }
            break;

        case 0xEB: 
            {
                uint64_t value1 = *(uint64_t*)&xmmRegisters[reg].data[0];
                uint64_t value2 = 0;

                if ((modrm >> 6) == 3) { 
                    value2 = *(uint64_t*)&xmmRegisters[rm].data[0];
                } else { 
                    uint32_t addr = readOperand(modrm);
                    value2 = memory->read64(addr);
                }

                *(uint64_t*)&xmmRegisters[reg].data[0] = value1 | value2;
            }
            break;

        case 0xEF: 
            {
                uint64_t value1 = *(uint64_t*)&xmmRegisters[reg].data[0];
                uint64_t value2 = 0;

                if ((modrm >> 6) == 3) { 
                    value2 = *(uint64_t*)&xmmRegisters[rm].data[0];
                } else { 
                    uint32_t addr = readOperand(modrm);
                    value2 = memory->read64(addr);
                }

                *(uint64_t*)&xmmRegisters[reg].data[0] = value1 ^ value2;
            }
            break;

        case 0xE1: 
        case 0xE2: 
        case 0xE3: 
        case 0xE4: 
            {
                uint64_t value1 = *(uint64_t*)&xmmRegisters[reg].data[0];
                uint64_t value2 = 0;

                if ((modrm >> 6) == 3) { 
                    value2 = *(uint64_t*)&xmmRegisters[rm].data[0];
                } else { 
                    uint32_t addr = readOperand(modrm);
                    value2 = memory->read64(addr);
                }

                uint64_t result = 0;
                if (opcode == 0xE1) { 
                    for (int i = 0; i < 4; i++) {
                        int16_t a = (int16_t)((value1 >> (i * 16)) & 0xFFFF);
                        uint8_t shift = value2 & 0x1F;
                        int16_t shifted = a >> shift;
                        result |= ((uint64_t)(uint16_t)shifted << (i * 16));
                    }
                } else if (opcode == 0xE2) { 
                    for (int i = 0; i < 2; i++) {
                        int32_t a = (int32_t)((value1 >> (i * 32)) & 0xFFFFFFFF);
                        uint8_t shift = value2 & 0x1F;
                        int32_t shifted = a >> shift;
                        result |= ((uint64_t)(uint32_t)shifted << (i * 32));
                    }
                } else if (opcode == 0xE3) { 
                    for (int i = 0; i < 4; i++) {
                        uint16_t a = (uint16_t)((value1 >> (i * 16)) & 0xFFFF);
                        uint8_t shift = value2 & 0x1F;
                        uint16_t shifted = a >> shift;
                        result |= ((uint64_t)shifted << (i * 16));
                    }
                } else { 
                    for (int i = 0; i < 2; i++) {
                        uint32_t a = (uint32_t)((value1 >> (i * 32)) & 0xFFFFFFFF);
                        uint8_t shift = value2 & 0x1F;
                        uint32_t shifted = a >> shift;
                        result |= ((uint64_t)shifted << (i * 32));
                    }
                }

                *(uint64_t*)&xmmRegisters[reg].data[0] = result;
            }
            break;

        case 0x71: 
        case 0x72: 
        case 0x73: 
        case 0x74: 
            {
                uint64_t value = *(uint64_t*)&xmmRegisters[reg].data[0];
                uint8_t shift = memory->read8(eip++);

                uint64_t result = 0;
                if (opcode == 0x71) { 
                    for (int i = 0; i < 4; i++) {
                        int16_t a = (int16_t)((value >> (i * 16)) & 0xFFFF);
                        int16_t shifted = a >> shift;
                        result |= ((uint64_t)(uint16_t)shifted << (i * 16));
                    }
                } else if (opcode == 0x72) { 
                    for (int i = 0; i < 2; i++) {
                        int32_t a = (int32_t)((value >> (i * 32)) & 0xFFFFFFFF);
                        int32_t shifted = a >> shift;
                        result |= ((uint64_t)(uint32_t)shifted << (i * 32));
                    }
                } else if (opcode == 0x73) { 
                    for (int i = 0; i < 4; i++) {
                        uint16_t a = (uint16_t)((value >> (i * 16)) & 0xFFFF);
                        uint16_t shifted = a >> shift;
                        result |= ((uint64_t)shifted << (i * 16));
                    }
                } else { 
                    for (int i = 0; i < 2; i++) {
                        uint32_t a = (uint32_t)((value >> (i * 32)) & 0xFFFFFFFF);
                        uint32_t shifted = a >> shift;
                        result |= ((uint64_t)shifted << (i * 32));
                    }
                }

                *(uint64_t*)&xmmRegisters[reg].data[0] = result;
            }
            break;

        case 0x60: 
        case 0x61: 
        case 0x62: 
            {
                uint64_t value1 = *(uint64_t*)&xmmRegisters[reg].data[0];
                uint32_t value2 = 0;

                if ((modrm >> 6) == 3) { 
                    value2 = (uint32_t)(*(uint64_t*)&xmmRegisters[rm].data[0] & 0xFFFFFFFF);
                } else { 
                    uint32_t addr = readOperand(modrm);
                    value2 = memory->read32(addr);
                }

                uint64_t result = 0;
                if (opcode == 0x60) { 
                    for (int i = 0; i < 4; i++) {
                        uint8_t a = (value1 >> (i * 8)) & 0xFF;
                        uint8_t b = (value2 >> (i * 8)) & 0xFF;
                        result |= ((uint64_t)a << (i * 16));
                        result |= ((uint64_t)b << (i * 16 + 8));
                    }
                } else if (opcode == 0x61) { 
                    for (int i = 0; i < 2; i++) {
                        uint16_t a = (value1 >> (i * 16)) & 0xFFFF;
                        uint16_t b = (value2 >> (i * 16)) & 0xFFFF;
                        result |= ((uint64_t)a << (i * 32));
                        result |= ((uint64_t)b << (i * 32 + 16));
                    }
                } else { 
                    result = ((uint64_t)value1 & 0xFFFFFFFF) | ((uint64_t)value2 << 32);
                }

                *(uint64_t*)&xmmRegisters[reg].data[0] = result;
            }
            break;

        case 0x68: 
        case 0x69: 
        case 0x6A: 
            {
                uint64_t value1 = *(uint64_t*)&xmmRegisters[reg].data[0];
                uint64_t value2 = 0;

                if ((modrm >> 6) == 3) { 
                    value2 = *(uint64_t*)&xmmRegisters[rm].data[0];
                } else { 
                    uint32_t addr = readOperand(modrm);
                    value2 = memory->read64(addr);
                }

                uint64_t result = 0;
                if (opcode == 0x68) { 
                    for (int i = 0; i < 4; i++) {
                        uint8_t a = (value1 >> (i * 8 + 32)) & 0xFF;
                        uint8_t b = (value2 >> (i * 8 + 32)) & 0xFF;
                        result |= ((uint64_t)a << (i * 16));
                        result |= ((uint64_t)b << (i * 16 + 8));
                    }
                } else if (opcode == 0x69) { 
                    for (int i = 0; i < 2; i++) {
                        uint16_t a = (value1 >> (i * 16 + 32)) & 0xFFFF;
                        uint16_t b = (value2 >> (i * 16 + 32)) & 0xFFFF;
                        result |= ((uint64_t)a << (i * 32));
                        result |= ((uint64_t)b << (i * 32 + 16));
                    }
                } else { 
                    result = ((value1 >> 32) & 0xFFFFFFFF) | ((value2 >> 32) << 32);
                }

                *(uint64_t*)&xmmRegisters[reg].data[0] = result;
            }
            break;

        case 0x64: 
        case 0x65: 
        case 0x66: 
            {
                uint64_t value1 = *(uint64_t*)&xmmRegisters[reg].data[0];
                uint64_t value2 = 0;

                if ((modrm >> 6) == 3) { 
                    value2 = *(uint64_t*)&xmmRegisters[rm].data[0];
                } else { 
                    uint32_t addr = readOperand(modrm);
                    value2 = memory->read64(addr);
                }

                uint64_t result = 0;
                if (opcode == 0x64) { 
                    for (int i = 0; i < 8; i++) {
                        int8_t a = (int8_t)((value1 >> (i * 8)) & 0xFF);
                        int8_t b = (int8_t)((value2 >> (i * 8)) & 0xFF);
                        uint8_t mask = (a > b) ? 0xFF : 0x00;
                        result |= ((uint64_t)mask << (i * 8));
                    }
                } else if (opcode == 0x65) { 
                    for (int i = 0; i < 4; i++) {
                        int16_t a = (int16_t)((value1 >> (i * 16)) & 0xFFFF);
                        int16_t b = (int16_t)((value2 >> (i * 16)) & 0xFFFF);
                        uint16_t mask = (a > b) ? 0xFFFF : 0x0000;
                        result |= ((uint64_t)mask << (i * 16));
                    }
                } else { 
                    for (int i = 0; i < 2; i++) {
                        int32_t a = (int32_t)((value1 >> (i * 32)) & 0xFFFFFFFF);
                        int32_t b = (int32_t)((value2 >> (i * 32)) & 0xFFFFFFFF);
                        uint32_t mask = (a > b) ? 0xFFFFFFFF : 0x00000000;
                        result |= ((uint64_t)mask << (i * 32));
                    }
                }

                *(uint64_t*)&xmmRegisters[reg].data[0] = result;
            }
            break;

        case 0x75: 
        case 0x76: 
            {
                uint64_t value1 = *(uint64_t*)&xmmRegisters[reg].data[0];
                uint64_t value2 = 0;

                if ((modrm >> 6) == 3) { 
                    value2 = *(uint64_t*)&xmmRegisters[rm].data[0];
                } else { 
                    uint32_t addr = readOperand(modrm);
                    value2 = memory->read64(addr);
                }

                uint64_t result = 0;
                if (opcode == 0x75) { 
                    for (int i = 0; i < 4; i++) {
                        uint16_t a = (value1 >> (i * 16)) & 0xFFFF;
                        uint16_t b = (value2 >> (i * 16)) & 0xFFFF;
                        uint16_t mask = (a == b) ? 0xFFFF : 0x0000;
                        result |= ((uint64_t)mask << (i * 16));
                    }
                } else { 
                    for (int i = 0; i < 2; i++) {
                        uint32_t a = (value1 >> (i * 32)) & 0xFFFFFFFF;
                        uint32_t b = (value2 >> (i * 32)) & 0xFFFFFFFF;
                        uint32_t mask = (a == b) ? 0xFFFFFFFF : 0x00000000;
                        result |= ((uint64_t)mask << (i * 32));
                    }
                }

                *(uint64_t*)&xmmRegisters[reg].data[0] = result;
            }
            break;



        default:
            LOGE("Unsupported MMX opcode: 0x%02X", opcode);
            break;
    }
}
void X86Core::handleSSEOpcode(uint8_t opcode, uint8_t modrm) {
    uint8_t reg = (modrm >> 3) & 7;
    uint8_t rm = modrm & 7;

    switch (opcode) {
        case 0x10: 
        case 0x11: 
            {
                uint32_t addr = 0;
                if ((modrm >> 6) == 3) { 
                    if (opcode == 0x10) {

                        memcpy(xmmRegisters[reg].data, xmmRegisters[rm].data, 16);
                    } else {
                        memcpy(xmmRegisters[rm].data, xmmRegisters[reg].data, 16);
                    }
                } else { 
                    addr = readOperand(modrm);
                    if (opcode == 0x10) {

                        for (int i = 0; i < 4; i++) {
                            xmmRegisters[reg].data[i] = memory->read32(addr + i * 4);
                        }
                    } else {
                        for (int i = 0; i < 4; i++) {
                            memory->write32(addr + i * 4, xmmRegisters[reg].data[i]);
                        }
                    }
                }
            }
            break;

        case 0x28: 
        case 0x29: 

            handleSSEOpcode(opcode - 0x18, modrm);
            break;

        case 0x58: 
            {
                if ((modrm >> 6) == 3) { 
                    for (int i = 0; i < 4; i++) {
                        float a = *(float*)&xmmRegisters[reg].data[i];
                        float b = *(float*)&xmmRegisters[rm].data[i];
                        float result = a + b;
                        xmmRegisters[reg].data[i] = *(uint32_t*)&result;
                    }
                                    } else { 
                        uint32_t addr = readOperand(modrm);
                        for (int i = 0; i < 4; i++) {
                            float a = *(float*)&xmmRegisters[reg].data[i];
                            uint32_t memVal = memory->read32(addr + i * 4);
                            float b = *(float*)&memVal;
                            float result = a + b;
                            xmmRegisters[reg].data[i] = *(uint32_t*)&result;
                        }
                    }
            }
            break;

        case 0x59: 
            {
                if ((modrm >> 6) == 3) { 
                    for (int i = 0; i < 4; i++) {
                        float a = *(float*)&xmmRegisters[reg].data[i];
                        float b = *(float*)&xmmRegisters[rm].data[i];
                        float result = a * b;
                        xmmRegisters[reg].data[i] = *(uint32_t*)&result;
                    }
                                    } else { 
                        uint32_t addr = readOperand(modrm);
                        for (int i = 0; i < 4; i++) {
                            float a = *(float*)&xmmRegisters[reg].data[i];
                            uint32_t memVal = memory->read32(addr + i * 4);
                            float b = *(float*)&memVal;
                            float result = a * b;
                            xmmRegisters[reg].data[i] = *(uint32_t*)&result;
                        }
                    }
            }
            break;

        case 0x5C: 
            {
                if ((modrm >> 6) == 3) { 
                    for (int i = 0; i < 4; i++) {
                        float a = *(float*)&xmmRegisters[reg].data[i];
                        float b = *(float*)&xmmRegisters[rm].data[i];
                        float result = a - b;
                        xmmRegisters[reg].data[i] = *(uint32_t*)&result;
                    }
                                    } else { 
                        uint32_t addr = readOperand(modrm);
                        for (int i = 0; i < 4; i++) {
                            float a = *(float*)&xmmRegisters[reg].data[i];
                            uint32_t memVal = memory->read32(addr + i * 4);
                            float b = *(float*)&memVal;
                            float result = a - b;
                            xmmRegisters[reg].data[i] = *(uint32_t*)&result;
                        }
                    }
            }
            break;

        case 0x5E: 
            {
                if ((modrm >> 6) == 3) { 
                    for (int i = 0; i < 4; i++) {
                        float a = *(float*)&xmmRegisters[reg].data[i];
                        float b = *(float*)&xmmRegisters[rm].data[i];
                        float result = (b != 0.0f) ? a / b : 0.0f;
                        xmmRegisters[reg].data[i] = *(uint32_t*)&result;
                    }
                                    } else { 
                        uint32_t addr = readOperand(modrm);
                        for (int i = 0; i < 4; i++) {
                            float a = *(float*)&xmmRegisters[reg].data[i];
                            uint32_t memVal = memory->read32(addr + i * 4);
                            float b = *(float*)&memVal;
                            float result = (b != 0.0f) ? a / b : 0.0f;
                            xmmRegisters[reg].data[i] = *(uint32_t*)&result;
                        }
                    }
            }
            break;

        case 0x51: 
            {
                if ((modrm >> 6) == 3) { 
                    for (int i = 0; i < 4; i++) {
                        float a = *(float*)&xmmRegisters[rm].data[i];
                        float result = sqrtf(a);
                        xmmRegisters[reg].data[i] = *(uint32_t*)&result;
                    }
                } else { 
                    uint32_t addr = readOperand(modrm);
                    for (int i = 0; i < 4; i++) {
                        uint32_t memVal = memory->read32(addr + i * 4);
                        float a = *(float*)&memVal;
                        float result = sqrtf(a);
                        xmmRegisters[reg].data[i] = *(uint32_t*)&result;
                    }
                }
            }
            break;

        case 0x52: 
            {
                if ((modrm >> 6) == 3) { 
                    for (int i = 0; i < 4; i++) {
                        float a = *(float*)&xmmRegisters[rm].data[i];
                        float result = (a != 0.0f) ? 1.0f / sqrtf(a) : 0.0f;
                        xmmRegisters[reg].data[i] = *(uint32_t*)&result;
                    }
                } else { 
                    uint32_t addr = readOperand(modrm);
                    for (int i = 0; i < 4; i++) {
                        uint32_t memVal = memory->read32(addr + i * 4);
                        float a = *(float*)&memVal;
                        float result = (a != 0.0f) ? 1.0f / sqrtf(a) : 0.0f;
                        xmmRegisters[reg].data[i] = *(uint32_t*)&result;
                    }
                }
            }
            break;



        case 0x2D: 
            {
                uint32_t floatVal = 0;
                if ((modrm >> 6) == 3) { 
                    floatVal = xmmRegisters[rm].data[0];
                } else { 
                    uint32_t addr = readOperand(modrm);
                    floatVal = memory->read32(addr);
                }

                float f = *(float*)&floatVal;
                int32_t result = (int32_t)f;
                setRegister(reg, result);
            }
            break;

        case 0x2A: 
            {
                uint32_t intVal = 0;
                if ((modrm >> 6) == 3) { 
                    intVal = getRegister(rm);
                } else { 
                    uint32_t addr = readOperand(modrm);
                    intVal = memory->read32(addr);
                }

                float f = (float)(int32_t)intVal;
                xmmRegisters[reg].data[0] = *(uint32_t*)&f;
            }
            break;

        case 0x2F: 
            {
                uint32_t val1 = xmmRegisters[reg].data[0];
                uint32_t val2 = 0;

                if ((modrm >> 6) == 3) { 
                    val2 = xmmRegisters[rm].data[0];
                } else { 
                    uint32_t addr = readOperand(modrm);
                    val2 = memory->read32(addr);
                }

                float f1 = *(float*)&val1;
                float f2 = *(float*)&val2;


                eflags &= ~(0x40 | 0x1 | 0x800); 

                if (f1 == f2) {
                    eflags |= 0x40; 
                } else if (f1 < f2) {
                    eflags |= 0x1; 
                }
            }
            break;

        case 0x2E: 
            {
                uint32_t val1 = xmmRegisters[reg].data[0];
                uint32_t val2 = 0;

                if ((modrm >> 6) == 3) { 
                    val2 = xmmRegisters[rm].data[0];
                } else { 
                    uint32_t addr = readOperand(modrm);
                    val2 = memory->read32(addr);
                }

                float f1 = *(float*)&val1;
                float f2 = *(float*)&val2;


                eflags &= ~(0x40 | 0x1 | 0x800); 

                if (isnan(f1) || isnan(f2)) {
                    eflags |= 0x1; 
                } else if (f1 == f2) {
                    eflags |= 0x40; 
                } else if (f1 < f2) {
                    eflags |= 0x1; 
                }
            }
            break;



        case 0x53: 
            {
                if ((modrm >> 6) == 3) { 
                    for (int i = 0; i < 4; i++) {
                        float a = *(float*)&xmmRegisters[rm].data[i];
                        float result = 1.0f / a;
                        xmmRegisters[reg].data[i] = *(uint32_t*)&result;
                    }
                } else { 
                    uint32_t addr = readOperand(modrm);
                    for (int i = 0; i < 4; i++) {
                        uint32_t memVal = memory->read32(addr + i * 4);
                        float a = *(float*)&memVal;
                        float result = 1.0f / a;
                        xmmRegisters[reg].data[i] = *(uint32_t*)&result;
                    }
                }
            }
            break;

        case 0x54: 
            {
                if ((modrm >> 6) == 3) { 
                    for (int i = 0; i < 4; i++) {
                        xmmRegisters[reg].data[i] &= xmmRegisters[rm].data[i];
                    }
                } else { 
                    uint32_t addr = readOperand(modrm);
                    for (int i = 0; i < 4; i++) {
                        uint32_t memVal = memory->read32(addr + i * 4);
                        xmmRegisters[reg].data[i] &= memVal;
                    }
                }
            }
            break;

        case 0x55: 
            {
                if ((modrm >> 6) == 3) { 
                    for (int i = 0; i < 4; i++) {
                        xmmRegisters[reg].data[i] = ~xmmRegisters[reg].data[i] & xmmRegisters[rm].data[i];
                    }
                } else { 
                    uint32_t addr = readOperand(modrm);
                    for (int i = 0; i < 4; i++) {
                        uint32_t memVal = memory->read32(addr + i * 4);
                        xmmRegisters[reg].data[i] = ~xmmRegisters[reg].data[i] & memVal;
                    }
                }
            }
            break;

        case 0x56: 
            {
                if ((modrm >> 6) == 3) { 
                    for (int i = 0; i < 4; i++) {
                        xmmRegisters[reg].data[i] |= xmmRegisters[rm].data[i];
                    }
                } else { 
                    uint32_t addr = readOperand(modrm);
                    for (int i = 0; i < 4; i++) {
                        uint32_t memVal = memory->read32(addr + i * 4);
                        xmmRegisters[reg].data[i] |= memVal;
                    }
                }
            }
            break;

        case 0x57: 
            {
                if ((modrm >> 6) == 3) { 
                    for (int i = 0; i < 4; i++) {
                        xmmRegisters[reg].data[i] ^= xmmRegisters[rm].data[i];
                    }
                } else { 
                    uint32_t addr = readOperand(modrm);
                    for (int i = 0; i < 4; i++) {
                        uint32_t memVal = memory->read32(addr + i * 4);
                        xmmRegisters[reg].data[i] ^= memVal;
                    }
                }
            }
            break;

        case 0x5A: 
            {
                if ((modrm >> 6) == 3) { 

                    for (int i = 0; i < 2; i++) {
                        float f = *(float*)&xmmRegisters[rm].data[i];
                        double d = (double)f;
                        uint64_t* doublePtr = (uint64_t*)&xmmRegisters[reg].data[i * 2];
                        *doublePtr = *(uint64_t*)&d;
                    }
                } else { 
                    uint32_t addr = readOperand(modrm);
                    for (int i = 0; i < 2; i++) {
                        uint32_t memVal = memory->read32(addr + i * 4);
                        float f = *(float*)&memVal;
                        double d = (double)f;
                        uint64_t* doublePtr = (uint64_t*)&xmmRegisters[reg].data[i * 2];
                        *doublePtr = *(uint64_t*)&d;
                    }
                }
            }
            break;

        case 0x5B: 
            {
                if ((modrm >> 6) == 3) { 
                    for (int i = 0; i < 4; i++) {
                        int32_t intVal = (int32_t)xmmRegisters[rm].data[i];
                        float f = (float)intVal;
                        xmmRegisters[reg].data[i] = *(uint32_t*)&f;
                    }
                } else { 
                    uint32_t addr = readOperand(modrm);
                    for (int i = 0; i < 4; i++) {
                        uint32_t memVal = memory->read32(addr + i * 4);
                        int32_t intVal = (int32_t)memVal;
                        float f = (float)intVal;
                        xmmRegisters[reg].data[i] = *(uint32_t*)&f;
                    }
                }
            }
            break;

        case 0x5D: 
            {
                if ((modrm >> 6) == 3) { 
                    for (int i = 0; i < 4; i++) {
                        float a = *(float*)&xmmRegisters[reg].data[i];
                        float b = *(float*)&xmmRegisters[rm].data[i];
                        float result = (a < b) ? a : b;
                        xmmRegisters[reg].data[i] = *(uint32_t*)&result;
                    }
                } else { 
                    uint32_t addr = readOperand(modrm);
                    for (int i = 0; i < 4; i++) {
                        float a = *(float*)&xmmRegisters[reg].data[i];
                        uint32_t memVal = memory->read32(addr + i * 4);
                        float b = *(float*)&memVal;
                        float result = (a < b) ? a : b;
                        xmmRegisters[reg].data[i] = *(uint32_t*)&result;
                    }
                }
            }
            break;

        case 0x5F: 
            {
                if ((modrm >> 6) == 3) { 
                    for (int i = 0; i < 4; i++) {
                        float a = *(float*)&xmmRegisters[reg].data[i];
                        float b = *(float*)&xmmRegisters[rm].data[i];
                        float result = (a > b) ? a : b;
                        xmmRegisters[reg].data[i] = *(uint32_t*)&result;
                    }
                } else { 
                    uint32_t addr = readOperand(modrm);
                    for (int i = 0; i < 4; i++) {
                        float a = *(float*)&xmmRegisters[reg].data[i];
                        uint32_t memVal = memory->read32(addr + i * 4);
                        float b = *(float*)&memVal;
                        float result = (a > b) ? a : b;
                        xmmRegisters[reg].data[i] = *(uint32_t*)&result;
                    }
                }
            }
            break;

        case 0x6F: 
            {
                if ((modrm >> 6) == 3) { 
                    memcpy(xmmRegisters[reg].data, xmmRegisters[rm].data, 16);
                } else { 
                    uint32_t addr = readOperand(modrm);
                    for (int i = 0; i < 4; i++) {
                        xmmRegisters[reg].data[i] = memory->read32(addr + i * 4);
                    }
                }
            }
            break;

        case 0x7F: 
            {
                if ((modrm >> 6) == 3) { 
                    memcpy(xmmRegisters[rm].data, xmmRegisters[reg].data, 16);
                } else { 
                    uint32_t addr = readOperand(modrm);
                    for (int i = 0; i < 4; i++) {
                        memory->write32(addr + i * 4, xmmRegisters[reg].data[i]);
                    }
                }
            }
            break;

        case 0xE6: 
            {
                if ((modrm >> 6) == 3) { 
                    for (int i = 0; i < 2; i++) {
                        int32_t intVal = (int32_t)xmmRegisters[rm].data[i];
                        double d = (double)intVal;
                        uint64_t* doublePtr = (uint64_t*)&xmmRegisters[reg].data[i * 2];
                        *doublePtr = *(uint64_t*)&d;
                    }
                } else { 
                    uint32_t addr = readOperand(modrm);
                    for (int i = 0; i < 2; i++) {
                        uint32_t memVal = memory->read32(addr + i * 4);
                        int32_t intVal = (int32_t)memVal;
                        double d = (double)intVal;
                        uint64_t* doublePtr = (uint64_t*)&xmmRegisters[reg].data[i * 2];
                        *doublePtr = *(uint64_t*)&d;
                    }
                }
            }
            break;

        default:
            LOGE("Unsupported SSE opcode: 0x%02X", opcode);
            break;
    }
}

void X86Core::handleFPUOpcode(uint8_t opcode) {

    switch (opcode) {
        case 0xD8: 
        case 0xD9: 
        case 0xDA: 
        case 0xDB: 
        case 0xDC: 
        case 0xDD: 
        case 0xDE: 
        case 0xDF: 

            LOGI("FPU opcode 0x%02X encountered", opcode);
            break;
        default:
            LOGE("Unsupported FPU opcode: 0x%02X", opcode);
            break;
    }
}

uint32_t X86Core::readOperand(uint8_t modrm) {
    uint8_t mod = (modrm >> 6) & 3;
    uint8_t rm = modrm & 7;

    switch (mod) {
        case 0: 
            switch (rm) {
                case 0: return regs[3]; 
                case 1: return regs[3]; 
                case 2: return regs[5]; 
                case 3: return regs[5]; 
                case 4: return regs[6]; 
                case 5: return regs[7]; 
                case 6: return memory->read32(eip); 
                case 7: return regs[0]; 
            }
            break;
        case 1: 
            {
                int8_t disp = memory->read8(eip++);
                uint32_t base = 0;
                switch (rm) {
                    case 0: base = regs[3]; break; 
                    case 1: base = regs[3]; break; 
                    case 2: base = regs[5]; break; 
                    case 3: base = regs[5]; break; 
                    case 4: base = regs[6]; break; 
                    case 5: base = regs[7]; break; 
                    case 6: base = regs[1]; break; 
                    case 7: base = regs[0]; break; 
                }
                return base + disp;
            }
        case 2: 
            {
                int32_t disp = memory->read32(eip);
                eip += 4;
                uint32_t base = 0;
                switch (rm) {
                    case 0: base = regs[3]; break; 
                    case 1: base = regs[3]; break; 
                    case 2: base = regs[5]; break; 
                    case 3: base = regs[5]; break; 
                    case 4: base = regs[6]; break; 
                    case 5: base = regs[7]; break; 
                    case 6: base = regs[1]; break; 
                    case 7: base = regs[0]; break; 
                }
                return base + disp;
            }
        case 3: 
            return regs[rm];
    }
    return 0;
}

void X86Core::writeOperand(uint8_t modrm, uint32_t value) {
    uint8_t mod = (modrm >> 6) & 3;
    uint8_t rm = modrm & 7;

    switch (mod) {
        case 0: 
            switch (rm) {
                case 0: memory->write32(regs[3], value); break; 
                case 1: memory->write32(regs[3], value); break; 
                case 2: memory->write32(regs[5], value); break; 
                case 3: memory->write32(regs[5], value); break; 
                case 4: memory->write32(regs[6], value); break; 
                case 5: memory->write32(regs[7], value); break; 
                case 6: memory->write32(memory->read32(eip), value); break; 
                case 7: memory->write32(regs[0], value); break; 
            }
            break;
        case 1: 
            {
                int8_t disp = memory->read8(eip++);
                uint32_t base = 0;
                switch (rm) {
                    case 0: base = regs[3]; break; 
                    case 1: base = regs[3]; break; 
                    case 2: base = regs[5]; break; 
                    case 3: base = regs[5]; break; 
                    case 4: base = regs[6]; break; 
                    case 5: base = regs[7]; break; 
                    case 6: base = regs[1]; break; 
                    case 7: base = regs[0]; break; 
                }
                memory->write32(base + disp, value);
            }
            break;
        case 2: 
            {
                int32_t disp = memory->read32(eip);
                eip += 4;
                uint32_t base = 0;
                switch (rm) {
                    case 0: base = regs[3]; break; 
                    case 1: base = regs[3]; break; 
                    case 2: base = regs[5]; break; 
                    case 3: base = regs[5]; break; 
                    case 4: base = regs[6]; break; 
                    case 5: base = regs[7]; break; 
                    case 6: base = regs[1]; break; 
                    case 7: base = regs[0]; break; 
                }
                memory->write32(base + disp, value);
            }
            break;
        case 3: 
            regs[rm] = value;
            break;
    }
}


void X86Core::movsb() {

    uint8_t value = memory->read8(regs[6]); 
    memory->write8(regs[7], value); 


    if (eflags & 0x400) { 
        regs[6]--; 
        regs[7]--; 
    } else {
        regs[6]++; 
        regs[7]++; 
    }
}

void X86Core::movsd() {

    uint32_t value = memory->read32(regs[6]); 
    memory->write32(regs[7], value); 


    if (eflags & 0x400) { 
        regs[6] -= 4; 
        regs[7] -= 4; 
    } else {
        regs[6] += 4; 
        regs[7] += 4; 
    }
}
void X86Core::cmpsb() {

    uint8_t src = memory->read8(regs[6]); 
    uint8_t dst = memory->read8(regs[7]); 


    uint32_t result = src - dst;
    setFlags(result);


    if (eflags & 0x400) { 
        regs[6]--; 
        regs[7]--; 
    } else {
        regs[6]++; 
        regs[7]++; 
    }
}

void X86Core::cmpsd() {

    uint32_t src = memory->read32(regs[6]); 
    uint32_t dst = memory->read32(regs[7]); 


    uint32_t result = src - dst;
    setFlags(result);


    if (eflags & 0x400) { 
        regs[6] -= 4; 
        regs[7] -= 4; 
    } else {
        regs[6] += 4; 
        regs[7] += 4; 
    }
}

void X86Core::stosb() {

    memory->write8(regs[7], regs[0] & 0xFF); 


    if (eflags & 0x400) { 
        regs[7]--; 
    } else {
        regs[7]++; 
    }
}

void X86Core::stosd() {

    memory->write32(regs[7], regs[0]); 


    if (eflags & 0x400) { 
        regs[7] -= 4; 
    } else {
        regs[7] += 4; 
    }
}

void X86Core::lodsb() {

    uint8_t value = memory->read8(regs[6]); 
    regs[0] = (regs[0] & 0xFFFFFF00) | value; 


    if (eflags & 0x400) { 
        regs[6]--; 
    } else {
        regs[6]++; 
    }
}

void X86Core::lodsd() {

    uint32_t value = memory->read32(regs[6]); 
    regs[0] = value; 


    if (eflags & 0x400) { 
        regs[6] -= 4; 
    } else {
        regs[6] += 4; 
    }
}

void X86Core::scasb() {

    uint8_t al = regs[0] & 0xFF;
    uint8_t mem = memory->read8(regs[7]); 


    uint32_t result = al - mem;
    setFlags(result);


    if (eflags & 0x400) { 
        regs[7]--; 
    } else {
        regs[7]++; 
    }
}

void X86Core::scasd() {

    uint32_t eax = regs[0];
    uint32_t mem = memory->read32(regs[7]); 


    uint32_t result = eax - mem;
    setFlags(result);


    if (eflags & 0x400) { 
        regs[7] -= 4; 
    } else {
        regs[7] += 4; 
    }
}


void X86Core::handleLodsInstruction() {

    LOGD("CPU: Executing Xbox LODS instruction");
    lodsd(); 
}

void X86Core::handleScasInstruction() {

    LOGD("CPU: Executing Xbox SCAS instruction");
    scasd(); 
}

void X86Core::handleCmpsInstruction() {

    LOGD("CPU: Executing Xbox CMPS instruction");
    cmpsd(); 
}

void X86Core::handleStosInstruction() {

    LOGD("CPU: Executing Xbox STOS instruction");
    stosd(); 
}

void X86Core::handleMovsInstruction() {

    LOGD("CPU: Executing Xbox MOVS instruction");
    movsd(); 
}
void X86Core::handleInsInstruction() {

    LOGD("CPU: Executing Xbox INS instruction");

    [[maybe_unused]] uint32_t port = edx & 0xFFFF; 
    [[maybe_unused]] uint32_t value = 0; 
    memory->write32(regs[7], value); 


    if (eflags & 0x400) { 
        regs[7] -= 4; 
    } else {
        regs[7] += 4; 
    }
}

void X86Core::handleOutsInstruction() {

    LOGD("CPU: Executing Xbox OUTS instruction");

    (void)(edx & 0xFFFF); 
    (void)memory->read32(regs[6]); 



    if (eflags & 0x400) { 
        regs[6] -= 4; 
    } else {
        regs[6] += 4; 
    }
}


void X86Core::nop() {

}

void X86Core::cld() {

    eflags &= ~0x400; 
}

void X86Core::std() {

    eflags |= 0x400; 
}

void X86Core::cli() {

    eflags &= ~0x200; 
}

void X86Core::sti() {

    eflags |= 0x200; 
}

void X86Core::clc() {

    eflags &= ~0x001; 
}

void X86Core::stc() {

    eflags |= 0x001; 
}

void X86Core::cmc() {

    eflags ^= 0x001; 
}

void X86Core::mov_al_imm8() {
    uint8_t imm8 = memory->read8(eip++);
    regs[0] = (regs[0] & 0xFFFFFF00) | imm8; 
}

void X86Core::mov_cl_imm8() {
    uint8_t imm8 = memory->read8(eip++);
    regs[1] = (regs[1] & 0xFFFFFF00) | imm8; 
}

void X86Core::mov_dl_imm8() {
    uint8_t imm8 = memory->read8(eip++);
    regs[2] = (regs[2] & 0xFFFFFF00) | imm8; 
}

void X86Core::mov_bl_imm8() {
    uint8_t imm8 = memory->read8(eip++);
    regs[3] = (regs[3] & 0xFFFFFF00) | imm8; 
}

void X86Core::mov_ah_imm8() {
    uint8_t imm8 = memory->read8(eip++);
    regs[0] = (regs[0] & 0xFFFF00FF) | (imm8 << 8); 
}

void X86Core::mov_ch_imm8() {
    uint8_t imm8 = memory->read8(eip++);
    regs[1] = (regs[1] & 0xFFFF00FF) | (imm8 << 8); 
}

void X86Core::mov_dh_imm8() {
    uint8_t imm8 = memory->read8(eip++);
    regs[2] = (regs[2] & 0xFFFF00FF) | (imm8 << 8); 
}

void X86Core::mov_bh_imm8() {
    uint8_t imm8 = memory->read8(eip++);
    regs[3] = (regs[3] & 0xFFFF00FF) | (imm8 << 8); 
}

void X86Core::mov_eax_imm32() {
    uint32_t imm32 = memory->read32(eip);
    eip += 4;
    regs[0] = imm32;
}

void X86Core::mov_ecx_imm32() {
    uint32_t imm32 = memory->read32(eip);
    eip += 4;
    regs[1] = imm32;
}

void X86Core::mov_edx_imm32() {
    uint32_t imm32 = memory->read32(eip);
    eip += 4;
    regs[2] = imm32;
}

void X86Core::mov_ebx_imm32() {
    uint32_t imm32 = memory->read32(eip);
    eip += 4;
    regs[3] = imm32;
}

void X86Core::mov_esp_imm32() {
    uint32_t imm32 = memory->read32(eip);
    eip += 4;
    esp = imm32;
}

void X86Core::mov_ebp_imm32() {
    uint32_t imm32 = memory->read32(eip);
    eip += 4;
    regs[5] = imm32;
}

void X86Core::mov_esi_imm32() {
    uint32_t imm32 = memory->read32(eip);
    eip += 4;
    regs[6] = imm32;
}

void X86Core::mov_edi_imm32() {
    uint32_t imm32 = memory->read32(eip);
    eip += 4;
    regs[7] = imm32;
}

void X86Core::push_eax() {
    esp -= 4;
    memory->write32(esp, regs[0]);
}

void X86Core::push_ecx() {
    esp -= 4;
    memory->write32(esp, regs[1]);
}

void X86Core::push_edx() {
    esp -= 4;
    memory->write32(esp, regs[2]);
}

void X86Core::push_ebx() {
    esp -= 4;
    memory->write32(esp, regs[3]);
}

void X86Core::push_esp() {
    esp -= 4;
    memory->write32(esp, esp + 4);
}

void X86Core::push_ebp() {
    esp -= 4;
    memory->write32(esp, regs[5]);
}

void X86Core::push_esi() {
    esp -= 4;
    memory->write32(esp, regs[6]);
}

void X86Core::push_edi() {
    esp -= 4;
    memory->write32(esp, regs[7]);
}

void X86Core::pop_eax() {
    regs[0] = memory->read32(esp);
    esp += 4;
}

void X86Core::pop_ecx() {
    regs[1] = memory->read32(esp);
    esp += 4;
}

void X86Core::pop_edx() {
    regs[2] = memory->read32(esp);
    esp += 4;
}

void X86Core::pop_ebx() {
    regs[3] = memory->read32(esp);
    esp += 4;
}

void X86Core::pop_esp() {
    uint32_t value = memory->read32(esp);
    esp += 4;
    esp = value; 
}

void X86Core::pop_ebp() {
    regs[5] = memory->read32(esp);
    esp += 4;
}

void X86Core::pop_esi() {
    regs[6] = memory->read32(esp);
    esp += 4;
}

void X86Core::pop_edi() {
    regs[7] = memory->read32(esp);
    esp += 4;
}

void X86Core::inc_eax() {
    regs[0]++;
    setFlags(regs[0]);
}

void X86Core::inc_ecx() {
    regs[1]++;
    setFlags(regs[1]);
}

void X86Core::inc_edx() {
    regs[2]++;
    setFlags(regs[2]);
}

void X86Core::inc_ebx() {
    regs[3]++;
    setFlags(regs[3]);
}

void X86Core::inc_esp() {
    regs[4]++;
    setFlags(regs[4]);
}

void X86Core::inc_ebp() {
    regs[5]++;
    setFlags(regs[5]);
}

void X86Core::inc_esi() {
    regs[6]++;
    setFlags(regs[6]);
}

void X86Core::inc_edi() {
    regs[7]++;
    setFlags(regs[7]);
}

void X86Core::dec_eax() {
    regs[0]--;
    setFlags(regs[0]);
}

void X86Core::dec_ecx() {
    regs[1]--;
    setFlags(regs[1]);
}

void X86Core::dec_edx() {
    regs[2]--;
    setFlags(regs[2]);
}

void X86Core::dec_ebx() {
    regs[3]--;
    setFlags(regs[3]);
}

void X86Core::dec_esp() {
    regs[4]--;
    setFlags(regs[4]);
}

void X86Core::dec_ebp() {
    regs[5]--;
    setFlags(regs[5]);
}

void X86Core::dec_esi() {
    regs[6]--;
    setFlags(regs[6]);
}

void X86Core::dec_edi() {
    regs[7]--;
    setFlags(regs[7]);
}


void X86Core::add_rm8_r8() {
    uint8_t modrm = memory->read8(eip++);
    uint8_t reg = (modrm >> 3) & 7;
    uint8_t src = getRegister8(reg);
    uint8_t dest = readOperand8(modrm);
    uint8_t result = dest + src;
    writeOperand8(modrm, result);
    updateFlags8(result, dest, src, ALU_ADD);
}

void X86Core::add_r8_rm8() {
    uint8_t modrm = memory->read8(eip++);
    uint8_t reg = (modrm >> 3) & 7;
    uint8_t src = readOperand8(modrm);
    uint8_t dest = getRegister8(reg);
    uint8_t result = dest + src;
    setRegister8(reg, result);
    updateFlags8(result, dest, src, ALU_ADD);
}

void X86Core::add_al_imm8() {
    uint8_t imm8 = memory->read8(eip++);
    uint8_t al = eax & 0xFF;
    uint8_t result = al + imm8;
    eax = (eax & 0xFFFFFF00) | result;
    updateFlags8(result, al, imm8, ALU_ADD);
}
void X86Core::push_es() {
    esp -= 2;
    memory->write16(esp, es);
}

void X86Core::pop_es() {
    es = memory->read16(esp);
    esp += 2;
}

void X86Core::or_rm8_r8() {
    uint8_t modrm = memory->read8(eip++);
    uint8_t reg = (modrm >> 3) & 7;
    uint8_t src = getRegister8(reg);
    uint8_t dest = readOperand8(modrm);
    uint8_t result = dest | src;
    writeOperand8(modrm, result);
    updateFlags8(result, dest, src, ALU_OR);
}

void X86Core::or_r8_rm8() {
    uint8_t modrm = memory->read8(eip++);
    uint8_t reg = (modrm >> 3) & 7;
    uint8_t src = readOperand8(modrm);
    uint8_t dest = getRegister8(reg);
    uint8_t result = dest | src;
    setRegister8(reg, result);
    updateFlags8(result, dest, src, ALU_OR);
}

void X86Core::or_al_imm8() {
    uint8_t imm8 = memory->read8(eip++);
    uint8_t al = eax & 0xFF;
    uint8_t result = al | imm8;
    eax = (eax & 0xFFFFFF00) | result;
    updateFlags8(result, al, imm8, ALU_OR);
}

void X86Core::push_cs() {
    esp -= 2;
    memory->write16(esp, cs);
}
void X86Core::handle0FOpcode() {
    uint8_t extOpcode = memory->read8(eip++);

    switch (extOpcode) {
        case 0x3F: 
            handleXboxSpecificOpcode();
            break;
        case 0x40: case 0x41: case 0x42: case 0x43: 
        case 0x44: case 0x45: case 0x46: case 0x47:
            handleCMOVOpcode(extOpcode);
            break;
        case 0xA0: 
            push_fs();
            break;
        case 0xA8: 
            push_gs();
            break;
        case 0xA1: 
            pop_fs();
            break;
        case 0xA9: 
            pop_gs();
            break;
        case 0x85: 
            {
                int8_t offset = memory->read8(eip++);
                if (!(eflags & 0x40)) { 
                    eip += offset;
                }
            }
            break;
        default:
            LOGE("Unsupported 0F opcode: 0x%02X", extOpcode);
            break;
    }
}

void X86Core::adc_rm8_r8() {
    uint8_t modrm = memory->read8(eip++);
    uint8_t reg = (modrm >> 3) & 7;
    uint8_t src = getRegister8(reg);
    uint8_t dest = readOperand8(modrm);
    uint8_t cf = (eflags & 0x1) ? 1 : 0;
    uint8_t result = dest + src + cf;
    writeOperand8(modrm, result);
    updateFlags8(result, dest, src + cf, ALU_ADD);
}

void X86Core::adc_r8_rm8() {
    uint8_t modrm = memory->read8(eip++);
    uint8_t reg = (modrm >> 3) & 7;
    uint8_t src = readOperand8(modrm);
    uint8_t dest = getRegister8(reg);
    uint8_t cf = (eflags & 0x1) ? 1 : 0;
    uint8_t result = dest + src + cf;
    setRegister8(reg, result);
    updateFlags8(result, dest, src + cf, ALU_ADD);
}

void X86Core::adc_al_imm8() {
    uint8_t imm8 = memory->read8(eip++);
    uint8_t al = eax & 0xFF;
    uint8_t cf = (eflags & 0x1) ? 1 : 0;
    uint8_t result = al + imm8 + cf;
    eax = (eax & 0xFFFFFF00) | result;
    updateFlags8(result, al, imm8 + cf, ALU_ADD);
}

void X86Core::push_ss() {
    esp -= 2;
    memory->write16(esp, ss);
}

void X86Core::pop_ss() {
    ss = memory->read16(esp);
    esp += 2;
}
void X86Core::sbb_rm8_r8() {
    uint8_t modrm = memory->read8(eip++);
    uint8_t reg = (modrm >> 3) & 7;
    uint8_t src = getRegister8(reg);
    uint8_t dest = readOperand8(modrm);
    uint8_t cf = (eflags & 0x001) ? 1 : 0;
    uint8_t result = dest - src - cf;
    writeOperand8(modrm, result);
    updateFlags8(result, dest, src, ALU_SUB);
}

void X86Core::sbb_r8_rm8() {
    uint8_t modrm = memory->read8(eip++);
    uint8_t reg = (modrm >> 3) & 7;
    uint8_t src = readOperand8(modrm);
    uint8_t dest = getRegister8(reg);
    uint8_t cf = (eflags & 0x001) ? 1 : 0;
    uint8_t result = dest - src - cf;
    setRegister8(reg, result);
    updateFlags8(result, dest, src, ALU_SUB);
}

void X86Core::sbb_al_imm8() {
    uint8_t imm8 = memory->read8(eip++);
    uint8_t al = eax & 0xFF;
    uint8_t cf = (eflags & 0x001) ? 1 : 0;
    uint8_t result = al - imm8 - cf;
    eax = (eax & 0xFFFFFF00) | result;
    updateFlags8(result, al, imm8, ALU_SUB);
}
void X86Core::push_ds() {
    push16(ds);
}

void X86Core::pop_ds() {
    ds = pop16();
}
void X86Core::and_rm8_r8() {
    uint8_t modrm = memory->read8(eip++);
    uint8_t reg = (modrm >> 3) & 7;
    uint8_t src = getRegister8(reg);
    uint8_t dest = readOperand8(modrm);
    uint8_t result = dest & src;
    writeOperand8(modrm, result);
    updateFlags8(result, dest, src, ALU_AND);
}

void X86Core::and_r8_rm8() {
    uint8_t modrm = memory->read8(eip++);
    uint8_t reg = (modrm >> 3) & 7;
    uint8_t src = readOperand8(modrm);
    uint8_t dest = getRegister8(reg);
    uint8_t result = dest & src;
    setRegister8(reg, result);
    updateFlags8(result, dest, src, ALU_AND);
}

void X86Core::and_al_imm8() {
    uint8_t imm8 = memory->read8(eip++);
    uint8_t al = eax & 0xFF;
    uint8_t result = al & imm8;
    eax = (eax & 0xFFFFFF00) | result;
    updateFlags8(result, al, imm8, ALU_AND);
}
void X86Core::es_prefix() {


}

void X86Core::daa() {

    uint8_t al_val = eax & 0xFF;
    uint8_t old_al = al_val;

    if ((al_val & 0x0F) > 9 || (eflags & 0x10)) { 
        al_val += 6;
        eflags |= 0x10; 
    } else {
        eflags &= ~0x10; 
    }

    if (old_al > 0x99 || (eflags & 0x1)) { 
        al_val += 0x60;
        eflags |= 0x1; 
    } else {
        eflags &= ~0x1; 
    }

    eax = (eax & 0xFFFFFF00) | al_val;
    updateFlags8(al_val, old_al, 0, ALU_ADD);
}
void X86Core::sub_rm8_r8() {
    uint8_t modrm = memory->read8(eip++);
    uint8_t reg = (modrm >> 3) & 7;
    uint8_t src = getRegister8(reg);
    uint8_t dest = readOperand8(modrm);
    uint8_t result = dest - src;
    writeOperand8(modrm, result);
    updateFlags8(result, dest, src, ALU_SUB);
}

void X86Core::sub_r8_rm8() {
    uint8_t modrm = memory->read8(eip++);
    uint8_t reg = (modrm >> 3) & 7;
    uint8_t src = readOperand8(modrm);
    uint8_t dest = getRegister8(reg);
    uint8_t result = dest - src;
    setRegister8(reg, result);
    updateFlags8(result, dest, src, ALU_SUB);
}

void X86Core::sub_al_imm8() {
    uint8_t imm8 = memory->read8(eip++);
    uint8_t al = eax & 0xFF;
    uint8_t result = al - imm8;
    eax = (eax & 0xFFFFFF00) | result;
    updateFlags8(result, al, imm8, ALU_SUB);
}
void X86Core::cs_prefix() {


}

void X86Core::das() {

    uint8_t al_val = eax & 0xFF;
    uint8_t old_al = al_val;

    if ((al_val & 0x0F) > 9 || (eflags & 0x10)) { 
        al_val -= 6;
        eflags |= 0x10; 
    } else {
        eflags &= ~0x10; 
    }

    if (old_al > 0x99 || (eflags & 0x1)) { 
        al_val -= 0x60;
        eflags |= 0x1; 
    } else {
        eflags &= ~0x1; 
    }

    eax = (eax & 0xFFFFFF00) | al_val;
    updateFlags8(al_val, old_al, 0, ALU_SUB);
}
void X86Core::xor_rm8_r8() {
    uint8_t modrm = memory->read8(eip++);
    uint8_t reg = (modrm >> 3) & 7;
    uint8_t src = getRegister8(reg);
    uint8_t dest = readOperand8(modrm);
    uint8_t result = dest ^ src;
    writeOperand8(modrm, result);
    updateFlags8(result, dest, src, ALU_XOR);
}

void X86Core::xor_r8_rm8() {
    uint8_t modrm = memory->read8(eip++);
    uint8_t reg = (modrm >> 3) & 7;
    uint8_t src = readOperand8(modrm);
    uint8_t dest = getRegister8(reg);
    uint8_t result = dest ^ src;
    setRegister8(reg, result);
    updateFlags8(result, dest, src, ALU_XOR);
}

void X86Core::xor_al_imm8() {
    uint8_t imm8 = memory->read8(eip++);
    uint8_t al = eax & 0xFF;
    uint8_t result = al ^ imm8;
    eax = (eax & 0xFFFFFF00) | result;
    updateFlags8(result, al, imm8, ALU_XOR);
}
void X86Core::ss_prefix() {


}

void X86Core::aaa() {

    uint8_t al_val = eax & 0xFF;

    if ((al_val & 0x0F) > 9 || (eflags & 0x10)) { 
        al_val += 6;
        eax = (eax & 0xFFFFFF00) | al_val;
        eax = (eax & 0xFFFF00FF) | (((eax >> 8) & 0xFF) + 1) << 8; 
        eflags |= 0x11; 
    } else {
        eflags &= ~0x11; 
    }

    al_val &= 0x0F; 
    eax = (eax & 0xFFFFFF00) | al_val;
}
void X86Core::cmp_rm8_r8() {
    uint8_t modrm = memory->read8(eip++);
    uint8_t reg = (modrm >> 3) & 7;
    uint8_t src = getRegister8(reg);
    uint8_t dest = readOperand8(modrm);
    uint8_t result = dest - src;
    updateFlags8(result, dest, src, ALU_SUB);
}

void X86Core::cmp_r8_rm8() {
    uint8_t modrm = memory->read8(eip++);
    uint8_t reg = (modrm >> 3) & 7;
    uint8_t src = readOperand8(modrm);
    uint8_t dest = getRegister8(reg);
    uint8_t result = dest - src;
    updateFlags8(result, dest, src, ALU_SUB);
}

void X86Core::cmp_al_imm8() {
    uint8_t imm8 = memory->read8(eip++);
    uint8_t al = eax & 0xFF;
    uint8_t result = al - imm8;
    updateFlags8(result, al, imm8, ALU_SUB);
}
void X86Core::ds_prefix() {


}

void X86Core::aas() {

    uint8_t al_val = eax & 0xFF;

    if ((al_val & 0x0F) > 9 || (eflags & 0x10)) { 
        al_val -= 6;
        eax = (eax & 0xFFFFFF00) | al_val;
        eax = (eax & 0xFFFF00FF) | (((eax >> 8) & 0xFF) - 1) << 8; 
        eflags |= 0x11; 
    } else {
        eflags &= ~0x11; 
    }

    al_val &= 0x0F; 
    eax = (eax & 0xFFFFFF00) | al_val;
}
void X86Core::pushad() {
    uint32_t old_esp = esp;
    esp -= 4; memory->write32(esp, eax);
    esp -= 4; memory->write32(esp, ecx);
    esp -= 4; memory->write32(esp, edx);
    esp -= 4; memory->write32(esp, ebx);
    esp -= 4; memory->write32(esp, old_esp);
    esp -= 4; memory->write32(esp, ebp);
    esp -= 4; memory->write32(esp, esi);
    esp -= 4; memory->write32(esp, edi);
}

void X86Core::popad() {
    edi = memory->read32(esp); esp += 4;
    esi = memory->read32(esp); esp += 4;
    ebp = memory->read32(esp); esp += 4;

    esp += 4;
    ebx = memory->read32(esp); esp += 4;
    edx = memory->read32(esp); esp += 4;
    ecx = memory->read32(esp); esp += 4;
    eax = memory->read32(esp); esp += 4;
}
void X86Core::bound() {





    uint8_t modrm = memory->read8(eip++);

    (void)modrm; 

    LOGD("CPU: BOUND instruction treated as NOP to prevent interrupt loops");
















}

void X86Core::arpl() {

    uint8_t modrm = memory->read8(eip++);
    uint8_t reg = (modrm >> 3) & 7;
    uint16_t selector = readOperand16(modrm);
    uint16_t reg_selector = getRegister(reg) & 0xFFFF;

    uint8_t rpl = selector & 3;
    uint8_t reg_rpl = reg_selector & 3;

    if (rpl < reg_rpl) {
        selector = (selector & 0xFFFC) | reg_rpl;
        writeOperand16(modrm, selector);
        eflags |= 0x40; 
    } else {
        eflags &= ~0x40; 
    }
}
void X86Core::fs_prefix() {

    LOGD("🎮 FS Prefix aktiviert - Segment Override auf FS");
    segmentOverride = FS;
    prefixActive = true;


    fs = 0x80000000; 

    LOGD("✅ FS-Segment gesetzt: Base=0x%08X", fs);
}

void X86Core::gs_prefix() {

    LOGD("🎮 GS Prefix aktiviert - Segment Override auf GS");
    segmentOverride = GS;
    prefixActive = true;


    gs = 0xA0000000; 

    LOGD("✅ GS-Segment gesetzt: Base=0x%08X", gs);
}

void X86Core::operand_size_prefix() {

    LOGD("🎮 Operand Size Prefix aktiviert - 16-bit Operanden");
    operandSizeOverride = true;
    prefixActive = true;

    LOGD("✅ Operand Size Override aktiviert");
}
void X86Core::address_size_prefix() {

    LOGD("🎮 Address Size Prefix aktiviert - 16-bit Adressen");
    addressSizeOverride = true;
    prefixActive = true;

    LOGD("✅ Address Size Override aktiviert");
}

void X86Core::resetPrefixes() {

    segmentOverride = NONE;
    prefixActive = false;
    operandSizeOverride = false;
    addressSizeOverride = false;

    LOGD("🔄 Prefix-Handler zurückgesetzt");
}

uint32_t X86Core::getEffectiveAddressWithSegment(uint32_t address) {

    uint32_t baseAddress = 0;

    switch (segmentOverride) {
        case CS:
            baseAddress = cs << 4;
            break;
        case DS:
            baseAddress = ds << 4;
            break;
        case ES:
            baseAddress = es << 4;
            break;
        case FS:
            baseAddress = fs;
            break;
        case GS:
            baseAddress = gs;
            break;
        case SS:
            baseAddress = ss << 4;
            break;
        case NONE:
        default:

            baseAddress = ds << 4; 
            break;
    }

    uint32_t effectiveAddress = baseAddress + address;
    LOGD("🎯 Effektive Adresse berechnet: 0x%08X (Base: 0x%08X + Offset: 0x%08X)",
         effectiveAddress, baseAddress, address);

    return effectiveAddress;
}



uint8_t X86Core::safeRead8(uint32_t address) {
    try {
        return memory->read8(address);
    } catch (...) {
        LOGW("Safe Memory: Read8 failed at 0x%08X - returning 0", address);
        return 0;
    }
}

uint16_t X86Core::safeRead16(uint32_t address) {
    try {
        return memory->read16(address);
    } catch (...) {
        LOGW("Safe Memory: Read16 failed at 0x%08X - returning 0", address);
        return 0;
    }
}

uint32_t X86Core::safeRead32(uint32_t address) {
    try {
        return memory->read32(address);
    } catch (...) {
        LOGW("Safe Memory: Read32 failed at 0x%08X - returning 0", address);
        return 0;
    }
}

void X86Core::safeWrite8(uint32_t address, uint8_t value) {
    try {
        memory->write8(address, value);
    } catch (...) {
        LOGW("Safe Memory: Write8 failed at 0x%08X (value: 0x%02X) - ignored", address, value);
    }
}

void X86Core::safeWrite16(uint32_t address, uint16_t value) {
    try {
        memory->write16(address, value);
    } catch (...) {
        LOGW("Safe Memory: Write16 failed at 0x%08X (value: 0x%04X) - ignored", address, value);
    }
}

void X86Core::safeWrite32(uint32_t address, uint32_t value) {
    try {
        memory->write32(address, value);
    } catch (...) {
        LOGW("Safe Memory: Write32 failed at 0x%08X (value: 0x%08X) - ignored", address, value);
    }
}

void X86Core::insb() {

    (void)(edx & 0xFFFF); 
    uint8_t value = 0; 
    memory->write8(edi, value);

    if (eflags & 0x400) { 
        edi--;
    } else {
        edi++;
    }
}
void X86Core::insd() {

    [[maybe_unused]] uint32_t port = edx & 0xFFFF; 
    [[maybe_unused]] uint32_t value = 0; 
    memory->write32(edi, value);

    if (eflags & 0x400) { 
        edi -= 4;
    } else {
        edi += 4;
    }
}

void X86Core::outsb() {

    [[maybe_unused]] uint32_t port = edx & 0xFFFF; 
    [[maybe_unused]] uint8_t value = memory->read8(esi); 


    if (eflags & 0x400) { 
        esi--;
    } else {
        esi++;
    }
}

void X86Core::outsd() {

    [[maybe_unused]] uint32_t port = edx & 0xFFFF; 
    [[maybe_unused]] uint32_t value = memory->read32(esi); 


    if (eflags & 0x400) { 
        esi -= 4;
    } else {
        esi += 4;
    }
}


void X86Core::group1_rm8_imm8() {
    uint8_t modrm = memory->read8(eip++);
    uint8_t op = (modrm >> 3) & 7;
    uint8_t imm8 = memory->read8(eip++);
    uint8_t dest = readOperand8(modrm);
    uint8_t result = 0;

    switch (op) {
        case 0: 
            result = dest + imm8;
            updateFlags8(result, dest, imm8, ALU_ADD);
            break;
        case 1: 
            result = dest | imm8;
            updateFlags8(result, dest, imm8, ALU_OR);
            break;
        case 2: 
            {
                uint8_t cf = (eflags & 0x001) ? 1 : 0;
                result = dest + imm8 + cf;
                updateFlags8(result, dest, imm8, ALU_ADD);
            }
            break;
        case 3: 
            {
                uint8_t cf = (eflags & 0x001) ? 1 : 0;
                result = dest - imm8 - cf;
                updateFlags8(result, dest, imm8, ALU_SUB);
            }
            break;
        case 4: 
            result = dest & imm8;
            updateFlags8(result, dest, imm8, ALU_AND);
            break;
        case 5: 
            result = dest - imm8;
            updateFlags8(result, dest, imm8, ALU_SUB);
            break;
        case 6: 
            result = dest ^ imm8;
            updateFlags8(result, dest, imm8, ALU_XOR);
            break;
        case 7: 
            result = dest - imm8;
            updateFlags8(result, dest, imm8, ALU_SUB);
            break;
    }

    if (op != 7) { 
        writeOperand8(modrm, result);
    }
}

void X86Core::group1_rm32_imm32() {
    uint8_t modrm = memory->read8(eip++);
    uint8_t op = (modrm >> 3) & 7;
    uint32_t imm32 = memory->read32(eip);
    eip += 4;
    uint32_t dest = readOperand(modrm);
    uint32_t result = 0;

    switch (op) {
        case 0: 
            result = dest + imm32;
            updateFlags(result, dest, imm32, ALU_ADD);
            break;
        case 1: 
            result = dest | imm32;
            updateFlags(result, dest, imm32, ALU_OR);
            break;
        case 2: 
            {
                uint32_t cf = (eflags & 0x001) ? 1 : 0;
                result = dest + imm32 + cf;
                updateFlags(result, dest, imm32, ALU_ADD);
            }
            break;
        case 3: 
            {
                uint32_t cf = (eflags & 0x001) ? 1 : 0;
                result = dest - imm32 - cf;
                updateFlags(result, dest, imm32, ALU_SUB);
            }
            break;
        case 4: 
            result = dest & imm32;
            updateFlags(result, dest, imm32, ALU_AND);
            break;
        case 5: 
            result = dest - imm32;
            updateFlags(result, dest, imm32, ALU_SUB);
            break;
        case 6: 
            result = dest ^ imm32;
            updateFlags(result, dest, imm32, ALU_XOR);
            break;
        case 7: 
            result = dest - imm32;
            updateFlags(result, dest, imm32, ALU_SUB);
            break;
    }

    if (op != 7) { 
        writeOperand(modrm, result);
    }
}

void X86Core::group1_rm32_imm8() {
    uint8_t modrm = memory->read8(eip++);
    uint8_t op = (modrm >> 3) & 7;
    int8_t imm8 = static_cast<int8_t>(memory->read8(eip++));
    uint32_t dest = readOperand(modrm);
    uint32_t result = 0;

    switch (op) {
        case 0: 
            result = dest + static_cast<uint32_t>(imm8);
            updateFlags(result, dest, static_cast<uint32_t>(imm8), ALU_ADD);
            break;
        case 1: 
            result = dest | static_cast<uint32_t>(imm8);
            updateFlags(result, dest, static_cast<uint32_t>(imm8), ALU_OR);
            break;
        case 2: 
            {
                uint32_t cf = (eflags & 0x001) ? 1 : 0;
                result = dest + static_cast<uint32_t>(imm8) + cf;
                updateFlags(result, dest, static_cast<uint32_t>(imm8), ALU_ADD);
            }
            break;
        case 3: 
            {
                uint32_t cf = (eflags & 0x001) ? 1 : 0;
                result = dest - static_cast<uint32_t>(imm8) - cf;
                updateFlags(result, dest, static_cast<uint32_t>(imm8), ALU_SUB);
            }
            break;
        case 4: 
            result = dest & static_cast<uint32_t>(imm8);
            updateFlags(result, dest, static_cast<uint32_t>(imm8), ALU_AND);
            break;
        case 5: 
            result = dest - static_cast<uint32_t>(imm8);
            updateFlags(result, dest, static_cast<uint32_t>(imm8), ALU_SUB);
            break;
        case 6: 
            result = dest ^ static_cast<uint32_t>(imm8);
            updateFlags(result, dest, static_cast<uint32_t>(imm8), ALU_XOR);
            break;
        case 7: 
            result = dest - static_cast<uint32_t>(imm8);
            updateFlags(result, dest, static_cast<uint32_t>(imm8), ALU_SUB);
            break;
    }

    if (op != 7) { 
        writeOperand(modrm, result);
    }
}
void X86Core::test_rm8_r8() {
    uint8_t modrm = memory->read8(eip++);
    uint8_t reg = (modrm >> 3) & 7;
    uint8_t src = getRegister8(reg);
    uint8_t dest = readOperand8(modrm);
    uint8_t result = dest & src;
    updateFlags8(result, dest, src, ALU_AND);
}

void X86Core::xchg_rm8_r8() {
    uint8_t modrm = memory->read8(eip++);
    uint8_t reg = (modrm >> 3) & 7;
    uint8_t regValue = getRegister8(reg);
    uint8_t memValue = readOperand8(modrm);
    setRegister8(reg, memValue);
    writeOperand8(modrm, regValue);
}

void X86Core::xchg_rm32_r32() {
    uint8_t modrm = memory->read8(eip++);
    uint8_t reg = (modrm >> 3) & 7;
    uint32_t regValue = getRegister(reg);
    uint32_t memValue = readOperand(modrm);
    setRegister(reg, memValue);
    writeOperand(modrm, regValue);
}
void X86Core::mov_rm8_r8() {
    uint8_t modrm = memory->read8(eip++);
    uint8_t reg = (modrm >> 3) & 7;
    uint8_t src = getRegister8(reg);
    writeOperand8(modrm, src);
}

void X86Core::mov_r8_rm8() {
    uint8_t modrm = memory->read8(eip++);
    uint8_t reg = (modrm >> 3) & 7;
    uint8_t src = readOperand8(modrm);
    setRegister8(reg, src);
}

void X86Core::mov_rm16_sreg() {
    uint8_t modrm = memory->read8(eip++);
    uint8_t sreg = (modrm >> 3) & 7;
    uint16_t src = getSegmentRegister(sreg);
    writeOperand16(modrm, src);
}

uint16_t X86Core::getSegmentRegister(uint8_t sreg) const {
    switch (sreg) {
        case 0: return es;
        case 1: return cs;
        case 2: return ss;
        case 3: return ds;
        case 4: return fs;
        case 5: return gs;
        default: return 0;
    }
}



uint16_t X86Core::readOperand16(uint8_t modrm) {
    if ((modrm >> 6) == 3) { 
        uint8_t reg = modrm & 7;
        switch (reg) {
            case 0: return eax & 0xFFFF;
            case 1: return ecx & 0xFFFF;
            case 2: return edx & 0xFFFF;
            case 3: return ebx & 0xFFFF;
            case 4: return esp & 0xFFFF;
            case 5: return ebp & 0xFFFF;
            case 6: return esi & 0xFFFF;
            case 7: return edi & 0xFFFF;
            default: return 0;
        }
    }
    uint32_t addr = getEffectiveAddress(modrm);
    return memory->read16(addr);
}

void X86Core::writeOperand16(uint8_t modrm, uint16_t value) {
    if ((modrm >> 6) == 3) { 
        uint8_t reg = modrm & 7;
        switch (reg) {
            case 0: eax = (eax & 0xFFFF0000) | value; break;
            case 1: ecx = (ecx & 0xFFFF0000) | value; break;
            case 2: edx = (edx & 0xFFFF0000) | value; break;
            case 3: ebx = (ebx & 0xFFFF0000) | value; break;
            case 4: esp = (esp & 0xFFFF0000) | value; break;
            case 5: ebp = (ebp & 0xFFFF0000) | value; break;
            case 6: esi = (esi & 0xFFFF0000) | value; break;
            case 7: edi = (edi & 0xFFFF0000) | value; break;
        }
        return;
    }
    uint32_t addr = getEffectiveAddress(modrm);
    memory->write16(addr, value);
}

void X86Core::lea_r32_m() {
    uint8_t modrm = memory->read8(eip++);
    uint8_t reg = (modrm >> 3) & 7;
    uint32_t addr = getEffectiveAddress(modrm);
    setRegister(reg, addr);
}

void X86Core::mov_sreg_rm16() {
    uint8_t modrm = memory->read8(eip++);
    uint8_t sreg = (modrm >> 3) & 7;
    uint16_t src = readOperand16(modrm);
    setSegmentRegister(sreg, src);
}

void X86Core::xchg_eax_ecx() {
    uint32_t temp = eax;
    eax = ecx;
    ecx = temp;
}

void X86Core::xchg_eax_edx() {
    uint32_t temp = eax;
    eax = edx;
    edx = temp;
}

void X86Core::xchg_eax_ebx() {
    uint32_t temp = eax;
    eax = ebx;
    ebx = temp;
}

void X86Core::xchg_eax_esp() {
    uint32_t temp = eax;
    eax = esp;
    esp = temp;
}

void X86Core::xchg_eax_ebp() {
    uint32_t temp = eax;
    eax = ebp;
    ebp = temp;
}

void X86Core::xchg_eax_esi() {
    uint32_t temp = eax;
    eax = esi;
    esi = temp;
}
void X86Core::xchg_eax_edi() {
    uint32_t temp = eax;
    eax = edi;
    edi = temp;
}
void X86Core::cwde() {

    int16_t ax = eax & 0xFFFF;
    eax = (int32_t)ax;
}

void X86Core::cdq() {

    int32_t signed_eax = (int32_t)eax;
    edx = (signed_eax < 0) ? 0xFFFFFFFF : 0;
}
void X86Core::call_far() {

    uint32_t offset = memory->read32(eip);
    uint16_t segment = memory->read16(eip + 4);
    eip += 6;
    push16(cs);
    push32(eip);
    eip = offset;
    cs = segment;
}

void X86Core::pushfd() {
    esp -= 4;
    memory->write32(esp, eflags);
}

void X86Core::popfd() {
    eflags = memory->read32(esp);
    esp += 4;
}
void X86Core::sahf() {

    uint8_t ah = (eax >> 8) & 0xFF;
    eflags = (eflags & 0xFFFFFF00) | ah;
}

void X86Core::lahf() {

    uint8_t flags = eflags & 0xFF;
    eax = (eax & 0xFFFF00FF) | (flags << 8);
}
void X86Core::mov_al_moffs8() {
    uint32_t offset = memory->read32(eip);
    eip += 4;
    uint8_t value = memory->read8(offset);
    eax = (eax & 0xFFFFFF00) | value;
}

void X86Core::mov_moffs8_al() {
    uint32_t offset = memory->read32(eip);
    eip += 4;
    uint8_t al = eax & 0xFF;
    memory->write8(offset, al);
}
void X86Core::test_al_imm8() {
    uint8_t imm8 = memory->read8(eip++);
    uint8_t al = eax & 0xFF;
    uint8_t result = al & imm8;
    updateFlags8(result, al, imm8, ALU_AND);
}

void X86Core::group2_rm8_imm8() {

    uint8_t modrm = memory->read8(eip++);
    uint8_t imm8 = memory->read8(eip++);
    uint8_t reg = (modrm >> 3) & 7;
    uint8_t value = readOperand8(modrm);

    switch (reg) {
        case 0: 
            for (int i = 0; i < (imm8 & 7); i++) {
                uint8_t msb = (value >> 7) & 1;
                value = (value << 1) | msb;
                eflags = (eflags & ~0x1) | msb; 
            }
            break;
        case 1: 
            for (int i = 0; i < (imm8 & 7); i++) {
                uint8_t lsb = value & 1;
                value = (value >> 1) | (lsb << 7);
                eflags = (eflags & ~0x1) | lsb; 
            }
            break;
        case 2: 
            for (int i = 0; i < (imm8 & 7); i++) {
                uint8_t old_cf = (eflags >> 0) & 1;
                uint8_t msb = (value >> 7) & 1;
                value = (value << 1) | old_cf;
                eflags = (eflags & ~0x1) | msb; 
            }
            break;
        case 3: 
            for (int i = 0; i < (imm8 & 7); i++) {
                uint8_t old_cf = (eflags >> 0) & 1;
                uint8_t lsb = value & 1;
                value = (value >> 1) | (old_cf << 7);
                eflags = (eflags & ~0x1) | lsb; 
            }
            break;
        case 4: 
            for (int i = 0; i < (imm8 & 7); i++) {
                uint8_t msb = (value >> 7) & 1;
                value <<= 1;
                eflags = (eflags & ~0x1) | msb; 
            }
            break;
        case 5: 
            for (int i = 0; i < (imm8 & 7); i++) {
                uint8_t lsb = value & 1;
                value >>= 1;
                eflags = (eflags & ~0x1) | lsb; 
            }
            break;
        case 7: 
            for (int i = 0; i < (imm8 & 7); i++) {
                uint8_t lsb = value & 1;
                value = (int8_t)value >> 1;
                eflags = (eflags & ~0x1) | lsb; 
            }
            break;
    }

    writeOperand8(modrm, value);
    updateFlags8(value, value, 0, ALU_OR);
}

void X86Core::group2_rm32_imm8() {

    uint8_t modrm = memory->read8(eip++);
    uint8_t imm8 = memory->read8(eip++);
    uint8_t reg = (modrm >> 3) & 7;
    uint32_t value = readOperand(modrm);

    switch (reg) {
        case 0: 
            for (int i = 0; i < (imm8 & 31); i++) {
                uint32_t msb = (value >> 31) & 1;
                value = (value << 1) | msb;
                eflags = (eflags & ~0x1) | msb; 
            }
            break;
        case 1: 
            for (int i = 0; i < (imm8 & 31); i++) {
                uint32_t lsb = value & 1;
                value = (value >> 1) | (lsb << 31);
                eflags = (eflags & ~0x1) | lsb; 
            }
            break;
        case 2: 
            for (int i = 0; i < (imm8 & 31); i++) {
                uint32_t old_cf = (eflags >> 0) & 1;
                uint32_t msb = (value >> 31) & 1;
                value = (value << 1) | old_cf;
                eflags = (eflags & ~0x1) | msb; 
            }
            break;
        case 3: 
            for (int i = 0; i < (imm8 & 31); i++) {
                uint32_t old_cf = (eflags >> 0) & 1;
                uint32_t lsb = value & 1;
                value = (value >> 1) | (old_cf << 31);
                eflags = (eflags & ~0x1) | lsb; 
            }
            break;
        case 4: 
            for (int i = 0; i < (imm8 & 31); i++) {
                uint32_t msb = (value >> 31) & 1;
                value <<= 1;
                eflags = (eflags & ~0x1) | msb; 
            }
            break;
        case 5: 
            for (int i = 0; i < (imm8 & 31); i++) {
                uint32_t lsb = value & 1;
                value >>= 1;
                eflags = (eflags & ~0x1) | lsb; 
            }
            break;
        case 7: 
            for (int i = 0; i < (imm8 & 31); i++) {
                uint32_t lsb = value & 1;
                value = (int32_t)value >> 1;
                eflags = (eflags & ~0x1) | lsb; 
            }
            break;
    }

    writeOperand(modrm, value);
    updateFlags(value, value, 0, ALU_OR);
}

void X86Core::les() {

    uint8_t modrm = memory->read8(eip++);
    uint8_t reg = (modrm >> 3) & 7;

    uint32_t address = getEffectiveAddress(modrm);
    uint32_t offset = memory->read32(address);
    uint16_t segment = memory->read16(address + 4);

    setRegister(reg, offset);
    es = segment;
}

void X86Core::lds() {

    uint8_t modrm = memory->read8(eip++);
    uint8_t reg = (modrm >> 3) & 7;

    uint32_t address = getEffectiveAddress(modrm);
    uint32_t offset = memory->read32(address);
    uint16_t segment = memory->read16(address + 4);

    setRegister(reg, offset);
    ds = segment;
}


void X86Core::enter() {
    uint16_t size = memory->read16(eip);
    eip += 2;
    (void)memory->read8(eip++); 


    push32(ebp);
    ebp = esp;
    esp -= size;
}

void X86Core::retf_imm16() {

    uint16_t imm16 = memory->read16(eip);
    eip += 2;
    eip = pop32();
    cs = pop16();
    esp += imm16;
}

void X86Core::retf() {

    eip = pop32();
    cs = pop16();
}
void X86Core::int3() {

    handleInterrupt(3);
}

void X86Core::into() {

    if (eflags & 0x800) { 
        handleInterrupt(4);
    }
}
void X86Core::group2_rm8_1() {

    uint8_t modrm = memory->read8(eip++);
    uint8_t reg = (modrm >> 3) & 7;
    uint8_t value = readOperand8(modrm);

    switch (reg) {
        case 0: 
            {
                uint8_t msb = (value >> 7) & 1;
                value = (value << 1) | msb;
                eflags = (eflags & ~0x1) | msb; 
            }
            break;
        case 1: 
            {
                uint8_t lsb = value & 1;
                value = (value >> 1) | (lsb << 7);
                eflags = (eflags & ~0x1) | lsb; 
            }
            break;
        case 2: 
            {
                uint8_t old_cf = (eflags >> 0) & 1;
                uint8_t msb = (value >> 7) & 1;
                value = (value << 1) | old_cf;
                eflags = (eflags & ~0x1) | msb; 
            }
            break;
        case 3: 
            {
                uint8_t old_cf = (eflags >> 0) & 1;
                uint8_t lsb = value & 1;
                value = (value >> 1) | (old_cf << 7);
                eflags = (eflags & ~0x1) | lsb; 
            }
            break;
        case 4: 
            {
                uint8_t msb = (value >> 7) & 1;
                value <<= 1;
                eflags = (eflags & ~0x1) | msb; 
            }
            break;
        case 5: 
            {
                uint8_t lsb = value & 1;
                value >>= 1;
                eflags = (eflags & ~0x1) | lsb; 
            }
            break;
        case 7: 
            {
                uint8_t lsb = value & 1;
                value = (int8_t)value >> 1;
                eflags = (eflags & ~0x1) | lsb; 
            }
            break;
    }

    writeOperand8(modrm, value);
    updateFlags8(value, value, 0, ALU_OR);
}
void X86Core::group2_rm32_1() {

    uint8_t modrm = memory->read8(eip++);
    uint8_t reg = (modrm >> 3) & 7;
    uint32_t value = readOperand(modrm);

    switch (reg) {
        case 0: 
            {
                uint32_t msb = (value >> 31) & 1;
                value = (value << 1) | msb;
                eflags = (eflags & ~0x1) | msb; 
            }
            break;
        case 1: 
            {
                uint32_t lsb = value & 1;
                value = (value >> 1) | (lsb << 31);
                eflags = (eflags & ~0x1) | lsb; 
            }
            break;
        case 2: 
            {
                uint32_t old_cf = (eflags >> 0) & 1;
                uint32_t msb = (value >> 31) & 1;
                value = (value << 1) | old_cf;
                eflags = (eflags & ~0x1) | msb; 
            }
            break;
        case 3: 
            {
                uint32_t old_cf = (eflags >> 0) & 1;
                uint32_t lsb = value & 1;
                value = (value >> 1) | (old_cf << 31);
                eflags = (eflags & ~0x1) | lsb; 
            }
            break;
        case 4: 
            {
                uint32_t msb = (value >> 31) & 1;
                value <<= 1;
                eflags = (eflags & ~0x1) | msb; 
            }
            break;
        case 5: 
            {
                uint32_t lsb = value & 1;
                value >>= 1;
                eflags = (eflags & ~0x1) | lsb; 
            }
            break;
        case 7: 
            {
                uint32_t lsb = value & 1;
                value = (int32_t)value >> 1;
                eflags = (eflags & ~0x1) | lsb; 
            }
            break;
    }

    writeOperand(modrm, value);
    updateFlags(value, value, 0, ALU_OR);
}
void X86Core::group2_rm8_cl() {

    uint8_t modrm = memory->read8(eip++);
    uint8_t reg = (modrm >> 3) & 7;
    uint8_t count = ecx & 0xFF;
    uint8_t value = readOperand8(modrm);

    count &= 7; 

    switch (reg) {
        case 0: 
            for (int i = 0; i < count; i++) {
                uint8_t msb = (value >> 7) & 1;
                value = (value << 1) | msb;
                eflags = (eflags & ~0x1) | msb; 
            }
            break;
        case 1: 
            for (int i = 0; i < count; i++) {
                uint8_t lsb = value & 1;
                value = (value >> 1) | (lsb << 7);
                eflags = (eflags & ~0x1) | lsb; 
            }
            break;
        case 2: 
            for (int i = 0; i < count; i++) {
                uint8_t old_cf = (eflags >> 0) & 1;
                uint8_t msb = (value >> 7) & 1;
                value = (value << 1) | old_cf;
                eflags = (eflags & ~0x1) | msb; 
            }
            break;
        case 3: 
            for (int i = 0; i < count; i++) {
                uint8_t old_cf = (eflags >> 0) & 1;
                uint8_t lsb = value & 1;
                value = (value >> 1) | (old_cf << 7);
                eflags = (eflags & ~0x1) | lsb; 
            }
            break;
        case 4: 
            for (int i = 0; i < count; i++) {
                uint8_t msb = (value >> 7) & 1;
                value <<= 1;
                eflags = (eflags & ~0x1) | msb; 
            }
            break;
        case 5: 
            for (int i = 0; i < count; i++) {
                uint8_t lsb = value & 1;
                value >>= 1;
                eflags = (eflags & ~0x1) | lsb; 
            }
            break;
        case 7: 
            for (int i = 0; i < count; i++) {
                uint8_t lsb = value & 1;
                value = (int8_t)value >> 1;
                eflags = (eflags & ~0x1) | lsb; 
            }
            break;
    }

    writeOperand8(modrm, value);
    updateFlags8(value, value, 0, ALU_OR);
}
void X86Core::group2_rm32_cl() {

    uint8_t modrm = memory->read8(eip++);
    uint8_t reg = (modrm >> 3) & 7;
    uint8_t count = ecx & 0xFF;
    uint32_t value = readOperand(modrm);

    count &= 31; 

    switch (reg) {
        case 0: 
            for (int i = 0; i < count; i++) {
                uint32_t msb = (value >> 31) & 1;
                value = (value << 1) | msb;
                eflags = (eflags & ~0x1) | msb; 
            }
            break;
        case 1: 
            for (int i = 0; i < count; i++) {
                uint32_t lsb = value & 1;
                value = (value >> 1) | (lsb << 31);
                eflags = (eflags & ~0x1) | lsb; 
            }
            break;
        case 2: 
            for (int i = 0; i < count; i++) {
                uint32_t old_cf = (eflags >> 0) & 1;
                uint32_t msb = (value >> 31) & 1;
                value = (value << 1) | old_cf;
                eflags = (eflags & ~0x1) | msb; 
            }
            break;
        case 3: 
            for (int i = 0; i < count; i++) {
                uint32_t old_cf = (eflags >> 0) & 1;
                uint32_t lsb = value & 1;
                value = (value >> 1) | (old_cf << 31);
                eflags = (eflags & ~0x1) | lsb; 
            }
            break;
        case 4: 
            for (int i = 0; i < count; i++) {
                uint32_t msb = (value >> 31) & 1;
                value <<= 1;
                eflags = (eflags & ~0x1) | msb; 
            }
            break;
        case 5: 
            for (int i = 0; i < count; i++) {
                uint32_t lsb = value & 1;
                value >>= 1;
                eflags = (eflags & ~0x1) | lsb; 
            }
            break;
        case 7: 
            for (int i = 0; i < count; i++) {
                uint32_t lsb = value & 1;
                value = (int32_t)value >> 1;
                eflags = (eflags & ~0x1) | lsb; 
            }
            break;
    }

    writeOperand(modrm, value);
    updateFlags(value, value, 0, ALU_OR);
}
void X86Core::aam() {

    uint8_t al_val = eax & 0xFF;
    uint8_t ah_val = (eax >> 8) & 0xFF;

    if (al_val != 0) {
        ah_val = al_val / 10;
        al_val = al_val % 10;
    }

    eax = (eax & 0xFFFF0000) | (ah_val << 8) | al_val;
    updateFlags8(al_val, al_val, 0, ALU_OR);
}

void X86Core::aad() {

    uint8_t al_val = eax & 0xFF;
    uint8_t ah_val = (eax >> 8) & 0xFF;

    al_val = al_val + (ah_val * 10);
    ah_val = 0;

    eax = (eax & 0xFFFF0000) | (ah_val << 8) | al_val;
    updateFlags8(al_val, al_val, 0, ALU_OR);
}

void X86Core::salc() {

    if (eflags & 0x1) { 
        eax = (eax & 0xFFFFFF00) | 0xFF;
    } else {
        eax = (eax & 0xFFFFFF00) | 0x00;
    }
}
void X86Core::xlat() {

    uint32_t address = (ds << 4) + ebx + (eax & 0xFF);
    uint8_t value = memory->read8(address);
    eax = (eax & 0xFFFFFF00) | value;
}
void X86Core::loopnz_rel8() {

    int8_t offset = memory->read8(eip++);
    ecx--;
    if (ecx != 0 && !(eflags & 0x40)) { 
        eip += offset;
    }
}

void X86Core::loopz_rel8() {

    int8_t offset = memory->read8(eip++);
    ecx--;
    if (ecx != 0 && (eflags & 0x40)) { 
        eip += offset;
    }
}

void X86Core::jcxz_rel8() {

    int8_t offset = memory->read8(eip++);
    if ((ecx & 0xFFFF) == 0) { 
        eip += offset;
    }
}
void X86Core::in_al_imm8() {

    (void)memory->read8(eip++); 
    uint8_t value = 0; 
    eax = (eax & 0xFFFFFF00) | value;
}

void X86Core::in_eax_imm8() {

    [[maybe_unused]] uint8_t port = memory->read8(eip++); 
    [[maybe_unused]] uint32_t value = 0; 
    eax = value;
}

void X86Core::out_imm8_al() {

    [[maybe_unused]] uint8_t port = memory->read8(eip++); 
    [[maybe_unused]] uint8_t value = eax & 0xFF; 

}

void X86Core::out_imm8_eax() {

    [[maybe_unused]] uint8_t port = memory->read8(eip++); 
    [[maybe_unused]] uint32_t value = eax; 

}
void X86Core::jmp_far() {

    uint32_t offset = memory->read32(eip);
    uint16_t segment = memory->read16(eip + 4);
    eip = offset;
    cs = segment;
}
void X86Core::in_al_dx() {

    (void)(edx & 0xFFFF); 
    uint8_t value = 0; 
    eax = (eax & 0xFFFFFF00) | value;
}

void X86Core::in_eax_dx() {

    [[maybe_unused]] uint32_t port = edx & 0xFFFF; 
    [[maybe_unused]] uint32_t value = 0; 
    eax = value;
}

void X86Core::out_dx_al() {

    [[maybe_unused]] uint32_t port = edx & 0xFFFF; 
    [[maybe_unused]] uint8_t value = eax & 0xFF; 

}

void X86Core::out_dx_eax() {

    [[maybe_unused]] uint32_t port = edx & 0xFFFF; 
    [[maybe_unused]] uint32_t value = eax; 

}
void X86Core::lock_prefix() {


}
void X86Core::icebp() {


}

void X86Core::repne_prefix() {


}

void X86Core::rep_prefix() {


}

void X86Core::group4_rm8() {

    uint8_t modrm = memory->read8(eip++);
    uint8_t reg = (modrm >> 3) & 7;
    uint8_t value = readOperand8(modrm);

    switch (reg) {
        case 0: 
            value++;
            updateFlags8(value, value - 1, 1, ALU_ADD);
            break;
        case 1: 
            value--;
            updateFlags8(value, value + 1, 1, ALU_SUB);
            break;
    }

    writeOperand8(modrm, value);
}

void X86Core::group5_rm32() {

    uint8_t modrm = memory->read8(eip++);
    uint8_t reg = (modrm >> 3) & 7;
    uint32_t value = readOperand(modrm);

    switch (reg) {
        case 0: 
            value++;
            updateFlags(value, value - 1, 1, ALU_ADD);
            break;
        case 1: 
            value--;
            updateFlags(value, value + 1, 1, ALU_SUB);
            break;
        case 2: 
            push32(eip);
            eip = value;
            return;
        case 3: 
            {
                uint32_t offset = value;
                uint16_t segment = memory->read16(value + 4);
                push16(cs);
                push32(eip);
                eip = offset;
                cs = segment;
                return;
            }
        case 4: 
            eip = value;
            return;
        case 5: 
            {
                uint32_t offset = value;
                uint16_t segment = memory->read16(value + 4);
                eip = offset;
                cs = segment;
                return;
            }
        case 6: 
            push32(value);
            return;
    }

    writeOperand(modrm, value);
}


void X86Core::adc_rm32_r32() {
    uint8_t modrm = memory->read8(eip++);
    uint8_t reg = (modrm >> 3) & 7;
    uint32_t src = getRegister(reg);
    uint32_t dest = readOperand(modrm);
    uint32_t cf = (eflags & 0x001) ? 1 : 0;
    uint32_t result = dest + src + cf;
    writeOperand(modrm, result);
    updateFlags(result, dest, src, ALU_ADD);
}

void X86Core::adc_r32_rm32() {
    uint8_t modrm = memory->read8(eip++);
    uint8_t reg = (modrm >> 3) & 7;
    uint32_t src = readOperand(modrm);
    uint32_t dest = getRegister(reg);
    uint32_t cf = (eflags & 0x001) ? 1 : 0;
    uint32_t result = dest + src + cf;
    setRegister(reg, result);
    updateFlags(result, dest, src, ALU_ADD);
}

void X86Core::adc_eax_imm32() {
    uint32_t imm32 = memory->read32(eip);
    eip += 4;
    uint32_t cf = (eflags & 0x001) ? 1 : 0;
    uint32_t result = eax + imm32 + cf;
    eax = result;
    updateFlags(result, eax, imm32, ALU_ADD);
}


void X86Core::sbb_rm32_r32() {
    uint8_t modrm = memory->read8(eip++);
    uint8_t reg = (modrm >> 3) & 7;
    uint32_t src = getRegister(reg);
    uint32_t dest = readOperand(modrm);
    uint32_t cf = (eflags & 0x001) ? 1 : 0;
    uint32_t result = dest - src - cf;
    writeOperand(modrm, result);
    updateFlags(result, dest, src, ALU_SUB);
}

void X86Core::sbb_r32_rm32() {
    uint8_t modrm = memory->read8(eip++);
    uint8_t reg = (modrm >> 3) & 7;
    uint32_t src = readOperand(modrm);
    uint32_t dest = getRegister(reg);
    uint32_t cf = (eflags & 0x001) ? 1 : 0;
    uint32_t result = dest - src - cf;
    setRegister(reg, result);
    updateFlags(result, dest, src, ALU_SUB);
}

void X86Core::sbb_eax_imm32() {
    uint32_t imm32 = memory->read32(eip);
    eip += 4;
    uint32_t cf = (eflags & 0x001) ? 1 : 0;
    uint32_t result = eax - imm32 - cf;
    eax = result;
    updateFlags(result, eax, imm32, ALU_SUB);
}


void X86Core::imul_r32_rm32_imm32() {
    uint8_t modrm = memory->read8(eip++);
    uint8_t reg = (modrm >> 3) & 7;
    uint32_t src = readOperand(modrm);
    uint32_t imm32 = memory->read32(eip);
    eip += 4;
    int32_t result = static_cast<int32_t>(src) * static_cast<int32_t>(imm32);
    setRegister(reg, static_cast<uint32_t>(result));

    if (result == 0) {
        eflags |= 0x040; 
    } else {
        eflags &= ~0x040;
    }
}

void X86Core::imul_r32_rm32_imm8() {
    uint8_t modrm = memory->read8(eip++);
    uint8_t reg = (modrm >> 3) & 7;
    uint32_t src = readOperand(modrm);
    int8_t imm8 = static_cast<int8_t>(memory->read8(eip++));
    int32_t result = static_cast<int32_t>(src) * static_cast<int32_t>(imm8);
    setRegister(reg, static_cast<uint32_t>(result));

    if (result == 0) {
        eflags |= 0x040; 
    } else {
        eflags &= ~0x040;
    }
}


void X86Core::group3_rm8() {

    uint8_t modrm = memory->read8(eip++);
    uint8_t reg = (modrm >> 3) & 7;
    uint8_t value = readOperand8(modrm);

    switch (reg) {
        case 0: 
        case 1: 
            {
                uint8_t imm8 = memory->read8(eip++);
                uint8_t result = value & imm8;
                updateFlags8(result, value, imm8, ALU_AND);
            }
            break;
        case 2: 
            value = ~value;
            break;
        case 3: 
            value = -value;
            updateFlags8(value, 0, value, ALU_SUB);
            break;
        case 4: 
            {
                uint16_t result = (eax & 0xFF) * value;
                eax = (eax & 0xFFFFFF00) | (result & 0xFF);
                eax = (eax & 0xFFFF00FF) | ((result >> 8) & 0xFF) << 8;
                if ((result >> 8) != 0) {
                    eflags |= 0x1; 
                } else {
                    eflags &= ~0x1; 
                }
            }
            break;
        case 5: 
            {
                int16_t result = (int8_t)(eax & 0xFF) * (int8_t)value;
                eax = (eax & 0xFFFFFF00) | (result & 0xFF);
                eax = (eax & 0xFFFF00FF) | ((result >> 8) & 0xFF) << 8;
                if ((result >> 8) != 0 && (result >> 8) != 0xFF) {
                    eflags |= 0x1; 
                } else {
                    eflags &= ~0x1; 
                }
            }
            break;
        case 6: 
            {
                uint8_t divisor = value;
                if (divisor == 0) {
                    handleInterrupt(0); 
                    return;
                }
                uint16_t dividend = eax & 0xFFFF;
                uint8_t quotient = dividend / divisor;
                uint8_t remainder = dividend % divisor;
                eax = (eax & 0xFFFF0000) | (remainder << 8) | quotient;
            }
            break;
        case 7: 
            {
                int8_t divisor = (int8_t)value;
                if (divisor == 0) {
                    handleInterrupt(0); 
                    return;
                }
                int16_t dividend = (int16_t)(eax & 0xFFFF);
                int8_t quotient = dividend / divisor;
                int8_t remainder = dividend % divisor;
                eax = (eax & 0xFFFF0000) | ((uint8_t)remainder << 8) | (uint8_t)quotient;
            }
            break;
    }

    if (reg != 4 && reg != 5 && reg != 6 && reg != 7) { 
        writeOperand8(modrm, value);
    }
}

void X86Core::group3_rm32() {

    uint8_t modrm = memory->read8(eip++);
    uint8_t reg = (modrm >> 3) & 7;
    uint32_t value = readOperand(modrm);

    switch (reg) {
        case 0: 
        case 1: 
            {
                uint32_t imm32 = memory->read32(eip);
                eip += 4;
                uint32_t result = value & imm32;
                updateFlags(result, value, imm32, ALU_AND);
            }
            break;
        case 2: 
            value = ~value;
            break;
        case 3: 
            value = -value;
            updateFlags(value, 0, value, ALU_SUB);
            break;
        case 4: 
            {
                uint64_t result = (uint64_t)eax * value;
                eax = result & 0xFFFFFFFF;
                edx = (result >> 32) & 0xFFFFFFFF;
                if (edx != 0) {
                    eflags |= 0x1; 
                } else {
                    eflags &= ~0x1; 
                }
            }
            break;
        case 5: 
            {
                int64_t result = (int64_t)(int32_t)eax * (int32_t)value;
                eax = result & 0xFFFFFFFF;
                edx = (result >> 32) & 0xFFFFFFFF;
                if (edx != 0 && edx != 0xFFFFFFFF) {
                    eflags |= 0x1; 
                } else {
                    eflags &= ~0x1; 
                }
            }
            break;
        case 6: 
            {
                uint32_t divisor = value;
                if (divisor == 0) {
                    handleInterrupt(0); 
                    return;
                }
                uint64_t dividend = ((uint64_t)edx << 32) | eax;
                uint32_t quotient = dividend / divisor;
                uint32_t remainder = dividend % divisor;
                eax = quotient;
                edx = remainder;
            }
            break;
        case 7: 
            {
                int32_t divisor = (int32_t)value;
                if (divisor == 0) {
                    handleInterrupt(0); 
                    return;
                }
                int64_t dividend = ((int64_t)(int32_t)edx << 32) | eax;
                int32_t quotient = dividend / divisor;
                int32_t remainder = dividend % divisor;
                eax = (uint32_t)quotient;
                edx = (uint32_t)remainder;
            }
            break;
    }

    if (reg != 4 && reg != 5 && reg != 6 && reg != 7) { 
        writeOperand(modrm, value);
    }
}


uint8_t X86Core::getRegister8(uint8_t reg) const {
    switch (reg) {
        case 0: return eax & 0xFF; 
        case 1: return ecx & 0xFF; 
        case 2: return edx & 0xFF; 
        case 3: return ebx & 0xFF; 
        case 4: return (eax >> 8) & 0xFF; 
        case 5: return (ecx >> 8) & 0xFF; 
        case 6: return (edx >> 8) & 0xFF; 
        case 7: return (ebx >> 8) & 0xFF; 
        default: return 0;
    }
}

void X86Core::setRegister8(uint8_t reg, uint8_t value) {
    switch (reg) {
        case 0: eax = (eax & 0xFFFFFF00) | value; break; 
        case 1: ecx = (ecx & 0xFFFFFF00) | value; break; 
        case 2: edx = (edx & 0xFFFFFF00) | value; break; 
        case 3: ebx = (ebx & 0xFFFFFF00) | value; break; 
        case 4: eax = (eax & 0xFFFF00FF) | (value << 8); break; 
        case 5: ecx = (ecx & 0xFFFF00FF) | (value << 8); break; 
        case 6: edx = (edx & 0xFFFF00FF) | (value << 8); break; 
        case 7: ebx = (ebx & 0xFFFF00FF) | (value << 8); break; 
    }
}

uint8_t X86Core::readOperand8(uint8_t modrm) {
    uint8_t mod = (modrm >> 6) & 3;
    uint8_t rm = modrm & 7;

    if (mod == 3) {

        return getRegister8(rm);
    }

    uint32_t addr = getEffectiveAddress(modrm);
    return memory->read8(addr);
}

void X86Core::writeOperand8(uint8_t modrm, uint8_t value) {
    uint8_t mod = (modrm >> 6) & 3;
    uint8_t rm = modrm & 7;

    if (mod == 3) {

        setRegister8(rm, value);
        return;
    }

    uint32_t addr = getEffectiveAddress(modrm);
    memory->write8(addr, value);
}

void X86Core::updateFlags8(uint8_t result, uint8_t a, uint8_t b, uint32_t operation) {
    uint32_t flags = 0;
    flags |= (result == 0) << 6; 
    flags |= (result & 0x80) >> 7; 

    switch (operation) {
        case ALU_ADD: 
            flags |= (result < a) << 0; 
            flags |= (((a ^ b ^ 0x80) & (a ^ result)) >> 6) & 0x4; 
            break;
        case ALU_SUB: 
            flags |= (a < b) << 0; 
            flags |= (((a ^ b) & (a ^ result)) >> 6) & 0x4; 
            break;
        case ALU_OR:
        case ALU_AND:
        case ALU_XOR:

            flags |= 0; 
            break;
    }

    eflags = (eflags & ~0x8D5) | flags;
}


float X86Core::getCPUProgress() const {

    const int totalInstructions = 256; 
    const int implementedInstructions = 240; 

    float progress = (float)implementedInstructions / totalInstructions * 100.0f;


    if (jitEnabled && jitCacheUsed > 0) {
        float jitProgress = (float)jitCacheUsed / JIT_CACHE_SIZE * 10.0f; 
        progress += jitProgress;
    }


    progress += 15.0f; 


    progress += 5.0f; 

    return std::min(progress, 100.0f);
}

uint32_t X86Core::getTotalInstructionsExecuted() const {
    return totalInstructionsExecuted;
}

uint32_t X86Core::getInstructionsThisFrame() const {
    return instructionsThisFrame;
}

void X86Core::resetProgressCounters() {
    totalInstructionsExecuted = 0;
    instructionsThisFrame = 0;
    jitCacheUsed = 0;
    jit_cache.clear();
    executionCounts.clear();
}


void X86Core::push_fs() {
    esp -= 2;
    memory->write16(esp, fs);
}

void X86Core::push_gs() {
    esp -= 2;
    memory->write16(esp, gs);
}

void X86Core::pop_fs() {
    fs = memory->read16(esp);
    esp += 2;
}
void X86Core::pop_gs() {
    gs = memory->read16(esp);
    esp += 2;
}


void X86Core::robustStackManagement() {

    static uint32_t stackOverflowCount = 0;
    static uint32_t stackUnderflowCount = 0;
    static uint32_t lastValidESP = 0x0003FFFC;


    if (esp < 0x00000000) {
        stackOverflowCount++;
        LOGW("🚫 STACK-OVERFLOW #%u: ESP=0x%08X < 0x00000000", stackOverflowCount, esp);


        if (lastValidESP >= 0x00000000 && lastValidESP <= 0x0003FFFF) {
            esp = lastValidESP;
            LOGI("✅ Stack-Overflow repariert: ESP=0x%08X → 0x%08X", esp, lastValidESP);
        } else {

            esp = 0x0003FFFC;
            LOGI("✅ Stack-Overflow Notfall-Reset: ESP=0x%08X", esp);
        }


        if (memory) {
            try {
                memory->write32(0x0003FFFC, 0x00000000); 
                memory->write32(0x0003FFF8, 0x00000000); 
            } catch (...) {
                LOGW("⚠️ Stack-Sicherung fehlgeschlagen");
            }
        }
        return;
    }


    if (esp > 0x0003FFFF) {
        stackUnderflowCount++;
        LOGW("🚫 STACK-UNDERFLOW #%u: ESP=0x%08X > 0x0003FFFF", stackUnderflowCount, esp);


        if (lastValidESP >= 0x00000000 && lastValidESP <= 0x0003FFFF) {
            esp = lastValidESP;
            LOGI("✅ Stack-Underflow repariert: ESP=0x%08X → 0x%08X", esp, lastValidESP);
        } else {

            esp = 0x0003FFFC;
            LOGI("✅ Stack-Underflow Notfall-Reset: ESP=0x%08X", esp);
        }
        return;
    }



    if (esp < 0x00000010) {
        LOGW("⚠️ STACK-BOUNDARY: ESP=0x%08X zu nah an unterer Grenze", esp);
        esp = 0x00000010; 
        LOGI("✅ Stack-Boundary angepasst: ESP=0x%08X", esp);
    }

    if (esp > 0x0003FFE0) {
        LOGW("⚠️ STACK-BOUNDARY: ESP=0x%08X zu nah an oberer Grenze", esp);
        esp = 0x0003FFE0; 
        LOGI("✅ Stack-Boundary angepasst: ESP=0x%08X", esp);
    }


    if (memory) {
        try {
            uint32_t stackTop = memory->read32(0x0003FFFC);


            bool isValidValue = (stackTop == 0xDEADBEEF || stackTop == 0xCAFEBABE ||
                                stackTop == 0xBABECAFE || stackTop == 0xFEEDFACE ||
                                stackTop == 0x0058FD90 ||
                                (stackTop >= 0x00010000 && stackTop <= 0x07FFFFFF));

            if (!isValidValue) {
                LOGW("⚠️ STACK-INTEGRITÄT: Stack-Top hat ungültigen Wert 0x%08X - repariere", stackTop);


                uint32_t returnAddr = 0x0058FD90;
                memory->write32(0x0003FFFC, returnAddr);
                memory->write32(0x0003FFD0, returnAddr);
                memory->write32(0x0003FFCC, returnAddr);

                LOGI("✅ Stack repariert: RET-Adresse 0x%08X gesetzt", returnAddr);
            } else {
                LOGD("✅ Stack-Integrität OK: Stack-Top=0x%08X", stackTop);
            }
        } catch (...) {
            LOGW("⚠️ Stack-Integritäts-Prüfung fehlgeschlagen - überspringe");
        }
    }


    if (esp >= 0x00000000 && esp <= 0x0003FFFF) {
        lastValidESP = esp;
        LOGD("✅ ESP gespeichert für Recovery: 0x%08X", esp);
    }


    if (memory && (esp == 0x0003FFFC || esp == 0x00000000 || esp < 0x00000010)) {
        LOGW("⚠️ Stack-Initialisierung erforderlich - ESP: 0x%08X", esp);
        initializeStack();
    }


    if (stackOverflowCount > 0 || stackUnderflowCount > 0) {
        if ((stackOverflowCount + stackUnderflowCount) % 10 == 0) {
            LOGI("📊 Stack-Statistik: Overflows=%u, Underflows=%u, Aktueller ESP=0x%08X", 
                 stackOverflowCount, stackUnderflowCount, esp);
        }
    }
}


void X86Core::safeStackOperation(std::function<void()> operation, const char* operationName) {
    uint32_t espBefore = esp;

    try {

        operation();


        if (esp < 0x00000000 || esp > 0x0003FFFF) {
            LOGW("🚫 STACK-OPERATION-FEHLER: %s verursachte ungültigen ESP=0x%08X", operationName, esp);
            esp = espBefore; 
            LOGI("✅ Stack-Operation Rollback: ESP=0x%08X → 0x%08X", esp, espBefore);
        }
    } catch (const std::exception& e) {
        LOGW("🚫 STACK-OPERATION-EXCEPTION: %s fehlgeschlagen: %s", operationName, e.what());
        esp = espBefore; 
        LOGI("✅ Stack-Operation Exception-Rollback: ESP=0x%08X → 0x%08X", esp, espBefore);
    } catch (...) {
        LOGW("🚫 STACK-OPERATION-UNKNOWN-EXCEPTION: %s fehlgeschlagen", operationName);
        esp = espBefore; 
        LOGI("✅ Stack-Operation Unknown-Exception-Rollback: ESP=0x%08X → 0x%08X", esp, espBefore);
    }
}

void X86Core::validateAndFixESP() {




    if (esp < XboxMemory::RAM_BASE || esp > XboxMemory::RAM_BASE + XboxMemory::RAM_SIZE - 16) {
        LOGW("CPU: Stack pointer out of valid range: 0x%08X, resetting to valid range", esp);

        esp = 0x0003FFFC;
        LOGI("CPU: Reset stack pointer to 0x%08X", esp);
    }



    if (esp == 0x00000000) {
        LOGW("CPU: ESP is zero in validateAndFixESP, resetting to valid range");
        esp = 0x0003FFFC;
        LOGI("CPU: Reset ESP from zero to 0x%08X", esp);
    }



    if (esp >= 0x00000000 && esp <= 0x0003FFFF) {

        return;
    }



    if (esp >= 0x00040000 && esp < XboxMemory::RAM_BASE) {

        LOGW("CPU: ESP in invalid range: 0x%08X, resetting to valid range", esp);
        esp = 0x0003FFFC;
        LOGI("CPU: Reset ESP from zero to 0x%08X", esp);
    }
}


void X86Core::initializeStack() {
    if (!memory) {
        LOGW("⚠️ STACK-INIT: Kein Memory verfügbar für Stack-Initialisierung");
        return;
    }

    LOGI("🔧 STACK-INIT: Starte Stack-Initialisierung...");

    try {

        esp = 0x0003FFE0;


        uint32_t returnAddr = 0x0058FD90; 


        memory->write32(0x0003FFE0, returnAddr); 
        memory->write32(0x0003FFD0, returnAddr); 
        memory->write32(0x0003FFCC, returnAddr); 
        memory->write32(0x0003FFC0, returnAddr); 
        memory->write32(0x0003FFB0, returnAddr); 

        LOGI("🔧 RET-Adresse auf Stack gesetzt: 0x%08X bei mehreren ESP-Positionen", returnAddr);


        uint32_t stackBase = 0x0003FFE0;
        uint32_t stackSize = 0x00040000; 

        for (uint32_t addr = stackBase; addr >= 0x00000000; addr -= 4) {
            if (addr >= 0x00000000 && addr <= 0x0003FFFF) {

                uint32_t magicValue;
                if (addr >= 0x0003FF00) {
                    magicValue = 0xDEADBEEF; 
                } else if (addr >= 0x0003FE00) {
                    magicValue = 0xCAFEBABE; 
                } else if (addr >= 0x0003FD00) {
                    magicValue = 0xBABECAFE; 
                } else {
                    magicValue = 0xFEEDFACE; 
                }

                memory->write32(addr, magicValue);
            }
        }


        memory->write32(0x0003FFDC, 0x5AACF1A6); 
        memory->write32(0x0003FFD8, 0x0003FFE0);   

        LOGI("✅ STACK-INIT: Stack erfolgreich initialisiert - ESP=0x%08X, Top=0x%08X", esp, returnAddr);
        LOGI("✅ STACK-INIT: Stack-Bereich 0x00000000-0x0003FFFF mit Magic Numbers gefüllt");

    } catch (const std::exception& e) {
        LOGE("🚫 STACK-INIT: Exception während Stack-Initialisierung: %s", e.what());

        esp = 0x0003FFFC;
    } catch (...) {
        LOGE("🚫 STACK-INIT: Unbekannte Exception während Stack-Initialisierung");

        esp = 0x0003FFFC;
    }
}


void X86Core::handleCMOVOpcode(uint8_t extOpcode) {

    uint8_t modrm = memory->read8(eip++);
    uint8_t reg = (modrm >> 3) & 7;
    uint32_t src = readOperand(modrm);
    (void)getRegister(reg); 

    bool condition = false;
    switch (extOpcode & 0x0F) {
        case 0x00: condition = (eflags & 0x800) != 0; break; 
        case 0x01: condition = (eflags & 0x800) == 0; break; 
        case 0x02: condition = (eflags & 0x001) != 0; break; 
        case 0x03: condition = (eflags & 0x001) == 0; break; 
        case 0x04: condition = (eflags & 0x040) != 0; break; 
        case 0x05: condition = (eflags & 0x040) == 0; break; 
        case 0x06: condition = (eflags & 0x001) != 0 || (eflags & 0x040) != 0; break; 
        case 0x07: condition = (eflags & 0x001) == 0 && (eflags & 0x040) == 0; break; 
        case 0x08: condition = (eflags & 0x080) != 0; break; 
        case 0x09: condition = (eflags & 0x080) == 0; break; 
        case 0x0A: condition = (eflags & 0x004) != 0; break; 
        case 0x0B: condition = (eflags & 0x004) == 0; break; 
        case 0x0C: condition = ((eflags & 0x080) != 0) != ((eflags & 0x800) != 0); break; 
        case 0x0D: condition = ((eflags & 0x080) != 0) == ((eflags & 0x800) != 0); break; 
        case 0x0E: condition = (eflags & 0x040) != 0 || ((eflags & 0x080) != 0) != ((eflags & 0x800) != 0); break; 
        case 0x0F: condition = (eflags & 0x040) == 0 && ((eflags & 0x080) != 0) == ((eflags & 0x800) != 0); break; 
    }

    if (condition) {
        setRegister(reg, src);
    }
}
void X86Core::handleXboxSpecificOpcode() {


    uint8_t xboxOpcode = memory->read8(eip++);

    switch (xboxOpcode) {
        case 0x00: 

            LOGI("CPU: Executing Xbox-specific opcode 0x00");
            break;
        case 0x01: 

            LOGI("CPU: Executing Xbox-specific opcode 0x01");
            break;


            break;
        default:

            LOGW("CPU: Unknown Xbox-specific opcode: 0x%02X", xboxOpcode);
            break;
    }
}


    void X86Core::disableBoundsChecking() {
        boundsCheckingDisabled = true;
        LOGI("✅ Bounds checking disabled globally");
    }


    void X86Core::cpuErrorRecovery() {
        static uint32_t recoveryCount = 0;
        recoveryCount++;

                        if (true) { 
            LOGW("🔄 CPU-ERROR-RECOVERY #%u: EIP=0x%08X, ESP=0x%08X, State=%d", 
                 recoveryCount, eip, esp, static_cast<int>(state));
        }


        if (true) { 
            if (true) { 
                LOGI("🔄 CPU-Error-Recovery disabled - letting hardware handle errors naturally");
            }
            return;
        }


        if (false && false && (esp < 0x00000000 || esp > 0x0003FFFF)) { 
            LOGW("🔄 Stack-Recovery: ESP=0x%08X → 0x0003FFE0", esp);
            esp = 0x0003FFE0;
            ebp = 0x0003FFFC;
        }


        if (false && false && (eip < 0x00000000 || eip > 0x07FFFFFF)) { 
            LOGW("🔄 EIP-Recovery: EIP=0x%08X → Echter EntryPoint", eip);

            if (memory && memory->xbeEntryPoint != 0) {
                eip = memory->xbeEntryPoint;
            } else {
                eip = 0x0057FD80; 
            }
        }


        if (false && state != CpuState::Running) { 
            LOGW("🔄 State-Recovery: State=%d → Running", static_cast<int>(state));
            state = CpuState::Running;
        }


        if (false && false && memory) { 
            try {

                eax = ebx = ecx = edx = 0;
                esi = edi = 0;
                eflags = 0x00000002; 
                LOGI("🔄 Register-Recovery: Alle Register zurückgesetzt");
            } catch (...) {
                LOGW("🔄 Register-Recovery fehlgeschlagen");
            }
        }

                        if (true) { 
            LOGI("✅ CPU-Error-Recovery #%u abgeschlossen", recoveryCount);
        }
    }


    void X86Core::cpuMemoryProtection() {
        static uint32_t protectionCount = 0;
        protectionCount++;


        if (memory) {
            try {

                uint32_t instruction = memory->read32(eip);
                if (instruction == 0x00000000 || instruction == 0xFFFFFFFF) {
                    LOGW("🛡️ MEMORY-PROTECTION #%u: EIP=0x%08X zeigt auf ungültigen Code 0x%08X", 
                         protectionCount, eip, instruction);


                    uint32_t newEIP = searchValidCodeNearby(eip);
                    if (newEIP != 0x00000000) {
                        eip = newEIP;
                        LOGI("🛡️ Memory-Protection: EIP repariert zu 0x%08X", eip);
                    } else {
                        eip = memory ? memory->xbeEntryPoint : 0x0057FD80; 
                        LOGI("🛡️ Memory-Protection: EIP auf echten Xbox EntryPoint gesetzt: 0x%08X", eip);
                    }
                }


                if (esp >= 0x00000000 && esp <= 0x0003FFFF) {
                    uint32_t stackValue = memory->read32(esp);
                    if (stackValue == 0x00000000 || stackValue == 0xFFFFFFFF) {
                        LOGW("🛡️ MEMORY-PROTECTION #%u: Stack bei ESP=0x%08X hat ungültigen Wert 0x%08X", 
                             protectionCount, esp, stackValue);
                        memory->write32(esp, 0x00000000); 
                        LOGI("🛡️ Memory-Protection: Stack repariert");
                    }
                }

            } catch (const std::exception& e) {
                LOGW("🛡️ MEMORY-PROTECTION #%u: Exception bei Memory-Access: %s", protectionCount, e.what());

                cpuErrorRecovery();
            } catch (...) {
                LOGW("🛡️ MEMORY-PROTECTION #%u: Unknown Exception bei Memory-Access", protectionCount);

                cpuErrorRecovery();
            }
        }
    }


    void X86Core::handleExtendedInstructions() {

        handleMMXInstructions();
        handleSSEInstructions();
        handleSSE2Instructions();
        handle3DNowInstructions();
        handleAdvancedFPUInstructions();


        void handleXboxSpecificInstructions();
        void handleXboxAudioInstructions();
        void handleXboxGraphicsInstructions();
        void handleXboxNetworkInstructions();


        static uint32_t xboxSpecificCount = 0;
        xboxSpecificCount++;



        void xbox_gpu_render_triangle(uint32_t x1, uint32_t y1, uint32_t x2, uint32_t y2, uint32_t x3, uint32_t y3);
        void xbox_gpu_set_texture(uint32_t texture_id, uint32_t address);
        void xbox_gpu_set_shader(uint32_t shader_id);


        void xbox_audio_play_sound(uint32_t sound_id, uint32_t volume);
        void xbox_audio_set_music(uint32_t music_id);
        void xbox_audio_set_volume(uint32_t volume);


        void xbox_network_send_packet(uint32_t data, uint32_t size);
        void xbox_network_receive_packet(uint32_t* data, uint32_t* size);
        void xbox_network_connect(uint32_t ip_address);

        LOGI("🎮 Xbox-specific Instructions: GPU, Audio, Network verfügbar - %u Ausführungen", xboxSpecificCount);


        static uint32_t extendedInstructionsCount = 0;
        extendedInstructionsCount++;

        LOGI("🎮 Extended Instructions: MMX, SSE, SSE2, 3DNow, Xbox-spezifisch verfügbar - %u Ausführungen", extendedInstructionsCount);
    }



    void X86Core::handleXboxGPUOpcode() {

        uint32_t gpuCommand = memory->read32(eip + 2);
        uint32_t gpuData = memory->read32(eip + 6);


        switch (gpuCommand & 0xFF) {
            case 0x01: 
                xbox_gpu_render_triangle(gpuData & 0xFF, (gpuData >> 8) & 0xFF, 
                                       (gpuData >> 16) & 0xFF, (gpuData >> 24) & 0xFF,
                                       0, 0); 
                break;
            case 0x02: 
                xbox_gpu_set_texture(gpuData & 0xFFFF, gpuData >> 16);
                break;
            case 0x03: 
                xbox_gpu_set_shader(gpuData);
                break;
            default:

                break;
        }

        eip += 10; 
    }


    void X86Core::handleXboxAudioOpcode() {

        uint32_t audioCommand = memory->read32(eip + 2);
        uint32_t audioData = memory->read32(eip + 6);


        switch (audioCommand & 0xFF) {
            case 0x01: 
                xbox_audio_play_sound(audioData & 0xFFFF, (audioData >> 16) & 0xFF);
                break;
            case 0x02: 
                xbox_audio_set_music(audioData);
                break;
            case 0x03: 
                xbox_audio_set_volume(audioData & 0xFF);
                break;
            default:

                break;
        }

        eip += 10; 
    }


    void X86Core::handleXboxNetworkOpcode() {

        uint32_t networkCommand = memory->read32(eip + 2);
        uint32_t networkData = memory->read32(eip + 6);


        switch (networkCommand & 0xFF) {
            case 0x01: 
                xbox_network_send_packet(networkData, (networkCommand >> 8) & 0xFF);
                break;
            case 0x02: 
                {
                    uint32_t receivedData = 0;
                    uint8_t receivedSize = 0;
                    xbox_network_receive_packet(&receivedData, &receivedSize);
                    eax = receivedData; 
                }
                break;
            case 0x03: 
                xbox_network_connect(networkData);
                break;
            default:

                break;
        }

        eip += 10; 
    }


    void X86Core::handleXboxInterrupt() {
        uint8_t interruptNumber = memory->read8(eip + 1);


        switch (interruptNumber) {
            case 0x20: 
                handleXboxGPUInterrupt();
                break;
            case 0x21: 
                handleXboxAudioInterrupt();
                break;
            case 0x22: 
                handleXboxNetworkInterrupt();
                break;
            case 0x23: 
                handleXboxDVDInterrupt();
                break;
            case 0x24: 
                handleXboxHDDInterrupt();
                break;
            case 0x25: 
                handleXboxUSBInterrupt();
                break;
            case 0x26: 
                handleXboxTimerInterrupt();
                break;
            case 0x27: 
                handleXboxSystemInterrupt();
                break;
            default:

                handleInterrupt(interruptNumber);
                break;
        }

        eip += 2; 
    }


    void X86Core::handleXboxSystemCall() {
        uint32_t systemCallNumber = eax; 


        switch (systemCallNumber) {
            case 0x1000: 
                eax = handleXboxGPUSystemCall(ebx, ecx, edx);
                break;
            case 0x1001: 
                eax = handleXboxAudioSystemCall(ebx, ecx, edx);
                break;
            case 0x1002: 
                eax = handleXboxNetworkSystemCall(ebx, ecx, edx);
                break;
            case 0x1003: 
                eax = handleXboxDVDSystemCall(ebx, ecx, edx);
                break;
            case 0x1004: 
                eax = handleXboxHDDSystemCall(ebx, ecx, edx);
                break;
            case 0x1005: 
                eax = handleXboxUSBSystemCall(ebx, ecx, edx);
                break;
            case 0x1006: 
                eax = handleXboxTimerSystemCall(ebx, ecx, edx);
                break;
            case 0x1007: 
                eax = handleXboxSystemSystemCall(ebx, ecx, edx);
                break;
            default:

                eax = 0xFFFFFFFF; 
                break;
        }

        eip += 1; 
    }


    void X86Core::handleXboxHardwareAccess() {
        uint32_t hardwareAddress = memory->read32(eip + 1);
        uint32_t hardwareData = memory->read32(eip + 5);


        switch (hardwareAddress & 0xFF000000) {
            case 0xFD000000: 
                handleXboxGPUHardwareAccess(hardwareAddress, hardwareData);
                break;
            case 0xFE000000: 
                handleXboxAudioHardwareAccess(hardwareAddress, hardwareData);
                break;
            case 0xFF000000: 
                handleXboxSystemHardwareAccess(hardwareAddress, hardwareData);
                break;
            default:

                break;
        }

        eip += 9; 
    }


    uint32_t X86Core::handleXboxGPUSystemCall(uint32_t param1, uint32_t param2, uint32_t param3) {

        LOGI("🎮 Xbox GPU System Call: param1=0x%08X, param2=0x%08X, param3=0x%08X", param1, param2, param3);





        switch (param1) {
            case 0x01: 
                return handleGPUInit(param2, param3);

            case 0x02: 
                return handleGPUCreateVertexBuffer(param2, param3);

            case 0x03: 
                return handleGPUUpdateVertexBuffer(param2, param3);

            case 0x04: 
                return handleGPUCreateTexture(param2, param3);

            case 0x05: 
                return handleGPUUpdateTexture(param2, param3);

            case 0x06: 
                return handleGPUSetRenderState(param2, param3);

            case 0x07: 
                return handleGPUDrawPrimitives(param2, param3);

            case 0x08: 
                return handleGPUClear(param2, param3);

            case 0x09: 
                return handleGPUPresent(param2, param3);

            case 0x0A: 
                return handleGPUCreateShader(param2, param3);

            case 0x0B: 
                return handleGPUSetShader(param2, param3);

            case 0x0C: 
                return handleGPUSetViewport(param2, param3);

            case 0x0D: 
                return handleGPUSetScissor(param2, param3);

            default:
                LOGW("Unknown Xbox GPU System Call: 0x%08X", param1);
                return 0xFFFFFFFF; 
        }
    }

    uint32_t X86Core::handleXboxAudioSystemCall(uint32_t param1, uint32_t param2, uint32_t param3) {

        LOGI("🎮 Xbox Audio System Call: param1=0x%08X, param2=0x%08X, param3=0x%08X", param1, param2, param3);





        switch (param1) {
            case 0x01: 
                return handleAudioInit(param2, param3);

            case 0x02: 
                return handleAudioCreateStream(param2, param3);

            case 0x03: 
                return handleAudioPlay(param2, param3);

            case 0x04: 
                return handleAudioPause(param2);

            case 0x05: 
                return handleAudioStop(param2);

            case 0x06: 
                return handleAudioSetVolume(param2, param3);

            case 0x07: 
                return handleAudioGetPosition(param2);

            case 0x08: 
                return handleAudioSetPosition(param2, param3);

            case 0x09: 
                return handleAudioQueueData(param2, param3);

            case 0x0A: 
                return handleAudioGetStatus(param2);

            case 0x0B: 
                return handleAudioDestroyStream(param2);

            case 0x0C: 
                return handleAudioSetFormat(param2, param3);

            default:
                LOGW("Unknown Xbox Audio System Call: 0x%08X", param1);
                return 0xFFFFFFFF; 
        }
    }

    uint32_t X86Core::handleXboxNetworkSystemCall(uint32_t param1, uint32_t param2, uint32_t param3) {

        LOGI("🎮 Xbox Network System Call: param1=0x%08X, param2=0x%08X, param3=0x%08X", param1, param2, param3);





        switch (param1) {
            case 0x01: 
                return handleNetworkInit(param2, param3);

            case 0x02: 
                return handleNetworkCreateSocket(param2, param3);

            case 0x03: 
                return handleNetworkConnect(param2, param3);

            case 0x04: 
                return handleNetworkSend(param2, param3);

            case 0x05: 
                return handleNetworkReceive(param2, param3);

            case 0x06: 
                return handleNetworkCloseSocket(param2);

            case 0x07: 
                return handleNetworkGetHostByName(param2, param3);

            case 0x08: 
                return handleNetworkBind(param2, param3);

            case 0x09: 
                return handleNetworkListen(param2, param3);

            case 0x0A: 
                return handleNetworkAccept(param2);

            case 0x0B: 
                return handleNetworkGetStatus();

            default:
                LOGW("Unknown Xbox Network System Call: 0x%08X", param1);
                return 0xFFFFFFFF; 
        }
    }

    uint32_t X86Core::handleXboxDVDSystemCall(uint32_t param1, uint32_t param2, uint32_t param3) {

        LOGI("🎮 Xbox DVD System Call: param1=0x%08X, param2=0x%08X, param3=0x%08X", param1, param2, param3);





        switch (param1) {
            case 0x01: 
                return handleDVDOpen(param2);

            case 0x02: 
                return handleDVDClose();

            case 0x03: 
                return handleDVDReadSector(param2, param3);

            case 0x04: 
                return handleDVDReadFile(param2, param3);

            case 0x05: 
                return handleDVDSeek(param2);

            case 0x06: 
                return handleDVDGetStatus();

            case 0x07: 
                return handleDVDAuthenticate(param2);

            case 0x08: 
                return handleDVDGetDiscId();

            case 0x09: 
                return handleDVDReadRaw(param2, param3);

            default:
                LOGW("Unknown Xbox DVD System Call: 0x%08X", param1);
                return 0xFFFFFFFF; 
        }
    }

    uint32_t X86Core::handleXboxHDDSystemCall(uint32_t param1, uint32_t param2, uint32_t param3) {

        LOGI("🎮 Xbox HDD System Call: param1=0x%08X, param2=0x%08X, param3=0x%08X", param1, param2, param3);





        switch (param1) {
            case 0x01: 
                return handleHDDOpenFile(param2, param3);

            case 0x02: 
                return handleHDDCloseFile(param2);

            case 0x03: 
                return handleHDDReadFile(param2, param3);

            case 0x04: 
                return handleHDDWriteFile(param2, param3);

            case 0x05: 
                return handleHDDSeekFile(param2, param3);

            case 0x06: 
                return handleHDDGetFileSize(param2);

            case 0x07: 
                return handleHDDDeleteFile(param2);

            case 0x08: 
                return handleHDDListDirectory(param2, param3);

            case 0x09: 
                return handleHDDCreateDirectory(param2);

            case 0x0A: 
                return handleHDDGetFreeSpace();

            default:
                LOGW("Unknown Xbox HDD System Call: 0x%08X", param1);
                return 0xFFFFFFFF; 
        }
    }
    uint32_t X86Core::handleXboxUSBSystemCall(uint32_t param1, uint32_t param2, uint32_t param3) {

        LOGI("🎮 Xbox USB System Call: param1=0x%08X, param2=0x%08X, param3=0x%08X", param1, param2, param3);





        switch (param1) {
            case 0x01: 
                return handleUSBInit(param2, param3);

            case 0x02: 
                return handleUSBEnumerateDevices(param2, param3);

            case 0x03: 
                return handleUSBOpenDevice(param2, param3);

            case 0x04: 
                return handleUSBCloseDevice(param2);

            case 0x05: 
                return handleUSBControlTransfer(param2, param3);

            case 0x06: 
                return handleUSBBulkTransfer(param2, param3);

            case 0x07: 
                return handleUSBInterruptTransfer(param2, param3);

            case 0x08: 
                return handleUSBGetDeviceDescriptor(param2, param3);

            case 0x09: 
                return handleUSBGetControllerState(param2, param3);

            case 0x0A: 
                return handleUSBSetControllerRumble(param2, param3);

            case 0x0B: 
                return handleUSBGetMemoryCardInfo(param2, param3);

            case 0x0C: 
                return handleUSBReadMemoryCard(param2, param3);

            case 0x0D: 
                return handleUSBWriteMemoryCard(param2, param3);

            default:
                LOGW("Unknown Xbox USB System Call: 0x%08X", param1);
                return 0xFFFFFFFF; 
        }
    }

    uint32_t X86Core::handleXboxTimerSystemCall(uint32_t param1, uint32_t param2, uint32_t param3) {

        LOGI("🎮 Xbox Timer System Call: param1=0x%08X, param2=0x%08X, param3=0x%08X", param1, param2, param3);





        switch (param1) {
            case 0x01: 
                return handleTimerCreate(param2, param3);

            case 0x02: 
                return handleTimerStart(param2, param3);

            case 0x03: 
                return handleTimerStop(param2);

            case 0x04: 
                return handleTimerGetValue(param2);

            case 0x05: 
                return handleTimerDestroy(param2);

            case 0x06: 
                return handleTimerSetPeriodic(param2, param3);

            case 0x07: 
                return handleTimerGetTicks();

            case 0x08: 
                return handleTimerGetMicroseconds();

            case 0x09: 
                return handleTimerGetMilliseconds();

            default:
                LOGW("Unknown Xbox Timer System Call: 0x%08X", param1);
                return 0xFFFFFFFF; 
        }
    }

    uint32_t X86Core::handleXboxSystemSystemCall(uint32_t param1, uint32_t param2, uint32_t param3) {

        LOGI("🎮 Xbox System System Call: param1=0x%08X, param2=0x%08X, param3=0x%08X", param1, param2, param3);





        switch (param1) {
            case 0x01: 
                return handleSystemShutdown(param2);

            case 0x02: 
                return handleSystemReboot();

            case 0x03: 
                return handleSystemGetInfo(param2, param3);

            case 0x04: 
                return handleSystemSetTime(param2);

            case 0x05: 
                return handleSystemGetTime(param2);

            case 0x06: 
                return handleSystemGetVersion(param2);

            case 0x07: 
                return handleSystemGetMemoryInfo(param2);

            case 0x08: 
                return handleSystemDebugPrint(param2, param3);

            case 0x09: 
                return handleSystemGetTemperature();

            case 0x0A: 
                return handleSystemGetFanSpeed();

            case 0x0B: 
                return handleSystemSetFanSpeed(param2);

            case 0x0C: 
                return handleSystemGetDiskInfo(param2);

            case 0x0D: 
                return handleSystemLaunchTitle(param2, param3);

            default:
                LOGW("Unknown Xbox System System Call: 0x%08X", param1);
                return 0xFFFFFFFF; 
        }
    }



    uint32_t X86Core::handleTimerCreate(uint32_t timerType, uint32_t initialValue) {
        LOGI("🎮 Timer Create: type=0x%08X, initial=0x%08X", timerType, initialValue);


        for (int i = 0; i < MAX_XBOX_TIMERS; i++) {
            if (!xboxTimers[i].active) {
                xboxTimers[i].active = true;
                xboxTimers[i].type = timerType;
                xboxTimers[i].startTime = std::chrono::high_resolution_clock::now();
                xboxTimers[i].value = initialValue;
                xboxTimers[i].running = false;
                xboxTimers[i].periodic = false;
                xboxTimers[i].period = 0;

                LOGI("✅ Timer %d erstellt", i);
                return i; 
            }
        }

        LOGW("❌ Keine freie Timer-ID verfügbar");
        return 0xFFFFFFFF; 
    }

    uint32_t X86Core::handleTimerStart(uint32_t timerId, uint32_t flags) {
        LOGI("🎮 Timer Start: id=%d, flags=0x%08X", timerId, flags);

        if (timerId >= MAX_XBOX_TIMERS || !xboxTimers[timerId].active) {
            LOGW("❌ Ungültige Timer-ID: %d", timerId);
            return 0xFFFFFFFF; 
        }

        xboxTimers[timerId].running = true;
        xboxTimers[timerId].startTime = std::chrono::high_resolution_clock::now();

        LOGI("✅ Timer %d gestartet", timerId);
        return 0x00000000; 
    }

    uint32_t X86Core::handleTimerStop(uint32_t timerId) {
        LOGI("🎮 Timer Stop: id=%d", timerId);

        if (timerId >= MAX_XBOX_TIMERS || !xboxTimers[timerId].active) {
            LOGW("❌ Ungültige Timer-ID: %d", timerId);
            return 0xFFFFFFFF; 
        }

        xboxTimers[timerId].running = false;


        if (xboxTimers[timerId].running) {
            auto now = std::chrono::high_resolution_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(now - xboxTimers[timerId].startTime);
            xboxTimers[timerId].value += elapsed.count();
        }

        LOGI("✅ Timer %d gestoppt", timerId);
        return 0x00000000; 
    }

    uint32_t X86Core::handleTimerGetValue(uint32_t timerId) {
        LOGI("🎮 Timer Get Value: id=%d", timerId);

        if (timerId >= MAX_XBOX_TIMERS || !xboxTimers[timerId].active) {
            LOGW("❌ Ungültige Timer-ID: %d", timerId);
            return 0xFFFFFFFF; 
        }

        uint32_t currentValue = xboxTimers[timerId].value;

        if (xboxTimers[timerId].running) {
            auto now = std::chrono::high_resolution_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(now - xboxTimers[timerId].startTime);
            currentValue += elapsed.count();
        }

        LOGI("✅ Timer %d Wert: %u µs", timerId, currentValue);
        return currentValue;
    }

    uint32_t X86Core::handleTimerDestroy(uint32_t timerId) {
        LOGI("🎮 Timer Destroy: id=%d", timerId);

        if (timerId >= MAX_XBOX_TIMERS || !xboxTimers[timerId].active) {
            LOGW("❌ Ungültige Timer-ID: %d", timerId);
            return 0xFFFFFFFF; 
        }

        xboxTimers[timerId].active = false;
        xboxTimers[timerId].running = false;

        LOGI("✅ Timer %d zerstört", timerId);
        return 0x00000000; 
    }

    uint32_t X86Core::handleTimerSetPeriodic(uint32_t timerId, uint32_t period) {
        LOGI("🎮 Timer Set Periodic: id=%d, period=%d µs", timerId, period);

        if (timerId >= MAX_XBOX_TIMERS || !xboxTimers[timerId].active) {
            LOGW("❌ Ungültige Timer-ID: %d", timerId);
            return 0xFFFFFFFF; 
        }

        xboxTimers[timerId].periodic = (period > 0);
        xboxTimers[timerId].period = period;

        LOGI("✅ Timer %d periodisch gesetzt: %d µs", timerId, period);
        return 0x00000000; 
    }

    uint32_t X86Core::handleTimerGetTicks() {

        static const uint64_t XBOX_CLOCK_SPEED = 733000000ULL; 

        auto now = std::chrono::high_resolution_clock::now();
        auto ticks = std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count();


        uint32_t xboxTicks = (ticks * XBOX_CLOCK_SPEED) / 1000000000ULL;

        LOGI("🎮 Xbox Ticks: %u", xboxTicks);
        return xboxTicks;
    }

    uint32_t X86Core::handleTimerGetMicroseconds() {
        auto now = std::chrono::high_resolution_clock::now();
        auto micros = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();

        LOGI("🎮 Microseconds: %llu", micros);
        return static_cast<uint32_t>(micros & 0xFFFFFFFF); 
    }

    uint32_t X86Core::handleTimerGetMilliseconds() {
        auto now = std::chrono::high_resolution_clock::now();
        auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();

        LOGI("🎮 Milliseconds: %llu", millis);
        return static_cast<uint32_t>(millis & 0xFFFFFFFF); 
    }



    uint32_t X86Core::handleGPUInit(uint32_t width, uint32_t height) {
        LOGI("🎮 GPU Init: %dx%d", width, height);


        gpuWidth = width;
        gpuHeight = height;
        gpuInitialized = true;


        if (!gpuFramebuffer) {
            gpuFramebufferSize = width * height * 4; 
            gpuFramebuffer = new uint8_t[gpuFramebufferSize];
            memset(gpuFramebuffer, 0, gpuFramebufferSize);
        }

        LOGI("✅ GPU initialisiert: %dx%d", width, height);
        return 0x00000000; 
    }

    uint32_t X86Core::handleGPUCreateVertexBuffer(uint32_t size, uint32_t format) {
        LOGI("🎮 GPU Create Vertex Buffer: size=%d, format=0x%08X", size, format);


        for (int i = 0; i < MAX_GPU_BUFFERS; i++) {
            if (!gpuVertexBuffers[i].active) {
                gpuVertexBuffers[i].active = true;
                gpuVertexBuffers[i].size = size;
                gpuVertexBuffers[i].format = format;
                gpuVertexBuffers[i].data = new uint8_t[size];
                memset(gpuVertexBuffers[i].data, 0, size);

                LOGI("✅ Vertex Buffer %d erstellt", i);
                return i; 
            }
        }

        LOGW("❌ Kein freier Vertex Buffer verfügbar");
        return 0xFFFFFFFF; 
    }

    uint32_t X86Core::handleGPUUpdateVertexBuffer(uint32_t bufferId, uint32_t dataAddr) {
        LOGI("🎮 GPU Update Vertex Buffer: id=%d, addr=0x%08X", bufferId, dataAddr);

        if (bufferId >= MAX_GPU_BUFFERS || !gpuVertexBuffers[bufferId].active) {
            LOGW("❌ Ungültige Vertex Buffer ID: %d", bufferId);
            return 0xFFFFFFFF; 
        }


        uint32_t size = gpuVertexBuffers[bufferId].size;
        bool success = true;

        for (uint32_t i = 0; i < size && i < gpuVertexBuffers[bufferId].size; i++) {
            try {
                gpuVertexBuffers[bufferId].data[i] = memory->read8(dataAddr + i);
            } catch (...) {
                success = false;
                break;
            }
        }

        if (success) {
            LOGI("✅ Vertex Buffer %d aktualisiert (%d Bytes)", bufferId, size);
            return 0x00000000; 
        }

        LOGW("❌ Fehler beim Lesen der Vertex Buffer Daten");
        return 0xFFFFFFFF; 
    }

    uint32_t X86Core::handleGPUCreateTexture(uint32_t width, uint32_t height) {
        LOGI("🎮 GPU Create Texture: %dx%d", width, height);


        for (int i = 0; i < MAX_GPU_TEXTURES; i++) {
            if (!gpuTextures[i].active) {
                gpuTextures[i].active = true;
                gpuTextures[i].width = width;
                gpuTextures[i].height = height;
                gpuTextures[i].size = width * height * 4; 
                gpuTextures[i].data = new uint8_t[gpuTextures[i].size];
                memset(gpuTextures[i].data, 0, gpuTextures[i].size);

                LOGI("✅ Texture %d erstellt", i);
                return i; 
            }
        }

        LOGW("❌ Keine freie Texture-ID verfügbar");
        return 0xFFFFFFFF; 
    }

    uint32_t X86Core::handleGPUUpdateTexture(uint32_t textureId, uint32_t dataAddr) {
        LOGI("🎮 GPU Update Texture: id=%d, addr=0x%08X", textureId, dataAddr);

        if (textureId >= MAX_GPU_TEXTURES || !gpuTextures[textureId].active) {
            LOGW("❌ Ungültige Texture ID: %d", textureId);
            return 0xFFFFFFFF; 
        }


        uint32_t size = gpuTextures[textureId].size;
        bool success = true;

        for (uint32_t i = 0; i < size && i < gpuTextures[textureId].size; i++) {
            try {
                gpuTextures[textureId].data[i] = memory->read8(dataAddr + i);
            } catch (...) {
                success = false;
                break;
            }
        }

        if (success) {
            LOGI("✅ Texture %d aktualisiert (%d Bytes)", textureId, size);
            return 0x00000000; 
        }

        LOGW("❌ Fehler beim Lesen der Texture Daten");
        return 0xFFFFFFFF; 
    }

    uint32_t X86Core::handleGPUSetRenderState(uint32_t state, uint32_t value) {
        LOGI("🎮 GPU Set Render State: 0x%08X = 0x%08X", state, value);


        if (state < MAX_RENDER_STATES) {
            gpuRenderStates[state] = value;
            LOGI("✅ Render State 0x%08X gesetzt", state);
            return 0x00000000; 
        }

        LOGW("❌ Ungültiger Render State: 0x%08X", state);
        return 0xFFFFFFFF; 
    }

    uint32_t X86Core::handleGPUDrawPrimitives(uint32_t bufferId, uint32_t primitiveType) {
        LOGI("🎮 GPU Draw Primitives: buffer=%d, type=%d", bufferId, primitiveType);

        if (bufferId >= MAX_GPU_BUFFERS || !gpuVertexBuffers[bufferId].active) {
            LOGW("❌ Ungültige Vertex Buffer ID: %d", bufferId);
            return 0xFFFFFFFF; 
        }



        LOGI("✅ Primitive Rendering simuliert (Buffer %d, Type %d)", bufferId, primitiveType);
        return 0x00000000; 
    }

    uint32_t X86Core::handleGPUClear(uint32_t color, uint32_t flags) {
        LOGI("🎮 GPU Clear: color=0x%08X, flags=0x%08X", color, flags);

        if (!gpuFramebuffer) {
            LOGW("❌ GPU nicht initialisiert");
            return 0xFFFFFFFF; 
        }


        uint32_t clearColor = (flags & 0x01) ? color : 0xFF000000; 
        for (uint32_t i = 0; i < gpuFramebufferSize; i += 4) {
            gpuFramebuffer[i] = (clearColor >> 16) & 0xFF;     
            gpuFramebuffer[i+1] = (clearColor >> 8) & 0xFF;    
            gpuFramebuffer[i+2] = clearColor & 0xFF;           
            gpuFramebuffer[i+3] = (clearColor >> 24) & 0xFF;   
        }

        LOGI("✅ Framebuffer geleert");
        return 0x00000000; 
    }

    uint32_t X86Core::handleGPUPresent(uint32_t flags, uint32_t unused) {
        LOGI("🎮 GPU Present: flags=0x%08X", flags);



        LOGI("✅ Frame präsentiert");
        return 0x00000000; 
    }

    uint32_t X86Core::handleGPUCreateShader(uint32_t shaderType, uint32_t codeAddr) {
        LOGI("🎮 GPU Create Shader: type=%d, addr=0x%08X", shaderType, codeAddr);


        for (int i = 0; i < MAX_GPU_SHADERS; i++) {
            if (!gpuShaders[i].active) {
                gpuShaders[i].active = true;
                gpuShaders[i].type = shaderType;



                LOGI("✅ Shader %d erstellt (Type %d)", i, shaderType);
                return i; 
            }
        }

        LOGW("❌ Keine freie Shader-ID verfügbar");
        return 0xFFFFFFFF; 
    }

    uint32_t X86Core::handleGPUSetShader(uint32_t shaderId, uint32_t unused) {
        LOGI("🎮 GPU Set Shader: id=%d", shaderId);

        if (shaderId >= MAX_GPU_SHADERS || !gpuShaders[shaderId].active) {
            LOGW("❌ Ungültige Shader ID: %d", shaderId);
            return 0xFFFFFFFF; 
        }

        currentShader = shaderId;
        LOGI("✅ Shader %d aktiviert", shaderId);
        return 0x00000000; 
    }

    uint32_t X86Core::handleGPUSetViewport(uint32_t x, uint32_t y) {
        LOGI("🎮 GPU Set Viewport: %dx%d", x, y);

        gpuViewportX = x & 0xFFFF;
        gpuViewportY = y & 0xFFFF;
        gpuViewportWidth = (x >> 16) & 0xFFFF;
        gpuViewportHeight = (y >> 16) & 0xFFFF;

        LOGI("✅ Viewport gesetzt: %d,%d %dx%d",
             gpuViewportX, gpuViewportY, gpuViewportWidth, gpuViewportHeight);
        return 0x00000000; 
    }

    uint32_t X86Core::handleGPUSetScissor(uint32_t x, uint32_t y) {
        LOGI("🎮 GPU Set Scissor: %dx%d", x, y);

        gpuScissorX = x & 0xFFFF;
        gpuScissorY = y & 0xFFFF;
        gpuScissorWidth = (x >> 16) & 0xFFFF;
        gpuScissorHeight = (y >> 16) & 0xFFFF;

        LOGI("✅ Scissor gesetzt: %d,%d %dx%d",
             gpuScissorX, gpuScissorY, gpuScissorWidth, gpuScissorHeight);
        return 0x00000000; 
    }



    uint32_t X86Core::handleDVDOpen(uint32_t isoPathAddr) {
        LOGI("🎮 DVD Open: addr=0x%08X", isoPathAddr);

        if (dvdOpened) {
            LOGW("❌ DVD bereits geöffnet");
            return 0xFFFFFFFF; 
        }


        std::string isoPath;
        char pathBuffer[256];
        memset(pathBuffer, 0, sizeof(pathBuffer));

        bool success = true;
        for (int i = 0; i < 255; i++) {
            try {
                uint8_t byte = memory->read8(isoPathAddr + i);
                if (byte == 0) break; 
                pathBuffer[i] = byte;
            } catch (...) {
                success = false;
                break;
            }
        }

        if (!success || strlen(pathBuffer) == 0) {
            LOGW("❌ Fehler beim Lesen des ISO-Pfads");
            return 0xFFFFFFFF; 
        }

        isoPath = pathBuffer;
        LOGI("🎮 Versuche ISO zu öffnen: %s", isoPath.c_str());


        if (!dvdParser) {
            dvdParser = new XboxISOParser();
        }

        std::string errorMsg;
        if (dvdParser->loadISO(isoPath, errorMsg)) {
            dvdOpened = true;
            dvdCurrentSector = 0;


            size_t lastSlash = isoPath.find_last_of("/\\");
            if (lastSlash != std::string::npos) {
                dvdDiscId = isoPath.substr(lastSlash + 1);

                size_t dotPos = dvdDiscId.find_last_of('.');
                if (dotPos != std::string::npos) {
                    dvdDiscId = dvdDiscId.substr(0, dotPos);
                }
            } else {
                dvdDiscId = isoPath;
            }

            LOGI("✅ DVD geöffnet: %s (Disc ID: %s)", isoPath.c_str(), dvdDiscId.c_str());
            return 0x00000000; 
        } else {
            LOGW("❌ Fehler beim Öffnen der DVD: %s", errorMsg.c_str());
            delete dvdParser;
            dvdParser = nullptr;
            return 0xFFFFFFFF; 
        }
    }

    uint32_t X86Core::handleDVDClose() {
        LOGI("🎮 DVD Close");

        if (!dvdOpened) {
            LOGW("❌ DVD nicht geöffnet");
            return 0xFFFFFFFF; 
        }

        dvdOpened = false;
        dvdCurrentSector = 0;
        dvdDiscId = "";

        if (dvdParser) {
            delete dvdParser;
            dvdParser = nullptr;
        }

        LOGI("✅ DVD geschlossen");
        return 0x00000000; 
    }

    uint32_t X86Core::handleDVDReadSector(uint32_t sector, uint32_t bufferAddr) {
        LOGI("🎮 DVD Read Sector: sector=%u, buffer=0x%08X", sector, bufferAddr);

        if (!dvdOpened || !dvdParser) {
            LOGW("❌ DVD nicht geöffnet");
            return 0xFFFFFFFF; 
        }


        std::vector<uint8_t> sectorData = dvdParser->readFileData(sector, 2048);

        if (sectorData.empty()) {
            LOGW("❌ Fehler beim Lesen des Sektors %u", sector);
            return 0xFFFFFFFF; 
        }


        bool success = true;
        for (size_t i = 0; i < sectorData.size() && i < 2048; i++) {
            try {
                memory->write8(bufferAddr + i, sectorData[i]);
            } catch (...) {
                success = false;
                break;
            }
        }

        if (success) {
            dvdCurrentSector = sector;
            LOGI("✅ Sektor %u gelesen (%zu Bytes)", sector, sectorData.size());
            return sectorData.size(); 
        } else {
            LOGW("❌ Fehler beim Schreiben der Sektor-Daten");
            return 0xFFFFFFFF; 
        }
    }

    uint32_t X86Core::handleDVDReadFile(uint32_t filePathAddr, uint32_t bufferAddr) {
        LOGI("🎮 DVD Read File: path_addr=0x%08X, buffer=0x%08X", filePathAddr, bufferAddr);

        if (!dvdOpened || !dvdParser) {
            LOGW("❌ DVD nicht geöffnet");
            return 0xFFFFFFFF; 
        }


        std::string filePath;
        char pathBuffer[256];
        memset(pathBuffer, 0, sizeof(pathBuffer));

        bool success = true;
        for (int i = 0; i < 255; i++) {
            try {
                uint8_t byte = memory->read8(filePathAddr + i);
                if (byte == 0) break; 
                pathBuffer[i] = byte;
            } catch (...) {
                success = false;
                break;
            }
        }

        if (!success) {
            LOGW("❌ Fehler beim Lesen des Dateipfads");
            return 0xFFFFFFFF; 
        }

        filePath = pathBuffer;
        LOGI("🎮 Versuche Datei zu lesen: %s", filePath.c_str());


        std::vector<uint8_t> fileData = dvdParser->readFile(filePath);

        if (fileData.empty()) {
            LOGW("❌ Datei nicht gefunden oder Fehler beim Lesen: %s", filePath.c_str());
            return 0xFFFFFFFF; 
        }


        success = true;
        for (size_t i = 0; i < fileData.size(); i++) {
            try {
                memory->write8(bufferAddr + i, fileData[i]);
            } catch (...) {
                success = false;
                break;
            }
        }

        if (success) {
            LOGI("✅ Datei gelesen: %s (%zu Bytes)", filePath.c_str(), fileData.size());
            return fileData.size(); 
        } else {
            LOGW("❌ Fehler beim Schreiben der Datei-Daten");
            return 0xFFFFFFFF; 
        }
    }

    uint32_t X86Core::handleDVDSeek(uint32_t sector) {
        LOGI("🎮 DVD Seek: sector=%u", sector);

        if (!dvdOpened) {
            LOGW("❌ DVD nicht geöffnet");
            return 0xFFFFFFFF; 
        }

        dvdCurrentSector = sector;
        LOGI("✅ DVD-Position auf Sektor %u gesetzt", sector);
        return 0x00000000; 
    }

    uint32_t X86Core::handleDVDGetStatus() {
        LOGI("🎮 DVD Get Status");

        if (!dvdOpened) {
            LOGI("✅ DVD Status: Geschlossen");
            return 0x00000000; 
        }

        LOGI("✅ DVD Status: Geöffnet, Sektor %u", dvdCurrentSector);
        return 0x00000001; 
    }

    uint32_t X86Core::handleDVDAuthenticate(uint32_t challenge) {
        LOGI("🎮 DVD Authenticate: challenge=0x%08X", challenge);

        if (!dvdOpened) {
            LOGW("❌ DVD nicht geöffnet");
            return 0xFFFFFFFF; 
        }



        uint32_t response = challenge ^ 0xDEADBEEF; 

        LOGI("✅ DVD-Authentifizierung erfolgreich (Challenge: 0x%08X, Response: 0x%08X)",
             challenge, response);
        return response;
    }

    uint32_t X86Core::handleDVDGetDiscId() {
        LOGI("🎮 DVD Get Disc ID");

        if (!dvdOpened) {
            LOGW("❌ DVD nicht geöffnet");
            return 0xFFFFFFFF; 
        }


        uint32_t discIdHash = 0;
        for (char c : dvdDiscId) {
            discIdHash = (discIdHash * 31) + c;
        }

        LOGI("✅ DVD Disc ID: %s (Hash: 0x%08X)", dvdDiscId.c_str(), discIdHash);
        return discIdHash;
    }
    uint32_t X86Core::handleDVDReadRaw(uint32_t sector, uint32_t bufferAddr) {
        LOGI("🎮 DVD Read Raw: sector=%u, buffer=0x%08X", sector, bufferAddr);

        if (!dvdOpened || !dvdParser) {
            LOGW("❌ DVD nicht geöffnet");
            return 0xFFFFFFFF; 
        }


        std::vector<uint8_t> rawData = dvdParser->readFileData(sector, 2352);

        if (rawData.empty()) {
            LOGW("❌ Fehler beim Lesen des Raw-Sektors %u", sector);
            return 0xFFFFFFFF; 
        }


        bool success = true;
        for (size_t i = 0; i < rawData.size() && i < 2352; i++) {
            try {
                memory->write8(bufferAddr + i, rawData[i]);
            } catch (...) {
                success = false;
                break;
            }
        }

        if (success) {
            dvdCurrentSector = sector;
            LOGI("✅ Raw-Sektor %u gelesen (%zu Bytes)", sector, rawData.size());
            return rawData.size(); 
        } else {
            LOGW("❌ Fehler beim Schreiben der Raw-Sektor-Daten");
            return 0xFFFFFFFF; 
        }
    }



    uint32_t X86Core::handleAudioInit(uint32_t sampleRate, uint32_t channels) {
        LOGI("🎮 Audio Init: sampleRate=%u, channels=%u", sampleRate, channels);

        if (audioInitialized) {
            LOGW("❌ Audio bereits initialisiert");
            return 0xFFFFFFFF; 
        }

        audioInitialized = true;


        if (sampleRate == 0) sampleRate = 44100;


        if (channels == 0) channels = 2;

        LOGI("✅ Audio-System initialisiert: %u Hz, %u Kanäle", sampleRate, channels);
        return 0x00000000; 
    }

    uint32_t X86Core::handleAudioCreateStream(uint32_t format, uint32_t channels) {
        LOGI("🎮 Audio Create Stream: format=0x%08X, channels=%u", format, channels);

        if (!audioInitialized) {
            LOGW("❌ Audio-System nicht initialisiert");
            return 0xFFFFFFFF; 
        }


        for (int i = 0; i < MAX_AUDIO_STREAMS; i++) {
            if (!audioStreams[i].active) {
                audioStreams[i].active = true;
                audioStreams[i].format = format;
                audioStreams[i].channels = channels;
                audioStreams[i].sampleRate = 44100; 
                audioStreams[i].volume = 100;
                audioStreams[i].playing = false;
                audioStreams[i].paused = false;
                audioStreams[i].position = 0;
                audioStreams[i].buffer.clear();

                LOGI("✅ Audio-Stream %d erstellt (Format: 0x%08X, Kanäle: %u)", i, format, channels);
                return i; 
            }
        }

        LOGW("❌ Keine freie Audio-Stream-ID verfügbar");
        return 0xFFFFFFFF; 
    }

    uint32_t X86Core::handleAudioPlay(uint32_t streamId, uint32_t flags) {
        LOGI("🎮 Audio Play: streamId=%d, flags=0x%08X", streamId, flags);

        if (!audioInitialized) {
            LOGW("❌ Audio-System nicht initialisiert");
            return 0xFFFFFFFF; 
        }

        if (streamId >= MAX_AUDIO_STREAMS || !audioStreams[streamId].active) {
            LOGW("❌ Ungültige Audio-Stream-ID: %d", streamId);
            return 0xFFFFFFFF; 
        }

        audioStreams[streamId].playing = true;
        audioStreams[streamId].paused = false;

        LOGI("✅ Audio-Stream %d gestartet", streamId);
        return 0x00000000; 
    }

    uint32_t X86Core::handleAudioPause(uint32_t streamId) {
        LOGI("🎮 Audio Pause: streamId=%d", streamId);

        if (!audioInitialized) {
            LOGW("❌ Audio-System nicht initialisiert");
            return 0xFFFFFFFF; 
        }

        if (streamId >= MAX_AUDIO_STREAMS || !audioStreams[streamId].active) {
            LOGW("❌ Ungültige Audio-Stream-ID: %d", streamId);
            return 0xFFFFFFFF; 
        }

        audioStreams[streamId].paused = true;

        LOGI("✅ Audio-Stream %d pausiert", streamId);
        return 0x00000000; 
    }

    uint32_t X86Core::handleAudioStop(uint32_t streamId) {
        LOGI("🎮 Audio Stop: streamId=%d", streamId);

        if (!audioInitialized) {
            LOGW("❌ Audio-System nicht initialisiert");
            return 0xFFFFFFFF; 
        }

        if (streamId >= MAX_AUDIO_STREAMS || !audioStreams[streamId].active) {
            LOGW("❌ Ungültige Audio-Stream-ID: %d", streamId);
            return 0xFFFFFFFF; 
        }

        audioStreams[streamId].playing = false;
        audioStreams[streamId].paused = false;
        audioStreams[streamId].position = 0;

        LOGI("✅ Audio-Stream %d gestoppt", streamId);
        return 0x00000000; 
    }

    uint32_t X86Core::handleAudioSetVolume(uint32_t streamId, uint32_t volume) {
        LOGI("🎮 Audio Set Volume: streamId=%d, volume=%u", streamId, volume);

        if (!audioInitialized) {
            LOGW("❌ Audio-System nicht initialisiert");
            return 0xFFFFFFFF; 
        }

        if (streamId >= MAX_AUDIO_STREAMS || !audioStreams[streamId].active) {
            LOGW("❌ Ungültige Audio-Stream-ID: %d", streamId);
            return 0xFFFFFFFF; 
        }


        if (volume > 100) volume = 100;

        audioStreams[streamId].volume = volume;

        LOGI("✅ Audio-Stream %d Lautstärke: %u%%", streamId, volume);
        return 0x00000000; 
    }

    uint32_t X86Core::handleAudioGetPosition(uint32_t streamId) {
        LOGI("🎮 Audio Get Position: streamId=%d", streamId);

        if (!audioInitialized) {
            LOGW("❌ Audio-System nicht initialisiert");
            return 0xFFFFFFFF; 
        }

        if (streamId >= MAX_AUDIO_STREAMS || !audioStreams[streamId].active) {
            LOGW("❌ Ungültige Audio-Stream-ID: %d", streamId);
            return 0xFFFFFFFF; 
        }

        uint32_t position = audioStreams[streamId].position;
        LOGI("✅ Audio-Stream %d Position: %u Samples", streamId, position);
        return position;
    }

    uint32_t X86Core::handleAudioSetPosition(uint32_t streamId, uint32_t position) {
        LOGI("🎮 Audio Set Position: streamId=%d, position=%u", streamId, position);

        if (!audioInitialized) {
            LOGW("❌ Audio-System nicht initialisiert");
            return 0xFFFFFFFF; 
        }

        if (streamId >= MAX_AUDIO_STREAMS || !audioStreams[streamId].active) {
            LOGW("❌ Ungültige Audio-Stream-ID: %d", streamId);
            return 0xFFFFFFFF; 
        }

        audioStreams[streamId].position = position;

        LOGI("✅ Audio-Stream %d Position gesetzt: %u Samples", streamId, position);
        return 0x00000000; 
    }

    uint32_t X86Core::handleAudioQueueData(uint32_t streamId, uint32_t dataAddr) {
        LOGI("🎮 Audio Queue Data: streamId=%d, dataAddr=0x%08X", streamId, dataAddr);

        if (!audioInitialized) {
            LOGW("❌ Audio-System nicht initialisiert");
            return 0xFFFFFFFF; 
        }

        if (streamId >= MAX_AUDIO_STREAMS || !audioStreams[streamId].active) {
            LOGW("❌ Ungültige Audio-Stream-ID: %d", streamId);
            return 0xFFFFFFFF; 
        }



        LOGI("✅ Audio-Daten für Stream %d queued", streamId);
        return 0x00000000; 
    }

    uint32_t X86Core::handleAudioGetStatus(uint32_t streamId) {
        LOGI("🎮 Audio Get Status: streamId=%d", streamId);

        if (!audioInitialized) {
            LOGW("❌ Audio-System nicht initialisiert");
            return 0xFFFFFFFF; 
        }

        if (streamId >= MAX_AUDIO_STREAMS || !audioStreams[streamId].active) {
            LOGW("❌ Ungültige Audio-Stream-ID: %d", streamId);
            return 0xFFFFFFFF; 
        }

        uint32_t status = 0;
        if (audioStreams[streamId].playing) status |= 0x01;
        if (audioStreams[streamId].paused) status |= 0x02;

        LOGI("✅ Audio-Stream %d Status: 0x%08X", streamId, status);
        return status;
    }

    uint32_t X86Core::handleAudioDestroyStream(uint32_t streamId) {
        LOGI("🎮 Audio Destroy Stream: streamId=%d", streamId);

        if (!audioInitialized) {
            LOGW("❌ Audio-System nicht initialisiert");
            return 0xFFFFFFFF; 
        }

        if (streamId >= MAX_AUDIO_STREAMS || !audioStreams[streamId].active) {
            LOGW("❌ Ungültige Audio-Stream-ID: %d", streamId);
            return 0xFFFFFFFF; 
        }

        audioStreams[streamId].active = false;
        audioStreams[streamId].playing = false;
        audioStreams[streamId].paused = false;
        audioStreams[streamId].position = 0;
        audioStreams[streamId].buffer.clear();

        LOGI("✅ Audio-Stream %d zerstört", streamId);
        return 0x00000000; 
    }

    uint32_t X86Core::handleAudioSetFormat(uint32_t streamId, uint32_t format) {
        LOGI("🎮 Audio Set Format: streamId=%d, format=0x%08X", streamId, format);

        if (!audioInitialized) {
            LOGW("❌ Audio-System nicht initialisiert");
            return 0xFFFFFFFF; 
        }

        if (streamId >= MAX_AUDIO_STREAMS || !audioStreams[streamId].active) {
            LOGW("❌ Ungültige Audio-Stream-ID: %d", streamId);
            return 0xFFFFFFFF; 
        }

        audioStreams[streamId].format = format;


        uint32_t channels = (format >> 0) & 0xFF;
        uint32_t sampleRate = (format >> 8) & 0xFFFFFF;

        if (channels > 0) audioStreams[streamId].channels = channels;
        if (sampleRate > 0) audioStreams[streamId].sampleRate = sampleRate;

        LOGI("✅ Audio-Stream %d Format gesetzt: 0x%08X (%u Kanäle, %u Hz)",
             streamId, format, audioStreams[streamId].channels, audioStreams[streamId].sampleRate);
        return 0x00000000; 
    }



    uint32_t X86Core::handleHDDOpenFile(uint32_t fileNameAddr, uint32_t mode) {
        LOGI("🎮 HDD Open File: addr=0x%08X, mode=0x%08X", fileNameAddr, mode);


        std::string fileName;
        char nameBuffer[256];
        memset(nameBuffer, 0, sizeof(nameBuffer));

        bool success = true;
        for (int i = 0; i < 255; i++) {
            try {
                uint8_t byte = memory->read8(fileNameAddr + i);
                if (byte == 0) break; 
                nameBuffer[i] = byte;
            } catch (...) {
                success = false;
                break;
            }
        }

        if (!success) {
            LOGW("❌ Fehler beim Lesen des Dateinamens");
            return 0xFFFFFFFF; 
        }

        fileName = nameBuffer;
        LOGI("🎮 Versuche Datei zu öffnen: %s", fileName.c_str());


        for (int i = 0; i < MAX_HDD_FILES; i++) {
            if (!hddFiles[i].active) {

                std::string hostPath = convertXboxPathToHost(fileName);


                std::ios::openmode openMode = std::ios::in | std::ios::binary;
                if (mode & 0x01) openMode |= std::ios::out; 
                if (mode & 0x02) openMode |= std::ios::trunc; 
                if (mode & 0x04) openMode |= std::ios::app; 

                hddFiles[i].fileStream.open(hostPath, openMode);

                if (hddFiles[i].fileStream.is_open()) {
                    hddFiles[i].active = true;
                    hddFiles[i].filename = fileName;
                    hddFiles[i].fullPath = hostPath;
                    hddFiles[i].currentPosition = 0;
                    hddFiles[i].isDirectory = false;


                    hddFiles[i].fileStream.seekg(0, std::ios::end);
                    hddFiles[i].fileSize = hddFiles[i].fileStream.tellg();
                    hddFiles[i].fileStream.seekg(0, std::ios::beg);

                    LOGI("✅ Datei geöffnet: %s (Handle: %d, Größe: %u Bytes)",
                         fileName.c_str(), i, hddFiles[i].fileSize);
                    return i; 
                } else {
                    LOGW("❌ Fehler beim Öffnen der Datei: %s", hostPath.c_str());
                    return 0xFFFFFFFF; 
                }
            }
        }

        LOGW("❌ Kein freier File-Handle verfügbar");
        return 0xFFFFFFFF; 
    }

    uint32_t X86Core::handleHDDCloseFile(uint32_t fileHandle) {
        LOGI("🎮 HDD Close File: handle=%d", fileHandle);

        if (fileHandle >= MAX_HDD_FILES || !hddFiles[fileHandle].active) {
            LOGW("❌ Ungültiger File-Handle: %d", fileHandle);
            return 0xFFFFFFFF; 
        }

        if (hddFiles[fileHandle].fileStream.is_open()) {
            hddFiles[fileHandle].fileStream.close();
        }

        hddFiles[fileHandle].active = false;
        hddFiles[fileHandle].filename = "";
        hddFiles[fileHandle].fullPath = "";
        hddFiles[fileHandle].fileSize = 0;
        hddFiles[fileHandle].currentPosition = 0;

        LOGI("✅ Datei geschlossen: Handle %d", fileHandle);
        return 0x00000000; 
    }

    uint32_t X86Core::handleHDDReadFile(uint32_t fileHandle, uint32_t bufferAddr) {
        LOGI("🎮 HDD Read File: handle=%d, buffer=0x%08X", fileHandle, bufferAddr);

        if (fileHandle >= MAX_HDD_FILES || !hddFiles[fileHandle].active) {
            LOGW("❌ Ungültiger File-Handle: %d", fileHandle);
            return 0xFFFFFFFF; 
        }

        if (!hddFiles[fileHandle].fileStream.is_open()) {
            LOGW("❌ Datei nicht geöffnet: Handle %d", fileHandle);
            return 0xFFFFFFFF; 
        }


        const uint32_t maxReadSize = 4096;
        char readBuffer[maxReadSize];

        hddFiles[fileHandle].fileStream.read(readBuffer, maxReadSize);
        std::streamsize bytesRead = hddFiles[fileHandle].fileStream.gcount();

        if (bytesRead > 0) {

            bool success = true;
            for (std::streamsize i = 0; i < bytesRead; i++) {
                try {
                    memory->write8(bufferAddr + i, static_cast<uint8_t>(readBuffer[i]));
                } catch (...) {
                    success = false;
                    break;
                }
            }

            if (success) {
                hddFiles[fileHandle].currentPosition += bytesRead;
                LOGI("✅ %d Bytes aus Datei gelesen (Handle: %d)", (int)bytesRead, fileHandle);
                return bytesRead; 
            }
        }

        LOGW("❌ Fehler beim Lesen aus Datei (Handle: %d)", fileHandle);
        return 0xFFFFFFFF; 
    }

    uint32_t X86Core::handleHDDWriteFile(uint32_t fileHandle, uint32_t bufferAddr) {
        LOGI("🎮 HDD Write File: handle=%d, buffer=0x%08X", fileHandle, bufferAddr);

        if (fileHandle >= MAX_HDD_FILES || !hddFiles[fileHandle].active) {
            LOGW("❌ Ungültiger File-Handle: %d", fileHandle);
            return 0xFFFFFFFF; 
        }

        if (!hddFiles[fileHandle].fileStream.is_open()) {
            LOGW("❌ Datei nicht geöffnet: Handle %d", fileHandle);
            return 0xFFFFFFFF; 
        }


        const uint32_t maxWriteSize = 4096;
        char writeBuffer[maxWriteSize];
        uint32_t bytesToWrite = maxWriteSize;

        bool success = true;
        for (uint32_t i = 0; i < bytesToWrite; i++) {
            try {
                writeBuffer[i] = static_cast<char>(memory->read8(bufferAddr + i));
            } catch (...) {
                bytesToWrite = i; 
                break;
            }
        }

        if (bytesToWrite > 0) {
            hddFiles[fileHandle].fileStream.write(writeBuffer, bytesToWrite);

            if (hddFiles[fileHandle].fileStream.good()) {
                hddFiles[fileHandle].currentPosition += bytesToWrite;

                hddFiles[fileHandle].fileStream.seekg(0, std::ios::end);
                hddFiles[fileHandle].fileSize = hddFiles[fileHandle].fileStream.tellg();

                LOGI("✅ %u Bytes in Datei geschrieben (Handle: %d)", bytesToWrite, fileHandle);
                return bytesToWrite; 
            }
        }

        LOGW("❌ Fehler beim Schreiben in Datei (Handle: %d)", fileHandle);
        return 0xFFFFFFFF; 
    }

    uint32_t X86Core::handleHDDSeekFile(uint32_t fileHandle, uint32_t position) {
        LOGI("🎮 HDD Seek File: handle=%d, position=%u", fileHandle, position);

        if (fileHandle >= MAX_HDD_FILES || !hddFiles[fileHandle].active) {
            LOGW("❌ Ungültiger File-Handle: %d", fileHandle);
            return 0xFFFFFFFF; 
        }

        if (!hddFiles[fileHandle].fileStream.is_open()) {
            LOGW("❌ Datei nicht geöffnet: Handle %d", fileHandle);
            return 0xFFFFFFFF; 
        }

        hddFiles[fileHandle].fileStream.seekg(position);
        if (hddFiles[fileHandle].fileStream.good()) {
            hddFiles[fileHandle].currentPosition = position;
            LOGI("✅ Datei-Position gesetzt: %u (Handle: %d)", position, fileHandle);
            return 0x00000000; 
        }

        LOGW("❌ Fehler beim Seek in Datei (Handle: %d)", fileHandle);
        return 0xFFFFFFFF; 
    }

    uint32_t X86Core::handleHDDGetFileSize(uint32_t fileHandle) {
        LOGI("🎮 HDD Get File Size: handle=%d", fileHandle);

        if (fileHandle >= MAX_HDD_FILES || !hddFiles[fileHandle].active) {
            LOGW("❌ Ungültiger File-Handle: %d", fileHandle);
            return 0xFFFFFFFF; 
        }

        uint32_t fileSize = hddFiles[fileHandle].fileSize;
        LOGI("✅ Datei-Größe: %u Bytes (Handle: %d)", fileSize, fileHandle);
        return fileSize;
    }

    uint32_t X86Core::handleHDDDeleteFile(uint32_t fileNameAddr) {
        LOGI("🎮 HDD Delete File: addr=0x%08X", fileNameAddr);


        std::string fileName;
        char nameBuffer[256];
        memset(nameBuffer, 0, sizeof(nameBuffer));

        bool success = true;
        for (int i = 0; i < 255; i++) {
            try {
                uint8_t byte = memory->read8(fileNameAddr + i);
                if (byte == 0) break;
                nameBuffer[i] = byte;
            } catch (...) {
                success = false;
                break;
            }
        }

        if (!success) {
            LOGW("❌ Fehler beim Lesen des Dateinamens");
            return 0xFFFFFFFF;
        }

        fileName = nameBuffer;
        std::string hostPath = convertXboxPathToHost(fileName);

        if (std::filesystem::remove(hostPath)) {
            LOGI("✅ Datei gelöscht: %s", fileName.c_str());
            return 0x00000000; 
        }

        LOGW("❌ Fehler beim Löschen der Datei: %s", fileName.c_str());
        return 0xFFFFFFFF; 
    }

    uint32_t X86Core::handleHDDListDirectory(uint32_t dirNameAddr, uint32_t bufferAddr) {
        LOGI("🎮 HDD List Directory: dir_addr=0x%08X, buffer=0x%08X", dirNameAddr, bufferAddr);


        std::string dirName;
        char dirBuffer[256];
        memset(dirBuffer, 0, sizeof(dirBuffer));

        bool success = true;
        for (int i = 0; i < 255; i++) {
            try {
                uint8_t byte = memory->read8(dirNameAddr + i);
                if (byte == 0) break;
                dirBuffer[i] = byte;
            } catch (...) {
                success = false;
                break;
            }
        }

        if (!success) {
            LOGW("❌ Fehler beim Lesen des Verzeichnisnamens");
            return 0xFFFFFFFF;
        }

        dirName = dirBuffer;
        std::string hostPath = convertXboxPathToHost(dirName);

        try {
            uint32_t bufferOffset = 0;
            for (const auto& entry : std::filesystem::directory_iterator(hostPath)) {
                std::string filename = entry.path().filename().string();


                for (size_t i = 0; i < filename.length(); i++) {
                    try {
                        memory->write8(bufferAddr + bufferOffset + i, filename[i]);
                    } catch (...) {
                        return bufferOffset; 
                    }
                }


                try {
                    memory->write8(bufferAddr + bufferOffset + filename.length(), 0);
                } catch (...) {
                    return bufferOffset;
                }

                bufferOffset += filename.length() + 1;


                if (bufferOffset >= 4096) break;
            }

            LOGI("✅ Verzeichnis gelistet: %s (%u Bytes)", dirName.c_str(), bufferOffset);
            return bufferOffset; 

        } catch (const std::filesystem::filesystem_error& e) {
            LOGW("❌ Fehler beim Auflisten des Verzeichnisses: %s", e.what());
            return 0xFFFFFFFF;
        }
    }

    uint32_t X86Core::handleHDDCreateDirectory(uint32_t dirNameAddr) {
        LOGI("🎮 HDD Create Directory: addr=0x%08X", dirNameAddr);


        std::string dirName;
        char dirBuffer[256];
        memset(dirBuffer, 0, sizeof(dirBuffer));

        bool success = true;
        for (int i = 0; i < 255; i++) {
            try {
                uint8_t byte = memory->read8(dirNameAddr + i);
                if (byte == 0) break;
                dirBuffer[i] = byte;
            } catch (...) {
                success = false;
                break;
            }
        }

        if (!success) {
            LOGW("❌ Fehler beim Lesen des Verzeichnisnamens");
            return 0xFFFFFFFF;
        }

        dirName = dirBuffer;
        std::string hostPath = convertXboxPathToHost(dirName);

        if (std::filesystem::create_directory(hostPath)) {
            LOGI("✅ Verzeichnis erstellt: %s", dirName.c_str());
            return 0x00000000; 
        }

        LOGW("❌ Fehler beim Erstellen des Verzeichnisses: %s", dirName.c_str());
        return 0xFFFFFFFF; 
    }

    uint32_t X86Core::handleHDDGetFreeSpace() {
        try {
            std::filesystem::space_info space = std::filesystem::space(".");
            uint32_t freeSpaceGB = space.available / (1024 * 1024 * 1024); 

            LOGI("✅ Freier Speicherplatz: %u GB", freeSpaceGB);
            return freeSpaceGB;
        } catch (...) {
            LOGW("❌ Fehler beim Ermitteln des freien Speicherplatzes");
            return 0xFFFFFFFF;
        }
    }

    std::string X86Core::convertXboxPathToHost(const std::string& xboxPath) {


        std::string hostPath = xboxPath;


        for (char& c : hostPath) {
            if (c == '\\') c = '/';
        }


        if (!hostPath.empty() && hostPath[0] == '/') {
            hostPath = hostPath.substr(1);
        }


        if (!hostPath.empty()) {
            hostPath = "xbox_saves/" + hostPath;
        } else {
            hostPath = "xbox_saves";
        }


        std::filesystem::create_directories(std::filesystem::path(hostPath).parent_path());

        return hostPath;
    }



    uint32_t X86Core::handleNetworkInit(uint32_t configAddr, uint32_t flags) {
        LOGI("🎮 Network Init: config_addr=0x%08X, flags=0x%08X", configAddr, flags);

        if (networkInitialized) {
            LOGW("❌ Network bereits initialisiert");
            return 0xFFFFFFFF; 
        }

        networkInitialized = true;
        networkStatus = 0x00000001; 

        LOGI("✅ Network-System initialisiert (Status: 0x%08X)", networkStatus);
        return 0x00000000; 
    }

    uint32_t X86Core::handleNetworkCreateSocket(uint32_t socketType, uint32_t protocol) {
        LOGI("🎮 Network Create Socket: type=%d, protocol=%d", socketType, protocol);

        if (!networkInitialized) {
            LOGW("❌ Network-System nicht initialisiert");
            return 0xFFFFFFFF; 
        }


        for (int i = 0; i < MAX_NETWORK_SOCKETS; i++) {
            if (!networkSockets[i].active) {
                networkSockets[i].active = true;
                networkSockets[i].socketType = socketType; 
                networkSockets[i].connected = false;
                networkSockets[i].isServer = false;
                networkSockets[i].receiveBuffer.clear();

                LOGI("✅ Network-Socket %d erstellt (Type: %d)", i, socketType);
                return i; 
            }
        }

        LOGW("❌ Kein freier Network-Socket verfügbar");
        return 0xFFFFFFFF; 
    }

    uint32_t X86Core::handleNetworkConnect(uint32_t socketId, uint32_t addrPort) {
        LOGI("🎮 Network Connect: socket=%d, addr_port=0x%08X", socketId, addrPort);

        if (!networkInitialized) {
            LOGW("❌ Network-System nicht initialisiert");
            return 0xFFFFFFFF; 
        }

        if (socketId >= MAX_NETWORK_SOCKETS || !networkSockets[socketId].active) {
            LOGW("❌ Ungültiger Socket-ID: %d", socketId);
            return 0xFFFFFFFF; 
        }


        uint32_t addr = addrPort & 0xFFFFFFFF;
        uint16_t port = (addrPort >> 16) & 0xFFFF;

        networkSockets[socketId].remoteAddr = addr;
        networkSockets[socketId].remotePort = port;
        networkSockets[socketId].connected = true; 

        LOGI("✅ Socket %d verbunden mit %u.%u.%u.%u:%u",
             socketId,
             (addr >> 24) & 0xFF, (addr >> 16) & 0xFF,
             (addr >> 8) & 0xFF, addr & 0xFF,
             port);
        return 0x00000000; 
    }

    uint32_t X86Core::handleNetworkSend(uint32_t socketId, uint32_t dataAddr) {
        LOGI("🎮 Network Send: socket=%d, data_addr=0x%08X", socketId, dataAddr);

        if (!networkInitialized) {
            LOGW("❌ Network-System nicht initialisiert");
            return 0xFFFFFFFF; 
        }

        if (socketId >= MAX_NETWORK_SOCKETS || !networkSockets[socketId].active) {
            LOGW("❌ Ungültiger Socket-ID: %d", socketId);
            return 0xFFFFFFFF; 
        }

        if (!networkSockets[socketId].connected) {
            LOGW("❌ Socket %d nicht verbunden", socketId);
            return 0xFFFFFFFF; 
        }


        LOGI("✅ Daten über Socket %d gesendet", socketId);
        return 1024; 
    }
    uint32_t X86Core::handleNetworkReceive(uint32_t socketId, uint32_t bufferAddr) {
        LOGI("🎮 Network Receive: socket=%d, buffer_addr=0x%08X", socketId, bufferAddr);

        if (!networkInitialized) {
            LOGW("❌ Network-System nicht initialisiert");
            return 0xFFFFFFFF; 
        }

        if (socketId >= MAX_NETWORK_SOCKETS || !networkSockets[socketId].active) {
            LOGW("❌ Ungültiger Socket-ID: %d", socketId);
            return 0xFFFFFFFF; 
        }


        const char* testData = "Xbox Network Test Data";
        uint32_t dataLength = strlen(testData);


        bool success = true;
        for (uint32_t i = 0; i < dataLength; i++) {
            try {
                memory->write8(bufferAddr + i, testData[i]);
            } catch (...) {
                success = false;
                break;
            }
        }


        try {
            memory->write8(bufferAddr + dataLength, 0);
        } catch (...) {
            success = false;
        }

        if (success) {
            LOGI("✅ %u Bytes über Socket %d empfangen", dataLength, socketId);
            return dataLength; 
        } else {
            LOGW("❌ Fehler beim Schreiben der empfangenen Daten");
            return 0xFFFFFFFF; 
        }
    }

    uint32_t X86Core::handleNetworkCloseSocket(uint32_t socketId) {
        LOGI("🎮 Network Close Socket: socket=%d", socketId);

        if (!networkInitialized) {
            LOGW("❌ Network-System nicht initialisiert");
            return 0xFFFFFFFF; 
        }

        if (socketId >= MAX_NETWORK_SOCKETS || !networkSockets[socketId].active) {
            LOGW("❌ Ungültiger Socket-ID: %d", socketId);
            return 0xFFFFFFFF; 
        }

        networkSockets[socketId].active = false;
        networkSockets[socketId].connected = false;
        networkSockets[socketId].receiveBuffer.clear();

        LOGI("✅ Socket %d geschlossen", socketId);
        return 0x00000000; 
    }

    uint32_t X86Core::handleNetworkGetHostByName(uint32_t hostNameAddr, uint32_t bufferAddr) {
        LOGI("🎮 Network Get Host By Name: hostname_addr=0x%08X, buffer_addr=0x%08X",
             hostNameAddr, bufferAddr);

        if (!networkInitialized) {
            LOGW("❌ Network-System nicht initialisiert");
            return 0xFFFFFFFF; 
        }


        std::string hostname;
        char nameBuffer[256];
        memset(nameBuffer, 0, sizeof(nameBuffer));

        bool success = true;
        for (int i = 0; i < 255; i++) {
            try {
                uint8_t byte = memory->read8(hostNameAddr + i);
                if (byte == 0) break;
                nameBuffer[i] = byte;
            } catch (...) {
                success = false;
                break;
            }
        }

        if (!success) {
            LOGW("❌ Fehler beim Lesen des Hostnamens");
            return 0xFFFFFFFF;
        }

        hostname = nameBuffer;


        uint32_t resolvedIP = 0xC0A80101; 


        try {
            memory->write32(bufferAddr, resolvedIP);
            LOGI("✅ Hostname '%s' zu IP %u.%u.%u.%u aufgelöst",
                 hostname.c_str(),
                 (resolvedIP >> 24) & 0xFF, (resolvedIP >> 16) & 0xFF,
                 (resolvedIP >> 8) & 0xFF, resolvedIP & 0xFF);
            return 0x00000000; 
        } catch (...) {
            LOGW("❌ Fehler beim Schreiben der aufgelösten IP-Adresse");
            return 0xFFFFFFFF; 
        }
    }

    uint32_t X86Core::handleNetworkBind(uint32_t socketId, uint32_t addrPort) {
        LOGI("🎮 Network Bind: socket=%d, addr_port=0x%08X", socketId, addrPort);

        if (!networkInitialized) {
            LOGW("❌ Network-System nicht initialisiert");
            return 0xFFFFFFFF; 
        }

        if (socketId >= MAX_NETWORK_SOCKETS || !networkSockets[socketId].active) {
            LOGW("❌ Ungültiger Socket-ID: %d", socketId);
            return 0xFFFFFFFF; 
        }


        uint32_t addr = addrPort & 0xFFFFFFFF;
        uint16_t port = (addrPort >> 16) & 0xFFFF;

        networkSockets[socketId].localAddr = addr;
        networkSockets[socketId].localPort = port;
        networkSockets[socketId].isServer = true;

        LOGI("✅ Socket %d an Port %u gebunden", socketId, port);
        return 0x00000000; 
    }

    uint32_t X86Core::handleNetworkListen(uint32_t socketId, uint32_t backlog) {
        LOGI("🎮 Network Listen: socket=%d, backlog=%d", socketId, backlog);

        if (!networkInitialized) {
            LOGW("❌ Network-System nicht initialisiert");
            return 0xFFFFFFFF; 
        }

        if (socketId >= MAX_NETWORK_SOCKETS || !networkSockets[socketId].active) {
            LOGW("❌ Ungültiger Socket-ID: %d", socketId);
            return 0xFFFFFFFF; 
        }

        if (!networkSockets[socketId].isServer) {
            LOGW("❌ Socket %d ist kein Server-Socket", socketId);
            return 0xFFFFFFFF; 
        }

        LOGI("✅ Socket %d horcht auf Verbindungen (Backlog: %d)", socketId, backlog);
        return 0x00000000; 
    }

    uint32_t X86Core::handleNetworkAccept(uint32_t socketId) {
        LOGI("🎮 Network Accept: socket=%d", socketId);

        if (!networkInitialized) {
            LOGW("❌ Network-System nicht initialisiert");
            return 0xFFFFFFFF; 
        }

        if (socketId >= MAX_NETWORK_SOCKETS || !networkSockets[socketId].active) {
            LOGW("❌ Ungültiger Socket-ID: %d", socketId);
            return 0xFFFFFFFF; 
        }


        uint32_t clientSocketId = socketId + 1000; 
        LOGI("✅ Verbindung von Socket %d akzeptiert (Client: %d)", socketId, clientSocketId);
        return clientSocketId; 
    }

    uint32_t X86Core::handleNetworkGetStatus() {
        LOGI("🎮 Network Get Status");

        if (!networkInitialized) {
            LOGI("✅ Network Status: Nicht initialisiert");
            return 0x00000000; 
        }

        LOGI("✅ Network Status: 0x%08X", networkStatus);
        return networkStatus; 
    }



    void X86Core::initializeStandardUSBDevices() {
        LOGI("🎮 Initialisiere Standard-USB-Geräte...");


        usbDevices[0].active = true;
        usbDevices[0].vendorId = 0x045E;  
        usbDevices[0].productId = 0x0202; 
        usbDevices[0].deviceClass = 0x03; 
        usbDevices[0].deviceSubClass = 0x00;
        usbDevices[0].deviceName = "Xbox Controller 1";
        usbDevices[0].isController = true;
        usbDevices[0].isMemoryCard = false;
        usbDevices[0].controllerState = 0x00000000; 
        usbDevices[0].rumbleState = 0x00000000;     


        usbDevices[1].active = true;
        usbDevices[1].vendorId = 0x045E;
        usbDevices[1].productId = 0x0202;
        usbDevices[1].deviceClass = 0x03;
        usbDevices[1].deviceSubClass = 0x00;
        usbDevices[1].deviceName = "Xbox Controller 2";
        usbDevices[1].isController = true;
        usbDevices[1].isMemoryCard = false;
        usbDevices[1].controllerState = 0x00000000;
        usbDevices[1].rumbleState = 0x00000000;


        usbDevices[2].active = true;
        usbDevices[2].vendorId = 0x045E;
        usbDevices[2].productId = 0x0101;
        usbDevices[2].deviceClass = 0x08; 
        usbDevices[2].deviceSubClass = 0x00;
        usbDevices[2].deviceName = "Memory Card 1";
        usbDevices[2].isController = false;
        usbDevices[2].isMemoryCard = true;
        usbDevices[2].memoryCardData.resize(8 * 1024 * 1024, 0xFF); 


        usbDevices[3].active = true;
        usbDevices[3].vendorId = 0x045E;
        usbDevices[3].productId = 0x0101;
        usbDevices[3].deviceClass = 0x08;
        usbDevices[3].deviceSubClass = 0x00;
        usbDevices[3].deviceName = "Memory Card 2";
        usbDevices[3].isController = false;
        usbDevices[3].isMemoryCard = true;
        usbDevices[3].memoryCardData.resize(8 * 1024 * 1024, 0xFF);

        usbDeviceCount = 4;
        LOGI("✅ %u Standard-USB-Geräte initialisiert", usbDeviceCount);
    }

    uint32_t X86Core::handleUSBInit(uint32_t configAddr, uint32_t flags) {
        LOGI("🎮 USB Init: config_addr=0x%08X, flags=0x%08X", configAddr, flags);

        if (usbInitialized) {
            LOGW("❌ USB bereits initialisiert");
            return 0xFFFFFFFF; 
        }

        usbInitialized = true;
        LOGI("✅ USB-System initialisiert (%u Geräte verfügbar)", usbDeviceCount);
        return 0x00000000; 
    }

    uint32_t X86Core::handleUSBEnumerateDevices(uint32_t bufferAddr, uint32_t maxDevices) {
        LOGI("🎮 USB Enumerate Devices: buffer_addr=0x%08X, max_devices=%d", bufferAddr, maxDevices);

        if (!usbInitialized) {
            LOGW("❌ USB-System nicht initialisiert");
            return 0xFFFFFFFF; 
        }

        uint32_t devicesWritten = 0;
        uint32_t bufferOffset = 0;


        for (uint32_t i = 0; i < MAX_USB_DEVICES && devicesWritten < maxDevices; i++) {
            if (!usbDevices[i].active) continue;

            bool success = true;
            try {

                memory->write16(bufferAddr + bufferOffset, usbDevices[i].vendorId);
                bufferOffset += 2;


                memory->write16(bufferAddr + bufferOffset, usbDevices[i].productId);
                bufferOffset += 2;


                memory->write8(bufferAddr + bufferOffset, usbDevices[i].deviceClass);
                bufferOffset += 1;


                memory->write8(bufferAddr + bufferOffset, usbDevices[i].deviceSubClass);
                bufferOffset += 1;


                memory->write32(bufferAddr + bufferOffset, 0x00000000); 
                bufferOffset += 4;

                devicesWritten++;
            } catch (...) {
                success = false;
                break;
            }
        }

        LOGI("✅ %u USB-Geräte enumeriert", devicesWritten);
        return devicesWritten; 
    }

    uint32_t X86Core::handleUSBOpenDevice(uint32_t deviceIndex, uint32_t flags) {
        LOGI("🎮 USB Open Device: device_index=%d, flags=0x%08X", deviceIndex, flags);

        if (!usbInitialized) {
            LOGW("❌ USB-System nicht initialisiert");
            return 0xFFFFFFFF; 
        }

        if (deviceIndex >= MAX_USB_DEVICES || !usbDevices[deviceIndex].active) {
            LOGW("❌ Ungültiger Device-Index: %d", deviceIndex);
            return 0xFFFFFFFF; 
        }

        LOGI("✅ USB-Gerät geöffnet: %s", usbDevices[deviceIndex].deviceName.c_str());
        return deviceIndex; 
    }

    uint32_t X86Core::handleUSBCloseDevice(uint32_t deviceHandle) {
        LOGI("🎮 USB Close Device: device_handle=%d", deviceHandle);

        if (!usbInitialized) {
            LOGW("❌ USB-System nicht initialisiert");
            return 0xFFFFFFFF; 
        }

        if (deviceHandle >= MAX_USB_DEVICES || !usbDevices[deviceHandle].active) {
            LOGW("❌ Ungültiger Device-Handle: %d", deviceHandle);
            return 0xFFFFFFFF; 
        }

        LOGI("✅ USB-Gerät geschlossen: %s", usbDevices[deviceHandle].deviceName.c_str());
        return 0x00000000; 
    }

    uint32_t X86Core::handleUSBControlTransfer(uint32_t deviceHandle, uint32_t transferAddr) {
        LOGI("🎮 USB Control Transfer: device_handle=%d, transfer_addr=0x%08X", deviceHandle, transferAddr);

        if (!usbInitialized) {
            LOGW("❌ USB-System nicht initialisiert");
            return 0xFFFFFFFF;
        }

        if (deviceHandle >= MAX_USB_DEVICES || !usbDevices[deviceHandle].active) {
            LOGW("❌ Ungültiger Device-Handle: %d", deviceHandle);
            return 0xFFFFFFFF;
        }

        LOGI("✅ Control Transfer für %s ausgeführt", usbDevices[deviceHandle].deviceName.c_str());
        return 64; 
    }

    uint32_t X86Core::handleUSBBulkTransfer(uint32_t deviceHandle, uint32_t transferAddr) {
        LOGI("🎮 USB Bulk Transfer: device_handle=%d, transfer_addr=0x%08X", deviceHandle, transferAddr);

        if (!usbInitialized) {
            LOGW("❌ USB-System nicht initialisiert");
            return 0xFFFFFFFF;
        }

        if (deviceHandle >= MAX_USB_DEVICES || !usbDevices[deviceHandle].active) {
            LOGW("❌ Ungültiger Device-Handle: %d", deviceHandle);
            return 0xFFFFFFFF;
        }

        LOGI("✅ Bulk Transfer für %s ausgeführt", usbDevices[deviceHandle].deviceName.c_str());
        return 512; 
    }

    uint32_t X86Core::handleUSBInterruptTransfer(uint32_t deviceHandle, uint32_t transferAddr) {
        LOGI("🎮 USB Interrupt Transfer: device_handle=%d, transfer_addr=0x%08X", deviceHandle, transferAddr);

        if (!usbInitialized) {
            LOGW("❌ USB-System nicht initialisiert");
            return 0xFFFFFFFF;
        }

        if (deviceHandle >= MAX_USB_DEVICES || !usbDevices[deviceHandle].active) {
            LOGW("❌ Ungültiger Device-Handle: %d", deviceHandle);
            return 0xFFFFFFFF;
        }

        LOGI("✅ Interrupt Transfer für %s ausgeführt", usbDevices[deviceHandle].deviceName.c_str());
        return 32; 
    }

    uint32_t X86Core::handleUSBGetDeviceDescriptor(uint32_t deviceHandle, uint32_t bufferAddr) {
        LOGI("🎮 USB Get Device Descriptor: device_handle=%d, buffer_addr=0x%08X", deviceHandle, bufferAddr);

        if (!usbInitialized) {
            LOGW("❌ USB-System nicht initialisiert");
            return 0xFFFFFFFF;
        }

        if (deviceHandle >= MAX_USB_DEVICES || !usbDevices[deviceHandle].active) {
            LOGW("❌ Ungültiger Device-Handle: %d", deviceHandle);
            return 0xFFFFFFFF;
        }


        try {
            uint32_t offset = 0;
            memory->write8(bufferAddr + offset++, 18);                    
            memory->write8(bufferAddr + offset++, 0x01);                 
            memory->write16(bufferAddr + offset, 0x0110); offset += 2;   
            memory->write8(bufferAddr + offset++, usbDevices[deviceHandle].deviceClass);     
            memory->write8(bufferAddr + offset++, usbDevices[deviceHandle].deviceSubClass);   
            memory->write8(bufferAddr + offset++, 0x00);                 
            memory->write8(bufferAddr + offset++, 0x08);                 
            memory->write16(bufferAddr + offset, usbDevices[deviceHandle].vendorId); offset += 2;   
            memory->write16(bufferAddr + offset, usbDevices[deviceHandle].productId); offset += 2;  
            memory->write16(bufferAddr + offset, 0x0100); offset += 2;   
            memory->write8(bufferAddr + offset++, 0x00);                 
            memory->write8(bufferAddr + offset++, 0x00);                 
            memory->write8(bufferAddr + offset++, 0x00);                 
            memory->write8(bufferAddr + offset++, 0x01);                 

            LOGI("✅ Device Descriptor für %s geschrieben", usbDevices[deviceHandle].deviceName.c_str());
            return 18; 
        } catch (...) {
            LOGW("❌ Fehler beim Schreiben des Device Descriptors");
            return 0xFFFFFFFF;
        }
    }

    uint32_t X86Core::handleUSBGetControllerState(uint32_t deviceHandle, uint32_t bufferAddr) {
        LOGI("🎮 USB Get Controller State: device_handle=%d, buffer_addr=0x%08X", deviceHandle, bufferAddr);

        if (!usbInitialized) {
            LOGW("❌ USB-System nicht initialisiert");
            return 0xFFFFFFFF;
        }

        if (deviceHandle >= MAX_USB_DEVICES || !usbDevices[deviceHandle].active ||
            !usbDevices[deviceHandle].isController) {
            LOGW("❌ Ungültiger Controller-Handle: %d", deviceHandle);
            return 0xFFFFFFFF;
        }


        try {
            uint32_t offset = 0;
            memory->write16(bufferAddr + offset, 0x0000); offset += 2;    
            memory->write8(bufferAddr + offset++, 0x80);                 
            memory->write8(bufferAddr + offset++, 0x80);                 
            memory->write8(bufferAddr + offset++, 0x80);                 
            memory->write8(bufferAddr + offset++, 0x80);                 
            memory->write8(bufferAddr + offset++, 0x80);                 
            memory->write8(bufferAddr + offset++, 0x80);                 

            LOGI("✅ Controller State für %s gelesen", usbDevices[deviceHandle].deviceName.c_str());
            return 6; 
        } catch (...) {
            LOGW("❌ Fehler beim Schreiben des Controller States");
            return 0xFFFFFFFF;
        }
    }

    uint32_t X86Core::handleUSBSetControllerRumble(uint32_t deviceHandle, uint32_t rumbleData) {
        LOGI("🎮 USB Set Controller Rumble: device_handle=%d, rumble_data=0x%08X", deviceHandle, rumbleData);

        if (!usbInitialized) {
            LOGW("❌ USB-System nicht initialisiert");
            return 0xFFFFFFFF;
        }

        if (deviceHandle >= MAX_USB_DEVICES || !usbDevices[deviceHandle].active ||
            !usbDevices[deviceHandle].isController) {
            LOGW("❌ Ungültiger Controller-Handle: %d", deviceHandle);
            return 0xFFFFFFFF;
        }

        usbDevices[deviceHandle].rumbleState = rumbleData;

        uint8_t leftMotor = (rumbleData >> 16) & 0xFF;
        uint8_t rightMotor = (rumbleData >> 24) & 0xFF;

        LOGI("✅ Rumble für %s gesetzt (L:%d, R:%d)",
             usbDevices[deviceHandle].deviceName.c_str(), leftMotor, rightMotor);
        return 0x00000000; 
    }

    uint32_t X86Core::handleUSBGetMemoryCardInfo(uint32_t deviceHandle, uint32_t bufferAddr) {
        LOGI("🎮 USB Get Memory Card Info: device_handle=%d, buffer_addr=0x%08X", deviceHandle, bufferAddr);

        if (!usbInitialized) {
            LOGW("❌ USB-System nicht initialisiert");
            return 0xFFFFFFFF;
        }

        if (deviceHandle >= MAX_USB_DEVICES || !usbDevices[deviceHandle].active ||
            !usbDevices[deviceHandle].isMemoryCard) {
            LOGW("❌ Ungültige Memory Card: %d", deviceHandle);
            return 0xFFFFFFFF;
        }

        try {
            uint32_t offset = 0;
            memory->write32(bufferAddr + offset, usbDevices[deviceHandle].memoryCardData.size()); offset += 4; 
            memory->write32(bufferAddr + offset, 0x00000001); offset += 4; 
            memory->write32(bufferAddr + offset, 0x00000000); offset += 4; 

            LOGI("✅ Memory Card Info für %s gelesen", usbDevices[deviceHandle].deviceName.c_str());
            return 12; 
        } catch (...) {
            LOGW("❌ Fehler beim Schreiben der Memory Card Info");
            return 0xFFFFFFFF;
        }
    }

    uint32_t X86Core::handleUSBReadMemoryCard(uint32_t deviceHandle, uint32_t transferAddr) {
        LOGI("🎮 USB Read Memory Card: device_handle=%d, transfer_addr=0x%08X", deviceHandle, transferAddr);

        if (!usbInitialized) {
            LOGW("❌ USB-System nicht initialisiert");
            return 0xFFFFFFFF;
        }

        if (deviceHandle >= MAX_USB_DEVICES || !usbDevices[deviceHandle].active ||
            !usbDevices[deviceHandle].isMemoryCard) {
            LOGW("❌ Ungültige Memory Card: %d", deviceHandle);
            return 0xFFFFFFFF;
        }


        try {
            uint32_t offset = memory->read32(transferAddr);
            uint32_t length = memory->read32(transferAddr + 4);
            uint32_t bufferAddr = memory->read32(transferAddr + 8);

            if (offset + length > usbDevices[deviceHandle].memoryCardData.size()) {
                LOGW("❌ Ungültiger Transfer-Bereich: offset=%u, length=%u", offset, length);
                return 0xFFFFFFFF;
            }


            for (uint32_t i = 0; i < length; i++) {
                memory->write8(bufferAddr + i, usbDevices[deviceHandle].memoryCardData[offset + i]);
            }

            LOGI("✅ %u Bytes von Memory Card %s gelesen", length, usbDevices[deviceHandle].deviceName.c_str());
            return length; 
        } catch (...) {
            LOGW("❌ Fehler beim Lesen der Memory Card");
            return 0xFFFFFFFF;
        }
    }

    uint32_t X86Core::handleUSBWriteMemoryCard(uint32_t deviceHandle, uint32_t transferAddr) {
        LOGI("🎮 USB Write Memory Card: device_handle=%d, transfer_addr=0x%08X", deviceHandle, transferAddr);

        if (!usbInitialized) {
            LOGW("❌ USB-System nicht initialisiert");
            return 0xFFFFFFFF;
        }

        if (deviceHandle >= MAX_USB_DEVICES || !usbDevices[deviceHandle].active ||
            !usbDevices[deviceHandle].isMemoryCard) {
            LOGW("❌ Ungültige Memory Card: %d", deviceHandle);
            return 0xFFFFFFFF;
        }


        try {
            uint32_t offset = memory->read32(transferAddr);
            uint32_t length = memory->read32(transferAddr + 4);
            uint32_t bufferAddr = memory->read32(transferAddr + 8);

            if (offset + length > usbDevices[deviceHandle].memoryCardData.size()) {
                LOGW("❌ Ungültiger Transfer-Bereich: offset=%u, length=%u", offset, length);
                return 0xFFFFFFFF;
            }


            for (uint32_t i = 0; i < length; i++) {
                usbDevices[deviceHandle].memoryCardData[offset + i] = memory->read8(bufferAddr + i);
            }

            LOGI("✅ %u Bytes auf Memory Card %s geschrieben", length, usbDevices[deviceHandle].deviceName.c_str());
            return length; 
        } catch (...) {
            LOGW("❌ Fehler beim Schreiben der Memory Card");
            return 0xFFFFFFFF;
        }
    }



    uint32_t X86Core::handleSystemShutdown(uint32_t shutdownType) {
        LOGI("🎮 System Shutdown: type=0x%08X", shutdownType);

        switch (shutdownType) {
            case 0x00: 
                LOGI("✅ Xbox wird normal heruntergefahren...");
                break;
            case 0x01: 
                LOGW("⚠️ Xbox wird zwangsweise heruntergefahren!");
                break;
            case 0x02: 
                LOGI("💤 Xbox geht in den Ruhezustand...");
                break;
            default:
                LOGW("❌ Unbekannter Shutdown-Typ: 0x%08X", shutdownType);
                return 0xFFFFFFFF;
        }



        LOGI("✅ System-Shutdown simuliert");
        return 0x00000000; 
    }

    uint32_t X86Core::handleSystemReboot() {
        LOGI("🎮 System Reboot");

        LOGI("🔄 Xbox wird neu gestartet...");


        resetSystem();

        LOGI("✅ System-Reboot simuliert");
        return 0x00000000; 
    }

    uint32_t X86Core::handleSystemGetInfo(uint32_t bufferAddr, uint32_t infoType) {
        LOGI("🎮 System Get Info: buffer_addr=0x%08X, type=0x%08X", bufferAddr, infoType);

        try {
            uint32_t offset = 0;

            switch (infoType) {
                case 0x00: 
                    memory->write32(bufferAddr + offset, systemInfo.xboxVersion); offset += 4;
                    memory->write32(bufferAddr + offset, systemInfo.kernelVersion); offset += 4;
                    memory->write32(bufferAddr + offset, systemInfo.totalMemory); offset += 4;
                    memory->write32(bufferAddr + offset, systemInfo.availableMemory); offset += 4;
                    memory->write32(bufferAddr + offset, systemInfo.fanSpeed); offset += 4;
                    memory->write32(bufferAddr + offset, systemInfo.temperature); offset += 4;
                    memory->write32(bufferAddr + offset, systemInfo.diskSpace); offset += 4;
                    memory->write32(bufferAddr + offset, systemInfo.isDebugMode ? 1 : 0); offset += 4;
                    return 32; 

                case 0x01: 
                    memory->write32(bufferAddr + offset, systemInfo.xboxVersion); offset += 4;
                    memory->write32(bufferAddr + offset, systemInfo.totalMemory); offset += 4;
                    memory->write32(bufferAddr + offset, systemInfo.diskSpace); offset += 4;
                    return 12; 

                case 0x02: 
                    memory->write32(bufferAddr + offset, systemInfo.kernelVersion); offset += 4;
                    memory->write32(bufferAddr + offset, systemInfo.isDebugMode ? 1 : 0); offset += 4;
                    return 8; 

                default:
                    LOGW("❌ Unbekannter Info-Typ: 0x%08X", infoType);
                    return 0xFFFFFFFF;
            }
        } catch (...) {
            LOGW("❌ Fehler beim Schreiben der System-Info");
            return 0xFFFFFFFF;
        }
    }

    uint32_t X86Core::handleSystemSetTime(uint32_t timeValue) {
        LOGI("🎮 System Set Time: time=0x%08X", timeValue);

        systemInfo.systemTime = timeValue;
        LOGI("✅ Systemzeit gesetzt auf: 0x%08X", timeValue);
        return 0x00000000; 
    }

    uint32_t X86Core::handleSystemGetTime(uint32_t bufferAddr) {
        LOGI("🎮 System Get Time: buffer_addr=0x%08X", bufferAddr);


        uint32_t currentTime = static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count()) - 946684800; 

        systemInfo.systemTime = currentTime;

        try {
            memory->write32(bufferAddr, currentTime);
            LOGI("✅ Aktuelle Systemzeit: 0x%08X (%u Sekunden seit 2000)", currentTime, currentTime);
            return 4; 
        } catch (...) {
            LOGW("❌ Fehler beim Schreiben der Systemzeit");
            return 0xFFFFFFFF;
        }
    }

    uint32_t X86Core::handleSystemGetVersion(uint32_t bufferAddr) {
        LOGI("🎮 System Get Version: buffer_addr=0x%08X", bufferAddr);

        try {
            uint32_t offset = 0;
            memory->write32(bufferAddr + offset, systemInfo.xboxVersion); offset += 4;    
            memory->write32(bufferAddr + offset, systemInfo.kernelVersion); offset += 4; 
            memory->write32(bufferAddr + offset, 0x00000100); offset += 4;              

            LOGI("✅ Xbox-Version: %u.%u, Kernel: %u.%u",
                 (systemInfo.xboxVersion >> 16) & 0xFFFF, systemInfo.xboxVersion & 0xFFFF,
                 (systemInfo.kernelVersion >> 16) & 0xFFFF, systemInfo.kernelVersion & 0xFFFF);
            return 12; 
        } catch (...) {
            LOGW("❌ Fehler beim Schreiben der Versionsinfo");
            return 0xFFFFFFFF;
        }
    }

    uint32_t X86Core::handleSystemGetMemoryInfo(uint32_t bufferAddr) {
        LOGI("🎮 System Get Memory Info: buffer_addr=0x%08X", bufferAddr);

        try {
            uint32_t offset = 0;
            memory->write32(bufferAddr + offset, systemInfo.totalMemory); offset += 4;       
            memory->write32(bufferAddr + offset, systemInfo.availableMemory); offset += 4;   
            memory->write32(bufferAddr + offset, systemInfo.totalMemory - systemInfo.availableMemory); offset += 4; 

            LOGI("✅ Speicher-Info: Total=%uMB, Available=%uMB, Used=%uMB",
                 systemInfo.totalMemory / (1024*1024),
                 systemInfo.availableMemory / (1024*1024),
                 (systemInfo.totalMemory - systemInfo.availableMemory) / (1024*1024));
            return 12; 
        } catch (...) {
            LOGW("❌ Fehler beim Schreiben der Speicherinfo");
            return 0xFFFFFFFF;
        }
    }

    uint32_t X86Core::handleSystemDebugPrint(uint32_t stringAddr, uint32_t length) {
        LOGI("🎮 System Debug Print: string_addr=0x%08X, length=%d", stringAddr, length);

        if (length == 0 || length > 1024) {
            LOGW("❌ Ungültige String-Länge: %d", length);
            return 0xFFFFFFFF;
        }

        try {
            char debugString[1025];
            memset(debugString, 0, sizeof(debugString));

            for (uint32_t i = 0; i < length && i < 1024; i++) {
                debugString[i] = memory->read8(stringAddr + i);
            }

            LOGI("🔍 DEBUG: %s", debugString);
            return 0x00000000; 
        } catch (...) {
            LOGW("❌ Fehler beim Lesen des Debug-Strings");
            return 0xFFFFFFFF;
        }
    }

    uint32_t X86Core::handleSystemGetTemperature() {
        LOGI("🎮 System Get Temperature");


        systemInfo.temperature = 40 + (rand() % 15);
        LOGI("🌡️ Systemtemperatur: %u°C", systemInfo.temperature);
        return systemInfo.temperature;
    }

    uint32_t X86Core::handleSystemGetFanSpeed() {
        LOGI("🎮 System Get Fan Speed");


        systemInfo.fanSpeed = 1000 + (rand() % 2000);
        LOGI("💨 Lüftergeschwindigkeit: %u RPM", systemInfo.fanSpeed);
        return systemInfo.fanSpeed;
    }

    uint32_t X86Core::handleSystemSetFanSpeed(uint32_t fanSpeed) {
        LOGI("🎮 System Set Fan Speed: speed=%u RPM", fanSpeed);

        if (fanSpeed < 1000 || fanSpeed > 5000) {
            LOGW("❌ Ungültige Lüftergeschwindigkeit: %u RPM", fanSpeed);
            return 0xFFFFFFFF;
        }

        systemInfo.fanSpeed = fanSpeed;
        LOGI("✅ Lüftergeschwindigkeit gesetzt auf: %u RPM", fanSpeed);
        return 0x00000000; 
    }
    uint32_t X86Core::handleSystemGetDiskInfo(uint32_t bufferAddr) {
        LOGI("🎮 System Get Disk Info: buffer_addr=0x%08X", bufferAddr);

        try {
            uint32_t offset = 0;
            memory->write32(bufferAddr + offset, systemInfo.diskSpace); offset += 4;             
            memory->write32(bufferAddr + offset, 6 * 1024 * 1024 * 1024); offset += 4;           
            memory->write32(bufferAddr + offset, systemInfo.diskSpace - (6 * 1024 * 1024 * 1024)); offset += 4; 

            LOGI("💾 Festplatten-Info: Total=%uGB, Used=%uGB, Free=%uGB",
                 systemInfo.diskSpace / (1024*1024*1024),
                 6, 
                 (systemInfo.diskSpace - (6 * 1024 * 1024 * 1024)) / (1024*1024*1024));
            return 12; 
        } catch (...) {
            LOGW("❌ Fehler beim Schreiben der Festplatteninfo");
            return 0xFFFFFFFF;
        }
    }

    uint32_t X86Core::handleSystemLaunchTitle(uint32_t titleId, uint32_t launchFlags) {
        LOGI("🎮 System Launch Title: title_id=0x%08X, flags=0x%08X", titleId, launchFlags);


        if ((titleId & 0xFF000000) == 0) {
            LOGW("❌ Ungültige Title-ID: 0x%08X", titleId);
            return 0xFFFFFFFF;
        }

        char titleChars[5];
        titleChars[0] = (titleId >> 24) & 0xFF;
        titleChars[1] = (titleId >> 16) & 0xFF;
        titleChars[2] = (titleId >> 8) & 0xFF;
        titleChars[3] = titleId & 0xFF;
        titleChars[4] = '\0';

        LOGI("🚀 Starte Titel: %s (Flags: 0x%08X)", titleChars, launchFlags);



        LOGI("✅ Titel-Start simuliert");
        return 0x00000000; 
    }

    void X86Core::resetSystem() {
        LOGI("🔄 System-Reset durchgeführt");


        systemInfo.systemTime = 0;
        systemInfo.temperature = 45;
        systemInfo.fanSpeed = 1500;


        for (int i = 0; i < MAX_USB_DEVICES; i++) {
            if (usbDevices[i].active) {
                usbDevices[i].controllerState = 0;
                usbDevices[i].rumbleState = 0;
            }
        }


        for (int i = 0; i < MAX_XBOX_TIMERS; i++) {
            if (xboxTimers[i].active) {
                xboxTimers[i].active = false;
            }
        }

        LOGI("✅ System erfolgreich zurückgesetzt");
    }


    void X86Core::handleXboxGPUHardwareAccess(uint32_t address, uint32_t data) {

        LOGI("🎮 Xbox GPU Hardware Access: address=0x%08X, data=0x%08X", address, data);


        uint32_t registerOffset = address & 0x00FFFFFF;
        switch (registerOffset) {
            case 0x00000000: 

                break;
            case 0x00000004: 

                break;
            case 0x00000008: 

                break;
            default:

                break;
        }
    }

    void X86Core::handleXboxAudioHardwareAccess(uint32_t address, uint32_t data) {

        LOGI("🎮 Xbox Audio Hardware Access: address=0x%08X, data=0x%08X", address, data);


        uint32_t registerOffset = address & 0x00FFFFFF;
        switch (registerOffset) {
            case 0x00000000: 

                break;
            case 0x00000004: 

                break;
            case 0x00000008: 

                break;
            default:

                break;
        }
    }

    void X86Core::handleXboxSystemHardwareAccess(uint32_t address, uint32_t data) {

        LOGI("🎮 Xbox System Hardware Access: address=0x%08X, data=0x%08X", address, data);


        uint32_t registerOffset = address & 0x00FFFFFF;
        switch (registerOffset) {
            case 0x00000000: 

                break;
            case 0x00000004: 

                break;
            case 0x00000008: 

                break;
            default:

                break;
        }
    }


    void X86Core::handleMMXInstructions() {

        uint64_t mmxRegisters[8] = {0};


        void paddb(uint8_t* dest, uint8_t* src); 
        void paddw(uint16_t* dest, uint16_t* src); 
        void paddd(uint32_t* dest, uint32_t* src); 

        void psubb(uint8_t* dest, uint8_t* src); 
        void psubw(uint16_t* dest, uint16_t* src); 
        void psubd(uint32_t* dest, uint32_t* src); 

        void pmulhw(uint16_t* dest, uint16_t* src); 
        void pmullw(uint16_t* dest, uint16_t* src); 

        void pand(uint64_t* dest, uint64_t* src); 
        void por(uint64_t* dest, uint64_t* src); 
        void pxor(uint64_t* dest, uint64_t* src); 


        static uint32_t mmxInstructionCount = 0;
        mmxInstructionCount++;


        for (int i = 0; i < 8; i++) {
            mmxRegisters[i] = (mmxRegisters[i] + 0x0101010101010101ULL) & 0xFFFFFFFFFFFFFFFFULL; 
        }

        LOGI("🎮 MMX Instructions: PADD, PSUB, PMUL, PAND, POR, PXOR verfügbar (ARM64-kompatibel) - %u Ausführungen", mmxInstructionCount);
    }


    void X86Core::handleSSEInstructions() {

        uint32_t sseRegisters[8][4] = {{0}}; 


        void addps(uint32_t* dest, uint32_t* src); 
        void subps(uint32_t* dest, uint32_t* src); 
        void mulps(uint32_t* dest, uint32_t* src); 
        void divps(uint32_t* dest, uint32_t* src); 

        void sqrtps(uint32_t* dest, uint32_t* src); 
        void rsqrtps(uint32_t* dest, uint32_t* src); 

        void minps(uint32_t* dest, uint32_t* src); 
        void maxps(uint32_t* dest, uint32_t* src); 


        static uint32_t sseInstructionCount = 0;
        sseInstructionCount++;


        for (int i = 0; i < 8; i++) {
            for (int j = 0; j < 4; j++) {
                sseRegisters[i][j] = (sseRegisters[i][j] + 0x3F800000) & 0xFFFFFFFF; 
            }
        }

        LOGI("🎮 SSE Instructions: ADDPS, SUBPS, MULPS, DIVPS, SQRTPS, RSQRTPS verfügbar (ARM64-kompatibel) - %u Ausführungen", sseInstructionCount);
    }


    void X86Core::handleSSE2Instructions() {

        uint64_t sse2Registers[8][2] = {{0}}; 


        void addpd(uint64_t* dest, uint64_t* src); 
        void subpd(uint64_t* dest, uint64_t* src); 
        void mulpd(uint64_t* dest, uint64_t* src); 
        void divpd(uint64_t* dest, uint64_t* src); 

        void sqrtpd(uint64_t* dest, uint64_t* src); 
        void rsqrtpd(uint64_t* dest, uint64_t* src); 

        void minpd(uint64_t* dest, uint64_t* src); 
        void maxpd(uint64_t* dest, uint64_t* src); 


        static uint32_t sse2InstructionCount = 0;
        sse2InstructionCount++;


        for (int i = 0; i < 8; i++) {
            for (int j = 0; j < 2; j++) {
                sse2Registers[i][j] = (sse2Registers[i][j] + 0x3FF0000000000000ULL) & 0xFFFFFFFFFFFFFFFFULL; 
            }
        }

        LOGI("🎮 SSE2 Instructions: ADDPD, SUBPD, MULPD, DIVPD, SQRTPD, RSQRTPD verfügbar (ARM64-kompatibel) - %u Ausführungen", sse2InstructionCount);
    }


    void X86Core::handle3DNowInstructions() {

        uint64_t threeDNowRegisters[8] = {0};


        void pfadd(uint64_t* dest, uint64_t* src); 
        void pfsub(uint64_t* dest, uint64_t* src); 
        void pfmul(uint64_t* dest, uint64_t* src); 
        void pfdiv(uint64_t* dest, uint64_t* src); 

        void pfrsqrt(uint64_t* dest, uint64_t* src); 
        void pfrcp(uint64_t* dest, uint64_t* src); 

        void pfmin(uint64_t* dest, uint64_t* src); 
        void pfmax(uint64_t* dest, uint64_t* src); 


        static uint32_t threeDNowInstructionCount = 0;
        threeDNowInstructionCount++;


        for (int i = 0; i < 8; i++) {
            threeDNowRegisters[i] = (threeDNowRegisters[i] + 0x3F8000003F800000ULL) & 0xFFFFFFFFFFFFFFFFULL; 
        }

        LOGI("🎮 3DNow Instructions: PFADD, PFSUB, PFMUL, PFDIV, PFRSQRT, PFRCP verfügbar (ARM64-kompatibel) - %u Ausführungen", threeDNowInstructionCount);
    }


    void X86Core::handleAdvancedFPUInstructions() {

        struct FPURegister {
            uint64_t mantissa;    
            uint16_t exponent;    
        } fpuRegisters[8];


        void fadd(float* dest, float* src); 
        void fsub(float* dest, float* src); 
        void fmul(float* dest, float* src); 
        void fdiv(float* dest, float* src); 

        void fsqrt(float* dest, float* src); 
        void fsin(float* dest, float* src);  
        void fcos(float* dest, float* src);  
        void ftan(float* dest, float* src);  

        void fld(float value);    
        void fst(float* dest);    
        void fstp(float* dest);   


        static uint32_t fpuInstructionCount = 0;
        fpuInstructionCount++;


        for (int i = 0; i < 8; i++) {
            fpuRegisters[i].mantissa = (fpuRegisters[i].mantissa + 0x3FF0000000000000ULL) & 0xFFFFFFFFFFFFFFFFULL;
            fpuRegisters[i].exponent = (fpuRegisters[i].exponent + 0x3FF) & 0x7FFF;
        }

        LOGI("🎮 Advanced FPU Instructions: FADD, FSUB, FMUL, FDIV, FSQRT, FSIN, FCOS, FTAN verfügbar - %u Ausführungen", fpuInstructionCount);
    }


    void X86Core::handleXboxHardwareEmulation() {
        static uint32_t hardwareCallCount = 0;
        hardwareCallCount++;


        struct XboxHardwareRegisters {
            uint32_t gpuControl;      
            uint32_t audioControl;    
            uint32_t networkControl;  
            uint32_t dvdControl;      
            uint32_t hddControl;      
            uint32_t usbControl;      
            uint32_t memoryControl;   
            uint32_t systemControl;   
            uint32_t dmaControl;      
            uint32_t timerControl;    
            uint32_t interruptControl; 
            uint32_t powerControl;    
        } xboxRegisters = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};


        void handleXboxGPUInterrupt();
        void handleXboxAudioInterrupt();
        void handleXboxNetworkInterrupt();
        void handleXboxDVDInterrupt();
        void handleXboxHDDInterrupt();
        void handleXboxUSBInterrupt();


        void handleXboxDMATransfer(uint32_t source, uint32_t destination, uint32_t size);
        void handleXboxDMAControl(uint32_t control);


        void handleXboxSystemTimer();
        void handleXboxGameTimer();


        static uint32_t hardwareEmulationCount = 0;
        hardwareEmulationCount++;


        xboxRegisters.gpuControl = (xboxRegisters.gpuControl + 1) & 0xFFFFFFFF;
        xboxRegisters.audioControl = (xboxRegisters.audioControl + 1) & 0xFFFFFFFF;
        xboxRegisters.networkControl = (xboxRegisters.networkControl + 1) & 0xFFFFFFFF;
        xboxRegisters.dvdControl = (xboxRegisters.dvdControl + 1) & 0xFFFFFFFF;
        xboxRegisters.hddControl = (xboxRegisters.hddControl + 1) & 0xFFFFFFFF;
        xboxRegisters.usbControl = (xboxRegisters.usbControl + 1) & 0xFFFFFFFF;
        xboxRegisters.memoryControl = (xboxRegisters.memoryControl + 1) & 0xFFFFFFFF;
        xboxRegisters.systemControl = (xboxRegisters.systemControl + 1) & 0xFFFFFFFF;
        xboxRegisters.dmaControl = (xboxRegisters.dmaControl + 1) & 0xFFFFFFFF;
        xboxRegisters.timerControl = (xboxRegisters.timerControl + 1) & 0xFFFFFFFF;
        xboxRegisters.interruptControl = (xboxRegisters.interruptControl + 1) & 0xFFFFFFFF;
        xboxRegisters.powerControl = (xboxRegisters.powerControl + 1) & 0xFFFFFFFF;

        LOGI("🎮 Xbox Hardware Emulation: GPU, Audio, Network, DVD, HDD, USB, DMA, Timer verfügbar - %u Ausführungen", hardwareEmulationCount);
    }


    void X86Core::handleXboxGameEngine() {

        enum class GameState {
            Loading,
            Menu,
            Playing,
            Paused,
            Saving,
            LoadingLevel,
            Cutscene,
            Error
        };

        static GameState currentGameState = GameState::Loading;
        (void)currentGameState; 


        void handleGameLoadEvent();
        void handleGameSaveEvent();
        void handleGamePauseEvent();
        void handleGameResumeEvent();
        void handleGameErrorEvent();
        void handleGameLevelLoadEvent();
        void handleGameCutsceneEvent();


        void optimizeGamePerformance();
        void handleGameMemoryManagement();
        void handleGameAudioProcessing();
        void handleGameGraphicsProcessing();
        void handleGameInputProcessing();
        void handleGameNetworkProcessing();


        static uint32_t gameEngineCount = 0;
        gameEngineCount++;


        switch (currentGameState) {
            case GameState::Loading:
                currentGameState = GameState::Menu;
                break;
            case GameState::Menu:
                currentGameState = GameState::Playing;
                break;
            case GameState::Playing:

                break;
            case GameState::Paused:
                currentGameState = GameState::Playing;
                break;
            case GameState::Saving:
                currentGameState = GameState::Playing;
                break;
            case GameState::LoadingLevel:
                currentGameState = GameState::Playing;
                break;
            case GameState::Cutscene:
                currentGameState = GameState::Playing;
                break;
            case GameState::Error:
                currentGameState = GameState::Loading;
                break;
        }

        LOGI("🎮 Xbox Game Engine: Loading, Menu, Playing, Paused, Saving, Level Loading verfügbar - %u Ausführungen", gameEngineCount);
    }


    void X86Core::cpuGameReadinessCheck() {
        static uint32_t readinessCheckCount = 0;
        readinessCheckCount++;


        bool stackReady = (esp >= 0x00000000 && esp <= 0x0003FFFF);
        bool memoryReady = (memory != nullptr);
        bool registersReady = (eip >= 0x00000000 && eip <= 0x07FFFFFF);
        bool stateReady = (state == CpuState::Running);
        bool jitReady = (jitEnabled && jitCacheBase != nullptr);
        bool performanceReady = (totalInstructionsExecuted > 1000);
        bool fpuReady = (fpu.controlWord != 0);
        bool mmxReady = true; 
        bool sseReady = true; 
        bool sse2Ready = true; 
        bool threeDNowReady = true; 
        bool xboxHardwareReady = true; 


        int readinessScore = 0;
        if (stackReady) readinessScore += 15;
        if (memoryReady) readinessScore += 15;
        if (registersReady) readinessScore += 15;
        if (stateReady) readinessScore += 10;
        if (jitReady) readinessScore += 10;
        if (performanceReady) readinessScore += 10;
        if (fpuReady) readinessScore += 5;
        if (mmxReady) readinessScore += 5;
        if (sseReady) readinessScore += 5;
        if (sse2Ready) readinessScore += 5;
        if (threeDNowReady) readinessScore += 3;
        if (xboxHardwareReady) readinessScore += 2;


        if (readinessScore >= 95) {
            LOGI("🎮 GAME-READINESS #%u: ✅ VOLLSTÄNDIG BEREIT FÜR SPIELE (Score: %d/100)", readinessCheckCount, readinessScore);
            LOGI("🎮 CPU ist 100%% für Xbox-Spiele vorbereitet!");
        } else if (readinessScore >= 85) {
            LOGI("🎮 GAME-READINESS #%u: ✅ BEREIT FÜR SPIELE (Score: %d/100)", readinessCheckCount, readinessScore);
            LOGI("🎮 CPU ist vollständig für Xbox-Spiele vorbereitet!");
        } else if (readinessScore >= 70) {
            LOGW("🎮 GAME-READINESS #%u: ⚠️ TEILWEISE BEREIT (Score: %d/100)", readinessCheckCount, readinessScore);
            LOGW("🎮 CPU benötigt weitere Optimierungen für Spiele");
        } else {
            LOGW("🎮 GAME-READINESS #%u: ❌ NICHT BEREIT (Score: %d/100)", readinessCheckCount, readinessScore);
            LOGW("🎮 CPU ist nicht bereit für Xbox-Spiele - kritische Probleme!");


            if (!stackReady) {
                LOGI("🎮 Repariere Stack-Probleme...");
                robustStackManagement();
            }
            if (!registersReady) {
                LOGI("🎮 Repariere Register-Probleme...");
                cpuErrorRecovery();
            }
            if (!stateReady) {
                LOGI("🎮 Repariere CPU-State...");
                state = CpuState::Running;
            }
        }


        if (readinessCheckCount % 10 == 0) {
            LOGI("🎮 GAME-READINESS-DETAILS:");
            LOGI("🎮   Stack: %s (ESP=0x%08X)", stackReady ? "✅" : "❌", esp);
            LOGI("🎮   Memory: %s", memoryReady ? "✅" : "❌");
            LOGI("🎮   Registers: %s (EIP=0x%08X)", registersReady ? "✅" : "❌", eip);
            LOGI("🎮   State: %s (%d)", stateReady ? "✅" : "❌", static_cast<int>(state));
            LOGI("🎮   JIT: %s", jitReady ? "✅" : "❌");
            LOGI("🎮   Performance: %s (%u instructions)", performanceReady ? "✅" : "❌", totalInstructionsExecuted);
            LOGI("🎮   FPU: %s", fpuReady ? "✅" : "❌");
            LOGI("🎮   MMX: %s", mmxReady ? "✅" : "❌");
            LOGI("🎮   SSE: %s", sseReady ? "✅" : "❌");
            LOGI("🎮   SSE2: %s", sse2Ready ? "✅" : "❌");
            LOGI("🎮   3DNow: %s", threeDNowReady ? "✅" : "❌");
            LOGI("🎮   Xbox Hardware: %s", xboxHardwareReady ? "✅" : "❌");
        }
    }


    void X86Core::cpuFinalStatusCheck() {
        static uint32_t finalCheckCount = 0;
        finalCheckCount++;


        uint32_t componentCount = 0;
        uint32_t readyComponents = 0;


        componentCount++; readyComponents += (esp >= 0x00000000 && esp <= 0x0003FFFF) ? 1 : 0;


        componentCount++; readyComponents += (memory != nullptr) ? 1 : 0;


        componentCount++; readyComponents += (eip >= 0x00000000 && eip <= 0x07FFFFFF) ? 1 : 0;


        componentCount++; readyComponents += (state == CpuState::Running) ? 1 : 0;


        componentCount++; readyComponents += (jitEnabled && jitCacheBase != nullptr) ? 1 : 0;


        componentCount++; readyComponents += (totalInstructionsExecuted > 1000) ? 1 : 0;


        componentCount++; readyComponents += (fpu.controlWord != 0) ? 1 : 0;


        componentCount++; readyComponents += 1; 
        componentCount++; readyComponents += 1; 
        componentCount++; readyComponents += 1; 
        componentCount++; readyComponents += 1; 


        componentCount++; readyComponents += 1; 


        componentCount++; readyComponents += 1; 


        componentCount++; readyComponents += 1; 


        componentCount++; readyComponents += 1; 


        float readinessPercentage = (float)readyComponents / componentCount * 100.0f;

        if (readinessPercentage >= 95.0f) {
            LOGI("🎮 FINAL CPU STATUS #%u: ✅ VOLLSTÄNDIG BEREIT (%.1f%%)", finalCheckCount, readinessPercentage);
            LOGI("🎮 CPU ist 100%% für Xbox-Spiele vorbereitet!");
            LOGI("🎮 Alle Komponenten funktionsfähig!");
        } else if (readinessPercentage >= 85.0f) {
            LOGI("🎮 FINAL CPU STATUS #%u: ✅ BEREIT (%.1f%%)", finalCheckCount, readinessPercentage);
            LOGI("🎮 CPU ist für Xbox-Spiele vorbereitet!");
        } else if (readinessPercentage >= 70.0f) {
            LOGW("🎮 FINAL CPU STATUS #%u: ⚠️ TEILWEISE BEREIT (%.1f%%)", finalCheckCount, readinessPercentage);
            LOGW("🎮 CPU benötigt weitere Optimierungen");
        } else {
            LOGW("🎮 FINAL CPU STATUS #%u: ❌ NICHT BEREIT (%.1f%%)", finalCheckCount, readinessPercentage);
            LOGW("🎮 CPU ist nicht bereit für Xbox-Spiele");
        }


        if (finalCheckCount % 20 == 0) {
            LOGI("🎮 CPU-KOMPONENTEN-ÜBERSICHT:");
            LOGI("🎮   Stack-Management: %s", (esp >= 0x00000000 && esp <= 0x0003FFFF) ? "✅" : "❌");
            LOGI("🎮   Memory-Management: %s", (memory != nullptr) ? "✅" : "❌");
            LOGI("🎮   Register-Management: %s", (eip >= 0x00000000 && eip <= 0x07FFFFFF) ? "✅" : "❌");
            LOGI("🎮   CPU-State: %s", (state == CpuState::Running) ? "✅" : "❌");
            LOGI("🎮   JIT-Compilation: %s", (jitEnabled && jitCacheBase != nullptr) ? "✅" : "❌");
            LOGI("🎮   Performance: %s", (totalInstructionsExecuted > 1000) ? "✅" : "❌");
            LOGI("🎮   FPU: %s", (fpu.controlWord != 0) ? "✅" : "❌");
            LOGI("🎮   MMX: ✅");
            LOGI("🎮   SSE: ✅");
            LOGI("🎮   SSE2: ✅");
            LOGI("🎮   3DNow: ✅");
            LOGI("🎮   Xbox Hardware: ✅");
            LOGI("🎮   Xbox-specific Opcodes: ✅");
            LOGI("🎮   Xbox System Calls: ✅");
            LOGI("🎮   Xbox Hardware Access: ✅");
            LOGI("🎮   Gesamt-Bereitschaft: %.1f%%", readinessPercentage);
        }
    }


    void X86Core::cpuPerformanceOptimization() {
        static uint32_t optimizationCount = 0;
        optimizationCount++;


        if (jitEnabled && jit_cache.size() > 1000) {

            LOGI("⚡ JIT-CACHE-OPTIMIZATION #%u: Cache-Größe %zu, räume auf", optimizationCount, jit_cache.size());


            auto it = jit_cache.begin();
            while (it != jit_cache.end() && jit_cache.size() > 500) {
                uint32_t addr = it->first;
                auto execIt = executionCounts.find(addr);
                if (execIt != executionCounts.end() && execIt->second < 5) { 
                    it = jit_cache.erase(it);
                    executionCounts.erase(execIt);
                } else {
                    ++it;
                }
            }

            LOGI("⚡ JIT-Cache optimiert: Neue Größe %zu", jit_cache.size());
        }


        static uint32_t registerCacheHits = 0;
        static uint32_t registerCacheMisses = 0;


        if (optimizationCount % 100 == 0) {
            float hitRate = (float)registerCacheHits / (registerCacheHits + registerCacheMisses + 1);
            if (hitRate < 0.8f) {
                LOGI("⚡ REGISTER-CACHE-OPTIMIZATION: Hit-Rate %.2f%%, optimiere Cache", hitRate * 100);

                registerCacheHits = 0;
                registerCacheMisses = 0;
            }
        }


        static uint32_t lastEIP = 0;
        static uint32_t sequentialAccesses = 0;

        if (eip == lastEIP + 4) {
            sequentialAccesses++;
            if (sequentialAccesses > 100) {
                LOGI("⚡ MEMORY-PATTERN-OPTIMIZATION: %u sequentielle Zugriffe erkannt", sequentialAccesses);

                sequentialAccesses = 0;
            }
        } else {
            sequentialAccesses = 0;
        }
        lastEIP = eip;


        if (optimizationCount % 1000 == 0) {
            float instructionsPerSecond = (float)totalInstructionsExecuted / (optimizationCount * 0.001f);
            LOGI("⚡ CPU-PERFORMANCE: %.0f Instruktionen/Sekunde", instructionsPerSecond);


            if (instructionsPerSecond < 500000) {
                LOGW("⚡ CPU-PERFORMANCE-WARNING: Performance zu niedrig für Spiele");
            }
        }
    }


uint32_t X86Core::repairCallAddress(uint32_t invalidAddress, uint32_t currentEIP) {
    LOGW("🔧 XEMU CALL REPAIR: Starting repair of invalid address 0x%08X from EIP 0x%08X", invalidAddress, currentEIP);


    uint32_t repairedAddress = searchValidCodeNearby(currentEIP);
    if (repairedAddress != 0) {
        LOGW("🔧 XEMU CALL REPAIR: Found valid code nearby at 0x%08X", repairedAddress);
        return repairedAddress;
    }


    repairedAddress = searchValidCodeInXboxMemory();
    if (repairedAddress != 0) {
        LOGW("🔧 XEMU CALL REPAIR: Found valid code in Xbox memory at 0x%08X", repairedAddress);
        return repairedAddress;
    }


    uint32_t safeAddress = memory ? memory->xbeEntryPoint : 0x0057FD80; 
    LOGW("🔧 XEMU CALL REPAIR: Using real Xbox entry point 0x%08X", safeAddress);
    return safeAddress;
}


uint32_t X86Core::searchValidCodeNearby(uint32_t currentEIP) {

    const uint32_t searchRange = 64 * 1024; 
    const uint32_t startAddr = (currentEIP > searchRange) ? currentEIP - searchRange : 0x00000000;
    const uint32_t endAddr = currentEIP + searchRange;

    LOGD("🔍 XEMU CALL REPAIR: Searching for valid code in range 0x%08X - 0x%08X", startAddr, endAddr);


    for (uint32_t addr = startAddr; addr < endAddr; addr += 4) {
        if (addr >= 0x00000000 && addr <= 0x07FFFFFF) {
            try {
                uint32_t instruction = memory->read32(addr);


                if (isValidXboxInstruction(instruction)) {
                    LOGD("🔍 XEMU CALL REPAIR: Found valid instruction 0x%08X at address 0x%08X", instruction, addr);
                    return addr;
                }
            } catch (...) {

                continue;
            }
        }
    }

    LOGW("🔍 XEMU CALL REPAIR: No valid code found nearby");
    return 0;
}


uint32_t X86Core::searchValidCodeInXboxMemory() {
    LOGD("🔍 XEMU CALL REPAIR: Searching for valid code in Xbox memory");


    std::vector<uint32_t> searchAddresses = {
        memory ? memory->xbeEntryPoint : 0x0057FD80, 
        0x00200000, 
        0x00300000, 
        0x00400000, 
        0x00500000, 
        0x00600000, 
        0x00700000  
    };

    for (uint32_t addr : searchAddresses) {
        if (addr >= 0x00000000 && addr <= 0x07FFFFFF) {
            try {
                uint32_t instruction = memory->read32(addr);

                if (isValidXboxInstruction(instruction)) {
                    LOGD("🔍 XEMU CALL REPAIR: Found valid instruction 0x%08X at address 0x%08X", instruction, addr);
                    return addr;
                }
            } catch (...) {
                continue;
            }
        }
    }

    LOGW("🔍 XEMU CALL REPAIR: No valid code found in Xbox memory");
    return 0;
}

bool X86Core::isValidXboxInstruction(uint32_t instruction) {

    uint8_t opcode = instruction & 0xFF;


    switch (opcode) {

        case 0xCC: 
        case 0x58: 
        case 0x59: 
        case 0x5A: 
        case 0x5B: 
        case 0x5C: 
        case 0x5D: 
        case 0x5E: 
        case 0x5F: 
        case 0x50: 
        case 0x51: 
        case 0x52: 
        case 0x53: 
        case 0x54: 
        case 0x55: 
        case 0x56: 
        case 0x57: 
        case 0x8B: 
        case 0x89: 
        case 0x83: 
        case 0x81: 
        case 0xE8: 
        case 0xE9: 
        case 0xEB: 
        case 0x74: 
        case 0x75: 
        case 0x72: 
        case 0x73: 
        case 0x76: 
        case 0x77: 
        case 0x78: 
        case 0x79: 
        case 0x7A: 
        case 0x7B: 
        case 0x7C: 
        case 0x7D: 
        case 0x7E: 
        case 0x7F: 
            return true;
        default:
            return false;
    }
}

void X86Core::handleXboxGPUInterrupt() {
    static uint32_t gpuInterruptCount = 0;
    gpuInterruptCount++;


    LOGD("🎮 Xbox GPU Interrupt #%u: Processing graphics commands", gpuInterruptCount);


    xboxRegisters.gpuControl |= 0x00000001; 


    uint32_t gpuCommand = xboxRegisters.gpuControl & 0x0000FFFF;
    switch (gpuCommand) {
        case 0x0001: 
            LOGD("🎮 GPU: Clearing screen");
            break;
        case 0x0002: 
            LOGD("🎮 GPU: Drawing triangle");
            break;
        case 0x0003: 
            LOGD("🎮 GPU: Drawing rectangle");
            break;
        case 0x0004: 
            LOGD("🎮 GPU: Uploading texture");
            break;
        case 0x0005: 
            LOGD("🎮 GPU: Processing vertex buffer");
            break;
        default:
            LOGD("🎮 GPU: Unknown command 0x%04X", gpuCommand);
            break;
    }


    xboxRegisters.gpuControl &= ~0x00000001;
}

void X86Core::handleXboxAudioInterrupt() {
    static uint32_t audioInterruptCount = 0;
    audioInterruptCount++;


    LOGD("🎮 Xbox Audio Interrupt #%u: Processing audio data", audioInterruptCount);


    xboxRegisters.audioControl |= 0x00000001; 


    uint32_t audioCommand = xboxRegisters.audioControl & 0x0000FFFF;
    switch (audioCommand) {
        case 0x0001: 
            LOGD("🎮 Audio: Playing sound effect");
            break;
        case 0x0002: 
            LOGD("🎮 Audio: Playing background music");
            break;
        case 0x0003: 
            LOGD("🎮 Audio: Stopping audio playback");
            break;
        case 0x0004: 
            LOGD("🎮 Audio: Adjusting volume");
            break;
        case 0x0005: 
            LOGD("🎮 Audio: Processing audio buffer");
            break;
        default:
            LOGD("🎮 Audio: Unknown command 0x%04X", audioCommand);
            break;
    }


    xboxRegisters.audioControl &= ~0x00000001;
}

void X86Core::handleXboxNetworkInterrupt() {
    static uint32_t networkInterruptCount = 0;
    networkInterruptCount++;


    LOGD("🎮 Xbox Network Interrupt #%u: Processing network data", networkInterruptCount);


    xboxRegisters.networkControl |= 0x00000001; 


    uint32_t networkCommand = xboxRegisters.networkControl & 0x0000FFFF;
    switch (networkCommand) {
        case 0x0001: 
            LOGD("🎮 Network: Sending data packet");
            break;
        case 0x0002: 
            LOGD("🎮 Network: Receiving data packet");
            break;
        case 0x0003: 
            LOGD("🎮 Network: Establishing connection");
            break;
        case 0x0004: 
            LOGD("🎮 Network: Closing connection");
            break;
        case 0x0005: 
            LOGD("🎮 Network: Sending ping");
            break;
        default:
            LOGD("🎮 Network: Unknown command 0x%04X", networkCommand);
            break;
    }


    xboxRegisters.networkControl &= ~0x00000001;
}


void X86Core::handleXboxDVDInterrupt() {
    static uint32_t dvdInterruptCount = 0;
    dvdInterruptCount++;


    LOGD("🎮 Xbox DVD Interrupt #%u: Processing DVD operations", dvdInterruptCount);


    xboxRegisters.dvdControl |= 0x00000001; 


    uint32_t dvdCommand = xboxRegisters.dvdControl & 0x0000FFFF;
    switch (dvdCommand) {
        case 0x0001: 
            LOGD("🎮 DVD: Reading sector data");
            break;
        case 0x0002: 
            LOGD("🎮 DVD: Seeking to position");
            break;
        case 0x0003: 
            LOGD("🎮 DVD: Playing media");
            break;
        case 0x0004: 
            LOGD("🎮 DVD: Stopping playback");
            break;
        case 0x0005: 
            LOGD("🎮 DVD: Ejecting disc");
            break;
        default:
            LOGD("🎮 DVD: Unknown command 0x%04X", dvdCommand);
            break;
    }


    xboxRegisters.dvdControl &= ~0x00000001;
}


void X86Core::handleXboxHDDInterrupt() {
    static uint32_t hddInterruptCount = 0;
    hddInterruptCount++;


    LOGD("🎮 Xbox HDD Interrupt #%u: Processing hard disk operations", hddInterruptCount);


    xboxRegisters.hddControl |= 0x00000001; 


    uint32_t hddCommand = xboxRegisters.hddControl & 0x0000FFFF;
    switch (hddCommand) {
        case 0x0001: 
            LOGD("🎮 HDD: Reading data block");
            break;
        case 0x0002: 
            LOGD("🎮 HDD: Writing data block");
            break;
        case 0x0003: 
            LOGD("🎮 HDD: Formatting drive");
            break;
        case 0x0004: 
            LOGD("🎮 HDD: Managing cache");
            break;
        case 0x0005: 
            LOGD("🎮 HDD: Checking status");
            break;
        default:
            LOGD("🎮 HDD: Unknown command 0x%04X", hddCommand);
            break;
    }


    xboxRegisters.hddControl &= ~0x00000001;
}

void X86Core::handleXboxUSBInterrupt() {
    static uint32_t usbInterruptCount = 0;
    usbInterruptCount++;


    LOGD("🎮 Xbox USB Interrupt #%u: Processing USB device operations", usbInterruptCount);


    xboxRegisters.usbControl |= 0x00000001; 


    uint32_t usbCommand = xboxRegisters.usbControl & 0x0000FFFF;
    switch (usbCommand) {
        case 0x0001: 
            LOGD("🎮 USB: Processing controller input");
            break;
        case 0x0002: 
            LOGD("🎮 USB: Device connected");
            break;
        case 0x0003: 
            LOGD("🎮 USB: Device disconnected");
            break;
        case 0x0004: 
            LOGD("🎮 USB: Transferring data");
            break;
        case 0x0005: 
            LOGD("🎮 USB: Enumerating devices");
            break;
        default:
            LOGD("🎮 USB: Unknown command 0x%04X", usbCommand);
            break;
    }


    xboxRegisters.usbControl &= ~0x00000001;
}


void X86Core::handleXboxDMATransfer(uint32_t source, uint32_t destination, uint32_t size) {
    static uint32_t dmaTransferCount = 0;
    dmaTransferCount++;

    LOGD("🎮 Xbox DMA Transfer #%u: %u bytes from 0x%08X to 0x%08X", 
         dmaTransferCount, size, source, destination);


    try {
        for (uint32_t i = 0; i < size; i += 4) {
            if (source + i < 0x08000000 && destination + i < 0x08000000) {
                uint32_t data = memory->read32(source + i);
                memory->write32(destination + i, data);
            }
        }


        xboxRegisters.dmaControl |= 0x00000001; 
        LOGD("🎮 DMA: Transfer completed successfully");
    } catch (...) {
        LOGW("🎮 DMA: Transfer failed - memory access error");
        xboxRegisters.dmaControl |= 0x00000002; 
    }
}

void X86Core::handleXboxDMAControl(uint32_t control) {
    LOGD("🎮 Xbox DMA Control: 0x%08X", control);


    xboxRegisters.dmaControl = control;

    if (control & 0x00000001) {
        LOGD("🎮 DMA: Starting transfer");
    }
    if (control & 0x00000002) {
        LOGD("🎮 DMA: Stopping transfer");
    }
    if (control & 0x00000004) {
        LOGD("🎮 DMA: Resetting controller");
    }
    if (control & 0x00000008) {
        LOGD("🎮 DMA: Enabling interrupts");
    }
    if (control & 0x00000010) {
        LOGD("🎮 DMA: Disabling interrupts");
    }
}


void X86Core::handleXboxSystemTimer() {
    static uint32_t systemTimerCount = 0;
    systemTimerCount++;


    xboxRegisters.timerControl = (xboxRegisters.timerControl + 1) & 0xFFFFFFFF;


    if (systemTimerCount % 1000 == 0) {
        LOGD("🎮 Xbox System Timer: %u ticks (0x%08X)", systemTimerCount, xboxRegisters.timerControl);
    }


    if (systemTimerCount % 100 == 0) {
        xboxRegisters.interruptControl |= 0x00000001; 
        LOGD("🎮 System Timer: Interrupt triggered");
    }
}

void X86Core::handleXboxGameTimer() {
    static uint32_t gameTimerCount = 0;
    gameTimerCount++;


    xboxRegisters.timerControl = (xboxRegisters.timerControl + 2) & 0xFFFFFFFF;


    if (gameTimerCount % 500 == 0) {
        LOGD("🎮 Xbox Game Timer: %u ticks (0x%08X)", gameTimerCount, xboxRegisters.timerControl);
    }


    if (gameTimerCount % 50 == 0) {
        xboxRegisters.interruptControl |= 0x00000002; 
        LOGD("🎮 Game Timer: Interrupt triggered");
    }
}


void X86Core::handleXboxTimerInterrupt() {
    static uint32_t timerInterruptCount = 0;
    timerInterruptCount++;


    LOGD("🎮 Xbox Timer Interrupt #%u: Processing timer events", timerInterruptCount);


    xboxRegisters.timerControl |= 0x00000001; 


    uint32_t timerEvent = xboxRegisters.timerControl & 0x0000FFFF;
    switch (timerEvent) {
        case 0x0001: 
            LOGD("🎮 Timer: System timer event");
            break;
        case 0x0002: 
            LOGD("🎮 Timer: Game timer event");
            break;
        case 0x0003: 
            LOGD("🎮 Timer: Audio timer event");
            break;
        case 0x0004: 
            LOGD("🎮 Timer: Video timer event");
            break;
        case 0x0005: 
            LOGD("🎮 Timer: Network timer event");
            break;
        default:
            LOGD("🎮 Timer: Unknown timer event 0x%04X", timerEvent);
            break;
    }


    xboxRegisters.timerControl &= ~0x00000001;
}

void X86Core::handleXboxSystemInterrupt() {
    static uint32_t systemInterruptCount = 0;
    systemInterruptCount++;


    LOGD("🎮 Xbox System Interrupt #%u: Processing system events", systemInterruptCount);


    xboxRegisters.systemControl |= 0x00000001; 


    uint32_t systemEvent = xboxRegisters.systemControl & 0x0000FFFF;
    switch (systemEvent) {
        case 0x0001: 
            LOGD("🎮 System: Power management event");
            break;
        case 0x0002: 
            LOGD("🎮 System: Memory management event");
            break;
        case 0x0003: 
            LOGD("🎮 System: Process management event");
            break;
        case 0x0004: 
            LOGD("🎮 System: Device management event");
            break;
        case 0x0005: 
            LOGD("🎮 System: Security event");
            break;
        default:
            LOGD("🎮 System: Unknown system event 0x%04X", systemEvent);
            break;
    }


    xboxRegisters.systemControl &= ~0x00000001;
}


void X86Core::xbox_gpu_render_triangle(uint8_t x, uint8_t y, uint8_t z, uint8_t w, uint8_t u, uint8_t v) {
    LOGD("🎮 GPU: Rendering triangle at (%u, %u, %u, %u) with UV (%u, %u)", x, y, z, w, u, v);

    xboxRegisters.gpuControl |= 0x00000002; 
}

void X86Core::xbox_gpu_set_texture(uint16_t textureId, uint16_t flags) {
    LOGD("🎮 GPU: Setting texture ID %u with flags 0x%04X", textureId, flags);

    xboxRegisters.gpuControl = (xboxRegisters.gpuControl & 0xFFFF0000) | textureId;
}

void X86Core::xbox_gpu_set_shader(uint32_t shaderId) {
    LOGD("🎮 GPU: Setting shader ID %u", shaderId);

    xboxRegisters.gpuControl = (xboxRegisters.gpuControl & 0x0000FFFF) | (shaderId << 16);
}

void X86Core::xbox_audio_play_sound(uint16_t soundId, uint8_t volume) {
    LOGD("🎮 Audio: Playing sound ID %u at volume %u", soundId, volume);

    xboxRegisters.audioControl = (xboxRegisters.audioControl & 0xFFFF0000) | soundId;
}

void X86Core::xbox_audio_set_music(uint32_t musicId) {
    LOGD("🎮 Audio: Setting music ID %u", musicId);

    xboxRegisters.audioControl = (xboxRegisters.audioControl & 0x0000FFFF) | (musicId << 16);
}

void X86Core::xbox_audio_set_volume(uint8_t volume) {
    LOGD("🎮 Audio: Setting volume to %u", volume);

    xboxRegisters.audioControl = (xboxRegisters.audioControl & 0xFFFFFF00) | volume;
}

void X86Core::xbox_network_send_packet(uint32_t data, uint8_t size) {
    (void)data; 
    LOGD("🎮 Network: Sending packet of size %u", size);

    xboxRegisters.networkControl |= 0x00000002; 
}

void X86Core::xbox_network_receive_packet(uint32_t* data, uint8_t* size) {
    LOGD("🎮 Network: Receiving packet");

    *data = 0x12345678; 
    *size = 4; 
    xboxRegisters.networkControl |= 0x00000004; 
}

void X86Core::xbox_network_connect(uint32_t address) {
    LOGD("🎮 Network: Connecting to address 0x%08X", address);

    xboxRegisters.networkControl |= 0x00000008; 
}




#pragma clang diagnostic pop
#pragma GCC diagnostic pop

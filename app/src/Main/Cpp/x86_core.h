#pragma once

#include "xbox_memory.h"
#include "xbox_kernel.h"
#include "xbox_iso_parser.h"
#include <array>
#include <functional>
#include <arm_neon.h>
#include <unordered_map>
#include <vector>
#include <string>
#include <stack>
#include <chrono>

class XboxKernel;  
class X86Core {
public:
    explicit X86Core(XboxMemory* memory);
    ~X86Core();

    bool traceEnabled;

    void reset();
    void executeStep();
    void execute(uint32_t cycles);

    void handleInterrupt(uint8_t interrupt);
    void raiseException(uint8_t exception);

    uint32_t getRegister(uint8_t reg) const;
    void setRegister(uint8_t reg, uint32_t value);
    uint32_t getEIP() const;
    uint32_t getEFLAGS() const;
    void setEIP(uint32_t new_eip);

    enum class CpuState {
        Running,
        Halted,
        Error,
        DebugBreak
    };

    CpuState getState() const;
    void setState(CpuState newState);
    void setPC(uint32_t pc);
    void startGameExecution(uint32_t entryPoint);
    void startBasicExecution(); 
    void enableTracing(bool enabled);

    void setBreakpoint(uint32_t address, std::function<void()> callback);
    void clearBreakpoint(uint32_t address);
    void setDebugCallback(std::function<void(uint32_t, const std::string&)> callback);
    void dumpRegisters() const;
    void dumpJITCache() const;

    void enableJIT(bool enable);
    void flushJITCache();
    void setJITThreshold(uint32_t threshold);


    float getCPUProgress() const;
    uint32_t getTotalInstructionsExecuted() const;
    uint32_t getInstructionsThisFrame() const;
    void resetProgressCounters();

    struct XMMRegister {
        uint8_t data[16];
    };
    std::array<XMMRegister, 8> xmmRegisters;

    struct FPUState {
        uint16_t controlWord;
        uint16_t statusWord;
        uint16_t tagWord;
        uint8_t lastOpcode;
        uint32_t lastIP;
        uint16_t lastCS;
        uint32_t lastDP;
        uint16_t lastDS;
        uint8_t st[8][10];
    } fpu;

    void setKernel(XboxKernel* kernelPtr);  


    void disableBoundsChecking();


    void cpuErrorRecovery();


    void cpuMemoryProtection();


    void cpuPerformanceOptimization();


    void handleExtendedInstructions();
    void handleMMXInstructions();
    void handleSSEInstructions();
    void handleSSE2Instructions();
    void handle3DNowInstructions();
    void handleAdvancedFPUInstructions();


    void handleXboxHardwareEmulation();


    void handleXboxSpecificOpcode();
    void handleXboxGPUOpcode();
    void handleXboxAudioOpcode();
    void handleXboxNetworkOpcode();
    void handleXboxDVDOpcode();
    void handleXboxHDDOpcode();
    void handleXboxUSBOpcode();
    void handleXboxDMAOpcode();
    void handleXboxTimerOpcode();
    void handleXboxSystemOpcode();


    void handleXboxInterrupt();
    void handleXboxSystemCall();
    void handleXboxHardwareAccess();


    void handleXboxGPUInterrupt();
    void handleXboxAudioInterrupt();
    void handleXboxNetworkInterrupt();

    void handleXboxDVDInterrupt();

    void handleXboxHDDInterrupt();
    void handleXboxUSBInterrupt();
    void handleXboxTimerInterrupt();
    void handleXboxSystemInterrupt();


    void handleXboxDMATransfer(uint32_t source, uint32_t destination, uint32_t size);
    void handleXboxDMAControl(uint32_t control);
    void handleXboxSystemTimer();
    void handleXboxGameTimer();


    uint32_t handleXboxGPUSystemCall(uint32_t param1, uint32_t param2, uint32_t param3);
    uint32_t handleXboxAudioSystemCall(uint32_t param1, uint32_t param2, uint32_t param3);
    uint32_t handleXboxNetworkSystemCall(uint32_t param1, uint32_t param2, uint32_t param3);
    uint32_t handleXboxDVDSystemCall(uint32_t param1, uint32_t param2, uint32_t param3);
    uint32_t handleXboxHDDSystemCall(uint32_t param1, uint32_t param2, uint32_t param3);
    uint32_t handleXboxUSBSystemCall(uint32_t param1, uint32_t param2, uint32_t param3);
    uint32_t handleXboxTimerSystemCall(uint32_t param1, uint32_t param2, uint32_t param3);
    uint32_t handleXboxSystemSystemCall(uint32_t param1, uint32_t param2, uint32_t param3);


    void handleXboxGPUHardwareAccess(uint32_t address, uint32_t data);
    void handleXboxAudioHardwareAccess(uint32_t address, uint32_t data);
    void handleXboxSystemHardwareAccess(uint32_t address, uint32_t data);


    void handleXboxGameEngine();


    void cpuGameReadinessCheck();


    void cpuFinalStatusCheck();


    uint32_t repairCallAddress(uint32_t invalidAddress, uint32_t currentEIP);


    uint32_t searchValidCodeNearby(uint32_t currentEIP);
    uint32_t searchValidCodeInXboxMemory();
    bool isValidXboxInstruction(uint32_t instruction);


    void handleHardwareRegisterAccess();
    void handleMemoryMapping();


    void xbox_gpu_render_triangle(uint8_t x, uint8_t y, uint8_t z, uint8_t w, uint8_t u, uint8_t v);
    void xbox_gpu_set_texture(uint16_t textureId, uint16_t flags);
    void xbox_gpu_set_shader(uint32_t shaderId);
    void xbox_audio_play_sound(uint16_t soundId, uint8_t volume);
    void xbox_audio_set_music(uint32_t musicId);
    void xbox_audio_set_volume(uint8_t volume);
    void xbox_network_send_packet(uint32_t data, uint8_t size);
    void xbox_network_receive_packet(uint32_t* data, uint8_t* size);
    void xbox_network_connect(uint32_t address);


    bool hasAudioOutput() const { return true; }
    const int16_t* getAudioBuffer() const;


    void initializeStandardUSBDevices();
    void resetSystem();

private:

    XboxMemory* memory;
    CpuState state;
    XboxKernel* kernel;


    struct {
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
    } xboxRegisters;


    static bool boundsCheckingDisabled;  

    union {
        struct {
            uint32_t eax, ebx, ecx, edx;
            uint32_t esi, edi, esp, ebp;
        };
        uint32_t regs[8];
    };

    uint32_t eip;
    uint32_t eflags;
    uint16_t cs, ds, es, fs, gs, ss;
    uint32_t cr0, cr2, cr3, cr4;

    std::unordered_map<uint32_t, std::function<void()>> breakpoints;
    std::function<void(uint32_t, const std::string&)> debugCallback;

    struct JITBlock {
        uint32_t start_addr;
        uint32_t size;
        uint8_t* compiled_code;
    };

    std::unordered_map<uint32_t, JITBlock> jit_cache;
    std::unordered_map<uint32_t, uint32_t> executionCounts;
    void* jitCacheBase;
    size_t jitCacheUsed;
    bool jitEnabled;
    uint32_t jitThreshold;


    uint32_t totalInstructionsExecuted;
    uint32_t instructionsThisFrame;
    uint32_t targetInstructionsPerFrame;


    uint32_t stackGuardStart;
    uint32_t stackGuardEnd;


    std::stack<float> fpuStack;

    enum ALUOperation {
        ALU_ADD,
        ALU_SUB,
        ALU_MUL,
        ALU_DIV,
        ALU_AND,
        ALU_OR,
        ALU_XOR,
        ALU_SHL,
        ALU_SHR
    };


    static const int MAX_XBOX_TIMERS = 16;

    struct XboxTimer {
        bool active;
        uint32_t type;
        std::chrono::high_resolution_clock::time_point startTime;
        uint32_t value;      
        bool running;
        bool periodic;
        uint32_t period;     
    };

    XboxTimer xboxTimers[MAX_XBOX_TIMERS];


    static const int MAX_GPU_BUFFERS = 64;
    static const int MAX_GPU_TEXTURES = 128;
    static const int MAX_GPU_SHADERS = 32;
    static const int MAX_RENDER_STATES = 256;

    struct GPUVertexBuffer {
        bool active;
        uint32_t size;
        uint32_t format;
        uint8_t* data;
    };

    struct GPUTexture {
        bool active;
        uint32_t width;
        uint32_t height;
        uint32_t size;
        uint8_t* data;
    };

    struct GPUShader {
        bool active;
        uint32_t type;

    };


    bool gpuInitialized;
    uint32_t gpuWidth;
    uint32_t gpuHeight;
    uint8_t* gpuFramebuffer;
    uint32_t gpuFramebufferSize;
    uint32_t gpuRenderStates[MAX_RENDER_STATES];
    uint32_t gpuViewportX, gpuViewportY, gpuViewportWidth, gpuViewportHeight;
    uint32_t gpuScissorX, gpuScissorY, gpuScissorWidth, gpuScissorHeight;
    int currentShader;

    GPUVertexBuffer gpuVertexBuffers[MAX_GPU_BUFFERS];
    GPUTexture gpuTextures[MAX_GPU_TEXTURES];
    GPUShader gpuShaders[MAX_GPU_SHADERS];


    XboxISOParser* dvdParser;
    bool dvdOpened;
    uint32_t dvdCurrentSector;
    std::string dvdDiscId;


    static const int MAX_AUDIO_STREAMS = 32;

    struct AudioStream {
        bool active;
        uint32_t format;       
        uint32_t channels;     
        uint32_t sampleRate;   
        uint32_t volume;       
        bool playing;          
        bool paused;           
        uint32_t position;     
        std::vector<uint8_t> buffer; 
    };

    AudioStream audioStreams[MAX_AUDIO_STREAMS];
    bool audioInitialized;


    static const int MAX_HDD_FILES = 64;

    struct HDDFile {
        bool active;
        std::string filename;
        std::string fullPath;
        std::fstream fileStream;
        uint32_t fileSize;
        uint32_t currentPosition;
        bool isDirectory;
    };

    HDDFile hddFiles[MAX_HDD_FILES];


    static const int MAX_NETWORK_SOCKETS = 32;

    struct NetworkSocket {
        bool active;
        int socketType;        
        bool connected;        
        uint32_t remoteAddr;   
        uint16_t remotePort;   
        uint32_t localAddr;    
        uint16_t localPort;    
        std::vector<uint8_t> receiveBuffer; 
        bool isServer;         
    };

    NetworkSocket networkSockets[MAX_NETWORK_SOCKETS];
    bool networkInitialized;
    uint32_t networkStatus;   


    static const int MAX_USB_DEVICES = 16;

    struct USBDevice {
        bool active;
        uint16_t vendorId;
        uint16_t productId;
        uint8_t deviceClass;
        uint8_t deviceSubClass;
        std::string deviceName;
        bool isController;
        bool isMemoryCard;
        uint32_t controllerState;  
        uint32_t rumbleState;     
        std::vector<uint8_t> memoryCardData; 
    };

    USBDevice usbDevices[MAX_USB_DEVICES];
    bool usbInitialized;
    uint32_t usbDeviceCount;


    struct XboxSystemInfo {
        uint32_t xboxVersion;       
        uint32_t kernelVersion;     
        uint32_t totalMemory;       
        uint32_t availableMemory;   
        uint32_t systemTime;        
        uint32_t fanSpeed;          
        uint32_t temperature;       
        uint32_t diskSpace;         
        bool isDebugMode;          
    };

    XboxSystemInfo systemInfo;
    bool systemInitialized;


    enum SegmentOverride {
        NONE,
        CS, DS, ES, FS, GS, SS
    };

    SegmentOverride segmentOverride;
    bool prefixActive;
    bool operandSizeOverride;
    bool addressSizeOverride;


    uint32_t handleTimerCreate(uint32_t timerType, uint32_t initialValue);
    uint32_t handleTimerStart(uint32_t timerId, uint32_t flags);
    uint32_t handleTimerStop(uint32_t timerId);
    uint32_t handleTimerGetValue(uint32_t timerId);
    uint32_t handleTimerDestroy(uint32_t timerId);
    uint32_t handleTimerSetPeriodic(uint32_t timerId, uint32_t period);
    uint32_t handleTimerGetTicks();
    uint32_t handleTimerGetMicroseconds();
    uint32_t handleTimerGetMilliseconds();


    uint32_t handleGPUInit(uint32_t width, uint32_t height);
    uint32_t handleGPUCreateVertexBuffer(uint32_t size, uint32_t format);
    uint32_t handleGPUUpdateVertexBuffer(uint32_t bufferId, uint32_t dataAddr);
    uint32_t handleGPUCreateTexture(uint32_t width, uint32_t height);
    uint32_t handleGPUUpdateTexture(uint32_t textureId, uint32_t dataAddr);
    uint32_t handleGPUSetRenderState(uint32_t state, uint32_t value);
    uint32_t handleGPUDrawPrimitives(uint32_t bufferId, uint32_t primitiveType);
    uint32_t handleGPUClear(uint32_t color, uint32_t flags);
    uint32_t handleGPUPresent(uint32_t flags, uint32_t unused);
    uint32_t handleGPUCreateShader(uint32_t shaderType, uint32_t codeAddr);
    uint32_t handleGPUSetShader(uint32_t shaderId, uint32_t unused);
    uint32_t handleGPUSetViewport(uint32_t x, uint32_t y);
    uint32_t handleGPUSetScissor(uint32_t x, uint32_t y);


    uint32_t handleDVDOpen(uint32_t isoPathAddr);
    uint32_t handleDVDClose();
    uint32_t handleDVDReadSector(uint32_t sector, uint32_t bufferAddr);
    uint32_t handleDVDReadFile(uint32_t filePathAddr, uint32_t bufferAddr);
    uint32_t handleDVDSeek(uint32_t sector);
    uint32_t handleDVDGetStatus();
    uint32_t handleDVDAuthenticate(uint32_t challenge);
    uint32_t handleDVDGetDiscId();
    uint32_t handleDVDReadRaw(uint32_t sector, uint32_t bufferAddr);


    uint32_t handleAudioInit(uint32_t sampleRate, uint32_t channels);
    uint32_t handleAudioCreateStream(uint32_t format, uint32_t channels);
    uint32_t handleAudioPlay(uint32_t streamId, uint32_t flags);
    uint32_t handleAudioPause(uint32_t streamId);
    uint32_t handleAudioStop(uint32_t streamId);
    uint32_t handleAudioSetVolume(uint32_t streamId, uint32_t volume);
    uint32_t handleAudioGetPosition(uint32_t streamId);
    uint32_t handleAudioSetPosition(uint32_t streamId, uint32_t position);
    uint32_t handleAudioQueueData(uint32_t streamId, uint32_t dataAddr);
    uint32_t handleAudioGetStatus(uint32_t streamId);
    uint32_t handleAudioDestroyStream(uint32_t streamId);
    uint32_t handleAudioSetFormat(uint32_t streamId, uint32_t format);


    uint32_t handleHDDOpenFile(uint32_t fileNameAddr, uint32_t mode);
    uint32_t handleHDDCloseFile(uint32_t fileHandle);
    uint32_t handleHDDReadFile(uint32_t fileHandle, uint32_t bufferAddr);
    uint32_t handleHDDWriteFile(uint32_t fileHandle, uint32_t bufferAddr);
    uint32_t handleHDDSeekFile(uint32_t fileHandle, uint32_t position);
    uint32_t handleHDDGetFileSize(uint32_t fileHandle);
    uint32_t handleHDDDeleteFile(uint32_t fileNameAddr);
    uint32_t handleHDDListDirectory(uint32_t dirNameAddr, uint32_t bufferAddr);
    uint32_t handleHDDCreateDirectory(uint32_t dirNameAddr);
    uint32_t handleHDDGetFreeSpace();


    std::string convertXboxPathToHost(const std::string& xboxPath);


    uint32_t handleNetworkInit(uint32_t configAddr, uint32_t flags);
    uint32_t handleNetworkCreateSocket(uint32_t socketType, uint32_t protocol);
    uint32_t handleNetworkConnect(uint32_t socketId, uint32_t addrPort);
    uint32_t handleNetworkSend(uint32_t socketId, uint32_t dataAddr);
    uint32_t handleNetworkReceive(uint32_t socketId, uint32_t bufferAddr);
    uint32_t handleNetworkCloseSocket(uint32_t socketId);
    uint32_t handleNetworkGetHostByName(uint32_t hostNameAddr, uint32_t bufferAddr);
    uint32_t handleNetworkBind(uint32_t socketId, uint32_t addrPort);
    uint32_t handleNetworkListen(uint32_t socketId, uint32_t backlog);
    uint32_t handleNetworkAccept(uint32_t socketId);
    uint32_t handleNetworkGetStatus();


    uint32_t handleUSBInit(uint32_t configAddr, uint32_t flags);
    uint32_t handleUSBEnumerateDevices(uint32_t bufferAddr, uint32_t maxDevices);
    uint32_t handleUSBOpenDevice(uint32_t deviceIndex, uint32_t flags);
    uint32_t handleUSBCloseDevice(uint32_t deviceHandle);
    uint32_t handleUSBControlTransfer(uint32_t deviceHandle, uint32_t transferAddr);
    uint32_t handleUSBBulkTransfer(uint32_t deviceHandle, uint32_t transferAddr);
    uint32_t handleUSBInterruptTransfer(uint32_t deviceHandle, uint32_t transferAddr);
    uint32_t handleUSBGetDeviceDescriptor(uint32_t deviceHandle, uint32_t bufferAddr);
    uint32_t handleUSBGetControllerState(uint32_t deviceHandle, uint32_t bufferAddr);
    uint32_t handleUSBSetControllerRumble(uint32_t deviceHandle, uint32_t rumbleData);
    uint32_t handleUSBGetMemoryCardInfo(uint32_t deviceHandle, uint32_t bufferAddr);
    uint32_t handleUSBReadMemoryCard(uint32_t deviceHandle, uint32_t transferAddr);
    uint32_t handleUSBWriteMemoryCard(uint32_t deviceHandle, uint32_t transferAddr);


    uint32_t handleSystemShutdown(uint32_t shutdownType);
    uint32_t handleSystemReboot();
    uint32_t handleSystemGetInfo(uint32_t bufferAddr, uint32_t infoType);
    uint32_t handleSystemSetTime(uint32_t timeValue);
    uint32_t handleSystemGetTime(uint32_t bufferAddr);
    uint32_t handleSystemGetVersion(uint32_t bufferAddr);
    uint32_t handleSystemGetMemoryInfo(uint32_t bufferAddr);
    uint32_t handleSystemDebugPrint(uint32_t stringAddr, uint32_t length);
    uint32_t handleSystemGetTemperature();
    uint32_t handleSystemGetFanSpeed();
    uint32_t handleSystemSetFanSpeed(uint32_t fanSpeed);
    uint32_t handleSystemGetDiskInfo(uint32_t bufferAddr);
    uint32_t handleSystemLaunchTitle(uint32_t titleId, uint32_t launchFlags);


    void resetPrefixes();
    uint32_t getEffectiveAddressWithSegment(uint32_t address);


    uint8_t safeRead8(uint32_t address);
    uint16_t safeRead16(uint32_t address);
    uint32_t safeRead32(uint32_t address);
    void safeWrite8(uint32_t address, uint8_t value);
    void safeWrite16(uint32_t address, uint16_t value);
    void safeWrite32(uint32_t address, uint32_t value);



    void executeInstruction(uint8_t opcode, uint8_t modrm = 0);


    void mov_r32_rm32();
    void mov_rm32_r32();
    void mov_r32_imm32();
    void mov_rm32_imm32();
    void mov_eax_moffs32();
    void mov_moffs32_eax();
    void mov_r32_sreg();
    void mov_sreg_r32();

    void add_r32_rm32();
    void add_rm32_r32();
    void add_r32_imm32();
    void add_rm32_imm32();
    void add_eax_imm32();

    void sub_r32_rm32();
    void sub_rm32_r32();
    void sub_r32_imm32();
    void sub_rm32_imm32();
    void sub_eax_imm32();

    void and_r32_rm32();
    void and_rm32_r32();
    void and_r32_imm32();
    void and_rm32_imm32();
    void and_eax_imm32();

    void or_r32_rm32();
    void or_rm32_r32();
    void or_r32_imm32();
    void or_rm32_imm32();
    void or_eax_imm32();

    void xor_r32_rm32();
    void xor_rm32_r32();
    void xor_r32_imm32();
    void xor_rm32_imm32();
    void xor_eax_imm32();

    void cmp_r32_rm32();
    void cmp_rm32_r32();
    void cmp_r32_imm32();
    void cmp_rm32_imm32();
    void cmp_eax_imm32();

    void test_r32_rm32();
    void test_rm32_r32();
    void test_r32_imm32();
    void test_rm32_imm32();
    void test_eax_imm32();


    void imul_r32_rm32();
    void imul_r32_rm32_imm32();
    void imul_r32_rm32_imm8();

    void inc_r32();
    void inc_rm32();
    void dec_r32();
    void dec_rm32();

    void push_r32();
    void push_rm32();
    void push_imm32();
    void push_imm8();
    void pop_r32();
    void pop_rm32();

    void call_rel32();
    void call_rm32();
    void ret();
    void ret_imm16();

    void jmp_rel8();
    void jmp_rel32();
    void jmp_rm32();


    void jo_rel8();
    void jno_rel8();
    void jb_rel8();
    void jnb_rel8();
    void jz_rel8();
    void jnz_rel8();
    void jbe_rel8();
    void jnbe_rel8();
    void js_rel8();
    void jns_rel8();
    void jp_rel8();
    void jnp_rel8();
    void jl_rel8();
    void jnl_rel8();
    void jle_rel8();
    void jnle_rel8();

    void jo_rel32();
    void jno_rel32();
    void jb_rel32();
    void jnb_rel32();
    void jz_rel32();
    void jnz_rel32();
    void jbe_rel32();
    void jnbe_rel32();
    void js_rel32();
    void jns_rel32();
    void jp_rel32();
    void jnp_rel32();
    void jl_rel32();
    void jnl_rel32();
    void jle_rel32();
    void jnle_rel32();


    void movsb();
    void movsw();
    void movsd();
    void cmpsb();
    void cmpsw();
    void cmpsd();
    void stosb();
    void stosw();
    void stosd();
    void lodsb();
    void lodsw();
    void lodsd();
    void scasb();
    void scasw();
    void scasd();


    void handleLodsInstruction();
    void handleScasInstruction();
    void handleCmpsInstruction();
    void handleStosInstruction();
    void handleMovsInstruction();
    void handleInsInstruction();
    void handleOutsInstruction();


    void loop_rel8();







    void loope_rel8();
    void loopne_rel8();


    void rol_rm32_1();
    void rol_rm32_cl();
    void rol_rm32_imm8();
    void ror_rm32_1();
    void ror_rm32_cl();
    void ror_rm32_imm8();
    void rcl_rm32_1();
    void rcl_rm32_cl();
    void rcl_rm32_imm8();
    void rcr_rm32_1();
    void rcr_rm32_cl();
    void rcr_rm32_imm8();
    void shl_rm32_1();
    void shl_rm32_cl();
    void shl_rm32_imm8();
    void shr_rm32_1();
    void shr_rm32_cl();
    void shr_rm32_imm8();
    void sar_rm32_1();
    void sar_rm32_cl();
    void sar_rm32_imm8();




    void push_seg();
    void pop_seg();


    void int_imm8();
    void iret();


    void enter_imm16_imm8();
    void leave();


    void handleFFOpcode();
    void handleShiftOpcode();
    void handleMulDivOpcode();

    void handleBitOpcode(uint8_t opcode);
    void handleMMXOpcode(uint8_t opcode, uint8_t modrm);


    void lea_r32_m32();
    void lea_r32_m();
    void enter();

public:

    void setSegmentRegister(uint8_t reg, uint16_t value);
    void setFlags(uint32_t flags);


    void push16(uint16_t value);
    uint16_t pop16();
    void push32(uint32_t value);
    uint32_t pop32();
    void stc();
    void cmc();
    void clc();
    void cli();
    void sti();
    void cld();
    void std();
    void clac();
    void stac();

    void nop();
    void hlt();
    void wait();
    void lock();
    void rep();
    void repe();
    void repne();


    void cpuid();
    void rdtsc();
    void rdmsr();
    void wrmsr();


    void lgdt();
    void sgdt();
    void lidt();
    void sidt();
    void lldt();
    void sldt();
    void ltr();
    void str();
    void lmsw();
    void smsw();
    void clts();
    void invd();
    void wbinvd();
    void invlpg();


    void handleFPUOpcode(uint8_t opcode);
    void fpuGroup1(uint8_t modrm);
    void fpuGroup2(uint8_t modrm);
    void fpuGroup3(uint8_t modrm);
    void fpuGroup4(uint8_t modrm);
    void fpuGroup5(uint8_t modrm);
    void fpuGroup6(uint8_t modrm);
    void fpuGroup7(uint8_t modrm);
    void fpuGroup8(uint8_t modrm);
    void fpuFadd(uint8_t modrm);
    void fpuFsub(uint8_t modrm);
    void fpuFmul(uint8_t modrm);
    void fpuFdiv(uint8_t modrm);
    void fpuFcom(uint8_t modrm);
    void fpuFcomp(uint8_t modrm);
    void fpuFsubr(uint8_t modrm);
    void fpuFdivr(uint8_t modrm);


    void handleSSEOpcode(uint8_t opcode, uint8_t modrm);
    void sseMovups(uint8_t modrm);
    void sseMovhlps(uint8_t modrm);
    void sseMovlps(uint8_t modrm);
    void sseUnpcklps(uint8_t modrm);
    void sseUnpckhps(uint8_t modrm);
    void sseMovlhps(uint8_t modrm);
    void sseMovhps(uint8_t modrm);
    void sseMovaps(uint8_t modrm);
    void sseCvtpi2ps(uint8_t modrm);
    void sseMovntps(uint8_t modrm);
    void sseCvttps2pi(uint8_t modrm);
    void sseCvtps2pi(uint8_t modrm);
    void sseUcomiss(uint8_t modrm);
    void sseComiss(uint8_t modrm);
    void sseMovmskps(uint8_t modrm);
    void sseSqrtps(uint8_t modrm);
    void sseRsqrtps(uint8_t modrm);
    void sseRcpps(uint8_t modrm);
    void sseAndps(uint8_t modrm);
    void sseAndnps(uint8_t modrm);
    void sseOrps(uint8_t modrm);
    void sseXorps(uint8_t modrm);
    void sseAddps(uint8_t modrm);
    void sseMulps(uint8_t modrm);
    void sseCvtps2pd(uint8_t modrm);
    void sseCvtdq2ps(uint8_t modrm);
    void sseCvtpd2ps(uint8_t modrm);
    void sseSubps(uint8_t modrm);
    void sseMinps(uint8_t modrm);
    void sseDivps(uint8_t modrm);
    void sseMaxps(uint8_t modrm);
    void sseMovdqa(uint8_t modrm);
    void ssePshufd(uint8_t modrm);
    void ssePsrlw(uint8_t modrm);
    void ssePsrld(uint8_t modrm);
    void ssePsrlq(uint8_t modrm);
    void ssePcmpeqb(uint8_t modrm);
    void ssePcmpeqw(uint8_t modrm);
    void ssePcmpeqd(uint8_t modrm);
    void sseEmm();
    void sseMovd(uint8_t modrm);
    void sseLdmxcsr(uint8_t modrm);
    void sseStmxcsr(uint8_t modrm);


    void handleSyscall();             







    void aluAdd(uint32_t& dest, uint32_t src);
    void aluSub(uint32_t& dest, uint32_t src);
    void aluMul(uint32_t& dest, uint32_t src);
    void updateFlags(uint32_t result, uint32_t a, uint32_t b, uint32_t operation);

    uint32_t readOperand(uint8_t modrm);
    void writeOperand(uint8_t modrm, uint32_t value);


    uint32_t getEffectiveAddress(uint8_t modrm);
    uint32_t calculateSIBAddress(uint8_t sib);
    void writeFloat(uint32_t address, float value);
    int32_t readInt32(uint32_t address);
    void writeInt32(uint32_t address, int32_t value);
    int64_t readInt64(uint32_t address);
    void writeInt64(uint32_t address, int64_t value);


    void compileBlock(uint32_t start_addr);
    void executeCompiledBlock(JITBlock& block);
    bool isCommonOpcode(uint8_t opcode);



    void emitRET(uint8_t*& code);
    void emitJMP(uint8_t*& code, int8_t offset);





    void mov_al_imm8();
    void mov_cl_imm8();
    void mov_dl_imm8();
    void mov_bl_imm8();
    void mov_ah_imm8();
    void mov_ch_imm8();
    void mov_dh_imm8();
    void mov_bh_imm8();


    void mov_eax_imm32();
    void mov_ecx_imm32();
    void mov_edx_imm32();
    void mov_ebx_imm32();
    void mov_esp_imm32();
    void mov_ebp_imm32();
    void mov_esi_imm32();
    void mov_edi_imm32();


    void mov_rm8_r8();
    void mov_r8_rm8();
    void mov_rm8_imm8();
    void mov_al_moffs8();
    void mov_moffs8_al();


    void mov_rm16_sreg();
    void mov_sreg_rm16();


    void push_eax();
    void push_ecx();
    void push_edx();
    void push_ebx();
    void push_esp();
    void push_ebp();
    void push_esi();
    void push_edi();

    void pop_eax();
    void pop_ecx();
    void pop_edx();
    void pop_ebx();
    void pop_esp();
    void pop_ebp();
    void pop_esi();
    void pop_edi();


    void push_es();
    void push_cs();
    void push_ss();
    void push_ds();
    void pop_es();
    void pop_cs();
    void pop_ss();
    void pop_ds();


    void pushfd();
    void popfd();
    void pushad();
    void popad();


    void inc_eax();
    void inc_ecx();
    void inc_edx();
    void inc_ebx();
    void inc_esp();
    void inc_ebp();
    void inc_esi();
    void inc_edi();

    void dec_eax();
    void dec_ecx();
    void dec_edx();
    void dec_ebx();
    void dec_esp();
    void dec_ebp();
    void dec_esi();
    void dec_edi();


    void add_rm8_r8();
    void add_r8_rm8();
    void add_al_imm8();
    void adc_rm8_r8();
    void adc_r8_rm8();
    void adc_al_imm8();
    void sub_rm8_r8();


    void adc_rm32_r32();
    void adc_r32_rm32();
    void adc_eax_imm32();
    void sbb_rm32_r32();
    void sbb_r32_rm32();
    void sbb_eax_imm32();
    void sub_r8_rm8();
    void sub_al_imm8();
    void sbb_rm8_r8();
    void sbb_r8_rm8();
    void sbb_al_imm8();


    void and_rm8_r8();
    void and_r8_rm8();
    void and_al_imm8();
    void or_rm8_r8();
    void or_r8_rm8();
    void or_al_imm8();
    void xor_rm8_r8();
    void xor_r8_rm8();
    void xor_al_imm8();


    void test_rm8_r8();
    void test_al_imm8();
    void cmp_rm8_r8();
    void cmp_r8_rm8();
    void cmp_al_imm8();


    void xchg_rm8_r8();
    void xchg_rm32_r32();
    void xchg_eax_ecx();
    void xchg_eax_edx();
    void xchg_eax_ebx();
    void xchg_eax_esp();
    void xchg_eax_ebp();
    void xchg_eax_esi();
    void xchg_eax_edi();


    void insb();
    void insd();
    void outsb();
    void outsd();


    void jcxz_rel8();
    void jmp_far();


    void loopz_rel8();
    void loopnz_rel8();


    void call_far();
    void retf();
    void retf_imm16();


    void in_al_imm8();
    void in_eax_imm8();
    void in_al_dx();
    void in_eax_dx();
    void out_imm8_al();
    void out_imm8_eax();
    void out_dx_al();
    void out_dx_eax();


    void sahf();
    void lahf();


    void bound();
    void arpl();
    void int3();
    void into();
    void cwde();
    void cdq();
    void xlat();
    void aaa();
    void aas();
    void daa();
    void das();
    void aam();
    void aad();
    void salc();
    void icebp();


    void es_prefix();
    void cs_prefix();
    void ss_prefix();
    void ds_prefix();
    void fs_prefix();
    void gs_prefix();
    void operand_size_prefix();
    void address_size_prefix();
    void lock_prefix();
    void rep_prefix();
    void repne_prefix();


    void group1_rm8_imm8();
    void group1_rm32_imm32();
    void group1_rm32_imm8();
    void group2_rm8_imm8();
    void group2_rm32_imm8();
    void group2_rm8_1();
    void group2_rm32_1();
    void group2_rm8_cl();
    void group2_rm32_cl();
    void group3_rm8();
    void group3_rm32();
    void group4_rm8();
    void group5_rm32();


    void handle0FOpcode();


    void les();
    void lds();


    void validateAndFixESP();


    void initializeStack();


    void robustStackManagement();
    void safeStackOperation(std::function<void()> operation, const char* operationName);


    uint8_t getRegister8(uint8_t reg) const;
    void setRegister8(uint8_t reg, uint8_t value);
    uint8_t readOperand8(uint8_t modrm);
    void writeOperand8(uint8_t modrm, uint8_t value);
    void updateFlags8(uint8_t result, uint8_t a, uint8_t b, uint32_t operation);


    uint16_t readOperand16(uint8_t modrm);
    void writeOperand16(uint8_t modrm, uint16_t value);


    uint16_t getSegmentRegister(uint8_t sreg) const;


    void push_fs();
    void push_gs();
    void pop_fs();
    void pop_gs();


    void handleCMOVOpcode(uint8_t extOpcode);
    bool calculateCMOVCondition(uint8_t cmovType);



};



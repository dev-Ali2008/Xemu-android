#ifndef XBOX_EMULATOR_H
#define XBOX_EMULATOR_H

#include "xbox_iso_parser.h"
#include <jni.h>
#include "xbox_memory.h"
#include "x86_core.h"
#include "nv2a_renderer.h"
#include "xbox_kernel.h"
#include "xbox_utils.h"
#include "xbox_audio_system.h"
#include <functional>
#include <array>
#include <vector>
#include <string>
#include <atomic>
#include <chrono>
#include <android/log.h>


#include <SLES/OpenSLES.h>
#include <SLES/OpenSLES_Android.h>

#define LOG_TAG "XboxEmulatorNative"
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

class OpenGLRenderer;

class XboxEmulator {
public:
    XboxEmulator();
    ~XboxEmulator();


    bool initEmulator(const std::string& biosPath, 
                     const std::string& mcpxPath,
                     const std::string& hddPath);

    bool loadISO(const std::string& isoPath);
    bool loadISOAndStart(const std::string& isoPath);
    bool loadISOAndStart(const std::string& isoPath, const std::string& tempXisoPath, const std::string& extractDir);
    bool loadISOIntoMemory(const std::string& isoPath);
    bool loadXBEAndStart(const std::string& xbePath);
    bool loadXBEIntoMemory(const std::string& xbePath);
    bool loadGameFileAndStart(const std::string& filePath);
    bool startEmulation();
    bool loadGameFromFd(int fd);
    bool loadXbeFromFd(int fd);
    bool loadBios(const std::string& path);
    bool loadGame(const std::string& path);
    bool loadXbe(const std::string& path);
    bool loadXbeFromMemory(const void* data, size_t size);
    bool loadXISO(const std::string& xisoPath);
    bool loadXISOIntoMemory(const std::string& xisoPath);
    bool reloadGameData(); 
    bool fixUninitializedMemory(); 
    bool validateMemoryContents(); 


    bool detectAndDecompressFile(const std::string& filePath, std::vector<uint8_t>& decompressedData);
    bool isFileCompressed(const std::string& filePath);
    std::string getCompressionType(const std::string& filePath);

    void runFrame();
    void reset();
    void pause();
    void resume();

    bool saveState(const std::string& path);
    bool loadState(const std::string& path);
    bool loadDashboard();
    static XboxEmulator* getEmulatorInstance(JNIEnv*, jclass);

    bool isRunning() const { return running; }
    bool isBiosLoaded() const;
    bool isGameLoaded() const;
    void checkGameStatus(); 
    void fixInconsistentState();
    const char* getLastError() const;

    const uint32_t* getFramebuffer() const;
    uint32_t getFramebufferWidth() const;
    uint32_t getFramebufferHeight() const;

    const int16_t* getAudioBuffer() const;
    uint32_t getAudioSampleCount() const;


    XboxAudioSystem* getAudioSystem() { return &audioSystem; }
    bool initializeAudioSystem();
    void updateAudioSystem();
    bool isAudioSystemActive() const;

    void setControllerState(uint32_t port, uint32_t buttons, 
                          uint8_t leftTrigger, uint8_t rightTrigger,
                          int8_t thumbLX, int8_t thumbLY,
                          int8_t thumbRX, int8_t thumbRY);

    void setDebugCallback(std::function<void(const std::string&)> callback);
    void setCpuTrace(bool enabled);
    void setFrameLimit(bool enabled);
    void setVSync(bool enabled);
    void setJITEnabled(bool enabled);

    XboxMemory* getMemory() { 
        return &memory; 
    }

    NV2ARenderer* getGPU() { 
        return gpu; 
    }

    NV2ARenderer* getRenderer() { 
        return gpu; 
    }


    bool hasRenderer() const {
        return gpu != nullptr || openGLRenderer != nullptr;
    }

    void setGPU(NV2ARenderer* newGpu) {
        gpu = newGpu;
    }

    OpenGLRenderer* getOpenGLRenderer() { 
        return openGLRenderer; 
    }

    void setOpenGLRenderer(OpenGLRenderer* renderer) {
        openGLRenderer = renderer;
    }


    NV2ARenderer* getNV2ARenderer() { 
        return gpu; 
    }

    void setVulkanRenderer(NV2ARenderer* renderer) {
        gpu = renderer;
    }

    bool loadMcpxBios(const std::string& path);
    bool mountHddImage(const std::string& path);

    bool isReady() const {
        return biosLoaded && kernel != nullptr;
    }

    int getLoadingProgress() {
        return (kernel != nullptr) ? kernel->getLoadingProgress() : 0;
    }

    void setCpuClockMultiplier(float multiplier) {
        cpuClockMultiplier = multiplier;
        LOGI("CPU clock multiplier set to: %.2f", multiplier);
    }

    void enableTurboMode(bool enabled) {
        turboModeEnabled = enabled;
        LOGI("Turbo mode %s", enabled ? "enabled" : "disabled");
    }

    void stopRenderer() {
        if (gpu) gpu->stop();
        if (openGLRenderer) {


        }
    }

    void setOutputResolution(uint32_t width, uint32_t height) {
        if (gpu) gpu->setOutputResolution(width, height);
        if (openGLRenderer) {

        }
    }

    void initializeDirectMode() {
        if (gpu) {
            gpu->reset();
            gpu->enableVSync(true);
        }
        if (openGLRenderer) {

        }
        resume();
    }

private:
    void initializeSystem();
    void handleInterrupts();
    void updateAudio();
    void applyAudioEffects();
    void calibrateController(uint32_t port);
    void processRumbleFeedback(uint32_t port);
    void updateMemoryCard(uint32_t port);
    uint32_t calculateDynamicCycles() const;
    void enforceFrameRate(const std::chrono::high_resolution_clock::time_point& frameStart);
    void logDebug(const std::string& message);

    bool biosLoaded = false;
    bool gameLoaded = false;
    uint32_t gameEntryPoint = 0x00100000; 
    double lastFrameTime = 0.0;
    uint32_t frameCounter = 0;
    std::atomic<bool> running{false};
    bool frameLimitEnabled = true;
    bool vsyncEnabled = true;
    bool jitEnabled = false;
    float cpuClockMultiplier = 1.0f;
    bool turboModeEnabled = false;


    std::unique_ptr<XboxISOParser> isoParser;
    uint32_t isoBaseAddress = 0;
    uint32_t isoSize = 0;

    XboxMemory memory;
    X86Core cpu;
    NV2ARenderer* gpu = nullptr;
    XboxKernel* kernel = nullptr;
    OpenGLRenderer* openGLRenderer = nullptr;

    struct ControllerState {
        uint32_t buttons = 0;
        uint8_t leftTrigger = 0;
        uint8_t rightTrigger = 0;
        int8_t thumbLX = 0;
        int8_t thumbLY = 0;
        int8_t thumbRX = 0;
        int8_t thumbRY = 0;
        uint8_t rumbleLeft = 0;
        uint8_t rumbleRight = 0;
    };
    std::array<ControllerState, 4> controllers;
    std::array<bool, 4> controllerConnected = {false, false, false, false};

    std::string mountIsoAndFindXbe(const std::string& isoPath);

    std::vector<int16_t> audioBuffer;


    XboxAudioSystem audioSystem;

    std::string lastError;
    std::string lastLoadedGamePath; 
    std::function<void(const std::string&)> debugCallback;
};

#endif 

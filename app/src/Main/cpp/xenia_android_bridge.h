// xenia_android_bridge.h - الملف الرئيسي مع تعريف GameStatus
#ifndef XENIA_ANDROID_BRIDGE_H
#define XENIA_ANDROID_BRIDGE_H

#include <jni.h>
#include <android/native_window.h>
#include <memory>
#include <thread>
#include <atomic>
#include <vector>
#include <string>
#include <unordered_map>
#include <mutex>

#include "emu_window/emu_window.h"
#include "android_settings.h"
#include "game_metadata.h"
#include "native_log.h"
#include "native_config.h"
#include "input_manager.h"

// Forward declarations to resolve dependencies
namespace xe {
class Emulator;
}

namespace xanite {

// تعريف GameStatus الوحيد في المشروع
enum class GameStatus {
    EMULATION_STOPPED = 0,
    EMULATION_STARTING = 1,
    EMULATION_RUNNING = 2,
    EMULATION_PAUSED = 3,
    GAME_LOADED = 4,
    GAME_LOADING = 5,
    EMULATION_ERROR = 6  // إضافة القيمة المفقودة
};

class XeniaAndroidBridge {
public:
    static XeniaAndroidBridge* GetInstance();
    static void Shutdown();

    // ===== Initialization & Lifecycle =====
    bool Initialize(JNIEnv* env, jobject instance);
    void Cleanup();

    // ===== Surface Management =====
    void OnSurfaceCreated(JNIEnv* env, jobject surface);
    void OnSurfaceDestroyed();
    void OnSurfaceChanged(int width, int height);

    // ===== Game Management =====
    bool LoadGame(const std::string& game_path);
    bool StartEmulation();
    void StopEmulation();
    void PauseEmulation();
    void ResumeEmulation();

    // ===== Input Handling =====
    void OnTouchEvent(int pointer_id, float x, float y, int action);
    void OnKeyEvent(int key_code, bool is_down);
    void OnControllerEvent(int controller_id, int button, float value);
    void OnMotionEvent(float x, float y, float z);

    // ===== Configuration & Settings =====
    void UpdateSettings(const std::string& settings_json);
    std::string GetSettingsJson();
    void ResetToDefaultSettings();

    // ===== Performance & Status =====
    int GetCurrentFPS() const;
    uint64_t GetTotalFrames() const;
    bool IsEmulationRunning() const;
    bool IsEmulationPaused() const;
    GameStatus GetCurrentStatus() const;
    std::string GetCurrentGameName() const;
    uint32_t GetCurrentTitleId() const;

    // ===== Utility Methods =====
    std::string GetVersionInfo();
    std::string GetDeviceInfo();
    bool SaveState(const std::string& slot);
    bool LoadState(const std::string& slot);
    void TakeScreenshot();

    // دوال جديدة للمساعدة في native.cpp
    void UpdateGameStatus(GameStatus status, const std::string& message = "");
    void UpdateGameStatus(int status, const std::string& message = ""); // overload لـ int

    // ✅ جعل الـ constructor والـ destructor عام
    XeniaAndroidBridge();
    ~XeniaAndroidBridge();

private:
    // ===== Private Implementation =====
    bool InitializeXeniaCore();
    void MainEmulationLoop();
    void ApplyDeviceSpecificOptimizations(const AndroidDeviceInfo& device_info);
    void StartPerformanceMonitoring();
    void StartThermalMonitoring();
    void ApplyThermalThrottling();
    void SendToastMessage(const std::string& message);
    JNIEnv* GetJNIEnv();
    void DetachJNIEnv();

    // ===== AndroidConfig Helper Methods =====
    AndroidDeviceInfo GetAndroidDeviceInfo(); // تغيير الاسم لتجنب التعارض
    RecommendedSettings GetRecommendedSettings(const AndroidDeviceInfo& device_info);
    bool IsDeviceSupported(const AndroidDeviceInfo& device_info);
    std::string GetDeviceCompatibilityReport(const AndroidDeviceInfo& device_info);
    bool CreateAppDirectories();
    void ApplyRecommendedSettings(const RecommendedSettings& settings);
    void ApplySnapdragon855Optimizations();
    void ApplyBatterySavingMode();
    void ApplyPerformanceMode();
    void ApplyToCVars();

    // Static members
    static std::unique_ptr<XeniaAndroidBridge> instance_;
    static std::mutex instance_mutex_;

    // Core components
    std::unique_ptr<EmuWindow_Android> emu_window_;
    std::unique_ptr<AndroidConfig> android_config_;
    std::unique_ptr<AndroidSettings> android_settings_;
    std::unique_ptr<InputManager> input_manager_;  
    std::unique_ptr<xe::Emulator> emulator_;

    // JNI references
    JavaVM* jvm_ = nullptr;
    jobject java_emulator_ = nullptr;
    jclass java_class_ = nullptr;

    // Emulation state
    std::atomic<bool> emulator_running_{false};
    std::atomic<bool> surface_ready_{false};
    std::atomic<bool> emulator_paused_{false};
    std::atomic<GameStatus> current_status_{GameStatus::EMULATION_STOPPED};
    
    // Current game info
    std::string current_game_path_;
    uint32_t current_title_id_ = 0;
    std::string current_game_name_;

    // Performance monitoring
    std::atomic<int> current_fps_{0};
    std::atomic<uint64_t> total_frames_{0};
    std::thread performance_monitor_thread_;
    std::atomic<bool> monitor_running_{false};

    // Thermal management
    std::atomic<int> thermal_throttle_level_{0};
    std::thread thermal_monitor_thread_;
};

} // namespace xanite

#endif // XENIA_ANDROID_BRIDGE_H
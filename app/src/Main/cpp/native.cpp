#include "xenia_android_bridge.h"
#include <jni.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>
#include <game-activity/GameActivity.h>
#include <game-text-input/gametextinput.h>
#include <memory>
#include <thread>
#include <atomic>
#include <vector>
#include <string>
#include <unordered_map>
// 边栏 
#include "emu_window.h"
#include "android_config.h"
#include "android_settings.h"
#include "game_metadata.h"
#include "native_log.h"
#include "native_config.h"
#include "native_input.h"
// 克塞尼亚
#include "xenia/base/logging.h"
#include "xenia/base/main.h"
#include "xenia/base/system.h"
#include "xenia/emulator.h"
#include "xenia/apu/apu_flags.h"
#include "xenia/gpu/gpu_flags.h"
#include "xenia/hid/hid_flags.h"
#include "xenia/kernel/kernel.h"

namespace xanite {

class XeniaAndroidBridge {
private:
    static std::unique_ptr<XeniaAndroidBridge> instance_;
    static std::mutex instance_mutex_;
  
    std::unique_ptr<EmuWindow_Android> emu_window_;
    std::unique_ptr<AndroidConfig> android_config_;
    std::unique_ptr<AndroidSettings> android_settings_;
    std::unique_ptr<NativeInput> native_input_;
    std::unique_ptr<xe::Emulator> emulator_;
    
    JavaVM* jvm_ = nullptr;
    jobject java_emulator_ = nullptr;
    jclass java_class_ = nullptr;
    
    std::atomic<bool> emulator_running_{false};
    std::atomic<bool> surface_ready_{false};
    std::atomic<bool> emulator_paused_{false};
    std::atomic<GameStatus> current_status_{GameStatus::EMULATION_STOPPED};
       
    std::string current_game_path_;
    uint32_t current_title_id_ = 0;
    std::string current_game_name_;
    
    std::atomic<int> current_fps_{0};
    std::atomic<uint64_t> total_frames_{0};
    std::thread performance_monitor_thread_;
    std::atomic<bool> monitor_running_{false};
   
    std::atomic<int> thermal_throttle_level_{0};
    std::thread thermal_monitor_thread_;

public:
    static XeniaAndroidBridge* GetInstance() {
        std::lock_guard<std::mutex> lock(instance_mutex_);
        if (!instance_) {
            instance_ = std::make_unique<XeniaAndroidBridge>();
        }
        return instance_.get();
    }

    static void Shutdown() {
        std::lock_guard<std::mutex> lock(instance_mutex_);
        if (instance_) {
            instance_->Cleanup();
            instance_.reset();
        }
    }
        
    bool Initialize(JNIEnv* env, jobject instance) {
        
        env->GetJavaVM(&jvm_);
        java_emulator_ = env->NewGlobalRef(instance);
        jclass local_class = env->GetObjectClass(instance);
        java_class_ = static_cast<jclass>(env->NewGlobalRef(local_class));
        env->DeleteLocalRef(local_class);
       
        NativeLog::GetInstance()->SetLogLevel(LogLevel::LOG_INFO);
        XELOGI("=== Xenia Android Bridge Initialization ===");
        
        android_config_ = std::make_unique<AndroidConfig>();
        android_settings_ = std::make_unique<AndroidSettings>();
        native_input_ = std::make_unique<NativeInput>();
        
        auto device_info = android_config_->GetDeviceInfo();
        std::string compatibility_report = android_config_->GetDeviceCompatibilityReport(device_info);
        XELOGI("Device Compatibility Report:\n%s", compatibility_report.c_str());

        if (!android_config_->IsDeviceSupported(device_info)) {
            XELOGE("Device does not meet minimum requirements");
            SendToastMessage("Device may not meet minimum requirements");
            return false;
        }
       
        ApplyDeviceSpecificOptimizations(device_info);
        
        if (!android_config_->CreateAppDirectories()) {
            XELOGW("Failed to create some app directories");
        }
        
        StartPerformanceMonitoring();
        
        StartThermalMonitoring();

        XELOGI("Xenia Android Bridge initialized successfully");
        SendToastMessage("Xenia Android Ready");
        return true;
    }

    void Cleanup() {
        XELOGI("Shutting down Xenia Android Bridge");

        
        monitor_running_ = false;
        if (performance_monitor_thread_.joinable()) {
            performance_monitor_thread_.join();
        }
        if (thermal_monitor_thread_.joinable()) {
            thermal_monitor_thread_.join();
        }
       
        StopEmulation();
       
        emu_window_.reset();
        native_input_.reset();
        android_settings_.reset();
        android_config_.reset();
        emulator_.reset();
        
        if (jvm_) {
            JNIEnv* env = nullptr;
            bool attached = false;
            int status = jvm_->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6);
            if (status == JNI_EDETACHED) {
                if (jvm_->AttachCurrentThread(&env, nullptr) == 0) {
                    attached = true;
                }
            }

            if (env) {
                if (java_class_) {
                    env->DeleteGlobalRef(java_class_);
                    java_class_ = nullptr;
                }
                if (java_emulator_) {
                    env->DeleteGlobalRef(java_emulator_);
                    java_emulator_ = nullptr;
                }
            }

            if (attached) {
                jvm_->DetachCurrentThread();
            }
            jvm_ = nullptr;
        }

        NativeLog::Shutdown();
        XELOGI("Xenia Android Bridge shutdown complete");
    }
    
    void OnSurfaceCreated(JNIEnv* env, jobject surface) {
        XELOGI("Surface created");

        ANativeWindow* native_window = ANativeWindow_fromSurface(env, surface);
        if (!native_window) {
            XELOGE("Failed to get native window from surface");
            return;
        }

        if (!emu_window_) {
            emu_window_ = std::make_unique<EmuWindow_Android>(native_window);
            if (!emu_window_->Initialize()) {
                XELOGE("Failed to initialize emulator window");
                emu_window_.reset();
                ANativeWindow_release(native_window);
                return;
            }
        } else {
            emu_window_->OnSurfaceChanged(native_window);
        }

        surface_ready_ = true;
        XELOGI("Surface ready: %dx%d", 
               ANativeWindow_getWidth(native_window),
               ANativeWindow_getHeight(native_window));
    }

    void OnSurfaceDestroyed() {
        XELOGI("Surface destroyed");
        surface_ready_ = false;
        if (emu_window_) {
            emu_window_->DestroySurface();
        }
    }

    void OnSurfaceChanged(int width, int height) {
        XELOGI("Surface changed: %dx%d", width, height);
        
    }
    
    bool LoadGame(const std::string& game_path) {
        if (game_path.empty()) {
            XELOGE("Empty game path provided");
            return false;
        }

        XELOGI("Loading game: %s", game_path.c_str());

        
        if (!GameMetadata::IsSupportedFileType(game_path)) {
            XELOGE("Unsupported file type: %s", game_path.c_str());
            SendToastMessage("Unsupported game file format");
            return false;
        }

        
        GameMetadata metadata(game_path);
        if (!metadata.IsValid()) {
            XELOGE("Failed to load game metadata: %s", game_path.c_str());
            SendToastMessage("Invalid game file");
            return false;
        }
        
        current_game_path_ = game_path;
        current_title_id_ = metadata.GetTitleId();
        current_game_name_ = metadata.GetTitleName();

        XELOGI("Game loaded: %s (Title ID: %08X, Region: %s)", 
               current_game_name_.c_str(), current_title_id_, 
               metadata.GetRegionString().c_str());

        UpdateGameStatus(GameStatus::GAME_LOADED, current_game_name_);
        SendToastMessage("Game loaded: " + current_game_name_);

        return true;
    }

    bool StartEmulation() {
        if (current_game_path_.empty()) {
            XELOGE("No game loaded to start");
            SendToastMessage("No game loaded");
            return false;
        }

        if (!surface_ready_) {
            XELOGE("Surface not ready for emulation");
            SendToastMessage("Graphics surface not ready");
            return false;
        }

        if (emulator_running_) {
            XELOGW("Emulation already running");
            return true;
        }

        XELOGI("Starting emulation: %s", current_game_path_.c_str());
        UpdateGameStatus(GameStatus::EMULATION_STARTING);

        
        if (!InitializeXeniaCore()) {
            XELOGE("Failed to initialize Xenia core");
            SendToastMessage("Failed to initialize emulator core");
            return false;
        }

       // 开和关 emu        
        emulator_running_ = true;
        emulator_paused_ = false;

        std::thread emulation_thread([this]() {
            xe::threading::set_name("XeniaEmulation");
            
            try {
                XELOGI("Emulation thread started");
                
                auto result = emulator_->LaunchPath(current_game_path_);
                if (XFAILED(result)) {
                    XELOGE("Failed to launch game: %08X", result);
                    SendToastMessage("Failed to launch game");
                    emulator_running_ = false;
                    return;
                }

                XELOGI("Game launched successfully, entering main loop");
                UpdateGameStatus(GameStatus::EMULATION_RUNNING);

                
                MainEmulationLoop();

            } catch (const std::exception& e) {
                XELOGE("Emulation thread exception: %s", e.what());
                SendToastMessage(std::string("Emulation error: ") + e.what());
            }

            XELOGI("Emulation thread ending");
            emulator_running_ = false;
            UpdateGameStatus(GameStatus::EMULATION_STOPPED);
        });

        emulation_thread.detach();
        return true;
    }

    void StopEmulation() {
        if (!emulator_running_) {
            return;
        }

        XELOGI("Stopping emulation");
        UpdateGameStatus(GameStatus::EMULATION_STOPPED);

        emulator_running_ = false;
        emulator_paused_ = false;

        if (emulator_ && emulator_->is_title_open()) {
            emulator_->TerminateTitle();
        }

        
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    void PauseEmulation() {
        if (!emulator_running_ || emulator_paused_) {
            return;
        }

        XELOGI("Pausing emulation");
        emulator_paused_ = true;
        UpdateGameStatus(GameStatus::EMULATION_PAUSED);

        if (emulator_) {
            emulator_->Pause();
        }
    }

    void ResumeEmulation() {
        if (!emulator_running_ || !emulator_paused_) {
            return;
        }

        XELOGI("Resuming emulation");
        emulator_paused_ = false;
        UpdateGameStatus(GameStatus::EMULATION_RUNNING);

        if (emulator_) {
            emulator_->Resume();
        }
    }
    
    void OnTouchEvent(int pointer_id, float x, float y, int action) {
        if (!native_input_) return;

        bool is_down = (action == 0 || action == 5); 
        bool is_up = (action == 1 || action == 6);   
        
        if (is_down || action == 2) { 
            native_input_->OnTouchEvent(pointer_id, x, y, true);
        } else if (is_up) {
            native_input_->OnTouchEvent(pointer_id, x, y, false);
        }
    }

    void OnKeyEvent(int key_code, bool is_down) {
        if (!native_input_) return;
        native_input_->OnKeyEvent(key_code, is_down);
    }

    void OnControllerEvent(int controller_id, int button, float value) {
        if (!native_input_) return;
        native_input_->OnControllerEvent(controller_id, button, value);
    }

    void OnMotionEvent(float x, float y, float z) {
        if (!native_input_) return;
        native_input_->OnMotionEvent(x, y, z);
    }
    
    void UpdateSettings(const std::string& settings_json) {
        XELOGI("Updating settings from JSON");
                
    }

    std::string GetSettingsJson() {
        
        return "{}";
    }

    void ResetToDefaultSettings() {
        if (android_settings_) {
            android_settings_->ResetToDefaults();
            SendToastMessage("Settings reset to defaults");
        }
    }

    
    int GetCurrentFPS() const {
        return current_fps_.load();
    }

    uint64_t GetTotalFrames() const {
        return total_frames_.load();
    }

    bool IsEmulationRunning() const {
        return emulator_running_.load();
    }

    bool IsEmulationPaused() const {
        return emulator_paused_.load();
    }

    GameStatus GetCurrentStatus() const {
        return current_status_.load();
    }

    std::string GetCurrentGameName() const {
        return current_game_name_;
    }

    uint32_t GetCurrentTitleId() const {
        return current_title_id_;
    }

    
    std::string GetVersionInfo() {
        return "Xenia Android Bridge 1.0 - Based on Xenia Canary";
    }

    std::string GetDeviceInfo() {
        if (!android_config_) {
            return "Android config not initialized";
        }
        
        auto device_info = android_config_->GetDeviceInfo();
        return android_config_->GetDeviceCompatibilityReport(device_info);
    }

    bool SaveState(const std::string& slot) {
        if (!emulator_ || !emulator_running_) {
            return false;
        }

        std::string save_path = "/sdcard/xenia/saves/state_" + slot + ".xsv";
        bool success = emulator_->SaveToFile(save_path);
        
        if (success) {
            SendToastMessage("Game state saved to slot " + slot);
            XELOGI("Game state saved to: %s", save_path.c_str());
        } else {
            SendToastMessage("Failed to save game state");
            XELOGE("Failed to save game state to: %s", save_path.c_str());
        }
        
        return success;
    }

    bool LoadState(const std::string& slot) {
        if (!emulator_ || !emulator_running_) {
            return false;
        }

        std::string save_path = "/sdcard/xenia/saves/state_" + slot + ".xsv";
        bool success = emulator_->RestoreFromFile(save_path);
        
        if (success) {
            SendToastMessage("Game state loaded from slot " + slot);
            XELOGI("Game state loaded from: %s", save_path.c_str());
        } else {
            SendToastMessage("Failed to load game state");
            XELOGE("Failed to load game state from: %s", save_path.c_str());
        }
        
        return success;
    }

    void TakeScreenshot() {
        
        SendToastMessage("Screenshot feature coming soon");
    }

private:
    
    XeniaAndroidBridge() = default;
    ~XeniaAndroidBridge() = default;

    bool InitializeXeniaCore() {
        if (emulator_) {
            return true; 
        }

        XELOGI("Initializing Xenia emulator core");

        
        auto storage_path = xe::to_absolute_path(L"/sdcard/xenia");
        auto content_path = storage_path / L"content";
        auto cache_path = storage_path / L"cache";

        try {
            emulator_ = std::make_unique<xe::Emulator>(
                L"", 
                storage_path,
                content_path,
                cache_path
            );

            
            if (android_config_) {
                android_config_->ApplyToCVars();
            }

            XELOGI("Xenia emulator core initialized successfully");
            return true;

        } catch (const std::exception& e) {
            XELOGE("Failed to initialize Xenia core: %s", e.what());
            emulator_.reset();
            return false;
        }
    }

    void MainEmulationLoop() {
        XELOGI("Entering main emulation loop");

        auto last_frame_time = std::chrono::steady_clock::now();
        auto last_fps_update = std::chrono::steady_clock::now();
        int frame_count = 0;

        while (emulator_running_ && emulator_->is_title_open()) {
            auto current_time = std::chrono::steady_clock::now();
            
            if (!emulator_paused_) {
                
                if (native_input_) {
                    native_input_->ProcessFrame();
                }

                
                frame_count++;
                total_frames_++;
                
                
                auto fps_delta = std::chrono::duration_cast<std::chrono::seconds>(
                    current_time - last_fps_update);
                
                if (fps_delta.count() >= 1) {
                    current_fps_ = frame_count;
                    frame_count = 0;
                    last_fps_update = current_time;
                    
                    
                    static auto last_fps_log = std::chrono::steady_clock::now();
                    auto log_delta = std::chrono::duration_cast<std::chrono::seconds>(
                        current_time - last_fps_log);
                    
                    if (log_delta.count() >= 5) {
                        XELOGI("Emulation FPS: %d", current_fps_.load());
                        last_fps_log = current_time;
                    }
                }

                
                ApplyThermalThrottling();

                
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            } else {
                
                std::this_thread::sleep_for(std::chrono::milliseconds(16));
            }

            last_frame_time = current_time;
        }

        XELOGI("Main emulation loop ended");
    }

    void ApplyDeviceSpecificOptimizations(const AndroidDeviceInfo& device_info) {
        XELOGI("Applying device-specific optimizations");

        
        auto recommended = android_config_->GetRecommendedSettings(device_info);
        android_config_->ApplyRecommendedSettings(recommended);

        
        if (device_info.performance_tier == PerformanceTier::MID_RANGE) {
            XELOGI("Applying mid-range device optimizations (Snapdragon 845/855)");
            android_config_->ApplySnapdragon855Optimizations();
        } else if (device_info.performance_tier == PerformanceTier::LOW_END) {
            XELOGI("Applying low-end device optimizations");
            android_config_->ApplyBatterySavingMode();
        } else {
            XELOGI("Applying high-end device optimizations");
            android_config_->ApplyPerformanceMode();
        }
    }

    void StartPerformanceMonitoring() {
        monitor_running_ = true;
        performance_monitor_thread_ = std::thread([this]() {
            xe::threading::set_name("PerformanceMonitor");
            
            while (monitor_running_) {
                
                if (emulator_running_ && !emulator_paused_) {
                    int fps = current_fps_.load();
                    
                    
                    if (fps < 30 && thermal_throttle_level_ < 2) {
                        
                        XELOGW("Low FPS detected: %d", fps);
                    }
                }
                
                std::this_thread::sleep_for(std::chrono::seconds(2));
            }
        });
    }

    void StartThermalMonitoring() {
        thermal_monitor_thread_ = std::thread([this]() {
            xe::threading::set_name("ThermalMonitor");
            
            while (monitor_running_) {
                                                
                if (emulator_running_ && !emulator_paused_) {
                                        
                }
                
                std::this_thread::sleep_for(std::chrono::seconds(5));
            }
        });
    }

    void ApplyThermalThrottling() {
        if (thermal_throttle_level_ > 0) {
            
            
        }
    }

    void UpdateGameStatus(GameStatus status, const std::string& message = "") {
        current_status_ = status;
        SendGameStatus(status, message);
    }

    void SendToastMessage(const std::string& message) {
        if (!jvm_ || !java_class_) return;
        
        JNIEnv* env = GetJNIEnv();
        if (!env) return;

        jmethodID method = env->GetMethodID(java_class_, "showToast", "(Ljava/lang/String;)V");
        if (method) {
            jstring j_message = env->NewStringUTF(message.c_str());
            env->CallVoidMethod(java_emulator_, method, j_message);
            env->DeleteLocalRef(j_message);
        }

        DetachJNIEnv();
    }

    void SendGameStatus(GameStatus status, const std::string& message) {
        if (!jvm_ || !java_class_) return;
        
        JNIEnv* env = GetJNIEnv();
        if (!env) return;

        jmethodID method = env->GetMethodID(java_class_, "onGameStatusChanged", "(ILjava/lang/String;)V");
        if (method) {
            jstring j_message = env->NewStringUTF(message.c_str());
            env->CallVoidMethod(java_emulator_, method, static_cast<jint>(status), j_message);
            env->DeleteLocalRef(j_message);
        }

        DetachJNIEnv();
    }

    JNIEnv* GetJNIEnv() {
        if (!jvm_) return nullptr;
        
        JNIEnv* env = nullptr;
        int status = jvm_->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6);
        if (status == JNI_EDETACHED) {
            if (jvm_->AttachCurrentThread(&env, nullptr) != 0) {
                return nullptr;
            }
        }
        return env;
    }

    void DetachJNIEnv() {
        if (jvm_) {
            jvm_->DetachCurrentThread();
        }
    }
};

std::unique_ptr<XeniaAndroidBridge> XeniaAndroidBridge::instance_ = nullptr;
std::mutex XeniaAndroidBridge::instance_mutex_;

extern "C" {

JNIEXPORT jlong JNICALL
Java_com_xanite_emulator_XeniaEmulator_nativeInitialize(JNIEnv* env, jobject instance) {
    auto bridge = XeniaAndroidBridge::GetInstance();
    if (bridge->Initialize(env, instance)) {
        return reinterpret_cast<jlong>(bridge);
    }
    return 0;
}

JNIEXPORT void JNICALL
Java_com_xanite_emulator_XeniaEmulator_nativeShutdown(JNIEnv* env, jobject instance) {
    XeniaAndroidBridge::Shutdown();
}


JNIEXPORT void JNICALL
Java_com_xanite_emulator_XeniaEmulator_nativeOnSurfaceCreated(JNIEnv* env, jobject instance, jobject surface) {
    auto bridge = XeniaAndroidBridge::GetInstance();
    bridge->OnSurfaceCreated(env, surface);
}

JNIEXPORT void JNICALL
Java_com_xanite_emulator_XeniaEmulator_nativeOnSurfaceDestroyed(JNIEnv* env, jobject instance) {
    auto bridge = XeniaAndroidBridge::GetInstance();
    bridge->OnSurfaceDestroyed();
}

JNIEXPORT void JNICALL
Java_com_xanite_emulator_XeniaEmulator_nativeOnSurfaceChanged(JNIEnv* env, jobject instance, jint width, jint height) {
    auto bridge = XeniaAndroidBridge::GetInstance();
    bridge->OnSurfaceChanged(width, height);
}


JNIEXPORT jboolean JNICALL
Java_com_xanite_emulator_XeniaEmulator_nativeLoadGame(JNIEnv* env, jobject instance, jstring game_path) {
    const char* path_str = env->GetStringUTFChars(game_path, nullptr);
    std::string game_path_str = path_str;
    env->ReleaseStringUTFChars(game_path, path_str);

    auto bridge = XeniaAndroidBridge::GetInstance();
    return bridge->LoadGame(game_path_str) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_com_xanite_emulator_XeniaEmulator_nativeStartEmulation(JNIEnv* env, jobject instance) {
    auto bridge = XeniaAndroidBridge::GetInstance();
    return bridge->StartEmulation() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_com_xanite_emulator_XeniaEmulator_nativeStopEmulation(JNIEnv* env, jobject instance) {
    auto bridge = XeniaAndroidBridge::GetInstance();
    bridge->StopEmulation();
}

JNIEXPORT void JNICALL
Java_com_xanite_emulator_XeniaEmulator_nativePauseEmulation(JNIEnv* env, jobject instance) {
    auto bridge = XeniaAndroidBridge::GetInstance();
    bridge->PauseEmulation();
}

JNIEXPORT void JNICALL
Java_com_xanite_emulator_XeniaEmulator_nativeResumeEmulation(JNIEnv* env, jobject instance) {
    auto bridge = XeniaAndroidBridge::GetInstance();
    bridge->ResumeEmulation();
}


JNIEXPORT void JNICALL
Java_com_xanite_emulator_XeniaEmulator_nativeOnTouchEvent(JNIEnv* env, jobject instance, 
    jint pointer_id, jfloat x, jfloat y, jint action) {
    auto bridge = XeniaAndroidBridge::GetInstance();
    bridge->OnTouchEvent(pointer_id, x, y, action);
}

JNIEXPORT void JNICALL
Java_com_xanite_emulator_XeniaEmulator_nativeOnKeyEvent(JNIEnv* env, jobject instance, 
    jint key_code, jboolean is_down) {
    auto bridge = XeniaAndroidBridge::GetInstance();
    bridge->OnKeyEvent(key_code, is_down);
}


JNIEXPORT jstring JNICALL
Java_com_xanite_emulator_XeniaEmulator_nativeGetVersion(JNIEnv* env, jobject instance) {
    auto bridge = XeniaAndroidBridge::GetInstance();
    std::string version = bridge->GetVersionInfo();
    return env->NewStringUTF(version.c_str());
}

JNIEXPORT jstring JNICALL
Java_com_xanite_emulator_XeniaEmulator_nativeGetDeviceInfo(JNIEnv* env, jobject instance) {
    auto bridge = XeniaAndroidBridge::GetInstance();
    std::string info = bridge->GetDeviceInfo();
    return env->NewStringUTF(info.c_str());
}

JNIEXPORT jboolean JNICALL
Java_com_xanite_emulator_XeniaEmulator_nativeIsEmulationRunning(JNIEnv* env, jobject instance) {
    auto bridge = XeniaAndroidBridge::GetInstance();
    return bridge->IsEmulationRunning() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_com_xanite_emulator_XeniaEmulator_nativeIsEmulationPaused(JNIEnv* env, jobject instance) {
    auto bridge = XeniaAndroidBridge::GetInstance();
    return bridge->IsEmulationPaused() ? JNI_TRUE : JNI_FALSE;
}

} 

} 

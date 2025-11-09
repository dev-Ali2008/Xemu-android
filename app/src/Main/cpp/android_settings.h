#ifndef XANITE_ANDROID_SETTINGS_H
#define XANITE_ANDROID_SETTINGS_H

#include <string>

namespace xanite {

enum class PerformanceMode {
    POWER_SAVING = 0,
    BALANCED = 1,
    PERFORMANCE = 2,
    MAXIMUM = 3
};

enum class ControlScheme {
    TOUCH_GAMEPAD = 0,
    GESTURE_BASED = 1,
    EXTERNAL_GAMEPAD = 2,
    HYBRID = 3
};

enum class ButtonLayout {
    STANDARD = 0,
    COMPACT = 1,
    LEFT_HANDED = 2,
    CUSTOM = 3
};

enum class RenderAPI {
    OPENGL_ES = 0,
    VULKAN = 1,
    AUTOMATIC = 2
};

enum class AudioBackend {
    OPENSLES = 0,
    AAUDIO = 1,
    OBOE = 2
};

struct InterfaceSettings {
    std::string theme = "dark";
    int language = 1; 
    bool show_fps = true;
    bool show_controller = true;
    bool touch_controls = true;
    float touch_opacity = 0.7f;
    float vibration_strength = 0.8f;
    float button_size = 1.0f;
    float analog_stick_size = 1.0f;
    bool auto_hide_controls = false;
    int hide_delay = 3000; 
};

struct PerformanceSettings {
    PerformanceMode performance_mode = PerformanceMode::BALANCED;
    int frame_limit = 60;
    bool power_saving = false;
    bool thermal_throttling = true;
    bool background_audio = false;
    bool suspend_on_focus_loss = true;
    int memory_usage_limit = 75; 
};

struct ControlSettings {
    ControlScheme control_scheme = ControlScheme::TOUCH_GAMEPAD;
    float touch_sensitivity = 1.0f;
    float analog_deadzone = 0.15f;
    float trigger_deadzone = 0.1f;
    ButtonLayout button_layout = ButtonLayout::STANDARD;
    bool enable_gestures = true;
    bool swipe_gestures = true;
    bool tap_zones = true;
    bool haptic_feedback = true;
};

struct GraphicsSettings {
    float resolution_scale = 1.0f;
    int texture_filtering = 2; 
    int msaa_level = 2;
    bool vsync = true;
    int anisotropic_filtering = 4;
    bool gpu_timing = false;
    RenderAPI render_api = RenderAPI::VULKAN;
    bool post_processing = false;
    float brightness = 1.0f;
    float contrast = 1.0f;
};

struct AudioSettings {
    int audio_latency = 128; 
    float volume = 1.0f;
    bool mute_on_focus_loss = true;
    AudioBackend audio_backend = AudioBackend::OPENSLES;
    bool surround_sound = false;
    bool audio_boost = false;
};

struct StorageSettings {
    std::string content_directory = "/sdcard/xenia/content";
    std::string cache_directory = "/sdcard/xenia/cache";
    std::string save_directory = "/sdcard/xenia/saves";
    std::string screenshot_directory = "/sdcard/xenia/screenshots";
    bool auto_save = true;
    bool save_compression = true;
    bool cloud_sync = false;
};

struct NetworkSettings {
    bool enable_network = false;
    bool xbox_live = false;
    bool upnp = false;
    bool port_forwarding = false;
};

struct DebugSettings {
    int log_level = 1; 
    bool show_log_window = false;
    bool performance_overlay = false;
    bool crash_reporting = true;
    bool analytics = false;
    bool developer_mode = false;
};

class AndroidSettings {
public:
    AndroidSettings();
    ~AndroidSettings();
    
    InterfaceSettings& GetInterfaceSettings();
    PerformanceSettings& GetPerformanceSettings();
    ControlSettings& GetControlSettings();
    GraphicsSettings& GetGraphicsSettings();
    AudioSettings& GetAudioSettings();
    StorageSettings& GetStorageSettings();
    NetworkSettings& GetNetworkSettings();
    DebugSettings& GetDebugSettings();
  
    void SetInterfaceSettings(const InterfaceSettings& settings);
    void SetPerformanceSettings(const PerformanceSettings& settings);
    void SetControlSettings(const ControlSettings& settings);
    void SetGraphicsSettings(const GraphicsSettings& settings);
    void SetAudioSettings(const AudioSettings& settings);
    void SetStorageSettings(const StorageSettings& settings);
    void SetNetworkSettings(const NetworkSettings& settings);
    void SetDebugSettings(const DebugSettings& settings);
   
    void ResetToDefaults();
    bool ExportSettings(const std::string& filename);
    bool ImportSettings(const std::string& filename);

private:
    void LoadSettings();
    void SaveSettings();
    void CreateDefaultSettings();
    void ProcessSettingsValue(const std::string& section, const std::string& key, const std::string& value);
    std::string GetSettingsPath();

    InterfaceSettings interface_settings_;
    PerformanceSettings performance_settings_;
    ControlSettings control_settings_;
    GraphicsSettings graphics_settings_;
    AudioSettings audio_settings_;
    StorageSettings storage_settings_;
    NetworkSettings network_settings_;
    DebugSettings debug_settings_;

    std::string settings_path_;
};

} 

#endif 

#ifndef XANITE_ANDROID_SETTINGS_H
#define XANITE_ANDROID_SETTINGS_H

#include <string>
#include <unordered_map>
#include <vector>
#include <cstdint>
#include <memory>
#include <functional>

namespace xanite {

enum class GraphicsBackend {
    VULKAN,
    OPENGL_ES,
    D3D12
};

enum class ResolutionScale {
    NATIVE = 1,
    HALF = 2,
    QUARTER = 4,
    CUSTOM = 8
};

enum class TextureFiltering {
    POINT,
    LINEAR,
    ANISOTROPIC_2X,
    ANISOTROPIC_4X,
    ANISOTROPIC_8X,
    ANISOTROPIC_16X
};

enum class AudioBackend {
    OPENSL_ES,
    AAUDIO,
    SDL_AUDIO
};

enum class ControllerLayout {
    XBOX_360,
    XBOX_ONE,
    PS4,
    PS5,
    NINTENDO_SWITCH,
    CUSTOM
};

enum class PerformanceProfile {
    BATTERY_SAVER,
    BALANCED,
    PERFORMANCE,
    ULTRA_PERFORMANCE
};

enum class ThermalMode {
    AGGRESSIVE_COOLING,
    BALANCED,
    QUIET
};

struct GraphicsSettings {
    GraphicsBackend backend = GraphicsBackend::VULKAN;
    ResolutionScale resolution_scale = ResolutionScale::NATIVE;
    float custom_scale = 1.0f;
    bool vsync = true;
    bool fullscreen = true;
    TextureFiltering texture_filtering = TextureFiltering::ANISOTROPIC_4X;
    int msaa_samples = 1;
    bool depth_stencil = true;
    bool gpu_timing = false;
    bool frame_rate_limit = false;
    int max_frame_rate = 60;
    bool triple_buffering = true;
    bool async_presentation = true;
    bool hardware_acceleration = true;
    
    bool disable_srgb = false;
    bool disable_alpha_to_coverage = false;
    bool disable_primitive_restart = false;
    bool force_host_GPU_cache = false;
    int texture_cache_memory_limit_mb = 512;
    int shader_cache_memory_limit_mb = 256;
};

struct AudioSettings {
    AudioBackend backend = AudioBackend::AAUDIO;
    int sample_rate = 48000;
    int buffer_size = 1024;
    int channels = 2;
    bool enable_audio = true;
    bool enable_3d_audio = true;
    float master_volume = 1.0f;
    float game_volume = 1.0f;
    float music_volume = 0.8f;
    float effects_volume = 1.0f;
    bool audio_stretching = true;
    int resampling_quality = 2;
    
    bool enable_audio_dump = false;
    bool enable_audio_loopback = false;
    int audio_processing_threads = 2;
};

struct InputSettings {
    ControllerLayout controller_layout = ControllerLayout::XBOX_360;
    float touch_sensitivity = 1.0f;
    float analog_deadzone = 0.15f;
    float trigger_deadzone = 0.1f;
    bool vibration_enabled = true;
    bool motion_controls = false;
    float motion_sensitivity = 1.0f;
    bool touch_gamepad = true;
    bool physical_controller_priority = true;
    bool button_remapping = false;
    
    struct TouchZone {
        float x, y, width, height;
        int button_id;
        bool visible;
        float opacity;
    };
    
    std::vector<TouchZone> touch_zones;
    bool show_touch_controls = true;
    float touch_opacity = 0.7f;
    bool touch_haptic_feedback = true;
};

struct SystemSettings {
    PerformanceProfile performance_profile = PerformanceProfile::BALANCED;
    ThermalMode thermal_mode = ThermalMode::BALANCED;
    bool enable_thermal_throttling = true;
    int cpu_thread_count = 0;
    bool enable_smt = true;
    int gpu_clock_boost = 0;
    bool memory_optimization = true;
    int cache_size_mb = 256;
    bool background_processing = false;
    bool power_saving_suspend = true;
    
    bool enable_debugging = false;
    bool enable_profiling = false;
    bool enable_tracing = false;
    int log_level = 2;
    bool crash_dumps = true;
    bool performance_counters = true;
};

struct DisplaySettings {
    int screen_orientation = 0;
    bool keep_screen_on = true;
    bool fullscreen_immersive = true;
    bool show_fps = true;
    bool show_statistics = false;
    bool show_controller_overlay = true;
    int brightness_boost = 0;
    bool hdr_support = false;
    bool force_rgb_range = false;
    bool color_correction = true;
    
    struct OSDConfig {
        bool enabled = true;
        int position = 0;
        float scale = 1.0f;
        int opacity = 80;
        std::vector<std::string> elements;
    };
    
    OSDConfig osd_config;
};

struct NetworkSettings {
    bool enable_network_emulation = false;
    bool xbox_live_emulation = false;
    bool system_link_emulation = false;
    std::string network_adapter = "default";
    int network_latency_ms = 0;
    int packet_loss_percent = 0;
    
    bool enable_multiplayer = false;
    int max_players = 4;
    bool nat_traversal = true;
    int port_forwarding = 0;
};

struct GameSpecificSettings {
    std::string title_id;
    std::string profile_name;
    bool use_global_settings = false;
    
    GraphicsSettings graphics_overrides;
    AudioSettings audio_overrides;
    InputSettings input_overrides;
    SystemSettings system_overrides;
    
    bool force_compatibility_mode = false;
    bool disable_specific_features = false;
    std::vector<std::string> enabled_patches;
    std::vector<std::string> disabled_patches;
};

class AndroidSettings {
public:
    AndroidSettings();
    ~AndroidSettings();

    bool LoadFromFile(const std::string& file_path);
    bool SaveToFile(const std::string& file_path);
    void ResetToDefaults();
    bool ValidateSettings() const;

    bool SaveProfile(const std::string& profile_name);
    bool LoadProfile(const std::string& profile_name);
    bool DeleteProfile(const std::string& profile_name);
    std::vector<std::string> GetAvailableProfiles() const;

    bool LoadGameSettings(const std::string& title_id);
    bool SaveGameSettings(const std::string& title_id);
    bool DeleteGameSettings(const std::string& title_id);
    std::vector<std::string> GetConfiguredGames() const;

    GraphicsSettings& GetGraphicsSettings() { return graphics_; }
    AudioSettings& GetAudioSettings() { return audio_; }
    InputSettings& GetInputSettings() { return input_; }
    SystemSettings& GetSystemSettings() { return system_; }
    DisplaySettings& GetDisplaySettings() { return display_; }
    NetworkSettings& GetNetworkSettings() { return network_; }

    const GraphicsSettings& GetGraphicsSettings() const { return graphics_; }
    const AudioSettings& GetAudioSettings() const { return audio_; }
    const InputSettings& GetInputSettings() const { return input_; }
    const SystemSettings& GetSystemSettings() const { return system_; }
    const DisplaySettings& GetDisplaySettings() const { return display_; }
    const NetworkSettings& GetNetworkSettings() const { return network_; }

    bool GetGameSettings(const std::string& title_id, GameSpecificSettings& settings) const;
    bool SetGameSettings(const std::string& title_id, const GameSpecificSettings& settings);

    void ApplyPerformanceProfile(PerformanceProfile profile);
    void ApplyThermalProfile(ThermalMode mode);
    void OptimizeForBattery();
    void OptimizeForPerformance();
    void AutoDetectOptimalSettings();

    std::string ToJson() const;
    bool FromJson(const std::string& json_string);
    std::string GetSettingsSummary() const;
    bool HasUnsavedChanges() const { return has_unsaved_changes_; }

    using SettingsChangedCallback = std::function<void(const std::string& setting_name)>;
    void RegisterSettingsCallback(const std::string& setting_name, SettingsChangedCallback callback);
    void UnregisterSettingsCallback(const std::string& setting_name);

private:
    void InitializeDefaults();
    void NotifyCallbacks(const std::string& setting_name);

    GraphicsSettings graphics_;
    AudioSettings audio_;
    InputSettings input_;
    SystemSettings system_;
    DisplaySettings display_;
    NetworkSettings network_;

    std::unordered_map<std::string, GameSpecificSettings> game_settings_;
    std::unordered_map<std::string, std::string> profiles_;
    std::string current_profile_ = "default";

    std::unordered_map<std::string, std::vector<SettingsChangedCallback>> callbacks_;

    bool has_unsaved_changes_ = false;
    std::string config_file_path_;
    bool is_initialized_ = false;

    uint64_t last_optimization_time_ = 0;
    int optimization_count_ = 0;
};

} // namespace xanite

#endif // XANITE_ANDROID_SETTINGS_H
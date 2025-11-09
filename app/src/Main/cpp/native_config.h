#ifndef XANITE_NATIVE_CONFIG_H
#define XANITE_NATIVE_CONFIG_H

#include <string>

namespace xanite {

struct GraphicsConfig {
    std::string gpu = "vulkan";  
    float resolution_scale = 1.0f;
    int msaa_samples = 1;
    bool vsync = true;
    int texture_filtering = 1;
    bool gpu_timing = false;
    bool render_target_path_d3d12 = false;
    bool render_target_path_vulkan = true;
    bool vulkan_validation = false;
    bool vulkan_prime_idle = true;
    
    int vulkan_version_major = 1;
    int vulkan_version_minor = 1;
    bool require_vulkan_1_1 = true;
};

struct AudioConfig {
    std::string audio_system = "opensles";
    int channels = 2;
    int buffer_size = 1024;
    bool mute_unfocus = true;
};

struct CPUConfig {
    std::string cpu = "any";
    bool break_on_launch = false;
    bool break_on_debugbreak = false;
    bool ignore_llvm_unsafe_optimizations = true;
    bool llvm_optimizations = true;
};

struct HIDConfig {
    std::string hid = "nop";
    bool vibration = true;
    float left_stick_deadzone = 0.15f;
    float right_stick_deadzone = 0.15f;
    float left_trigger_deadzone = 0.1f;
    float right_trigger_deadzone = 0.1f;
};

struct UIConfig {
    int language = 1; 
    float time_scalar = 1.0f;
    std::string content_root = "/sdcard/xenia/content";
    std::string cache_root = "/sdcard/xenia/cache";
    std::string storage_root = "/sdcard/xenia";
    bool show_fps = true;
    bool show_controller = true;
    bool touch_controls = true;
    float touch_opacity = 0.7f;
    bool vulkan_only = true; 
};

struct SystemConfig {
    uint32_t license_mask = 0xFFFFFFFF;
    uint32_t content_license_mask = 0xFFFFFFFF;
    bool persistent_local_storage = true;
    bool mount_cache = true;
    bool mount_content = true;
    bool mount_scratch = true;
    bool require_vulkan = true; 
};

struct DebugConfig {
    bool debug = false;
    int log_level = 1; 
    bool dump_shaders = false;
    bool disable_guest_paging = false;
    bool disable_global_lock = false;
    bool disable_host_guest_stack_synchronization = false;
    bool allow_game_relative_writes = false;
    bool log_vulkan_info = true; 
};

class AndroidConfig {
public:
    AndroidConfig();
    ~AndroidConfig();

    
    GraphicsConfig& GetGraphicsConfig();
    AudioConfig& GetAudioConfig();
    CPUConfig& GetCPUConfig();
    HIDConfig& GetHIDConfig();
    UIConfig& GetUIConfig();
    SystemConfig& GetSystemConfig();
    DebugConfig& GetDebugConfig();

    
    void SetGraphicsConfig(const GraphicsConfig& config);
    void SetAudioConfig(const AudioConfig& config);
    void SetCPUConfig(const CPUConfig& config);
    void SetHIDConfig(const HIDConfig& config);
    void SetUIConfig(const UIConfig& config);
    void SetSystemConfig(const SystemConfig& config);
    void SetDebugConfig(const DebugConfig& config);

    
    bool CheckVulkanCompatibility();
    std::string GetVulkanDeviceInfo();
    bool IsVulkan11Supported();

    
    void ApplyToCVars();

private:
    void LoadConfig();
    void SaveConfig();
    void CreateDefaultConfig();
    void ProcessConfigValue(const std::string& section, const std::string& key, const std::string& value);
    std::string GetConfigPath();

    GraphicsConfig graphics_config_;
    AudioConfig audio_config_;
    CPUConfig cpu_config_;
    HIDConfig hid_config_;
    UIConfig ui_config_;
    SystemConfig system_config_;
    DebugConfig debug_config_;
    
    std::string config_path_;
};

} 

#endif 

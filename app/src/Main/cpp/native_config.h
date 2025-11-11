#ifndef XANITE_NATIVE_CONFIG_H
#define XANITE_NATIVE_CONFIG_H

#include <string>
#include <unordered_map>

namespace xanite {

enum class GPUType {
    UNKNOWN = 0,
    ADRENO = 1,
    MALI = 2,
    POWERVR = 3,
    NVIDIA = 4
};

enum class PerformanceTier {
    LOW_END = 0,
    MID_RANGE = 1,
    HIGH_END = 2
};

struct AndroidDeviceInfo {
    std::string device_model;
    std::string manufacturer;
    int android_version = 0;
    int cpu_cores = 0;
    int cpu_family = 0;
    bool supports_neon = false;
    bool supports_armv8 = false;
    bool supports_vfpv3 = false;
    GPUType gpu_type = GPUType::UNKNOWN;
    std::string gpu_vendor;
    std::string gpu_renderer;
    uint32_t total_ram_mb = 0;
    uint32_t available_ram_mb = 0;
    PerformanceTier performance_tier = PerformanceTier::LOW_END;
};

struct RecommendedSettings {
    float resolution_scale = 1.0f;
    int msaa_samples = 1;
    int texture_filtering = 1;
    bool vsync = true;
    bool gpu_timing = false;
    int audio_buffer_size = 1024;
};

class AndroidConfig {
public:
    // ✅ Static functions (تعمل بدون instance)
    static AndroidDeviceInfo GetDeviceInfo();
    static bool IsDeviceSupported(const AndroidDeviceInfo& info);
    static std::string GetDeviceCompatibilityReport(const AndroidDeviceInfo& info);
    
    // Device-specific optimizations (static)
    static void ApplySnapdragon855Optimizations();
    static void ApplyBatterySavingMode();
    static void ApplyPerformanceMode();
    static void ApplyToCVars();

    // ✅ دوال ال instance (تعمل مع object)
    explicit AndroidConfig(const std::string& config_path);
    
    // Configuration management
    void LoadConfig();
    void SaveConfig();
    void CreateDefaultConfig();
    void ValidateConfig();
    
    // Getters with default values
    std::string GetString(const std::string& key, const std::string& default_value = "");
    int GetInt(const std::string& key, int default_value = 0);
    bool GetBool(const std::string& key, bool default_value = false);
    float GetFloat(const std::string& key, float default_value = 0.0f);
    
    // Setters
    void SetString(const std::string& key, const std::string& value);
    void SetInt(const std::string& key, int value);
    void SetBool(const std::string& key, bool value);
    void SetFloat(const std::string& key, float value);
    
    // Specific configuration getters
    std::string GetGPUBackend();
    std::string GetResolution();
    bool IsFullscreen();
    bool IsVSyncEnabled();
    bool IsAudioEnabled();
    std::string GetControllerType();
    std::string GetLanguage();
    int GetRegion();
    std::string GetContentRoot();
    std::string GetCacheRoot();
    int GetLogLevel();
    
    void ApplyRecommendedSettings(const RecommendedSettings& settings);

private:
    // ✅ المتغيرات الثابتة المطلوبة
    static AndroidDeviceInfo g_device_info;
    static bool g_device_info_initialized;

    // ✅ Static helper functions
    static void DetectSOCAndGPU(AndroidDeviceInfo& info, const char* hardware, const char* platform);
    static void DetectMemoryInfo(AndroidDeviceInfo& info);
    static void EstimatePerformanceTier(AndroidDeviceInfo& info);
    static void EstimateGPUPerformanceTier(AndroidDeviceInfo& info);

    std::string config_path_;
    std::unordered_map<std::string, std::string> config_values_;
};

} // namespace xanite

#endif // XANITE_NATIVE_CONFIG_H
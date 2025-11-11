#ifndef XANITE_ANDROID_CONFIG_H
#define XANITE_ANDROID_CONFIG_H

#include <string>
#include <unordered_map>
#include <vector>
#include <memory>
#include <functional>

namespace xanite {

enum class GPUType {
    UNKNOWN,
    ADRENO,
    MALI,
    POWERVR,
    NVIDIA,
    INTEL
};

enum class PerformanceTier {
    VERY_LOW_END,
    LOW_END,
    MID_RANGE,
    HIGH_END,
    FLAGSHIP
};

enum class SOCVendor {
    UNKNOWN,
    QUALCOMM,
    SAMSUNG,
    MEDIATEK,
    HUAWEI,
    UNISOC,
    GOOGLE
};

enum class ThermalProfile {
    COOL,
    WARM,
    HOT,
    CRITICAL
};

struct AndroidDeviceInfo {
    // Basic device information
    std::string device_model;
    std::string manufacturer;
    std::string hardware;
    std::string board;
    std::string product;
    
    // Android system information
    int android_version = 0;
    std::string android_version_name;
    int sdk_version = 0;
    std::string build_id;
    
    // CPU information
    int cpu_cores = 0;
    int cpu_family = 0;
    std::string cpu_architecture;
    std::string cpu_abi;
    std::string cpu_abi2;
    int cpu_max_freq_mhz = 0;
    bool supports_neon = false;
    bool supports_armv8 = false;
    bool supports_vfpv3 = false;
    bool supports_aes = false;
    bool supports_sha = false;
    bool supports_crc32 = false;
    
    // GPU information
    std::string gpu_vendor;
    std::string gpu_renderer;
    GPUType gpu_type = GPUType::UNKNOWN;
    std::string gpu_driver_version;
    int gpu_max_freq_mhz = 0;
    bool supports_vulkan = false;
    bool supports_opengl_es_3_2 = false;
    bool supports_astc = false;
    bool supports_etc2 = false;
    
    // Memory information
    unsigned int total_ram_mb = 0;
    unsigned int available_ram_mb = 0;
    unsigned int storage_total_mb = 0;
    unsigned int storage_available_mb = 0;
    bool has_external_storage = false;
    
    // Display information
    int display_width = 0;
    int display_height = 0;
    float display_density = 0.0f;
    int display_refresh_rate = 60;
    bool supports_hdr = false;
    bool supports_wide_color = false;
    
    // Performance classification
    PerformanceTier performance_tier = PerformanceTier::MID_RANGE;
    SOCVendor soc_vendor = SOCVendor::UNKNOWN;
    std::string soc_model;
    
    // Thermal characteristics
    ThermalProfile thermal_profile = ThermalProfile::WARM;
    bool has_thermal_control = false;
    
    // Feature support
    bool has_gyroscope = false;
    bool has_accelerometer = false;
    bool has_magnetometer = false;
    bool has_gamepad_support = false;
    bool has_multitouch = false;
    int max_touch_points = 5;
    
    // Benchmark scores (if available)
    int antutu_score = 0;
    int geekbench_single_core = 0;
    int geekbench_multi_core = 0;
    int gfxbench_score = 0;
};

struct RecommendedSettings {
    // Graphics settings
    float resolution_scale = 1.0f;
    int msaa_samples = 1;
    int texture_filtering = 1;
    bool vsync = true;
    bool gpu_timing = false;
    bool async_presentation = true;
    bool triple_buffering = true;
    
    // Audio settings
    int audio_buffer_size = 1024;
    int audio_sample_rate = 48000;
    bool audio_stretching = true;
    int audio_resampling_quality = 2;
    
    // System settings
    int cpu_thread_count = 0; // 0 = auto
    bool enable_smt = true;
    int cache_size_mb = 256;
    bool memory_optimization = true;
    bool background_processing = false;
    
    // Performance settings
    bool enable_thermal_throttling = true;
    int gpu_clock_boost = 0;
    bool frame_rate_limit = false;
    int max_frame_rate = 60;
    
    // Compatibility settings
    bool force_compatibility_mode = false;
    bool disable_specific_features = false;
    std::vector<std::string> required_patches;
    
    // Quality presets
    std::string graphics_preset;
    std::string performance_preset;
    std::string battery_preset;
};

struct DeviceCompatibility {
    bool is_supported = false;
    int compatibility_rating = 0; // 0-10
    std::vector<std::string> supported_features;
    std::vector<std::string> unsupported_features;
    std::vector<std::string> known_issues;
    std::vector<std::string> recommended_workarounds;
    std::string compatibility_notes;
};

class AndroidConfig {
public:
    AndroidConfig(const std::string& config_path = "");
    ~AndroidConfig();

    // Configuration management
    bool LoadConfig();
    bool SaveConfig();
    void ResetToDefaults();
    bool ValidateConfig();
    
    // Device information
    static AndroidDeviceInfo GetDeviceInfo();
    static bool RefreshDeviceInfo();
    static std::string GetDeviceSignature();
    
    // Performance assessment
    static PerformanceTier AssessPerformanceTier(const AndroidDeviceInfo& info);
    static ThermalProfile AssessThermalProfile(const AndroidDeviceInfo& info);
    static int CalculatePerformanceScore(const AndroidDeviceInfo& info);
    
    // Compatibility checking
    static bool IsDeviceSupported(const AndroidDeviceInfo& info);
    static DeviceCompatibility GetDeviceCompatibility(const AndroidDeviceInfo& info);
    static std::string GetCompatibilityReport(const AndroidDeviceInfo& info);
    
    // Settings recommendation
    static RecommendedSettings GetRecommendedSettings(const AndroidDeviceInfo& info);
    static RecommendedSettings GetOptimalSettings(const AndroidDeviceInfo& info, PerformanceTier tier);
    static RecommendedSettings GetBatterySavingSettings(const AndroidDeviceInfo& info);
    static RecommendedSettings GetPerformanceSettings(const AndroidDeviceInfo& info);
    
    // Optimization profiles
    static void ApplySnapdragonOptimizations(const AndroidDeviceInfo& info);
    static void ApplyMaliOptimizations(const AndroidDeviceInfo& info);
    static void ApplyPowerVROptimizations(const AndroidDeviceInfo& info);
    static void ApplyNvidiaOptimizations(const AndroidDeviceInfo& info);
    
    // Thermal management
    static void ApplyThermalThrottling(int throttle_level);
    static void ApplyCoolingProfile(ThermalProfile profile);
    static int GetCurrentThermalLevel();
    
    // Configuration accessors
    std::string GetString(const std::string& key, const std::string& default_value = "");
    int GetInt(const std::string& key, int default_value = 0);
    bool GetBool(const std::string& key, bool default_value = false);
    float GetFloat(const std::string& key, float default_value = 0.0f);
    
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
    
    // CVars integration
    static void ApplyToCVars();
    static void ApplyRecommendedSettings(const RecommendedSettings& settings);
    static void UpdateCVarsFromConfig();
    
    // Benchmarking
    static int RunQuickBenchmark();
    static int RunComprehensiveBenchmark();
    static void SaveBenchmarkResults(const std::string& file_path);
    static std::string GetBenchmarkSummary();
    
    // Utility functions
    static std::string GetAndroidVersionName(int sdk_version);
    static std::string GetGPUTypeName(GPUType type);
    static std::string GetPerformanceTierName(PerformanceTier tier);
    static std::string GetSOCVendorName(SOCVendor vendor);
    
    // Diagnostic information
    static std::string GetSystemInfo();
    static std::string GetHardwareInfo();
    static std::string GetPerformanceInfo();
    static std::string GetCompatibilityInfo();

private:
    // Configuration storage
    std::string config_path_;
    std::unordered_map<std::string, std::string> config_values_;
    
    // Device info cache
    static AndroidDeviceInfo cached_device_info_;
    static bool device_info_initialized_;
    static int last_refresh_time_;
    
    // Helper methods
    void LoadDefaultConfig();
    void ParseConfigLine(const std::string& line);
    std::string GenerateConfigLine(const std::string& key, const std::string& value);
    
    // Static detection methods
    static void DetectCPUInfo(AndroidDeviceInfo& info);
    static void DetectGPUInfo(AndroidDeviceInfo& info);
    static void DetectMemoryInfo(AndroidDeviceInfo& info);
    static void DetectDisplayInfo(AndroidDeviceInfo& info);
    static void DetectSOCInfo(AndroidDeviceInfo& info);
    static void DetectSensorInfo(AndroidDeviceInfo& info);
    static void DetectFeatureSupport(AndroidDeviceInfo& info);
    
    // Performance estimation
    static void EstimateGPUPerformance(AndroidDeviceInfo& info);
    static void EstimateCPUPerformance(AndroidDeviceInfo& info);
    static void EstimateMemoryPerformance(AndroidDeviceInfo& info);
    static void CalculateOverallPerformance(AndroidDeviceInfo& info);
    
    // Compatibility checking
    static bool CheckCPUCompatibility(const AndroidDeviceInfo& info);
    static bool CheckGPUCompatibility(const AndroidDeviceInfo& info);
    static bool CheckMemoryCompatibility(const AndroidDeviceInfo& info);
    static bool CheckSystemCompatibility(const AndroidDeviceInfo& info);
    
    // Benchmarking helpers
    static int BenchmarkCPU();
    static int BenchmarkGPU();
    static int BenchmarkMemory();
    static int BenchmarkStorage();
    
    // Thermal monitoring
    static int ReadThermalZone(const std::string& zone_path);
    static std::vector<std::string> GetThermalZones();
    static int GetAverageTemperature();
    
    // CVars application
    static void ApplyGraphicsCVars(const RecommendedSettings& settings);
    static void ApplyAudioCVars(const RecommendedSettings& settings);
    static void ApplySystemCVars(const RecommendedSettings& settings);
    static void ApplyPerformanceCVars(const RecommendedSettings& settings);
};

} // namespace xanite

#endif // XANITE_ANDROID_CONFIG_H
#ifndef XANITE_ANDROID_CONFIG_H
#define XANITE_ANDROID_CONFIG_H

#include <cstdint>
#include <string>
#include <vector>

namespace xanite {

enum class GPUType {
    UNKNOWN = 0,
    ADRENO = 1,
    MALI = 2,
    POWERVR = 3,
    NVIDIA = 4
};

// 需要更改设置，但不是现在。 
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
    std::string gpu_vendor;
    std::string gpu_renderer;
    GPUType gpu_type = GPUType::UNKNOWN;
    uint32_t total_ram_mb = 0;
    uint32_t available_ram_mb = 0;
    PerformanceTier performance_tier = PerformanceTier::MID_RANGE;
};

struct RecommendedSettings {
    float resolution_scale = 1.0f;
    int msaa_samples = 1;
    int texture_filtering = 1;
    bool vsync = true;
    bool cpu_optimizations = true;
    bool gpu_timing = false;
    int audio_buffer_size = 1024;
};

class AndroidConfig {
public:
    static AndroidDeviceInfo GetDeviceInfo();
    static bool IsDeviceSupported(const AndroidDeviceInfo& info);
    static std::string GetDeviceCompatibilityReport(const AndroidDeviceInfo& info);
    static RecommendedSettings GetRecommendedSettings(const AndroidDeviceInfo& info);
    static void ApplyRecommendedSettings(const RecommendedSettings& settings);
    static std::vector<std::string> GetSystemDirectories();
    static bool CreateAppDirectories();

private:
    static void DetectGPUInfo(AndroidDeviceInfo& info);
    static void EstimateGPUPerformanceTier(AndroidDeviceInfo& info);
    static void DetectMemoryInfo(AndroidDeviceInfo& info);
};

} 

#endif 

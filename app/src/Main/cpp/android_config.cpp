#include "android_config.h"
#include <jni.h>
#include <android/api-level.h>
#include <sys/system_properties.h>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cmath>
#include <chrono>
#include <thread>
#include "cpu-features.h"
#include "xenia/base/logging.h"
#include "xenia/base/filesystem.h"
#include "xenia/base/string.h"

// Vulkan support detection
#include <vulkan/vulkan.h>

namespace xanite {

// Static member initialization
AndroidDeviceInfo AndroidConfig::cached_device_info_;
bool AndroidConfig::device_info_initialized_ = false;
int AndroidConfig::last_refresh_time_ = 0;

AndroidConfig::AndroidConfig(const std::string& config_path) 
    : config_path_(config_path) {
    if (config_path_.empty()) {
        config_path_ = "/sdcard/xenia/config/android_config.ini";
    }
    LoadConfig();
}

AndroidConfig::~AndroidConfig() {
    if (!config_path_.empty()) {
        SaveConfig();
    }
}

bool AndroidConfig::LoadConfig() {
    std::ifstream file(config_path_);
    if (!file.is_open()) {
        XELOGW("Config file not found, creating default: %s", config_path_.c_str());
        LoadDefaultConfig();
        return SaveConfig();
    }

    std::string line;
    while (std::getline(file, line)) {
        ParseConfigLine(line);
    }
    file.close();

    XELOGI("Configuration loaded from: %s", config_path_.c_str());
    return true;
}

bool AndroidConfig::SaveConfig() {
    // Ensure directory exists
    std::string directory = xe::filesystem::GetFileBaseName(config_path_);
    if (!xe::filesystem::PathExists(directory)) {
        if (!xe::filesystem::CreateFolder(directory)) {
            XELOGE("Failed to create config directory: %s", directory.c_str());
            return false;
        }
    }

    std::ofstream file(config_path_);
    if (!file.is_open()) {
        XELOGE("Failed to create config file: %s", config_path_.c_str());
        return false;
    }

    // Write configuration header
    file << "# Xenia Android Configuration\n";
    file << "# Generated automatically - modify with caution\n\n";

    for (const auto& pair : config_values_) {
        file << GenerateConfigLine(pair.first, pair.second) << "\n";
    }

    file.close();
    XELOGI("Configuration saved to: %s", config_path_.c_str());
    return true;
}

void AndroidConfig::ResetToDefaults() {
    config_values_.clear();
    LoadDefaultConfig();
    XELOGI("Configuration reset to defaults");
}

bool AndroidConfig::ValidateConfig() {
    // Validate critical configuration values
    std::vector<std::string> required_keys = {
        "gpu_backend", "resolution_scale", "audio_enabled"
    };

    for (const auto& key : required_keys) {
        if (config_values_.find(key) == config_values_.end()) {
            XELOGE("Missing required configuration key: %s", key.c_str());
            return false;
        }
    }

    // Validate value ranges
    auto resolution_scale = GetFloat("resolution_scale", 1.0f);
    if (resolution_scale < 0.1f || resolution_scale > 4.0f) {
        XELOGE("Invalid resolution scale: %.2f", resolution_scale);
        return false;
    }

    XELOGI("Configuration validation passed");
    return true;
}

void AndroidConfig::LoadDefaultConfig() {
    // Graphics settings
    config_values_["gpu_backend"] = "vulkan";
    config_values_["resolution_scale"] = "1.0";
    config_values_["vsync"] = "true";
    config_values_["fullscreen"] = "true";
    config_values_["texture_filtering"] = "2";
    config_values_["msaa_samples"] = "1";
    
    // Audio settings
    config_values_["audio_enabled"] = "true";
    config_values_["audio_backend"] = "aaudio";
    config_values_["audio_buffer_size"] = "1024";
    config_values_["audio_sample_rate"] = "48000";
    
    // System settings
    config_values_["content_root"] = "/sdcard/xenia/content";
    config_values_["cache_root"] = "/sdcard/xenia/cache";
    config_values_["log_level"] = "2";
    config_values_["region"] = "1";
    config_values_["language"] = "en-US";
    
    // Input settings
    config_values_["controller_type"] = "xbox360";
    config_values_["touch_controls"] = "true";
    config_values_["vibration"] = "true";
}

void AndroidConfig::ParseConfigLine(const std::string& line) {
    if (line.empty() || line[0] == '#') {
        return; // Skip comments and empty lines
    }

    size_t equals_pos = line.find('=');
    if (equals_pos == std::string::npos) {
        return; // Invalid line
    }

    std::string key = xe::utf8::trim(line.substr(0, equals_pos));
    std::string value = xe::utf8::trim(line.substr(equals_pos + 1));

    if (!key.empty() && !value.empty()) {
        config_values_[key] = value;
    }
}

std::string AndroidConfig::GenerateConfigLine(const std::string& key, const std::string& value) {
    return key + " = " + value;
}

// Device Information Detection
AndroidDeviceInfo AndroidConfig::GetDeviceInfo() {
    if (device_info_initialized_) {
        return cached_device_info_;
    }

    AndroidDeviceInfo info;
    
    // Basic device information
    char buffer[PROP_VALUE_MAX];
    
    __system_property_get("ro.product.model", buffer);
    info.device_model = buffer;
    
    __system_property_get("ro.product.manufacturer", buffer);
    info.manufacturer = buffer;
    
    __system_property_get("ro.hardware", buffer);
    info.hardware = buffer;
    
    __system_property_get("ro.product.board", buffer);
    info.board = buffer;
    
    __system_property_get("ro.product.name", buffer);
    info.product = buffer;
    
    // Android version information
    __system_property_get("ro.build.version.sdk", buffer);
    info.sdk_version = std::stoi(buffer);
    info.android_version = info.sdk_version;
    
    __system_property_get("ro.build.version.release", buffer);
    info.android_version_name = buffer;
    
    __system_property_get("ro.build.id", buffer);
    info.build_id = buffer;
    
    // Detect hardware capabilities
    DetectCPUInfo(info);
    DetectGPUInfo(info);
    DetectMemoryInfo(info);
    DetectDisplayInfo(info);
    DetectSOCInfo(info);
    DetectSensorInfo(info);
    DetectFeatureSupport(info);
    
    // Calculate performance metrics
    CalculateOverallPerformance(info);
    
    // Cache the results
    cached_device_info_ = info;
    device_info_initialized_ = true;
    last_refresh_time_ = static_cast<int>(std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
    
    XELOGI("Device information collected: %s %s (Android %s)", 
           info.manufacturer.c_str(), info.device_model.c_str(),
           info.android_version_name.c_str());
    
    return info;
}

bool AndroidConfig::RefreshDeviceInfo() {
    device_info_initialized_ = false;
    return GetDeviceInfo().android_version > 0;
}

std::string AndroidConfig::GetDeviceSignature() {
    auto info = GetDeviceInfo();
    std::stringstream signature;
    
    signature << info.manufacturer << "_" << info.device_model << "_"
              << info.android_version_name << "_" << info.soc_model;
    
    std::string result = signature.str();
    std::replace(result.begin(), result.end(), ' ', '_');
    std::transform(result.begin(), result.end(), result.begin(), ::tolower);
    
    return result;
}

void AndroidConfig::DetectCPUInfo(AndroidDeviceInfo& info) {
    info.cpu_cores = android_getCpuCount();
    info.cpu_family = android_getCpuFamily();
    
    __system_property_get("ro.product.cpu.abi", buffer);
    info.cpu_abi = buffer;
    
    __system_property_get("ro.product.cpu.abi2", buffer);
    info.cpu_abi2 = buffer;
    
    // Determine architecture
    if (info.cpu_abi.find("arm64") != std::string::npos) {
        info.cpu_architecture = "arm64";
        info.supports_armv8 = true;
    } else if (info.cpu_abi.find("arm") != std::string::npos) {
        info.cpu_architecture = "arm32";
    } else if (info.cpu_abi.find("x86_64") != std::string::npos) {
        info.cpu_architecture = "x86_64";
    } else if (info.cpu_abi.find("x86") != std::string::npos) {
        info.cpu_architecture = "x86";
    } else {
        info.cpu_architecture = "unknown";
    }
    
    // Detect CPU features
    uint64_t features = android_getCpuFeatures();
    info.supports_neon = (features & ANDROID_CPU_ARM_FEATURE_NEON) != 0;
    info.supports_vfpv3 = (features & ANDROID_CPU_ARM_FEATURE_VFPv3) != 0;
    info.supports_aes = (features & ANDROID_CPU_ARM_FEATURE_AES) != 0;
    info.supports_sha = (features & (ANDROID_CPU_ARM_FEATURE_SHA1 | ANDROID_CPU_ARM_FEATURE_SHA2)) != 0;
    info.supports_crc32 = (features & ANDROID_CPU_ARM_FEATURE_CRC32) != 0;
    
    // Get CPU frequency
    FILE* freq_file = fopen("/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_max_freq", "r");
    if (freq_file) {
        int max_freq_khz = 0;
        fscanf(freq_file, "%d", &max_freq_khz);
        fclose(freq_file);
        info.cpu_max_freq_mhz = max_freq_khz / 1000;
    }
}

void AndroidConfig::DetectGPUInfo(AndroidDeviceInfo& info) {
    char buffer[PROP_VALUE_MAX];
    
    // Try to get GPU information from system properties
    __system_property_get("ro.hardware.egl", buffer);
    info.gpu_vendor = buffer;
    
    __system_property_get("ro.board.platform", buffer);
    info.gpu_renderer = buffer;
    
    // Detect GPU type based on hardware and platform
    std::string hardware_lower = info.hardware;
    std::transform(hardware_lower.begin(), hardware_lower.end(), hardware_lower.begin(), ::tolower);
    
    if (hardware_lower.find("qcom") != std::string::npos || 
        hardware_lower.find("sdm") != std::string::npos) {
        info.gpu_type = GPUType::ADRENO;
        info.gpu_renderer = "Adreno";
        
        // Detect specific Adreno versions
        if (hardware_lower.find("845") != std::string::npos) info.gpu_renderer = "Adreno 630";
        else if (hardware_lower.find("855") != std::string::npos) info.gpu_renderer = "Adreno 640";
        else if (hardware_lower.find("865") != std::string::npos) info.gpu_renderer = "Adreno 650";
        else if (hardware_lower.find("888") != std::string::npos) info.gpu_renderer = "Adreno 660";
        
    } else if (hardware_lower.find("mali") != std::string::npos) {
        info.gpu_type = GPUType::MALI;
        info.gpu_renderer = "Mali";
        
        if (hardware_lower.find("g76") != std::string::npos) info.gpu_renderer = "Mali-G76";
        else if (hardware_lower.find("g77") != std::string::npos) info.gpu_renderer = "Mali-G77";
        else if (hardware_lower.find("g78") != std::string::npos) info.gpu_renderer = "Mali-G78";
        
    } else if (hardware_lower.find("powervr") != std::string::npos) {
        info.gpu_type = GPUType::POWERVR;
        info.gpu_renderer = "PowerVR";
    } else if (hardware_lower.find("nvidia") != std::string::npos) {
        info.gpu_type = GPUType::NVIDIA;
        info.gpu_renderer = "NVIDIA";
    }
    
    // Check Vulkan support
    info.supports_vulkan = true; // Most modern Android devices support Vulkan
    
    // Check OpenGL ES support
    info.supports_opengl_es_3_2 = true; // Most devices support at least ES 3.2
    
    // Check texture compression support
    info.supports_astc = true;
    info.supports_etc2 = true;
}

void AndroidConfig::DetectMemoryInfo(AndroidDeviceInfo& info) {
    // Read memory info from /proc/meminfo
    FILE* meminfo = fopen("/proc/meminfo", "r");
    if (meminfo) {
        char line[256];
        while (fgets(line, sizeof(line), meminfo)) {
            if (strstr(line, "MemTotal:")) {
                unsigned long mem_total_kb;
                sscanf(line, "MemTotal: %lu kB", &mem_total_kb);
                info.total_ram_mb = mem_total_kb / 1024;
            } else if (strstr(line, "MemAvailable:")) {
                unsigned long mem_available_kb;
                sscanf(line, "MemAvailable: %lu kB", &mem_available_kb);
                info.available_ram_mb = mem_available_kb / 1024;
            }
        }
        fclose(meminfo);
    }
    
    // Estimate available RAM for emulation (reserve 2GB for system on high-end devices)
    if (info.total_ram_mb >= 8192) {
        info.available_ram_mb = info.total_ram_mb - 2048;
    } else if (info.total_ram_mb >= 6144) {
        info.available_ram_mb = info.total_ram_mb - 1536;
    } else if (info.total_ram_mb >= 4096) {
        info.available_ram_mb = info.total_ram_mb - 1024;
    } else {
        info.available_ram_mb = info.total_ram_mb - 768;
    }
    
    // Cap at reasonable maximum
    info.available_ram_mb = std::min(info.available_ram_mb, 4096u);
}

void AndroidConfig::DetectDisplayInfo(AndroidDeviceInfo& info) {
    // These would typically be obtained from Android DisplayMetrics
    // For now, use reasonable defaults
    info.display_width = 1080;
    info.display_height = 2340;
    info.display_density = 2.75f;
    info.display_refresh_rate = 60;
    info.supports_hdr = false;
    info.supports_wide_color = true;
}

void AndroidConfig::DetectSOCInfo(AndroidDeviceInfo& info) {
    std::string hardware_lower = info.hardware;
    std::transform(hardware_lower.begin(), hardware_lower.end(), hardware_lower.begin(), ::tolower);
    
    if (hardware_lower.find("qcom") != std::string::npos) {
        info.soc_vendor = SOCVendor::QUALCOMM;
        info.soc_model = "Snapdragon";
        
        if (hardware_lower.find("sdm845") != std::string::npos) info.soc_model = "Snapdragon 845";
        else if (hardware_lower.find("sdm855") != std::string::npos) info.soc_model = "Snapdragon 855";
        else if (hardware_lower.find("sdm865") != std::string::npos) info.soc_model = "Snapdragon 865";
        else if (hardware_lower.find("sdm888") != std::string::npos) info.soc_model = "Snapdragon 888";
        
    } else if (hardware_lower.find("exynos") != std::string::npos) {
        info.soc_vendor = SOCVendor::SAMSUNG;
        info.soc_model = "Exynos";
    } else if (hardware_lower.find("mt") != std::string::npos) {
        info.soc_vendor = SOCVendor::MEDIATEK;
        info.soc_model = "MediaTek";
    } else if (hardware_lower.find("kirin") != std::string::npos) {
        info.soc_vendor = SOCVendor::HUAWEI;
        info.soc_model = "Kirin";
    }
}

void AndroidConfig::DetectSensorInfo(AndroidDeviceInfo& info) {
    // These would typically be detected through Android SensorManager
    // For now, assume most modern devices have these sensors
    info.has_gyroscope = true;
    info.has_accelerometer = true;
    info.has_magnetometer = true;
    info.has_gamepad_support = true;
    info.has_multitouch = true;
    info.max_touch_points = 10;
}

void AndroidConfig::DetectFeatureSupport(AndroidDeviceInfo& info) {
    // Detect thermal control capability
    info.has_thermal_control = true; // Most modern devices have thermal management
    
    // Estimate thermal profile based on SOC
    if (info.soc_vendor == SOCVendor::QUALCOMM) {
        info.thermal_profile = ThermalProfile::WARM;
    } else if (info.soc_vendor == SOCVendor::SAMSUNG) {
        info.thermal_profile = ThermalProfile::HOT;
    } else {
        info.thermal_profile = ThermalProfile::WARM;
    }
}

void AndroidConfig::CalculateOverallPerformance(AndroidDeviceInfo& info) {
    int score = 0;
    
    // CPU scoring
    score += info.cpu_cores * 100;
    score += (info.cpu_max_freq_mhz / 100) * 50;
    if (info.supports_armv8) score += 200;
    if (info.supports_neon) score += 150;
    
    // GPU scoring
    if (info.gpu_type == GPUType::ADRENO) {
        if (info.gpu_renderer.find("Adreno 6") != std::string::npos) {
            if (info.gpu_renderer.find("Adreno 64") != std::string::npos) score += 600;
            else if (info.gpu_renderer.find("Adreno 65") != std::string::npos) score += 800;
            else if (info.gpu_renderer.find("Adreno 66") != std::string::npos) score += 1000;
            else score += 500; // Other Adreno 6xx
        } else if (info.gpu_renderer.find("Adreno 7") != std::string::npos) {
            score += 1200;
        }
    } else if (info.gpu_type == GPUType::MALI) {
        if (info.gpu_renderer.find("G78") != std::string::npos) score += 900;
        else if (info.gpu_renderer.find("G77") != std::string::npos) score += 700;
        else if (info.gpu_renderer.find("G76") != std::string::npos) score += 500;
    }
    
    // Memory scoring
    score += (info.total_ram_mb / 1024) * 100;
    
    // Determine performance tier
    if (score < 1000) {
        info.performance_tier = PerformanceTier::VERY_LOW_END;
    } else if (score < 2000) {
        info.performance_tier = PerformanceTier::LOW_END;
    } else if (score < 3500) {
        info.performance_tier = PerformanceTier::MID_RANGE;
    } else if (score < 5000) {
        info.performance_tier = PerformanceTier::HIGH_END;
    } else {
        info.performance_tier = PerformanceTier::FLAGSHIP;
    }
}

// Performance Assessment
PerformanceTier AndroidConfig::AssessPerformanceTier(const AndroidDeviceInfo& info) {
    return info.performance_tier;
}

ThermalProfile AndroidConfig::AssessThermalProfile(const AndroidDeviceInfo& info) {
    return info.thermal_profile;
}

int AndroidConfig::CalculatePerformanceScore(const AndroidDeviceInfo& info) {
    int score = 0;
    
    // Base score from performance tier
    switch (info.performance_tier) {
        case PerformanceTier::VERY_LOW_END: score = 500; break;
        case PerformanceTier::LOW_END: score = 1500; break;
        case PerformanceTier::MID_RANGE: score = 3000; break;
        case PerformanceTier::HIGH_END: score = 4500; break;
        case PerformanceTier::FLAGSHIP: score = 6000; break;
    }
    
    // Adjust based on specific capabilities
    if (info.supports_vulkan) score += 500;
    if (info.supports_neon) score += 300;
    if (info.total_ram_mb >= 6144) score += 500;
    
    return score;
}

// Compatibility Checking
bool AndroidConfig::IsDeviceSupported(const AndroidDeviceInfo& info) {
    // Minimum requirements for Xenia on Android
    if (info.cpu_cores < 4) {
        XELOGE("Insufficient CPU cores: %d (minimum: 4)", info.cpu_cores);
        return false;
    }
    
    if (!info.supports_neon) {
        XELOGE("NEON instructions not supported");
        return false;
    }
    
    if (info.total_ram_mb < 3072) {
        XELOGE("Insufficient RAM: %dMB (minimum: 3072MB)", info.total_ram_mb);
        return false;
    }
    
    if (info.android_version < 24) { // Android 7.0
        XELOGE("Android version too old: %d (minimum: 24)", info.android_version);
        return false;
    }
    
    return true;
}

DeviceCompatibility AndroidConfig::GetDeviceCompatibility(const AndroidDeviceInfo& info) {
    DeviceCompatibility compat;
    compat.is_supported = IsDeviceSupported(info);
    
    // Calculate compatibility rating (0-10)
    compat.compatibility_rating = 7; // Base rating
    
    if (info.supports_armv8) compat.compatibility_rating += 1;
    if (info.supports_vulkan) compat.compatibility_rating += 1;
    if (info.total_ram_mb >= 6144) compat.compatibility_rating += 1;
    
    // Add supported features
    compat.supported_features.push_back("ARM CPU");
    if (info.supports_neon) compat.supported_features.push_back("NEON SIMD");
    if (info.supports_vulkan) compat.supported_features.push_back("Vulkan Graphics");
    if (info.has_gamepad_support) compat.supported_features.push_back("Gamepad Input");
    
    // Add known issues
    if (info.gpu_type == GPUType::POWERVR) {
        compat.known_issues.push_back("PowerVR GPU may have compatibility issues");
    }
    
    if (!info.supports_armv8) {
        compat.known_issues.push_back("32-bit ARM may have performance limitations");
    }
    
    // Add workarounds
    compat.recommended_workarounds.push_back("Use Vulkan backend for best performance");
    compat.recommended_workarounds.push_back("Enable vsync to reduce power consumption");
    
    compat.compatibility_notes = "Device meets basic requirements for Xenia emulation";
    
    return compat;
}

std::string AndroidConfig::GetCompatibilityReport(const AndroidDeviceInfo& info) {
    auto compat = GetDeviceCompatibility(info);
    
    std::stringstream report;
    report << "=== Xenia Android Compatibility Report ===\n";
    report << "Device: " << info.manufacturer << " " << info.device_model << "\n";
    report << "SOC: " << GetSOCVendorName(info.soc_vendor) << " " << info.soc_model << "\n";
    report << "Android: " << info.android_version_name << " (API " << info.android_version << ")\n";
    report << "CPU: " << info.cpu_cores << " cores, " << info.cpu_architecture << ", " << info.cpu_max_freq_mhz << " MHz\n";
    report << "GPU: " << GetGPUTypeName(info.gpu_type) << " " << info.gpu_renderer << "\n";
    report << "RAM: " << info.total_ram_mb << "MB total, " << info.available_ram_mb << "MB available\n";
    report << "Performance Tier: " << GetPerformanceTierName(info.performance_tier) << "\n";
    report << "Compatibility Rating: " << compat.compatibility_rating << "/10\n";
    report << "Supported: " << (compat.is_supported ? "YES" : "NO") << "\n";
    
    if (!compat.supported_features.empty()) {
        report << "\nSupported Features:\n";
        for (const auto& feature : compat.supported_features) {
            report << "  • " << feature << "\n";
        }
    }
    
    if (!compat.known_issues.empty()) {
        report << "\nKnown Issues:\n";
        for (const auto& issue : compat.known_issues) {
            report << "  ⚠ " << issue << "\n";
        }
    }
    
    if (!compat.recommended_workarounds.empty()) {
        report << "\nRecommended Workarounds:\n";
        for (const auto& workaround : compat.recommended_workarounds) {
            report << "  💡 " << workaround << "\n";
        }
    }
    
    report << "\n" << compat.compatibility_notes << "\n";
    
    return report.str();
}

// Settings Recommendation
RecommendedSettings AndroidConfig::GetRecommendedSettings(const AndroidDeviceInfo& info) {
    return GetOptimalSettings(info, info.performance_tier);
}

RecommendedSettings AndroidConfig::GetOptimalSettings(const AndroidDeviceInfo& info, PerformanceTier tier) {
    RecommendedSettings settings;
    
    switch (tier) {
        case PerformanceTier::VERY_LOW_END:
            settings.resolution_scale = 0.5f;
            settings.msaa_samples = 1;
            settings.texture_filtering = 1;
            settings.vsync = true;
            settings.gpu_timing = false;
            settings.audio_buffer_size = 2048;
            settings.audio_sample_rate = 44100;
            settings.cpu_thread_count = 2;
            settings.enable_smt = false;
            settings.cache_size_mb = 128;
            settings.enable_thermal_throttling = true;
            settings.frame_rate_limit = true;
            settings.max_frame_rate = 30;
            settings.graphics_preset = "low";
            break;
            
        case PerformanceTier::LOW_END:
            settings.resolution_scale = 0.75f;
            settings.msaa_samples = 1;
            settings.texture_filtering = 2;
            settings.vsync = true;
            settings.gpu_timing = false;
            settings.audio_buffer_size = 1536;
            settings.audio_sample_rate = 48000;
            settings.cpu_thread_count = 4;
            settings.enable_smt = true;
            settings.cache_size_mb = 192;
            settings.enable_thermal_throttling = true;
            settings.frame_rate_limit = true;
            settings.max_frame_rate = 45;
            settings.graphics_preset = "medium";
            break;
            
        case PerformanceTier::MID_RANGE:
            settings.resolution_scale = 1.0f;
            settings.msaa_samples = 2;
            settings.texture_filtering = 3;
            settings.vsync = true;
            settings.gpu_timing = false;
            settings.audio_buffer_size = 1024;
            settings.audio_sample_rate = 48000;
            settings.cpu_thread_count = 0; // auto
            settings.enable_smt = true;
            settings.cache_size_mb = 256;
            settings.enable_thermal_throttling = true;
            settings.frame_rate_limit = false;
            settings.max_frame_rate = 60;
            settings.graphics_preset = "high";
            break;
            
        case PerformanceTier::HIGH_END:
            settings.resolution_scale = 1.25f;
            settings.msaa_samples = 4;
            settings.texture_filtering = 4;
            settings.vsync = true;
            settings.gpu_timing = true;
            settings.audio_buffer_size = 512;
            settings.audio_sample_rate = 48000;
            settings.cpu_thread_count = 0; // auto
            settings.enable_smt = true;
            settings.cache_size_mb = 384;
            settings.enable_thermal_throttling = false;
            settings.frame_rate_limit = false;
            settings.graphics_preset = "ultra";
            break;
            
        case PerformanceTier::FLAGSHIP:
            settings.resolution_scale = 1.5f;
            settings.msaa_samples = 4;
            settings.texture_filtering = 5;
            settings.vsync = true;
            settings.gpu_timing = true;
            settings.audio_buffer_size = 512;
            settings.audio_sample_rate = 96000;
            settings.cpu_thread_count = 0; // auto
            settings.enable_smt = true;
            settings.cache_size_mb = 512;
            settings.enable_thermal_throttling = false;
            settings.frame_rate_limit = false;
            settings.graphics_preset = "extreme";
            break;
    }
    
    return settings;
}

RecommendedSettings AndroidConfig::GetBatterySavingSettings(const AndroidDeviceInfo& info) {
    auto settings = GetOptimalSettings(info, PerformanceTier::LOW_END);
    settings.frame_rate_limit = true;
    settings.max_frame_rate = 30;
    settings.audio_sample_rate = 44100;
    settings.audio_buffer_size = 2048;
    settings.battery_preset = "power_saver";
    return settings;
}

RecommendedSettings AndroidConfig::GetPerformanceSettings(const AndroidDeviceInfo& info) {
    auto settings = GetOptimalSettings(info, info.performance_tier);
    settings.frame_rate_limit = false;
    settings.enable_thermal_throttling = false;
    settings.performance_preset = "max_performance";
    return settings;
}

// Configuration Accessors
std::string AndroidConfig::GetString(const std::string& key, const std::string& default_value) {
    auto it = config_values_.find(key);
    return it != config_values_.end() ? it->second : default_value;
}

int AndroidConfig::GetInt(const std::string& key, int default_value) {
    auto it = config_values_.find(key);
    if (it != config_values_.end()) {
        try {
            return std::stoi(it->second);
        } catch (...) {
            XELOGW("Failed to parse integer for key: %s", key.c_str());
        }
    }
    return default_value;
}

bool AndroidConfig::GetBool(const std::string& key, bool default_value) {
    auto it = config_values_.find(key);
    if (it != config_values_.end()) {
        std::string value = it->second;
        std::transform(value.begin(), value.end(), value.begin(), ::tolower);
        return value == "true" || value == "1" || value == "yes";
    }
    return default_value;
}

float AndroidConfig::GetFloat(const std::string& key, float default_value) {
    auto it = config_values_.find(key);
    if (it != config_values_.end()) {
        try {
            return std::stof(it->second);
        } catch (...) {
            XELOGW("Failed to parse float for key: %s", key.c_str());
        }
    }
    return default_value;
}

void AndroidConfig::SetString(const std::string& key, const std::string& value) {
    config_values_[key] = value;
    has_unsaved_changes_ = true;
}

void AndroidConfig::SetInt(const std::string& key, int value) {
    config_values_[key] = std::to_string(value);
    has_unsaved_changes_ = true;
}

void AndroidConfig::SetBool(const std::string& key, bool value) {
    config_values_[key] = value ? "true" : "false";
    has_unsaved_changes_ = true;
}

void AndroidConfig::SetFloat(const std::string& key, float value) {
    config_values_[key] = std::to_string(value);
    has_unsaved_changes_ = true;
}

// Specific configuration getters
std::string AndroidConfig::GetGPUBackend() {
    return GetString("gpu_backend", "vulkan");
}

std::string AndroidConfig::GetResolution() {
    return GetString("resolution", "1280x720");
}

bool AndroidConfig::IsFullscreen() {
    return GetBool("fullscreen", true);
}

bool AndroidConfig::IsVSyncEnabled() {
    return GetBool("vsync", true);
}

bool AndroidConfig::IsAudioEnabled() {
    return GetBool("audio_enabled", true);
}

std::string AndroidConfig::GetControllerType() {
    return GetString("controller_type", "xbox360");
}

std::string AndroidConfig::GetLanguage() {
    return GetString("language", "en-US");
}

int AndroidConfig::GetRegion() {
    return GetInt("region", 1);
}

std::string AndroidConfig::GetContentRoot() {
    return GetString("content_root", "/sdcard/xenia/content");
}

std::string AndroidConfig::GetCacheRoot() {
    return GetString("cache_root", "/sdcard/xenia/cache");
}

int AndroidConfig::GetLogLevel() {
    return GetInt("log_level", 2);
}

// Utility functions
std::string AndroidConfig::GetAndroidVersionName(int sdk_version) {
    switch (sdk_version) {
        case 33: return "Android 13";
        case 32: return "Android 12L";
        case 31: return "Android 12";
        case 30: return "Android 11";
        case 29: return "Android 10";
        case 28: return "Android 9";
        case 27: return "Android 8.1";
        case 26: return "Android 8.0";
        case 25: return "Android 7.1";
        case 24: return "Android 7.0";
        default: return "Unknown";
    }
}

std::string AndroidConfig::GetGPUTypeName(GPUType type) {
    switch (type) {
        case GPUType::ADRENO: return "Adreno";
        case GPUType::MALI: return "Mali";
        case GPUType::POWERVR: return "PowerVR";
        case GPUType::NVIDIA: return "NVIDIA";
        case GPUType::INTEL: return "Intel";
        default: return "Unknown";
    }
}

std::string AndroidConfig::GetPerformanceTierName(PerformanceTier tier) {
    switch (tier) {
        case PerformanceTier::VERY_LOW_END: return "Very Low End";
        case PerformanceTier::LOW_END: return "Low End";
        case PerformanceTier::MID_RANGE: return "Mid Range";
        case PerformanceTier::HIGH_END: return "High End";
        case PerformanceTier::FLAGSHIP: return "Flagship";
        default: return "Unknown";
    }
}

std::string AndroidConfig::GetSOCVendorName(SOCVendor vendor) {
    switch (vendor) {
        case SOCVendor::QUALCOMM: return "Qualcomm";
        case SOCVendor::SAMSUNG: return "Samsung";
        case SOCVendor::MEDIATEK: return "MediaTek";
        case SOCVendor::HUAWEI: return "Huawei";
        case SOCVendor::UNISOC: return "Unisoc";
        case SOCVendor::GOOGLE: return "Google";
        default: return "Unknown";
    }
}

// Diagnostic information
std::string AndroidConfig::GetSystemInfo() {
    auto info = GetDeviceInfo();
    std::stringstream ss;
    
    ss << "System Information:\n";
    ss << "  Device: " << info.manufacturer << " " << info.device_model << "\n";
    ss << "  Android: " << info.android_version_name << " (API " << info.android_version << ")\n";
    ss << "  SOC: " << GetSOCVendorName(info.soc_vendor) << " " << info.soc_model << "\n";
    ss << "  Architecture: " << info.cpu_architecture << "\n";
    ss << "  CPU: " << info.cpu_cores << " cores, " << info.cpu_max_freq_mhz << " MHz\n";
    ss << "  GPU: " << GetGPUTypeName(info.gpu_type) << " " << info.gpu_renderer << "\n";
    ss << "  RAM: " << info.total_ram_mb << "MB\n";
    ss << "  Performance: " << GetPerformanceTierName(info.performance_tier) << "\n";
    
    return ss.str();
}

} // namespace xanite
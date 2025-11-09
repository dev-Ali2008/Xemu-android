#include "android_config.h"
#include <jni.h>
#include <android/api-level.h>
#include <sys/system_properties.h>
#include <cpu-features.h>
#include <string>
#include <sstream>
#include <fstream>
#include <vector>
#include "xenia/base/logging.h"
#include "xenia/base/string.h"
#include "xenia/base/filesystem.h"
// 用于与……通信的移动系统 xenia
namespace xanite {

static AndroidDeviceInfo g_device_info;
static bool g_device_info_initialized = false;

AndroidDeviceInfo AndroidConfig::GetDeviceInfo() {
    if (g_device_info_initialized) {
        return g_device_info;
    }

    AndroidDeviceInfo info;
      
    char model[PROP_VALUE_MAX];
    __system_property_get("ro.product.model", model);
    info.device_model = model;
        
    char manufacturer[PROP_VALUE_MAX];
    __system_property_get("ro.product.manufacturer", manufacturer);
    info.manufacturer = manufacturer;
        
    char sdk_version[PROP_VALUE_MAX];
    __system_property_get("ro.build.version.sdk", sdk_version);
    info.android_version = std::stoi(sdk_version);
        
    char hardware[PROP_VALUE_MAX];
    __system_property_get("ro.hardware", hardware);
    char platform[PROP_VALUE_MAX];
    __system_property_get("ro.board.platform", platform);
        
    info.cpu_cores = android_getCpuCount();
    info.cpu_family = android_getCpuFamily();
        
    uint64_t cpu_features = android_getCpuFeatures();
    info.supports_neon = (cpu_features & ANDROID_CPU_ARM_FEATURE_NEON) != 0;
    info.supports_armv8 = (cpu_features & ANDROID_CPU_ARM_FEATURE_ARMv8) != 0;
    info.supports_vfpv3 = (cpu_features & ANDROID_CPU_ARM_FEATURE_VFPv3) != 0;
        
    DetectSOCAndGPU(info, hardware, platform);
        
    DetectMemoryInfo(info);
        
    EstimatePerformanceTier(info);
        
    g_device_info = info;
    g_device_info_initialized = true;
    
    XELOGI("Device Info: %s %s, Android %d, %d cores, %dMB RAM, GPU: %s %s",
           info.manufacturer.c_str(), info.device_model.c_str(),
           info.android_version, info.cpu_cores, info.total_ram_mb,
           info.gpu_vendor.c_str(), info.gpu_renderer.c_str());
    
    return info;
}

void AndroidConfig::DetectSOCAndGPU(AndroidDeviceInfo& info, const char* hardware, const char* platform) {
    info.gpu_vendor = hardware;
    info.gpu_renderer = platform;
    
    std::string hardware_lower = xe::utf8::to_lower_case(hardware);
    std::string platform_lower = xe::utf8::to_lower_case(platform);
    std::string model_lower = xe::utf8::to_lower_case(info.device_model);
        
    if (hardware_lower.find("qcom") != std::string::npos ||
        platform_lower.find("qcom") != std::string::npos ||
        model_lower.find("snapdragon") != std::string::npos) {
        
        info.gpu_type = GPUType::ADRENO;
        // GPU 治疗         
        if (model_lower.find("845") != std::string::npos || 
            hardware_lower.find("sdm845") != std::string::npos) {
            info.gpu_renderer = "Adreno 630";
        } else if (model_lower.find("855") != std::string::npos ||
                  hardware_lower.find("sdm855") != std::string::npos) {
            info.gpu_renderer = "Adreno 640";
        } else if (model_lower.find("865") != std::string::npos ||
                  hardware_lower.find("sdm865") != std::string::npos) {
            info.gpu_renderer = "Adreno 650";
        } else if (model_lower.find("888") != std::string::npos) {
            info.gpu_renderer = "Adreno 660";
        } else {
            info.gpu_renderer = "Adreno (Unknown)";
        }
    }
    
    else if (hardware_lower.find("mali") != std::string::npos ||
             platform_lower.find("mali") != std::string::npos) {
        info.gpu_type = GPUType::MALI;
        
        if (model_lower.find("g76") != std::string::npos) {
            info.gpu_renderer = "Mali-G76";
        } else if (model_lower.find("g77") != std::string::npos) {
            info.gpu_renderer = "Mali-G77";
        } else if (model_lower.find("g78") != std::string::npos) {
            info.gpu_renderer = "Mali-G78";
        } else {
            info.gpu_renderer = "Mali (Unknown)";
        }
    }
    
    else if (hardware_lower.find("powervr") != std::string::npos) {
        info.gpu_type = GPUType::POWERVR;
        info.gpu_renderer = "PowerVR";
    }
    
    else if (hardware_lower.find("nvidia") != std::string::npos ||
             hardware_lower.find("tegra") != std::string::npos) {
        info.gpu_type = GPUType::NVIDIA;
        info.gpu_renderer = "NVIDIA";
    }
    else {
        info.gpu_type = GPUType::UNKNOWN;
        info.gpu_renderer = "Unknown";
    }
}

void AndroidConfig::DetectMemoryInfo(AndroidDeviceInfo& info) {
    
    FILE* meminfo = fopen("/proc/meminfo", "r");
    if (meminfo) {
        char line[256];
        while (fgets(line, sizeof(line), meminfo)) {
            if (strstr(line, "MemTotal:")) {
                unsigned long mem_total_kb;
                sscanf(line, "MemTotal: %lu kB", &mem_total_kb);
                info.total_ram_mb = mem_total_kb / 1024;
                break;
            }
        }
        fclose(meminfo);
    }
    
    
    if (info.total_ram_mb == 0) {
        
        if (info.cpu_cores >= 8) {
            info.total_ram_mb = 6144; 
        } else if (info.cpu_cores >= 6) {
            info.total_ram_mb = 4096; 
        } else {
            info.total_ram_mb = 3072; 
        }
    }
        
    if (info.total_ram_mb >= 8192) { 
        info.available_ram_mb = info.total_ram_mb - 2048; 
    } else if (info.total_ram_mb >= 6144) { 
        info.available_ram_mb = info.total_ram_mb - 1536; 
    } else if (info.total_ram_mb >= 4096) { 
        info.available_ram_mb = info.total_ram_mb - 1024; 
    } else { 
        info.available_ram_mb = info.total_ram_mb - 768; 
    }
        
    info.available_ram_mb = std::min(info.available_ram_mb, 4096u); 
    
    XELOGI("Memory: %dMB total, %dMB available for emulation", 
           info.total_ram_mb, info.available_ram_mb);
}

void AndroidConfig::EstimatePerformanceTier(AndroidDeviceInfo& info) {
    
    info.performance_tier = PerformanceTier::MID_RANGE;
    
    
    if (info.gpu_type == GPUType::ADRENO) {
        
        if (info.gpu_renderer.find("Adreno 6") != std::string::npos) {
            if (info.gpu_renderer.find("Adreno 64") != std::string::npos ||
                info.gpu_renderer.find("Adreno 65") != std::string::npos ||
                info.gpu_renderer.find("Adreno 66") != std::string::npos) {
                
                info.performance_tier = PerformanceTier::HIGH_END;
            } else if (info.gpu_renderer.find("Adreno 63") != std::string::npos) {
                
                info.performance_tier = PerformanceTier::MID_RANGE;
            } else {
                
                info.performance_tier = PerformanceTier::MID_RANGE;
            }
        } else if (info.gpu_renderer.find("Adreno 7") != std::string::npos) {
            
            info.performance_tier = PerformanceTier::HIGH_END;
        } else {
            
            info.performance_tier = PerformanceTier::LOW_END;
        }
    } 
    else if (info.gpu_type == GPUType::MALI) {
        
        if (info.gpu_renderer.find("G78") != std::string::npos ||
            info.gpu_renderer.find("G77") != std::string::npos) {
            info.performance_tier = PerformanceTier::HIGH_END;
        } else if (info.gpu_renderer.find("G76") != std::string::npos) {
            info.performance_tier = PerformanceTier::MID_RANGE;
        } else {
            info.performance_tier = PerformanceTier::LOW_END;
        }
    }
    else if (info.gpu_type == GPUType::POWERVR) {
        
        info.performance_tier = PerformanceTier::LOW_END;
    }
        
    if (info.cpu_cores >= 8 && info.supports_armv8) {
        
        info.performance_tier = static_cast<PerformanceTier>(
            std::min(static_cast<int>(info.performance_tier) + 1, 
                    static_cast<int>(PerformanceTier::HIGH_END)));
    } else if (info.cpu_cores < 4 || !info.supports_neon) {
        
        info.performance_tier = PerformanceTier::LOW_END;
    }
        
    if (info.available_ram_mb < 2048) {
        
        info.performance_tier = PerformanceTier::LOW_END;
    } else if (info.available_ram_mb >= 3072) {
        
        if (info.performance_tier < PerformanceTier::HIGH_END) {
            info.performance_tier = static_cast<PerformanceTier>(
                static_cast<int>(info.performance_tier) + 1);
        }
    }
    
    XELOGI("Performance Tier: %d", static_cast<int>(info.performance_tier));
}

bool AndroidConfig::IsDeviceSupported(const AndroidDeviceInfo& info) {
       
    if (info.cpu_cores < 4) {
        XELOGE("Device has insufficient CPU cores: %d (minimum: 4)", info.cpu_cores);
        return false;
    }
    
    
    if (!info.supports_neon) {
        XELOGE("Device does not support NEON instructions");
        return false;
    }
    
    
    if (info.total_ram_mb < 3072) {
        XELOGE("Device has insufficient RAM: %dMB (minimum: 3072MB)", info.total_ram_mb);
        return false;
    }
    
    
    if (info.android_version < 24) {
        XELOGE("Android version too old: %d (minimum: 24 - Android 7.0)", info.android_version);
        return false;
    }
    
    
    if (!info.supports_armv8) {
        XELOGW("Device is 32-bit only, performance may be limited");
        
    }
    
    
    if (info.gpu_type == GPUType::POWERVR) {
        XELOGW("PowerVR GPU detected - compatibility may be limited");
    }
    
    XELOGI("Device meets minimum requirements for Xenia");
    return true;
}

std::string AndroidConfig::GetDeviceCompatibilityReport(const AndroidDeviceInfo& info) {
    std::stringstream report;
    
    report << "=== Xanite Device Compatibility Report ===\n";
    report << "Device: " << info.manufacturer << " " << info.device_model << "\n";
    report << "Android: " << info.android_version << " (API " << info.android_version << ")\n";
    report << "CPU: " << info.cpu_cores << " cores, " 
           << (info.supports_armv8 ? "ARMv8" : "ARMv7") << ", "
           << (info.supports_neon ? "NEON supported" : "No NEON") << "\n";
    report << "GPU: " << info.gpu_vendor << " " << info.gpu_renderer << "\n";
    report << "RAM: " << info.total_ram_mb << "MB total, " 
           << info.available_ram_mb << "MB available for emulation\n";
    report << "Performance Tier: ";
    
    switch (info.performance_tier) {
        case PerformanceTier::LOW_END:
            report << "Low End\n";
            report << "Recommendation: Use low settings, simpler 2D games\n";
            report << "Expected Performance: 15-30 FPS for light games\n";
            break;
        case PerformanceTier::MID_RANGE:
            report << "Mid Range (Snapdragon 845/855 level)\n";
            report << "Recommendation: Medium settings, most games playable\n";
            report << "Expected Performance: 30-60 FPS for many games\n";
            break;
        case PerformanceTier::HIGH_END:
            report << "High End (Snapdragon 865+ level)\n";
            report << "Recommendation: High settings, all games should work\n";
            report << "Expected Performance: 60+ FPS for most games\n";
            break;
    }
    
    
    report << "\nCompatibility Status: ";
    if (!IsDeviceSupported(info)) {
        report << "❌ NOT SUPPORTED - Minimum requirements not met\n";
    } else {
        report << "✅ SUPPORTED - Meets minimum requirements\n";
        
        
        if (info.performance_tier == PerformanceTier::MID_RANGE) {
            report << "\n💡 Tips for Snapdragon 845/855 devices:\n";
            report << "• Start with 0.8x resolution scale\n";
            report << "• Disable MSAA for better performance\n";
            report << "• Use Vulkan backend\n";
            report << "• Close background apps before playing\n";
        }
    }
    
    return report.str();
}

RecommendedSettings AndroidConfig::GetRecommendedSettings(const AndroidDeviceInfo& info) {
    RecommendedSettings settings;
    
    
    switch (info.performance_tier) {
        case PerformanceTier::LOW_END:
            settings.resolution_scale = 0.5f;
            settings.msaa_samples = 1;
            settings.texture_filtering = 1; 
            settings.vsync = true;
            settings.cpu_optimizations = true;
            settings.gpu_timing = false;
            settings.audio_buffer_size = 2048; 
            break;
            
        case PerformanceTier::MID_RANGE:
            
            settings.resolution_scale = 0.8f;
            settings.msaa_samples = 1; 
            settings.texture_filtering = 2; 
            settings.vsync = true;
            settings.cpu_optimizations = true;
            settings.gpu_timing = false;
            settings.audio_buffer_size = 1024;
            break;
            
        case PerformanceTier::HIGH_END:
            settings.resolution_scale = 1.0f;
            settings.msaa_samples = 2;
            settings.texture_filtering = 3; 
            settings.vsync = true;
            settings.cpu_optimizations = true;
            settings.gpu_timing = true;
            settings.audio_buffer_size = 512;
            break;
    }
       
    if (info.gpu_type == GPUType::ADRENO) {
        
        if (info.performance_tier >= PerformanceTier::MID_RANGE) {
            settings.msaa_samples = 2; 
        }
    } else if (info.gpu_type == GPUType::MALI) {
        
        settings.texture_filtering = std::min(settings.texture_filtering + 1, 3);
        settings.msaa_samples = 1; 
    } else if (info.gpu_type == GPUType::POWERVR) {
        
        settings.resolution_scale = std::max(0.5f, settings.resolution_scale - 0.2f);
        settings.msaa_samples = 1;
    }
        
    if (info.available_ram_mb < 3072) {
        
        settings.resolution_scale = std::max(0.5f, settings.resolution_scale - 0.2f);
        settings.texture_filtering = std::max(1, settings.texture_filtering - 1);
    } else if (info.available_ram_mb >= 4096) {
        
        settings.resolution_scale = std::min(1.0f, settings.resolution_scale + 0.1f);
    }
      
    if (info.android_version >= 30) { 
        
        settings.gpu_timing = true; 
    }
    
    XELOGI("Recommended settings: resolution=%.1fx, MSAA=%dx, filtering=%d",
           settings.resolution_scale, settings.msaa_samples, settings.texture_filtering);
    
    return settings;
}

void AndroidConfig::ApplyRecommendedSettings(const RecommendedSettings& settings) {
       
    XELOGI("Applying recommended settings for device:");
    XELOGI("  Resolution Scale: %.2f", settings.resolution_scale);
    XELOGI("  MSAA Samples: %d", settings.msaa_samples);
    XELOGI("  Texture Filtering: %d", settings.texture_filtering);
    XELOGI("  VSync: %s", settings.vsync ? "Enabled" : "Disabled");
    XELOGI("  CPU Optimizations: %s", settings.cpu_optimizations ? "Enabled" : "Disabled");
    XELOGI("  GPU Timing: %s", settings.gpu_timing ? "Enabled" : "Disabled");
    XELOGI("  Audio Buffer: %d samples", settings.audio_buffer_size);
      
}

std::vector<std::string> AndroidConfig::GetSystemDirectories() {
    std::vector<std::string> dirs;
    
    
    dirs.push_back("/sdcard/xenia");
    dirs.push_back("/storage/emulated/0/xenia");
    
    
    dirs.push_back("/storage/sdcard1/xenia");
    dirs.push_back("/storage/extSdCard/xenia");
    
    
    dirs.push_back("/data/data/com.xanite.emulator/files");
    
    
    dirs.push_back("/mnt/sdcard/xenia");
    dirs.push_back("/sdcard/external_sd/xenia");
    
    return dirs;
}

bool AndroidConfig::CreateAppDirectories() {
    auto dirs = GetSystemDirectories();
    bool success = true;
    int created_count = 0;
    
    for (const auto& base_dir : dirs) {
        
        if (!xe::filesystem::PathExists(base_dir)) {
            
            if (!xe::filesystem::CreateFolder(base_dir)) {
                XELOGW("Failed to create base directory: %s", base_dir.c_str());
                continue;
            }
        }
        
        std::vector<std::string> subdirs = {
            base_dir + "/content",
            base_dir + "/cache",
            base_dir + "/saves",
            base_dir + "/config",
            base_dir + "/logs",
            base_dir + "/screenshots",
            base_dir + "/states",
            base_dir + "/backups"
        };
        
        for (const auto& dir : subdirs) {
            if (xe::filesystem::CreateFolder(dir)) {
                created_count++;
                XELOGD("Created directory: %s", dir.c_str());
            } else {
                XELOGW("Failed to create directory: %s", dir.c_str());
                success = false;
            }
        }
    }
    
    XELOGI("Created %d app directories", created_count);
    return success;
}

bool IsSnapdragon845(const AndroidDeviceInfo& info) {
    std::string model_lower = xe::utf8::to_lower_case(info.device_model);
    return (model_lower.find("845") != std::string::npos ||
            info.gpu_renderer.find("Adreno 630") != std::string::npos);
}

bool IsSnapdragon855(const AndroidDeviceInfo& info) {
    std::string model_lower = xe::utf8::to_lower_case(info.device_model);
    return (model_lower.find("855") != std::string::npos ||
            info.gpu_renderer.find("Adreno 640") != std::string::npos);
}

} 

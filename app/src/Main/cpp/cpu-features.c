#include "cpu-features.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/system_properties.h>

// Implementation for Android CPU features detection
int android_getCpuCount(void) {
    return sysconf(_SC_NPROCESSORS_CONF);
}

int android_getCpuFamily(void) {
    char cpu_abi[PROP_VALUE_MAX];
    char cpu_abi2[PROP_VALUE_MAX];
    
    __system_property_get("ro.product.cpu.abi", cpu_abi);
    __system_property_get("ro.product.cpu.abi2", cpu_abi2);
    
    if (strstr(cpu_abi, "arm64") != NULL || strstr(cpu_abi2, "arm64") != NULL) {
        return ANDROID_CPU_FAMILY_ARM64;
    } else if (strstr(cpu_abi, "arm") != NULL || strstr(cpu_abi2, "arm") != NULL) {
        return ANDROID_CPU_FAMILY_ARM;
    } else if (strstr(cpu_abi, "x86_64") != NULL || strstr(cpu_abi2, "x86_64") != NULL) {
        return ANDROID_CPU_FAMILY_X86_64;
    } else if (strstr(cpu_abi, "x86") != NULL || strstr(cpu_abi2, "x86") != NULL) {
        return ANDROID_CPU_FAMILY_X86;
    } else if (strstr(cpu_abi, "mips64") != NULL || strstr(cpu_abi2, "mips64") != NULL) {
        return ANDROID_CPU_FAMILY_MIPS64;
    } else if (strstr(cpu_abi, "mips") != NULL || strstr(cpu_abi2, "mips") != NULL) {
        return ANDROID_CPU_FAMILY_MIPS;
    }
    
    return ANDROID_CPU_FAMILY_UNKNOWN;
}

uint64_t android_getCpuFeatures(void) {
    uint64_t features = 0;
    char hardware[PROP_VALUE_MAX];
    char features_str[PROP_VALUE_MAX];
    
    __system_property_get("ro.hardware", hardware);
    __system_property_get("ro.product.cpu.features", features_str);
    
    int cpu_family = android_getCpuFamily();
    
    if (cpu_family == ANDROID_CPU_FAMILY_ARM || cpu_family == ANDROID_CPU_FAMILY_ARM64) {
        // Assume ARMv7 for most modern ARM devices
        features |= ANDROID_CPU_ARM_FEATURE_ARMv7;
        
        // Check for NEON support
        if (strstr(features_str, "neon") != NULL || strstr(features_str, "asimd") != NULL) {
            features |= ANDROID_CPU_ARM_FEATURE_NEON;
        }
        
        // Check for VFPv3
        if (strstr(features_str, "vfpv3") != NULL || strstr(features_str, "vfpv4") != NULL) {
            features |= ANDROID_CPU_ARM_FEATURE_VFPv3;
        }
        
        // Check for ARMv8 (64-bit)
        if (cpu_family == ANDROID_CPU_FAMILY_ARM64) {
            features |= ANDROID_CPU_ARM_FEATURE_ARMv8;
        }
        
        // Additional feature detection based on common hardware
        if (strstr(hardware, "qcom") != NULL || strstr(hardware, "sdm") != NULL) {
            // Qualcomm Snapdragon devices typically support these features
            features |= ANDROID_CPU_ARM_FEATURE_VFPv3;
            features |= ANDROID_CPU_ARM_FEATURE_NEON;
        }
        
        // Check for specific ARMv8 features
        if (cpu_family == ANDROID_CPU_FAMILY_ARM64) {
            // ARMv8 devices typically support AES, SHA, CRC32
            if (strstr(features_str, "aes") != NULL) {
                features |= ANDROID_CPU_ARM_FEATURE_AES;
            }
            if (strstr(features_str, "sha1") != NULL || strstr(features_str, "sha2") != NULL) {
                features |= ANDROID_CPU_ARM_FEATURE_SHA1;
                features |= ANDROID_CPU_ARM_FEATURE_SHA2;
            }
            if (strstr(features_str, "crc32") != NULL) {
                features |= ANDROID_CPU_ARM_FEATURE_CRC32;
            }
        }
    }
    else if (cpu_family == ANDROID_CPU_FAMILY_X86 || cpu_family == ANDROID_CPU_FAMILY_X86_64) {
        // x86 CPU features
        features |= ANDROID_CPU_ARM_FEATURE_ARMv7; // Mark as supported for compatibility
        
        // x86 devices typically support SSE, SSE2, SSE3
        if (strstr(features_str, "sse") != NULL) {
            // Mark as having SIMD support similar to NEON
            features |= ANDROID_CPU_ARM_FEATURE_NEON;
        }
    }
    
    return features;
}

int android_getCpuFeaturesExt(void) {
    // Extended features - read from additional system properties
    int extended_features = 0;
    char features2_str[PROP_VALUE_MAX];
    
    __system_property_get("ro.product.cpu.features2", features2_str);
    
    // Parse additional features if needed
    if (strstr(features2_str, "avx") != NULL) {
        // AVX support detected
    }
    
    return extended_features;
}

// Additional helper function to get CPU max frequency
int android_getCpuMaxFreq(void) {
    FILE* file = fopen("/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_max_freq", "r");
    if (file) {
        int max_freq_khz = 0;
        fscanf(file, "%d", &max_freq_khz);
        fclose(file);
        return max_freq_khz;
    }
    return 0;
}

// Helper function to get CPU architecture string
const char* android_getCpuArchitecture(void) {
    int family = android_getCpuFamily();
    switch (family) {
        case ANDROID_CPU_FAMILY_ARM: return "ARM32";
        case ANDROID_CPU_FAMILY_ARM64: return "ARM64";
        case ANDROID_CPU_FAMILY_X86: return "x86";
        case ANDROID_CPU_FAMILY_X86_64: return "x86_64";
        case ANDROID_CPU_FAMILY_MIPS: return "MIPS";
        case ANDROID_CPU_FAMILY_MIPS64: return "MIPS64";
        default: return "UNKNOWN";
    }
}

// Enhanced feature checking with better detection
bool android_cpuSupportsAdvancedSIMD(void) {
    uint64_t features = android_getCpuFeatures();
    
    // Check for NEON on ARM or SSE on x86
    if (features & ANDROID_CPU_ARM_FEATURE_NEON) {
        return true;
    }
    
    // For x86, check CPU family and assume SSE2+ support
    int family = android_getCpuFamily();
    if (family == ANDROID_CPU_FAMILY_X86 || family == ANDROID_CPU_FAMILY_X86_64) {
        return true; // x86 devices typically have SSE2+
    }
    
    return false;
}

// Check if device is 64-bit
bool android_cpuIs64Bit(void) {
    int family = android_getCpuFamily();
    return (family == ANDROID_CPU_FAMILY_ARM64 || 
            family == ANDROID_CPU_FAMILY_X86_64 || 
            family == ANDROID_CPU_FAMILY_MIPS64);
}

// Get CPU implementer and architecture for ARM
void android_getArmCpuInfo(uint32_t* implementer, uint32_t* architecture) {
    if (implementer) *implementer = 0;
    if (architecture) *architecture = 0;
    
    // Try to read from /proc/cpuinfo
    FILE* cpuinfo = fopen("/proc/cpuinfo", "r");
    if (cpuinfo) {
        char line[256];
        while (fgets(line, sizeof(line), cpuinfo)) {
            if (strstr(line, "CPU implementer") && implementer) {
                sscanf(line, "CPU implementer : 0x%x", implementer);
            } else if (strstr(line, "CPU architecture") && architecture) {
                sscanf(line, "CPU architecture : %u", architecture);
            }
        }
        fclose(cpuinfo);
    }
}
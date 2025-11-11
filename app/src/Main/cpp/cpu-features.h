#ifndef CPU_FEATURES_H
#define CPU_FEATURES_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// CPU families
enum {
    ANDROID_CPU_FAMILY_UNKNOWN = 0,
    ANDROID_CPU_FAMILY_ARM,
    ANDROID_CPU_FAMILY_X86,
    ANDROID_CPU_FAMILY_MIPS,
    ANDROID_CPU_FAMILY_ARM64,
    ANDROID_CPU_FAMILY_X86_64,
    ANDROID_CPU_FAMILY_MIPS64
};

// ARM CPU features
enum {
    ANDROID_CPU_ARM_FEATURE_ARMv7       = (1 << 0),
    ANDROID_CPU_ARM_FEATURE_VFPv3       = (1 << 1),
    ANDROID_CPU_ARM_FEATURE_NEON        = (1 << 2),
    ANDROID_CPU_ARM_FEATURE_LDREX_STREX = (1 << 3),
    ANDROID_CPU_ARM_FEATURE_VFPv2       = (1 << 4),
    ANDROID_CPU_ARM_FEATURE_VFP_D32     = (1 << 5),
    ANDROID_CPU_ARM_FEATURE_VFP_FP16    = (1 << 6),
    ANDROID_CPU_ARM_FEATURE_VFP_FMA     = (1 << 7),
    ANDROID_CPU_ARM_FEATURE_NEON_FMA    = (1 << 8),
    ANDROID_CPU_ARM_FEATURE_IDIV_ARM    = (1 << 9),
    ANDROID_CPU_ARM_FEATURE_IDIV_THUMB2 = (1 << 10),
    ANDROID_CPU_ARM_FEATURE_iWMMXt      = (1 << 11),
    ANDROID_CPU_ARM_FEATURE_AES         = (1 << 12),
    ANDROID_CPU_ARM_FEATURE_PMULL       = (1 << 13),
    ANDROID_CPU_ARM_FEATURE_SHA1        = (1 << 14),
    ANDROID_CPU_ARM_FEATURE_SHA2        = (1 << 15),
    ANDROID_CPU_ARM_FEATURE_CRC32       = (1 << 16),
    ANDROID_CPU_ARM_FEATURE_ARMv8       = (1 << 17)
};

// Function declarations
int android_getCpuCount(void);
int android_getCpuFamily(void);
uint64_t android_getCpuFeatures(void);
int android_getCpuFeaturesExt(void);

// New function declarations
int android_getCpuMaxFreq(void);
const char* android_getCpuArchitecture(void);
bool android_cpuSupportsAdvancedSIMD(void);
bool android_cpuIs64Bit(void);
void android_getArmCpuInfo(uint32_t* implementer, uint32_t* architecture);

// Helper functions for common checks
static inline bool android_cpuSupportsARMv7(void) {
    return (android_getCpuFeatures() & ANDROID_CPU_ARM_FEATURE_ARMv7) != 0;
}

static inline bool android_cpuSupportsNEON(void) {
    return (android_getCpuFeatures() & ANDROID_CPU_ARM_FEATURE_NEON) != 0;
}

static inline bool android_cpuSupportsVFPv3(void) {
    return (android_getCpuFeatures() & ANDROID_CPU_ARM_FEATURE_VFPv3) != 0;
}

static inline bool android_cpuSupportsARMv8(void) {
    return (android_getCpuFeatures() & ANDROID_CPU_ARM_FEATURE_ARMv8) != 0;
}

static inline bool android_cpuSupportsAES(void) {
    return (android_getCpuFeatures() & ANDROID_CPU_ARM_FEATURE_AES) != 0;
}

static inline bool android_cpuSupportsSHA(void) {
    uint64_t features = android_getCpuFeatures();
    return (features & (ANDROID_CPU_ARM_FEATURE_SHA1 | ANDROID_CPU_ARM_FEATURE_SHA2)) != 0;
}

static inline bool android_cpuSupportsCRC32(void) {
    return (android_getCpuFeatures() & ANDROID_CPU_ARM_FEATURE_CRC32) != 0;
}

// Performance tier estimation based on CPU capabilities
static inline int android_estimatePerformanceTier(void) {
    int cores = android_getCpuCount();
    bool is64bit = android_cpuIs64Bit();
    bool hasNeon = android_cpuSupportsNEON();
    bool hasAES = android_cpuSupportsAES();
    int maxFreq = android_getCpuMaxFreq();
    
    // Simple performance tier estimation
    if (cores >= 8 && is64bit && hasNeon && hasAES && maxFreq > 2000000) {
        return 3; // HIGH_END
    } else if (cores >= 6 && is64bit && hasNeon && maxFreq > 1500000) {
        return 2; // MID_RANGE
    } else if (cores >= 4 && hasNeon) {
        return 1; // LOW_END
    } else {
        return 0; // VERY_LOW_END
    }
}

// CPU vendor detection
static inline const char* android_getCpuVendor(void) {
    int family = android_getCpuFamily();
    switch (family) {
        case ANDROID_CPU_FAMILY_ARM:
        case ANDROID_CPU_FAMILY_ARM64:
            return "ARM";
        case ANDROID_CPU_FAMILY_X86:
        case ANDROID_CPU_FAMILY_X86_64:
            return "Intel";
        case ANDROID_CPU_FAMILY_MIPS:
        case ANDROID_CPU_FAMILY_MIPS64:
            return "MIPS";
        default:
            return "Unknown";
    }
}

// Check if CPU meets minimum requirements for Xenia emulation
static inline bool android_cpuMeetsXeniaRequirements(void) {
    int cores = android_getCpuCount();
    bool hasNeon = android_cpuSupportsNEON();
    bool is64bit = android_cpuIs64Bit();
    
    // Minimum: 4 cores, NEON support, 64-bit preferred
    if (cores < 4) return false;
    if (!hasNeon) return false;
    
    // 64-bit is strongly recommended but not strictly required
    return true;
}

// Get detailed CPU info as string for logging
static inline const char* android_getDetailedCpuInfo(void) {
    static char buffer[256];
    int cores = android_getCpuCount();
    const char* arch = android_getCpuArchitecture();
    const char* vendor = android_getCpuVendor();
    int maxFreq = android_getCpuMaxFreq();
    
    snprintf(buffer, sizeof(buffer), 
             "CPU: %s %s, %d cores, %d MHz, %s",
             vendor, arch, cores, maxFreq / 1000,
             android_cpuIs64Bit() ? "64-bit" : "32-bit");
    
    return buffer;
}

#ifdef __cplusplus
}
#endif

#endif // CPU_FEATURES_H
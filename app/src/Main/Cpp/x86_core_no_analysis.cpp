










#include "x86_core.h"
#include "xbox_memory.h"
#include <android/log.h>
#include <stdexcept>
#include <arm_neon.h>
#include <unistd.h>
#include <sys/mman.h>
#include <cstring>
#include <vector>
#include <algorithm>
#include <cstdio>

#define LOG_TAG "X86Core_NoAnalysis"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)

constexpr size_t JIT_CACHE_SIZE = 16 * 1024 * 1024;
constexpr uint32_t MAX_BLOCK_SIZE = 256;
constexpr uint32_t JIT_THRESHOLD = 10;

bool X86Core::boundsCheckingDisabled = false;


extern X86Core* createX86CoreInstance(XboxMemory* memory);


X86Core* X86Core::createInstance(XboxMemory* memory) {
    return createX86CoreInstance(memory);
}







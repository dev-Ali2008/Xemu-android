#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <functional>
#include <chrono>
#include <arm_neon.h>

namespace XboxUtils {
    std::string formatHex(uint32_t value, uint8_t width = 8);
    std::string formatHex64(uint64_t value);
    std::string formatSize(size_t bytes);
    std::string formatTimestamp(uint32_t timestamp);

    bool fileExists(const std::string& path);
    std::vector<uint8_t> readFile(const std::string& path);
    bool writeFile(const std::string& path, const std::vector<uint8_t>& data);

    uint32_t calculateCRC32(const uint8_t* data, size_t length);
    uint32_t calculateCRC32NEON(const uint8_t* data, size_t length);
    uint32_t calculateAdler32(const uint8_t* data, size_t length);
    uint32_t calculateAdler32NEON(const uint8_t* data, size_t length);

    uint16_t swap16(uint16_t value);
    uint32_t swap32(uint32_t value);
    uint64_t swap64(uint64_t value);
    uint32x4_t swap128(uint32x4_t value);

    uint32_t setBit(uint32_t value, uint8_t bit);
    uint32_t clearBit(uint32_t value, uint8_t bit);
    bool testBit(uint32_t value, uint8_t bit);

    void setDebugOutput(std::function<void(const std::string&)> callback);
    void logDebug(const std::string& message);
    void logPerformance(const std::string& message);

    uint64_t getCurrentTime();
    uint64_t getHighResolutionTime();
    uint64_t getPerformanceCounter();

    uint32_t alignUp(uint32_t value, uint32_t alignment);
    uint32_t alignDown(uint32_t value, uint32_t alignment);
    bool isAligned(uint32_t value, uint32_t alignment);

    void* alignedAlloc(size_t size, size_t alignment);
    void alignedFree(void* ptr);

    class PerformanceTimer {
    public:
        PerformanceTimer();
        void start();
        void stop();
        double elapsed() const;

    private:
        std::chrono::high_resolution_clock::time_point startTime;
        std::chrono::high_resolution_clock::time_point endTime;
    };
};

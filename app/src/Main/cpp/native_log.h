#ifndef NATIVE_LOG_H
#define NATIVE_LOG_H

#include <string>
#include <vector>
#include <deque>
#include <chrono>
#include <functional>
#include <mutex>
#include <jni.h>

namespace xanite {

enum class LogLevel {
    LOG_ERROR = 0,
    LOG_WARNING = 1,
    LOG_INFO = 2,
    LOG_DEBUG = 3,
    LOG_VERBOSE = 4
};

class NativeLog {
public:
    static NativeLog* GetInstance();
    static void Shutdown();

    void SetLogLevel(LogLevel level);
    LogLevel GetLogLevel() const;

    void Log(LogLevel level, const char* tag, const char* format, ...);

    std::vector<std::string> GetRecentLogs(int max_entries = 100);
    void ClearLogBuffer();
    bool SaveToFile(const std::string& filename = "");

    using LogCallback = std::function<void(LogLevel, const std::string&)>;
    void SetLogCallback(LogCallback callback);

private:
    NativeLog();
    ~NativeLog();

    void SendToAndroidLog(LogLevel level, const char* tag, const char* message);
    void AddToBuffer(const char* message);
    std::string GetCurrentTimestamp() const;
    const char* LogLevelToString(LogLevel level) const;

private:
    struct LogEntry {
        std::chrono::system_clock::time_point timestamp;
        std::string message;
    };

    static constexpr size_t MAX_LOG_ENTRIES = 10000;
    static constexpr size_t MAX_LOG_BUFFER = 1024 * 1024; 
    static constexpr size_t MAX_LOG_LINE = 4096;

    LogLevel log_level_;
    std::deque<LogEntry> log_entries_;
    std::string log_buffer_;
    LogCallback log_callback_;
    mutable std::mutex log_mutex_;
};

} 

#endif 

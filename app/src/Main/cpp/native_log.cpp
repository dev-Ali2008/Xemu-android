#include "native_log.h"
#include <android/log.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <mutex>
#include "xenia/base/assert.h"
#include "xenia/base/string.h"

namespace xanite {

// Global log instance
static NativeLog* g_native_log = nullptr;
static std::mutex g_log_mutex;

NativeLog::NativeLog() : log_level_(LogLevel::LOG_INFO) {
    log_buffer_.reserve(MAX_LOG_BUFFER);
}

NativeLog::~NativeLog() {
    SaveToFile();
}

NativeLog* NativeLog::GetInstance() {
    std::lock_guard<std::mutex> lock(g_log_mutex);
    if (!g_native_log) {
        g_native_log = new NativeLog();
    }
    return g_native_log;
}

void NativeLog::Shutdown() {
    std::lock_guard<std::mutex> lock(g_log_mutex);
    if (g_native_log) {
        delete g_native_log;
        g_native_log = nullptr;
    }
}

void NativeLog::SetLogLevel(LogLevel level) {
    std::lock_guard<std::mutex> lock(log_mutex_);
    log_level_ = level;
}

LogLevel NativeLog::GetLogLevel() const {
    return log_level_;
}

void NativeLog::Log(LogLevel level, const char* tag, const char* format, ...) {
    if (level < log_level_) {
        return;
    }
    
    char message_buffer[MAX_LOG_LINE];
    va_list args;
    
    // Format the message
    va_start(args, format);
    vsnprintf(message_buffer, sizeof(message_buffer), format, args);
    va_end(args);
    
    // Get current time
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    
    // Format timestamp
    char timestamp[32];
    std::strftime(timestamp, sizeof(timestamp), "%H:%M:%S", std::localtime(&time_t));
    
    // Create full log entry
    char full_message[MAX_LOG_LINE + 64];
    snprintf(full_message, sizeof(full_message), "[%s.%03lld] %s: %s", 
             timestamp, static_cast<long long>(ms.count()), tag, message_buffer);
    
    // Send to Android log
    SendToAndroidLog(level, tag, full_message);
    
    // Add to buffer
    AddToBuffer(full_message);
    
    // Call callback if set
    if (log_callback_) {
        log_callback_(level, full_message);
    }
}

void NativeLog::SendToAndroidLog(LogLevel level, const char* tag, const char* message) {
    android_LogPriority priority;
    
    switch (level) {
        case LogLevel::LOG_ERROR:
            priority = ANDROID_LOG_ERROR;
            break;
        case LogLevel::LOG_WARNING:
            priority = ANDROID_LOG_WARN;
            break;
        case LogLevel::LOG_INFO:
            priority = ANDROID_LOG_INFO;
            break;
        case LogLevel::LOG_DEBUG:
            priority = ANDROID_LOG_DEBUG;
            break;
        case LogLevel::LOG_VERBOSE:
            priority = ANDROID_LOG_VERBOSE;
            break;
        default:
            priority = ANDROID_LOG_INFO;
            break;
    }
    
    __android_log_write(priority, tag, message);
}

void NativeLog::AddToBuffer(const char* message) {
    std::lock_guard<std::mutex> lock(log_mutex_);
    
    // Add to circular buffer
    if (log_entries_.size() >= MAX_LOG_ENTRIES) {
        log_entries_.pop_front();
    }
    
    LogEntry entry;
    entry.timestamp = std::chrono::system_clock::now();
    entry.message = message;
    
    log_entries_.push_back(entry);
    
    // Also add to continuous buffer for file output
    log_buffer_ += message;
    log_buffer_ += "\n";
    
    // Trim buffer if too large
    if (log_buffer_.size() > MAX_LOG_BUFFER) {
        size_t excess = log_buffer_.size() - MAX_LOG_BUFFER;
        size_t newline_pos = log_buffer_.find('\n', excess);
        if (newline_pos != std::string::npos) {
            log_buffer_.erase(0, newline_pos + 1);
        } else {
            log_buffer_.erase(0, excess);
        }
    }
}

std::vector<std::string> NativeLog::GetRecentLogs(int max_entries) {
    std::lock_guard<std::mutex> lock(log_mutex_);
    std::vector<std::string> logs;
    
    int start_index = std::max(0, static_cast<int>(log_entries_.size()) - max_entries);
    auto it = log_entries_.begin();
    std::advance(it, start_index);
    
    for (; it != log_entries_.end(); ++it) {
        logs.push_back(it->message);
    }
    
    return logs;
}

void NativeLog::ClearLogBuffer() {
    std::lock_guard<std::mutex> lock(log_mutex_);
    log_entries_.clear();
    log_buffer_.clear();
}

bool NativeLog::SaveToFile(const std::string& filename) {
    std::lock_guard<std::mutex> lock(log_mutex_);
    
    std::string actual_filename = filename;
    if (actual_filename.empty()) {
        // Generate default filename with timestamp
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        char time_str[64];
        std::strftime(time_str, sizeof(time_str), "%Y%m%d_%H%M%S", std::localtime(&time_t));
        actual_filename = std::string("/sdcard/xenia/logs/xenia_") + time_str + ".log";
    }
    
    FILE* file = fopen(actual_filename.c_str(), "w");
    if (!file) {
        return false;
    }
    
    // Write log header
    fprintf(file, "=== Xanite Emulator Log ===\n");
    fprintf(file, "Generated: %s\n", GetCurrentTimestamp().c_str());
    fprintf(file, "Log Level: %s\n", LogLevelToString(log_level_));
    fprintf(file, "===========================\n\n");
    
    // Write log entries
    if (fwrite(log_buffer_.c_str(), 1, log_buffer_.size(), file) != log_buffer_.size()) {
        fclose(file);
        return false;
    }
    
    fclose(file);
    return true;
}

void NativeLog::SetLogCallback(LogCallback callback) {
    std::lock_guard<std::mutex> lock(log_mutex_);
    log_callback_ = callback;
}

std::string NativeLog::GetCurrentTimestamp() const {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    char buffer[64];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", std::localtime(&time_t));
    return buffer;
}

const char* NativeLog::LogLevelToString(LogLevel level) const {
    switch (level) {
        case LogLevel::LOG_ERROR: return "ERROR";
        case LogLevel::LOG_WARNING: return "WARNING";
        case LogLevel::LOG_INFO: return "INFO";
        case LogLevel::LOG_DEBUG: return "DEBUG";
        case LogLevel::LOG_VERBOSE: return "VERBOSE";
        default: return "UNKNOWN";
    }
}

// C-style interface for JNI
extern "C" {

JNIEXPORT void JNICALL
Java_com_xanite_emulator_NativeLogger_setLogLevel(JNIEnv* env, jclass clazz, jint level) {
    NativeLog::GetInstance()->SetLogLevel(static_cast<LogLevel>(level));
}

JNIEXPORT void JNICALL
Java_com_xanite_emulator_NativeLogger_log(JNIEnv* env, jclass clazz, 
                                         jint level, jstring tag, jstring message) {
    const char* tag_str = env->GetStringUTFChars(tag, nullptr);
    const char* message_str = env->GetStringUTFChars(message, nullptr);
    
    NativeLog::GetInstance()->Log(static_cast<LogLevel>(level), tag_str, "%s", message_str);
    
    env->ReleaseStringUTFChars(tag, tag_str);
    env->ReleaseStringUTFChars(message, message_str);
}

JNIEXPORT jboolean JNICALL
Java_com_xanite_emulator_NativeLogger_saveToFile(JNIEnv* env, jclass clazz, jstring filename) {
    const char* filename_str = env->GetStringUTFChars(filename, nullptr);
    bool result = NativeLog::GetInstance()->SaveToFile(filename_str);
    env->ReleaseStringUTFChars(filename, filename_str);
    return result;
}

JNIEXPORT void JNICALL
Java_com_xanite_emulator_NativeLogger_clearLogs(JNIEnv* env, jclass clazz) {
    NativeLog::GetInstance()->ClearLogBuffer();
}

} // extern "C"

// Integration with Xenia's logging system
namespace xe {
namespace internal {

void AndroidLogOutput(LogLevel level, const char* file_path, uint32_t line_number,
                      const char* function_name, const char* format, ...) {
    char message_buffer[1024];
    va_list args;
    
    // Format the message
    va_start(args, format);
    vsnprintf(message_buffer, sizeof(message_buffer), format, args);
    va_end(args);
    
    // Extract filename from path
    const char* filename = strrchr(file_path, '/');
    if (filename) {
        filename++;
    } else {
        filename = file_path;
    }
    
    // Create formatted log message
    char formatted_message[2048];
    if (level == LogLevel::LOG_ERROR) {
        snprintf(formatted_message, sizeof(formatted_message), 
                 "[%s:%d] %s: %s", filename, line_number, function_name, message_buffer);
    } else {
        snprintf(formatted_message, sizeof(formatted_message), "%s", message_buffer);
    }
    
    // Convert Xenia log level to Android log level
    LogLevel android_level;
    switch (level) {
        case LogLevel::LOG_ERROR:
            android_level = LogLevel::LOG_ERROR;
            break;
        case LogLevel::LOG_WARNING:
            android_level = LogLevel::LOG_WARNING;
            break;
        case LogLevel::LOG_INFO:
            android_level = LogLevel::LOG_INFO;
            break;
        default:
            android_level = LogLevel::LOG_DEBUG;
            break;
    }
    
    // Send to native log
    NativeLog::GetInstance()->Log(android_level, "Xenia", "%s", formatted_message);
}

} // namespace internal
} // namespace xe

} // namespace xanite
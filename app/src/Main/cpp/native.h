// native.h - الملف المعدل بدون تعارضات
#ifndef XANITE_NATIVE_H
#define XANITE_NATIVE_H

#include <jni.h>
#include <cstdint>
#include <string>
#include <memory>
#include <atomic>
#include <functional>

namespace xanite {

// استخدام نفس تعريف GameStatus الموجود في xenia_android_bridge.h
// لتجنب إعادة التعريف - إزالة التعريف من هنا واستخدام forward declaration
// enum class GameStatus; // forward declaration

// بدلاً من ذلك، نستخدم تعريف واحد فقط في ملف واحد
// سنعتمد على أن xenia_android_bridge.h هو الملف الأساسي

// Forward declarations
class XeniaAndroidBridge;
class EmuWindow_Android;
class AndroidConfig;
class AndroidSettings;
class InputManager;
class GameMetadata;

// JNI environment management
JNIEnv* GetJNIEnv();
void DetachJNIEnv();
bool AttachJNIEnv(JavaVM* vm);
void SetJavaVM(JavaVM* vm);

// Utility functions
void SendToastMessage(const std::string& message);
void SendGameStatus(int status, const std::string& message = ""); // تغيير إلى int لتجنب التعارض
void ShowProgressDialog(const std::string& title, const std::string& message);
void DismissProgressDialog();
void UpdateProgressDialog(int progress, const std::string& message = "");

// File system utilities
std::string GetExternalFilesDir();
std::string GetGameStoragePath();
bool CreateDirectoryIfNotExists(const std::string& path);

// Global instance access
XeniaAndroidBridge* GetAndroidBridge();
void SetAndroidBridge(XeniaAndroidBridge* bridge);

// Thread safety
void ExecuteOnUIThread(std::function<void()> task);
bool IsOnUIThread();

// Error handling
void ReportError(const std::string& error_message, bool fatal = false);
void LogDebug(const std::string& message);

// Performance monitoring
int GetCurrentFPS();
uint64_t GetTotalFrames();
bool IsEmulationRunning();
bool IsEmulationPaused();

} // namespace xanite

#endif // XANITE_NATIVE_H
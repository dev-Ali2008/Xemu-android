#ifndef XANITE_EMU_WINDOW_H
#define XANITE_EMU_WINDOW_H

#include <memory>
#include <thread>
#include <atomic>
#include <mutex>
#include <android/native_window.h>
#include "xenia/ui/window.h"
#include "xenia/ui/graphics_provider.h"
#include "xenia/emulator.h"

namespace xanite {

class EmuWindow_Android : public xe::ui::Window {
 public:
  explicit EmuWindow_Android(ANativeWindow* surface);
  ~EmuWindow_Android() override;
  
  bool Initialize() override;
  void Close() override;
  bool IsOpen() const override { return is_open_; }
  std::unique_ptr<xe::ui::GraphicsProvider> CreateGraphicsProvider() override;
    
  void SetSurface(ANativeWindow* surface);
  void DestroySurface();
  void OnSurfaceChanged(ANativeWindow* surface);
  void OnPause();
  void OnResume();
   
  void OnTouchEvent(int pointer_id, float x, float y, bool is_down);
  void OnKeyEvent(int key_code, bool is_down);
    
  static int GetCurrentFPS();
  static uint64_t GetTotalFrames();
  static void ResetPerformanceCounters();
    
  void RequestClose();
  bool CanClose() const;

 protected:
  void OnClose() override;
  bool OnCreate() override;
  void OnDestroy() override;
  
 private:
  bool InitializeVulkan();
  void DestroyVulkan();
  void MainLoop();
  void ProcessEvents();
  void ApplyAndroidGraphicsSettings(xe::gpu::GraphicsSystem* graphics_system);
  void ApplyAndroidConfiguration();
  void CheckSystemEvents();
  void CheckMemoryPressure();
  
  ANativeWindow* native_window_ = nullptr;
    
  std::atomic<bool> is_open_{false};
  std::atomic<bool> is_running_{false};
  std::atomic<bool> surface_ready_{false};
  std::atomic<bool> is_paused_{false};
    
  std::thread main_loop_thread_;
  mutable std::mutex state_mutex_;
    
  std::unique_ptr<xe::Emulator> emulator_;
  std::unique_ptr<xe::ui::GraphicsProvider> graphics_provider_;
    
  int window_width_ = 1280;
  int window_height_ = 720;
   
  static std::atomic<uint64_t> frame_count_;
  static std::atomic<uint64_t> last_fps_time_;
  static std::atomic<int> current_fps_;
};

} 

#endif 

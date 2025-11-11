#ifndef XANITE_EMU_WINDOW_H
#define XANITE_EMU_WINDOW_H

#ifndef MAX_FRAMES_IN_FLIGHT
#define MAX_FRAMES_IN_FLIGHT 2
#endif

#include <memory>
#include <thread>
#include <atomic>
#include <mutex>
#include <android/native_window.h>
#include <vulkan/vulkan.h>
#include <vector>
#include <functional>

// Forward declarations
namespace xe {
namespace ui {
class Window;
class GraphicsProvider;
} // namespace ui
} // namespace xe

namespace xanite {

class EmuWindow_Android {
 public:
  explicit EmuWindow_Android(ANativeWindow* surface);
  ~EmuWindow_Android();

  // Custom initialization and management
  bool Initialize();
  void Close();
  bool IsOpen() const { return is_open_; }
  
  // Surface management
  void SetSurface(ANativeWindow* surface);
  void DestroySurface();
  void OnSurfaceChanged(ANativeWindow* surface);
  void OnPause();
  void OnResume();
  
  // Input handling
  void OnTouchEvent(int pointer_id, float x, float y, bool is_down);
  void OnKeyEvent(int key_code, bool is_down);
  
  // Performance monitoring
  static int GetCurrentFPS();
  static uint64_t GetTotalFrames();
  static void ResetPerformanceCounters();
  
  // Utility methods
  void RequestClose();
  bool CanClose() const;

  // Vulkan-specific methods
  bool InitializeVulkan();
  void CleanupVulkan();
  VkInstance GetVulkanInstance() const { return vulkan_instance_; }
  VkDevice GetVulkanDevice() const { return vulkan_device_; }
  VkPhysicalDevice GetVulkanPhysicalDevice() const { return vulkan_physical_device_; }
  VkQueue GetVulkanQueue() const { return vulkan_queue_; }
  VkSurfaceKHR GetVulkanSurface() const { return vulkan_surface_; }

 private:
  bool InitializeGraphics();
  void DestroyGraphics();
  void MainLoop();
  void ProcessEvents();
  void ApplyAndroidConfiguration();
  void CheckSystemEvents();
  void CheckMemoryPressure();
  
  // Vulkan initialization methods
  bool CreateVulkanInstance();
  bool CreateVulkanSurface();
  bool SelectPhysicalDevice();
  bool CreateLogicalDevice();
  bool CreateSwapchain();
  bool CreateRenderPass();
  bool CreateFramebuffers();
  bool CreateCommandBuffers();
  bool CreateSyncObjects();
  
  // Vulkan rendering methods
  void DrawFrame();
  void RecreateSwapchain();
  void CleanupSwapchain();

  // Graphics and system components
  ANativeWindow* native_window_ = nullptr;
  std::unique_ptr<xe::ui::Window> window_;
  std::unique_ptr<xe::ui::GraphicsProvider> graphics_provider_;
  
  // Vulkan members
  VkInstance vulkan_instance_ = VK_NULL_HANDLE;
  VkPhysicalDevice vulkan_physical_device_ = VK_NULL_HANDLE;
  VkDevice vulkan_device_ = VK_NULL_HANDLE;
  VkQueue vulkan_queue_ = VK_NULL_HANDLE;
  VkSurfaceKHR vulkan_surface_ = VK_NULL_HANDLE;
  VkSwapchainKHR vulkan_swapchain_ = VK_NULL_HANDLE;
  std::vector<VkImage> vulkan_swapchain_images_;
  std::vector<VkImageView> vulkan_swapchain_image_views_;
  std::vector<VkFramebuffer> vulkan_framebuffers_;
  VkRenderPass vulkan_render_pass_ = VK_NULL_HANDLE;
  VkPipelineLayout vulkan_pipeline_layout_ = VK_NULL_HANDLE;
  VkPipeline vulkan_graphics_pipeline_ = VK_NULL_HANDLE;
  VkCommandPool vulkan_command_pool_ = VK_NULL_HANDLE;
  std::vector<VkCommandBuffer> vulkan_command_buffers_;
  
  // Synchronization objects
  std::vector<VkSemaphore> vulkan_image_available_semaphores_;
  std::vector<VkSemaphore> vulkan_render_finished_semaphores_;
  std::vector<VkFence> vulkan_in_flight_fences_;
  size_t current_frame_ = 0;
  
  // Swapchain properties
  VkFormat vulkan_swapchain_image_format_;
  VkExtent2D vulkan_swapchain_extent_;
  uint32_t vulkan_swapchain_image_count_ = 0;
  
  // State management
  std::atomic<bool> is_open_{false};
  std::atomic<bool> is_running_{false};
  std::atomic<bool> surface_ready_{false};
  std::atomic<bool> is_paused_{false};
  std::atomic<bool> vulkan_initialized_{false};
  std::atomic<bool> framebuffer_resized_{false};
  
  // Threading
  std::thread main_loop_thread_;
  mutable std::mutex state_mutex_;
  mutable std::mutex vulkan_mutex_;
  
  // Window properties
  int window_width_ = 1280;
  int window_height_ = 720;
  
  // Performance tracking
  static std::atomic<uint64_t> frame_count_;
  static std::atomic<uint64_t> last_fps_time_;
  static std::atomic<int> current_fps_;
  
  // Vulkan validation
  bool enable_validation_layers_ = true;
  const std::vector<const char*> validation_layers_ = {
      "VK_LAYER_KHRONOS_validation"
  };
  const std::vector<const char*> device_extensions_ = {
      VK_KHR_SWAPCHAIN_EXTENSION_NAME
  };
};

} // namespace xanite

#endif // XANITE_EMU_WINDOW_H
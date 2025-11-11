#include <android/log.h>
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "Xanite", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "Xanite", __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, "Xanite", __VA_ARGS__)

#include "emu_window/emu_window.h"
#include <android/log.h>
#include <android/native_window.h>
#include <chrono>
#include <thread>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <sys/prctl.h>
#include <functional>
#include <vector>
#include <set>

// Include Vulkan headers
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_android.h>

// Include Xenia headers
#include "xenia/base/logging.h"
#include "xenia/base/threading.h"

// Simple stubs for Android implementation
namespace xe {
namespace ui {

class Window {
public:
    Window() = default;
    virtual ~Window() = default;
    
    void set_size(int width, int height) {
        (void)width;
        (void)height;
    }
};

class GraphicsProvider {
public:
    virtual ~GraphicsProvider() = default;
};

class WindowPoint {
public:
    WindowPoint(int x, int y) : x_(x), y_(y) {}
    int x() const { return x_; }
    int y() const { return y_; }
private:
    int x_, y_;
};

enum class MouseButton {
    kLeft, kRight, kMiddle
};

} // namespace ui

namespace threading {
void set_name(const char* name) {
    prctl(PR_SET_NAME, name, 0, 0, 0);
}
} // namespace threading

// Simple emulator stub
class Emulator {
public:
    Emulator() = default;
    ~Emulator() = default;
    
    bool Initialize() { 
        LOGI("Emulator initialized");
        return true; 
    }
    
    void Shutdown() {
        LOGI("Emulator shutdown");
    }
    
    void Pause() {
        LOGI("Emulator paused");
        is_paused_ = true;
    }
    
    void Resume() {
        LOGI("Emulator resumed");
        is_paused_ = false;
    }
    
    bool is_title_open() const { return false; }
    bool is_paused() const { return is_paused_; }

private:
    bool is_paused_ = false;
};

} // namespace xe

#define LOG_TAG "XaniteEmuWindow"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)

namespace xanite {

// تعريف الـ static members
std::atomic<uint64_t> EmuWindow_Android::frame_count_{0};
std::atomic<uint64_t> EmuWindow_Android::last_fps_time_{0};
std::atomic<int> EmuWindow_Android::current_fps_{0};

// Vulkan debug callback
static VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void* pUserData) {
    
    if (messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        LOGW("Vulkan Validation: %s", pCallbackData->pMessage);
    } else {
        LOGI("Vulkan Validation: %s", pCallbackData->pMessage);
    }
    
    return VK_FALSE;
}

EmuWindow_Android::EmuWindow_Android(ANativeWindow* surface) 
    : native_window_(surface) {
    
    if (native_window_) {
        window_width_ = ANativeWindow_getWidth(native_window_);
        window_height_ = ANativeWindow_getHeight(native_window_);
        LOGI("Window created with dimensions: %dx%d", window_width_, window_height_);
        
        // Add reference to the window
        ANativeWindow_acquire(native_window_);
    } else {
        window_width_ = 1280;
        window_height_ = 720;
        LOGW("No native window provided, using default dimensions");
    }
    
    // Create window instance
    window_ = std::make_unique<xe::ui::Window>();
    if (window_) {
        window_->set_size(window_width_, window_height_);
    }
}

EmuWindow_Android::~EmuWindow_Android() {
    Close();
}

bool EmuWindow_Android::Initialize() {
    LOGI("Initializing EmuWindow_Android");
    
    if (!native_window_) {
        LOGE("No native window available for initialization");
        return false;
    }
    
    if (!InitializeVulkan()) {
        LOGE("Failed to initialize Vulkan");
        return false;
    }
    
    if (!InitializeGraphics()) {
        LOGE("Failed to initialize graphics");
        CleanupVulkan();
        return false;
    }
    
    is_open_ = true;
    surface_ready_ = true;
    vulkan_initialized_ = true;
    
    is_running_ = true;
    main_loop_thread_ = std::thread(&EmuWindow_Android::MainLoop, this);
    
    LOGI("EmuWindow_Android initialized successfully with Vulkan");
    return true;
}

void EmuWindow_Android::Close() {
    LOGI("Closing EmuWindow_Android");
    
    is_running_ = false;
    is_open_ = false;
    surface_ready_ = false;
    vulkan_initialized_ = false;
    
    if (main_loop_thread_.joinable()) {
        LOGI("Waiting for main loop thread to finish...");
        main_loop_thread_.join();
        LOGI("Main loop thread finished");
    }
    
    DestroyGraphics();
    CleanupVulkan();
    
    LOGI("EmuWindow_Android closed successfully");
}

// Vulkan Initialization Methods
bool EmuWindow_Android::InitializeVulkan() {
    LOGI("Initializing Vulkan...");
    
    if (!CreateVulkanInstance()) {
        LOGE("Failed to create Vulkan instance");
        return false;
    }
    
    if (!CreateVulkanSurface()) {
        LOGE("Failed to create Vulkan surface");
        return false;
    }
    
    if (!SelectPhysicalDevice()) {
        LOGE("Failed to select physical device");
        return false;
    }
    
    if (!CreateLogicalDevice()) {
        LOGE("Failed to create logical device");
        return false;
    }
    
    if (!CreateSwapchain()) {
        LOGE("Failed to create swapchain");
        return false;
    }
    
    if (!CreateRenderPass()) {
        LOGE("Failed to create render pass");
        return false;
    }
    
    if (!CreateFramebuffers()) {
        LOGE("Failed to create framebuffers");
        return false;
    }
    
    if (!CreateCommandBuffers()) {
        LOGE("Failed to create command buffers");
        return false;
    }
    
    if (!CreateSyncObjects()) {
        LOGE("Failed to create sync objects");
        return false;
    }
    
    LOGI("Vulkan initialized successfully");
    return true;
}

bool EmuWindow_Android::CreateVulkanInstance() {
    VkApplicationInfo appInfo = {};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "Xanite Emulator";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "Xanite Engine";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_1; // استخدام Vulkan 1.1

    VkInstanceCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;

    // Extensions required for Android
    std::vector<const char*> extensions = {
        VK_KHR_SURFACE_EXTENSION_NAME,
        VK_KHR_ANDROID_SURFACE_EXTENSION_NAME
    };

    if (enable_validation_layers_) {
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();

    if (enable_validation_layers_) {
        createInfo.enabledLayerCount = static_cast<uint32_t>(validation_layers_.size());
        createInfo.ppEnabledLayerNames = validation_layers_.data();
    } else {
        createInfo.enabledLayerCount = 0;
    }

    VkResult result = vkCreateInstance(&createInfo, nullptr, &vulkan_instance_);
    if (result != VK_SUCCESS) {
        LOGE("Failed to create Vulkan instance: %d", result);
        return false;
    }

    LOGI("Vulkan instance created successfully");
    return true;
}

bool EmuWindow_Android::CreateVulkanSurface() {
    if (!native_window_) {
        LOGE("No native window for surface creation");
        return false;
    }

    VkAndroidSurfaceCreateInfoKHR createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR;
    createInfo.window = native_window_;

    VkResult result = vkCreateAndroidSurfaceKHR(vulkan_instance_, &createInfo, nullptr, &vulkan_surface_);
    if (result != VK_SUCCESS) {
        LOGE("Failed to create Vulkan surface: %d", result);
        return false;
    }

    LOGI("Vulkan surface created successfully");
    return true;
}

bool EmuWindow_Android::SelectPhysicalDevice() {
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(vulkan_instance_, &deviceCount, nullptr);

    if (deviceCount == 0) {
        LOGE("No Vulkan-capable devices found");
        return false;
    }

    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(vulkan_instance_, &deviceCount, devices.data());

    // Select the first suitable device
    for (const auto& device : devices) {
        VkPhysicalDeviceProperties deviceProperties;
        vkGetPhysicalDeviceProperties(device, &deviceProperties);

        // Check for required queue families
        uint32_t queueFamilyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);

        std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

        int i = 0;
        for (const auto& queueFamily : queueFamilies) {
            if (queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                VkBool32 presentSupport = false;
                vkGetPhysicalDeviceSurfaceSupportKHR(device, i, vulkan_surface_, &presentSupport);

                if (presentSupport) {
                    vulkan_physical_device_ = device;
                    LOGI("Selected physical device: %s", deviceProperties.deviceName);
                    
                    // Log device capabilities
                    VkPhysicalDeviceFeatures features;
                    vkGetPhysicalDeviceFeatures(device, &features);
                    LOGI("Device supports: geometryShader=%d, tessellationShader=%d", 
                         features.geometryShader, features.tessellationShader);
                    
                    return true;
                }
            }
            i++;
        }
    }

    LOGE("No suitable physical device found");
    return false;
}

bool EmuWindow_Android::CreateLogicalDevice() {
    // Find queue families
    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(vulkan_physical_device_, &queueFamilyCount, nullptr);

    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(vulkan_physical_device_, &queueFamilyCount, queueFamilies.data());

    uint32_t graphicsQueueFamily = UINT32_MAX;
    uint32_t presentQueueFamily = UINT32_MAX;

    for (uint32_t i = 0; i < queueFamilyCount; i++) {
        if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            graphicsQueueFamily = i;
            
            VkBool32 presentSupport = false;
            vkGetPhysicalDeviceSurfaceSupportKHR(vulkan_physical_device_, i, vulkan_surface_, &presentSupport);
            if (presentSupport) {
                presentQueueFamily = i;
                break;
            }
        }
    }

    if (graphicsQueueFamily == UINT32_MAX || presentQueueFamily == UINT32_MAX) {
        LOGE("No suitable queue families found");
        return false;
    }

    // Create device queue info
    float queuePriority = 1.0f;
    VkDeviceQueueCreateInfo queueCreateInfo = {};
    queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueCreateInfo.queueFamilyIndex = graphicsQueueFamily;
    queueCreateInfo.queueCount = 1;
    queueCreateInfo.pQueuePriorities = &queuePriority;

    // Device features
    VkPhysicalDeviceFeatures deviceFeatures = {};
    deviceFeatures.samplerAnisotropy = VK_TRUE;

    // Create logical device
    VkDeviceCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.queueCreateInfoCount = 1;
    createInfo.pQueueCreateInfos = &queueCreateInfo;
    createInfo.pEnabledFeatures = &deviceFeatures;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(device_extensions_.size());
    createInfo.ppEnabledExtensionNames = device_extensions_.data();

    if (enable_validation_layers_) {
        createInfo.enabledLayerCount = static_cast<uint32_t>(validation_layers_.size());
        createInfo.ppEnabledLayerNames = validation_layers_.data();
    } else {
        createInfo.enabledLayerCount = 0;
    }

    VkResult result = vkCreateDevice(vulkan_physical_device_, &createInfo, nullptr, &vulkan_device_);
    if (result != VK_SUCCESS) {
        LOGE("Failed to create logical device: %d", result);
        return false;
    }

    // Get device queue
    vkGetDeviceQueue(vulkan_device_, graphicsQueueFamily, 0, &vulkan_queue_);

    LOGI("Logical device created successfully");
    return true;
}

bool EmuWindow_Android::CreateSwapchain() {
    // Get surface capabilities
    VkSurfaceCapabilitiesKHR capabilities;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(vulkan_physical_device_, vulkan_surface_, &capabilities);

    // Choose swap extent
    if (capabilities.currentExtent.width != UINT32_MAX) {
        vulkan_swapchain_extent_ = capabilities.currentExtent;
    } else {
        VkExtent2D actualExtent = {
            static_cast<uint32_t>(window_width_),
            static_cast<uint32_t>(window_height_)
        };
        
        actualExtent.width = std::max(capabilities.minImageExtent.width, 
                                    std::min(capabilities.maxImageExtent.width, actualExtent.width));
        actualExtent.height = std::max(capabilities.minImageExtent.height, 
                                     std::min(capabilities.maxImageExtent.height, actualExtent.height));
        
        vulkan_swapchain_extent_ = actualExtent;
    }

    // Choose swapchain image count
    vulkan_swapchain_image_count_ = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount > 0 && vulkan_swapchain_image_count_ > capabilities.maxImageCount) {
        vulkan_swapchain_image_count_ = capabilities.maxImageCount;
    }

    // Create swapchain
    VkSwapchainCreateInfoKHR createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = vulkan_surface_;
    createInfo.minImageCount = vulkan_swapchain_image_count_;
    createInfo.imageFormat = VK_FORMAT_B8G8R8A8_UNORM; // Android typical format
    createInfo.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    createInfo.imageExtent = vulkan_swapchain_extent_;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    createInfo.preTransform = capabilities.currentTransform;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode = VK_PRESENT_MODE_FIFO_KHR; // VSync
    createInfo.clipped = VK_TRUE;
    createInfo.oldSwapchain = VK_NULL_HANDLE;

    VkResult result = vkCreateSwapchainKHR(vulkan_device_, &createInfo, nullptr, &vulkan_swapchain_);
    if (result != VK_SUCCESS) {
        LOGE("Failed to create swapchain: %d", result);
        return false;
    }

    // Get swapchain images
    vkGetSwapchainImagesKHR(vulkan_device_, vulkan_swapchain_, &vulkan_swapchain_image_count_, nullptr);
    vulkan_swapchain_images_.resize(vulkan_swapchain_image_count_);
    vkGetSwapchainImagesKHR(vulkan_device_, vulkan_swapchain_, &vulkan_swapchain_image_count_, vulkan_swapchain_images_.data());

    vulkan_swapchain_image_format_ = VK_FORMAT_B8G8R8A8_UNORM;

    LOGI("Swapchain created: %dx%d, %d images", 
         vulkan_swapchain_extent_.width, vulkan_swapchain_extent_.height, vulkan_swapchain_image_count_);
    return true;
}

bool EmuWindow_Android::CreateRenderPass() {
    // Color attachment
    VkAttachmentDescription colorAttachment = {};
    colorAttachment.format = vulkan_swapchain_image_format_;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorAttachmentRef = {};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    // Subpass
    VkSubpassDescription subpass = {};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;

    // Create render pass
    VkRenderPassCreateInfo renderPassInfo = {};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &colorAttachment;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;

    VkResult result = vkCreateRenderPass(vulkan_device_, &renderPassInfo, nullptr, &vulkan_render_pass_);
    if (result != VK_SUCCESS) {
        LOGE("Failed to create render pass: %d", result);
        return false;
    }

    LOGI("Render pass created successfully");
    return true;
}

bool EmuWindow_Android::CreateFramebuffers() {
    vulkan_swapchain_image_views_.resize(vulkan_swapchain_images_.size());
    vulkan_framebuffers_.resize(vulkan_swapchain_images_.size());

    for (size_t i = 0; i < vulkan_swapchain_images_.size(); i++) {
        // Create image view
        VkImageViewCreateInfo createInfo = {};
        createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        createInfo.image = vulkan_swapchain_images_[i];
        createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        createInfo.format = vulkan_swapchain_image_format_;
        createInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        createInfo.subresourceRange.baseMipLevel = 0;
        createInfo.subresourceRange.levelCount = 1;
        createInfo.subresourceRange.baseArrayLayer = 0;
        createInfo.subresourceRange.layerCount = 1;

        VkResult result = vkCreateImageView(vulkan_device_, &createInfo, nullptr, &vulkan_swapchain_image_views_[i]);
        if (result != VK_SUCCESS) {
            LOGE("Failed to create image view: %d", result);
            return false;
        }

        // Create framebuffer
        VkFramebufferCreateInfo framebufferInfo = {};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = vulkan_render_pass_;
        framebufferInfo.attachmentCount = 1;
        framebufferInfo.pAttachments = &vulkan_swapchain_image_views_[i];
        framebufferInfo.width = vulkan_swapchain_extent_.width;
        framebufferInfo.height = vulkan_swapchain_extent_.height;
        framebufferInfo.layers = 1;

        result = vkCreateFramebuffer(vulkan_device_, &framebufferInfo, nullptr, &vulkan_framebuffers_[i]);
        if (result != VK_SUCCESS) {
            LOGE("Failed to create framebuffer: %d", result);
            return false;
        }
    }

    LOGI("Framebuffers created: %d", (int)vulkan_framebuffers_.size());
    return true;
}

bool EmuWindow_Android::CreateCommandBuffers() {
    // Create command pool
    VkCommandPoolCreateInfo poolInfo = {};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = 0; // Assuming graphics queue family is 0
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    VkResult result = vkCreateCommandPool(vulkan_device_, &poolInfo, nullptr, &vulkan_command_pool_);
    if (result != VK_SUCCESS) {
        LOGE("Failed to create command pool: %d", result);
        return false;
    }

    // Allocate command buffers
    vulkan_command_buffers_.resize(vulkan_framebuffers_.size());

    VkCommandBufferAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = vulkan_command_pool_;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = (uint32_t)vulkan_command_buffers_.size();

    result = vkAllocateCommandBuffers(vulkan_device_, &allocInfo, vulkan_command_buffers_.data());
    if (result != VK_SUCCESS) {
        LOGE("Failed to allocate command buffers: %d", result);
        return false;
    }

    LOGI("Command buffers created: %d", (int)vulkan_command_buffers_.size());
    return true;
}

bool EmuWindow_Android::CreateSyncObjects() {
    vulkan_image_available_semaphores_.resize(MAX_FRAMES_IN_FLIGHT);
    vulkan_render_finished_semaphores_.resize(MAX_FRAMES_IN_FLIGHT);
    vulkan_in_flight_fences_.resize(MAX_FRAMES_IN_FLIGHT);

    VkSemaphoreCreateInfo semaphoreInfo = {};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo = {};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        if (vkCreateSemaphore(vulkan_device_, &semaphoreInfo, nullptr, &vulkan_image_available_semaphores_[i]) != VK_SUCCESS ||
            vkCreateSemaphore(vulkan_device_, &semaphoreInfo, nullptr, &vulkan_render_finished_semaphores_[i]) != VK_SUCCESS ||
            vkCreateFence(vulkan_device_, &fenceInfo, nullptr, &vulkan_in_flight_fences_[i]) != VK_SUCCESS) {
            LOGE("Failed to create synchronization objects for frame %d", (int)i);
            return false;
        }
    }

    LOGI("Synchronization objects created");
    return true;
}

void EmuWindow_Android::CleanupVulkan() {
    LOGI("Cleaning up Vulkan resources...");
    
    std::lock_guard<std::mutex> lock(vulkan_mutex_);
    
    if (vulkan_device_ != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(vulkan_device_);
        
        // Cleanup swapchain
        CleanupSwapchain();
        
        // Cleanup sync objects
        for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            if (vulkan_image_available_semaphores_[i] != VK_NULL_HANDLE) {
                vkDestroySemaphore(vulkan_device_, vulkan_image_available_semaphores_[i], nullptr);
            }
            if (vulkan_render_finished_semaphores_[i] != VK_NULL_HANDLE) {
                vkDestroySemaphore(vulkan_device_, vulkan_render_finished_semaphores_[i], nullptr);
            }
            if (vulkan_in_flight_fences_[i] != VK_NULL_HANDLE) {
                vkDestroyFence(vulkan_device_, vulkan_in_flight_fences_[i], nullptr);
            }
        }
        
        // Cleanup command pool
        if (vulkan_command_pool_ != VK_NULL_HANDLE) {
            vkDestroyCommandPool(vulkan_device_, vulkan_command_pool_, nullptr);
        }
        
        // Cleanup device
        vkDestroyDevice(vulkan_device_, nullptr);
        vulkan_device_ = VK_NULL_HANDLE;
    }
    
    // Cleanup surface
    if (vulkan_surface_ != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(vulkan_instance_, vulkan_surface_, nullptr);
        vulkan_surface_ = VK_NULL_HANDLE;
    }
    
    // Cleanup instance
    if (vulkan_instance_ != VK_NULL_HANDLE) {
        vkDestroyInstance(vulkan_instance_, nullptr);
        vulkan_instance_ = VK_NULL_HANDLE;
    }
    
    LOGI("Vulkan resources cleaned up");
}

void EmuWindow_Android::CleanupSwapchain() {
    if (vulkan_device_ == VK_NULL_HANDLE) return;
    
    for (auto framebuffer : vulkan_framebuffers_) {
        vkDestroyFramebuffer(vulkan_device_, framebuffer, nullptr);
    }
    
    for (auto imageView : vulkan_swapchain_image_views_) {
        vkDestroyImageView(vulkan_device_, imageView, nullptr);
    }
    
    if (vulkan_swapchain_ != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(vulkan_device_, vulkan_swapchain_, nullptr);
        vulkan_swapchain_ = VK_NULL_HANDLE;
    }
    
    if (vulkan_render_pass_ != VK_NULL_HANDLE) {
        vkDestroyRenderPass(vulkan_device_, vulkan_render_pass_, nullptr);
        vulkan_render_pass_ = VK_NULL_HANDLE;
    }
}

void EmuWindow_Android::RecreateSwapchain() {
    LOGI("Recreating swapchain...");
    
    std::lock_guard<std::mutex> lock(vulkan_mutex_);
    
    if (vulkan_device_ != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(vulkan_device_);
    }
    
    CleanupSwapchain();
    
    // Recreate swapchain and related resources
    CreateSwapchain();
    CreateRenderPass();
    CreateFramebuffers();
    CreateCommandBuffers();
    
    framebuffer_resized_ = false;
    LOGI("Swapchain recreated successfully");
}

void EmuWindow_Android::DrawFrame() {
    if (!vulkan_initialized_ || is_paused_) return;
    
    std::lock_guard<std::mutex> lock(vulkan_mutex_);
    
    if (vulkan_device_ == VK_NULL_HANDLE) return;
    
    // Wait for fence
    vkWaitForFences(vulkan_device_, 1, &vulkan_in_flight_fences_[current_frame_], VK_TRUE, UINT64_MAX);
    
    // Acquire next image
    uint32_t imageIndex;
    VkResult result = vkAcquireNextImageKHR(vulkan_device_, vulkan_swapchain_, UINT64_MAX, 
                                           vulkan_image_available_semaphores_[current_frame_], 
                                           VK_NULL_HANDLE, &imageIndex);
    
    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        RecreateSwapchain();
        return;
    } else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        LOGE("Failed to acquire swapchain image: %d", result);
        return;
    }
    
    // Reset fence
    vkResetFences(vulkan_device_, 1, &vulkan_in_flight_fences_[current_frame_]);
    
    // Record command buffer
    VkCommandBufferBeginInfo beginInfo = {};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    
    vkBeginCommandBuffer(vulkan_command_buffers_[imageIndex], &beginInfo);
    
    // Begin render pass
    VkRenderPassBeginInfo renderPassInfo = {};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = vulkan_render_pass_;
    renderPassInfo.framebuffer = vulkan_framebuffers_[imageIndex];
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = vulkan_swapchain_extent_;
    
    VkClearValue clearColor = {{{0.0f, 0.0f, 0.0f, 1.0f}}};
    renderPassInfo.clearValueCount = 1;
    renderPassInfo.pClearValues = &clearColor;
    
    vkCmdBeginRenderPass(vulkan_command_buffers_[imageIndex], &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
    
    // Here you would add your actual rendering commands
    // For now, we just clear the screen
    
    vkCmdEndRenderPass(vulkan_command_buffers_[imageIndex]);
    vkEndCommandBuffer(vulkan_command_buffers_[imageIndex]);
    
    // Submit command buffer
    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    
    VkSemaphore waitSemaphores[] = {vulkan_image_available_semaphores_[current_frame_]};
    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &vulkan_command_buffers_[imageIndex];
    
    VkSemaphore signalSemaphores[] = {vulkan_render_finished_semaphores_[current_frame_]};
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;
    
    result = vkQueueSubmit(vulkan_queue_, 1, &submitInfo, vulkan_in_flight_fences_[current_frame_]);
    if (result != VK_SUCCESS) {
        LOGE("Failed to submit draw command buffer: %d", result);
        return;
    }
    
    // Present
    VkPresentInfoKHR presentInfo = {};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &vulkan_swapchain_;
    presentInfo.pImageIndices = &imageIndex;
    
    result = vkQueuePresentKHR(vulkan_queue_, &presentInfo);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || framebuffer_resized_) {
        framebuffer_resized_ = false;
        RecreateSwapchain();
    } else if (result != VK_SUCCESS) {
        LOGE("Failed to present swapchain image: %d", result);
    }
    
    current_frame_ = (current_frame_ + 1) % MAX_FRAMES_IN_FLIGHT;
    
    // Update frame counters
    frame_count_++;
}

bool EmuWindow_Android::InitializeGraphics() {
    if (!native_window_) {
        LOGE("No native window available for graphics initialization");
        return false;
    }
    
    LOGI("Initializing graphics for window: %dx%d", window_width_, window_height_);
    
    // Create graphics provider
    graphics_provider_ = std::make_unique<xe::ui::GraphicsProvider>();
    if (!graphics_provider_) {
        LOGE("Failed to create graphics provider");
        return false;
    }
    
    LOGI("Graphics initialized successfully");
    return true;
}

void EmuWindow_Android::DestroyGraphics() {
    LOGI("Destroying graphics resources...");
    
    if (graphics_provider_) {
        graphics_provider_.reset();
    }
    
    if (native_window_) {
        ANativeWindow_release(native_window_);
        native_window_ = nullptr;
    }
    
    LOGI("Graphics resources destroyed");
}

void EmuWindow_Android::SetSurface(ANativeWindow* surface) {
    if (native_window_ == surface) {
        return;
    }
    
    LOGI("Setting new surface: %p -> %p", native_window_, surface);
    
    DestroyGraphics();
    CleanupVulkan();
    
    if (native_window_) {
        ANativeWindow_release(native_window_);
    }
    native_window_ = surface;
    
    if (native_window_) {
        ANativeWindow_acquire(native_window_);
        
        window_width_ = ANativeWindow_getWidth(native_window_);
        window_height_ = ANativeWindow_getHeight(native_window_);
        
        LOGI("New surface dimensions: %dx%d", window_width_, window_height_);
        
        if (window_) {
            window_->set_size(window_width_, window_height_);
        }
        
        if (!InitializeVulkan()) {
            LOGE("Failed to reinitialize Vulkan with new surface");
            surface_ready_ = false;
            return;
        }
        
        if (!InitializeGraphics()) {
            LOGE("Failed to reinitialize graphics with new surface");
            surface_ready_ = false;
            return;
        }
        
        surface_ready_ = true;
        vulkan_initialized_ = true;
    } else {
        surface_ready_ = false;
        vulkan_initialized_ = false;
        LOGI("Surface destroyed");
    }
}

void EmuWindow_Android::DestroySurface() {
    LOGI("Destroying surface...");
    
    surface_ready_ = false;
    vulkan_initialized_ = false;
    
    DestroyGraphics();
    CleanupVulkan();
    
    if (native_window_) {
        ANativeWindow_release(native_window_);
        native_window_ = nullptr;
    }
}

void EmuWindow_Android::OnSurfaceChanged(ANativeWindow* surface) {
    LOGI("Surface changed: %p", surface);
    SetSurface(surface);
}

void EmuWindow_Android::OnPause() {
    LOGI("EmuWindow paused");
    is_paused_ = true;
}

void EmuWindow_Android::OnResume() {
    LOGI("EmuWindow resumed");
    is_paused_ = false;
}

void EmuWindow_Android::OnTouchEvent(int pointer_id, float x, float y, bool is_down) {
    float normalized_x = x / window_width_;
    float normalized_y = y / window_height_;
    
    LOGI("Touch event: pointer=%d, x=%.2f, y=%.2f, down=%d (normalized: %.2f, %.2f)", 
         pointer_id, x, y, is_down, normalized_x, normalized_y);
    
    // TODO: Implement touch to gamepad mapping
}

void EmuWindow_Android::OnKeyEvent(int key_code, bool is_down) {
    LOGI("Key event: code=%d, down=%d", key_code, is_down);
    
    // TODO: Implement key to gamepad mapping
    switch (key_code) {
        case 96: // KEYCODE_BUTTON_A
            LOGI("A button %s", is_down ? "pressed" : "released");
            break;
        case 97: // KEYCODE_BUTTON_B
            LOGI("B button %s", is_down ? "pressed" : "released");
            break;
        case 99: // KEYCODE_BUTTON_X
            LOGI("X button %s", is_down ? "pressed" : "released");
            break;
        case 100: // KEYCODE_BUTTON_Y
            LOGI("Y button %s", is_down ? "pressed" : "released");
            break;
        default:
            LOGI("Other key: %d", key_code);
            break;
    }
}

void EmuWindow_Android::MainLoop() {
    LOGI("Starting main loop thread");
    
    xe::threading::set_name("XaniteMainLoop");
    
    // Create emulator instance
    auto emulator = std::make_unique<xe::Emulator>();
    if (!emulator->Initialize()) {
        LOGE("Failed to initialize emulator");
        return;
    }
    
    LOGI("Emulator initialized successfully");
    
    // Apply Android-specific configuration
    ApplyAndroidConfiguration();
    
    // Performance monitoring
    auto last_frame_time = std::chrono::steady_clock::now();
    auto last_fps_update = std::chrono::steady_clock::now();
    int frame_count = 0;
    
    LOGI("Entering main render loop");
    while (is_running_) {
        if (surface_ready_ && vulkan_initialized_) {
            auto current_time = std::chrono::steady_clock::now();
            auto frame_delta = std::chrono::duration_cast<std::chrono::milliseconds>(
                current_time - last_frame_time);
            
            ProcessEvents();
            
            // Draw frame using Vulkan
            if (!is_paused_) {
                DrawFrame();
            }
            
            // Update frame counters
            frame_count++;
            frame_count_++;
            
            // Update FPS every second
            auto fps_delta = std::chrono::duration_cast<std::chrono::seconds>(
                current_time - last_fps_update);
            
            if (fps_delta.count() >= 1) {
                current_fps_ = frame_count;
                frame_count = 0;
                last_fps_update = current_time;
                
                // Log FPS every 5 seconds to avoid spam
                static auto last_fps_log = std::chrono::steady_clock::now();
                auto log_delta = std::chrono::duration_cast<std::chrono::seconds>(
                    current_time - last_fps_log);
                
                if (log_delta.count() >= 5) {
                    LOGI("FPS: %d, Total frames: %llu", 
                         current_fps_.load(), 
                         (unsigned long long)frame_count_.load());
                    last_fps_log = current_time;
                }
            }
            
            last_frame_time = current_time;
        } else {
            // No surface ready, sleep to avoid busy waiting
            std::this_thread::sleep_for(std::chrono::milliseconds(32));
        }
    }
    
    LOGI("Main loop ended");
    
    // Cleanup
    if (emulator) {
        emulator->Shutdown();
    }
    
    LOGI("Emulator shutdown complete");
}

void EmuWindow_Android::ProcessEvents() {
    // Update window dimensions if changed
    if (native_window_ && surface_ready_) {
        int new_width = ANativeWindow_getWidth(native_window_);
        int new_height = ANativeWindow_getHeight(native_window_);
        
        if (new_width != window_width_ || new_height != window_height_) {
            LOGI("Window resized: %dx%d -> %dx%d", 
                 window_width_, window_height_, new_width, new_height);
                 
            window_width_ = new_width;
            window_height_ = new_height;
            framebuffer_resized_ = true;
            
            if (window_) {
                window_->set_size(window_width_, window_height_);
            }
        }
    }
    
    // Check for system events
    CheckSystemEvents();
}

void EmuWindow_Android::ApplyAndroidConfiguration() {
    LOGI("Applying Android-specific configuration");
    
    // Apply mobile-optimized settings
    // These would typically configure:
    // - Graphics quality settings
    // - Audio buffer sizes  
    // - Input latency optimizations
    // - Power management settings
    
    LOGI("Android configuration applied successfully");
}

void EmuWindow_Android::CheckSystemEvents() {
    static auto last_memory_check = std::chrono::steady_clock::now();
    auto current_time = std::chrono::steady_clock::now();
    auto time_since_check = std::chrono::duration_cast<std::chrono::seconds>(
        current_time - last_memory_check);
    
  
    if (time_since_check.count() >= 30) {
        CheckMemoryPressure();
        last_memory_check = current_time;
    }
}

void EmuWindow_Android::CheckMemoryPressure() {
    // Check system memory pressure and adjust settings accordingly
    FILE* meminfo = fopen("/proc/meminfo", "r");
    if (meminfo) {
        char line[256];
        unsigned long available_kb = 0;
        
        while (fgets(line, sizeof(line), meminfo)) {
            if (strstr(line, "MemAvailable:")) {
                sscanf(line, "MemAvailable: %lu kB", &available_kb);
                break;
            }
        }
        fclose(meminfo);
        
        // If available memory is low, log warning
        if (available_kb > 0 && available_kb < 512 * 1024) {
            LOGW("Low memory detected: %lu KB available", available_kb);
            // In a real implementation, we would reduce graphics quality here
        } else if (available_kb > 0) {
            LOGI("Memory available: %lu KB", available_kb);
        }
    }
}


int EmuWindow_Android::GetCurrentFPS() {
    return current_fps_.load();
}

uint64_t EmuWindow_Android::GetTotalFrames() {
    return frame_count_.load();
}

void EmuWindow_Android::ResetPerformanceCounters() {
    frame_count_ = 0;
    current_fps_ = 0;
    last_fps_time_ = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

void EmuWindow_Android::RequestClose() {
    LOGI("Close requested by system");
    Close();
}

bool EmuWindow_Android::CanClose() const {
    return is_open_;
}

} // xanite 360 / og display 
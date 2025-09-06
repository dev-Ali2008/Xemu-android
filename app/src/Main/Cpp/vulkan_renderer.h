#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <memory>
#include <string>
#include <functional>
#include <mutex>
#include <atomic>
#include <array>
#include <thread>

#if __cplusplus >= 201703L
    #include <optional>
#else

    #include <experimental/optional>
    namespace std {
        template<typename T>
        using optional = std::experimental::optional<T>;
    }
#endif

#define VK_USE_PLATFORM_ANDROID_KHR
#include <vulkan/vulkan_android.h>

class XboxMemory;

class VulkanRenderer {
public:
    static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;
    static constexpr uint32_t FB_WIDTH = 1280;
    static constexpr uint32_t FB_HEIGHT = 720;
    static constexpr uint32_t FB_SIZE = FB_WIDTH * FB_HEIGHT;

    struct Vertex {
        float pos[3];
        float texCoord[2];
        float color[4];

        static VkVertexInputBindingDescription getBindingDescription();
        static std::array<VkVertexInputAttributeDescription, 3> getAttributeDescriptions();
    };

    struct UniformBufferObject {
        alignas(16) float model[16];
        alignas(16) float view[16];
        alignas(16) float proj[16];
    };

    enum class RendererState {
        Uninitialized,
        Initialized,
        Error,
        Rendering
    };

    VulkanRenderer(XboxMemory* memory);
    ~VulkanRenderer();


    bool initialize();
    void cleanup();
    bool createInstance();
    bool setupDebugMessenger();
    bool pickPhysicalDevice();
    bool createLogicalDevice();
    bool createSurface();
    bool createSwapChain();
    bool createImageViews();
    bool createRenderPass();
    bool createDescriptorSetLayout();
    bool createGraphicsPipeline();
    bool createFramebuffers();
    bool createCommandPool();
    bool createTextureImage();
    bool createTextureImageView();
    bool createTextureSampler();
    bool createVertexBuffer();
    bool createIndexBuffer();
    bool createUniformBuffers();
    bool createDescriptorPool();
    bool createDescriptorSets();
    bool createCommandBuffers();
    bool createSyncObjects();


    void drawFrame();
    void renderFrame();
    void updateUniformBuffer(uint32_t currentImage);
    void recordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex);


    bool isDeviceSuitable(VkPhysicalDevice device);
    bool checkDeviceExtensionSupport(VkPhysicalDevice device);
    bool checkValidationLayerSupport();
    VkSurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats);
    VkPresentModeKHR chooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes);
    VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities);
    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const;
    VkFormat findSupportedFormat(const std::vector<VkFormat>& candidates, VkImageTiling tiling, VkFormatFeatureFlags features) const;
    VkFormat findDepthFormat() const;
    bool hasStencilComponent(VkFormat format) const;
    void createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& bufferMemory);
    void copyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size);
    void createImage(uint32_t width, uint32_t height, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags properties, VkImage& image, VkDeviceMemory& imageMemory);
    VkImageView createImageView(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags);
    void transitionImageLayout(VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout);
    void copyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height);


    VkCommandBuffer beginSingleTimeCommands() const;
    void endSingleTimeCommands(VkCommandBuffer commandBuffer) const;


    const uint32_t* getFramebuffer() const;
    bool hasAudioOutput() const;
    const uint32_t* getAudioBuffer() const;
    bool checkInterrupt() const;
    void enableVSync(bool enabled);
    void setOutputResolution(uint32_t width, uint32_t height);
    void enableDepthTest(bool enable);
    void enableAlphaBlending(bool enable);
    void setClipRect(int left, int top, int right, int bottom);


    void enableTurboMode(bool enabled);
    void setFrameLimit(bool enabled);
    void setJITEnabled(bool enabled);
    void flushCommandBuffers();
    void optimizeForMobile();


    RendererState getState() const { return state; }
    bool isInitialized() const { return state == RendererState::Initialized; }
    bool isRunning() const { return !shouldStop && state != RendererState::Error; }

    void stop() {
        shouldStop = true;
        if (renderThread.joinable()) {
            renderThread.join();
        }
    }


    std::vector<char> readFile(const std::string& filename);
    VkShaderModule createShaderModule(const std::vector<char>& code);


    static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity, VkDebugUtilsMessageTypeFlagsEXT messageType, const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData, void* pUserData);

private:
    XboxMemory* memory;
    RendererState state;


    VkInstance instance;
    VkDebugUtilsMessengerEXT debugMessenger;
    VkPhysicalDevice physicalDevice;
    VkDevice device;
    VkQueue graphicsQueue;
    VkQueue presentQueue;
    VkSurfaceKHR surface;


    VkSwapchainKHR swapChain;
    std::vector<VkImage> swapChainImages;
    VkFormat swapChainImageFormat;
    VkExtent2D swapChainExtent;
    std::vector<VkImageView> swapChainImageViews;
    std::vector<VkFramebuffer> swapChainFramebuffers;


    VkRenderPass renderPass;
    VkDescriptorSetLayout descriptorSetLayout;
    VkPipelineLayout pipelineLayout;
    VkPipeline graphicsPipeline;


    VkCommandPool commandPool;
    std::vector<VkCommandBuffer> commandBuffers;


    std::vector<VkSemaphore> imageAvailableSemaphores;
    std::vector<VkSemaphore> renderFinishedSemaphores;
    std::vector<VkFence> inFlightFences;
    size_t currentFrame;
    uint32_t currentImageIndex;


    VkBuffer vertexBuffer;
    VkDeviceMemory vertexBufferMemory;
    VkBuffer indexBuffer;
    VkDeviceMemory indexBufferMemory;


    std::vector<VkBuffer> uniformBuffers;
    std::vector<VkDeviceMemory> uniformBuffersMemory;
    std::vector<void*> uniformBuffersMapped;


    VkDescriptorPool descriptorPool;
    std::vector<VkDescriptorSet> descriptorSets;


    VkImage textureImage;
    VkDeviceMemory textureImageMemory;
    VkImageView textureImageView;
    VkSampler textureSampler;


    VkImage depthImage;
    VkDeviceMemory depthImageMemory;
    VkImageView depthImageView;


    std::atomic<bool> shouldStop{false};
    std::thread renderThread;
    bool vsyncEnabled = true;
    bool depthTestEnabled = true;
    bool alphaBlendingEnabled = true;
    bool turboModeEnabled = false;
    bool frameLimitEnabled = true;
    bool jitEnabled = false;
    struct {
        int left, top, right, bottom;
    } clipRect = {0, 0, FB_WIDTH, FB_HEIGHT};


    const std::vector<const char*> validationLayers = {
        "VK_LAYER_KHRONOS_validation"
    };


    const std::vector<const char*> deviceExtensions = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME
    };


    const std::vector<const char*> debugExtensions = {
        VK_EXT_DEBUG_UTILS_EXTENSION_NAME
    };


    struct QueueFamilyIndices {
        std::optional<uint32_t> graphicsFamily;
        std::optional<uint32_t> presentFamily;

        bool isComplete() {
            return graphicsFamily.has_value() && presentFamily.has_value();
        }
    } queueFamilyIndices;


    struct SwapChainSupportDetails {
        VkSurfaceCapabilitiesKHR capabilities;
        std::vector<VkSurfaceFormatKHR> formats;
        std::vector<VkPresentModeKHR> presentModes;
    };

    QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device);
    SwapChainSupportDetails querySwapChainSupport(VkPhysicalDevice device);
    void populateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& createInfo);
    VkResult CreateDebugUtilsMessengerEXT(VkInstance instance, const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDebugUtilsMessengerEXT* pDebugMessenger);
    void DestroyDebugUtilsMessengerEXT(VkInstance instance, VkDebugUtilsMessengerEXT debugMessenger, const VkAllocationCallbacks* pAllocator);
    void recreateSwapChain();
    void cleanupSwapChain();
};

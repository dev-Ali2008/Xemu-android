#include "emu_window.h"
#include <android/log.h>
#include <chrono>
#include <thread>
#include <atomic>
// 统一界面显示  ( vulkan 1.1 )
#include "xenia/base/logging.h"
#include "xenia/base/threading.h"
#include "xenia/ui/vulkan/vulkan_provider.h"
#include "xenia/ui/vulkan/vulkan_util.h"
#include "xenia/apu/apu_flags.h"
#include "xenia/apu/nop/nop_audio_system.h"
#include "xenia/gpu/gpu_flags.h"
#include "xenia/gpu/vulkan/vulkan_graphics_system.h"
#include "xenia/hid/hid_flags.h"
#include "xenia/hid/nop/nop_hid.h"
#include "xenia/hid/input_system.h"
#include "xenia/config.h"
#include "xenia/vfs/vfs_flags.h"
#include "xenia/emulator.h"

#define LOG_TAG "XaniteEmuWindow"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)

namespace xanite {

static std::atomic<uint64_t> g_frame_count{0};
static std::atomic<uint64_t> g_last_fps_time{0};
static std::atomic<int> g_current_fps{0};

EmuWindow_Android::EmuWindow_Android(ANativeWindow* surface) 
    : xe::ui::Window(nullptr, L"Xanite"), native_window_(surface) {
       
    if (native_window_) {
        window_width_ = ANativeWindow_getWidth(native_window_);
        window_height_ = ANativeWindow_getHeight(native_window_);
        LOGI("Window created with dimensions: %dx%d", window_width_, window_height_);
    } else {
        window_width_ = 1280;
        window_height_ = 720;
        LOGW("No native window provided, using default dimensions");
    }
    
    set_size(window_width_, window_height_);
    
    
    g_last_fps_time.store(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

EmuWindow_Android::~EmuWindow_Android() {
    Close();
}

bool EmuWindow_Android::Initialize() {
    LOGI("Initializing EmuWindow_Android with Vulkan only");
    
    if (!native_window_) {
        LOGE("No native window available for initialization");
        return false;
    }
        
    if (!InitializeVulkan()) {
        LOGE("Failed to initialize Vulkan");
        return false;
    }
    
    is_open_ = true;
    surface_ready_ = true;
        
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
    
    if (main_loop_thread_.joinable()) {
        LOGI("Waiting for main loop thread to finish...");
        main_loop_thread_.join();
        LOGI("Main loop thread finished");
    }
       
    if (emulator_) {
        LOGI("Shutting down emulator...");
        emulator_->Shutdown();
        emulator_.reset();
    }
        
    DestroyVulkan();
    
    LOGI("EmuWindow_Android closed successfully");
}

bool EmuWindow_Android::InitializeVulkan() {
    if (!native_window_) {
        LOGE("No native window available for Vulkan initialization");
        return false;
    }
    
    LOGI("Initializing Vulkan...");    
    
    LOGI("Vulkan surface ready for window: %dx%d", window_width_, window_height_);
    
    return true;
}

void EmuWindow_Android::DestroyVulkan() {
    LOGI("Destroying Vulkan resources...");
           
    if (native_window_) {
        ANativeWindow_release(native_window_);
        native_window_ = nullptr;
    }
    
    LOGI("Vulkan resources destroyed");
}

void EmuWindow_Android::SetSurface(ANativeWindow* surface) {
    if (native_window_ == surface) {
        return;
    }
    
    LOGI("Setting new surface: %p -> %p", native_window_, surface);
        
    DestroyVulkan();    
    
    if (native_window_) {
        ANativeWindow_release(native_window_);
    }
    native_window_ = surface;
    
    if (native_window_) {
        
        ANativeWindow_acquire(native_window_);
        
        window_width_ = ANativeWindow_getWidth(native_window_);
        window_height_ = ANativeWindow_getHeight(native_window_);
        set_size(window_width_, window_height_);
        
        LOGI("New surface dimensions: %dx%d", window_width_, window_height_);        
        
        if (!InitializeVulkan()) {
            LOGE("Failed to reinitialize Vulkan with new surface");
            surface_ready_ = false;
            return;
        }
        
        surface_ready_ = true;        
        
        if (graphics_system()) {
            graphics_system()->RequestFrameTrace();
        }
    } else {
        surface_ready_ = false;
        LOGI("Surface destroyed");
    }
}

void EmuWindow_Android::DestroySurface() {
    LOGI("Destroying surface...");
    
    surface_ready_ = false;
    DestroyVulkan();
    
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
    
    if (emulator_) {
        emulator_->Pause();
    }
        
    if (graphics_system()) {
        graphics_system()->Pause();
    }
}

void EmuWindow_Android::OnResume() {
    LOGI("EmuWindow resumed");
    
    if (emulator_) {
        emulator_->Resume();
    }    
    
    if (graphics_system()) {
        graphics_system()->Resume();
    }
}

void EmuWindow_Android::OnTouchEvent(int pointer_id, float x, float y, bool is_down) {
    if (!input_system()) {
        return;
    }
        
    float normalized_x = x / window_width_;
    float normalized_y = y / window_height_;
    
    LOGI("Touch event: pointer=%d, x=%.2f, y=%.2f, down=%d (normalized: %.2f, %.2f)",
         pointer_id, x, y, is_down, normalized_x, normalized_y);    
    
    auto window_point = xe::ui::WindowPoint(
        static_cast<int>(x), 
        static_cast<int>(y)
    );
    
    if (is_down) {
        OnMouseDown(window_point, xe::ui::MouseButton::kLeft);
    } else {
        OnMouseUp(window_point, xe::ui::MouseButton::kLeft);
    }
}

void EmuWindow_Android::OnKeyEvent(int key_code, bool is_down) {
    if (!input_system()) {
        return;
    }
                
    LOGI("Key event: code=%d, down=%d", key_code, is_down);
        
    switch (key_code) {
        case 96: 
            
            break;
        case 97: 
            
            break;
        case 99: 
            
            break;
        case 100: 
            
            break;
        default:
            
            break;
    }
}

std::unique_ptr<xe::ui::GraphicsProvider> EmuWindow_Android::CreateGraphicsProvider() {
    LOGI("Creating Vulkan graphics provider...");    
    
    try {
        auto provider = xe::ui::vulkan::VulkanProvider::Create(this);
        if (provider) {
            LOGI("Vulkan graphics provider created successfully");
                       
            auto vulkan_provider = static_cast<xe::ui::vulkan::VulkanProvider*>(provider.get());            
            
            vulkan_provider->SetRequiredVulkanVersion(1, 1);           
            
            std::vector<const char*> required_extensions = {
                VK_KHR_ANDROID_SURFACE_EXTENSION_NAME,
                VK_KHR_SURFACE_EXTENSION_NAME
            };
            
            vulkan_provider->SetRequiredExtensions(required_extensions);
            
            return provider;
        }
    } catch (const std::exception& e) {
        LOGE("Failed to create Vulkan provider: %s", e.what());
    }
    
    
    LOGE("Vulkan graphics provider is required but not available");
    return nullptr;
}

void EmuWindow_Android::OnClose() {
    LOGI("Window close requested");
    Close();
}

bool EmuWindow_Android::OnCreate() {
    LOGI("Window onCreate");
    return true;
}

void EmuWindow_Android::OnDestroy() {
    LOGI("Window onDestroy");
    Close();
}

void EmuWindow_Android::OnShow() {
    LOGI("Window onShow");
}

void EmuWindow_Android::OnHide() {
    LOGI("Window onHide");
}

void EmuWindow_Android::MainLoop() {
    LOGI("Starting main loop thread with Vulkan");
        
    xe::threading::set_name("XaniteMainLoop");
        
    auto storage_path = xe::to_absolute_path(L"/sdcard/xenia");
    auto content_path = storage_path / L"content";
    auto cache_path = storage_path / L"cache";
    
    LOGI("Storage path: %s", xe::path_to_utf8(storage_path).c_str());
        
    graphics_provider_ = CreateGraphicsProvider();
    if (!graphics_provider_) {
        LOGE("Failed to create Vulkan graphics provider - cannot continue");
        return;
    }   
    
    try {
        emulator_ = std::make_unique<xe::Emulator>(
            L"", 
            storage_path,
            content_path,
            cache_path
        );
        
        LOGI("Emulator instance created successfully");
    } catch (const std::exception& e) {
        LOGE("Failed to create emulator: %s", e.what());
        return;
    }
        
    auto audio_system_factory = [](xe::cpu::Processor* processor) {
        LOGI("Creating NOP audio system");
        return std::make_unique<xe::apu::nop::NopAudioSystem>(processor);
    };
    
    auto graphics_system_factory = [this]() {
        LOGI("Creating Vulkan graphics system");
        auto graphics_system = xe::gpu::vulkan::VulkanGraphicsSystem::Create(this);
        if (graphics_system) {
            
            ApplyAndroidGraphicsSettings(graphics_system.get());
        }
        return graphics_system;
    };
    
    auto input_driver_factory = [](xe::ui::Window* window) {
        LOGI("Creating input drivers");
        std::vector<std::unique_ptr<xe::hid::InputDriver>> drivers;
        drivers.push_back(std::make_unique<xe::hid::nop::NopInputDriver>(window));
        
        return drivers;
    };
        
    auto result = emulator_->Setup(
        this,                    
        nullptr,                 
        true,                    
        audio_system_factory,
        graphics_system_factory,
        input_driver_factory
    );
    
    if (XFAILED(result)) {
        LOGE("Failed to setup emulator: %08X", result);
        return;
    }
    
    LOGI("Emulator setup completed successfully with Vulkan");
        
    ApplyAndroidConfiguration();    
    
    auto last_frame_time = std::chrono::steady_clock::now();
    auto last_fps_update = std::chrono::steady_clock::now();
    int frame_count = 0;
        
    LOGI("Entering main render loop with Vulkan");
    while (is_running_) {
        if (surface_ready_ && emulator_) {
            auto current_time = std::chrono::steady_clock::now();
            auto frame_delta = std::chrono::duration_cast<std::chrono::milliseconds>(
                current_time - last_frame_time);
                        
            ProcessEvents();
                      
            frame_count++;
            g_frame_count++;
            
            auto fps_delta = std::chrono::duration_cast<std::chrono::seconds>(
                current_time - last_fps_update);
            
            if (fps_delta.count() >= 1) {
                g_current_fps = frame_count;
                frame_count = 0;
                last_fps_update = current_time;               
                
                static auto last_fps_log = std::chrono::steady_clock::now();
                auto log_delta = std::chrono::duration_cast<std::chrono::seconds>(
                    current_time - last_fps_log);
                
                if (log_delta.count() >= 5) {
                    LOGI("Vulkan FPS: %d, Frame: %llu", g_current_fps.load(), g_frame_count.load());
                    last_fps_log = current_time;
                }
            }
            
            
            if (emulator_->is_title_open() && !emulator_->is_paused()) {
                                                                
            }
            
            last_frame_time = current_time;
            
            
            if (g_current_fps > 60) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        } else {
            
            std::this_thread::sleep_for(std::chrono::milliseconds(32));
        }
    }
    
    LOGI("Main Vulkan loop ended");
    
    
    if (emulator_) {
        LOGI("Shutting down emulator...");
        emulator_->Shutdown();
        emulator_.reset();
    }
    
    if (graphics_provider_) {
        graphics_provider_.reset();
    }
}

void EmuWindow_Android::ProcessEvents() {
                
    if (native_window_ && surface_ready_) {
        int new_width = ANativeWindow_getWidth(native_window_);
        int new_height = ANativeWindow_getHeight(native_window_);
        
        if (new_width != window_width_ || new_height != window_height_) {
            LOGI("Window resized: %dx%d -> %dx%d", 
                 window_width_, window_height_, new_width, new_height);
                 
            window_width_ = new_width;
            window_height_ = new_height;
            set_size(window_width_, window_height_);
            
            
            if (graphics_system()) {
                graphics_system()->RequestFrameTrace();
            }
        }
    }
    
    
    CheckSystemEvents();
}

void EmuWindow_Android::ApplyAndroidGraphicsSettings(xe::gpu::GraphicsSystem* graphics_system) {
    if (!graphics_system) return;
    
    LOGI("Applying Android-optimized Vulkan graphics settings");
                   
    graphics_system->set_resolution_scale(0.8f);   
    
    graphics_system->set_texture_filter(xe::gpu::TextureFilter::kLinear);
        
    graphics_system->set_msaa_samples(1);    
    
    graphics_system->set_vsync(true);    
    
    graphics_system->set_anisotropic_filter(4);
        
    auto vulkan_graphics = dynamic_cast<xe::gpu::vulkan::VulkanGraphicsSystem*>(graphics_system);
    if (vulkan_graphics) {
        
        vulkan_graphics->SetPreferredGPUType(xe::gpu::vulkan::VulkanGraphicsSystem::GPUType::kIntegrated);
        vulkan_graphics->EnableBufferCaching(true);
        vulkan_graphics->SetTextureCacheSize(256); 
    }
    
    LOGI("Android Vulkan graphics settings applied: 0.8x scale, trilinear filtering, no MSAA");
}

void EmuWindow_Android::ApplyAndroidConfiguration() {
    if (!emulator_) return;
    
    LOGI("Applying Android-specific Vulkan configuration");  
    
    emulator_->set_memory_pool_size(256 * 1024 * 1024); 
    
    emulator_->set_cpu_backend(xe::CpuBackend::kAny);
        
    emulator_->set_audio_buffer_size(1024);
        
    emulator_->set_input_latency_optimized(true);    
    
    emulator_->set_power_saving_mode(true);    
    
    emulator_->set_graphics_api(xe::GraphicsAPI::kVulkan);
    
    LOGI("Android Vulkan configuration applied successfully");
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
        
        
        if (available_kb > 0 && available_kb < 512 * 1024) { 
            LOGW("Low memory detected: %lu KB available", available_kb);
            
            
            if (graphics_system()) {
                graphics_system()->set_resolution_scale(0.6f);
                graphics_system()->set_msaa_samples(1);
            }
        }
    }
}

int EmuWindow_Android::GetCurrentFPS() {
    return g_current_fps.load();
}

uint64_t EmuWindow_Android::GetTotalFrames() {
    return g_frame_count.load();
}

void EmuWindow_Android::ResetPerformanceCounters() {
    g_frame_count = 0;
    g_current_fps = 0;
    g_last_fps_time = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

void EmuWindow_Android::OnFocus() {
    LOGI("Window gained focus");
    if (emulator_ && emulator_->is_paused()) {
        emulator_->Resume();
    }
}

void EmuWindow_Android::OnBlur() {
    LOGI("Window lost focus");
    if (emulator_ && !emulator_->is_paused()) {
        emulator_->Pause();
    }
}

void EmuWindow_Android::RequestClose() {
    LOGI("Close requested by system");
    Close();
}

bool EmuWindow_Android::CanClose() const {
    return is_open_ && !emulator_->is_title_open();
}

} 

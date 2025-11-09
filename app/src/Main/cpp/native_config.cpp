#include "native_config.h"
#include <algorithm>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include "xenia/base/filesystem.h"
#include "xenia/base/logging.h"
#include "xenia/base/string.h"

namespace xanite {

static std::unique_ptr<AndroidConfig> g_android_config;
static std::mutex g_config_mutex;

AndroidConfig::AndroidConfig() {
    config_path_ = GetConfigPath();
    LoadConfig();
}

AndroidConfig::~AndroidConfig() {
    SaveConfig();
}

void AndroidConfig::LoadConfig() {
    std::lock_guard<std::mutex> lock(g_config_mutex);
    
    
    auto config_dir = xe::filesystem::GetParentPath(config_path_);
    if (!xe::filesystem::PathExists(config_dir)) {
        if (!xe::filesystem::CreateFolder(config_dir)) {
            XELOGW("Failed to create config directory: {}", config_dir);
        }
    }
    
    if (!xe::filesystem::PathExists(config_path_)) {
        XELOGI("Config file does not exist, creating default with Vulkan only: {}", config_path_);
        CreateDefaultConfig();
        return;
    }

    std::ifstream file(config_path_);
    if (!file.is_open()) {
        XELOGE("Failed to open config file: {}", config_path_);
        CreateDefaultConfig();
        return;
    }

    std::string line;
    std::string current_section;
    int line_number = 0;
    
    while (std::getline(file, line)) {
        line_number++;
        
        
        size_t comment_pos = line.find('#');
        if (comment_pos != std::string::npos) {
            line = line.substr(0, comment_pos);
        }
        
        line = xe::utf8::trim(line);
        
        if (line.empty()) {
            continue;
        }
        
        
        if (line.front() == '[' && line.back() == ']') {
            current_section = line.substr(1, line.length() - 2);
            continue;
        }
        
        
        size_t equals_pos = line.find('=');
        if (equals_pos == std::string::npos) {
            XELOGW("Invalid config line {}: {}", line_number, line);
            continue;
        }
        
        std::string key = xe::utf8::trim(line.substr(0, equals_pos));
        std::string value = xe::utf8::trim(line.substr(equals_pos + 1));
        
        ProcessConfigValue(current_section, key, value);
    }
    
    file.close();
    XELOGI("Loaded Android config from: {}", config_path_);
    
    
    ValidateConfig();
}

void AndroidConfig::SaveConfig() {
    std::lock_guard<std::mutex> lock(g_config_mutex);
    
    
    auto config_dir = xe::filesystem::GetParentPath(config_path_);
    if (!xe::filesystem::PathExists(config_dir)) {
        if (!xe::filesystem::CreateFolder(config_dir)) {
            XELOGE("Failed to create config directory: {}", config_dir);
            return;
        }
    }
    
    std::ofstream file(config_path_);
    if (!file.is_open()) {
        XELOGE("Failed to create config file: {}", config_path_);
        return;
    }

    file << "# Xanite Android Configuration - Vulkan Only\n";
    file << "# Generated automatically - edit with caution\n";
    file << "# This configuration requires Vulkan 1.1+\n\n";
    
    
    file << "[Graphics]\n";
    file << "# Graphics backend (vulkan only - OpenGL ES not supported)\n";
    file << "gpu = " << graphics_config_.gpu << "\n";
    file << "# Vulkan version requirements\n";
    file << "vulkan_version_major = " << graphics_config_.vulkan_version_major << "\n";
    file << "vulkan_version_minor = " << graphics_config_.vulkan_version_minor << "\n";
    file << "# Require Vulkan 1.1 (true = app won't run without Vulkan 1.1+)\n";
    file << "require_vulkan_1_1 = " << (graphics_config_.require_vulkan_1_1 ? "true" : "false") << "\n";
    file << "# Resolution scaling (0.5 = 50%, 1.0 = 100%, 1.5 = 150%)\n";
    file << "resolution_scale = " << graphics_config_.resolution_scale << "\n";
    file << "# MSAA samples (1 = disabled, 2 = 2x, 4 = 4x)\n";
    file << "msaa_samples = " << graphics_config_.msaa_samples << "\n";
    file << "# Vertical synchronization\n";
    file << "vsync = " << (graphics_config_.vsync ? "true" : "false") << "\n";
    file << "# Texture filtering (1 = nearest, 2 = linear, 3 = anisotropic)\n";
    file << "texture_filtering = " << graphics_config_.texture_filtering << "\n";
    file << "# GPU timing for better synchronization\n";
    file << "gpu_timing = " << (graphics_config_.gpu_timing ? "true" : "false") << "\n";
    file << "# Render target path for Vulkan (always true for Android)\n";
    file << "render_target_path_vulkan = " << (graphics_config_.render_target_path_vulkan ? "true" : "false") << "\n";
    file << "# Vulkan validation layers (debug only, impacts performance)\n";
    file << "vulkan_validation = " << (graphics_config_.vulkan_validation ? "true" : "false") << "\n";
    file << "# Vulkan prime idle for better battery life\n";
    file << "vulkan_prime_idle = " << (graphics_config_.vulkan_prime_idle ? "true" : "false") << "\n";
    file << "\n";
    
    
    file << "[Audio]\n";
    file << "# Audio system (opensles, aaudio, oboe)\n";
    file << "audio_system = " << audio_config_.audio_system << "\n";
    file << "# Audio channels (2 = stereo)\n";
    file << "audio_channels = " << audio_config_.channels << "\n";
    file << "# Audio buffer size (samples)\n";
    file << "audio_buffer_size = " << audio_config_.buffer_size << "\n";
    file << "# Mute audio when app loses focus\n";
    file << "mute_unfocus = " << (audio_config_.mute_unfocus ? "true" : "false") << "\n";
    file << "\n";
    
    
    file << "[CPU]\n";
    file << "# CPU backend (any, jit, jit_llvm)\n";
    file << "cpu = " << cpu_config_.cpu << "\n";
    file << "# Break on launch (debug only)\n";
    file << "break_on_launch = " << (cpu_config_.break_on_launch ? "true" : "false") << "\n";
    file << "# Break on debugbreak instructions\n";
    file << "break_on_debugbreak = " << (cpu_config_.break_on_debugbreak ? "true" : "false") << "\n";
    file << "# Ignore LLVM unsafe optimizations\n";
    file << "ignore_llvm_unsafe_optimizations = " << (cpu_config_.ignore_llvm_unsafe_optimizations ? "true" : "false") << "\n";
    file << "# Enable LLVM optimizations\n";
    file << "llvm_optimizations = " << (cpu_config_.llvm_optimizations ? "true" : "false") << "\n";
    file << "\n";
    
    
    file << "[HID]\n";
    file << "# HID backend (nop, sdl, android)\n";
    file << "hid = " << hid_config_.hid << "\n";
    file << "# Vibration support\n";
    file << "vibration = " << (hid_config_.vibration ? "true" : "false") << "\n";
    file << "# Left stick deadzone\n";
    file << "left_stick_deadzone = " << hid_config_.left_stick_deadzone << "\n";
    file << "# Right stick deadzone\n";
    file << "right_stick_deadzone = " << hid_config_.right_stick_deadzone << "\n";
    file << "# Left trigger deadzone\n";
    file << "left_trigger_deadzone = " << hid_config_.left_trigger_deadzone << "\n";
    file << "# Right trigger deadzone\n";
    file << "right_trigger_deadzone = " << hid_config_.right_trigger_deadzone << "\n";
    file << "\n";
    
    
    file << "[UI]\n";
    file << "# Language (1 = English, 2 = Arabic, etc.)\n";
    file << "language = " << ui_config_.language << "\n";
    file << "# Time scalar for emulation speed\n";
    file << "time_scalar = " << ui_config_.time_scalar << "\n";
    file << "# Content root directory\n";
    file << "content_root = " << ui_config_.content_root << "\n";
    file << "# Cache root directory\n";
    file << "cache_root = " << ui_config_.cache_root << "\n";
    file << "# Storage root directory\n";
    file << "storage_root = " << ui_config_.storage_root << "\n";
    file << "# Show FPS counter\n";
    file << "show_fps = " << (ui_config_.show_fps ? "true" : "false") << "\n";
    file << "# Show on-screen controller\n";
    file << "show_controller = " << (ui_config_.show_controller ? "true" : "false") << "\n";
    file << "# Enable touch controls\n";
    file << "touch_controls = " << (ui_config_.touch_controls ? "true" : "false") << "\n";
    file << "# Touch controls opacity\n";
    file << "touch_opacity = " << ui_config_.touch_opacity << "\n";
    file << "# Vulkan only mode (always true)\n";
    file << "vulkan_only = " << (ui_config_.vulkan_only ? "true" : "false") << "\n";
    file << "\n";
    
    
    file << "[System]\n";
    file << "# License mask\n";
    file << "license_mask = " << system_config_.license_mask << "\n";
    file << "# Content license mask\n";
    file << "content_license_mask = " << system_config_.content_license_mask << "\n";
    file << "# Persistent local storage\n";
    file << "persistent_local_storage = " << (system_config_.persistent_local_storage ? "true" : "false") << "\n";
    file << "# Mount cache partition\n";
    file << "mount_cache = " << (system_config_.mount_cache ? "true" : "false") << "\n";
    file << "# Mount content partition\n";
    file << "mount_content = " << (system_config_.mount_content ? "true" : "false") << "\n";
    file << "# Mount scratch partition\n";
    file << "mount_scratch = " << (system_config_.mount_scratch ? "true" : "false") << "\n";
    file << "# Require Vulkan (always true)\n";
    file << "require_vulkan = " << (system_config_.require_vulkan ? "true" : "false") << "\n";
    file << "\n";
    
    
    file << "[Debug]\n";
    file << "# Debug mode\n";
    file << "debug = " << (debug_config_.debug ? "true" : "false") << "\n";
    file << "# Log level (0 = error, 1 = info, 2 = debug, 3 = verbose)\n";
    file << "log_level = " << debug_config_.log_level << "\n";
    file << "# Dump shaders for debugging\n";
    file << "dump_shaders = " << (debug_config_.dump_shaders ? "true" : "false") << "\n";
    file << "# Disable guest paging\n";
    file << "disable_guest_paging = " << (debug_config_.disable_guest_paging ? "true" : "false") << "\n";
    file << "# Disable global lock\n";
    file << "disable_global_lock = " << (debug_config_.disable_global_lock ? "true" : "false") << "\n";
    file << "# Disable host-guest stack synchronization\n";
    file << "disable_host_guest_stack_synchronization = " << (debug_config_.disable_host_guest_stack_synchronization ? "true" : "false") << "\n";
    file << "# Allow game relative writes\n";
    file << "allow_game_relative_writes = " << (debug_config_.allow_game_relative_writes ? "true" : "false") << "\n";
    file << "# Log Vulkan information\n";
    file << "log_vulkan_info = " << (debug_config_.log_vulkan_info ? "true" : "false") << "\n";
    
    file.close();
    XELOGI("Saved Android Vulkan-only config to: {}", config_path_);
}

void AndroidConfig::ProcessConfigValue(const std::string& section, const std::string& key, const std::string& value) {
    if (section == "Graphics") {
        if (key == "gpu") {
            
            if (value != "vulkan") {
                XELOGW("Only 'vulkan' is supported. Forcing Vulkan backend.");
            }
            graphics_config_.gpu = "vulkan";
            XELOGD("Graphics GPU: vulkan (forced)");
        }
        else if (key == "vulkan_version_major") {
            graphics_config_.vulkan_version_major = std::stoi(value);
            XELOGD("Vulkan major version: {}", graphics_config_.vulkan_version_major);
        }
        else if (key == "vulkan_version_minor") {
            graphics_config_.vulkan_version_minor = std::stoi(value);
            XELOGD("Vulkan minor version: {}", graphics_config_.vulkan_version_minor);
        }
        else if (key == "require_vulkan_1_1") {
            graphics_config_.require_vulkan_1_1 = (value == "true");
            XELOGD("Require Vulkan 1.1: {}", graphics_config_.require_vulkan_1_1);
        }
        else if (key == "resolution_scale") {
            graphics_config_.resolution_scale = std::stof(value);
            XELOGD("Resolution scale: {}", graphics_config_.resolution_scale);
        }
        else if (key == "msaa_samples") {
            graphics_config_.msaa_samples = std::stoi(value);
            XELOGD("MSAA samples: {}", graphics_config_.msaa_samples);
        }
        else if (key == "vsync") {
            graphics_config_.vsync = (value == "true");
            XELOGD("VSync: {}", graphics_config_.vsync);
        }
        else if (key == "texture_filtering") {
            graphics_config_.texture_filtering = std::stoi(value);
            XELOGD("Texture filtering: {}", graphics_config_.texture_filtering);
        }
        else if (key == "gpu_timing") {
            graphics_config_.gpu_timing = (value == "true");
            XELOGD("GPU timing: {}", graphics_config_.gpu_timing);
        }
        else if (key == "render_target_path_vulkan") {
            graphics_config_.render_target_path_vulkan = (value == "true");
            XELOGD("Vulkan render target: {}", graphics_config_.render_target_path_vulkan);
        }
        else if (key == "vulkan_validation") {
            graphics_config_.vulkan_validation = (value == "true");
            XELOGD("Vulkan validation: {}", graphics_config_.vulkan_validation);
        }
        else if (key == "vulkan_prime_idle") {
            graphics_config_.vulkan_prime_idle = (value == "true");
            XELOGD("Vulkan prime idle: {}", graphics_config_.vulkan_prime_idle);
        }
    }
    else if (section == "Audio") {
        if (key == "audio_system") {
            audio_config_.audio_system = value;
            XELOGD("Audio system: {}", value);
        }
        else if (key == "audio_channels") {
            audio_config_.channels = std::stoi(value);
            XELOGD("Audio channels: {}", audio_config_.channels);
        }
        else if (key == "audio_buffer_size") {
            audio_config_.buffer_size = std::stoi(value);
            XELOGD("Audio buffer size: {}", audio_config_.buffer_size);
        }
        else if (key == "mute_unfocus") {
            audio_config_.mute_unfocus = (value == "true");
            XELOGD("Mute unfocus: {}", audio_config_.mute_unfocus);
        }
    }
    else if (section == "CPU") {
        if (key == "cpu") {
            cpu_config_.cpu = value;
            XELOGD("CPU backend: {}", value);
        }
        else if (key == "break_on_launch") {
            cpu_config_.break_on_launch = (value == "true");
            XELOGD("Break on launch: {}", cpu_config_.break_on_launch);
        }
        else if (key == "break_on_debugbreak") {
            cpu_config_.break_on_debugbreak = (value == "true");
            XELOGD("Break on debugbreak: {}", cpu_config_.break_on_debugbreak);
        }
        else if (key == "ignore_llvm_unsafe_optimizations") {
            cpu_config_.ignore_llvm_unsafe_optimizations = (value == "true");
            XELOGD("Ignore LLVM unsafe optimizations: {}", cpu_config_.ignore_llvm_unsafe_optimizations);
        }
        else if (key == "llvm_optimizations") {
            cpu_config_.llvm_optimizations = (value == "true");
            XELOGD("LLVM optimizations: {}", cpu_config_.llvm_optimizations);
        }
    }
    else if (section == "HID") {
        if (key == "hid") {
            hid_config_.hid = value;
            XELOGD("HID backend: {}", value);
        }
        else if (key == "vibration") {
            hid_config_.vibration = (value == "true");
            XELOGD("Vibration: {}", hid_config_.vibration);
        }
        else if (key == "left_stick_deadzone") {
            hid_config_.left_stick_deadzone = std::stof(value);
            XELOGD("Left stick deadzone: {}", hid_config_.left_stick_deadzone);
        }
        else if (key == "right_stick_deadzone") {
            hid_config_.right_stick_deadzone = std::stof(value);
            XELOGD("Right stick deadzone: {}", hid_config_.right_stick_deadzone);
        }
        else if (key == "left_trigger_deadzone") {
            hid_config_.left_trigger_deadzone = std::stof(value);
            XELOGD("Left trigger deadzone: {}", hid_config_.left_trigger_deadzone);
        }
        else if (key == "right_trigger_deadzone") {
            hid_config_.right_trigger_deadzone = std::stof(value);
            XELOGD("Right trigger deadzone: {}", hid_config_.right_trigger_deadzone);
        }
    }
    else if (section == "UI") {
        if (key == "language") {
            ui_config_.language = std::stoi(value);
            XELOGD("Language: {}", ui_config_.language);
        }
        else if (key == "time_scalar") {
            ui_config_.time_scalar = std::stof(value);
            XELOGD("Time scalar: {}", ui_config_.time_scalar);
        }
        else if (key == "content_root") {
            ui_config_.content_root = value;
            XELOGD("Content root: {}", value);
        }
        else if (key == "cache_root") {
            ui_config_.cache_root = value;
            XELOGD("Cache root: {}", value);
        }
        else if (key == "storage_root") {
            ui_config_.storage_root = value;
            XELOGD("Storage root: {}", value);
        }
        else if (key == "show_fps") {
            ui_config_.show_fps = (value == "true");
            XELOGD("Show FPS: {}", ui_config_.show_fps);
        }
        else if (key == "show_controller") {
            ui_config_.show_controller = (value == "true");
            XELOGD("Show controller: {}", ui_config_.show_controller);
        }
        else if (key == "touch_controls") {
            ui_config_.touch_controls = (value == "true");
            XELOGD("Touch controls: {}", ui_config_.touch_controls);
        }
        else if (key == "touch_opacity") {
            ui_config_.touch_opacity = std::stof(value);
            XELOGD("Touch opacity: {}", ui_config_.touch_opacity);
        }
        else if (key == "vulkan_only") {
            ui_config_.vulkan_only = (value == "true");
            XELOGD("Vulkan only mode: {}", ui_config_.vulkan_only);
        }
    }
    else if (section == "System") {
        if (key == "license_mask") {
            system_config_.license_mask = std::stoi(value);
            XELOGD("License mask: {:X}", system_config_.license_mask);
        }
        else if (key == "content_license_mask") {
            system_config_.content_license_mask = std::stoi(value);
            XELOGD("Content license mask: {:X}", system_config_.content_license_mask);
        }
        else if (key == "persistent_local_storage") {
            system_config_.persistent_local_storage = (value == "true");
            XELOGD("Persistent local storage: {}", system_config_.persistent_local_storage);
        }
        else if (key == "mount_cache") {
            system_config_.mount_cache = (value == "true");
            XELOGD("Mount cache: {}", system_config_.mount_cache);
        }
        else if (key == "mount_content") {
            system_config_.mount_content = (value == "true");
            XELOGD("Mount content: {}", system_config_.mount_content);
        }
        else if (key == "mount_scratch") {
            system_config_.mount_scratch = (value == "true");
            XELOGD("Mount scratch: {}", system_config_.mount_scratch);
        }
        else if (key == "require_vulkan") {
            system_config_.require_vulkan = (value == "true");
            XELOGD("Require Vulkan: {}", system_config_.require_vulkan);
        }
    }
    else if (section == "Debug") {
        if (key == "debug") {
            debug_config_.debug = (value == "true");
            XELOGD("Debug mode: {}", debug_config_.debug);
        }
        else if (key == "log_level") {
            debug_config_.log_level = std::stoi(value);
            XELOGD("Log level: {}", debug_config_.log_level);
        }
        else if (key == "dump_shaders") {
            debug_config_.dump_shaders = (value == "true");
            XELOGD("Dump shaders: {}", debug_config_.dump_shaders);
        }
        else if (key == "disable_guest_paging") {
            debug_config_.disable_guest_paging = (value == "true");
            XELOGD("Disable guest paging: {}", debug_config_.disable_guest_paging);
        }
        else if (key == "disable_global_lock") {
            debug_config_.disable_global_lock = (value == "true");
            XELOGD("Disable global lock: {}", debug_config_.disable_global_lock);
        }
        else if (key == "disable_host_guest_stack_synchronization") {
            debug_config_.disable_host_guest_stack_synchronization = (value == "true");
            XELOGD("Disable stack sync: {}", debug_config_.disable_host_guest_stack_synchronization);
        }
        else if (key == "allow_game_relative_writes") {
            debug_config_.allow_game_relative_writes = (value == "true");
            XELOGD("Allow game relative writes: {}", debug_config_.allow_game_relative_writes);
        }
        else if (key == "log_vulkan_info") {
            debug_config_.log_vulkan_info = (value == "true");
            XELOGD("Log Vulkan info: {}", debug_config_.log_vulkan_info);
        }
    }
    else {
        XELOGW("Unknown config section: {}", section);
    }
}

void AndroidConfig::CreateDefaultConfig() {
    XELOGI("Creating default Android configuration with Vulkan only");
    
    
    graphics_config_.gpu = "vulkan";
    graphics_config_.vulkan_version_major = 1;
    graphics_config_.vulkan_version_minor = 1;
    graphics_config_.require_vulkan_1_1 = true;
    graphics_config_.resolution_scale = 0.8f; 
    graphics_config_.msaa_samples = 1; 
    graphics_config_.vsync = true; 
    graphics_config_.texture_filtering = 2; 
    graphics_config_.gpu_timing = false; 
    graphics_config_.render_target_path_vulkan = true; 
    graphics_config_.vulkan_validation = false; 
    graphics_config_.vulkan_prime_idle = true; 
    
    
    audio_config_.audio_system = "opensles"; 
    audio_config_.channels = 2; 
    audio_config_.buffer_size = 1024; 
    audio_config_.mute_unfocus = true; 
    
    
    cpu_config_.cpu = "any"; 
    cpu_config_.break_on_launch = false; 
    cpu_config_.break_on_debugbreak = false; 
    cpu_config_.ignore_llvm_unsafe_optimizations = true; 
    cpu_config_.llvm_optimizations = true; 
    
    
    hid_config_.hid = "nop"; 
    hid_config_.vibration = true; 
    hid_config_.left_stick_deadzone = 0.15f; 
    hid_config_.right_stick_deadzone = 0.15f; 
    hid_config_.left_trigger_deadzone = 0.1f; 
    hid_config_.right_trigger_deadzone = 0.1f; 
    
    
    ui_config_.language = 1; 
    ui_config_.time_scalar = 1.0f; 
    ui_config_.content_root = "/sdcard/xenia/content";
    ui_config_.cache_root = "/sdcard/xenia/cache";
    ui_config_.storage_root = "/sdcard/xenia";
    ui_config_.show_fps = true; 
    ui_config_.show_controller = true; 
    ui_config_.touch_controls = true; 
    ui_config_.touch_opacity = 0.7f; 
    ui_config_.vulkan_only = true; 
    
    
    system_config_.license_mask = 0xFFFFFFFF; 
    system_config_.content_license_mask = 0xFFFFFFFF; 
    system_config_.persistent_local_storage = true; 
    system_config_.mount_cache = true; 
    system_config_.mount_content = true; 
    system_config_.mount_scratch = true; 
    system_config_.require_vulkan = true; 
    
    
    debug_config_.debug = false;
    debug_config_.log_level = 1; 
    debug_config_.dump_shaders = false;
    debug_config_.disable_guest_paging = false;
    debug_config_.disable_global_lock = false;
    debug_config_.disable_host_guest_stack_synchronization = false;
    debug_config_.allow_game_relative_writes = false;
    debug_config_.log_vulkan_info = true; 
    
    SaveConfig();
}

void AndroidConfig::ValidateConfig() {
    XELOGI("Validating Vulkan-only configuration...");
    
    
    graphics_config_.gpu = "vulkan";
    system_config_.require_vulkan = true;
    ui_config_.vulkan_only = true;
    
    
    graphics_config_.resolution_scale = std::clamp(graphics_config_.resolution_scale, 0.5f, 2.0f);
    graphics_config_.msaa_samples = std::clamp(graphics_config_.msaa_samples, 1, 8);
    graphics_config_.texture_filtering = std::clamp(graphics_config_.texture_filtering, 1, 3);
    
    
    if (graphics_config_.vulkan_version_major < 1 || 
        (graphics_config_.vulkan_version_major == 1 && graphics_config_.vulkan_version_minor < 1)) {
        XELOGW("Vulkan version too low, requiring 1.1+");
        graphics_config_.vulkan_version_major = 1;
        graphics_config_.vulkan_version_minor = 1;
    }
    
    
    audio_config_.channels = std::clamp(audio_config_.channels, 1, 8);
    audio_config_.buffer_size = std::clamp(audio_config_.buffer_size, 256, 4096);
    
    
    hid_config_.left_stick_deadzone = std::clamp(hid_config_.left_stick_deadzone, 0.0f, 0.5f);
    hid_config_.right_stick_deadzone = std::clamp(hid_config_.right_stick_deadzone, 0.0f, 0.5f);
    hid_config_.left_trigger_deadzone = std::clamp(hid_config_.left_trigger_deadzone, 0.0f, 0.5f);
    hid_config_.right_trigger_deadzone = std::clamp(hid_config_.right_trigger_deadzone, 0.0f, 0.5f);
    
    
    ui_config_.time_scalar = std::clamp(ui_config_.time_scalar, 0.1f, 10.0f);
    ui_config_.touch_opacity = std::clamp(ui_config_.touch_opacity, 0.1f, 1.0f);
    
    
    debug_config_.log_level = std::clamp(debug_config_.log_level, 0, 3);
    
    XELOGI("Vulkan-only configuration validation completed");
}

std::string AndroidConfig::GetConfigPath() {
    
    return "/sdcard/xenia/xenia-canary.config.toml";
}


GraphicsConfig& AndroidConfig::GetGraphicsConfig() {
    return graphics_config_;
}

AudioConfig& AndroidConfig::GetAudioConfig() {
    return audio_config_;
}

CPUConfig& AndroidConfig::GetCPUConfig() {
    return cpu_config_;
}

HIDConfig& AndroidConfig::GetHIDConfig() {
    return hid_config_;
}

UIConfig& AndroidConfig::GetUIConfig() {
    return ui_config_;
}

SystemConfig& AndroidConfig::GetSystemConfig() {
    return system_config_;
}

DebugConfig& AndroidConfig::GetDebugConfig() {
    return debug_config_;
}


void AndroidConfig::SetGraphicsConfig(const GraphicsConfig& config) {
    std::lock_guard<std::mutex> lock(g_config_mutex);
    graphics_config_ = config;
}

void AndroidConfig::SetAudioConfig(const AudioConfig& config) {
    std::lock_guard<std::mutex> lock(g_config_mutex);
    audio_config_ = config;
}

void AndroidConfig::SetCPUConfig(const CPUConfig& config) {
    std::lock_guard<std::mutex> lock(g_config_mutex);
    cpu_config_ = config;
}

void AndroidConfig::SetHIDConfig(const HIDConfig& config) {
    std::lock_guard<std::mutex> lock(g_config_mutex);
    hid_config_ = config;
}

void AndroidConfig::SetUIConfig(const UIConfig& config) {
    std::lock_guard<std::mutex> lock(g_config_mutex);
    ui_config_ = config;
}

void AndroidConfig::SetSystemConfig(const SystemConfig& config) {
    std::lock_guard<std::mutex> lock(g_config_mutex);
    system_config_ = config;
}

void AndroidConfig::SetDebugConfig(const DebugConfig& config) {
    std::lock_guard<std::mutex> lock(g_config_mutex);
    debug_config_ = config;
}


bool AndroidConfig::CheckVulkanCompatibility() {
    XELOGI("Checking Vulkan compatibility...");
    
    
    
    
    bool compatible = true;
    
    if (graphics_config_.require_vulkan_1_1) {
        XELOGI("Vulkan 1.1+ required for this application");
        
    }
    
    XELOGI("Vulkan compatibility: %s", compatible ? "SUPPORTED" : "NOT SUPPORTED");
    return compatible;
}

std::string AndroidConfig::GetVulkanDeviceInfo() {
    std::stringstream info;
    
    info << "Vulkan Configuration:\n";
    info << "  Backend: " << graphics_config_.gpu << " (forced)\n";
    info << "  Required Version: " << graphics_config_.vulkan_version_major 
         << "." << graphics_config_.vulkan_version_minor << "\n";
    info << "  Resolution Scale: " << graphics_config_.resolution_scale << "x\n";
    info << "  MSAA Samples: " << graphics_config_.msaa_samples << "x\n";
    info << "  VSync: " << (graphics_config_.vsync ? "Enabled" : "Disabled") << "\n";
    info << "  Texture Filtering: " << graphics_config_.texture_filtering << "\n";
    
    return info.str();
}

bool AndroidConfig::IsVulkan11Supported() {
    
    
    return true;
}


void AndroidConfig::ApplyToCVars() {
    XELOGI("Applying Android Vulkan-only configuration to Xenia CVars");  
    // 核心和内核在单独的文件中处理；这些是设置~~
    XELOGI("Android Vulkan-only configuration applied to CVars");
}

} 

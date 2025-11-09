#include "android_settings.h"
#include <algorithm>
#include <fstream>
#include <sstream>
#include "xenia/base/filesystem.h"
#include "xenia/base/logging.h"
#include "xenia/base/string.h"

namespace xanite {

AndroidSettings::AndroidSettings() {
    LoadSettings();
}

AndroidSettings::~AndroidSettings() {
    SaveSettings();
}

void AndroidSettings::LoadSettings() {
    settings_path_ = GetSettingsPath();
    
    if (!xe::filesystem::PathExists(settings_path_)) {
        XELOGI("Settings file does not exist, creating default: {}", settings_path_);
        CreateDefaultSettings();
        return;
    }

    std::ifstream file(settings_path_);
    if (!file.is_open()) {
        XELOGE("Failed to open settings file: {}", settings_path_);
        return;
    }

    std::string line;
    std::string current_section;
    
    while (std::getline(file, line)) {
        
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
            continue;
        }
        
        std::string key = xe::utf8::trim(line.substr(0, equals_pos));
        std::string value = xe::utf8::trim(line.substr(equals_pos + 1));
        
        ProcessSettingsValue(current_section, key, value);
    }
    
    file.close();
    XELOGI("Loaded Android settings from: {}", settings_path_);
}

void AndroidSettings::SaveSettings() {
    std::ofstream file(settings_path_);
    if (!file.is_open()) {
        XELOGE("Failed to create settings file: {}", settings_path_);
        return;
    }

    file << "# Xanite Android Settings\n";
    file << "# UI and emulator behavior settings\n\n";
       
    file << "[Interface]\n";
    file << "theme = " << interface_settings_.theme << "\n";
    file << "language = " << interface_settings_.language << "\n";
    file << "show_fps = " << (interface_settings_.show_fps ? "true" : "false") << "\n";
    file << "show_controller = " << (interface_settings_.show_controller ? "true" : "false") << "\n";
    file << "touch_controls = " << (interface_settings_.touch_controls ? "true" : "false") << "\n";
    file << "touch_opacity = " << interface_settings_.touch_opacity << "\n";
    file << "vibration_strength = " << interface_settings_.vibration_strength << "\n";
    file << "button_size = " << interface_settings_.button_size << "\n";
    file << "analog_stick_size = " << interface_settings_.analog_stick_size << "\n";
    file << "auto_hide_controls = " << (interface_settings_.auto_hide_controls ? "true" : "false") << "\n";
    file << "hide_delay = " << interface_settings_.hide_delay << "\n";
    file << "\n";
       
    file << "[Performance]\n";
    file << "performance_mode = " << static_cast<int>(performance_settings_.performance_mode) << "\n";
    file << "frame_limit = " << performance_settings_.frame_limit << "\n";
    file << "power_saving = " << (performance_settings_.power_saving ? "true" : "false") << "\n";
    file << "thermal_throttling = " << (performance_settings_.thermal_throttling ? "true" : "false") << "\n";
    file << "background_audio = " << (performance_settings_.background_audio ? "true" : "false") << "\n";
    file << "suspend_on_focus_loss = " << (performance_settings_.suspend_on_focus_loss ? "true" : "false") << "\n";
    file << "memory_usage_limit = " << performance_settings_.memory_usage_limit << "\n";
    file << "\n";
       
    file << "[Controls]\n";
    file << "control_scheme = " << static_cast<int>(control_settings_.control_scheme) << "\n";
    file << "touch_sensitivity = " << control_settings_.touch_sensitivity << "\n";
    file << "analog_deadzone = " << control_settings_.analog_deadzone << "\n";
    file << "trigger_deadzone = " << control_settings_.trigger_deadzone << "\n";
    file << "button_layout = " << static_cast<int>(control_settings_.button_layout) << "\n";
    file << "enable_gestures = " << (control_settings_.enable_gestures ? "true" : "false") << "\n";
    file << "swipe_gestures = " << (control_settings_.swipe_gestures ? "true" : "false") << "\n";
    file << "tap_zones = " << (control_settings_.tap_zones ? "true" : "false") << "\n";
    file << "haptic_feedback = " << (control_settings_.haptic_feedback ? "true" : "false") << "\n";
    file << "\n";
       
    file << "[Graphics]\n";
    file << "resolution_scale = " << graphics_settings_.resolution_scale << "\n";
    file << "texture_filtering = " << graphics_settings_.texture_filtering << "\n";
    file << "msaa_level = " << graphics_settings_.msaa_level << "\n";
    file << "vsync = " << (graphics_settings_.vsync ? "true" : "false") << "\n";
    file << "anisotropic_filtering = " << graphics_settings_.anisotropic_filtering << "\n";
    file << "gpu_timing = " << (graphics_settings_.gpu_timing ? "true" : "false") << "\n";
    file << "render_api = " << static_cast<int>(graphics_settings_.render_api) << "\n";
    file << "post_processing = " << (graphics_settings_.post_processing ? "true" : "false") << "\n";
    file << "brightness = " << graphics_settings_.brightness << "\n";
    file << "contrast = " << graphics_settings_.contrast << "\n";
    file << "\n";
    
    file << "[Audio]\n";
    file << "audio_latency = " << audio_settings_.audio_latency << "\n";
    file << "volume = " << audio_settings_.volume << "\n";
    file << "mute_on_focus_loss = " << (audio_settings_.mute_on_focus_loss ? "true" : "false") << "\n";
    file << "audio_backend = " << static_cast<int>(audio_settings_.audio_backend) << "\n";
    file << "surround_sound = " << (audio_settings_.surround_sound ? "true" : "false") << "\n";
    file << "audio_boost = " << (audio_settings_.audio_boost ? "true" : "false") << "\n";
    file << "\n";
      
    file << "[Storage]\n";
    file << "content_directory = " << storage_settings_.content_directory << "\n";
    file << "cache_directory = " << storage_settings_.cache_directory << "\n";
    file << "save_directory = " << storage_settings_.save_directory << "\n";
    file << "screenshot_directory = " << storage_settings_.screenshot_directory << "\n";
    file << "auto_save = " << (storage_settings_.auto_save ? "true" : "false") << "\n";
    file << "save_compression = " << (storage_settings_.save_compression ? "true" : "false") << "\n";
    file << "cloud_sync = " << (storage_settings_.cloud_sync ? "true" : "false") << "\n";
    file << "\n";
      
    file << "[Network]\n";
    file << "enable_network = " << (network_settings_.enable_network ? "true" : "false") << "\n";
    file << "xbox_live = " << (network_settings_.xbox_live ? "true" : "false") << "\n";
    file << "upnp = " << (network_settings_.upnp ? "true" : "false") << "\n";
    file << "port_forwarding = " << (network_settings_.port_forwarding ? "true" : "false") << "\n";
    file << "\n";
    
    file << "[Debug]\n";
    file << "log_level = " << debug_settings_.log_level << "\n";
    file << "show_log_window = " << (debug_settings_.show_log_window ? "true" : "false") << "\n";
    file << "performance_overlay = " << (debug_settings_.performance_overlay ? "true" : "false") << "\n";
    file << "crash_reporting = " << (debug_settings_.crash_reporting ? "true" : "false") << "\n";
    file << "analytics = " << (debug_settings_.analytics ? "true" : "false") << "\n";
    file << "developer_mode = " << (debug_settings_.developer_mode ? "true" : "false") << "\n";
    
    file.close();
    XELOGI("Saved Android settings to: {}", settings_path_);
}

void AndroidSettings::ProcessSettingsValue(const std::string& section, const std::string& key, const std::string& value) {
    if (section == "Interface") {
        if (key == "theme") interface_settings_.theme = value;
        else if (key == "language") interface_settings_.language = std::stoi(value);
        else if (key == "show_fps") interface_settings_.show_fps = (value == "true");
        else if (key == "show_controller") interface_settings_.show_controller = (value == "true");
        else if (key == "touch_controls") interface_settings_.touch_controls = (value == "true");
        else if (key == "touch_opacity") interface_settings_.touch_opacity = std::stof(value);
        else if (key == "vibration_strength") interface_settings_.vibration_strength = std::stof(value);
        else if (key == "button_size") interface_settings_.button_size = std::stof(value);
        else if (key == "analog_stick_size") interface_settings_.analog_stick_size = std::stof(value);
        else if (key == "auto_hide_controls") interface_settings_.auto_hide_controls = (value == "true");
        else if (key == "hide_delay") interface_settings_.hide_delay = std::stoi(value);
    }
    else if (section == "Performance") {
        if (key == "performance_mode") performance_settings_.performance_mode = static_cast<PerformanceMode>(std::stoi(value));
        else if (key == "frame_limit") performance_settings_.frame_limit = std::stoi(value);
        else if (key == "power_saving") performance_settings_.power_saving = (value == "true");
        else if (key == "thermal_throttling") performance_settings_.thermal_throttling = (value == "true");
        else if (key == "background_audio") performance_settings_.background_audio = (value == "true");
        else if (key == "suspend_on_focus_loss") performance_settings_.suspend_on_focus_loss = (value == "true");
        else if (key == "memory_usage_limit") performance_settings_.memory_usage_limit = std::stoi(value);
    }
    else if (section == "Controls") {
        if (key == "control_scheme") control_settings_.control_scheme = static_cast<ControlScheme>(std::stoi(value));
        else if (key == "touch_sensitivity") control_settings_.touch_sensitivity = std::stof(value);
        else if (key == "analog_deadzone") control_settings_.analog_deadzone = std::stof(value);
        else if (key == "trigger_deadzone") control_settings_.trigger_deadzone = std::stof(value);
        else if (key == "button_layout") control_settings_.button_layout = static_cast<ButtonLayout>(std::stoi(value));
        else if (key == "enable_gestures") control_settings_.enable_gestures = (value == "true");
        else if (key == "swipe_gestures") control_settings_.swipe_gestures = (value == "true");
        else if (key == "tap_zones") control_settings_.tap_zones = (value == "true");
        else if (key == "haptic_feedback") control_settings_.haptic_feedback = (value == "true");
    }
    else if (section == "Graphics") {
        if (key == "resolution_scale") graphics_settings_.resolution_scale = std::stof(value);
        else if (key == "texture_filtering") graphics_settings_.texture_filtering = std::stoi(value);
        else if (key == "msaa_level") graphics_settings_.msaa_level = std::stoi(value);
        else if (key == "vsync") graphics_settings_.vsync = (value == "true");
        else if (key == "anisotropic_filtering") graphics_settings_.anisotropic_filtering = std::stoi(value);
        else if (key == "gpu_timing") graphics_settings_.gpu_timing = (value == "true");
        else if (key == "render_api") graphics_settings_.render_api = static_cast<RenderAPI>(std::stoi(value));
        else if (key == "post_processing") graphics_settings_.post_processing = (value == "true");
        else if (key == "brightness") graphics_settings_.brightness = std::stof(value);
        else if (key == "contrast") graphics_settings_.contrast = std::stof(value);
    }
    else if (section == "Audio") {
        if (key == "audio_latency") audio_settings_.audio_latency = std::stoi(value);
        else if (key == "volume") audio_settings_.volume = std::stof(value);
        else if (key == "mute_on_focus_loss") audio_settings_.mute_on_focus_loss = (value == "true");
        else if (key == "audio_backend") audio_settings_.audio_backend = static_cast<AudioBackend>(std::stoi(value));
        else if (key == "surround_sound") audio_settings_.surround_sound = (value == "true");
        else if (key == "audio_boost") audio_settings_.audio_boost = (value == "true");
    }
    else if (section == "Storage") {
        if (key == "content_directory") storage_settings_.content_directory = value;
        else if (key == "cache_directory") storage_settings_.cache_directory = value;
        else if (key == "save_directory") storage_settings_.save_directory = value;
        else if (key == "screenshot_directory") storage_settings_.screenshot_directory = value;
        else if (key == "auto_save") storage_settings_.auto_save = (value == "true");
        else if (key == "save_compression") storage_settings_.save_compression = (value == "true");
        else if (key == "cloud_sync") storage_settings_.cloud_sync = (value == "true");
    }
    else if (section == "Network") {
        if (key == "enable_network") network_settings_.enable_network = (value == "true");
        else if (key == "xbox_live") network_settings_.xbox_live = (value == "true");
        else if (key == "upnp") network_settings_.upnp = (value == "true");
        else if (key == "port_forwarding") network_settings_.port_forwarding = (value == "true");
    }
    else if (section == "Debug") {
        if (key == "log_level") debug_settings_.log_level = std::stoi(value);
        else if (key == "show_log_window") debug_settings_.show_log_window = (value == "true");
        else if (key == "performance_overlay") debug_settings_.performance_overlay = (value == "true");
        else if (key == "crash_reporting") debug_settings_.crash_reporting = (value == "true");
        else if (key == "analytics") debug_settings_.analytics = (value == "true");
        else if (key == "developer_mode") debug_settings_.developer_mode = (value == "true");
    }
}

void AndroidSettings::CreateDefaultSettings() {
    
    interface_settings_.theme = "dark";
    interface_settings_.language = 1; 
    interface_settings_.show_fps = true;
    interface_settings_.show_controller = true;
    interface_settings_.touch_controls = true;
    interface_settings_.touch_opacity = 0.7f;
    interface_settings_.vibration_strength = 0.8f;
    interface_settings_.button_size = 1.0f;
    interface_settings_.analog_stick_size = 1.0f;
    interface_settings_.auto_hide_controls = false;
    interface_settings_.hide_delay = 3000;
        
    performance_settings_.performance_mode = PerformanceMode::BALANCED;
    performance_settings_.frame_limit = 60;
    performance_settings_.power_saving = false;
    performance_settings_.thermal_throttling = true;
    performance_settings_.background_audio = false;
    performance_settings_.suspend_on_focus_loss = true;
    performance_settings_.memory_usage_limit = 75;
        
    control_settings_.control_scheme = ControlScheme::TOUCH_GAMEPAD;
    control_settings_.touch_sensitivity = 1.0f;
    control_settings_.analog_deadzone = 0.15f;
    control_settings_.trigger_deadzone = 0.1f;
    control_settings_.button_layout = ButtonLayout::STANDARD;
    control_settings_.enable_gestures = true;
    control_settings_.swipe_gestures = true;
    control_settings_.tap_zones = true;
    control_settings_.haptic_feedback = true;
    
    graphics_settings_.resolution_scale = 1.0f;
    graphics_settings_.texture_filtering = 2; 
    graphics_settings_.msaa_level = 2;
    graphics_settings_.vsync = true;
    graphics_settings_.anisotropic_filtering = 4;
    graphics_settings_.gpu_timing = false;
    graphics_settings_.render_api = RenderAPI::VULKAN;
    graphics_settings_.post_processing = false;
    graphics_settings_.brightness = 1.0f;
    graphics_settings_.contrast = 1.0f;
        
    audio_settings_.audio_latency = 128;
    audio_settings_.volume = 1.0f;
    audio_settings_.mute_on_focus_loss = true;
    audio_settings_.audio_backend = AudioBackend::OPENSLES;
    audio_settings_.surround_sound = false;
    audio_settings_.audio_boost = false;
       
    storage_settings_.content_directory = "/sdcard/xenia/content";
    storage_settings_.cache_directory = "/sdcard/xenia/cache";
    storage_settings_.save_directory = "/sdcard/xenia/saves";
    storage_settings_.screenshot_directory = "/sdcard/xenia/screenshots";
    storage_settings_.auto_save = true;
    storage_settings_.save_compression = true;
    storage_settings_.cloud_sync = false;
        
    network_settings_.enable_network = false;
    network_settings_.xbox_live = false;
    network_settings_.upnp = false;
    network_settings_.port_forwarding = false;
        
    debug_settings_.log_level = 1; 
    debug_settings_.show_log_window = false;
    debug_settings_.performance_overlay = false;
    debug_settings_.crash_reporting = true;
    debug_settings_.analytics = false;
    debug_settings_.developer_mode = false;
    
    SaveSettings();
}

std::string AndroidSettings::GetSettingsPath() {
    return "/sdcard/xenia/settings/android_settings.ini";
}

InterfaceSettings& AndroidSettings::GetInterfaceSettings() { return interface_settings_; }
PerformanceSettings& AndroidSettings::GetPerformanceSettings() { return performance_settings_; }
ControlSettings& AndroidSettings::GetControlSettings() { return control_settings_; }
GraphicsSettings& AndroidSettings::GetGraphicsSettings() { return graphics_settings_; }
AudioSettings& AndroidSettings::GetAudioSettings() { return audio_settings_; }
StorageSettings& AndroidSettings::GetStorageSettings() { return storage_settings_; }
NetworkSettings& AndroidSettings::GetNetworkSettings() { return network_settings_; }
DebugSettings& AndroidSettings::GetDebugSettings() { return debug_settings_; }

void AndroidSettings::SetInterfaceSettings(const InterfaceSettings& settings) { interface_settings_ = settings; }
void AndroidSettings::SetPerformanceSettings(const PerformanceSettings& settings) { performance_settings_ = settings; }
void AndroidSettings::SetControlSettings(const ControlSettings& settings) { control_settings_ = settings; }
void AndroidSettings::SetGraphicsSettings(const GraphicsSettings& settings) { graphics_settings_ = settings; }
void AndroidSettings::SetAudioSettings(const AudioSettings& settings) { audio_settings_ = settings; }
void AndroidSettings::SetStorageSettings(const StorageSettings& settings) { storage_settings_ = settings; }
void AndroidSettings::SetNetworkSettings(const NetworkSettings& settings) { network_settings_ = settings; }
void AndroidSettings::SetDebugSettings(const DebugSettings& settings) { debug_settings_ = settings; }

void AndroidSettings::ResetToDefaults() {
    CreateDefaultSettings();
    LoadSettings(); 
}

bool AndroidSettings::ExportSettings(const std::string& filename) {
    std::string backup_path = "/sdcard/xenia/backups/" + filename;
    return xe::filesystem::CopyFile(settings_path_, backup_path);
}

bool AndroidSettings::ImportSettings(const std::string& filename) {
    std::string backup_path = "/sdcard/xenia/backups/" + filename;
    if (!xe::filesystem::PathExists(backup_path)) {
        return false;
    }
       
    std::string current_backup = settings_path_ + ".backup";
    xe::filesystem::CopyFile(settings_path_, current_backup);
        
    if (xe::filesystem::CopyFile(backup_path, settings_path_)) {
        LoadSettings();
        return true;
    } else {
        
        xe::filesystem::CopyFile(current_backup, settings_path_);
        return false;
    }
}

} 

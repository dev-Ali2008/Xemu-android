#include "android_settings.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include "xenia/base/logging.h"
#include "xenia/base/string.h"

// JSON library
#include <nlohmann/json.hpp>

namespace xanite {

using json = nlohmann::json;

AndroidSettings::AndroidSettings() {
    InitializeDefaults();
    is_initialized_ = true;
    XELOGI("AndroidSettings initialized with default configuration");
}

AndroidSettings::~AndroidSettings() {
    if (has_unsaved_changes_) {
        XELOGW("AndroidSettings destroyed with unsaved changes");
    }
    XELOGI("AndroidSettings destroyed");
}

void AndroidSettings::InitializeDefaults() {
    // Graphics defaults
    graphics_.backend = GraphicsBackend::VULKAN;
    graphics_.resolution_scale = ResolutionScale::NATIVE;
    graphics_.custom_scale = 1.0f;
    graphics_.vsync = true;
    graphics_.fullscreen = true;
    graphics_.texture_filtering = TextureFiltering::ANISOTROPIC_4X;
    graphics_.msaa_samples = 1;
    graphics_.depth_stencil = true;
    graphics_.gpu_timing = false;
    graphics_.frame_rate_limit = false;
    graphics_.max_frame_rate = 60;
    graphics_.triple_buffering = true;
    graphics_.async_presentation = true;
    graphics_.hardware_acceleration = true;
    graphics_.texture_cache_memory_limit_mb = 512;
    graphics_.shader_cache_memory_limit_mb = 256;

    // Audio defaults
    audio_.backend = AudioBackend::AAUDIO;
    audio_.sample_rate = 48000;
    audio_.buffer_size = 1024;
    audio_.channels = 2;
    audio_.enable_audio = true;
    audio_.enable_3d_audio = true;
    audio_.master_volume = 1.0f;
    audio_.game_volume = 1.0f;
    audio_.music_volume = 0.8f;
    audio_.effects_volume = 1.0f;
    audio_.audio_stretching = true;
    audio_.resampling_quality = 2;
    audio_.audio_processing_threads = 2;

    // Input defaults
    input_.controller_layout = ControllerLayout::XBOX_360;
    input_.touch_sensitivity = 1.0f;
    input_.analog_deadzone = 0.15f;
    input_.trigger_deadzone = 0.1f;
    input_.vibration_enabled = true;
    input_.motion_controls = false;
    input_.motion_sensitivity = 1.0f;
    input_.touch_gamepad = true;
    input_.physical_controller_priority = true;
    input_.button_remapping = false;
    input_.show_touch_controls = true;
    input_.touch_opacity = 0.7f;
    input_.touch_haptic_feedback = true;

    // System defaults
    system_.performance_profile = PerformanceProfile::BALANCED;
    system_.thermal_mode = ThermalMode::BALANCED;
    system_.enable_thermal_throttling = true;
    system_.cpu_thread_count = 0;
    system_.enable_smt = true;
    system_.gpu_clock_boost = 0;
    system_.memory_optimization = true;
    system_.cache_size_mb = 256;
    system_.background_processing = false;
    system_.power_saving_suspend = true;
    system_.log_level = 2;
    system_.crash_dumps = true;
    system_.performance_counters = true;

    // Display defaults
    display_.screen_orientation = 0;
    display_.keep_screen_on = true;
    display_.fullscreen_immersive = true;
    display_.show_fps = true;
    display_.show_statistics = false;
    display_.show_controller_overlay = true;
    display_.brightness_boost = 0;
    display_.hdr_support = false;
    display_.force_rgb_range = false;
    display_.color_correction = true;

    // Network defaults
    network_.enable_network_emulation = false;
    network_.xbox_live_emulation = false;
    network_.system_link_emulation = false;
    network_.network_adapter = "default";
    network_.network_latency_ms = 0;
    network_.packet_loss_percent = 0;
    network_.enable_multiplayer = false;
    network_.max_players = 4;
    network_.nat_traversal = true;
    network_.port_forwarding = 0;

    has_unsaved_changes_ = false;
}

bool AndroidSettings::LoadFromFile(const std::string& file_path) {
    std::ifstream file(file_path);
    if (!file.is_open()) {
        XELOGE("Failed to open settings file: %s", file_path.c_str());
        return false;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    file.close();

    bool success = FromJson(buffer.str());
    if (success) {
        config_file_path_ = file_path;
        has_unsaved_changes_ = false;
        XELOGI("Settings loaded successfully from: %s", file_path.c_str());
    } else {
        XELOGE("Failed to parse settings from: %s", file_path.c_str());
    }

    return success;
}

bool AndroidSettings::SaveToFile(const std::string& file_path) {
    std::string json_config = ToJson();
    if (json_config.empty()) {
        XELOGE("Failed to generate settings JSON");
        return false;
    }

    // Create directory if it doesn't exist using std::filesystem
    std::filesystem::path path_obj(file_path);
    std::string directory = path_obj.parent_path().string();
    
    if (!directory.empty() && !std::filesystem::exists(directory)) {
        if (!std::filesystem::create_directories(directory)) {
            XELOGE("Failed to create settings directory: %s", directory.c_str());
            return false;
        }
    }

    std::ofstream file(file_path);
    if (!file.is_open()) {
        XELOGE("Failed to create settings file: %s", file_path.c_str());
        return false;
    }

    file << json_config;
    file.close();

    config_file_path_ = file_path;
    has_unsaved_changes_ = false;
    
    XELOGI("Settings saved successfully to: %s", file_path.c_str());
    return true;
}

void AndroidSettings::ResetToDefaults() {
    InitializeDefaults();
    game_settings_.clear();
    profiles_.clear();
    current_profile_ = "default";
    has_unsaved_changes_ = true;
    
    XELOGI("Settings reset to defaults");
    NotifyCallbacks("all");
}

bool AndroidSettings::ValidateSettings() const {
    if (graphics_.custom_scale < 0.1f || graphics_.custom_scale > 4.0f) {
        XELOGE("Invalid custom scale: %.2f", graphics_.custom_scale);
        return false;
    }

    if (graphics_.max_frame_rate < 1 || graphics_.max_frame_rate > 240) {
        XELOGE("Invalid max frame rate: %d", graphics_.max_frame_rate);
        return false;
    }

    if (audio_.sample_rate < 8000 || audio_.sample_rate > 192000) {
        XELOGE("Invalid sample rate: %d", audio_.sample_rate);
        return false;
    }

    if (audio_.buffer_size < 64 || audio_.buffer_size > 8192) {
        XELOGE("Invalid buffer size: %d", audio_.buffer_size);
        return false;
    }

    if (input_.analog_deadzone < 0.0f || input_.analog_deadzone > 1.0f) {
        XELOGE("Invalid analog deadzone: %.2f", input_.analog_deadzone);
        return false;
    }

    if (input_.touch_sensitivity < 0.1f || input_.touch_sensitivity > 5.0f) {
        XELOGE("Invalid touch sensitivity: %.2f", input_.touch_sensitivity);
        return false;
    }

    if (system_.cpu_thread_count < 0 || system_.cpu_thread_count > 16) {
        XELOGE("Invalid CPU thread count: %d", system_.cpu_thread_count);
        return false;
    }

    if (system_.gpu_clock_boost < 0 || system_.gpu_clock_boost > 100) {
        XELOGE("Invalid GPU clock boost: %d", system_.gpu_clock_boost);
        return false;
    }

    XELOGI("Settings validation passed");
    return true;
}

bool AndroidSettings::SaveProfile(const std::string& profile_name) {
    if (profile_name.empty()) {
        XELOGE("Profile name cannot be empty");
        return false;
    }

    std::string profile_data = ToJson();
    if (profile_data.empty()) {
        XELOGE("Failed to generate profile data");
        return false;
    }

    profiles_[profile_name] = profile_data;
    has_unsaved_changes_ = true;
    
    XELOGI("Profile saved: %s", profile_name.c_str());
    return true;
}

bool AndroidSettings::LoadProfile(const std::string& profile_name) {
    auto it = profiles_.find(profile_name);
    if (it == profiles_.end()) {
        XELOGE("Profile not found: %s", profile_name.c_str());
        return false;
    }

    bool success = FromJson(it->second);
    if (success) {
        current_profile_ = profile_name;
        XELOGI("Profile loaded: %s", profile_name.c_str());
    } else {
        XELOGE("Failed to load profile: %s", profile_name.c_str());
    }

    return success;
}

bool AndroidSettings::DeleteProfile(const std::string& profile_name) {
    auto it = profiles_.find(profile_name);
    if (it == profiles_.end()) {
        XELOGE("Profile not found for deletion: %s", profile_name.c_str());
        return false;
    }

    profiles_.erase(it);
    has_unsaved_changes_ = true;
    
    XELOGI("Profile deleted: %s", profile_name.c_str());
    return true;
}

std::vector<std::string> AndroidSettings::GetAvailableProfiles() const {
    std::vector<std::string> profile_names;
    for (const auto& pair : profiles_) {
        profile_names.push_back(pair.first);
    }
    return profile_names;
}

bool AndroidSettings::LoadGameSettings(const std::string& title_id) {
    auto it = game_settings_.find(title_id);
    if (it == game_settings_.end()) {
        XELOGI("No specific settings found for game: %s", title_id.c_str());
        return false;
    }

    const GameSpecificSettings& game_settings = it->second;
    
    if (!game_settings.use_global_settings) {
        graphics_ = game_settings.graphics_overrides;
        audio_ = game_settings.audio_overrides;
        input_ = game_settings.input_overrides;
        system_ = game_settings.system_overrides;
    }

    XELOGI("Game settings loaded for: %s", title_id.c_str());
    NotifyCallbacks("game_settings");
    return true;
}

bool AndroidSettings::SaveGameSettings(const std::string& title_id) {
    GameSpecificSettings game_settings;
    game_settings.title_id = title_id;
    game_settings.profile_name = "game_" + title_id;
    game_settings.use_global_settings = false;
    game_settings.graphics_overrides = graphics_;
    game_settings.audio_overrides = audio_;
    game_settings.input_overrides = input_;
    game_settings.system_overrides = system_;

    game_settings_[title_id] = game_settings;
    has_unsaved_changes_ = true;
    
    XELOGI("Game settings saved for: %s", title_id.c_str());
    return true;
}

bool AndroidSettings::DeleteGameSettings(const std::string& title_id) {
    auto it = game_settings_.find(title_id);
    if (it == game_settings_.end()) {
        XELOGI("No game settings found for deletion: %s", title_id.c_str());
        return false;
    }

    game_settings_.erase(it);
    has_unsaved_changes_ = true;
    
    XELOGI("Game settings deleted for: %s", title_id.c_str());
    return true;
}

std::vector<std::string> AndroidSettings::GetConfiguredGames() const {
    std::vector<std::string> game_ids;
    for (const auto& pair : game_settings_) {
        game_ids.push_back(pair.first);
    }
    return game_ids;
}

bool AndroidSettings::GetGameSettings(const std::string& title_id, GameSpecificSettings& settings) const {
    auto it = game_settings_.find(title_id);
    if (it == game_settings_.end()) {
        return false;
    }
    
    settings = it->second;
    return true;
}

bool AndroidSettings::SetGameSettings(const std::string& title_id, const GameSpecificSettings& settings) {
    game_settings_[title_id] = settings;
    has_unsaved_changes_ = true;
    return true;
}

void AndroidSettings::ApplyPerformanceProfile(PerformanceProfile profile) {
    system_.performance_profile = profile;
    
    switch (profile) {
        case PerformanceProfile::BATTERY_SAVER:
            graphics_.resolution_scale = ResolutionScale::HALF;
            graphics_.texture_filtering = TextureFiltering::LINEAR;
            graphics_.msaa_samples = 1;
            graphics_.frame_rate_limit = true;
            graphics_.max_frame_rate = 30;
            audio_.resampling_quality = 1;
            system_.gpu_clock_boost = 0;
            system_.enable_thermal_throttling = true;
            break;
            
        case PerformanceProfile::BALANCED:
            graphics_.resolution_scale = ResolutionScale::NATIVE;
            graphics_.texture_filtering = TextureFiltering::ANISOTROPIC_2X;
            graphics_.msaa_samples = 2;
            graphics_.frame_rate_limit = true;
            graphics_.max_frame_rate = 60;
            audio_.resampling_quality = 2;
            system_.gpu_clock_boost = 25;
            system_.enable_thermal_throttling = true;
            break;
            
        case PerformanceProfile::PERFORMANCE:
            graphics_.resolution_scale = ResolutionScale::NATIVE;
            graphics_.texture_filtering = TextureFiltering::ANISOTROPIC_4X;
            graphics_.msaa_samples = 4;
            graphics_.frame_rate_limit = false;
            audio_.resampling_quality = 3;
            system_.gpu_clock_boost = 50;
            system_.enable_thermal_throttling = false;
            break;
            
        case PerformanceProfile::ULTRA_PERFORMANCE:
            graphics_.resolution_scale = ResolutionScale::NATIVE;
            graphics_.texture_filtering = TextureFiltering::ANISOTROPIC_16X;
            graphics_.msaa_samples = 4;
            graphics_.frame_rate_limit = false;
            audio_.resampling_quality = 4;
            system_.gpu_clock_boost = 100;
            system_.enable_thermal_throttling = false;
            break;
    }
    
    has_unsaved_changes_ = true;
    XELOGI("Applied performance profile: %d", static_cast<int>(profile));
    NotifyCallbacks("performance_profile");
}

void AndroidSettings::ApplyThermalProfile(ThermalMode mode) {
    system_.thermal_mode = mode;
    
    switch (mode) {
        case ThermalMode::AGGRESSIVE_COOLING:
            system_.gpu_clock_boost = std::max(0, system_.gpu_clock_boost - 20);
            graphics_.frame_rate_limit = true;
            graphics_.max_frame_rate = std::min(graphics_.max_frame_rate, 45);
            break;
            
        case ThermalMode::BALANCED:
            break;
            
        case ThermalMode::QUIET:
            system_.gpu_clock_boost = std::max(0, system_.gpu_clock_boost - 10);
            graphics_.frame_rate_limit = true;
            graphics_.max_frame_rate = std::min(graphics_.max_frame_rate, 30);
            break;
    }
    
    has_unsaved_changes_ = true;
    XELOGI("Applied thermal profile: %d", static_cast<int>(mode));
    NotifyCallbacks("thermal_profile");
}

void AndroidSettings::OptimizeForBattery() {
    ApplyPerformanceProfile(PerformanceProfile::BATTERY_SAVER);
    system_.power_saving_suspend = true;
    system_.background_processing = false;
    display_.brightness_boost = 0;
    audio_.enable_3d_audio = false;
    
    XELOGI("Optimized settings for battery saving");
    NotifyCallbacks("battery_optimization");
}

void AndroidSettings::OptimizeForPerformance() {
    ApplyPerformanceProfile(PerformanceProfile::PERFORMANCE);
    system_.power_saving_suspend = false;
    system_.background_processing = true;
    system_.memory_optimization = true;
    system_.cache_size_mb = 512;
    
    XELOGI("Optimized settings for maximum performance");
    NotifyCallbacks("performance_optimization");
}

void AndroidSettings::AutoDetectOptimalSettings() {
    ApplyPerformanceProfile(PerformanceProfile::BALANCED);
    XELOGI("Auto-detected optimal settings applied");
    NotifyCallbacks("auto_detection");
}

std::string AndroidSettings::ToJson() const {
    try {
        json j;
        
        j["graphics"]["backend"] = static_cast<int>(graphics_.backend);
        j["graphics"]["resolution_scale"] = static_cast<int>(graphics_.resolution_scale);
        j["graphics"]["custom_scale"] = graphics_.custom_scale;
        j["graphics"]["vsync"] = graphics_.vsync;
        j["graphics"]["texture_filtering"] = static_cast<int>(graphics_.texture_filtering);
        j["graphics"]["msaa_samples"] = graphics_.msaa_samples;
        j["graphics"]["max_frame_rate"] = graphics_.max_frame_rate;
        
        j["audio"]["backend"] = static_cast<int>(audio_.backend);
        j["audio"]["sample_rate"] = audio_.sample_rate;
        j["audio"]["buffer_size"] = audio_.buffer_size;
        j["audio"]["master_volume"] = audio_.master_volume;
        
        j["input"]["controller_layout"] = static_cast<int>(input_.controller_layout);
        j["input"]["analog_deadzone"] = input_.analog_deadzone;
        j["input"]["touch_sensitivity"] = input_.touch_sensitivity;
        
        j["system"]["performance_profile"] = static_cast<int>(system_.performance_profile);
        j["system"]["cpu_thread_count"] = system_.cpu_thread_count;
        j["system"]["log_level"] = system_.log_level;
        
        j["display"]["show_fps"] = display_.show_fps;
        j["display"]["screen_orientation"] = display_.screen_orientation;
        
        j["network"]["enable_network_emulation"] = network_.enable_network_emulation;
        
        for (const auto& game_pair : game_settings_) {
            j["game_settings"][game_pair.first] = game_pair.second.profile_name;
        }
        
        for (const auto& profile_pair : profiles_) {
            j["profiles"][profile_pair.first] = profile_pair.second;
        }
        
        return j.dump(4);
        
    } catch (const std::exception& e) {
        XELOGE("Failed to serialize settings to JSON: %s", e.what());
        return "";
    }
}

bool AndroidSettings::FromJson(const std::string& json_string) {
    try {
        json j = json::parse(json_string);
        
        if (j.contains("graphics")) {
            graphics_.backend = static_cast<GraphicsBackend>(j["graphics"].value("backend", 0));
            graphics_.resolution_scale = static_cast<ResolutionScale>(j["graphics"].value("resolution_scale", 1));
            graphics_.custom_scale = j["graphics"].value("custom_scale", 1.0f);
            graphics_.vsync = j["graphics"].value("vsync", true);
            graphics_.texture_filtering = static_cast<TextureFiltering>(j["graphics"].value("texture_filtering", 2));
            graphics_.msaa_samples = j["graphics"].value("msaa_samples", 1);
            graphics_.max_frame_rate = j["graphics"].value("max_frame_rate", 60);
        }
        
        if (j.contains("audio")) {
            audio_.backend = static_cast<AudioBackend>(j["audio"].value("backend", 1));
            audio_.sample_rate = j["audio"].value("sample_rate", 48000);
            audio_.buffer_size = j["audio"].value("buffer_size", 1024);
            audio_.master_volume = j["audio"].value("master_volume", 1.0f);
        }
        
        if (j.contains("input")) {
            input_.controller_layout = static_cast<ControllerLayout>(j["input"].value("controller_layout", 0));
            input_.analog_deadzone = j["input"].value("analog_deadzone", 0.15f);
            input_.touch_sensitivity = j["input"].value("touch_sensitivity", 1.0f);
        }
        
        if (j.contains("system")) {
            system_.performance_profile = static_cast<PerformanceProfile>(j["system"].value("performance_profile", 1));
            system_.cpu_thread_count = j["system"].value("cpu_thread_count", 0);
            system_.log_level = j["system"].value("log_level", 2);
        }
        
        if (j.contains("display")) {
            display_.show_fps = j["display"].value("show_fps", true);
            display_.screen_orientation = j["display"].value("screen_orientation", 0);
        }
        
        if (j.contains("network")) {
            network_.enable_network_emulation = j["network"].value("enable_network_emulation", false);
        }
        
        XELOGI("Settings loaded from JSON successfully");
        return true;
        
    } catch (const std::exception& e) {
        XELOGE("Failed to parse settings from JSON: %s", e.what());
        return false;
    }
}

std::string AndroidSettings::GetSettingsSummary() const {
    std::stringstream ss;
    
    ss << "Android Settings Summary:\n";
    ss << "Graphics: " << (graphics_.backend == GraphicsBackend::VULKAN ? "Vulkan" : "OpenGL") 
       << ", Scale: " << graphics_.custom_scale << "x, FPS: " 
       << (graphics_.frame_rate_limit ? std::to_string(graphics_.max_frame_rate) : "Unlimited") << "\n";
    ss << "Audio: " << (audio_.backend == AudioBackend::AAUDIO ? "AAudio" : "OpenSL") 
       << ", Rate: " << audio_.sample_rate << "Hz\n";
    ss << "Performance: " << static_cast<int>(system_.performance_profile) 
       << ", Thermal: " << static_cast<int>(system_.thermal_mode) << "\n";
    ss << "Configured Games: " << game_settings_.size() 
       << ", Profiles: " << profiles_.size();
    
    return ss.str();
}

void AndroidSettings::RegisterSettingsCallback(const std::string& setting_name, SettingsChangedCallback callback) {
    callbacks_[setting_name].push_back(callback);
    XELOGI("Callback registered for setting: %s", setting_name.c_str());
}

void AndroidSettings::UnregisterSettingsCallback(const std::string& setting_name) {
    auto it = callbacks_.find(setting_name);
    if (it != callbacks_.end()) {
        callbacks_.erase(it);
        XELOGI("Callbacks unregistered for setting: %s", setting_name.c_str());
    }
}

void AndroidSettings::NotifyCallbacks(const std::string& setting_name) {
    auto it = callbacks_.find(setting_name);
    if (it != callbacks_.end()) {
        for (const auto& callback : it->second) {
            callback(setting_name);
        }
    }
    
    auto all_it = callbacks_.find("all");
    if (all_it != callbacks_.end()) {
        for (const auto& callback : all_it->second) {
            callback(setting_name);
        }
    }
}

} // namespace xanite
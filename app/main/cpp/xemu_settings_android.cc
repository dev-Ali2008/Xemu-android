#include "qemu/osdep.h"

#include <SDL_filesystem.h>
#include <SDL_gamecontroller.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <toml++/toml.h>
#include <android/log.h>

#include <cctype>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

#include "xemu-settings.h"

struct config g_config;
using NetNatForwardPort =
    std::remove_pointer_t<decltype(g_config.net.nat.forward_ports)>;

static std::string settings_path_storage;
static const char *settings_path;
static const char *filename = "xemu.toml";
static std::string error_msg;

static char *xemu_strdup_or_null(const char *value)
{
    if (!value || *value == '\0') {
        return NULL;
    }
    return strdup(value);
}

static void xemu_settings_apply_defaults(void)
{
    memset(&g_config, 0, sizeof(g_config));

    g_config.general.show_welcome = true;
    g_config.general.updates.check = true;
    g_config.general.skip_boot_anim = true;
    g_config.general.last_viewed_menu_index = 0;

    g_config.input.auto_bind = true;
    g_config.input.allow_vibration = true;
    g_config.input.background_input_capture = false;
    g_config.input.keyboard_controller_scancode_map.a = 4;
    g_config.input.keyboard_controller_scancode_map.b = 5;
    g_config.input.keyboard_controller_scancode_map.x = 27;
    g_config.input.keyboard_controller_scancode_map.y = 28;
    g_config.input.keyboard_controller_scancode_map.dpad_left = 80;
    g_config.input.keyboard_controller_scancode_map.dpad_up = 82;
    g_config.input.keyboard_controller_scancode_map.dpad_right = 79;
    g_config.input.keyboard_controller_scancode_map.dpad_down = 81;
    g_config.input.keyboard_controller_scancode_map.back = 42;
    g_config.input.keyboard_controller_scancode_map.start = 40;
    g_config.input.keyboard_controller_scancode_map.white = 30;
    g_config.input.keyboard_controller_scancode_map.black = 31;
    g_config.input.keyboard_controller_scancode_map.lstick_btn = 32;
    g_config.input.keyboard_controller_scancode_map.rstick_btn = 33;
    g_config.input.keyboard_controller_scancode_map.guide = 34;
    g_config.input.keyboard_controller_scancode_map.lstick_up = 8;
    g_config.input.keyboard_controller_scancode_map.lstick_left = 22;
    g_config.input.keyboard_controller_scancode_map.lstick_right = 9;
    g_config.input.keyboard_controller_scancode_map.lstick_down = 7;
    g_config.input.keyboard_controller_scancode_map.ltrigger = 26;
    g_config.input.keyboard_controller_scancode_map.rstick_up = 12;
    g_config.input.keyboard_controller_scancode_map.rstick_left = 13;
    g_config.input.keyboard_controller_scancode_map.rstick_right = 15;
    g_config.input.keyboard_controller_scancode_map.rstick_down = 14;
    g_config.input.keyboard_controller_scancode_map.rtrigger = 18;

    g_config.display.renderer = CONFIG_DISPLAY_RENDERER_VULKAN;
    g_config.display.filtering = CONFIG_DISPLAY_FILTERING_NEAREST;
    g_config.display.quality.surface_scale = 1;
    g_config.display.window.fullscreen_on_startup = false;
    g_config.display.window.fullscreen_exclusive = false;
    g_config.display.window.startup_size =
        CONFIG_DISPLAY_WINDOW_STARTUP_SIZE_1280X720;
    g_config.display.window.last_width = 640;
    g_config.display.window.last_height = 480;
    g_config.display.window.vsync = true;
    g_config.display.ui.show_menubar = true;
    g_config.display.ui.show_notifications = true;
    g_config.display.ui.hide_cursor = true;
    g_config.display.ui.use_animations = true;
    g_config.display.ui.fit = CONFIG_DISPLAY_UI_FIT_SCALE;
    g_config.display.ui.aspect_ratio = CONFIG_DISPLAY_UI_ASPECT_RATIO_AUTO;
    g_config.display.ui.scale = 1;
    g_config.display.ui.auto_scale = true;
    g_config.display.setup_nvidia_profile = true;

    g_config.audio.vp.num_workers = 0;
    g_config.audio.use_dsp = false;
    g_config.audio.use_dsp_jit = true;
    g_config.audio.hrtf = true;
    g_config.audio.volume_limit = 1.0;

    g_config.net.enable = false;
    g_config.net.backend = CONFIG_NET_BACKEND_NAT;
    g_config.net.udp.bind_addr = xemu_strdup_or_null("0.0.0.0:9368");
    g_config.net.udp.remote_addr = xemu_strdup_or_null("1.2.3.4:9368");

    g_config.sys.mem_limit = CONFIG_SYS_MEM_LIMIT_64;
    g_config.sys.avpack = CONFIG_SYS_AVPACK_HDTV;

    g_config.perf.fp_jit = true;
    g_config.perf.cache_shaders = true;
    g_config.perf.unlock_framerate = true;
}

// Optimized parsers - avoid string allocations
static bool parse_renderer(const std::string &value, CONFIG_DISPLAY_RENDERER *out)
{
    if (value.size() == 6 && (value == "opengl" || value == "OpenGL" || value == "OPENGL")) {
        *out = CONFIG_DISPLAY_RENDERER_OPENGL;
        return true;
    }
    if (value.size() == 2 && (value == "gl" || value == "GL")) {
        *out = CONFIG_DISPLAY_RENDERER_OPENGL;
        return true;
    }
    if (value.size() == 6 && (value == "vulkan" || value == "Vulkan" || value == "VULKAN")) {
        *out = CONFIG_DISPLAY_RENDERER_VULKAN;
        return true;
    }
    if (value.size() == 2 && (value == "vk" || value == "VK")) {
        *out = CONFIG_DISPLAY_RENDERER_VULKAN;
        return true;
    }
    if ((value.size() == 4 && (value == "null" || value == "NULL" || value == "Null" || value == "none" || value == "NONE" || value == "None"))) {
        *out = CONFIG_DISPLAY_RENDERER_NULL;
        return true;
    }
    return false;
}

static bool parse_filtering(const std::string &value, CONFIG_DISPLAY_FILTERING *out)
{
    if (value.size() == 6 && (value == "linear" || value == "Linear" || value == "LINEAR")) {
        *out = CONFIG_DISPLAY_FILTERING_LINEAR;
        return true;
    }
    if (value.size() == 7 && (value == "nearest" || value == "Nearest" || value == "NEAREST")) {
        *out = CONFIG_DISPLAY_FILTERING_NEAREST;
        return true;
    }
    return false;
}

static std::string to_lower_ascii(std::string value)
{
    for (char &c : value) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return value;
}

static bool parse_network_backend(toml::node_view<toml::node> node,
                                  CONFIG_NET_BACKEND *out)
{
    if (!out) {
        return false;
    }

    if (auto backend = node.value<std::string>()) {
        std::string normalized = to_lower_ascii(*backend);
        if (normalized == "nat" || normalized == "user") {
            *out = CONFIG_NET_BACKEND_NAT;
            return true;
        }
        if (normalized == "udp" || normalized == "udp_tunnel" ||
            normalized == "udp tunnel") {
            *out = CONFIG_NET_BACKEND_UDP;
            return true;
        }
        if (normalized == "pcap" || normalized == "bridge" ||
            normalized == "bridged adapter") {
            *out = CONFIG_NET_BACKEND_PCAP;
            return true;
        }
        return false;
    }

    if (auto backend = node.value<int64_t>()) {
        if (*backend < 0 || *backend >= CONFIG_NET_BACKEND__COUNT) {
            return false;
        }
        *out = static_cast<CONFIG_NET_BACKEND>(*backend);
        return true;
    }

    return false;
}

static bool parse_nat_forward_protocol(
    toml::node_view<const toml::node> node,
    CONFIG_NET_NAT_FORWARD_PORTS_PROTOCOL *out)
{
    if (!out) {
        return false;
    }

    if (auto protocol = node.value<std::string>()) {
        std::string normalized = to_lower_ascii(*protocol);
        if (normalized == "tcp") {
            *out = CONFIG_NET_NAT_FORWARD_PORTS_PROTOCOL_TCP;
            return true;
        }
        if (normalized == "udp") {
            *out = CONFIG_NET_NAT_FORWARD_PORTS_PROTOCOL_UDP;
            return true;
        }
        return false;
    }

    if (auto protocol = node.value<int64_t>()) {
        if (*protocol < 0 || *protocol >= CONFIG_NET_NAT_FORWARD_PORTS_PROTOCOL__COUNT) {
            return false;
        }
        *out = static_cast<CONFIG_NET_NAT_FORWARD_PORTS_PROTOCOL>(*protocol);
        return true;
    }

    return false;
}

const char *xemu_settings_get_error_message(void)
{
    return error_msg.empty() ? NULL : error_msg.c_str();
}

void xemu_settings_set_path(const char *path)
{
    if (path && *path) {
        settings_path_storage = path;
        settings_path = settings_path_storage.c_str();
    } else {
        settings_path_storage.clear();
        settings_path = NULL;
    }
}

const char *xemu_settings_get_base_path(void)
{
    static const char *base_path = NULL;
    if (base_path) {
        return base_path;
    }

    char *base = SDL_GetPrefPath("xemu", "xemu");
    if (!base) {
        base = SDL_GetBasePath();
    }
    base_path = base ? strdup(base) : strdup("");
    SDL_free(base);
    __android_log_print(ANDROID_LOG_INFO, "xemu-config",
                        "xemu_settings_get_base_path: %s", base_path);
    return base_path;
}

const char *xemu_settings_get_path(void)
{
    if (settings_path != NULL) {
        return settings_path;
    }

    const char *base = xemu_settings_get_base_path();
    settings_path_storage = std::string(base ? base : "") + filename;
    settings_path = settings_path_storage.c_str();
    return settings_path;
}

const char *xemu_settings_get_default_eeprom_path(void)
{
    static char *eeprom_path = NULL;
    if (eeprom_path != NULL) {
        return eeprom_path;
    }

    const char *base = xemu_settings_get_base_path();
    eeprom_path = g_strdup_printf("%s%s", base, "eeprom.bin");
    return eeprom_path;
}

bool xemu_settings_load(void)
{
    const int kMaxAudioVpWorkers = 16;

    xemu_settings_apply_defaults();
    error_msg.clear();
    setenv("XEMU_ANDROID_FORCE_CPU_BLIT", "0", 1);
    setenv("XEMU_ANDROID_TCG_TUNING", "1", 1);
    setenv("XEMU_ANDROID_TCG_THREAD", "multi", 1);
    setenv("XEMU_ANDROID_TCG_TB_SIZE", "128", 1);

    const char *path = xemu_settings_get_path();
    if (!path || *path == '\0') {
        return true;
    }

    if (qemu_access(path, F_OK) == -1) {
        return true;
    }

    try {
        toml::table tbl = toml::parse_file(path);

        // Cache table lookups to avoid repeated traversal
        auto general = tbl["general"];
        auto display = tbl["display"];
        auto display_window = display["window"];
        auto audio = tbl["audio"];
        auto audio_vp = audio["vp"];
        auto perf = tbl["perf"];
        auto android_cfg = tbl["android"];
        auto net = tbl["net"];
        auto net_udp = net["udp"];
        auto net_pcap = net["pcap"];
        auto net_nat = net["nat"];
        auto sys = tbl["sys"];
        auto sys_files = sys["files"];

        // General settings
        if (auto show_welcome = general["show_welcome"].value<bool>()) {
            g_config.general.show_welcome = *show_welcome;
        }
        if (auto skip_boot_anim = general["skip_boot_anim"].value<bool>()) {
            g_config.general.skip_boot_anim = *skip_boot_anim;
        }

        // Display settings - default to Vulkan, allow OpenGL via config
        g_config.display.renderer = CONFIG_DISPLAY_RENDERER_VULKAN;
        if (auto renderer = display["renderer"].value<std::string>()) {
            CONFIG_DISPLAY_RENDERER parsed;
            if (parse_renderer(*renderer, &parsed)) {
                if (parsed == CONFIG_DISPLAY_RENDERER_VULKAN ||
                    parsed == CONFIG_DISPLAY_RENDERER_OPENGL) {
                    g_config.display.renderer = parsed;
                }
                __android_log_print(ANDROID_LOG_INFO, "xemu-android",
                                    "Config display.renderer=%s (using %s)",
                                    renderer->c_str(),
                                    g_config.display.renderer == CONFIG_DISPLAY_RENDERER_VULKAN
                                        ? "Vulkan" : "OpenGL");
            }
        }

        if (auto filtering = display["filtering"].value<std::string>()) {
            CONFIG_DISPLAY_FILTERING parsed;
            if (parse_filtering(*filtering, &parsed)) {
                g_config.display.filtering = parsed;
            }
        }

        if (auto vsync = display_window["vsync"].value<bool>()) {
            g_config.display.window.vsync = *vsync;
        }

        auto display_quality = display["quality"];
        if (auto scale = display_quality["surface_scale"].value<int64_t>()) {
            int s = (int)*scale;
            if (s >= 1 && s <= 10) {
                g_config.display.quality.surface_scale = s;
            }
        }

        auto display_vulkan = display["vulkan"];
        if (auto vl = display_vulkan["validation_layers"].value<bool>()) {
            g_config.display.vulkan.validation_layers = *vl;
        }

        auto display_ui = display["ui"];
        if (auto ar = display_ui["aspect_ratio"].value<std::string>()) {
            if (*ar == "native" || *ar == "4:3") {
                g_config.display.ui.aspect_ratio = CONFIG_DISPLAY_UI_ASPECT_RATIO_NATIVE;
                g_config.display.ui.fit = CONFIG_DISPLAY_UI_FIT_SCALE;
            } else if (*ar == "auto") {
                g_config.display.ui.aspect_ratio = CONFIG_DISPLAY_UI_ASPECT_RATIO_AUTO;
                g_config.display.ui.fit = CONFIG_DISPLAY_UI_FIT_SCALE;
            } else if (*ar == "16:9") {
                g_config.display.ui.aspect_ratio = CONFIG_DISPLAY_UI_ASPECT_RATIO_16X9;
                g_config.display.ui.fit = CONFIG_DISPLAY_UI_FIT_SCALE;
            } else if (*ar == "fit") {
                g_config.display.ui.fit = CONFIG_DISPLAY_UI_FIT_STRETCH;
            }
        }

        // Performance settings
        if (auto fp_jit = perf["fp_jit"].value<bool>()) {
            g_config.perf.fp_jit = *fp_jit;
        }
        if (auto cache_shaders = perf["cache_shaders"].value<bool>()) {
            g_config.perf.cache_shaders = *cache_shaders;
        }
        if (auto unlock_framerate = perf["unlock_framerate"].value<bool>()) {
            g_config.perf.unlock_framerate = *unlock_framerate;
        }

        // Audio settings
        if (auto vp_workers = audio_vp["num_workers"].value<int64_t>()) {
            int workers = (int)*vp_workers;
            if (workers < 0) {
                workers = 0;
            } else if (workers > kMaxAudioVpWorkers) {
                workers = kMaxAudioVpWorkers;
            }
            g_config.audio.vp.num_workers = workers;
        }
        if (auto use_dsp = audio["use_dsp"].value<bool>()) {
            g_config.audio.use_dsp = *use_dsp;
        }
        if (auto use_dsp_jit = audio["use_dsp_jit"].value<bool>()) {
            g_config.audio.use_dsp_jit = *use_dsp_jit;
        }
        if (auto hrtf = audio["hrtf"].value<bool>()) {
            g_config.audio.hrtf = *hrtf;
        }
        if (auto volume_limit = audio["volume_limit"].value<double>()) {
            double volume = *volume_limit;
            if (volume < 0.0) {
                volume = 0.0;
            } else if (volume > 1.0) {
                volume = 1.0;
            }
            g_config.audio.volume_limit = volume;
        }

        if (auto net_enable = net["enable"].value<bool>()) {
            g_config.net.enable = *net_enable;
        }
        {
            CONFIG_NET_BACKEND parsed_backend;
            if (parse_network_backend(net["backend"], &parsed_backend)) {
                g_config.net.backend = parsed_backend;
            }
        }
        if (auto bind_addr = net_udp["bind_addr"].value<std::string>()) {
            xemu_settings_set_string(&g_config.net.udp.bind_addr, bind_addr->c_str());
        }
        if (auto remote_addr = net_udp["remote_addr"].value<std::string>()) {
            xemu_settings_set_string(&g_config.net.udp.remote_addr, remote_addr->c_str());
        }
        if (auto netif = net_pcap["netif"].value<std::string>()) {
            xemu_settings_set_string(&g_config.net.pcap.netif, netif->c_str());
        }
        if (auto forward_ports = net_nat["forward_ports"].as_array()) {
            std::vector<NetNatForwardPort> parsed_ports;
            parsed_ports.reserve(forward_ports->size());

            for (const auto &entry : *forward_ports) {
                const toml::table *table = entry.as_table();
                if (!table) {
                    continue;
                }

                auto host = (*table)["host"].value<int64_t>();
                auto guest = (*table)["guest"].value<int64_t>();
                if (!host || !guest || *host < 1 || *host > 65535 ||
                    *guest < 1 || *guest > 65535) {
                    continue;
                }

                CONFIG_NET_NAT_FORWARD_PORTS_PROTOCOL protocol =
                    CONFIG_NET_NAT_FORWARD_PORTS_PROTOCOL_TCP;
                parse_nat_forward_protocol((*table)["protocol"], &protocol);

                NetNatForwardPort port = {};
                port.host = static_cast<int>(*host);
                port.guest = static_cast<int>(*guest);
                port.protocol = protocol;
                parsed_ports.push_back(port);
            }

            free(g_config.net.nat.forward_ports);
            g_config.net.nat.forward_ports = NULL;
            g_config.net.nat.forward_ports_count = 0;

            if (!parsed_ports.empty()) {
                auto *ports = static_cast<NetNatForwardPort *>(calloc(
                    parsed_ports.size(), sizeof(NetNatForwardPort)));
                if (ports) {
                    memcpy(ports, parsed_ports.data(),
                           parsed_ports.size() * sizeof(NetNatForwardPort));
                    g_config.net.nat.forward_ports = ports;
                    g_config.net.nat.forward_ports_count =
                        static_cast<unsigned int>(parsed_ports.size());
                }
            }
        }

        // Android-specific settings
        if (auto force_cpu_blit = android_cfg["force_cpu_blit"].value<bool>()) {
            setenv("XEMU_ANDROID_FORCE_CPU_BLIT", *force_cpu_blit ? "1" : "0", 1);
        }
        if (auto egl_offscreen = android_cfg["egl_offscreen"].value<bool>()) {
            if (!*egl_offscreen) {
                setenv("XEMU_ANDROID_EGL_OFFSCREEN", "0", 1);
            }
        }
        if (auto tcg_tuning = android_cfg["tcg_tuning"].value<bool>()) {
            setenv("XEMU_ANDROID_TCG_TUNING", *tcg_tuning ? "1" : "0", 1);
        }
        if (auto tcg_thread = android_cfg["tcg_thread"].value<std::string>()) {
            if (*tcg_thread == "single" || *tcg_thread == "multi") {
                setenv("XEMU_ANDROID_TCG_THREAD", tcg_thread->c_str(), 1);
            } else {
                __android_log_print(ANDROID_LOG_WARN, "xemu-android",
                                    "Ignoring android.tcg_thread=%s (expected single|multi)",
                                    tcg_thread->c_str());
            }
        }
        if (auto tcg_tb_size = android_cfg["tcg_tb_size"].value<int64_t>()) {
            int tb_size = (int)*tcg_tb_size;
            if (tb_size < 32) {
                tb_size = 32;
            } else if (tb_size > 512) {
                tb_size = 512;
            }
            char tb_size_str[16];
            snprintf(tb_size_str, sizeof(tb_size_str), "%d", tb_size);
            setenv("XEMU_ANDROID_TCG_TB_SIZE", tb_size_str, 1);
        }
        if (auto inline_aio = android_cfg["inline_aio"].value<bool>()) {
            setenv("XEMU_ANDROID_INLINE_AIO", *inline_aio ? "1" : "0", 1);
        }
        if (auto vp_workers = android_cfg["vp_workers"].value<int64_t>()) {
            int workers = (int)*vp_workers;
            if (workers < 0) {
                workers = 0;
            } else if (workers > kMaxAudioVpWorkers) {
                workers = kMaxAudioVpWorkers;
            }
            char workers_str[16];
            snprintf(workers_str, sizeof(workers_str), "%d", workers);
            setenv("XEMU_ANDROID_VP_WORKERS", workers_str, 1);
        }
        if (auto audio_samples = android_cfg["audio_samples"].value<int64_t>()) {
            int samples = (int)*audio_samples;
            if (samples < 256) {
                samples = 256;
            } else if (samples > 4096) {
                samples = 4096;
            }
            char samples_str[16];
            snprintf(samples_str, sizeof(samples_str), "%d", samples);
            setenv("XEMU_ANDROID_AUDIO_SAMPLES", samples_str, 1);
        }
        if (auto audio_fifo_frames =
                android_cfg["audio_fifo_frames"].value<int64_t>()) {
            int fifo_frames = (int)*audio_fifo_frames;
            if (fifo_frames < 3) {
                fifo_frames = 3;
            } else if (fifo_frames > 32) {
                fifo_frames = 32;
            }
            char fifo_str[16];
            snprintf(fifo_str, sizeof(fifo_str), "%d", fifo_frames);
            setenv("XEMU_ANDROID_AUDIO_FIFO_FRAMES", fifo_str, 1);
        }
        if (auto audio_driver = android_cfg["audio_driver"].value<std::string>()) {
            std::string driver = *audio_driver;
            std::string normalized = to_lower_ascii(driver);
            if (normalized == "audiotrack" || normalized == "android") {
                setenv("XEMU_ANDROID_AUDIO_DRIVER", "opensles", 1);
            } else if (normalized == "opensl" || normalized == "opensles") {
                setenv("XEMU_ANDROID_AUDIO_DRIVER", "opensles", 1);
            } else if (normalized == "aaudio") {
                setenv("XEMU_ANDROID_AUDIO_DRIVER", "aaudio", 1);
            } else if (normalized == "dummy" || normalized == "disabled") {
                setenv("XEMU_ANDROID_AUDIO_DRIVER", "dummy", 1);
            } else if (!driver.empty()) {
                setenv("XEMU_ANDROID_AUDIO_DRIVER", driver.c_str(), 1);
            }
        }

        if (auto mem_limit = sys["mem_limit"].value<std::string>()) {
            if (*mem_limit == "128") {
                g_config.sys.mem_limit = CONFIG_SYS_MEM_LIMIT_128;
            } else {
                g_config.sys.mem_limit = CONFIG_SYS_MEM_LIMIT_64;
            }
        } else if (auto mem_limit = sys["mem_limit"].value<int64_t>()) {
            if ((int)*mem_limit == 128) {
                g_config.sys.mem_limit = CONFIG_SYS_MEM_LIMIT_128;
            } else {
                g_config.sys.mem_limit = CONFIG_SYS_MEM_LIMIT_64;
            }
        }

        // System file paths
        if (auto bootrom = sys_files["bootrom_path"].value<std::string>()) {
            xemu_settings_set_string(&g_config.sys.files.bootrom_path, bootrom->c_str());
        }
        if (auto flashrom = sys_files["flashrom_path"].value<std::string>()) {
            xemu_settings_set_string(&g_config.sys.files.flashrom_path, flashrom->c_str());
        }
        if (auto hdd = sys_files["hdd_path"].value<std::string>()) {
            xemu_settings_set_string(&g_config.sys.files.hdd_path, hdd->c_str());
        }
        if (auto dvd = sys_files["dvd_path"].value<std::string>()) {
            xemu_settings_set_string(&g_config.sys.files.dvd_path, dvd->c_str());
        }
        if (auto eeprom = sys_files["eeprom_path"].value<std::string>()) {
            xemu_settings_set_string(&g_config.sys.files.eeprom_path, eeprom->c_str());
        }
    } catch (const toml::parse_error &err) {
        std::ostringstream oss;
        oss << "Error parsing config file at " << err.source().begin << ":\n"
            << "    " << err.description() << "\n";
        error_msg = oss.str();
        return false;
    }
    return true;
}

void xemu_settings_save(void)
{
}

void add_net_nat_forward_ports(int host, int guest,
                               CONFIG_NET_NAT_FORWARD_PORTS_PROTOCOL protocol)
{
    if (host < 1 || host > 65535 || guest < 1 || guest > 65535 ||
        protocol < 0 || protocol >= CONFIG_NET_NAT_FORWARD_PORTS_PROTOCOL__COUNT) {
        return;
    }

    unsigned int old_count = g_config.net.nat.forward_ports_count;
    unsigned int new_count = old_count + 1;
    auto *ports = static_cast<NetNatForwardPort *>(realloc(
        g_config.net.nat.forward_ports, sizeof(NetNatForwardPort) * new_count));
    if (!ports) {
        return;
    }

    ports[old_count].host = host;
    ports[old_count].guest = guest;
    ports[old_count].protocol = protocol;
    g_config.net.nat.forward_ports = ports;
    g_config.net.nat.forward_ports_count = new_count;
}

void remove_net_nat_forward_ports(unsigned int index)
{
    if (index >= g_config.net.nat.forward_ports_count) {
        return;
    }

    unsigned int old_count = g_config.net.nat.forward_ports_count;
    unsigned int new_count = old_count - 1;
    auto *ports =
        static_cast<NetNatForwardPort *>(g_config.net.nat.forward_ports);

    if (index + 1 < old_count) {
        memmove(&ports[index], &ports[index + 1],
                (old_count - index - 1) * sizeof(NetNatForwardPort));
    }

    if (new_count == 0) {
        free(ports);
        g_config.net.nat.forward_ports = NULL;
        g_config.net.nat.forward_ports_count = 0;
        return;
    }

    auto *resized = static_cast<NetNatForwardPort *>(realloc(
        ports, sizeof(NetNatForwardPort) * new_count));
    if (!resized) {
        g_config.net.nat.forward_ports_count = new_count;
        return;
    }

    g_config.net.nat.forward_ports = resized;
    g_config.net.nat.forward_ports_count = new_count;
}

bool xemu_settings_load_gamepad_mapping(const char *guid,
                                        GamepadMappings **mapping)
{
    if (!mapping) {
        return false;
    }

    *mapping = NULL;
    if (!guid || *guid == '\0') {
        return false;
    }

    unsigned int gamepad_mappings_count = g_config.input.gamepad_mappings_count;
    for (unsigned int i = 0; i < gamepad_mappings_count; ++i) {
        GamepadMappings *entry = &g_config.input.gamepad_mappings[i];
        if (!entry->gamepad_id || strcmp(entry->gamepad_id, guid) != 0) {
            continue;
        }

        // Preserve old behavior: global vibration off disables rumble.
        if (!g_config.input.allow_vibration) {
            entry->enable_rumble = false;
        }

        *mapping = entry;
        return false;
    }

    auto apply_default_controller_mapping = [](GamepadMappings *entry) {
        entry->controller_mapping.a = SDL_CONTROLLER_BUTTON_A;
        entry->controller_mapping.b = SDL_CONTROLLER_BUTTON_B;
        entry->controller_mapping.x = SDL_CONTROLLER_BUTTON_X;
        entry->controller_mapping.y = SDL_CONTROLLER_BUTTON_Y;
        entry->controller_mapping.back = SDL_CONTROLLER_BUTTON_BACK;
        entry->controller_mapping.guide = SDL_CONTROLLER_BUTTON_GUIDE;
        entry->controller_mapping.start = SDL_CONTROLLER_BUTTON_START;
        entry->controller_mapping.lstick_btn = SDL_CONTROLLER_BUTTON_LEFTSTICK;
        entry->controller_mapping.rstick_btn = SDL_CONTROLLER_BUTTON_RIGHTSTICK;
        entry->controller_mapping.lshoulder = SDL_CONTROLLER_BUTTON_LEFTSHOULDER;
        entry->controller_mapping.rshoulder =
            SDL_CONTROLLER_BUTTON_RIGHTSHOULDER;
        entry->controller_mapping.dpad_up = SDL_CONTROLLER_BUTTON_DPAD_UP;
        entry->controller_mapping.dpad_down = SDL_CONTROLLER_BUTTON_DPAD_DOWN;
        entry->controller_mapping.dpad_left = SDL_CONTROLLER_BUTTON_DPAD_LEFT;
        entry->controller_mapping.dpad_right =
            SDL_CONTROLLER_BUTTON_DPAD_RIGHT;
        entry->controller_mapping.axis_left_x = SDL_CONTROLLER_AXIS_LEFTX;
        entry->controller_mapping.axis_left_y = SDL_CONTROLLER_AXIS_LEFTY;
        entry->controller_mapping.axis_right_x = SDL_CONTROLLER_AXIS_RIGHTX;
        entry->controller_mapping.axis_right_y = SDL_CONTROLLER_AXIS_RIGHTY;
        entry->controller_mapping.axis_trigger_left =
            SDL_CONTROLLER_AXIS_TRIGGERLEFT;
        entry->controller_mapping.axis_trigger_right =
            SDL_CONTROLLER_AXIS_TRIGGERRIGHT;
        entry->controller_mapping.invert_axis_left_x = false;
        entry->controller_mapping.invert_axis_left_y = false;
        entry->controller_mapping.invert_axis_right_x = false;
        entry->controller_mapping.invert_axis_right_y = false;
    };

    const unsigned int old_count = g_config.input.gamepad_mappings_count;
    const unsigned int new_count = old_count + 1;
    GamepadMappings *new_mappings = static_cast<GamepadMappings *>(realloc(
        g_config.input.gamepad_mappings, sizeof(GamepadMappings) * new_count));
    if (!new_mappings) {
        __android_log_print(ANDROID_LOG_ERROR, "xemu-android",
                            "Failed to allocate gamepad mapping for %s", guid);
        return false;
    }

    g_config.input.gamepad_mappings = new_mappings;
    g_config.input.gamepad_mappings_count = new_count;

    GamepadMappings *entry = &g_config.input.gamepad_mappings[old_count];
    memset(entry, 0, sizeof(*entry));
    entry->gamepad_id = strdup(guid);
    entry->enable_rumble = g_config.input.allow_vibration;
    apply_default_controller_mapping(entry);

    *mapping = entry;
    return true;
}

void xemu_settings_reset_controller_mapping(const char *guid)
{
    if (!guid || *guid == '\0') {
        return;
    }

    unsigned int gamepad_mappings_count = g_config.input.gamepad_mappings_count;
    for (unsigned int i = 0; i < gamepad_mappings_count; ++i) {
        GamepadMappings *entry = &g_config.input.gamepad_mappings[i];
        if (!entry->gamepad_id || strcmp(entry->gamepad_id, guid) != 0) {
            continue;
        }

        entry->enable_rumble = g_config.input.allow_vibration;
        entry->controller_mapping.a = SDL_CONTROLLER_BUTTON_A;
        entry->controller_mapping.b = SDL_CONTROLLER_BUTTON_B;
        entry->controller_mapping.x = SDL_CONTROLLER_BUTTON_X;
        entry->controller_mapping.y = SDL_CONTROLLER_BUTTON_Y;
        entry->controller_mapping.back = SDL_CONTROLLER_BUTTON_BACK;
        entry->controller_mapping.guide = SDL_CONTROLLER_BUTTON_GUIDE;
        entry->controller_mapping.start = SDL_CONTROLLER_BUTTON_START;
        entry->controller_mapping.lstick_btn = SDL_CONTROLLER_BUTTON_LEFTSTICK;
        entry->controller_mapping.rstick_btn = SDL_CONTROLLER_BUTTON_RIGHTSTICK;
        entry->controller_mapping.lshoulder = SDL_CONTROLLER_BUTTON_LEFTSHOULDER;
        entry->controller_mapping.rshoulder = SDL_CONTROLLER_BUTTON_RIGHTSHOULDER;
        entry->controller_mapping.dpad_up = SDL_CONTROLLER_BUTTON_DPAD_UP;
        entry->controller_mapping.dpad_down = SDL_CONTROLLER_BUTTON_DPAD_DOWN;
        entry->controller_mapping.dpad_left = SDL_CONTROLLER_BUTTON_DPAD_LEFT;
        entry->controller_mapping.dpad_right = SDL_CONTROLLER_BUTTON_DPAD_RIGHT;
        entry->controller_mapping.axis_left_x = SDL_CONTROLLER_AXIS_LEFTX;
        entry->controller_mapping.axis_left_y = SDL_CONTROLLER_AXIS_LEFTY;
        entry->controller_mapping.axis_right_x = SDL_CONTROLLER_AXIS_RIGHTX;
        entry->controller_mapping.axis_right_y = SDL_CONTROLLER_AXIS_RIGHTY;
        entry->controller_mapping.axis_trigger_left = SDL_CONTROLLER_AXIS_TRIGGERLEFT;
        entry->controller_mapping.axis_trigger_right = SDL_CONTROLLER_AXIS_TRIGGERRIGHT;
        entry->controller_mapping.invert_axis_left_x = false;
        entry->controller_mapping.invert_axis_left_y = false;
        entry->controller_mapping.invert_axis_right_x = false;
        entry->controller_mapping.invert_axis_right_y = false;
        return;
    }
}

void xemu_settings_reset_keyboard_mapping(void)
{
}

extern "C" void xemu_set_fp_jit(bool enable)
{
    g_config.perf.fp_jit = enable;
}

extern "C" bool xemu_get_fp_jit(void)
{
    return g_config.perf.fp_jit;
}

/*
 * Geforce NV2A PGRAPH Vulkan Renderer
 *
 * Copyright (c) 2024-2025 Matt Borgerson
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "hw/xbox/nv2a/nv2a_int.h"
#include "renderer.h"
#include "qemu/error-report.h"
#include "qemu/fast-hash.h"
#include "ui/xemu-settings.h"

extern bool xemu_get_frame_skip(void);
#ifdef __ANDROID__
#include <android/log.h>
#define DBG_LOG(...) __android_log_print(ANDROID_LOG_INFO, "hakuX-vk-dbg", __VA_ARGS__)
#else
#define DBG_LOG(...) fprintf(stderr, __VA_ARGS__)
#endif

#include "gloffscreen.h"

#include <sys/stat.h>
#include <unistd.h>

#ifdef __ANDROID__
#include <android/log.h>
#endif

typedef struct {
    uint32_t vendor_id;
    uint32_t device_id;
    uint32_t driver_version;
    uint8_t  pipeline_cache_uuid[VK_UUID_SIZE];
} GpuDriverIdentity;

static void remove_directory_recursive(const char *path)
{
    GDir *dir = g_dir_open(path, 0, NULL);
    if (!dir) {
        return;
    }
    const gchar *name;
    while ((name = g_dir_read_name(dir)) != NULL) {
        gchar *child = g_build_filename(path, name, NULL);
        if (g_file_test(child, G_FILE_TEST_IS_DIR)) {
            remove_directory_recursive(child);
        } else {
            unlink(child);
        }
        g_free(child);
    }
    g_dir_close(dir);
    rmdir(path);
}

static void check_driver_identity_and_wipe_caches(PGRAPHVkState *r)
{
    if (!g_config.perf.cache_shaders) {
        return;
    }

    const char *base = xemu_settings_get_base_path();
    char *id_path = g_strdup_printf("%sgpu_driver_id.bin", base);

    GpuDriverIdentity current;
    current.vendor_id = r->device_props.vendorID;
    current.device_id = r->device_props.deviceID;
    current.driver_version = r->device_props.driverVersion;
    memcpy(current.pipeline_cache_uuid, r->device_props.pipelineCacheUUID,
           VK_UUID_SIZE);

    bool match = false;
    gchar *data = NULL;
    gsize len = 0;
    if (g_file_get_contents(id_path, &data, &len, NULL) &&
        len == sizeof(GpuDriverIdentity)) {
        match = memcmp(data, &current, sizeof(GpuDriverIdentity)) == 0;
    }
    g_free(data);

    if (!match) {
        char *spv_dir = g_strdup_printf("%sspv_cache", base);
        char *plc_path = g_strdup_printf("%svk_pipeline_cache.bin", base);

        VK_LOG("Driver changed -- wiping shader and pipeline caches");
#ifdef __ANDROID__
        __android_log_print(ANDROID_LOG_INFO, "hakuX-vk",
            "Driver identity mismatch: wiping spv_cache and pipeline cache "
            "(vendor=%04x device=%04x driverVer=%08x)",
            current.vendor_id, current.device_id, current.driver_version);
#else
        fprintf(stderr, "xemu-vk: Driver identity mismatch: wiping caches "
                "(vendor=%04x device=%04x driverVer=%08x)\n",
                current.vendor_id, current.device_id, current.driver_version);
#endif

        char *smk_path = g_strdup_printf("%sshader_module_keys.bin", base);
        remove_directory_recursive(spv_dir);
        unlink(plc_path);
        unlink(smk_path);
        g_free(spv_dir);
        g_free(plc_path);
        g_free(smk_path);

        g_file_set_contents(id_path, (const gchar *)&current,
                            sizeof(GpuDriverIdentity), NULL);
    }

    g_free(id_path);
}

#if HAVE_EXTERNAL_MEMORY
static GloContext *g_gl_context;
#endif

void pgraph_vk_gl_make_context_current(void)
{
#if HAVE_EXTERNAL_MEMORY
    if (!g_gl_context) {
        g_gl_context = glo_context_create();
    }
    if (g_gl_context) {
        glo_set_current(g_gl_context);
    }
#endif
}

static void early_context_init(void)
{
#if HAVE_EXTERNAL_MEMORY
#ifdef __ANDROID__
    /*
     * On Android, only cache EGL share/config on the SDL thread here.
     * Create/bind the offscreen context later on the renderer thread.
     */
    glo_android_cache_current_egl_state();
#else
    g_gl_context = glo_context_create();
#endif
#endif
}

static void pgraph_vk_init(NV2AState *d, Error **errp)
{
    PGRAPHState *pg = &d->pgraph;

    pg->vk_renderer_state = (PGRAPHVkState *)g_malloc0(sizeof(PGRAPHVkState));
    pg->vk_renderer_state->nv2a = d;
    pg->vk_renderer_state->need_descriptor_rebind = true;
    pg->vk_renderer_state->deferred_downloads_frame = -1;

#if HAVE_EXTERNAL_MEMORY
    bool use_external_memory = pgraph_vk_gl_external_memory_available();
    if (!use_external_memory) {
#ifdef __ANDROID__
        __android_log_print(ANDROID_LOG_WARN, "hakuX",
                            "pgraph_vk_init: external memory interop unavailable, using download fallback");
#endif
    }
#ifdef __ANDROID__
    __android_log_print(ANDROID_LOG_INFO, "hakuX",
                        "pgraph_vk_init: external memory interop=%s",
                        use_external_memory ? "enabled" : "disabled");
#endif
    pg->vk_renderer_state->display.use_external_memory = use_external_memory;
#endif

    pgraph_vk_debug_init();

    pgraph_vk_init_instance(pg, errp);
    if (*errp) {
        return;
    }

    check_driver_identity_and_wipe_caches(pg->vk_renderer_state);

    VK_LOG_ERROR("init: render_thread");
    pgraph_vk_render_thread_init(pg->vk_renderer_state);
    VK_LOG_ERROR("init: command_buffers");
    pgraph_vk_init_command_buffers(pg);
    VK_LOG_ERROR("init: buffers");
    if (!pgraph_vk_init_buffers(d, errp)) {
        VK_LOG_ERROR("init: buffers FAILED");
        return;
    }
    VK_LOG_ERROR("init: surfaces");
    pgraph_vk_init_surfaces(pg);
    pgraph_vk_surface_image_pool_init(pg->vk_renderer_state);
    VK_LOG_ERROR("init: shaders");
    pgraph_vk_init_shaders(pg);
    VK_LOG_ERROR("init: pipelines");
    pgraph_vk_init_pipelines(pg);
    VK_LOG_ERROR("init: textures");
    pgraph_vk_init_textures(pg);
    VK_LOG_ERROR("init: reports");
    pgraph_vk_init_reports(pg);

    {
        PGRAPHVkState *r = pg->vk_renderer_state;
        if (r->device_props.limits.timestampComputeAndGraphics) {
            VkQueryPoolCreateInfo ts_ci = {
                .sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
                .queryType = VK_QUERY_TYPE_TIMESTAMP,
                .queryCount = GPU_TS_QUERIES_PER_CB * NUM_SUBMIT_FRAMES,
            };
            VK_CHECK(vkCreateQueryPool(r->device, &ts_ci, NULL,
                                       &r->gpu_ts_pool));
            r->gpu_ts_supported = true;
            r->gpu_ts_period_ns = r->device_props.limits.timestampPeriod;
            VK_LOG_ERROR("init: GPU timestamps enabled (period=%.2f ns)",
                         r->gpu_ts_period_ns);
        } else {
            r->gpu_ts_supported = false;
            VK_LOG_ERROR("init: GPU timestamps not supported");
        }
    }

    VK_LOG_ERROR("init: compute");
    pgraph_vk_init_compute(pg);
    VK_LOG_ERROR("init: display");
    pgraph_vk_init_display(pg);

    pg->vk_renderer_state->vram_ram_addr = memory_region_get_ram_addr(d->vram);

    pgraph_vk_update_vertex_ram_buffer(&d->pgraph, 0, d->vram_ptr,
                                   memory_region_size(d->vram));

    pg->vk_renderer_state->frame_staging[0].vertex_ram_initialized = true;

    VK_LOG_ERROR("init: renderer_ready");

}

static void pgraph_vk_finalize(NV2AState *d)
{
    PGRAPHState *pg = &d->pgraph;

    pgraph_vk_render_thread_shutdown(pg->vk_renderer_state);
    pgraph_vk_finalize_display(pg);
    pgraph_vk_finalize_compute(pg);

    {
        PGRAPHVkState *r = pg->vk_renderer_state;
        if (r->gpu_ts_supported) {
            vkDestroyQueryPool(r->device, r->gpu_ts_pool, NULL);
            r->gpu_ts_pool = VK_NULL_HANDLE;
        }
    }

    pgraph_vk_finalize_reports(pg);
    pgraph_vk_finalize_textures(pg);
    pgraph_vk_finalize_pipelines(pg);
    pgraph_vk_finalize_shaders(pg);
    pgraph_vk_finalize_surfaces(pg);
    pgraph_vk_surface_image_pool_drain(pg->vk_renderer_state);
    pgraph_vk_finalize_buffers(d);
    pgraph_vk_finalize_command_buffers(pg);
    pgraph_vk_finalize_instance(pg);

    g_free(pg->vk_renderer_state);
    pg->vk_renderer_state = NULL;

#if HAVE_EXTERNAL_MEMORY
    if (g_gl_context) {
        glo_context_destroy(g_gl_context);
        g_gl_context = NULL;
    }
#endif
}

static void pgraph_vk_process_pending(NV2AState *d)
{
    PGRAPHVkState *r = d->pgraph.vk_renderer_state;

    bool need_downloads = qatomic_read(&r->downloads_pending);
    bool need_dirty_dl = qatomic_read(&r->download_dirty_surfaces_pending);
    bool need_sync = qatomic_read(&d->pgraph.sync_pending);
    bool need_flush = qatomic_read(&d->pgraph.flush_pending);

    if (!need_downloads && !need_dirty_dl && !need_sync && !need_flush) {
        return;
    }

    qemu_mutex_unlock(&d->pfifo.lock);

    if (need_downloads) {
        QemuEvent dl_event;
        qemu_event_init(&dl_event, false);
        RenderCommand *cmd = g_new0(RenderCommand, 1);
        cmd->type = RCMD_PROCESS_DOWNLOADS;
        cmd->download.dirty_surfaces = false;
        cmd->download.completion = &dl_event;
        pgraph_vk_render_thread_enqueue(r, cmd);
        qemu_event_wait(&dl_event);
        qemu_event_destroy(&dl_event);
    }
    if (need_dirty_dl) {
        QemuEvent dl_event;
        qemu_event_init(&dl_event, false);
        RenderCommand *cmd = g_new0(RenderCommand, 1);
        cmd->type = RCMD_PROCESS_DOWNLOADS;
        cmd->download.dirty_surfaces = true;
        cmd->download.completion = &dl_event;
        pgraph_vk_render_thread_enqueue(r, cmd);
        qemu_event_wait(&dl_event);
        qemu_event_destroy(&dl_event);
    }

    if (need_sync) {
        QemuEvent sync_event;
        qemu_event_init(&sync_event, false);
        RenderCommand *cmd = g_new0(RenderCommand, 1);
        cmd->type = RCMD_SYNC_DISPLAY;
        cmd->sync.completion = &sync_event;
        pgraph_vk_render_thread_enqueue(r, cmd);
        qemu_event_wait(&sync_event);
        qemu_event_destroy(&sync_event);
    }
    if (need_flush) {
        QemuEvent flush_event;
        qemu_event_init(&flush_event, false);
        RenderCommand *cmd = g_new0(RenderCommand, 1);
        cmd->type = RCMD_FLUSH;
        cmd->flush_op.completion = &flush_event;
        pgraph_vk_render_thread_enqueue(r, cmd);
        qemu_event_wait(&flush_event);
        qemu_event_destroy(&flush_event);
    }

    qemu_mutex_lock(&d->pfifo.lock);
}

#ifdef __ANDROID__
#define DIAG_LOG(...) __android_log_print(ANDROID_LOG_INFO, "hakuX-diag", __VA_ARGS__)
#else
#define DIAG_LOG(...) fprintf(stderr, "xemu-diag: " __VA_ARGS__)
#endif

static char rt_dump_dir[512] = "";

void nv2a_dbg_set_rt_dump_path(const char *dir)
{
    snprintf(rt_dump_dir, sizeof(rt_dump_dir), "%s", dir);
}

static void dump_surface_ppm(NV2AState *d, SurfaceBinding *surface,
                             const char *path)
{
    unsigned int w = surface->width;
    unsigned int h = surface->height;
    unsigned int bpp = surface->fmt.bytes_per_pixel;
    unsigned int pitch = surface->pitch;

    if (!w || !h || !bpp) {
        return;
    }

    const uint8_t *vram = d->vram_ptr + surface->vram_addr;

    FILE *f = fopen(path, "wb");
    if (!f) {
        DIAG_LOG("dump_surface_ppm: cannot open %s (errno=%d)\n", path, errno);
        return;
    }

    fprintf(f, "P6\n%u %u\n255\n", w, h);

    for (unsigned int y = 0; y < h; y++) {
        const uint8_t *row = vram + y * pitch;
        for (unsigned int x = 0; x < w; x++) {
            uint8_t rgb[3];
            if (bpp == 4) {
                rgb[0] = row[x * 4 + 2];
                rgb[1] = row[x * 4 + 1];
                rgb[2] = row[x * 4 + 0];
            } else if (bpp == 2) {
                uint16_t px = *(const uint16_t *)(row + x * 2);
                rgb[0] = (px >> 8) & 0xF8;
                rgb[1] = (px >> 3) & 0xFC;
                rgb[2] = (px << 3) & 0xF8;
            } else {
                rgb[0] = rgb[1] = rgb[2] = row[x * bpp];
            }
            fwrite(rgb, 1, 3, f);
        }
    }

    fclose(f);
}

/*
 * Multi-frame per-draw-call diagnostic capture
 *
 * Captures N consecutive frames with full render state, shader source,
 * and frame-to-frame diffs. Output is a single JSON session file designed
 * to be fed to Claude for rendering issue analysis.
 */

#include <time.h>

#define DIAG_MAX_DRAWS 4096
#define DIAG_MAX_FRAMES 64
#define DIAG_JSON_INITIAL_CAP (512 * 1024)

static volatile int diag_frame_pending = 0;
static volatile int diag_frame_active = 0;
static int diag_draw_index = 0;
static unsigned int diag_frame_num = 0;

/* Multi-frame state */
static int diag_frames_remaining = 0;
static int diag_total_frames = 0;
static int diag_current_frame_index = 0;
static unsigned int diag_session_id = 0;
static char diag_session_dir[700] = "";

static char *diag_json_buf = NULL;
static size_t diag_json_len = 0;
static size_t diag_json_cap = 0;

/* Per-draw fingerprint for frame diffs */
typedef struct DiagDrawFingerprint {
    uint64_t shader_hash;
    uint64_t state_hash;
} DiagDrawFingerprint;

static DiagDrawFingerprint diag_cur_fingerprints[DIAG_MAX_DRAWS];
static DiagDrawFingerprint diag_prev_fingerprints[DIAG_MAX_DRAWS];
static int diag_prev_draw_count = 0;

/* Diff accumulation buffer */
static char *diag_diff_buf = NULL;
static size_t diag_diff_len = 0;
static size_t diag_diff_cap = 0;
static bool diag_first_diff = true;

/* Shader source dedup table */
#define DIAG_MAX_SHADERS 512
typedef struct DiagShaderEntry {
    uint64_t hash;
    char *vsh_glsl;
    char *psh_glsl;
    char *geom_glsl;
} DiagShaderEntry;

static DiagShaderEntry diag_shaders[DIAG_MAX_SHADERS];
static int diag_shader_count = 0;

/* Per-frame JSON buffers (each frame's draw_calls array) */
static char **diag_frame_bufs = NULL;
static size_t *diag_frame_lens = NULL;
static unsigned int *diag_frame_nums = NULL;
static int *diag_frame_draw_counts = NULL;

void nv2a_dbg_trigger_diag_frames(int num_frames)
{
    if (num_frames < 1) num_frames = 1;
    if (num_frames > DIAG_MAX_FRAMES) num_frames = DIAG_MAX_FRAMES;
    DIAG_LOG("trigger_diag_frames called: num_frames=%d\n", num_frames);
    qatomic_set(&diag_frame_pending, num_frames);
    DIAG_LOG("diag_frame_pending set to %d\n", num_frames);
}

void nv2a_dbg_trigger_diag_frame(void)
{
    DIAG_LOG("trigger_diag_frame called (single frame)\n");
    nv2a_dbg_trigger_diag_frames(1);
}

bool nv2a_dbg_diag_frame_active(void)
{
    return qatomic_read(&diag_frame_active) != 0;
}

bool nv2a_dbg_diag_frame_pending(void)
{
    return qatomic_read(&diag_frame_pending) != 0;
}

static void diag_json_ensure(size_t needed)
{
    if (diag_json_len + needed <= diag_json_cap) {
        return;
    }
    size_t new_cap = diag_json_cap ? diag_json_cap * 2 : DIAG_JSON_INITIAL_CAP;
    while (new_cap < diag_json_len + needed) {
        new_cap *= 2;
    }
    diag_json_buf = g_realloc(diag_json_buf, new_cap);
    diag_json_cap = new_cap;
}

static void diag_json_append(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    va_list ap2;
    va_copy(ap2, ap);
    int n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (n > 0) {
        diag_json_ensure((size_t)n + 1);
        vsnprintf(diag_json_buf + diag_json_len, (size_t)n + 1, fmt, ap2);
        diag_json_len += (size_t)n;
    }
    va_end(ap2);
}

static void diag_diff_ensure(size_t needed)
{
    if (diag_diff_len + needed <= diag_diff_cap) {
        return;
    }
    size_t new_cap = diag_diff_cap ? diag_diff_cap * 2 : 4096;
    while (new_cap < diag_diff_len + needed) {
        new_cap *= 2;
    }
    diag_diff_buf = g_realloc(diag_diff_buf, new_cap);
    diag_diff_cap = new_cap;
}

static void diag_diff_append(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    va_list ap2;
    va_copy(ap2, ap);
    int n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (n > 0) {
        diag_diff_ensure((size_t)n + 1);
        vsnprintf(diag_diff_buf + diag_diff_len, (size_t)n + 1, fmt, ap2);
        diag_diff_len += (size_t)n;
    }
    va_end(ap2);
}

/* JSON-escape a string (handles \n, \t, \\, \", control chars) */
static void diag_json_append_escaped(const char *s)
{
    if (!s) {
        diag_json_append("null");
        return;
    }
    diag_json_append("\"");
    for (const char *p = s; *p; p++) {
        switch (*p) {
        case '"':  diag_json_append("\\\""); break;
        case '\\': diag_json_append("\\\\"); break;
        case '\n': diag_json_append("\\n"); break;
        case '\r': diag_json_append("\\r"); break;
        case '\t': diag_json_append("\\t"); break;
        default:
            if ((unsigned char)*p < 0x20) {
                diag_json_append("\\u%04x", (unsigned char)*p);
            } else {
                diag_json_ensure(2);
                diag_json_buf[diag_json_len++] = *p;
                diag_json_buf[diag_json_len] = '\0';
            }
        }
    }
    diag_json_append("\"");
}

static const char *diag_get_dump_base_dir(void)
{
    const char *dir;
    if (rt_dump_dir[0]) {
        dir = rt_dump_dir;
    } else {
        dir = xemu_settings_get_base_path();
    }
    DIAG_LOG("dump base dir: %s\n", dir ? dir : "(null)");
    return dir;
}

static void diag_init_session(unsigned int start_frame, int num_frames)
{
    DIAG_LOG("init_session: start_frame=%u num_frames=%d\n",
             start_frame, num_frames);

    diag_session_id = start_frame;
    diag_total_frames = num_frames;
    diag_frames_remaining = num_frames;
    diag_current_frame_index = 0;
    diag_shader_count = 0;
    diag_prev_draw_count = 0;
    diag_diff_len = 0;
    diag_first_diff = true;

    /* Create session directory */
    const char *base = diag_get_dump_base_dir();
    snprintf(diag_session_dir, sizeof(diag_session_dir),
             "%s/diag_session_%u", base, diag_session_id);

    /* Recursive mkdir — create all intermediate directories */
    {
        char tmp[700];
        snprintf(tmp, sizeof(tmp), "%s", diag_session_dir);
        for (char *p = tmp + 1; *p; p++) {
            if (*p == '/') {
                *p = '\0';
                int r = mkdir(tmp, 0755);
                DIAG_LOG("mkdir '%s' -> %d (errno=%d)\n", tmp, r,
                         (r < 0 && errno != EEXIST) ? errno : 0);
                *p = '/';
            }
        }
        int r = mkdir(tmp, 0755);
        DIAG_LOG("mkdir '%s' -> %d (errno=%d)\n", tmp, r,
                 (r < 0 && errno != EEXIST) ? errno : 0);
    }

    /* Allocate per-frame buffers */
    diag_frame_bufs = g_new0(char *, num_frames);
    diag_frame_lens = g_new0(size_t, num_frames);
    diag_frame_nums = g_new0(unsigned int, num_frames);
    diag_frame_draw_counts = g_new0(int, num_frames);

    DIAG_LOG("init_session complete, session_dir=%s\n", diag_session_dir);
}

static void diag_cleanup_session(void)
{
    if (diag_frame_bufs) {
        for (int i = 0; i < diag_total_frames; i++) {
            g_free(diag_frame_bufs[i]);
        }
        g_free(diag_frame_bufs);
        diag_frame_bufs = NULL;
    }
    g_free(diag_frame_lens);
    diag_frame_lens = NULL;
    g_free(diag_frame_nums);
    diag_frame_nums = NULL;
    g_free(diag_frame_draw_counts);
    diag_frame_draw_counts = NULL;

    for (int i = 0; i < diag_shader_count; i++) {
        g_free(diag_shaders[i].vsh_glsl);
        g_free(diag_shaders[i].psh_glsl);
        g_free(diag_shaders[i].geom_glsl);
    }
    diag_shader_count = 0;
}

static int diag_find_or_add_shader(uint64_t hash, const char *vsh,
                                    const char *psh, const char *geom)
{
    for (int i = 0; i < diag_shader_count; i++) {
        if (diag_shaders[i].hash == hash) {
            return i;
        }
    }
    if (diag_shader_count >= DIAG_MAX_SHADERS) {
        return -1;
    }
    int idx = diag_shader_count++;
    diag_shaders[idx].hash = hash;
    diag_shaders[idx].vsh_glsl = vsh ? g_strdup(vsh) : NULL;
    diag_shaders[idx].psh_glsl = psh ? g_strdup(psh) : NULL;
    diag_shaders[idx].geom_glsl = geom ? g_strdup(geom) : NULL;
    return idx;
}

static void diag_write_session_json(void)
{
    DIAG_LOG("write_session_json: building JSON, %d frames, %d shaders\n",
             diag_total_frames, diag_shader_count);

    /* Build final JSON into diag_json_buf */
    diag_json_len = 0;

    diag_json_append("{\n");

    /* capture_info */
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    char timestamp[64];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%S", tm);

    diag_json_append(
        "  \"capture_info\": {\n"
        "    \"frame_count\": %d,\n"
        "    \"start_frame\": %u,\n"
        "    \"timestamp\": \"%s\"\n"
        "  },\n",
        diag_total_frames, diag_session_id, timestamp);

    /* shaders */
    diag_json_append("  \"shaders\": {\n");
    for (int i = 0; i < diag_shader_count; i++) {
        diag_json_append("%s    \"0x%016" PRIx64 "\": {\n",
                         i > 0 ? ",\n" : "",
                         diag_shaders[i].hash);
        diag_json_append("      \"vertex\": ");
        diag_json_append_escaped(diag_shaders[i].vsh_glsl);
        diag_json_append(",\n      \"fragment\": ");
        diag_json_append_escaped(diag_shaders[i].psh_glsl);
        diag_json_append(",\n      \"geometry\": ");
        diag_json_append_escaped(diag_shaders[i].geom_glsl);
        diag_json_append("\n    }");
    }
    diag_json_append("\n  },\n");

    /* frames */
    diag_json_append("  \"frames\": [\n");
    for (int i = 0; i < diag_total_frames; i++) {
        diag_json_append(
            "%s    {\n"
            "      \"frame_number\": %u,\n"
            "      \"frame_index\": %d,\n"
            "      \"draw_count\": %d,\n"
            "      \"draw_calls\": [\n",
            i > 0 ? ",\n" : "",
            diag_frame_nums[i], i, diag_frame_draw_counts[i]);
        if (diag_frame_bufs[i] && diag_frame_lens[i] > 0) {
            diag_json_ensure(diag_frame_lens[i]);
            memcpy(diag_json_buf + diag_json_len, diag_frame_bufs[i],
                   diag_frame_lens[i]);
            diag_json_len += diag_frame_lens[i];
        }
        diag_json_append("\n      ]\n    }");
    }
    diag_json_append("\n  ],\n");

    /* frame_diffs */
    diag_json_append("  \"frame_diffs\": [\n");
    if (diag_diff_buf && diag_diff_len > 0) {
        diag_json_ensure(diag_diff_len);
        memcpy(diag_json_buf + diag_json_len, diag_diff_buf, diag_diff_len);
        diag_json_len += diag_diff_len;
    }
    diag_json_append("\n  ]\n}\n");

    /* Write to file */
    char path[800];
    snprintf(path, sizeof(path), "%s/diag_session.json", diag_session_dir);

    DIAG_LOG("writing JSON to: %s\n", path);
    FILE *f = fopen(path, "w");
    if (!f) {
        DIAG_LOG("ERROR: cannot open %s for writing (errno=%d)\n", path, errno);
        return;
    }
    if (diag_json_buf && diag_json_len > 0) {
        size_t written = fwrite(diag_json_buf, 1, diag_json_len, f);
        DIAG_LOG("fwrite: %zu of %zu bytes written\n", written, diag_json_len);
    }
    fclose(f);
    DIAG_LOG("wrote %zu bytes to %s\n", diag_json_len, path);
}

static const char *diag_stencil_op_name(uint32_t op)
{
    switch (op) {
    case 0x1: return "KEEP";
    case 0x2: return "ZERO";
    case 0x3: return "REPLACE";
    case 0x4: return "INCRSAT";
    case 0x5: return "DECRSAT";
    case 0x6: return "INVERT";
    case 0x7: return "INCR";
    case 0x8: return "DECR";
    default:  return "?";
    }
}

static const char *diag_compare_func_name(uint32_t f)
{
    switch (f) {
    case 0: return "NEVER";
    case 1: return "LESS";
    case 2: return "EQUAL";
    case 3: return "LEQUAL";
    case 4: return "GREATER";
    case 5: return "NOTEQUAL";
    case 6: return "GEQUAL";
    case 7: return "ALWAYS";
    default: return "?";
    }
}

static const char *diag_blend_factor_name(uint32_t f)
{
    switch (f) {
    case 0:  return "ZERO";
    case 1:  return "ONE";
    case 2:  return "SRC_COLOR";
    case 3:  return "INV_SRC_COLOR";
    case 4:  return "SRC_ALPHA";
    case 5:  return "INV_SRC_ALPHA";
    case 6:  return "DST_ALPHA";
    case 7:  return "INV_DST_ALPHA";
    case 8:  return "DST_COLOR";
    case 9:  return "INV_DST_COLOR";
    case 10: return "SRC_ALPHA_SAT";
    case 12: return "CONST_COLOR";
    case 13: return "INV_CONST_COLOR";
    case 14: return "CONST_ALPHA";
    case 15: return "INV_CONST_ALPHA";
    default: return "?";
    }
}

static const char *diag_blend_eq_name(uint32_t eq)
{
    switch (eq) {
    case 0: return "SUB";
    case 1: return "REV_SUB";
    case 2: return "ADD";
    case 3: return "MIN";
    case 4: return "MAX";
    case 5: return "REV_SUB";
    case 6: return "ADD";
    default: return "?";
    }
}

static const char *diag_prim_name(int mode)
{
    switch (mode) {
    case 1: return "POINTS";
    case 2: return "LINES";
    case 3: return "LINE_LOOP";
    case 4: return "LINE_STRIP";
    case 5: return "TRIANGLES";
    case 6: return "TRIANGLE_STRIP";
    case 7: return "TRIANGLE_FAN";
    case 8: return "QUADS";
    case 9: return "QUAD_STRIP";
    case 10: return "POLYGON";
    default: return "?";
    }
}

void nv2a_diag_log_clear(NV2AState *d, PGRAPHState *pg,
                          uint32_t parameter,
                          unsigned int xmin, unsigned int ymin,
                          unsigned int xmax, unsigned int ymax,
                          bool write_color, bool write_zeta)
{
    if (!qatomic_read(&diag_frame_active)) {
        return;
    }
    if (diag_draw_index >= DIAG_MAX_DRAWS) {
        return;
    }

    PGRAPHVkState *r = pg->vk_renderer_state;
    int idx = diag_draw_index++;

    DIAG_LOG("log_clear: frame_idx=%d draw=%d rect=(%u,%u)-(%u,%u) color=%d zeta=%d\n",
             diag_current_frame_index, idx, xmin, ymin, xmax, ymax,
             write_color, write_zeta);

    diag_json_append(
        "%s        {\n"
        "          \"draw_index\": %d,\n"
        "          \"type\": \"clear\",\n"
        "          \"clear_color\": %s,\n"
        "          \"clear_zeta\": %s,\n"
        "          \"clear_rect\": {\"xmin\": %u, \"ymin\": %u, "
                                   "\"xmax\": %u, \"ymax\": %u},\n",
        idx > 0 ? ",\n" : "",
        idx,
        write_color ? "true" : "false",
        write_zeta ? "true" : "false",
        xmin, ymin, xmax, ymax
    );

    if (r->color_binding) {
        diag_json_append(
            "          \"color_surface\": {"
                "\"format\": %u, \"width\": %u, \"height\": %u, "
                "\"pitch\": %u, \"bpp\": %u},\n",
            r->color_binding->shape.color_format,
            r->color_binding->width, r->color_binding->height,
            r->color_binding->pitch, r->color_binding->fmt.bytes_per_pixel
        );
    } else {
        diag_json_append("          \"color_surface\": null,\n");
    }

    if (r->zeta_binding) {
        diag_json_append(
            "          \"zeta_surface\": {"
                "\"format\": %u, \"width\": %u, \"height\": %u, "
                "\"pitch\": %u, \"bpp\": %u}\n",
            r->zeta_binding->shape.zeta_format,
            r->zeta_binding->width, r->zeta_binding->height,
            r->zeta_binding->pitch, r->zeta_binding->fmt.bytes_per_pixel
        );
    } else {
        diag_json_append("          \"zeta_surface\": null\n");
    }

    diag_json_append("        }");

    /* Store a fingerprint for diff tracking */
    memset(&diag_cur_fingerprints[idx], 0, sizeof(diag_cur_fingerprints[idx]));
    diag_cur_fingerprints[idx].state_hash = parameter;
}

void nv2a_diag_log_blit(NV2AState *d, PGRAPHState *pg)
{
    if (!qatomic_read(&diag_frame_active)) {
        return;
    }
    if (diag_draw_index >= DIAG_MAX_DRAWS) {
        return;
    }

    int idx = diag_draw_index++;
    ImageBlitState *ib = &pg->image_blit;
    ContextSurfaces2DState *cs = &pg->context_surfaces_2d;

    DIAG_LOG("log_blit: frame_idx=%d draw=%d src=(%u,%u) dst=(%u,%u) size=%ux%u\n",
             diag_current_frame_index, idx,
             ib->in_x, ib->in_y, ib->out_x, ib->out_y,
             ib->width, ib->height);

    diag_json_append(
        "%s        {\n"
        "          \"draw_index\": %d,\n"
        "          \"type\": \"blit\",\n"
        "          \"src\": {\"x\": %u, \"y\": %u},\n"
        "          \"dst\": {\"x\": %u, \"y\": %u},\n"
        "          \"size\": {\"w\": %u, \"h\": %u},\n"
        "          \"color_format\": %u,\n"
        "          \"src_pitch\": %u,\n"
        "          \"dst_pitch\": %u\n"
        "        }",
        idx > 0 ? ",\n" : "",
        idx,
        ib->in_x, ib->in_y,
        ib->out_x, ib->out_y,
        ib->width, ib->height,
        cs->color_format,
        cs->source_pitch,
        cs->dest_pitch
    );

    memset(&diag_cur_fingerprints[idx], 0, sizeof(diag_cur_fingerprints[idx]));
    diag_cur_fingerprints[idx].state_hash = ib->width | ((uint64_t)ib->height << 32);
}

void nv2a_diag_log_draw_call(NV2AState *d, PGRAPHState *pg,
                             const char *type, int count)
{
    if (!qatomic_read(&diag_frame_active)) {
        return;
    }

    if (diag_draw_index >= DIAG_MAX_DRAWS) {
        return;
    }

    PGRAPHVkState *r = pg->vk_renderer_state;
    int idx = diag_draw_index++;

    if (idx == 0 || (idx % 100) == 0) {
        DIAG_LOG("log_draw_call: frame_idx=%d draw=%d type=%s count=%d\n",
                 diag_current_frame_index, idx, type, count);
    }

    uint32_t control_0 = pgraph_vk_reg_r(pg, NV_PGRAPH_CONTROL_0);
    uint32_t control_1 = pgraph_vk_reg_r(pg, NV_PGRAPH_CONTROL_1);
    uint32_t control_2 = pgraph_vk_reg_r(pg, NV_PGRAPH_CONTROL_2);
    uint32_t blend_reg = pgraph_vk_reg_r(pg, NV_PGRAPH_BLEND);
    uint32_t setupraster = pgraph_vk_reg_r(pg, NV_PGRAPH_SETUPRASTER);

    bool blend_en    = blend_reg & NV_PGRAPH_BLEND_EN;
    uint32_t sfactor = GET_MASK(blend_reg, NV_PGRAPH_BLEND_SFACTOR);
    uint32_t dfactor = GET_MASK(blend_reg, NV_PGRAPH_BLEND_DFACTOR);
    uint32_t blend_eq = GET_MASK(blend_reg, NV_PGRAPH_BLEND_EQN);

    bool depth_test  = control_0 & NV_PGRAPH_CONTROL_0_ZENABLE;
    bool depth_write = !!(control_0 & NV_PGRAPH_CONTROL_0_ZWRITEENABLE);
    uint32_t zfunc   = GET_MASK(control_0, NV_PGRAPH_CONTROL_0_ZFUNC);

    bool stencil_test = control_1 & NV_PGRAPH_CONTROL_1_STENCIL_TEST_ENABLE;
    uint32_t stencil_func = GET_MASK(control_1, NV_PGRAPH_CONTROL_1_STENCIL_FUNC);
    uint32_t stencil_ref  = GET_MASK(control_1, NV_PGRAPH_CONTROL_1_STENCIL_REF);
    uint32_t stencil_mask_read  = GET_MASK(control_1,
                                           NV_PGRAPH_CONTROL_1_STENCIL_MASK_READ);
    uint32_t stencil_mask_write = GET_MASK(control_1,
                                           NV_PGRAPH_CONTROL_1_STENCIL_MASK_WRITE);
    uint32_t op_fail  = GET_MASK(control_2, NV_PGRAPH_CONTROL_2_STENCIL_OP_FAIL);
    uint32_t op_zfail = GET_MASK(control_2, NV_PGRAPH_CONTROL_2_STENCIL_OP_ZFAIL);
    uint32_t op_zpass = GET_MASK(control_2, NV_PGRAPH_CONTROL_2_STENCIL_OP_ZPASS);

    bool mask_r = !!(control_0 & NV_PGRAPH_CONTROL_0_RED_WRITE_ENABLE);
    bool mask_g = !!(control_0 & NV_PGRAPH_CONTROL_0_GREEN_WRITE_ENABLE);
    bool mask_b = !!(control_0 & NV_PGRAPH_CONTROL_0_BLUE_WRITE_ENABLE);
    bool mask_a = !!(control_0 & NV_PGRAPH_CONTROL_0_ALPHA_WRITE_ENABLE);

    bool cull_en = !!(setupraster & NV_PGRAPH_SETUPRASTER_CULLENABLE);
    uint32_t cull_face = GET_MASK(setupraster, NV_PGRAPH_SETUPRASTER_CULLCTRL);
    bool front_ccw = !!(setupraster & NV_PGRAPH_SETUPRASTER_FRONTFACE);

    const char *cull_str = "NONE";
    if (cull_en) {
        switch (cull_face) {
        case 1: cull_str = "FRONT"; break;
        case 2: cull_str = "BACK";  break;
        case 3: cull_str = "BOTH";  break;
        }
    }

    /* Compute shader hash and capture source */
    uint64_t shader_hash = 0;
    if (r->shader_binding) {
        shader_hash = fast_hash((const uint8_t *)&r->shader_binding->state,
                                sizeof(ShaderState));
        const char *vsh_src = (r->shader_binding->vsh.module_info &&
                               r->shader_binding->vsh.module_info->glsl)
                              ? r->shader_binding->vsh.module_info->glsl : NULL;
        const char *psh_src = (r->shader_binding->psh.module_info &&
                               r->shader_binding->psh.module_info->glsl)
                              ? r->shader_binding->psh.module_info->glsl : NULL;
        const char *geom_src = (r->shader_binding->geom.module_info &&
                                r->shader_binding->geom.module_info->glsl)
                               ? r->shader_binding->geom.module_info->glsl : NULL;
        diag_find_or_add_shader(shader_hash, vsh_src, psh_src, geom_src);
    }

    /* Compute state fingerprint for diff detection.
     * Must memset to zero to avoid padding bytes causing false diffs. */
    struct {
        uint32_t control_0, control_1, control_2, blend_reg, setupraster;
        uint32_t color_fmt, zeta_fmt;
        uint32_t color_w, color_h, zeta_w, zeta_h;
        uint32_t tex_fmt[NV2A_MAX_TEXTURES];
        int prim_mode;
    } state_block;
    memset(&state_block, 0, sizeof(state_block));
    state_block.control_0 = control_0;
    state_block.control_1 = control_1;
    state_block.control_2 = control_2;
    state_block.blend_reg = blend_reg;
    state_block.setupraster = setupraster;
    state_block.color_fmt = r->color_binding ? r->color_binding->shape.color_format : 0;
    state_block.zeta_fmt = r->zeta_binding ? r->zeta_binding->shape.zeta_format : 0;
    state_block.color_w = r->color_binding ? r->color_binding->width : 0;
    state_block.color_h = r->color_binding ? r->color_binding->height : 0;
    state_block.zeta_w = r->zeta_binding ? r->zeta_binding->width : 0;
    state_block.zeta_h = r->zeta_binding ? r->zeta_binding->height : 0;
    state_block.prim_mode = pg->primitive_mode;
    for (int i = 0; i < NV2A_MAX_TEXTURES; i++) {
        state_block.tex_fmt[i] = pgraph_vk_reg_r(pg, NV_PGRAPH_TEXFMT0 + i * 4);
    }
    diag_cur_fingerprints[idx].shader_hash = shader_hash;
    diag_cur_fingerprints[idx].state_hash =
        fast_hash((const uint8_t *)&state_block, sizeof(state_block));

    diag_json_append(
        "%s        {\n"
        "          \"draw_index\": %d,\n"
        "          \"type\": \"%s\",\n"
        "          \"count\": %d,\n"
        "          \"primitive_mode\": \"%s\",\n"
        "          \"shader_hash\": \"0x%016" PRIx64 "\",\n"
        "          \"blend\": {"
            "\"enabled\": %s, \"src\": \"%s\", \"dst\": \"%s\", "
            "\"eq\": \"%s\"},\n"
        "          \"depth\": {"
            "\"test\": %s, \"write\": %s, \"func\": \"%s\"},\n"
        "          \"stencil\": {"
            "\"test\": %s, \"func\": \"%s\", \"ref\": %u, "
            "\"mask_read\": %u, \"mask_write\": %u, "
            "\"op_fail\": \"%s\", \"op_zfail\": \"%s\", "
            "\"op_zpass\": \"%s\"},\n"
        "          \"color_write_mask\": {"
            "\"r\": %s, \"g\": %s, \"b\": %s, \"a\": %s},\n"
        "          \"cull\": {"
            "\"enabled\": %s, \"face\": \"%s\", \"front_ccw\": %s},\n",
        idx > 0 ? ",\n" : "",
        idx,
        type,
        count,
        diag_prim_name(pg->primitive_mode),
        shader_hash,
        blend_en ? "true" : "false",
        diag_blend_factor_name(sfactor),
        diag_blend_factor_name(dfactor),
        diag_blend_eq_name(blend_eq),
        depth_test ? "true" : "false",
        depth_write ? "true" : "false",
        diag_compare_func_name(zfunc),
        stencil_test ? "true" : "false",
        diag_compare_func_name(stencil_func),
        stencil_ref, stencil_mask_read, stencil_mask_write,
        diag_stencil_op_name(op_fail),
        diag_stencil_op_name(op_zfail),
        diag_stencil_op_name(op_zpass),
        mask_r ? "true" : "false",
        mask_g ? "true" : "false",
        mask_b ? "true" : "false",
        mask_a ? "true" : "false",
        cull_en ? "true" : "false",
        cull_str,
        front_ccw ? "true" : "false"
    );

    if (r->color_binding) {
        diag_json_append(
            "          \"color_surface\": {"
                "\"format\": %u, \"width\": %u, \"height\": %u, "
                "\"pitch\": %u, \"bpp\": %u},\n",
            r->color_binding->shape.color_format,
            r->color_binding->width, r->color_binding->height,
            r->color_binding->pitch, r->color_binding->fmt.bytes_per_pixel
        );
    } else {
        diag_json_append("          \"color_surface\": null,\n");
    }

    if (r->zeta_binding) {
        diag_json_append(
            "          \"zeta_surface\": {"
                "\"format\": %u, \"width\": %u, \"height\": %u, "
                "\"pitch\": %u, \"bpp\": %u},\n",
            r->zeta_binding->shape.zeta_format,
            r->zeta_binding->width, r->zeta_binding->height,
            r->zeta_binding->pitch, r->zeta_binding->fmt.bytes_per_pixel
        );
    } else {
        diag_json_append("          \"zeta_surface\": null,\n");
    }

    diag_json_append("          \"textures\": [");
    for (int i = 0; i < NV2A_MAX_TEXTURES; i++) {
        bool tex_en = pgraph_is_texture_enabled(pg, i);
        uint32_t tex_fmt = pgraph_vk_reg_r(pg, NV_PGRAPH_TEXFMT0 + i * 4);
        unsigned int color_format = GET_MASK(tex_fmt, NV_PGRAPH_TEXFMT0_COLOR);
        unsigned int dimensionality = GET_MASK(tex_fmt,
                                               NV_PGRAPH_TEXFMT0_DIMENSIONALITY);
        unsigned int log_w = GET_MASK(tex_fmt, NV_PGRAPH_TEXFMT0_BASE_SIZE_U);
        unsigned int log_h = GET_MASK(tex_fmt, NV_PGRAPH_TEXFMT0_BASE_SIZE_V);
        uint32_t tex_addr = pgraph_vk_reg_r(pg, NV_PGRAPH_TEXADDRESS0 + i * 4);
        unsigned int addru = GET_MASK(tex_addr, NV_PGRAPH_TEXADDRESS0_ADDRU);
        unsigned int addrv = (tex_addr >> 8) & 0x7;
        uint32_t border_color = pgraph_vk_reg_r(pg,
                                    NV_PGRAPH_BORDERCOLOR0 + i * 4);

        static const char *addr_names[] = {
            "?", "WRAP", "MIRROR", "CLAMP_TO_EDGE", "BORDER", "CLAMP_OGL"
        };
        const char *addru_name = addru < 6 ? addr_names[addru] : "?";
        const char *addrv_name = addrv < 6 ? addr_names[addrv] : "?";

        diag_json_append(
            "%s{\"stage\": %d, \"enabled\": %s, \"color_format\": %u, "
            "\"dim\": %u, \"width\": %u, \"height\": %u, "
            "\"addru\": \"%s\", \"addrv\": \"%s\", "
            "\"border_color\": \"0x%08x\"}",
            i > 0 ? ", " : "",
            i,
            tex_en ? "true" : "false",
            color_format,
            dimensionality,
            1u << log_w, 1u << log_h,
            addru_name, addrv_name,
            border_color
        );
    }
    diag_json_append("],\n");

    /* Per-draw surface dumps */
    pgraph_vk_finish(pg, VK_FINISH_REASON_SURFACE_DOWN);

    {
        char color_fname[128] = "";
        char depth_fname[128] = "";

        if (r->color_binding && r->color_binding->draw_dirty) {
            pgraph_vk_surface_download_if_dirty(d, r->color_binding);
            snprintf(color_fname, sizeof(color_fname),
                     "f%d_draw%d_color.ppm", diag_current_frame_index, idx);
            char path[800];
            snprintf(path, sizeof(path), "%s/%s", diag_session_dir, color_fname);
            dump_surface_ppm(d, r->color_binding, path);
        }

        if (r->zeta_binding && r->zeta_binding->draw_dirty) {
            pgraph_vk_surface_download_if_dirty(d, r->zeta_binding);
            snprintf(depth_fname, sizeof(depth_fname),
                     "f%d_draw%d_depth.ppm", diag_current_frame_index, idx);
            char path[800];
            snprintf(path, sizeof(path), "%s/%s", diag_session_dir, depth_fname);
            dump_surface_ppm(d, r->zeta_binding, path);
        }

        if (color_fname[0]) {
            diag_json_append("          \"color_image\": \"%s\",\n", color_fname);
        } else {
            diag_json_append("          \"color_image\": null,\n");
        }
        if (depth_fname[0]) {
            diag_json_append("          \"depth_image\": \"%s\"\n", depth_fname);
        } else {
            diag_json_append("          \"depth_image\": null\n");
        }
    }

    diag_json_append("        }");
}

static void pgraph_vk_flip_stall(NV2AState *d)
{
    if (qatomic_read(&diag_frame_pending) || qatomic_read(&diag_frame_active)) {
        DIAG_LOG("flip_stall: ENTER pending=%d active=%d\n",
                 qatomic_read(&diag_frame_pending),
                 qatomic_read(&diag_frame_active));
    }
#ifdef XBOX
    {
        extern volatile int32_t *xbox_ram_fp_active_ptr;
        extern uintptr_t *xbox_ram_fp_vram_base_ptr;
        if (xbox_ram_fp_active_ptr &&
            !qatomic_read(xbox_ram_fp_active_ptr)) {
            if (xbox_ram_fp_vram_base_ptr) {
                pcibus_t bar1 = pci_get_bar_addr(PCI_DEVICE(d), 1);
                *xbox_ram_fp_vram_base_ptr = (uintptr_t)bar1;
                error_report("[TLB-FP] vram_pci_base=0x%lx (BAR1)",
                             (unsigned long)bar1);
            }
            qatomic_set(xbox_ram_fp_active_ptr, 1);
            error_report("[TLB-FP] activated after first flip");
        }
    }
#endif
    if (qatomic_read(&diag_frame_pending) ||
        qatomic_read(&diag_frame_active)) {
        PGRAPHVkState *r = d->pgraph.vk_renderer_state;
        DBG_LOG("[FLIP] flip_stall: in_cb=%d frame=%d submit=%d",
                r->in_command_buffer, r->current_frame,
                (int)r->submit_count);
    }
    /*
     * Pre-record framebuffer download into the current command buffer before
     * the flip stall finish submits it. This piggybacks the surface-to-staging
     * copy onto the same GPU submission as the frame's draws, eliminating a
     * separate SURFACE_DOWN finish (one fewer vkQueueSubmit + fence cycle).
     */
    pgraph_vk_prerecord_display_download(d);

    pgraph_vk_finish(&d->pgraph, VK_FINISH_REASON_FLIP_STALL);

    {
        PGRAPHVkState *r = d->pgraph.vk_renderer_state;
        r->frame_was_skipped = r->frame_skip_active;
        if (r->frame_skip_active) {
            r->blend_after_skip = true;
        }

        if (xemu_get_frame_skip() && d->defer_count >= 6) {
            int dc = d->defer_count;
            int skip_pattern;
            if (dc >= 21)      skip_pattern = 3;
            else if (dc >= 16) skip_pattern = 2;
            else if (dc >= 11) skip_pattern = 1;
            else               skip_pattern = 0;

            r->skip_counter++;
            switch (skip_pattern) {
            case 0: r->frame_skip_active = (r->skip_counter % 4) == 0; break; /* 25% */
            case 1: r->frame_skip_active = (r->skip_counter % 5) == 0; break; /* 20% */
            case 2: r->frame_skip_active = (r->skip_counter % 3) == 0; break; /* 33% */
            case 3: r->frame_skip_active = (r->skip_counter % 2) == 0; break; /* 50% */
            default: r->frame_skip_active = false; break;
            }
        } else {
            r->frame_skip_active = false;
            r->skip_counter = 0;
        }
    }

    if (qatomic_read(&diag_frame_active)) {
        DIAG_LOG("flip_stall: diag active, frame_idx=%d/%d, %d draws captured, json_len=%zu\n",
                 diag_current_frame_index, diag_total_frames,
                 diag_draw_index, diag_json_len);
        /* Save current frame's draw call data */
        int fi = diag_current_frame_index;
        if (fi < diag_total_frames) {
            diag_frame_bufs[fi] = diag_json_buf ? g_strndup(diag_json_buf, diag_json_len) : NULL;
            diag_frame_lens[fi] = diag_json_len;
            diag_frame_nums[fi] = diag_frame_num;
            diag_frame_draw_counts[fi] = diag_draw_index;

            /* Dump final framebuffer PPM for this frame */
            PGRAPHVkState *diag_r = d->pgraph.vk_renderer_state;
            pgraph_vk_finish(&d->pgraph, VK_FINISH_REASON_SURFACE_DOWN);
            if (diag_r->color_binding && diag_r->color_binding->draw_dirty) {
                pgraph_vk_surface_download_if_dirty(d, diag_r->color_binding);
                char ppm_path[800];
                snprintf(ppm_path, sizeof(ppm_path),
                         "%s/frame_%d_final_color.ppm", diag_session_dir, fi);
                dump_surface_ppm(d, diag_r->color_binding, ppm_path);
            }
            if (diag_r->zeta_binding && diag_r->zeta_binding->draw_dirty) {
                pgraph_vk_surface_download_if_dirty(d, diag_r->zeta_binding);
                char ppm_path[800];
                snprintf(ppm_path, sizeof(ppm_path),
                         "%s/frame_%d_final_depth.ppm", diag_session_dir, fi);
                dump_surface_ppm(d, diag_r->zeta_binding, ppm_path);
            }

            /* Compute frame diff against previous frame */
            if (fi > 0) {
                int max_draws = diag_draw_index > diag_prev_draw_count
                                ? diag_draw_index : diag_prev_draw_count;
                int changed_count = 0;
                char changed_list[4096] = "";
                size_t cl_len = 0;
                bool draw_count_changed =
                    (diag_draw_index != diag_prev_draw_count);

                for (int di = 0; di < max_draws; di++) {
                    bool changed = false;
                    if (di >= diag_draw_index || di >= diag_prev_draw_count) {
                        changed = true;
                    } else if (diag_cur_fingerprints[di].shader_hash !=
                                   diag_prev_fingerprints[di].shader_hash ||
                               diag_cur_fingerprints[di].state_hash !=
                                   diag_prev_fingerprints[di].state_hash) {
                        changed = true;
                    }
                    if (changed) {
                        changed_count++;
                        int n = snprintf(changed_list + cl_len,
                                         sizeof(changed_list) - cl_len,
                                         "%s%d", cl_len > 0 ? ", " : "", di);
                        if (n > 0 && cl_len + n < sizeof(changed_list)) {
                            cl_len += n;
                        }
                    }
                }

                diag_diff_append(
                    "%s    {\n"
                    "      \"from_frame\": %d,\n"
                    "      \"to_frame\": %d,\n"
                    "      \"draw_count_changed\": %s,\n"
                    "      \"from_draw_count\": %d,\n"
                    "      \"to_draw_count\": %d,\n"
                    "      \"changed_draw_count\": %d,\n"
                    "      \"draws_with_state_changes\": [%s],\n"
                    "      \"summary\": \"%d draw(s) changed state out of %d total\"\n"
                    "    }",
                    diag_first_diff ? "" : ",\n",
                    fi - 1, fi,
                    draw_count_changed ? "true" : "false",
                    diag_prev_draw_count, diag_draw_index,
                    changed_count, changed_list,
                    changed_count, max_draws);
                diag_first_diff = false;
            }

            /* Save current fingerprints as previous for next frame */
            memcpy(diag_prev_fingerprints, diag_cur_fingerprints,
                   sizeof(DiagDrawFingerprint) * diag_draw_index);
            diag_prev_draw_count = diag_draw_index;
        }

        diag_current_frame_index++;
        diag_frames_remaining--;

        DIAG_LOG("frame %u captured (%d/%d, %d draw calls)\n",
                 diag_frame_num, diag_current_frame_index, diag_total_frames,
                 diag_draw_index);

        if (diag_frames_remaining <= 0) {
            /* All frames captured — write session JSON and clean up */
            DIAG_LOG("all frames done, writing session JSON...\n");
            diag_write_session_json();
            diag_cleanup_session();
            qatomic_set(&diag_frame_active, 0);
            DIAG_LOG("session %u complete\n", diag_session_id);
        } else {
            /* Reset for next frame */
            DIAG_LOG("resetting for next frame, %d remaining\n",
                     diag_frames_remaining);
            diag_draw_index = 0;
            diag_json_len = 0;
            diag_frame_num = g_nv2a_stats.frame_count;
        }
    }

    {
        int pending = qatomic_read(&diag_frame_pending);
        if (pending > 0) {
            DIAG_LOG("flip_stall: pending=%d, starting capture\n", pending);
            qatomic_set(&diag_frame_pending, 0);
            diag_frame_num = g_nv2a_stats.frame_count;
            diag_draw_index = 0;
            diag_json_len = 0;
            diag_init_session(diag_frame_num, pending);
            qatomic_set(&diag_frame_active, 1);
            DIAG_LOG("capture started: %d frames at frame %u\n",
                     pending, diag_frame_num);
        }
    }

    pgraph_vk_debug_frame_terminator();
}

static void pgraph_vk_pre_savevm_trigger(NV2AState *d)
{
    qatomic_set(&d->pgraph.vk_renderer_state->download_dirty_surfaces_pending, true);
    qemu_event_reset(&d->pgraph.vk_renderer_state->dirty_surfaces_download_complete);
}

static void pgraph_vk_pre_savevm_wait(NV2AState *d)
{
    qemu_event_wait(&d->pgraph.vk_renderer_state->dirty_surfaces_download_complete);
}

static void pgraph_vk_pre_shutdown_trigger(NV2AState *d)
{
    // qatomic_set(&d->pgraph.vk_renderer_state->shader_cache_writeback_pending, true);
    // qemu_event_reset(&d->pgraph.vk_renderer_state->shader_cache_writeback_complete);
}

static void pgraph_vk_pre_shutdown_wait(NV2AState *d)
{
    // qemu_event_wait(&d->pgraph.vk_renderer_state->shader_cache_writeback_complete);   
}

static int pgraph_vk_get_framebuffer_surface(NV2AState *d)
{
    PGRAPHState *pg = &d->pgraph;
    PGRAPHVkState *r = pg->vk_renderer_state;

    qemu_mutex_lock(&d->pfifo.lock);

    VGADisplayParams vga_display_params;
    d->vga.get_params(&d->vga, &vga_display_params);

    SurfaceBinding *surface = pgraph_vk_surface_get_within(
        d, d->pcrtc.start + vga_display_params.line_offset);
    if (surface == NULL || !surface->color) {
        qemu_mutex_unlock(&d->pfifo.lock);
        return 0;
    }

    assert(surface->color);

    surface->frame_time = pg->frame_time;

#if HAVE_EXTERNAL_MEMORY
    if (r->display.use_external_memory) {
        DisplayImage *ready = &r->display.images[r->display.display_idx];
        if (ready->valid) {
            if (ready->fence_submitted) {
                VK_CHECK(vkWaitForFences(r->device, 1, &ready->fence,
                                         VK_TRUE, UINT64_MAX));
                ready->fence_submitted = false;
            }
            int tex = ready->gl_texture_id;

            qemu_event_reset(&d->pgraph.sync_complete);
            qatomic_set(&pg->sync_pending, true);
            pfifo_kick(d);
            qemu_mutex_unlock(&d->pfifo.lock);
            return tex;
        }
        qemu_event_reset(&d->pgraph.sync_complete);
        qatomic_set(&pg->sync_pending, true);
        pfifo_kick(d);
        qemu_mutex_unlock(&d->pfifo.lock);
        qemu_event_wait(&d->pgraph.sync_complete);
        ready = &r->display.images[r->display.display_idx];
        if (ready->valid && ready->fence_submitted) {
            VK_CHECK(vkWaitForFences(r->device, 1, &ready->fence,
                                     VK_TRUE, UINT64_MAX));
            ready->fence_submitted = false;
        }
        return ready->valid ? ready->gl_texture_id : 0;
    }
    qemu_mutex_unlock(&d->pfifo.lock);
    pgraph_vk_wait_for_surface_download(surface);
    return 0;
#else
    qemu_mutex_unlock(&d->pfifo.lock);
    pgraph_vk_wait_for_surface_download(surface);
    return 0;
#endif
}

static PGRAPHRenderer pgraph_vk_renderer = {
    .type = CONFIG_DISPLAY_RENDERER_VULKAN,
    .name = "Vulkan",
    .ops = {
        .init = pgraph_vk_init,
        .early_context_init = early_context_init,
        .finalize = pgraph_vk_finalize,
        .clear_report_value = pgraph_vk_clear_report_value,
        .clear_surface = pgraph_vk_clear_surface,
        .draw_begin = pgraph_vk_draw_begin,
        .draw_end = pgraph_vk_draw_end,
        .flip_stall = pgraph_vk_flip_stall,
        .flush_draw = pgraph_vk_flush_draw,
        .get_report = pgraph_vk_get_report,
        .image_blit = pgraph_vk_image_blit,
        .pre_savevm_trigger = pgraph_vk_pre_savevm_trigger,
        .pre_savevm_wait = pgraph_vk_pre_savevm_wait,
        .pre_shutdown_trigger = pgraph_vk_pre_shutdown_trigger,
        .pre_shutdown_wait = pgraph_vk_pre_shutdown_wait,
        .process_pending = pgraph_vk_process_pending,
        .process_pending_reports = pgraph_vk_process_pending_reports,
        .surface_update = pgraph_vk_surface_update,
        .set_surface_scale_factor = pgraph_vk_set_surface_scale_factor,
        .get_surface_scale_factor = pgraph_vk_get_surface_scale_factor,
        .get_framebuffer_surface = pgraph_vk_get_framebuffer_surface,
    }
};

static void __attribute__((constructor)) register_renderer(void)
{
    pgraph_renderer_register(&pgraph_vk_renderer);
}

void pgraph_vk_force_register(void)
{
    static bool registered = false;
    if (registered) {
        return;
    }
    pgraph_renderer_register(&pgraph_vk_renderer);
    registered = true;
}

void pgraph_vk_check_memory_budget(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    VkPhysicalDeviceMemoryProperties const *props;
    vmaGetMemoryProperties(r->allocator, &props);

    g_autofree VmaBudget *budgets = g_malloc_n(props->memoryHeapCount, sizeof(VmaBudget));
    vmaGetHeapBudgets(r->allocator, budgets);

#ifdef __ANDROID__
    const float budget_threshold = 0.6;
#else
    const float budget_threshold = 0.8;
#endif
    bool near_budget = false;

    for (uint32_t i = 0; i < props->memoryHeapCount; i++) {
        VmaBudget *b = &budgets[i];
        if (b->budget == 0) {
            continue;
        }
        float use_to_budget_ratio =
            (double)b->statistics.allocationBytes / (double)b->budget;
        near_budget |= use_to_budget_ratio > budget_threshold;
    }

    if (near_budget) {
        pgraph_vk_trim_texture_cache(pg);
    }
}

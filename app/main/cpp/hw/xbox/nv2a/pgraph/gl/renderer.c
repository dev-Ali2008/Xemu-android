/*
 * Geforce NV2A PGRAPH OpenGL Renderer
 *
 * Copyright (c) 2012 espes
 * Copyright (c) 2015 Jannik Vogel
 * Copyright (c) 2018-2025 Matt Borgerson
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
#include "hw/xbox/nv2a/pgraph/pgraph.h"
#include "debug.h"
#include "renderer.h"

#ifdef __ANDROID__
#include <android/log.h>
#endif

GloContext *g_nv2a_context_render;
GloContext *g_nv2a_context_display;

#ifdef __ANDROID__
static bool gl_extension_list_has(const char *exts, const char *ext)
{
    if (!exts || !ext || !*ext) {
        return false;
    }
    size_t len = strlen(ext);
    const char *p = exts;
    while ((p = strstr(p, ext)) != NULL) {
        if ((p == exts || p[-1] == ' ') && (p[len] == '\0' || p[len] == ' ')) {
            return true;
        }
        p += len;
    }
    return false;
}

static bool gl_extension_supported(const char *exts, const char *ext)
{
    return gl_extension_list_has(exts, ext) || glo_check_extension(ext);
}
#endif

static void early_context_init(void)
{
#ifdef __ANDROID__
    __android_log_print(ANDROID_LOG_INFO, "hakuX",
                        "early_context_init: start");
    g_nv2a_context_display = glo_context_wrap_current();
    if (!g_nv2a_context_display) {
        __android_log_print(ANDROID_LOG_ERROR, "hakuX",
                            "early_context_init: no current GL context");
        fprintf(stderr, "Warning: Failed to wrap current GL context\n");
        return;
    }
    __android_log_print(ANDROID_LOG_INFO, "hakuX",
                        "early_context_init: wrapped display context (%p)",
                        (void *)g_nv2a_context_display);

    // Create render context NOW on main thread to avoid EGL threading issues
    __android_log_print(ANDROID_LOG_INFO, "hakuX",
                        "early_context_init: creating render GL context");
    g_nv2a_context_render = glo_context_create();
    if (!g_nv2a_context_render) {
        __android_log_print(ANDROID_LOG_ERROR, "hakuX",
                            "early_context_init: failed to create render GL context");
    } else {
        __android_log_print(ANDROID_LOG_INFO, "hakuX",
                            "early_context_init: created render GL context (%p)",
                            (void *)g_nv2a_context_render);
    }

    // Restore display context as current on this thread
    __android_log_print(ANDROID_LOG_INFO, "hakuX",
                        "early_context_init: restoring display context as current");
    glo_set_current(g_nv2a_context_display);

    __android_log_print(ANDROID_LOG_INFO, "hakuX",
                        "early_context_init: done");
    return;
#endif
    g_nv2a_context_render = glo_context_create();
    g_nv2a_context_display = glo_context_create();

    glo_set_current(g_nv2a_context_display);
}

static void pgraph_gl_init(NV2AState *d, Error **errp)
{
    PGRAPHState *pg = &d->pgraph;

    pg->gl_renderer_state = g_malloc0(sizeof(*pg->gl_renderer_state));
    PGRAPHGLState *r = pg->gl_renderer_state;

    /* fire up opengl */
    __android_log_print(ANDROID_LOG_INFO, "hakuX",
                        "pgraph_gl_init: start");
#ifdef __ANDROID__
    __android_log_print(ANDROID_LOG_INFO, "hakuX",
                        "pgraph_gl_init: g_nv2a_context_render=%p",
                        (void *)g_nv2a_context_render);
    if (!g_nv2a_context_render) {
        __android_log_print(ANDROID_LOG_INFO, "hakuX",
                            "pgraph_gl_init: creating render GL context (deferred)");
        g_nv2a_context_render = glo_context_create();
        if (!g_nv2a_context_render) {
            error_setg(errp, "Failed to create render GL context");
            return;
        }
        __android_log_print(ANDROID_LOG_INFO, "hakuX",
                            "pgraph_gl_init: created render GL context (%p)",
                            (void *)g_nv2a_context_render);
    } else {
        __android_log_print(ANDROID_LOG_INFO, "hakuX",
                            "pgraph_gl_init: using existing render GL context");
    }
#endif
    __android_log_print(ANDROID_LOG_INFO, "hakuX",
                        "pgraph_gl_init: setting render context current");
    glo_set_current(g_nv2a_context_render);
    __android_log_print(ANDROID_LOG_INFO, "hakuX",
                        "pgraph_gl_init: render context is current");

#ifdef __ANDROID__
    const char *exts = (const char *)glGetString(GL_EXTENSIONS);
    r->bgra_supported = gl_extension_supported(exts, "GL_EXT_texture_format_BGRA8888") ||
                        gl_extension_supported(exts, "GL_OES_texture_format_BGRA8888") ||
                        gl_extension_supported(exts, "GL_EXT_texture_format_BGRA8888_OES");
    r->geometry_shaders_supported =
        gl_extension_supported(exts, "GL_EXT_geometry_shader") ||
        gl_extension_supported(exts, "GL_OES_geometry_shader");

    /* Detect GLSL ES version from GL_SHADING_LANGUAGE_VERSION.
     * Format: "OpenGL ES GLSL ES X.YZ" — we want X*100 + Y*10. */
    r->gles_version = 300; /* safe default */
    const char *glsl_ver = (const char *)glGetString(GL_SHADING_LANGUAGE_VERSION);
    if (glsl_ver) {
        int major = 0, minor = 0;
        /* Try to parse "X.Y" from the version string */
        const char *p = glsl_ver;
        while (*p && !(*p >= '0' && *p <= '9')) p++; /* skip to first digit */
        if (*p) {
            major = *p - '0'; p++;
            if (*p == '.') { p++; if (*p >= '0' && *p <= '9') minor = *p - '0'; }
            if (major >= 3) {
                r->gles_version = major * 100 + minor * 10;
            }
        }
    }
    __android_log_print(ANDROID_LOG_INFO, "hakuX",
                        "pgraph_gl_init: bgra=%s geom_shader=%s glsl_es=%d (%s)",
                        r->bgra_supported ? "yes" : "no",
                        r->geometry_shaders_supported ? "yes" : "no",
                        r->gles_version,
                        glsl_ver ? glsl_ver : "unknown");
#endif

#if DEBUG_NV2A_GL
    gl_debug_initialize();
#endif

#ifdef __ANDROID__
    /* DXT textures may be available via extension on Android. */
    if (!glo_check_extension("GL_EXT_texture_compression_s3tc")) {
        fprintf(stderr, "Warning: GL_EXT_texture_compression_s3tc not available\n");
    }
#else
    /* DXT textures */
    assert(glo_check_extension("GL_EXT_texture_compression_s3tc"));
    /*  Internal RGB565 texture format */
    assert(glo_check_extension("GL_ARB_ES2_compatibility"));
#endif

#ifndef __ANDROID__ /* GL_SMOOTH_LINE_WIDTH_RANGE not valid in GLES 3.x */
    glGetFloatv(GL_SMOOTH_LINE_WIDTH_RANGE, r->supported_smooth_line_width_range);
#else
    r->supported_smooth_line_width_range[0] = 1.0f;
    r->supported_smooth_line_width_range[1] = 1.0f;
#endif
    glGetFloatv(GL_ALIASED_LINE_WIDTH_RANGE, r->supported_aliased_line_width_range);

    pgraph_gl_init_surfaces(pg);
    pgraph_gl_init_reports(d);
    pgraph_gl_init_textures(d);
    pgraph_gl_init_buffers(d);
    pgraph_gl_init_shaders(pg);
    pgraph_gl_init_display(d);

    pgraph_gl_update_entire_memory_buffer(d);

    pg->uniform_attrs = 0;
    pg->swizzle_attrs = 0;

    r->supported_extensions.texture_filter_anisotropic =
        glo_check_extension("GL_EXT_texture_filter_anisotropic");
    r->supported_extensions.texture_border_clamp = true;
    r->supported_extensions.texture_lod_bias = true;
    r->supported_extensions.occlusion_query_boolean = false;
    r->supported_extensions.max_texture_max_anisotropy = 1.0f;
#ifdef __ANDROID__
    r->supported_extensions.texture_border_clamp =
        NV2A_GL_CLAMP_TO_BORDER != 0 &&
        NV2A_GL_TEXTURE_BORDER_COLOR != 0 &&
        (gl_extension_supported(exts, "GL_EXT_texture_border_clamp") ||
         gl_extension_supported(exts, "GL_OES_texture_border_clamp") ||
         gl_extension_supported(exts, "GL_NV_texture_border_clamp"));
    r->supported_extensions.texture_lod_bias =
        NV2A_GL_TEXTURE_LOD_BIAS != 0 &&
        gl_extension_supported(exts, "GL_EXT_texture_lod_bias");
    r->supported_extensions.occlusion_query_boolean =
        NV2A_GL_ZPASS_QUERY_TARGET != 0 &&
        (epoxy_gl_version() >= 30 ||
         gl_extension_supported(exts, "GL_EXT_occlusion_query_boolean"));
    __android_log_print(ANDROID_LOG_INFO, "hakuX",
                        "pgraph_gl_init: texture_border_clamp=%s texture_lod_bias=%s occlusion_query_boolean=%s",
                        r->supported_extensions.texture_border_clamp ? "yes" : "no",
                        r->supported_extensions.texture_lod_bias ? "yes" : "no",
                        r->supported_extensions.occlusion_query_boolean ? "yes" : "no");
#endif
    if (r->supported_extensions.texture_filter_anisotropic) {
        glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT,
                    &r->supported_extensions.max_texture_max_anisotropy);
        if (r->supported_extensions.max_texture_max_anisotropy < 1.0f) {
            r->supported_extensions.max_texture_max_anisotropy = 1.0f;
        }
    }
}

static void pgraph_gl_finalize(NV2AState *d)
{
    PGRAPHState *pg = &d->pgraph;

    glo_set_current(g_nv2a_context_render);

    pgraph_gl_finalize_surfaces(pg);
    pgraph_gl_finalize_shaders(pg);
    pgraph_gl_finalize_textures(pg);
    pgraph_gl_finalize_reports(pg);
    pgraph_gl_finalize_buffers(pg);
    pgraph_gl_finalize_display(pg);

    glo_set_current(NULL);

    g_free(pg->gl_renderer_state);
    pg->gl_renderer_state = NULL;
}

static void pgraph_gl_flip_stall(NV2AState *d)
{
    NV2A_GL_DFRAME_TERMINATOR();
    glFlush();
}

static void pgraph_gl_flush(NV2AState *d)
{
    pgraph_gl_surface_flush(d);
    pgraph_gl_mark_textures_possibly_dirty(d, 0, memory_region_size(d->vram));
    pgraph_gl_update_entire_memory_buffer(d);
    /* FIXME: Flush more? */

    qatomic_set(&d->pgraph.flush_pending, false);
    qemu_event_set(&d->pgraph.flush_complete);
}

static void pgraph_gl_process_pending(NV2AState *d)
{
    PGRAPHState *pg = &d->pgraph;
    PGRAPHGLState *r = pg->gl_renderer_state;

    if (qatomic_read(&r->downloads_pending) ||
        qatomic_read(&r->download_dirty_surfaces_pending) ||
        qatomic_read(&d->pgraph.sync_pending) ||
        qatomic_read(&d->pgraph.flush_pending) ||
        qatomic_read(&r->shader_cache_writeback_pending)) {
        qemu_mutex_unlock(&d->pfifo.lock);
        qemu_mutex_lock(&d->pgraph.lock);
        if (qatomic_read(&r->downloads_pending)) {
            pgraph_gl_process_pending_downloads(d);
        }
        if (qatomic_read(&r->download_dirty_surfaces_pending)) {
            pgraph_gl_download_dirty_surfaces(d);
        }
        if (qatomic_read(&d->pgraph.sync_pending)) {
            pgraph_gl_sync(d);
        }
        if (qatomic_read(&d->pgraph.flush_pending)) {
            pgraph_gl_flush(d);
        }
        if (qatomic_read(&r->shader_cache_writeback_pending)) {
            pgraph_gl_shader_write_cache_reload_list(&d->pgraph);
        }
        qemu_mutex_unlock(&d->pgraph.lock);
        qemu_mutex_lock(&d->pfifo.lock);
    }
}

static void pgraph_gl_pre_savevm_trigger(NV2AState *d)
{
    PGRAPHState *pg = &d->pgraph;
    PGRAPHGLState *r = pg->gl_renderer_state;

    qatomic_set(&r->download_dirty_surfaces_pending, true);
    qemu_event_reset(&r->dirty_surfaces_download_complete);
}

static void pgraph_gl_pre_savevm_wait(NV2AState *d)
{
    PGRAPHState *pg = &d->pgraph;
    PGRAPHGLState *r = pg->gl_renderer_state;

    qemu_event_wait(&r->dirty_surfaces_download_complete);
}

static void pgraph_gl_pre_shutdown_trigger(NV2AState *d)
{
    PGRAPHState *pg = &d->pgraph;
    PGRAPHGLState *r = pg->gl_renderer_state;

    qatomic_set(&r->shader_cache_writeback_pending, true);
    qemu_event_reset(&r->shader_cache_writeback_complete);
}

static void pgraph_gl_pre_shutdown_wait(NV2AState *d)
{
    PGRAPHState *pg = &d->pgraph;
    PGRAPHGLState *r = pg->gl_renderer_state;

    qemu_event_wait(&r->shader_cache_writeback_complete);
}

static PGRAPHRenderer pgraph_gl_renderer = {
    .type = CONFIG_DISPLAY_RENDERER_OPENGL,
    .name = "OpenGL",
    .ops = {
        .init = pgraph_gl_init,
        .early_context_init = early_context_init,
        .finalize = pgraph_gl_finalize,
        .clear_report_value = pgraph_gl_clear_report_value,
        .clear_surface = pgraph_gl_clear_surface,
        .draw_begin = pgraph_gl_draw_begin,
        .draw_end = pgraph_gl_draw_end,
        .flip_stall = pgraph_gl_flip_stall,
        .flush_draw = pgraph_gl_flush_draw,
        .get_report = pgraph_gl_get_report,
        .image_blit = pgraph_gl_image_blit,
        .pre_savevm_trigger = pgraph_gl_pre_savevm_trigger,
        .pre_savevm_wait = pgraph_gl_pre_savevm_wait,
        .pre_shutdown_trigger = pgraph_gl_pre_shutdown_trigger,
        .pre_shutdown_wait = pgraph_gl_pre_shutdown_wait,
        .process_pending = pgraph_gl_process_pending,
        .process_pending_reports = pgraph_gl_process_pending_reports,
        .surface_update = pgraph_gl_surface_update,
        .set_surface_scale_factor = pgraph_gl_set_surface_scale_factor,
        .get_surface_scale_factor = pgraph_gl_get_surface_scale_factor,
        .get_framebuffer_surface = pgraph_gl_get_framebuffer_surface,
    }
};

static void __attribute__((constructor)) register_renderer(void)
{
    pgraph_renderer_register(&pgraph_gl_renderer);
}

void pgraph_gl_force_register(void)
{
    static bool registered = false;
    if (registered) {
        return;
    }
    pgraph_renderer_register(&pgraph_gl_renderer);
    registered = true;
}

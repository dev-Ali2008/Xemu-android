/*
 * Geforce NV2A PGRAPH OpenGL Renderer
 *
 * Copyright (c) 2012 espes
 * Copyright (c) 2015 Jannik Vogel
 * Copyright (c) 2018-2024 Matt Borgerson
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

#include "qemu/fast-hash.h"
#include "hw/xbox/nv2a/nv2a_int.h"
#include "hw/xbox/nv2a/pgraph/swizzle.h"
#include "hw/xbox/nv2a/pgraph/s3tc.h"
#include "hw/xbox/nv2a/pgraph/texture.h"
#include "debug.h"
#include "renderer.h"

#ifdef __ANDROID__
#include <android/log.h>

extern bool xemu_android_is_debug_logging_enabled(void);

#ifdef __aarch64__
#include <arm_neon.h>
#endif

static void android_log_gl_errors(const char *ctx)
{
    if (!xemu_android_is_debug_logging_enabled()) {
        return;
    }

    GLenum err;

    while ((err = glGetError()) != GL_NO_ERROR) {
        __android_log_print(ANDROID_LOG_WARN, "hakuX",
                            "GL error 0x%X at %s", err, ctx);
    }
}

static void android_log_texture_stage_errors(int unit, const char *stage,
                                             const TextureShape *shape,
                                             GLenum gl_target)
{
    if (!xemu_android_is_debug_logging_enabled()) {
        return;
    }

    GLenum err;

    if (!xemu_android_is_debug_logging_enabled()) {
        return;
    }

    while ((err = glGetError()) != GL_NO_ERROR) {
        if (shape) {
            __android_log_print(
                ANDROID_LOG_WARN, "hakuX",
                "GL error 0x%X at pgraph_gl_bind_textures[%d]: %s "
                "target=0x%X dim=%u fmt=0x%X levels=%u border=%d cubemap=%d",
                err, unit, stage, gl_target, shape->dimensionality,
                shape->color_format, shape->levels, shape->border,
                shape->cubemap);
        } else {
            __android_log_print(
                ANDROID_LOG_WARN, "hakuX",
                "GL error 0x%X at pgraph_gl_bind_textures[%d]: %s "
                "target=0x%X",
                err, unit, stage, gl_target);
        }
    }
}

static uint8_t android_expand_4_to_8(uint8_t value)
{
    return (value << 4) | value;
}

static uint8_t android_expand_5_to_8(uint8_t value)
{
    return (value << 3) | (value >> 2);
}

static bool android_texture_needs_rgba8_upload(const TextureShape s)
{
    switch (s.color_format) {
    case NV097_SET_TEXTURE_FORMAT_COLOR_SZ_Y8:
    case NV097_SET_TEXTURE_FORMAT_COLOR_SZ_AY8:
    case NV097_SET_TEXTURE_FORMAT_COLOR_SZ_A1R5G5B5:
    case NV097_SET_TEXTURE_FORMAT_COLOR_SZ_X1R5G5B5:
    case NV097_SET_TEXTURE_FORMAT_COLOR_SZ_A4R4G4B4:
    case NV097_SET_TEXTURE_FORMAT_COLOR_SZ_A8:
    case NV097_SET_TEXTURE_FORMAT_COLOR_SZ_A8Y8:
    case NV097_SET_TEXTURE_FORMAT_COLOR_SZ_A8R8G8B8:
    case NV097_SET_TEXTURE_FORMAT_COLOR_SZ_X8R8G8B8:
    case NV097_SET_TEXTURE_FORMAT_COLOR_SZ_I8_A8R8G8B8:
    case NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_Y8:
    case NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_G8B8:
    case NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_AY8:
    case NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_A1R5G5B5:
    case NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_A8R8G8B8:
    case NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_X1R5G5B5:
    case NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_A4R4G4B4:
    case NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_X8R8G8B8:
    case NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_A8:
    case NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_A8Y8:
    case NV097_SET_TEXTURE_FORMAT_COLOR_SZ_G8B8:
    case NV097_SET_TEXTURE_FORMAT_COLOR_SZ_R8B8:
    case NV097_SET_TEXTURE_FORMAT_COLOR_SZ_A8B8G8R8:
    case NV097_SET_TEXTURE_FORMAT_COLOR_SZ_B8G8R8A8:
    case NV097_SET_TEXTURE_FORMAT_COLOR_SZ_R8G8B8A8:
    case NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_A8B8G8R8:
    case NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_B8G8R8A8:
    case NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_R8G8B8A8:
        return true;
    default:
        return false;
    }
}

static bool android_texture_rgba8_upload_applies_swizzle(const TextureShape s)
{
    switch (s.color_format) {
    case NV097_SET_TEXTURE_FORMAT_COLOR_SZ_Y8:
    case NV097_SET_TEXTURE_FORMAT_COLOR_SZ_AY8:
    case NV097_SET_TEXTURE_FORMAT_COLOR_SZ_A8:
    case NV097_SET_TEXTURE_FORMAT_COLOR_SZ_A8Y8:
    case NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_Y8:
    case NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_G8B8:
    case NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_AY8:
    case NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_A8:
    case NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_A8Y8:
    case NV097_SET_TEXTURE_FORMAT_COLOR_SZ_G8B8:
    case NV097_SET_TEXTURE_FORMAT_COLOR_SZ_R8B8:
        return true;
    default:
        return false;
    }
}

static unsigned int android_texture_source_bpp(const TextureShape s,
                                               unsigned int default_bpp,
                                               bool has_converted_data)
{
    if (has_converted_data &&
        s.color_format == NV097_SET_TEXTURE_FORMAT_COLOR_SZ_I8_A8R8G8B8) {
        return 4;
    }
    return default_bpp;
}

static void android_texture_convert_to_rgba8(const TextureShape s,
                                             const uint8_t *src,
                                             unsigned int width,
                                             unsigned int height,
                                             unsigned int depth,
                                             unsigned int row_pitch,
                                             unsigned int slice_pitch,
                                             uint8_t *dst)
{
    unsigned int x, y, z;

    for (z = 0; z < depth; z++) {
        const uint8_t *src_slice = src + z * slice_pitch;
        uint8_t *dst_slice = dst + z * width * height * 4;

        for (y = 0; y < height; y++) {
            const uint8_t *src_row = src_slice + y * row_pitch;
            uint8_t *dst_row = dst_slice + y * width * 4;

            for (x = 0; x < width; x++) {
                uint8_t *out = dst_row + x * 4;

                switch (s.color_format) {
                case NV097_SET_TEXTURE_FORMAT_COLOR_SZ_Y8:
                case NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_Y8: {
                    uint8_t value = src_row[x];
                    out[0] = value;
                    out[1] = value;
                    out[2] = value;
                    out[3] = 0xFF;
                    break;
                }
                case NV097_SET_TEXTURE_FORMAT_COLOR_SZ_AY8:
                case NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_AY8: {
                    uint8_t value = src_row[x];
                    out[0] = value;
                    out[1] = value;
                    out[2] = value;
                    out[3] = value;
                    break;
                }
                case NV097_SET_TEXTURE_FORMAT_COLOR_SZ_A1R5G5B5:
                case NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_A1R5G5B5: {
                    uint16_t pixel = lduw_le_p(src_row + x * 2);
                    out[0] = android_expand_5_to_8((pixel >> 10) & 0x1F);
                    out[1] = android_expand_5_to_8((pixel >> 5) & 0x1F);
                    out[2] = android_expand_5_to_8(pixel & 0x1F);
                    out[3] = (pixel & 0x8000) ? 0xFF : 0x00;
                    break;
                }
                case NV097_SET_TEXTURE_FORMAT_COLOR_SZ_X1R5G5B5:
                case NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_X1R5G5B5: {
                    uint16_t pixel = lduw_le_p(src_row + x * 2);
                    out[0] = android_expand_5_to_8((pixel >> 10) & 0x1F);
                    out[1] = android_expand_5_to_8((pixel >> 5) & 0x1F);
                    out[2] = android_expand_5_to_8(pixel & 0x1F);
                    out[3] = 0xFF;
                    break;
                }
                case NV097_SET_TEXTURE_FORMAT_COLOR_SZ_A4R4G4B4:
                case NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_A4R4G4B4: {
                    uint16_t pixel = lduw_le_p(src_row + x * 2);
                    out[0] = android_expand_4_to_8((pixel >> 8) & 0x0F);
                    out[1] = android_expand_4_to_8((pixel >> 4) & 0x0F);
                    out[2] = android_expand_4_to_8(pixel & 0x0F);
                    out[3] = android_expand_4_to_8((pixel >> 12) & 0x0F);
                    break;
                }
                case NV097_SET_TEXTURE_FORMAT_COLOR_SZ_A8:
                case NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_A8: {
                    uint8_t value = src_row[x];
                    out[0] = 0xFF;
                    out[1] = 0xFF;
                    out[2] = 0xFF;
                    out[3] = value;
                    break;
                }
                case NV097_SET_TEXTURE_FORMAT_COLOR_SZ_A8Y8:
                case NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_A8Y8: {
                    const uint8_t *pixel = src_row + x * 2;
                    out[0] = pixel[0];
                    out[1] = pixel[0];
                    out[2] = pixel[0];
                    out[3] = pixel[1];
                    break;
                }
                case NV097_SET_TEXTURE_FORMAT_COLOR_SZ_A8R8G8B8:
                case NV097_SET_TEXTURE_FORMAT_COLOR_SZ_I8_A8R8G8B8:
                case NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_A8R8G8B8:
                case NV097_SET_TEXTURE_FORMAT_COLOR_SZ_X8R8G8B8:
                case NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_X8R8G8B8: {
                    bool preserve_alpha =
                        (s.color_format != NV097_SET_TEXTURE_FORMAT_COLOR_SZ_X8R8G8B8 &&
                         s.color_format != NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_X8R8G8B8);
#ifdef __aarch64__
                    /* NEON BGRA→RGBA shuffle: process 4 pixels (16 bytes) at a time */
                    static const uint8_t perm_arr[16] =
                        {2,1,0,3, 6,5,4,7, 10,9,8,11, 14,13,12,15};
                    uint8x16_t vperm = vld1q_u8(perm_arr);
                    static const uint8_t alpha_mask_arr[16] =
                        {0,0,0,0xFF, 0,0,0,0xFF, 0,0,0,0xFF, 0,0,0,0xFF};
                    uint8x16_t valpha_mask = preserve_alpha
                        ? vdupq_n_u8(0)
                        : vld1q_u8(alpha_mask_arr);
                    unsigned int remaining = width - x;
                    const uint8_t *sp = src_row + x * 4;
                    uint8_t *dp = out;
                    while (remaining >= 4) {
                        uint8x16_t v = vqtbl1q_u8(vld1q_u8(sp), vperm);
                        vst1q_u8(dp, vorrq_u8(v, valpha_mask));
                        sp += 16; dp += 16; remaining -= 4;
                    }
                    while (remaining-- > 0) {
                        dp[0] = sp[2]; dp[1] = sp[1];
                        dp[2] = sp[0]; dp[3] = preserve_alpha ? sp[3] : 0xFF;
                        sp += 4; dp += 4;
                    }
                    x = width; /* skip remaining scalar iterations */
#else
                    const uint8_t *pixel = src_row + x * 4;
                    out[0] = pixel[2];
                    out[1] = pixel[1];
                    out[2] = pixel[0];
                    out[3] = preserve_alpha ? pixel[3] : 0xFF;
#endif
                    break;
                }
                case NV097_SET_TEXTURE_FORMAT_COLOR_SZ_B8G8R8A8:
                case NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_B8G8R8A8: {
#ifdef __aarch64__
                    /* NEON B8G8R8A8→RGBA8: rotate bytes within each pixel */
                    static const uint8_t perm_arr[16] =
                        {1,2,3,0, 5,6,7,4, 9,10,11,8, 13,14,15,12};
                    uint8x16_t vperm = vld1q_u8(perm_arr);
                    unsigned int remaining = width - x;
                    const uint8_t *sp = src_row + x * 4;
                    uint8_t *dp = out;
                    while (remaining >= 4) {
                        vst1q_u8(dp, vqtbl1q_u8(vld1q_u8(sp), vperm));
                        sp += 16; dp += 16; remaining -= 4;
                    }
                    while (remaining-- > 0) {
                        dp[0] = sp[1]; dp[1] = sp[2];
                        dp[2] = sp[3]; dp[3] = sp[0];
                        sp += 4; dp += 4;
                    }
                    x = width;
#else
                    const uint8_t *pixel = src_row + x * 4;
                    out[0] = pixel[1];
                    out[1] = pixel[2];
                    out[2] = pixel[3];
                    out[3] = pixel[0];
#endif
                    break;
                }
                case NV097_SET_TEXTURE_FORMAT_COLOR_SZ_A8B8G8R8:
                case NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_A8B8G8R8: {
#ifdef __aarch64__
                    /* A8B8G8R8 is already RGBA8 — bulk memcpy */
                    memcpy(out, src_row + x * 4, (width - x) * 4);
                    x = width;
#else
                    const uint8_t *pixel = src_row + x * 4;
                    out[0] = pixel[0];
                    out[1] = pixel[1];
                    out[2] = pixel[2];
                    out[3] = pixel[3];
#endif
                    break;
                }
                case NV097_SET_TEXTURE_FORMAT_COLOR_SZ_R8G8B8A8:
                case NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_R8G8B8A8: {
#ifdef __aarch64__
                    /* NEON R8G8B8A8→RGBA8: full byte reverse per pixel */
                    static const uint8_t perm_arr[16] =
                        {3,2,1,0, 7,6,5,4, 11,10,9,8, 15,14,13,12};
                    uint8x16_t vperm = vld1q_u8(perm_arr);
                    unsigned int remaining = width - x;
                    const uint8_t *sp = src_row + x * 4;
                    uint8_t *dp = out;
                    while (remaining >= 4) {
                        vst1q_u8(dp, vqtbl1q_u8(vld1q_u8(sp), vperm));
                        sp += 16; dp += 16; remaining -= 4;
                    }
                    while (remaining-- > 0) {
                        dp[0] = sp[3]; dp[1] = sp[2];
                        dp[2] = sp[1]; dp[3] = sp[0];
                        sp += 4; dp += 4;
                    }
                    x = width;
#else
                    const uint8_t *pixel = src_row + x * 4;
                    out[0] = pixel[3];
                    out[1] = pixel[2];
                    out[2] = pixel[1];
                    out[3] = pixel[0];
#endif
                    break;
                }
                case NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_G8B8:
                case NV097_SET_TEXTURE_FORMAT_COLOR_SZ_G8B8: {
                    const uint8_t *pixel = src_row + x * 2;
                    out[0] = pixel[0];
                    out[1] = pixel[1];
                    out[2] = pixel[0];
                    out[3] = pixel[1];
                    break;
                }
                case NV097_SET_TEXTURE_FORMAT_COLOR_SZ_R8B8: {
                    const uint8_t *pixel = src_row + x * 2;
                    out[0] = pixel[1];
                    out[1] = pixel[0];
                    out[2] = pixel[0];
                    out[3] = pixel[1];
                    break;
                }
                default:
                    g_assert_not_reached();
                }
            }
        }
    }
}

static void android_prepare_tex_upload(PGRAPHGLState *r,
                                       const TextureShape s,
                                       const uint8_t *src,
                                       unsigned int width,
                                       unsigned int height,
                                       unsigned int depth,
                                       unsigned int row_pitch,
                                       unsigned int slice_pitch,
                                       GLint *ifmt,
                                       GLenum *fmt,
                                       GLenum *type,
                                       const uint8_t **upload_data)
{
    *upload_data = src;
    *ifmt = kelvin_color_format_gl_map[s.color_format].gl_internal_format;
    *fmt = kelvin_color_format_gl_map[s.color_format].gl_format;
    *type = kelvin_color_format_gl_map[s.color_format].gl_type;

    if (!android_texture_needs_rgba8_upload(s)) {
        return;
    }

    size_t needed = (size_t)width * height * depth * 4;
    if (needed > r->android_tex_conv_buf_size) {
        g_free(r->android_tex_conv_buf);
        r->android_tex_conv_buf = g_malloc(needed);
        r->android_tex_conv_buf_size = needed;
    }
    android_texture_convert_to_rgba8(s, src, width, height, depth,
                                     row_pitch, slice_pitch,
                                     r->android_tex_conv_buf);
    *upload_data = r->android_tex_conv_buf;
    *ifmt = GL_RGBA8;
    *fmt = GL_RGBA;
    *type = GL_UNSIGNED_BYTE;
}
#endif

static void pgraph_gl_unbind_texture_targets(void)
{
    glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
#ifndef __ANDROID__
    glBindTexture(GL_TEXTURE_1D, 0);
#endif
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindTexture(GL_TEXTURE_3D, 0);
}

static bool pgraph_gl_texture_range_valid(NV2AState *d,
                                          hwaddr vram_offset,
                                          size_t length)
{
    hwaddr vram_size = memory_region_size(d->vram);

    if (length == 0 || vram_offset >= vram_size) {
        return false;
    }

    return length <= (vram_size - vram_offset);
}

static void pgraph_gl_log_invalid_texture_range(NV2AState *d,
                                                int unit,
                                                const char *range_name,
                                                const TextureShape *shape,
                                                hwaddr vram_offset,
                                                size_t length)
{
    hwaddr vram_size = memory_region_size(d->vram);

    NV2A_XPRINTF(true,
                 "Skipping %s for stage %d: offset=0x%" HWADDR_PRIx
                 " length=0x%zx vram=0x%" HWADDR_PRIx
                 " dim=%u fmt=0x%X levels=%u border=%d cubemap=%d\n",
                 range_name, unit, vram_offset, length, vram_size,
                 shape->dimensionality, shape->color_format, shape->levels,
                 shape->border, shape->cubemap);
#ifdef __ANDROID__
    __android_log_print(ANDROID_LOG_WARN, "hakuX",
                        "Skipping %s for stage %d: offset=0x%" HWADDR_PRIx
                        " length=0x%zx vram=0x%" HWADDR_PRIx
                        " dim=%u fmt=0x%X levels=%u border=%d cubemap=%d",
                        range_name, unit, vram_offset, length, vram_size,
                        shape->dimensionality, shape->color_format,
                        shape->levels, shape->border, shape->cubemap);
#endif
}

static TextureBinding* generate_texture(PGRAPHGLState *r, const TextureShape s, const uint8_t *texture_data, const uint8_t *palette_data);
static void texture_binding_destroy(gpointer data);

struct pgraph_texture_possibly_dirty_struct {
    hwaddr addr, end;
};

static void mark_textures_possibly_dirty_visitor(Lru *lru, LruNode *node, void *opaque)
{
    struct pgraph_texture_possibly_dirty_struct *test =
        (struct pgraph_texture_possibly_dirty_struct *)opaque;

    struct TextureLruNode *tnode = container_of(node, TextureLruNode, node);
    if (tnode->binding == NULL || tnode->possibly_dirty) {
        return;
    }

    uintptr_t k_tex_addr = tnode->key.texture_vram_offset;
    uintptr_t k_tex_end = k_tex_addr + tnode->key.texture_length - 1;
    bool overlapping = !(test->addr > k_tex_end || k_tex_addr > test->end);

    if (tnode->key.palette_length > 0) {
        uintptr_t k_pal_addr = tnode->key.palette_vram_offset;
        uintptr_t k_pal_end = k_pal_addr + tnode->key.palette_length - 1;
        overlapping |= !(test->addr > k_pal_end || k_pal_addr > test->end);
    }

    tnode->possibly_dirty |= overlapping;
}

void pgraph_gl_mark_textures_possibly_dirty(NV2AState *d,
    hwaddr addr, hwaddr size)
{
    PGRAPHState *pg = &d->pgraph;
    PGRAPHGLState *r = pg->gl_renderer_state;

    hwaddr end = TARGET_PAGE_ALIGN(addr + size) - 1;
    addr &= TARGET_PAGE_MASK;
    assert(end <= memory_region_size(d->vram));

    struct pgraph_texture_possibly_dirty_struct test = {
        .addr = addr,
        .end = end,
    };

    lru_visit_active(&r->texture_cache,
                     mark_textures_possibly_dirty_visitor,
                     &test);
}

static bool check_texture_dirty(NV2AState *d, hwaddr addr, hwaddr size)
{
    hwaddr end = TARGET_PAGE_ALIGN(addr + size);
    addr &= TARGET_PAGE_MASK;
    assert(end < memory_region_size(d->vram));
    return memory_region_test_and_clear_dirty(d->vram, addr, end - addr,
                                              DIRTY_MEMORY_NV2A_TEX);
}

// Check if any of the pages spanned by the a texture are dirty.
static bool check_texture_possibly_dirty(NV2AState *d,
                                         hwaddr texture_vram_offset,
                                         unsigned int length,
                                         hwaddr palette_vram_offset,
                                         unsigned int palette_length)
{
    bool possibly_dirty = false;
    if (check_texture_dirty(d, texture_vram_offset, length)) {
        possibly_dirty = true;
        pgraph_gl_mark_textures_possibly_dirty(d, texture_vram_offset, length);
    }
    if (palette_length && check_texture_dirty(d, palette_vram_offset,
                                                     palette_length)) {
        possibly_dirty = true;
        pgraph_gl_mark_textures_possibly_dirty(d, palette_vram_offset,
                                            palette_length);
    }
    return possibly_dirty;
}

static void apply_texture_parameters(PGRAPHGLState *r,
                                     TextureBinding *binding,
                                     const BasicColorFormatInfo *f,
                                     unsigned int dimensionality,
                                     unsigned int filter,
                                     unsigned int address,
                                     bool is_bordered,
                                     uint32_t border_color,
                                     uint32_t max_anisotropy)
{
    unsigned int min_filter = GET_MASK(filter, NV_PGRAPH_TEXFILTER0_MIN);
    unsigned int mag_filter = GET_MASK(filter, NV_PGRAPH_TEXFILTER0_MAG);
    unsigned int lod_bias =
        GET_MASK(filter, NV_PGRAPH_TEXFILTER0_MIPMAP_LOD_BIAS);
    unsigned int addru = GET_MASK(address, NV_PGRAPH_TEXADDRESS0_ADDRU);
    unsigned int addrv = GET_MASK(address, NV_PGRAPH_TEXADDRESS0_ADDRV);
    unsigned int addrp = GET_MASK(address, NV_PGRAPH_TEXADDRESS0_ADDRP);

    if (f->linear) {
        /* somtimes games try to set mipmap min filters on linear textures.
             * this could indicate a bug... */
        switch (min_filter) {
        case NV_PGRAPH_TEXFILTER0_MIN_BOX_NEARESTLOD:
        case NV_PGRAPH_TEXFILTER0_MIN_BOX_TENT_LOD:
            min_filter = NV_PGRAPH_TEXFILTER0_MIN_BOX_LOD0;
            break;
        case NV_PGRAPH_TEXFILTER0_MIN_TENT_NEARESTLOD:
        case NV_PGRAPH_TEXFILTER0_MIN_TENT_TENT_LOD:
            min_filter = NV_PGRAPH_TEXFILTER0_MIN_TENT_LOD0;
            break;
        }
    }

    if (min_filter != binding->min_filter) {
        glTexParameteri(binding->gl_target, GL_TEXTURE_MIN_FILTER,
                        pgraph_texture_min_filter_gl_map[min_filter]);
        binding->min_filter = min_filter;
    }
    if (mag_filter != binding->mag_filter) {
        glTexParameteri(binding->gl_target, GL_TEXTURE_MAG_FILTER,
                        pgraph_texture_mag_filter_gl_map[mag_filter]);
        binding->mag_filter = mag_filter;
    }
    if (lod_bias != binding->lod_bias) {
        binding->lod_bias = lod_bias;
        if (r->supported_extensions.texture_lod_bias) {
            glTexParameterf(binding->gl_target, NV2A_GL_TEXTURE_LOD_BIAS,
                            pgraph_convert_lod_bias_to_float(lod_bias));
        }
    }

    /* Texture wrapping */
    assert(addru < ARRAY_SIZE(pgraph_texture_addr_gl_map));
    if (addru != binding->addru) {
        GLenum wrap_s = pgraph_texture_addr_gl_map[addru];
#ifdef __ANDROID__
        if (!r->supported_extensions.texture_border_clamp &&
            wrap_s == NV2A_GL_CLAMP_TO_BORDER) {
            wrap_s = GL_CLAMP_TO_EDGE;
        }
#endif
        glTexParameteri(binding->gl_target, GL_TEXTURE_WRAP_S,
                        wrap_s);
        binding->addru = addru;
    }
    bool needs_border_color = binding->addru == NV_PGRAPH_TEXADDRESS0_ADDRU_BORDER;
    if (dimensionality > 1) {
        if (addrv != binding->addrv) {
            assert(addrv < ARRAY_SIZE(pgraph_texture_addr_gl_map));
            GLenum wrap_t = pgraph_texture_addr_gl_map[addrv];
#ifdef __ANDROID__
            if (!r->supported_extensions.texture_border_clamp &&
                wrap_t == NV2A_GL_CLAMP_TO_BORDER) {
                wrap_t = GL_CLAMP_TO_EDGE;
            }
#endif
            glTexParameteri(binding->gl_target, GL_TEXTURE_WRAP_T,
                            wrap_t);
            binding->addrv = addrv;
        }
        needs_border_color = needs_border_color
                             || binding->addrv == NV_PGRAPH_TEXADDRESS0_ADDRU_BORDER;
    }
    if (dimensionality > 2) {
        if (addrp != binding->addrp) {
            assert(addrp < ARRAY_SIZE(pgraph_texture_addr_gl_map));
            GLenum wrap_r = pgraph_texture_addr_gl_map[addrp];
#ifdef __ANDROID__
            if (!r->supported_extensions.texture_border_clamp &&
                wrap_r == NV2A_GL_CLAMP_TO_BORDER) {
                wrap_r = GL_CLAMP_TO_EDGE;
            }
#endif
            glTexParameteri(binding->gl_target, GL_TEXTURE_WRAP_R,
                            wrap_r);
            binding->addrp = addrp;
        }
        needs_border_color = needs_border_color
                             || binding->addrp == NV_PGRAPH_TEXADDRESS0_ADDRU_BORDER;
    }

    if (r->supported_extensions.texture_filter_anisotropic) {
        GLfloat clamped_anisotropy = MIN(
            (GLfloat)max_anisotropy,
            r->supported_extensions.max_texture_max_anisotropy);
        if (clamped_anisotropy < 1.0f) {
            clamped_anisotropy = 1.0f;
        }
        glTexParameterf(binding->gl_target, GL_TEXTURE_MAX_ANISOTROPY_EXT,
                        clamped_anisotropy);
    }

    if (!is_bordered && needs_border_color) {
#ifdef __ANDROID__
        if (!r->supported_extensions.texture_border_clamp) {
            needs_border_color = false;
        }
#endif
    }
    if (!is_bordered && needs_border_color) {
        if (!binding->border_color_set || binding->border_color != border_color) {
            /* FIXME: Color channels might be wrong order */
            GLfloat gl_border_color[4];
            pgraph_argb_pack32_to_rgba_float(border_color, gl_border_color);
            glTexParameterfv(binding->gl_target, NV2A_GL_TEXTURE_BORDER_COLOR,
                             gl_border_color);

            binding->border_color_set = true;
            binding->border_color = border_color;
        }
    }
}

void pgraph_gl_bind_textures(NV2AState *d)
{
    int i;
    PGRAPHState *pg = &d->pgraph;
    PGRAPHGLState *r = pg->gl_renderer_state;

    NV2A_GL_DGROUP_BEGIN("%s", __func__);

    for (i=0; i<NV2A_MAX_TEXTURES; i++) {
        bool enabled = pgraph_is_texture_enabled(pg, i);
        /* FIXME: What happens if texture is disabled but stage is active? */

        glActiveTexture(GL_TEXTURE0 + i);
#ifdef __ANDROID__
        android_log_texture_stage_errors(i, "after_active_texture", NULL, 0);
#endif
        if (!enabled) {
            pgraph_gl_unbind_texture_targets();
#ifdef __ANDROID__
            android_log_texture_stage_errors(i, "disabled_unbind", NULL, 0);
#endif
            continue;
        }

        uint32_t filter = pgraph_reg_r(pg, NV_PGRAPH_TEXFILTER0 + i*4);
        uint32_t address = pgraph_reg_r(pg, NV_PGRAPH_TEXADDRESS0 + i*4);
        uint32_t border_color = pgraph_reg_r(pg, NV_PGRAPH_BORDERCOLOR0 + i*4);
        uint32_t max_anisotropy =
            1 << (GET_MASK(pgraph_reg_r(pg, NV_PGRAPH_TEXCTL0_0 + i*4),
                           NV_PGRAPH_TEXCTL0_0_MAX_ANISOTROPY));

        /* Check for unsupported features */
        if (filter & NV_PGRAPH_TEXFILTER0_ASIGNED) NV2A_UNIMPLEMENTED("NV_PGRAPH_TEXFILTER0_ASIGNED");
        if (filter & NV_PGRAPH_TEXFILTER0_RSIGNED) NV2A_UNIMPLEMENTED("NV_PGRAPH_TEXFILTER0_RSIGNED");
        if (filter & NV_PGRAPH_TEXFILTER0_GSIGNED) NV2A_UNIMPLEMENTED("NV_PGRAPH_TEXFILTER0_GSIGNED");
        if (filter & NV_PGRAPH_TEXFILTER0_BSIGNED) NV2A_UNIMPLEMENTED("NV_PGRAPH_TEXFILTER0_BSIGNED");

        TextureShape state = pgraph_get_texture_shape(pg, i);
        hwaddr texture_vram_offset, palette_vram_offset = 0;
        size_t length, palette_length = 0;
        bool is_indexed = (state.color_format ==
                NV097_SET_TEXTURE_FORMAT_COLOR_SZ_I8_A8R8G8B8);

        length = pgraph_get_texture_length(pg, &state);
        texture_vram_offset = pgraph_get_texture_phys_addr(pg, i);
        if (is_indexed) {
            palette_vram_offset = pgraph_get_texture_palette_phys_addr_length(
                pg, i, &palette_length);
        }

        if (!pgraph_gl_texture_range_valid(d, texture_vram_offset, length)) {
            pgraph_gl_log_invalid_texture_range(d, i, "texture", &state,
                                                texture_vram_offset, length);
            pgraph_gl_unbind_texture_targets();
#ifdef __ANDROID__
            android_log_texture_stage_errors(i, "invalid_texture_range",
                                             &state, 0);
#endif
            if (r->texture_binding[i]) {
                texture_binding_destroy(r->texture_binding[i]);
                r->texture_binding[i] = NULL;
            }
            pg->texture_dirty[i] = false;
            continue;
        }

        if (is_indexed &&
            !pgraph_gl_texture_range_valid(d, palette_vram_offset,
                                           palette_length)) {
            pgraph_gl_log_invalid_texture_range(d, i, "palette", &state,
                                                palette_vram_offset,
                                                palette_length);
            pgraph_gl_unbind_texture_targets();
#ifdef __ANDROID__
            android_log_texture_stage_errors(i, "invalid_palette_range",
                                             &state, 0);
#endif
            if (r->texture_binding[i]) {
                texture_binding_destroy(r->texture_binding[i]);
                r->texture_binding[i] = NULL;
            }
            pg->texture_dirty[i] = false;
            continue;
        }

        bool possibly_dirty = false;
        bool possibly_dirty_checked = false;

        SurfaceBinding *surface = pgraph_gl_surface_get(d, texture_vram_offset);
        TextureBinding *tbind = r->texture_binding[i];
        if (!pg->texture_dirty[i] && tbind) {
            bool reusable = false;
            if (surface && tbind->draw_time == surface->draw_time) {
                reusable = true;
            } else if (!surface) {
                possibly_dirty = check_texture_possibly_dirty(
                        d,
                        texture_vram_offset,
                        length,
                        palette_vram_offset,
                        is_indexed ? palette_length : 0);
                possibly_dirty_checked = true;
                reusable = !possibly_dirty;
            }

            if (reusable) {
                glBindTexture(r->texture_binding[i]->gl_target,
                              r->texture_binding[i]->gl_texture);
#ifdef __ANDROID__
                android_log_texture_stage_errors(
                    i, "reuse_bind_existing", &state,
                    r->texture_binding[i]->gl_target);
#endif
                apply_texture_parameters(r,
                                         r->texture_binding[i],
                                         &kelvin_color_format_info_map[state.color_format],
                                         state.dimensionality,
                                         filter,
                                         address,
                                         state.border,
                                         border_color,
                                         max_anisotropy);
#ifdef __ANDROID__
                android_log_texture_stage_errors(
                    i, "reuse_apply_texture_parameters", &state,
                    r->texture_binding[i]->gl_target);
#endif
                continue;
            }
        }

        /*
         * Check active surfaces to see if this texture was a render target
         */
        bool surf_to_tex = false;
        if (surface != NULL) {
            surf_to_tex = pgraph_gl_check_surface_to_texture_compatibility(
                    surface, &state);

            if (surf_to_tex && surface->upload_pending) {
                pgraph_gl_upload_surface_data(d, surface, false);
#ifdef __ANDROID__
                android_log_texture_stage_errors(i, "surface_upload_pending",
                                                 &state, GL_TEXTURE_2D);
#endif
            }
        }

        if (!surf_to_tex) {
            // FIXME: Restructure to support rendering surfaces to cubemap faces

            // Writeback any surfaces which this texture may index
            hwaddr tex_vram_end = texture_vram_offset + length - 1;
            QTAILQ_FOREACH(surface, &r->surfaces, entry) {
                hwaddr surf_vram_end = surface->vram_addr + surface->size - 1;
                bool overlapping = !(surface->vram_addr >= tex_vram_end
                                     || texture_vram_offset >= surf_vram_end);
                if (overlapping) {
                    pgraph_gl_surface_download_if_dirty(d, surface);
#ifdef __ANDROID__
                    android_log_texture_stage_errors(i, "download_overlap",
                                                     &state, GL_TEXTURE_2D);
#endif
                }
            }
        }

        TextureKey key;
        memset(&key, 0, sizeof(TextureKey));
        key.state = state;
        key.texture_vram_offset = texture_vram_offset;
        key.texture_length = length;
        if (is_indexed) {
            key.palette_vram_offset = palette_vram_offset;
            key.palette_length = palette_length;
        }

        // Search for existing texture binding in cache
        uint64_t tex_binding_hash = fast_hash((uint8_t*)&key, sizeof(key));
        LruNode *found = lru_lookup(&r->texture_cache,
                                     tex_binding_hash, &key);
        TextureLruNode *key_out = container_of(found, TextureLruNode, node);
        possibly_dirty |= (key_out->binding == NULL) || key_out->possibly_dirty;

        if (!surf_to_tex && !possibly_dirty_checked) {
            possibly_dirty |= check_texture_possibly_dirty(
                    d,
                    texture_vram_offset,
                    length,
                    palette_vram_offset,
                    is_indexed ? palette_length : 0);
        }

        // Calculate hash of texture data, if necessary
        void *texture_data = (char*)d->vram_ptr + texture_vram_offset;
        void *palette_data = (char*)d->vram_ptr + palette_vram_offset;

        uint64_t tex_data_hash = 0;
        if (!surf_to_tex && possibly_dirty) {
            tex_data_hash = fast_hash(texture_data, length);
            if (is_indexed) {
                tex_data_hash ^= fast_hash(palette_data, palette_length);
            }
        }

        // Free existing binding, if texture data has changed
        bool must_destroy = (key_out->binding != NULL)
                            && possibly_dirty
                            && (key_out->binding->data_hash != tex_data_hash);
        if (must_destroy) {
            texture_binding_destroy(key_out->binding);
            key_out->binding = NULL;
        }

        if (key_out->binding == NULL) {
            // Must create the texture
            key_out->binding = generate_texture(r, state, texture_data, palette_data);
            key_out->binding->data_hash = tex_data_hash;
            key_out->binding->scale = 1;
#ifdef __ANDROID__
            android_log_texture_stage_errors(i, "generate_texture", &state,
                                             key_out->binding->gl_target);
#endif
        } else {
            // Saved an upload! Reuse existing texture in graphics memory.
            glBindTexture(key_out->binding->gl_target,
                          key_out->binding->gl_texture);
#ifdef __ANDROID__
            android_log_texture_stage_errors(i, "reuse_cached_binding", &state,
                                             key_out->binding->gl_target);
#endif
        }

        key_out->possibly_dirty = false;
        TextureBinding *binding = key_out->binding;
        binding->refcnt++;

        if (surf_to_tex && binding->draw_time < surface->draw_time) {

            trace_nv2a_pgraph_surface_render_to_texture(
                surface->vram_addr, surface->width, surface->height);
            pgraph_gl_render_surface_to_texture(d, surface, binding, &state, i);
            binding->draw_time = surface->draw_time;
            binding->scale = pg->surface_scale_factor;
#ifdef __ANDROID__
            android_log_texture_stage_errors(i, "render_surface_to_texture",
                                             &state, binding->gl_target);
#endif
        }

        apply_texture_parameters(r,
                                 binding,
                                 &kelvin_color_format_info_map[state.color_format],
                                 state.dimensionality,
                                 filter,
                                 address,
                                 state.border,
                                 border_color,
                                 max_anisotropy);
#ifdef __ANDROID__
        android_log_texture_stage_errors(i, "apply_texture_parameters", &state,
                                         binding->gl_target);
#endif

        if (r->texture_binding[i]) {
            if (r->texture_binding[i]->gl_target != binding->gl_target) {
                glBindTexture(r->texture_binding[i]->gl_target, 0);
#ifdef __ANDROID__
                android_log_texture_stage_errors(
                    i, "unbind_old_target", &state,
                    r->texture_binding[i]->gl_target);
#endif
            }
            texture_binding_destroy(r->texture_binding[i]);
        }
        r->texture_binding[i] = binding;
        pg->texture_dirty[i] = false;
    }

#ifdef __ANDROID__
    android_log_gl_errors("pgraph_gl_bind_textures");
#endif
    NV2A_GL_DGROUP_END();
}

static enum S3TC_DECOMPRESS_FORMAT
gl_internal_format_to_s3tc_enum(GLint gl_internal_format)
{
    switch (gl_internal_format) {
    case GL_COMPRESSED_RGBA_S3TC_DXT1_EXT:
        return S3TC_DECOMPRESS_FORMAT_DXT1;
    case GL_COMPRESSED_RGBA_S3TC_DXT3_EXT:
        return S3TC_DECOMPRESS_FORMAT_DXT3;
    case GL_COMPRESSED_RGBA_S3TC_DXT5_EXT:
        return S3TC_DECOMPRESS_FORMAT_DXT5;
    default:
        assert(!"Invalid format");
    }
}

static void upload_gl_texture(PGRAPHGLState *r,
                              GLenum gl_target,
                              const TextureShape s,
                              const uint8_t *texture_data,
                              const uint8_t *palette_data)
{
    (void)r; /* used only in Android-specific paths */
    ColorFormatInfo f = kelvin_color_format_gl_map[s.color_format];
    nv2a_profile_inc_counter(NV2A_PROF_TEX_UPLOAD);
#ifdef __ANDROID__
    GLint prev_unpack_alignment;
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &prev_unpack_alignment);
    if (prev_unpack_alignment != 1) {
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    }
#endif

    unsigned int adjusted_width = s.width;
    unsigned int adjusted_height = s.height;
    unsigned int adjusted_pitch = s.pitch;
    unsigned int adjusted_depth = s.depth;
    if (!f.linear && s.border) {
        adjusted_width = MAX(16, adjusted_width * 2);
        adjusted_height = MAX(16, adjusted_height * 2);
        adjusted_pitch = adjusted_width * (s.pitch / s.width);
        adjusted_depth = MAX(16, s.depth * 2);
    }

    switch(gl_target) {
    case GL_TEXTURE_1D:
        assert(false);
        break;
    case GL_TEXTURE_2D:
        if (f.linear) {
            /* Can't handle strides unaligned to pixels */
            assert(s.pitch % f.bytes_per_pixel == 0);

            uint8_t *converted = pgraph_convert_texture_data(
                s, texture_data, palette_data, adjusted_width, adjusted_height, 1,
                adjusted_pitch, 0, NULL);
            {
                const uint8_t *pixel_data = converted ? converted : texture_data;
                const uint8_t *upload_data = pixel_data;
                unsigned int source_bpp =
#ifdef __ANDROID__
                    android_texture_source_bpp(s, f.bytes_per_pixel,
                                               converted != NULL);
#else
                    f.bytes_per_pixel;
#endif
                unsigned int row_pitch =
                    converted ? adjusted_width * source_bpp : adjusted_pitch;
                GLenum tex_fmt = f.gl_format;
                GLint  tex_ifmt = f.gl_internal_format;
                GLenum tex_type = f.gl_type;
#ifdef __ANDROID__
                android_prepare_tex_upload(r, s, pixel_data, adjusted_width,
                                           adjusted_height, 1, row_pitch,
                                           row_pitch * adjusted_height,
                                           &tex_ifmt, &tex_fmt, &tex_type,
                                           &upload_data);
#endif
                glPixelStorei(GL_UNPACK_ROW_LENGTH,
                              upload_data == texture_data ?
                                  adjusted_pitch / f.bytes_per_pixel : 0);
                glTexImage2D(GL_TEXTURE_2D, 0, tex_ifmt,
                             adjusted_width, adjusted_height, 0,
                             tex_fmt, tex_type,
                             upload_data);
                glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
            }

            if (converted) {
              g_free(converted);
            }
            break;
        }
        /* fallthru */
    case GL_TEXTURE_CUBE_MAP_POSITIVE_X:
    case GL_TEXTURE_CUBE_MAP_NEGATIVE_X:
    case GL_TEXTURE_CUBE_MAP_POSITIVE_Y:
    case GL_TEXTURE_CUBE_MAP_NEGATIVE_Y:
    case GL_TEXTURE_CUBE_MAP_POSITIVE_Z:
    case GL_TEXTURE_CUBE_MAP_NEGATIVE_Z: {

        unsigned int width = adjusted_width, height = adjusted_height;

        int level;
        for (level = 0; level < s.levels; level++) {
            width = MAX(width, 1);
            height = MAX(height, 1);

            if (f.gl_format == 0) { /* compressed */
                 // https://docs.microsoft.com/en-us/windows/win32/direct3d10/d3d10-graphics-programming-guide-resources-block-compression#virtual-size-versus-physical-size
                unsigned int block_size =
                    f.gl_internal_format == GL_COMPRESSED_RGBA_S3TC_DXT1_EXT ?
                        8 : 16;
                unsigned int physical_width = (width + 3) & ~3,
                             physical_height = (height + 3) & ~3;
                uint8_t *converted = s3tc_decompress_2d(
                    gl_internal_format_to_s3tc_enum(f.gl_internal_format),
                    texture_data, width, height);
                unsigned int tex_width = width;
                unsigned int tex_height = height;
                bool need_cubemap_border_strip =
                    s.cubemap && adjusted_width != s.width;

                if (need_cubemap_border_strip) {
                    // FIXME: Consider preserving the border.
                    // There does not seem to be a way to reference the border
                    // texels in a cubemap, so they are discarded.
                    glPixelStorei(GL_UNPACK_SKIP_PIXELS, 4);
                    glPixelStorei(GL_UNPACK_SKIP_ROWS, 4);
                    tex_width = s.width;
                    tex_height = s.height;
                    if (physical_width == width) {
                        glPixelStorei(GL_UNPACK_ROW_LENGTH, adjusted_width);
                    }
                }

                /* s3tc_decompress_2d outputs RGBA8 data; use GL_UNSIGNED_BYTE
                 * which is valid on both desktop GL and GLES. */
                glTexImage2D(gl_target, level, GL_RGBA8, tex_width, tex_height,
                             0, GL_RGBA, GL_UNSIGNED_BYTE, converted);
                g_free(converted);
                if (need_cubemap_border_strip) {
                    glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
                    glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
                    if (physical_width == width) {
                        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
                    }
                }
                texture_data +=
                    physical_width / 4 * physical_height / 4 * block_size;
            } else {
                unsigned int pitch = width * f.bytes_per_pixel;
                uint8_t *unswizzled = (uint8_t*)g_malloc(height * pitch);
                unswizzle_rect(texture_data, width, height,
                               unswizzled, pitch, f.bytes_per_pixel);
                uint8_t *converted = pgraph_convert_texture_data(
                    s, unswizzled, palette_data, width, height, 1, pitch, 0,
                    NULL);
                const uint8_t *pixel_data = converted ? converted : unswizzled;
                unsigned int tex_width = width;
                unsigned int tex_height = height;
                unsigned int source_bpp =
#ifdef __ANDROID__
                    android_texture_source_bpp(s, f.bytes_per_pixel,
                                               converted != NULL);
#else
                    f.bytes_per_pixel;
#endif
                unsigned int row_pitch = width * source_bpp;

                if (s.cubemap && adjusted_width != s.width) {
                    // FIXME: Consider preserving the border.
                    // There does not seem to be a way to reference the border
                    // texels in a cubemap, so they are discarded.
                    tex_width = s.width;
                    tex_height = s.height;
                    pixel_data += 4 * source_bpp + 4 * row_pitch;
                }

                {
                    const uint8_t *upload_data = pixel_data;
                    GLenum tex_fmt = f.gl_format;
                    GLint  tex_ifmt = f.gl_internal_format;
                    GLenum tex_type = f.gl_type;
#ifdef __ANDROID__
                    android_prepare_tex_upload(r, s, pixel_data, tex_width,
                                               tex_height, 1, row_pitch,
                                               row_pitch * tex_height,
                                               &tex_ifmt, &tex_fmt, &tex_type,
                                               &upload_data);
#endif
                    glPixelStorei(GL_UNPACK_ROW_LENGTH,
                                  upload_data == pixel_data &&
                                          row_pitch != tex_width * source_bpp ?
                                      row_pitch / source_bpp : 0);
                    glTexImage2D(gl_target, level, tex_ifmt, tex_width,
                                 tex_height, 0, tex_fmt, tex_type,
                                 upload_data);
                    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
                }
                if (converted) {
                    g_free(converted);
                }
                g_free(unswizzled);

                texture_data += width * height * f.bytes_per_pixel;
            }

            width /= 2;
            height /= 2;
        }

        break;
    }
    case GL_TEXTURE_3D: {

        unsigned int width = adjusted_width;
        unsigned int height = adjusted_height;
        unsigned int depth = adjusted_depth;

        assert(f.linear == false);

        int level;
        for (level = 0; level < s.levels; level++) {
            if (f.gl_format == 0) { /* compressed */
                width = MAX(width, 1);
                height = MAX(height, 1);
                unsigned int physical_width = (width + 3) & ~3,
                             physical_height = (height + 3) & ~3;
                depth = MAX(depth, 1);

                unsigned int block_size;
                if (f.gl_internal_format == GL_COMPRESSED_RGBA_S3TC_DXT1_EXT) {
                    block_size = 8;
                } else {
                    block_size = 16;
                }

                size_t texture_size = physical_width/4 * physical_height/4 * depth * block_size;

                uint8_t *converted = s3tc_decompress_3d(
                    gl_internal_format_to_s3tc_enum(f.gl_internal_format),
                    texture_data, width, height, depth);

                /* s3tc_decompress_3d outputs RGBA8; use GL_UNSIGNED_BYTE. */
                glTexImage3D(gl_target, level, GL_RGBA8,
                             width, height, depth, 0,
                             GL_RGBA, GL_UNSIGNED_BYTE,
                             converted);

                g_free(converted);

                texture_data += texture_size;
            } else {
                width = MAX(width, 1);
                height = MAX(height, 1);
                depth = MAX(depth, 1);

                unsigned int row_pitch = width * f.bytes_per_pixel;
                unsigned int slice_pitch = row_pitch * height;
                uint8_t *unswizzled = (uint8_t*)g_malloc(slice_pitch * depth);
                unswizzle_box(texture_data, width, height, depth, unswizzled,
                               row_pitch, slice_pitch, f.bytes_per_pixel);

                uint8_t *converted = pgraph_convert_texture_data(
                    s, unswizzled, palette_data, width, height, depth,
                    row_pitch, slice_pitch, NULL);

                {
                    const uint8_t *pixel_data = converted ? converted : unswizzled;
                    const uint8_t *upload_data = pixel_data;
                    unsigned int source_bpp =
#ifdef __ANDROID__
                        android_texture_source_bpp(s, f.bytes_per_pixel,
                                                   converted != NULL);
#else
                        f.bytes_per_pixel;
#endif
                    unsigned int upload_row_pitch = width * source_bpp;
                    unsigned int upload_slice_pitch = upload_row_pitch * height;
                    GLenum tex_fmt = f.gl_format;
                    GLint  tex_ifmt = f.gl_internal_format;
                    GLenum tex_type = f.gl_type;
#ifdef __ANDROID__
                    android_prepare_tex_upload(r, s, pixel_data, width, height,
                                               depth, upload_row_pitch,
                                               upload_slice_pitch, &tex_ifmt,
                                               &tex_fmt, &tex_type,
                                               &upload_data);
#endif
                    glTexImage3D(gl_target, level, tex_ifmt,
                                 width, height, depth, 0,
                                 tex_fmt, tex_type,
                                 upload_data);
                }

                if (converted) {
                    g_free(converted);
                }
                g_free(unswizzled);

                texture_data += width * height * depth * f.bytes_per_pixel;
            }

            width /= 2;
            height /= 2;
            depth /= 2;
        }
        break;
    }
    default:
        assert(false);
        break;
    }
#ifdef __ANDROID__
    glPixelStorei(GL_UNPACK_ALIGNMENT, prev_unpack_alignment);
#endif
}

static TextureBinding* generate_texture(PGRAPHGLState *r,
                                        const TextureShape s,
                                        const uint8_t *texture_data,
                                        const uint8_t *palette_data)
{
    ColorFormatInfo f = kelvin_color_format_gl_map[s.color_format];

    /* Create a new opengl texture */
    GLuint gl_texture;
    glGenTextures(1, &gl_texture);

    GLenum gl_target;
    if (s.cubemap) {
        assert(f.linear == false);
        assert(s.dimensionality == 2);
        gl_target = GL_TEXTURE_CUBE_MAP;
    } else {
        if (f.linear) {
            gl_target = GL_TEXTURE_2D;
            assert(s.dimensionality == 2);
        } else {
            switch(s.dimensionality) {
            case 1:
#ifdef __ANDROID__
                gl_target = GL_TEXTURE_2D;
#else
                gl_target = GL_TEXTURE_1D;
#endif
                break;
            case 2: gl_target = GL_TEXTURE_2D; break;
            case 3: gl_target = GL_TEXTURE_3D; break;
            default:
                assert(false);
                break;
            }
        }
    }

    glBindTexture(gl_target, gl_texture);

    NV2A_GL_DLABEL(GL_TEXTURE, gl_texture,
                   "offset: 0x%08lx, format: 0x%02X%s, %d dimensions%s, "
                   "width: %d, height: %d, depth: %d",
                   texture_data - g_nv2a->vram_ptr,
                   s.color_format, f.linear ? "" : " (SZ)",
                   s.dimensionality, s.cubemap ? " (Cubemap)" : "",
                   s.width, s.height, s.depth);

    if (gl_target == GL_TEXTURE_CUBE_MAP) {
        unsigned int block_size;
        if (f.gl_internal_format == GL_COMPRESSED_RGBA_S3TC_DXT1_EXT) {
            block_size = 8;
        } else {
            block_size = 16;
        }

        size_t length = 0;
        unsigned int w = s.width;
        unsigned int h = s.height;
        if (!f.linear && s.border) {
            w = MAX(16, w * 2);
            h = MAX(16, h * 2);
        }

        int level;
        for (level = 0; level < s.levels; level++) {
            if (f.gl_format == 0) {
                length += w/4 * h/4 * block_size;
            } else {
                length += w * h * f.bytes_per_pixel;
            }

            w /= 2;
            h /= 2;
        }

        length = (length + NV2A_CUBEMAP_FACE_ALIGNMENT - 1) & ~(NV2A_CUBEMAP_FACE_ALIGNMENT - 1);

        upload_gl_texture(r, GL_TEXTURE_CUBE_MAP_POSITIVE_X,
                          s, texture_data + 0 * length, palette_data);
        upload_gl_texture(r, GL_TEXTURE_CUBE_MAP_NEGATIVE_X,
                          s, texture_data + 1 * length, palette_data);
        upload_gl_texture(r, GL_TEXTURE_CUBE_MAP_POSITIVE_Y,
                          s, texture_data + 2 * length, palette_data);
        upload_gl_texture(r, GL_TEXTURE_CUBE_MAP_NEGATIVE_Y,
                          s, texture_data + 3 * length, palette_data);
        upload_gl_texture(r, GL_TEXTURE_CUBE_MAP_POSITIVE_Z,
                          s, texture_data + 4 * length, palette_data);
        upload_gl_texture(r, GL_TEXTURE_CUBE_MAP_NEGATIVE_Z,
                          s, texture_data + 5 * length, palette_data);
    } else {
        upload_gl_texture(r, gl_target, s, texture_data, palette_data);
    }

    /* Linear textures don't support mipmapping */
    if (!f.linear) {
        glTexParameteri(gl_target, GL_TEXTURE_BASE_LEVEL,
            s.min_mipmap_level);
        glTexParameteri(gl_target, GL_TEXTURE_MAX_LEVEL,
            s.levels - 1);
    }

    bool apply_swizzle = f.gl_swizzle_mask[0] != 0 || f.gl_swizzle_mask[1] != 0
        || f.gl_swizzle_mask[2] != 0 || f.gl_swizzle_mask[3] != 0;
#ifdef __ANDROID__
    if (apply_swizzle && android_texture_rgba8_upload_applies_swizzle(s)) {
        apply_swizzle = false;
    }
#endif
    if (apply_swizzle) {
#ifdef __ANDROID__
        /* GLES exposes per-channel texture swizzles, not the desktop RGBA
         * vector pname. */
        glTexParameteri(gl_target, GL_TEXTURE_SWIZZLE_R,
                        f.gl_swizzle_mask[0]);
        glTexParameteri(gl_target, GL_TEXTURE_SWIZZLE_G,
                        f.gl_swizzle_mask[1]);
        glTexParameteri(gl_target, GL_TEXTURE_SWIZZLE_B,
                        f.gl_swizzle_mask[2]);
        glTexParameteri(gl_target, GL_TEXTURE_SWIZZLE_A,
                        f.gl_swizzle_mask[3]);
#else
        glTexParameteriv(gl_target, GL_TEXTURE_SWIZZLE_RGBA,
                         (const GLint *)f.gl_swizzle_mask);
#endif
    }

    TextureBinding* ret = (TextureBinding *)g_malloc(sizeof(TextureBinding));
    ret->gl_target = gl_target;
    ret->gl_texture = gl_texture;
    ret->refcnt = 1;
    ret->draw_time = 0;
    ret->data_hash = 0;
    ret->min_filter = 0xFFFFFFFF;
    ret->mag_filter = 0xFFFFFFFF;
    ret->lod_bias = 0xFFFFFFFF;
    ret->addru = 0xFFFFFFFF;
    ret->addrv = 0xFFFFFFFF;
    ret->addrp = 0xFFFFFFFF;
    ret->border_color_set = false;
    return ret;
}

static void texture_binding_destroy(gpointer data)
{
    TextureBinding *binding = (TextureBinding *)data;
    assert(binding->refcnt > 0);
    binding->refcnt--;
    if (binding->refcnt == 0) {
        glDeleteTextures(1, &binding->gl_texture);
        g_free(binding);
    }
}

/* functions for texture LRU cache */
static void texture_cache_entry_init(Lru *lru, LruNode *node, const void *key)
{
    TextureLruNode *tnode = container_of(node, TextureLruNode, node);
    memcpy(&tnode->key, key, sizeof(TextureKey));

    tnode->binding = NULL;
    tnode->possibly_dirty = false;
}

static void texture_cache_entry_post_evict(Lru *lru, LruNode *node)
{
    TextureLruNode *tnode = container_of(node, TextureLruNode, node);
    if (tnode->binding) {
        texture_binding_destroy(tnode->binding);
        tnode->binding = NULL;
        tnode->possibly_dirty = false;
    }
}

static bool texture_cache_entry_compare(Lru *lru, LruNode *node,
                                        const void *key)
{
    TextureLruNode *tnode = container_of(node, TextureLruNode, node);
    return memcmp(&tnode->key, key, sizeof(TextureKey));
}

void pgraph_gl_init_textures(NV2AState *d)
{
    PGRAPHState *pg = &d->pgraph;
    PGRAPHGLState *r = pg->gl_renderer_state;

    const size_t texture_cache_size = 512;
    lru_init(&r->texture_cache, 1024);
    r->texture_cache_entries = malloc(texture_cache_size * sizeof(TextureLruNode));
    assert(r->texture_cache_entries != NULL);
    for (int i = 0; i < texture_cache_size; i++) {
        lru_add_free(&r->texture_cache, &r->texture_cache_entries[i].node);
    }

    r->texture_cache.init_node = texture_cache_entry_init;
    r->texture_cache.compare_nodes = texture_cache_entry_compare;
    r->texture_cache.post_node_evict = texture_cache_entry_post_evict;
}

void pgraph_gl_finalize_textures(PGRAPHState *pg)
{
    PGRAPHGLState *r = pg->gl_renderer_state;

    for (int i = 0; i < NV2A_MAX_TEXTURES; i++) {
        r->texture_binding[i] = NULL;
    }

    lru_flush(&r->texture_cache);
    free(r->texture_cache_entries);

    r->texture_cache_entries = NULL;
#ifdef __ANDROID__
    g_free(r->android_tex_conv_buf);
    r->android_tex_conv_buf = NULL;
    r->android_tex_conv_buf_size = 0;
#endif
}

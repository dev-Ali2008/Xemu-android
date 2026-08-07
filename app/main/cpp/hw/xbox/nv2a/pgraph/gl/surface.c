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

#include "hw/xbox/nv2a/pgraph/pgraph.h"
#include "ui/xemu-settings.h"
#include "hw/xbox/nv2a/nv2a_int.h"
#include "hw/xbox/nv2a/pgraph/swizzle.h"
#include "debug.h"
#include "renderer.h"

#ifdef __ANDROID__
#include <android/log.h>

#ifdef __aarch64__
#include <arm_neon.h>
#endif
#endif

#ifdef __ANDROID__
extern bool xemu_android_is_debug_logging_enabled(void);

static bool android_log_and_drain_gl_errors(const char *ctx)
{
    GLenum err;
    bool had_error = false;

    if (!xemu_android_is_debug_logging_enabled()) {
        return false;
    }

    while ((err = glGetError()) != GL_NO_ERROR) {
        __android_log_print(ANDROID_LOG_WARN, "hakuX",
                            "GL error 0x%X at %s", err, ctx);
        had_error = true;
    }

    return had_error;
}

static bool android_log_surface_download_errors(const char *ctx,
                                                const SurfaceBinding *surface)
{
    bool had_error = android_log_and_drain_gl_errors(ctx);

    if (!had_error || !surface) {
        return had_error;
    }

    __android_log_print(ANDROID_LOG_WARN, "hakuX",
                        "  surface download: kind=%s attachment=0x%X format=0x%X "
                        "type=0x%X bpp=%u size=%ux%u pitch=%u addr=0x%llX",
                        surface->color ? "color" : "zeta",
                        surface->fmt.gl_attachment,
                        surface->fmt.gl_format,
                        surface->fmt.gl_type,
                        surface->fmt.bytes_per_pixel,
                        surface->width,
                        surface->height,
                        surface->pitch,
                        (unsigned long long)surface->vram_addr);

    return had_error;
}

static bool android_drain_gl_errors_silent(void)
{
    GLenum err;
    bool had_error = false;

    if (!xemu_android_is_debug_logging_enabled()) {
        return false;
    }

    while ((err = glGetError()) != GL_NO_ERROR) {
        had_error = true;
    }

    return had_error;
}

static void android_glo_readpixels(PGRAPHGLState *r, GLenum gl_format,
                                   GLenum gl_type,
                                   unsigned int bytes_per_pixel,
                                   unsigned int stride,
                                   unsigned int width,
                                   unsigned int height, bool vflip,
                                   void *data)
{
    size_t required_size = stride * height;
    glBindBuffer(GL_PIXEL_PACK_BUFFER, r->gl_download_pbo);
    if (required_size > r->gl_download_pbo_size) {
        glBufferData(GL_PIXEL_PACK_BUFFER, required_size, NULL,
                     GL_STREAM_READ);
        r->gl_download_pbo_size = required_size;
    }

    int rl, pa;
    glGetIntegerv(GL_PACK_ROW_LENGTH, &rl);
    glGetIntegerv(GL_PACK_ALIGNMENT, &pa);
    glPixelStorei(GL_PACK_ROW_LENGTH, stride / bytes_per_pixel);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);

    glReadPixels(0, 0, width, height, gl_format, gl_type, 0);

    void *mapped_data = glMapBufferRange(GL_PIXEL_PACK_BUFFER, 0,
                                         required_size, GL_MAP_READ_BIT);
    if (mapped_data) {
        if (vflip) {
            GLubyte *b = (GLubyte *) data;
            GLubyte *c =
                &((GLubyte *) mapped_data)[stride * (height - 1)];
            for (unsigned int irow = 0; irow < height; irow++) {
                memcpy(b, c, width * bytes_per_pixel);
                b += stride;
                c -= stride;
            }
        } else {
            memcpy(data, mapped_data, required_size);
        }
        glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
    }

    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);

    glPixelStorei(GL_PACK_ROW_LENGTH, rl);
    glPixelStorei(GL_PACK_ALIGNMENT, pa);
}

static void android_sanitize_surface_format(PGRAPHGLState *r,
                                            SurfaceFormatInfo *fmt)
{
    /* Android keeps the guest surface format metadata intact and converts
     * unsupported BGRA guest layouts at upload/readback time instead. */
    (void)r;
    (void)fmt;
}

static bool android_surface_uses_rgba8_transfer(const SurfaceBinding *surface)
{
    if (!surface || !surface->color) {
        return false;
    }

    switch (surface->shape.color_format) {
    case NV097_SET_SURFACE_FORMAT_COLOR_LE_X1R5G5B5_Z1R5G5B5:
    case NV097_SET_SURFACE_FORMAT_COLOR_LE_X8R8G8B8_Z8R8G8B8:
    case NV097_SET_SURFACE_FORMAT_COLOR_LE_A8R8G8B8:
        return true;
    default:
        return false;
    }
}

static void android_surface_get_storage_format(const SurfaceBinding *surface,
                                               GLint *internal_format,
                                               GLenum *format,
                                               GLenum *type)
{
    *internal_format = surface->fmt.gl_internal_format;
    *format = surface->fmt.gl_format;
    *type = surface->fmt.gl_type;

    if (android_surface_uses_rgba8_transfer(surface)) {
        *internal_format = GL_RGBA8;
        *format = GL_RGBA;
        *type = GL_UNSIGNED_BYTE;
    }
}

static uint8_t android_expand_5_to_8(uint8_t value)
{
    return (value << 3) | (value >> 2);
}

#ifdef __aarch64__
static const uint8_t android_neon_bgra_to_rgba_perm[16] =
    {2,1,0,3, 6,5,4,7, 10,9,8,11, 14,13,12,15};
static const uint8_t android_neon_b8g8r8a8_to_rgba_perm[16] =
    {1,2,3,0, 5,6,7,4, 9,10,11,8, 13,14,15,12};
static const uint8_t android_neon_r8g8b8a8_to_rgba_perm[16] =
    {3,2,1,0, 7,6,5,4, 11,10,9,8, 15,14,13,12};
static const uint8_t android_neon_force_alpha_mask[16] =
    {0,0,0,0xFF, 0,0,0,0xFF, 0,0,0,0xFF, 0,0,0,0xFF};

static inline void android_neon_shuffle_row_4bpp(const uint8_t *src_row,
                                                 uint8_t *dst_row,
                                                 unsigned int width,
                                                 const uint8_t perm_arr[16],
                                                 bool force_opaque_alpha)
{
    uint8x16_t vperm = vld1q_u8(perm_arr);
    uint8x16_t valpha_mask = force_opaque_alpha
        ? vld1q_u8(android_neon_force_alpha_mask)
        : vdupq_n_u8(0);
    const uint8_t *src = src_row;
    uint8_t *dst = dst_row;
    unsigned int remaining = width;

    while (remaining >= 4) {
        uint8x16_t v = vqtbl1q_u8(vld1q_u8(src), vperm);
        vst1q_u8(dst, vorrq_u8(v, valpha_mask));
        src += 16;
        dst += 16;
        remaining -= 4;
    }

    while (remaining-- > 0) {
        dst[0] = src[perm_arr[0]];
        dst[1] = src[perm_arr[1]];
        dst[2] = src[perm_arr[2]];
        dst[3] = force_opaque_alpha ? 0xFF : src[perm_arr[3]];
        src += 4;
        dst += 4;
    }
}

static inline void android_neon_a1x1r5g5b5_to_rgba8_row(
    const uint8_t *src_row,
    uint8_t *dst_row,
    unsigned int width,
    bool preserve_alpha)
{
    const uint16x8_t mask_5 = vdupq_n_u16(0x1F);
    unsigned int remaining = width;
    const uint16_t *src = (const uint16_t *)src_row;
    uint8_t *dst = dst_row;

    while (remaining >= 8) {
        uint16x8_t pixels = vld1q_u16(src);
        uint16x8_t r5 = vandq_u16(vshrq_n_u16(pixels, 10), mask_5);
        uint16x8_t g5 = vandq_u16(vshrq_n_u16(pixels, 5), mask_5);
        uint16x8_t b5 = vandq_u16(pixels, mask_5);
        uint8x8_t r8 = vmovn_u16(vorrq_u16(vshlq_n_u16(r5, 3),
                                           vshrq_n_u16(r5, 2)));
        uint8x8_t g8 = vmovn_u16(vorrq_u16(vshlq_n_u16(g5, 3),
                                           vshrq_n_u16(g5, 2)));
        uint8x8_t b8 = vmovn_u16(vorrq_u16(vshlq_n_u16(b5, 3),
                                           vshrq_n_u16(b5, 2)));
        uint8x8_t a8 = preserve_alpha
            ? vmovn_u16(vceqq_u16(vshrq_n_u16(pixels, 15), vdupq_n_u16(1)))
            : vdup_n_u8(0xFF);
        uint8x8x4_t rgba = { { r8, g8, b8, a8 } };

        vst4_u8(dst, rgba);
        src += 8;
        dst += 32;
        remaining -= 8;
    }

    while (remaining-- > 0) {
        uint16_t pixel = *src++;
        dst[0] = android_expand_5_to_8((pixel >> 10) & 0x1F);
        dst[1] = android_expand_5_to_8((pixel >> 5) & 0x1F);
        dst[2] = android_expand_5_to_8(pixel & 0x1F);
        dst[3] = preserve_alpha ? ((pixel & 0x8000) ? 0xFF : 0x00) : 0xFF;
        dst += 4;
    }
}

static inline void android_neon_r5g6b5_to_rgba8_row(const uint8_t *src_row,
                                                    uint8_t *dst_row,
                                                    unsigned int width)
{
    const uint16x8_t mask_5 = vdupq_n_u16(0x1F);
    const uint16x8_t mask_6 = vdupq_n_u16(0x3F);
    unsigned int remaining = width;
    const uint16_t *src = (const uint16_t *)src_row;
    uint8_t *dst = dst_row;

    while (remaining >= 8) {
        uint16x8_t pixels = vld1q_u16(src);
        uint16x8_t r5 = vandq_u16(vshrq_n_u16(pixels, 11), mask_5);
        uint16x8_t g6 = vandq_u16(vshrq_n_u16(pixels, 5), mask_6);
        uint16x8_t b5 = vandq_u16(pixels, mask_5);
        uint8x8_t r8 = vmovn_u16(vorrq_u16(vshlq_n_u16(r5, 3),
                                           vshrq_n_u16(r5, 2)));
        uint8x8_t g8 = vmovn_u16(vorrq_u16(vshlq_n_u16(g6, 2),
                                           vshrq_n_u16(g6, 4)));
        uint8x8_t b8 = vmovn_u16(vorrq_u16(vshlq_n_u16(b5, 3),
                                           vshrq_n_u16(b5, 2)));
        uint8x8x4_t rgba = { { r8, g8, b8, vdup_n_u8(0xFF) } };

        vst4_u8(dst, rgba);
        src += 8;
        dst += 32;
        remaining -= 8;
    }

    while (remaining-- > 0) {
        uint16_t pixel = *src++;
        dst[0] = android_expand_5_to_8((pixel >> 11) & 0x1F);
        dst[1] = (uint8_t)(((pixel >> 5) & 0x3F) * 255 / 63);
        dst[2] = android_expand_5_to_8(pixel & 0x1F);
        dst[3] = 0xFF;
        dst += 4;
    }
}

static inline void android_neon_rgba8_to_x1r5g5b5_row(const uint8_t *src_row,
                                                      uint8_t *dst_row,
                                                      unsigned int width)
{
    unsigned int remaining = width;
    const uint8_t *src = src_row;
    uint16_t *dst = (uint16_t *)dst_row;

    while (remaining >= 8) {
        uint8x8x4_t rgba = vld4_u8(src);
        uint16x8_t r = vshlq_n_u16(vmovl_u8(vshr_n_u8(rgba.val[0], 3)), 10);
        uint16x8_t g = vshlq_n_u16(vmovl_u8(vshr_n_u8(rgba.val[1], 3)), 5);
        uint16x8_t b = vmovl_u8(vshr_n_u8(rgba.val[2], 3));
        uint16x8_t packed =
            vorrq_u16(vdupq_n_u16(0x8000), vorrq_u16(r, vorrq_u16(g, b)));

        vst1q_u16(dst, packed);
        src += 32;
        dst += 8;
        remaining -= 8;
    }

    while (remaining-- > 0) {
        uint16_t packed =
            0x8000 |
            ((uint16_t)(src[0] >> 3) << 10) |
            ((uint16_t)(src[1] >> 3) << 5) |
            (uint16_t)(src[2] >> 3);
        *dst++ = packed;
        src += 4;
    }
}

static inline bool android_neon_surface_copy_shrink_row_4bpp(
    uint8_t *out,
    const uint8_t *in,
    unsigned int width,
    unsigned int factor)
{
    uint32_t *dst = (uint32_t *)out;
    const uint32_t *src = (const uint32_t *)in;
    unsigned int remaining = width;

    switch (factor) {
    case 2:
        while (remaining >= 4) {
            uint32x4x2_t pixels = vld2q_u32(src);
            vst1q_u32(dst, pixels.val[0]);
            src += 8;
            dst += 4;
            remaining -= 4;
        }
        break;
    case 3:
        while (remaining >= 4) {
            uint32x4x3_t pixels = vld3q_u32(src);
            vst1q_u32(dst, pixels.val[0]);
            src += 12;
            dst += 4;
            remaining -= 4;
        }
        break;
    case 4:
        while (remaining >= 4) {
            uint32x4x4_t pixels = vld4q_u32(src);
            vst1q_u32(dst, pixels.val[0]);
            src += 16;
            dst += 4;
            remaining -= 4;
        }
        break;
    default:
        return false;
    }

    out = (uint8_t *)dst;
    in = (const uint8_t *)src;
    while (remaining-- > 0) {
        *(uint32_t *)out = *(const uint32_t *)in;
        out += 4;
        in += 4 * factor;
    }

    return true;
}

static inline bool android_neon_surface_copy_shrink_row_2bpp(
    uint8_t *out,
    const uint8_t *in,
    unsigned int width,
    unsigned int factor)
{
    uint16_t *dst = (uint16_t *)out;
    const uint16_t *src = (const uint16_t *)in;
    unsigned int remaining = width;

    switch (factor) {
    case 2:
        while (remaining >= 8) {
            uint16x8x2_t pixels = vld2q_u16(src);
            vst1q_u16(dst, pixels.val[0]);
            src += 16;
            dst += 8;
            remaining -= 8;
        }
        break;
    case 3:
        while (remaining >= 8) {
            uint16x8x3_t pixels = vld3q_u16(src);
            vst1q_u16(dst, pixels.val[0]);
            src += 24;
            dst += 8;
            remaining -= 8;
        }
        break;
    case 4:
        while (remaining >= 8) {
            uint16x8x4_t pixels = vld4q_u16(src);
            vst1q_u16(dst, pixels.val[0]);
            src += 32;
            dst += 8;
            remaining -= 8;
        }
        break;
    default:
        return false;
    }

    out = (uint8_t *)dst;
    in = (const uint8_t *)src;
    while (remaining-- > 0) {
        *(uint16_t *)out = *(const uint16_t *)in;
        out += 2;
        in += 2 * factor;
    }

    return true;
}

static inline void android_neon_pack_depth16_row_to_guest(const uint8_t *src_row,
                                                          uint8_t *dst_row,
                                                          unsigned int width)
{
    unsigned int remaining = width;

    while (remaining >= 8) {
        uint8x8x4_t rgba = vld4_u8(src_row);
        uint8x8x2_t depth16 = { { rgba.val[0], rgba.val[1] } };

        vst2_u8(dst_row, depth16);
        src_row += 32;
        dst_row += 16;
        remaining -= 8;
    }

    while (remaining-- > 0) {
        dst_row[0] = src_row[0];
        dst_row[1] = src_row[1];
        src_row += 4;
        dst_row += 2;
    }
}

static inline void android_neon_pack_z24s8_row_to_guest(
    const uint8_t *depth_row,
    const uint8_t *stencil_row,
    uint8_t *dst_row,
    unsigned int width)
{
    unsigned int remaining = width;

    while (remaining >= 8) {
        uint8x8x4_t depth = vld4_u8(depth_row);
        uint8x8x4_t z24s8 = { {
            vld1_u8(stencil_row),
            depth.val[0],
            depth.val[1],
            depth.val[2],
        } };

        vst4_u8(dst_row, z24s8);
        depth_row += 32;
        stencil_row += 8;
        dst_row += 32;
        remaining -= 8;
    }

    while (remaining-- > 0) {
        dst_row[0] = *stencil_row++;
        dst_row[1] = depth_row[0];
        dst_row[2] = depth_row[1];
        dst_row[3] = depth_row[2];
        depth_row += 4;
        dst_row += 4;
    }
}
#endif

static void android_surface_guest_to_rgba8(const SurfaceBinding *surface,
                                           const uint8_t *src,
                                           unsigned int width,
                                           unsigned int height,
                                           unsigned int src_stride,
                                           uint8_t *dst)
{
    unsigned int x, y;

    switch (surface->shape.color_format) {
    case NV097_SET_SURFACE_FORMAT_COLOR_LE_X1R5G5B5_Z1R5G5B5:
        for (y = 0; y < height; y++) {
            const uint8_t *src_row = src + y * src_stride;
            uint8_t *dst_row = dst + y * width * 4;
            for (x = 0; x < width; x++) {
                uint16_t pixel = lduw_le_p(src_row + x * 2);
                dst_row[x * 4 + 0] = android_expand_5_to_8((pixel >> 10) & 0x1F);
                dst_row[x * 4 + 1] = android_expand_5_to_8((pixel >> 5) & 0x1F);
                dst_row[x * 4 + 2] = android_expand_5_to_8(pixel & 0x1F);
                dst_row[x * 4 + 3] = 0xFF;
            }
        }
        break;
    case NV097_SET_SURFACE_FORMAT_COLOR_LE_X8R8G8B8_Z8R8G8B8:
    case NV097_SET_SURFACE_FORMAT_COLOR_LE_A8R8G8B8:
    {
        bool preserve_alpha = (surface->shape.color_format ==
                               NV097_SET_SURFACE_FORMAT_COLOR_LE_A8R8G8B8);
        for (y = 0; y < height; y++) {
            const uint8_t *src_row = src + y * src_stride;
            uint8_t *dst_row = dst + y * width * 4;
#ifdef __aarch64__
            /* vqtbl1q_u8: 16-byte shuffle, processes 4 pixels per instruction.
             * Permutation swaps R (byte 2) and B (byte 0) within each pixel. */
            static const uint8_t perm_arr[16] =
                {2,1,0,3, 6,5,4,7, 10,9,8,11, 14,13,12,15};
            uint8x16_t vperm = vld1q_u8(perm_arr);
            /* For X8R8G8B8, force alpha=0xFF by ORing a pre-built mask. */
            static const uint8_t alpha_mask_arr[16] =
                {0,0,0,0xFF, 0,0,0,0xFF, 0,0,0,0xFF, 0,0,0,0xFF};
            uint8x16_t valpha_mask = preserve_alpha
                ? vdupq_n_u8(0)
                : vld1q_u8(alpha_mask_arr);
            unsigned int px = width;
            while (px >= 4) {
                uint8x16_t v = vqtbl1q_u8(vld1q_u8(src_row), vperm);
                vst1q_u8(dst_row, vorrq_u8(v, valpha_mask));
                src_row += 16; dst_row += 16; px -= 4;
            }
            /* scalar tail for widths not divisible by 4 */
            while (px-- > 0) {
                dst_row[0] = src_row[2];
                dst_row[1] = src_row[1];
                dst_row[2] = src_row[0];
                dst_row[3] = preserve_alpha ? src_row[3] : 0xFF;
                src_row += 4; dst_row += 4;
            }
#else
            for (x = 0; x < width; x++) {
                const uint8_t *pixel = src_row + x * 4;
                dst_row[x * 4 + 0] = pixel[2];
                dst_row[x * 4 + 1] = pixel[1];
                dst_row[x * 4 + 2] = pixel[0];
                dst_row[x * 4 + 3] = preserve_alpha ? pixel[3] : 0xFF;
            }
#endif
        }
        break;
    }
    default:
        g_assert_not_reached();
    }
}

static bool android_surface_to_texture_needs_guest_reinterpretation(
    const SurfaceBinding *surface,
    const TextureShape *shape)
{
    switch (surface->shape.color_format) {
    case NV097_SET_SURFACE_FORMAT_COLOR_LE_X1R5G5B5_Z1R5G5B5:
        return shape->color_format !=
               NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_X1R5G5B5 &&
               shape->color_format !=
               NV097_SET_TEXTURE_FORMAT_COLOR_SZ_X1R5G5B5;
    case NV097_SET_SURFACE_FORMAT_COLOR_LE_R5G6B5:
        return false;
    case NV097_SET_SURFACE_FORMAT_COLOR_LE_X8R8G8B8_Z8R8G8B8:
        return shape->color_format !=
               NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_X8R8G8B8 &&
               shape->color_format !=
               NV097_SET_TEXTURE_FORMAT_COLOR_SZ_X8R8G8B8;
    case NV097_SET_SURFACE_FORMAT_COLOR_LE_A8R8G8B8:
        return shape->color_format !=
               NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_A8R8G8B8 &&
               shape->color_format !=
               NV097_SET_TEXTURE_FORMAT_COLOR_SZ_A8R8G8B8;
    default:
        return true;
    }
}

static void android_surface_guest_to_texture_rgba8(
    const TextureShape *shape,
    const uint8_t *src,
    unsigned int width,
    unsigned int height,
    unsigned int src_stride,
    uint8_t *dst)
{
    unsigned int x, y;

    switch (shape->color_format) {
    case NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_A1R5G5B5:
    case NV097_SET_TEXTURE_FORMAT_COLOR_SZ_A1R5G5B5:
        for (y = 0; y < height; y++) {
            const uint8_t *src_row = src + y * src_stride;
            uint8_t *dst_row = dst + y * width * 4;
#ifdef __aarch64__
            android_neon_a1x1r5g5b5_to_rgba8_row(src_row, dst_row, width, true);
#else
            for (x = 0; x < width; x++) {
                uint16_t pixel = lduw_le_p(src_row + x * 2);
                uint8_t *out = dst_row + x * 4;
                out[0] = android_expand_5_to_8((pixel >> 10) & 0x1F);
                out[1] = android_expand_5_to_8((pixel >> 5) & 0x1F);
                out[2] = android_expand_5_to_8(pixel & 0x1F);
                out[3] = (pixel & 0x8000) ? 0xFF : 0x00;
            }
#endif
        }
        break;
    case NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_X1R5G5B5:
    case NV097_SET_TEXTURE_FORMAT_COLOR_SZ_X1R5G5B5:
        for (y = 0; y < height; y++) {
            const uint8_t *src_row = src + y * src_stride;
            uint8_t *dst_row = dst + y * width * 4;
#ifdef __aarch64__
            android_neon_a1x1r5g5b5_to_rgba8_row(src_row, dst_row, width, false);
#else
            for (x = 0; x < width; x++) {
                uint16_t pixel = lduw_le_p(src_row + x * 2);
                uint8_t *out = dst_row + x * 4;
                out[0] = android_expand_5_to_8((pixel >> 10) & 0x1F);
                out[1] = android_expand_5_to_8((pixel >> 5) & 0x1F);
                out[2] = android_expand_5_to_8(pixel & 0x1F);
                out[3] = 0xFF;
            }
#endif
        }
        break;
    case NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_R5G6B5:
    case NV097_SET_TEXTURE_FORMAT_COLOR_SZ_R5G6B5:
        for (y = 0; y < height; y++) {
            const uint8_t *src_row = src + y * src_stride;
            uint8_t *dst_row = dst + y * width * 4;
#ifdef __aarch64__
            android_neon_r5g6b5_to_rgba8_row(src_row, dst_row, width);
#else
            for (x = 0; x < width; x++) {
                uint16_t pixel = lduw_le_p(src_row + x * 2);
                uint8_t *out = dst_row + x * 4;
                out[0] = android_expand_5_to_8((pixel >> 11) & 0x1F);
                out[1] = (uint8_t)(((pixel >> 5) & 0x3F) * 255 / 63);
                out[2] = android_expand_5_to_8(pixel & 0x1F);
                out[3] = 0xFF;
            }
#endif
        }
        break;
    case NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_A8R8G8B8:
    case NV097_SET_TEXTURE_FORMAT_COLOR_SZ_A8R8G8B8:
    case NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_X8R8G8B8:
    case NV097_SET_TEXTURE_FORMAT_COLOR_SZ_X8R8G8B8:
    {
        bool force_opaque_alpha =
            (shape->color_format == NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_X8R8G8B8 ||
             shape->color_format == NV097_SET_TEXTURE_FORMAT_COLOR_SZ_X8R8G8B8);

        for (y = 0; y < height; y++) {
            const uint8_t *src_row = src + y * src_stride;
            uint8_t *dst_row = dst + y * width * 4;
#ifdef __aarch64__
            android_neon_shuffle_row_4bpp(src_row, dst_row, width,
                                          android_neon_bgra_to_rgba_perm,
                                          force_opaque_alpha);
#else
            for (x = 0; x < width; x++) {
                const uint8_t *pixel = src_row + x * 4;
                uint8_t *out = dst_row + x * 4;
                out[0] = pixel[2];
                out[1] = pixel[1];
                out[2] = pixel[0];
                out[3] = force_opaque_alpha ? 0xFF : pixel[3];
            }
#endif
        }
        break;
    }
    case NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_A8B8G8R8:
    case NV097_SET_TEXTURE_FORMAT_COLOR_SZ_A8B8G8R8:
        for (y = 0; y < height; y++) {
            memcpy(dst + y * width * 4, src + y * src_stride, width * 4);
        }
        break;
    case NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_B8G8R8A8:
    case NV097_SET_TEXTURE_FORMAT_COLOR_SZ_B8G8R8A8:
        for (y = 0; y < height; y++) {
            const uint8_t *src_row = src + y * src_stride;
            uint8_t *dst_row = dst + y * width * 4;
#ifdef __aarch64__
            android_neon_shuffle_row_4bpp(src_row, dst_row, width,
                                          android_neon_b8g8r8a8_to_rgba_perm,
                                          false);
#else
            for (x = 0; x < width; x++) {
                const uint8_t *pixel = src_row + x * 4;
                uint8_t *out = dst_row + x * 4;
                out[0] = pixel[1];
                out[1] = pixel[2];
                out[2] = pixel[3];
                out[3] = pixel[0];
            }
#endif
        }
        break;
    case NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_R8G8B8A8:
    case NV097_SET_TEXTURE_FORMAT_COLOR_SZ_R8G8B8A8:
        for (y = 0; y < height; y++) {
            const uint8_t *src_row = src + y * src_stride;
            uint8_t *dst_row = dst + y * width * 4;
#ifdef __aarch64__
            android_neon_shuffle_row_4bpp(src_row, dst_row, width,
                                          android_neon_r8g8b8a8_to_rgba_perm,
                                          false);
#else
            for (x = 0; x < width; x++) {
                const uint8_t *pixel = src_row + x * 4;
                uint8_t *out = dst_row + x * 4;
                out[0] = pixel[3];
                out[1] = pixel[2];
                out[2] = pixel[1];
                out[3] = pixel[0];
            }
#endif
        }
        break;
    default:
        g_assert_not_reached();
    }
}

static void android_surface_rgba8_to_guest(const SurfaceBinding *surface,
                                           const uint8_t *src,
                                           unsigned int src_stride,
                                           unsigned int width,
                                           unsigned int height,
                                           uint8_t *dst,
                                           unsigned int dst_stride)
{
    unsigned int x, y;

    switch (surface->shape.color_format) {
    case NV097_SET_SURFACE_FORMAT_COLOR_LE_X1R5G5B5_Z1R5G5B5:
        for (y = 0; y < height; y++) {
            const uint8_t *src_row = src + y * src_stride;
            uint8_t *dst_row = dst + y * dst_stride;
#ifdef __aarch64__
            android_neon_rgba8_to_x1r5g5b5_row(src_row, dst_row, width);
#else
            for (x = 0; x < width; x++) {
                const uint8_t *pixel = src_row + x * 4;
                uint16_t packed =
                    0x8000 |
                    ((uint16_t)(pixel[0] >> 3) << 10) |
                    ((uint16_t)(pixel[1] >> 3) << 5) |
                    (uint16_t)(pixel[2] >> 3);
                stw_le_p(dst_row + x * 2, packed);
            }
#endif
        }
        break;
    case NV097_SET_SURFACE_FORMAT_COLOR_LE_X8R8G8B8_Z8R8G8B8:
    case NV097_SET_SURFACE_FORMAT_COLOR_LE_A8R8G8B8:
    {
        bool force_opaque_alpha =
            (surface->shape.color_format !=
             NV097_SET_SURFACE_FORMAT_COLOR_LE_A8R8G8B8);
        for (y = 0; y < height; y++) {
            const uint8_t *src_row = src + y * src_stride;
            uint8_t *dst_row = dst + y * dst_stride;
#ifdef __aarch64__
            android_neon_shuffle_row_4bpp(src_row, dst_row, width,
                                          android_neon_bgra_to_rgba_perm,
                                          force_opaque_alpha);
#else
            for (x = 0; x < width; x++) {
                const uint8_t *pixel = src_row + x * 4;
                uint8_t *out = dst_row + x * 4;
                out[0] = pixel[2];
                out[1] = pixel[1];
                out[2] = pixel[0];
                out[3] =
                    (surface->shape.color_format ==
                     NV097_SET_SURFACE_FORMAT_COLOR_LE_A8R8G8B8)
                        ? pixel[3]
                        : 0xFF;
            }
#endif
        }
        break;
    }
    default:
        g_assert_not_reached();
    }
}

static bool android_surface_to_texture_rgba8_compatible(
    const SurfaceBinding *surface,
    const TextureShape *shape)
{
    if (!surface || !shape || !surface->color || shape->cubemap ||
        shape->levels > 1) {
        return false;
    }

    switch (surface->shape.color_format) {
    case NV097_SET_SURFACE_FORMAT_COLOR_LE_X1R5G5B5_Z1R5G5B5:
        switch (shape->color_format) {
        case NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_X1R5G5B5:
        case NV097_SET_TEXTURE_FORMAT_COLOR_SZ_X1R5G5B5:
        case NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_A1R5G5B5:
        case NV097_SET_TEXTURE_FORMAT_COLOR_SZ_A1R5G5B5:
            return true;
        default:
            return false;
        }
    case NV097_SET_SURFACE_FORMAT_COLOR_LE_R5G6B5:
        switch (shape->color_format) {
        case NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_R5G6B5:
        case NV097_SET_TEXTURE_FORMAT_COLOR_SZ_R5G6B5:
            return true;
        default:
            return false;
        }
    case NV097_SET_SURFACE_FORMAT_COLOR_LE_X8R8G8B8_Z8R8G8B8:
        switch (shape->color_format) {
        case NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_X8R8G8B8:
        case NV097_SET_TEXTURE_FORMAT_COLOR_SZ_X8R8G8B8:
            return true;
        default:
            return false;
        }
    case NV097_SET_SURFACE_FORMAT_COLOR_LE_A8R8G8B8:
        switch (shape->color_format) {
        case NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_A8R8G8B8:
        case NV097_SET_TEXTURE_FORMAT_COLOR_SZ_A8R8G8B8:
        case NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_A8B8G8R8:
        case NV097_SET_TEXTURE_FORMAT_COLOR_SZ_A8B8G8R8:
        case NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_B8G8R8A8:
        case NV097_SET_TEXTURE_FORMAT_COLOR_SZ_B8G8R8A8:
        case NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_R8G8B8A8:
        case NV097_SET_TEXTURE_FORMAT_COLOR_SZ_R8G8B8A8:
            return true;
        default:
            return false;
        }
    default:
        return false;
    }
}
#endif

static void surface_download(NV2AState *d, SurfaceBinding *surface, bool force);
static void surface_download_to_buffer(NV2AState *d, SurfaceBinding *surface,
                                       bool swizzle, bool flip, bool downscale,
                                       uint8_t *pixels);
static void surface_get_dimensions(PGRAPHState *pg, unsigned int *width, unsigned int *height);

void pgraph_gl_set_surface_scale_factor(NV2AState *d, float scale)
{
    PGRAPHState *pg = &d->pgraph;
    PGRAPHGLState *r = pg->gl_renderer_state;

    g_config.display.quality.surface_scale = scale < 0.5f ? 0.5f : scale;

    qemu_mutex_lock(&d->pfifo.lock);
    qatomic_set(&d->pfifo.halt, true);
    qemu_mutex_unlock(&d->pfifo.lock);

    qemu_mutex_lock(&d->pgraph.lock);
    qemu_event_reset(&r->dirty_surfaces_download_complete);
    qatomic_set(&r->download_dirty_surfaces_pending, true);
    qemu_mutex_unlock(&d->pgraph.lock);
    qemu_mutex_lock(&d->pfifo.lock);
    pfifo_kick(d);
    qemu_mutex_unlock(&d->pfifo.lock);
    qemu_event_wait(&r->dirty_surfaces_download_complete);

    qemu_mutex_lock(&d->pgraph.lock);
    qemu_event_reset(&d->pgraph.flush_complete);
    qatomic_set(&d->pgraph.flush_pending, true);
    qemu_mutex_unlock(&d->pgraph.lock);
    qemu_mutex_lock(&d->pfifo.lock);
    pfifo_kick(d);
    qemu_mutex_unlock(&d->pfifo.lock);
    qemu_event_wait(&d->pgraph.flush_complete);

    qemu_mutex_lock(&d->pfifo.lock);
    qatomic_set(&d->pfifo.halt, false);
    pfifo_kick(d);
    qemu_mutex_unlock(&d->pfifo.lock);
}

float pgraph_gl_get_surface_scale_factor(NV2AState *d)
{
    return d->pgraph.surface_scale_factor;
}

void pgraph_gl_reload_surface_scale_factor(PGRAPHState *pg)
{
    float factor = g_config.display.quality.surface_scale;
    pg->surface_scale_factor = factor < 0.5f ? 0.5f : factor;
}

// FIXME: Move to common
static bool framebuffer_dirty(PGRAPHState *pg)
{
    bool shape_changed = memcmp(&pg->surface_shape, &pg->last_surface_shape,
                                sizeof(SurfaceShape)) != 0;
    if (!shape_changed || (!pg->surface_shape.color_format
            && !pg->surface_shape.zeta_format)) {
        return false;
    }
    return true;
}

void pgraph_gl_set_surface_dirty(PGRAPHState *pg, bool color, bool zeta)
{
    PGRAPHGLState *r = pg->gl_renderer_state;

    NV2A_DPRINTF("pgraph_set_surface_dirty(%d, %d) -- %d %d\n",
                 color, zeta,
                 pgraph_color_write_enabled(pg), pgraph_zeta_write_enabled(pg));
    /* FIXME: Does this apply to CLEARs too? */
    color = color && pgraph_color_write_enabled(pg);
    zeta = zeta && pgraph_zeta_write_enabled(pg);
    pg->surface_color.draw_dirty |= color;
    pg->surface_zeta.draw_dirty |= zeta;

    if (r->color_binding) {
        r->color_binding->draw_dirty |= color;
        r->color_binding->frame_time = pg->frame_time;
        r->color_binding->cleared = false;

    }

    if (r->zeta_binding) {
        r->zeta_binding->draw_dirty |= zeta;
        r->zeta_binding->frame_time = pg->frame_time;
        r->zeta_binding->cleared = false;

    }
}

static void init_render_to_texture(PGRAPHState *pg)
{
    PGRAPHGLState *r = pg->gl_renderer_state;

    const char *vs =
#ifdef __ANDROID__
        "#version 300 es\n"
        "precision highp float;\n"
#else
        "#version 330\n"
#endif
        "void main()\n"
        "{\n"
        "    float x = -1.0 + float((gl_VertexID & 1) << 2);\n"
        "    float y = -1.0 + float((gl_VertexID & 2) << 1);\n"
        "    gl_Position = vec4(x, y, 0, 1);\n"
        "}\n";
    const char *fs =
#ifdef __ANDROID__
        "#version 300 es\n"
        "precision highp float;\n"
        "precision highp int;\n"
#else
        "#version 330\n"
#endif
        "uniform sampler2D tex;\n"
        "uniform vec2 surface_size;\n"
        "layout(location = 0) out vec4 out_Color;\n"
        "void main()\n"
        "{\n"
        "    vec2 texSize = vec2(textureSize(tex, 0));\n"
        "    vec2 texCoord = gl_FragCoord.xy / texSize;\n"
        "    out_Color.rgba = texture(tex, texCoord);\n"
        "}\n";
#ifdef __ANDROID__
    const char *depth_fs =
        "#version 300 es\n"
        "precision highp float;\n"
        "precision highp int;\n"
        "precision highp sampler2D;\n"
        "uniform sampler2D tex;\n"
        "uniform float depth_scale;\n"
        "layout(location = 0) out vec4 out_Color;\n"
        "void main()\n"
        "{\n"
        "    float depth = texelFetch(tex, ivec2(gl_FragCoord.xy), 0).r;\n"
        "    uint packed = uint(clamp(depth * depth_scale + 0.5, 0.0, depth_scale));\n"
        "    out_Color = vec4(float(packed & 0xFFu) / 255.0,\n"
        "                     float((packed >> 8) & 0xFFu) / 255.0,\n"
        "                     float((packed >> 16) & 0xFFu) / 255.0,\n"
        "                     1.0);\n"
        "}\n";
#endif

    r->s2t_rndr.prog = pgraph_gl_compile_shader(vs, fs);
    r->s2t_rndr.tex_loc = glGetUniformLocation(r->s2t_rndr.prog, "tex");
    r->s2t_rndr.surface_size_loc = glGetUniformLocation(r->s2t_rndr.prog,
                                                    "surface_size");
#ifdef __ANDROID__
    r->s2t_rndr.depth_prog = pgraph_gl_compile_shader(vs, depth_fs);
    r->s2t_rndr.depth_tex_loc =
        glGetUniformLocation(r->s2t_rndr.depth_prog, "tex");
    r->s2t_rndr.depth_scale_loc =
        glGetUniformLocation(r->s2t_rndr.depth_prog, "depth_scale");
#endif

    glGenVertexArrays(1, &r->s2t_rndr.vao);
    glBindVertexArray(r->s2t_rndr.vao);
    glGenBuffers(1, &r->s2t_rndr.vbo);
    glBindBuffer(GL_ARRAY_BUFFER, r->s2t_rndr.vbo);
    glBufferData(GL_ARRAY_BUFFER, 0, NULL, GL_STATIC_DRAW);
    glGenFramebuffers(1, &r->s2t_rndr.fbo);
}

static void finalize_render_to_texture(PGRAPHState *pg)
{
    PGRAPHGLState *r = pg->gl_renderer_state;

    glDeleteProgram(r->s2t_rndr.prog);
    r->s2t_rndr.prog = 0;
#ifdef __ANDROID__
    glDeleteProgram(r->s2t_rndr.depth_prog);
    r->s2t_rndr.depth_prog = 0;
#endif

    glDeleteVertexArrays(1, &r->s2t_rndr.vao);
    r->s2t_rndr.vao = 0;

    glDeleteBuffers(1, &r->s2t_rndr.vbo);
    r->s2t_rndr.vbo = 0;

    glDeleteFramebuffers(1, &r->s2t_rndr.fbo);
    r->s2t_rndr.fbo = 0;
}

static bool surface_to_texture_can_fastpath(SurfaceBinding *surface,
                                            TextureShape *shape)
{
    // FIXME: Better checks/handling on formats and surface-texture compat

    int surface_fmt = surface->shape.color_format;
    int texture_fmt = shape->color_format;

    if (!surface->color) {
        // FIXME: Support zeta to color
        return false;
    }

#ifdef __ANDROID__
    if (android_surface_to_texture_rgba8_compatible(surface, shape)) {
        return true;
    }
#endif

    switch (surface_fmt) {
    case NV097_SET_SURFACE_FORMAT_COLOR_LE_X1R5G5B5_Z1R5G5B5: switch (texture_fmt) {
        case NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_X1R5G5B5: return true;
        default: break;
        }
        break;
    case NV097_SET_SURFACE_FORMAT_COLOR_LE_R5G6B5: switch (texture_fmt) {
        case NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_R5G6B5: return true;
        case NV097_SET_TEXTURE_FORMAT_COLOR_SZ_R5G6B5: return true;
        default: break;
        }
        break;
    case NV097_SET_SURFACE_FORMAT_COLOR_LE_X8R8G8B8_Z8R8G8B8: switch(texture_fmt) {
        case NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_X8R8G8B8: return true;
        case NV097_SET_TEXTURE_FORMAT_COLOR_SZ_X8R8G8B8: return true;
        default: break;
        }
        break;
    case NV097_SET_SURFACE_FORMAT_COLOR_LE_A8R8G8B8: switch (texture_fmt) {
        case NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_A8B8G8R8: return true;
        case NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_R8G8B8A8: return true;
        case NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_A8R8G8B8: return true;
        case NV097_SET_TEXTURE_FORMAT_COLOR_SZ_A8R8G8B8: return true;
        default: break;
        }
        break;
    default: break;
    }

    trace_nv2a_pgraph_surface_texture_compat_failed(
        surface_fmt, texture_fmt);
    return false;
}

static bool render_surface_to(NV2AState *d, SurfaceBinding *surface,
                              int texture_unit, GLuint gl_target,
                              GLuint gl_texture, unsigned int width,
                              unsigned int height)
{
    PGRAPHState *pg = &d->pgraph;
    PGRAPHGLState *r = pg->gl_renderer_state;

#ifdef __ANDROID__
    if (gl_target != GL_TEXTURE_2D) {
        return false;
    }
#endif

    glActiveTexture(GL_TEXTURE0 + texture_unit);
    glBindFramebuffer(GL_FRAMEBUFFER, r->s2t_rndr.fbo);

    GLenum draw_buffers[1] = { GL_COLOR_ATTACHMENT0 };
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, gl_target,
                           gl_texture, 0);
    glDrawBuffers(1, draw_buffers);
    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
#ifdef __ANDROID__
        const char *status_str = "unknown";
        switch (status) {
        case GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT:
            status_str = "incomplete_attachment";
            break;
        case GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT:
            status_str = "missing_attachment";
            break;
        case GL_FRAMEBUFFER_UNSUPPORTED:
            status_str = "unsupported";
            break;
        default:
            break;
        }
        fprintf(stderr, "nv2a: render_surface_to FBO incomplete (%s=0x%x)\n",
                status_str, status);
#endif
        glBindFramebuffer(GL_FRAMEBUFFER, r->gl_framebuffer);
        return false;
    }
#ifndef __ANDROID__
    assert(glGetError() == GL_NO_ERROR);
#else
    if (android_log_and_drain_gl_errors("render_surface_to: fbo setup")) {
        glBindFramebuffer(GL_FRAMEBUFFER, r->gl_framebuffer);
        return false;
    }
#endif

    float color[] = { 0.0f, 0.0f, 0.0f, 0.0f };
    glBindTexture(GL_TEXTURE_2D, surface->gl_buffer);
#ifdef __ANDROID__
    if (r->supported_extensions.texture_border_clamp) {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, NV2A_GL_CLAMP_TO_BORDER);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, NV2A_GL_CLAMP_TO_BORDER);
        glTexParameterfv(GL_TEXTURE_2D, NV2A_GL_TEXTURE_BORDER_COLOR, color);
    } else {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
#else
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, NV2A_GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, NV2A_GL_CLAMP_TO_BORDER);
    glTexParameterfv(GL_TEXTURE_2D, NV2A_GL_TEXTURE_BORDER_COLOR, color);
#endif

    glBindVertexArray(r->s2t_rndr.vao);
    glBindBuffer(GL_ARRAY_BUFFER, r->s2t_rndr.vbo);
    glUseProgram(r->s2t_rndr.prog);
    if (r->s2t_rndr.tex_loc >= 0) {
        glUniform1i(r->s2t_rndr.tex_loc, texture_unit);
    }
    if (r->s2t_rndr.surface_size_loc >= 0) {
        glUniform2f(r->s2t_rndr.surface_size_loc, width, height);
    }

    glViewport(0, 0, width, height);
    glColorMask(true, true, true, true);
    glDisable(GL_DITHER);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
#ifndef __ANDROID__ /* glPolygonMode not available in GLES 3.x core */
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
#endif
    glClearColor(0.0f, 0.0f, 1.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glDrawArrays(GL_TRIANGLES, 0, 3);

#ifdef __ANDROID__
    if (android_log_and_drain_gl_errors("render_surface_to: draw")) {
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, gl_target,
                               0, 0);
        glBindFramebuffer(GL_FRAMEBUFFER, r->gl_framebuffer);
        glBindVertexArray(r->gl_vertex_array);
        glBindTexture(gl_target, gl_texture);
        glUseProgram(r->shader_binding ? r->shader_binding->gl_program : 0);
        return false;
    }
#endif

    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, gl_target, 0,
                           0);
    glBindFramebuffer(GL_FRAMEBUFFER, r->gl_framebuffer);
    glBindVertexArray(r->gl_vertex_array);
    glBindTexture(gl_target, gl_texture);
    glUseProgram(
        r->shader_binding ? r->shader_binding->gl_program : 0);
    return true;
}

static void render_surface_to_texture_slow(NV2AState *d,
                                           SurfaceBinding *surface,
                                           TextureBinding *texture,
                                           TextureShape *texture_shape,
                                           int texture_unit)
{
    PGRAPHState *pg = &d->pgraph;

    const ColorFormatInfo *f = &kelvin_color_format_gl_map[texture_shape->color_format];
    assert(texture_shape->color_format < ARRAY_SIZE(kelvin_color_format_gl_map));
    nv2a_profile_inc_counter(NV2A_PROF_SURF_TO_TEX_FALLBACK);

    glActiveTexture(GL_TEXTURE0 + texture_unit);
    glBindTexture(texture->gl_target, texture->gl_texture);

#ifdef __ANDROID__
    if (android_surface_to_texture_rgba8_compatible(surface, texture_shape) &&
        !android_surface_to_texture_needs_guest_reinterpretation(surface,
                                                                 texture_shape) &&
        texture->gl_target == GL_TEXTURE_2D) {
        unsigned int tex_width = texture_shape->width;
        unsigned int tex_height = texture_shape->height;

        pgraph_apply_scaling_factor(pg, &tex_width, &tex_height);

        glTexImage2D(texture->gl_target, 0, GL_RGBA8, tex_width, tex_height,
                     0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        glBindTexture(texture->gl_target, 0);

        if (render_surface_to(d, surface, texture_unit, texture->gl_target,
                              texture->gl_texture, tex_width, tex_height)) {
            glBindTexture(texture->gl_target, texture->gl_texture);
            return;
        }

        glBindTexture(texture->gl_target, texture->gl_texture);
    }
#endif

    unsigned int width = surface->width,
                 height = surface->height;
    pgraph_apply_scaling_factor(pg, &width, &height);

    size_t bufsize = width * height * surface->fmt.bytes_per_pixel;

    uint8_t *buf = g_malloc(bufsize);
    surface_download_to_buffer(d, surface, false, false, false, buf);

    width = texture_shape->width;
    height = texture_shape->height;
    pgraph_apply_scaling_factor(pg, &width, &height);

#ifdef __ANDROID__
    if (android_surface_to_texture_rgba8_compatible(surface, texture_shape)) {
        PGRAPHGLState *r = pg->gl_renderer_state;
        size_t needed = (size_t)width * height * 4;
        if (needed > r->android_s2t_conv_buf_size) {
            r->android_s2t_conv_buf = g_realloc(r->android_s2t_conv_buf, needed);
            r->android_s2t_conv_buf_size = needed;
        }
        if (android_surface_to_texture_needs_guest_reinterpretation(
                surface, texture_shape)) {
            android_surface_guest_to_texture_rgba8(
                texture_shape, buf, width, height,
                width * surface->fmt.bytes_per_pixel, r->android_s2t_conv_buf);
        } else {
            android_surface_guest_to_rgba8(surface, buf, width, height,
                                           width * surface->fmt.bytes_per_pixel,
                                           r->android_s2t_conv_buf);
        }
        glTexImage2D(texture->gl_target, 0, GL_RGBA8, width, height, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, r->android_s2t_conv_buf);
    } else
#endif
    glTexImage2D(texture->gl_target, 0, f->gl_internal_format, width, height, 0,
                 f->gl_format, f->gl_type, buf);
    g_free(buf);
    glBindTexture(texture->gl_target, texture->gl_texture);
}

/* Note: This function is intended to be called before PGRAPH configures GL
 * state for rendering; it will configure GL state here but only restore a
 * couple of items.
 */
void pgraph_gl_render_surface_to_texture(NV2AState *d, SurfaceBinding *surface,
                                      TextureBinding *texture,
                                      TextureShape *texture_shape,
                                      int texture_unit)
{
    PGRAPHState *pg = &d->pgraph;
    PGRAPHGLState *r = pg->gl_renderer_state;

    const ColorFormatInfo *f =
        &kelvin_color_format_gl_map[texture_shape->color_format];
    assert(texture_shape->color_format < ARRAY_SIZE(kelvin_color_format_gl_map));

    nv2a_profile_inc_counter(NV2A_PROF_SURF_TO_TEX);

    if (!surface_to_texture_can_fastpath(surface, texture_shape)) {
        render_surface_to_texture_slow(d, surface, texture,
                                              texture_shape, texture_unit);
        return;
    }

    unsigned int width = texture_shape->width, height = texture_shape->height;
    pgraph_apply_scaling_factor(pg, &width, &height);

    glActiveTexture(GL_TEXTURE0 + texture_unit);
    glBindTexture(texture->gl_target, texture->gl_texture);
    glTexParameteri(texture->gl_target, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(texture->gl_target, GL_TEXTURE_MAX_LEVEL, 0);
    glTexParameteri(texture->gl_target, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
#ifdef __ANDROID__
    if (android_surface_to_texture_rgba8_compatible(surface, texture_shape) &&
        !android_surface_to_texture_needs_guest_reinterpretation(surface,
                                                                 texture_shape)) {
        glTexImage2D(texture->gl_target, 0, GL_RGBA8, width, height, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    } else
#endif
    glTexImage2D(texture->gl_target, 0, f->gl_internal_format, width, height, 0,
                 f->gl_format, f->gl_type, NULL);
    glBindTexture(texture->gl_target, 0);
    if (!render_surface_to(d, surface, texture_unit, texture->gl_target,
                           texture->gl_texture, width, height)) {
        render_surface_to_texture_slow(d, surface, texture,
                                       texture_shape, texture_unit);
        return;
    }
    glBindTexture(texture->gl_target, texture->gl_texture);
    glUseProgram(
        r->shader_binding ? r->shader_binding->gl_program : 0);
}

bool pgraph_gl_check_surface_to_texture_compatibility(
    const SurfaceBinding *surface,
    const TextureShape *shape)
{
    // FIXME: Better checks/handling on formats and surface-texture compat

    if ((!surface->swizzle && surface->pitch != shape->pitch) ||
        surface->width != shape->width ||
        surface->height != shape->height) {
        return false;
    }

    int surface_fmt = surface->shape.color_format;
    int texture_fmt = shape->color_format;

    if (!surface->color) {
        // FIXME: Support zeta to color
        return false;
    }

    if (shape->cubemap) {
        // FIXME: Support rendering surface to cubemap face
        return false;
    }

    if (shape->levels > 1) {
        // FIXME: Support rendering surface to mip levels
        return false;
    }

#ifdef __ANDROID__
    if (android_surface_to_texture_rgba8_compatible(surface, shape)) {
        return true;
    }
#endif

    switch (surface_fmt) {
    case NV097_SET_SURFACE_FORMAT_COLOR_LE_X1R5G5B5_Z1R5G5B5: switch (texture_fmt) {
        case NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_X1R5G5B5: return true;
        default: break;
        }
        break;
    case NV097_SET_SURFACE_FORMAT_COLOR_LE_R5G6B5: switch (texture_fmt) {
        case NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_R5G6B5: return true;
        case NV097_SET_TEXTURE_FORMAT_COLOR_SZ_R5G6B5: return true;
        default: break;
        }
        break;
    case NV097_SET_SURFACE_FORMAT_COLOR_LE_X8R8G8B8_Z8R8G8B8: switch(texture_fmt) {
        case NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_X8R8G8B8: return true;
        case NV097_SET_TEXTURE_FORMAT_COLOR_SZ_X8R8G8B8: return true;
        default: break;
        }
        break;
    case NV097_SET_SURFACE_FORMAT_COLOR_LE_A8R8G8B8: switch (texture_fmt) {
        case NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_A8B8G8R8: return true;
        case NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_R8G8B8A8: return true;
        case NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_A8R8G8B8: return true;
        case NV097_SET_TEXTURE_FORMAT_COLOR_SZ_A8R8G8B8: return true;
        default: break;
        }
        break;
    default:
        break;
    }

    trace_nv2a_pgraph_surface_texture_compat_failed(
        surface_fmt, texture_fmt);
    return false;
}

static bool check_surface_overlaps_range(const SurfaceBinding *surface,
                                         hwaddr range_start, hwaddr range_len)
{
    hwaddr surface_end = surface->vram_addr + surface->size;
    hwaddr range_end = range_start + range_len;
    return !(surface->vram_addr >= range_end || range_start >= surface_end);
}

static void surface_access_callback(void *opaque, MemoryRegion *mr, hwaddr addr,
                                    hwaddr len, bool write)
{
    NV2AState *d = (NV2AState *)opaque;
    qemu_mutex_lock(&d->pgraph.lock);

    PGRAPHGLState *r = d->pgraph.gl_renderer_state;
    bool wait_for_downloads = false;

    SurfaceBinding *surface;
    QTAILQ_FOREACH(surface, &r->surfaces, entry) {
        if (!check_surface_overlaps_range(surface, addr, len)) {
            continue;
        }

        hwaddr offset = addr - surface->vram_addr;

        if (write) {
            trace_nv2a_pgraph_surface_cpu_write(surface->vram_addr, offset);
        } else {
            trace_nv2a_pgraph_surface_cpu_read(surface->vram_addr, offset);
        }

        if (surface->draw_dirty) {
            surface->download_pending = true;
            wait_for_downloads = true;
        }

        if (write) {
            surface->upload_pending = true;
        }
    }

    qemu_mutex_unlock(&d->pgraph.lock);

    if (wait_for_downloads) {
        qemu_mutex_lock(&d->pfifo.lock);
        qemu_event_reset(&r->downloads_complete);
        qatomic_set(&r->downloads_pending, true);
        pfifo_kick(d);
        qemu_mutex_unlock(&d->pfifo.lock);
        qemu_event_wait(&r->downloads_complete);
    }
}

static void register_cpu_access_callback(NV2AState *d, SurfaceBinding *surface)
{
    if (tcg_enabled()) {
        if (surface->width && surface->height) {
            surface->access_cb = mem_access_callback_insert(
                qemu_get_cpu(0), d->vram, surface->vram_addr, surface->size,
                &surface_access_callback, d);
        } else {
            surface->access_cb = NULL;
        }
    }
}

static void unregister_cpu_access_callback(NV2AState *d,
                                           SurfaceBinding const *surface)
{
    if (tcg_enabled()) {
        mem_access_callback_remove_by_ref(qemu_get_cpu(0), surface->access_cb);
    }
}

static bool check_surfaces_overlap(const SurfaceBinding *surface,
                                   const SurfaceBinding *other_surface)
{
    return check_surface_overlaps_range(surface, other_surface->vram_addr,
                                        other_surface->size);
}

static void invalidate_overlapping_surfaces(NV2AState *d, SurfaceBinding *surface)
{
    PGRAPHState *pg = &d->pgraph;
    PGRAPHGLState *r = pg->gl_renderer_state;

    SurfaceBinding *other_surface, *next_surface;
    QTAILQ_FOREACH_SAFE(other_surface, &r->surfaces, entry, next_surface) {
        if (check_surfaces_overlap(surface, other_surface)) {
            trace_nv2a_pgraph_surface_evict_overlapping(
                other_surface->vram_addr, other_surface->width, other_surface->height,
                other_surface->pitch);
            pgraph_gl_surface_download_if_dirty(d, other_surface);
            pgraph_gl_surface_invalidate(d, other_surface);
        }
    }
}

static SurfaceBinding *surface_put(NV2AState *d, hwaddr addr,
                                   SurfaceBinding *surface_in)
{
    PGRAPHState *pg = &d->pgraph;
    PGRAPHGLState *r = pg->gl_renderer_state;

    assert(pgraph_gl_surface_get(d, addr) == NULL);

    invalidate_overlapping_surfaces(d, surface_in);

    SurfaceBinding *surface_out = g_malloc(sizeof(SurfaceBinding));
    assert(surface_out != NULL);
    *surface_out = *surface_in;

    register_cpu_access_callback(d, surface_out);

    QTAILQ_INSERT_TAIL(&r->surfaces, surface_out, entry);

    return surface_out;
}

SurfaceBinding *pgraph_gl_surface_get(NV2AState *d, hwaddr addr)
{
    PGRAPHState *pg = &d->pgraph;
    PGRAPHGLState *r = pg->gl_renderer_state;

    SurfaceBinding *surface;
    QTAILQ_FOREACH (surface, &r->surfaces, entry) {
        if (surface->vram_addr == addr) {
            return surface;
        }
    }

    return NULL;
}

SurfaceBinding *pgraph_gl_surface_get_within(NV2AState *d, hwaddr addr)
{
    PGRAPHState *pg = &d->pgraph;
    PGRAPHGLState *r = pg->gl_renderer_state;

    SurfaceBinding *surface;
    QTAILQ_FOREACH (surface, &r->surfaces, entry) {
        if (addr >= surface->vram_addr &&
            addr < (surface->vram_addr + surface->size)) {
            return surface;
        }
    }

    return NULL;
}

void pgraph_gl_surface_invalidate(NV2AState *d, SurfaceBinding *surface)
{
    PGRAPHState *pg = &d->pgraph;
    PGRAPHGLState *r = pg->gl_renderer_state;

    trace_nv2a_pgraph_surface_invalidated(surface->vram_addr);

    if (surface == r->color_binding) {
        assert(d->pgraph.surface_color.buffer_dirty);
        pgraph_gl_unbind_surface(d, true);
    }
    if (surface == r->zeta_binding) {
        assert(d->pgraph.surface_zeta.buffer_dirty);
        pgraph_gl_unbind_surface(d, false);
    }

    unregister_cpu_access_callback(d, surface);

    glDeleteTextures(1, &surface->gl_buffer);

    QTAILQ_REMOVE(&r->surfaces, surface, entry);
    g_free(surface);
}

static void surface_evict_old(NV2AState *d)
{
    PGRAPHState *pg = &d->pgraph;
    PGRAPHGLState *r = pg->gl_renderer_state;

    const int surface_age_limit = 5;

    SurfaceBinding *s, *next;
    QTAILQ_FOREACH_SAFE(s, &r->surfaces, entry, next) {
        int last_used = d->pgraph.frame_time - s->frame_time;
        if (last_used >= surface_age_limit) {
            trace_nv2a_pgraph_surface_evict_reason("old", s->vram_addr);
            pgraph_gl_surface_download_if_dirty(d, s);
            pgraph_gl_surface_invalidate(d, s);
        }
    }
}

static bool check_surface_compatibility(SurfaceBinding *s1, SurfaceBinding *s2,
                                        bool strict)
{
    bool format_compatible =
        (s1->color == s2->color) &&
        (s1->fmt.gl_attachment == s2->fmt.gl_attachment) &&
        (s1->fmt.gl_internal_format == s2->fmt.gl_internal_format) &&
        (s1->pitch == s2->pitch);
    if (!format_compatible) {
        return false;
    }

    if (!strict) {
        return (s1->width >= s2->width) && (s1->height >= s2->height);
    } else {
        return (s1->width == s2->width) && (s1->height == s2->height);
    }
}

void pgraph_gl_surface_download_if_dirty(NV2AState *d,
                                           SurfaceBinding *surface)
{
    if (surface->draw_dirty) {
        surface_download(d, surface, true);
    }
}

static void bind_current_surface(NV2AState *d)
{
    PGRAPHState *pg = &d->pgraph;
    PGRAPHGLState *r = pg->gl_renderer_state;

    /* Clear any temporary download/render attachments before restoring
     * the active render targets.
     */
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           0, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D,
                           0, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
                           GL_TEXTURE_2D, 0, 0);

    if (r->color_binding) {
        glFramebufferTexture2D(GL_FRAMEBUFFER, r->color_binding->fmt.gl_attachment,
                               GL_TEXTURE_2D, r->color_binding->gl_buffer, 0);
    }

    if (r->zeta_binding) {
        glFramebufferTexture2D(GL_FRAMEBUFFER, r->zeta_binding->fmt.gl_attachment,
                               GL_TEXTURE_2D, r->zeta_binding->gl_buffer, 0);
    }

    if (r->color_binding || r->zeta_binding) {
        GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (status != GL_FRAMEBUFFER_COMPLETE) {
#ifdef __ANDROID__
            const char *status_str = "unknown";
            switch (status) {
            case GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT:
                status_str = "incomplete_attachment";
                break;
            case GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT:
                status_str = "missing_attachment";
                break;
            case GL_FRAMEBUFFER_UNSUPPORTED:
                status_str = "unsupported";
                break;
            default:
                break;
            }
            fprintf(stderr,
                    "nv2a: FBO incomplete (%s=0x%x) color=%d zeta=%d\n",
                    status_str, status,
                    r->color_binding != NULL,
                    r->zeta_binding != NULL);
#endif
            if (r->zeta_binding) {
                glFramebufferTexture2D(GL_FRAMEBUFFER,
                                       r->zeta_binding->fmt.gl_attachment,
                                       GL_TEXTURE_2D, 0, 0);
                status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
                if (status == GL_FRAMEBUFFER_COMPLETE) {
                    return;
                }
            }
            if (r->color_binding) {
                glFramebufferTexture2D(GL_FRAMEBUFFER,
                                       r->color_binding->fmt.gl_attachment,
                                       GL_TEXTURE_2D, 0, 0);
                status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
                if (status == GL_FRAMEBUFFER_COMPLETE) {
                    return;
                }
            }
#ifndef __ANDROID__
            assert(status == GL_FRAMEBUFFER_COMPLETE);
#endif
        }
    }
}

static void surface_copy_shrink_row(uint8_t *out, const uint8_t *in,
                                    unsigned int width,
                                    unsigned int bytes_per_pixel,
                                    unsigned int factor)
{
    if (factor == 1) {
        memcpy(out, in, width * bytes_per_pixel);
        return;
    }

#ifdef __aarch64__
    if (bytes_per_pixel == 4 &&
        android_neon_surface_copy_shrink_row_4bpp(out, in, width, factor)) {
        return;
    }
    if (bytes_per_pixel == 2 &&
        android_neon_surface_copy_shrink_row_2bpp(out, in, width, factor)) {
        return;
    }
#endif

    if (bytes_per_pixel == 4) {
        for (unsigned int x = 0; x < width; x++) {
            *(uint32_t *)out = *(const uint32_t *)in;
            out += 4;
            in += 4 * factor;
        }
    } else if (bytes_per_pixel == 2) {
        for (unsigned int x = 0; x < width; x++) {
            *(uint16_t *)out = *(const uint16_t *)in;
            out += 2;
            in += 2 * factor;
        }
    } else {
        for (unsigned int x = 0; x < width; x++) {
            memcpy(out, in, bytes_per_pixel);
            out += bytes_per_pixel;
            in += bytes_per_pixel * factor;
        }
    }
}

#ifdef __ANDROID__
static bool android_surface_download_depth16_to_guest(NV2AState *d,
                                                      SurfaceBinding *surface,
                                                      bool swizzle, bool flip,
                                                      bool downscale,
                                                      uint8_t *pixels)
{
    PGRAPHState *pg = &d->pgraph;
    PGRAPHGLState *r = pg->gl_renderer_state;
    const float factor = downscale ? pg->surface_scale_factor : 1.0f;
    const unsigned int read_width = (unsigned int)(surface->width * factor);
    const unsigned int read_height = (unsigned int)(surface->height * factor);
    const unsigned int rgba_stride = read_width * 4;
    GLuint pack_texture = 0;
    uint8_t *rgba_pixels = NULL;
    uint8_t *rgba_linear = NULL;
    uint8_t *linear_guest = pixels;
    uint8_t *swizzle_buf = NULL;
    bool ok = false;

    if (surface->color || surface->fmt.gl_attachment != GL_DEPTH_ATTACHMENT ||
        surface->fmt.gl_format != GL_DEPTH_COMPONENT ||
        surface->fmt.gl_type != GL_UNSIGNED_SHORT ||
        r->s2t_rndr.depth_prog == 0) {
        return false;
    }

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, surface->gl_buffer);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_NONE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    glGenTextures(1, &pack_texture);
    glBindTexture(GL_TEXTURE_2D, pack_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, read_width, read_height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, NULL);

    glBindFramebuffer(GL_FRAMEBUFFER, r->s2t_rndr.fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           pack_texture, 0);
    {
        GLenum draw_buffers[1] = { GL_COLOR_ATTACHMENT0 };
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            goto cleanup;
        }
        glDrawBuffers(1, draw_buffers);
        glReadBuffer(GL_COLOR_ATTACHMENT0);
    }

    if (android_log_surface_download_errors(
            "surface_download_depth16: fbo setup", surface)) {
        goto cleanup;
    }

    glBindVertexArray(r->s2t_rndr.vao);
    glBindBuffer(GL_ARRAY_BUFFER, r->s2t_rndr.vbo);
    glUseProgram(r->s2t_rndr.depth_prog);
    if ((GLint)r->s2t_rndr.depth_tex_loc >= 0) {
        glUniform1i(r->s2t_rndr.depth_tex_loc, 0);
    }
    if (r->s2t_rndr.depth_scale_loc >= 0) {
        glUniform1f(r->s2t_rndr.depth_scale_loc, 65535.0f);
    }
    glViewport(0, 0, read_width, read_height);
    glColorMask(true, true, true, true);
    glDisable(GL_DITHER);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    if (android_log_surface_download_errors(
            "surface_download_depth16: pack draw", surface)) {
        goto cleanup;
    }

    rgba_pixels = g_malloc(read_height * rgba_stride);
    android_glo_readpixels(r, GL_RGBA, GL_UNSIGNED_BYTE, 4, rgba_stride,
                           read_width, read_height, flip, rgba_pixels);

    if (android_log_surface_download_errors(
            "surface_download_depth16: pack read", surface)) {
        goto cleanup;
    }

    rgba_linear = rgba_pixels;
    if (downscale) {
        rgba_linear = g_malloc(surface->width * surface->height * 4);
        for (unsigned int y = 0; y < surface->height; y++) {
            surface_copy_shrink_row(rgba_linear + y * surface->width * 4,
                                    rgba_pixels + (unsigned int)(y * rgba_stride * factor),
                                    surface->width, 4, (unsigned int)factor);
        }
    }

    if (swizzle) {
        swizzle_buf = g_malloc(surface->size);
        linear_guest = swizzle_buf;
    }

    for (unsigned int y = 0; y < surface->height; y++) {
        const uint8_t *src_row = rgba_linear + y * surface->width * 4;
        uint8_t *dst_row = linear_guest + y * surface->pitch;

#ifdef __aarch64__
        android_neon_pack_depth16_row_to_guest(src_row, dst_row, surface->width);
#else
        for (unsigned int x = 0; x < surface->width; x++) {
            const uint8_t *src = src_row + x * 4;
            stw_le_p(dst_row + x * 2, src[0] | (src[1] << 8));
        }
#endif
    }

    if (swizzle) {
        swizzle_rect(swizzle_buf, surface->width, surface->height, pixels,
                     surface->pitch, surface->fmt.bytes_per_pixel);
    }

    ok = true;

cleanup:
    if (swizzle_buf) {
        g_free(swizzle_buf);
    }
    if (rgba_linear && rgba_linear != rgba_pixels) {
        g_free(rgba_linear);
    }
    if (rgba_pixels) {
        g_free(rgba_pixels);
    }

    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           0, 0);
    if (pack_texture) {
        glDeleteTextures(1, &pack_texture);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, r->gl_framebuffer);
    glBindVertexArray(r->gl_vertex_array);
    glUseProgram(r->shader_binding ? r->shader_binding->gl_program : 0);

    return ok;
}

static bool android_surface_download_z24s8_to_guest(NV2AState *d,
                                                    SurfaceBinding *surface,
                                                    bool swizzle, bool flip,
                                                    bool downscale,
                                                    uint8_t *pixels)
{
    PGRAPHState *pg = &d->pgraph;
    PGRAPHGLState *r = pg->gl_renderer_state;
    const float scale = pg->surface_scale_factor;
    const unsigned int read_width = (unsigned int)(surface->width * scale);
    const unsigned int read_height = (unsigned int)(surface->height * scale);
    const unsigned int output_width = downscale ? surface->width : read_width;
    const unsigned int output_height = downscale ? surface->height : read_height;
    const unsigned int output_pitch = downscale ? surface->pitch
                                                : (unsigned int)(surface->pitch * scale);
    GLuint pack_texture = 0;
    uint8_t *depth_pixels = NULL;
    uint8_t *stencil_pixels = NULL;
    uint8_t *linear_guest = pixels;
    uint8_t *swizzle_buf = NULL;
    GLint prev_read_buffer = GL_NONE;
    bool ok = false;
    static bool warned_stencil_readback_unsupported = false;

    if (surface->color ||
        surface->fmt.gl_attachment != GL_DEPTH_STENCIL_ATTACHMENT ||
        surface->fmt.gl_format != GL_DEPTH_STENCIL ||
        surface->fmt.gl_type != GL_UNSIGNED_INT_24_8) {
        return false;
    }

    glGetIntegerv(GL_READ_BUFFER, &prev_read_buffer);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, surface->gl_buffer);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_NONE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    glGenTextures(1, &pack_texture);
    glBindTexture(GL_TEXTURE_2D, pack_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, read_width, read_height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, NULL);

    glBindFramebuffer(GL_FRAMEBUFFER, r->s2t_rndr.fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           pack_texture, 0);
    {
        GLenum draw_buffers[1] = { GL_COLOR_ATTACHMENT0 };
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            goto cleanup;
        }
        glDrawBuffers(1, draw_buffers);
        glReadBuffer(GL_COLOR_ATTACHMENT0);
    }

    if (android_log_surface_download_errors(
            "surface_download_z24s8: pack_fbo_setup", surface)) {
        goto cleanup;
    }

    glBindVertexArray(r->s2t_rndr.vao);
    glBindBuffer(GL_ARRAY_BUFFER, r->s2t_rndr.vbo);
    glUseProgram(r->s2t_rndr.depth_prog);
    if ((GLint)r->s2t_rndr.depth_tex_loc >= 0) {
        glUniform1i(r->s2t_rndr.depth_tex_loc, 0);
    }
    if (r->s2t_rndr.depth_scale_loc >= 0) {
        glUniform1f(r->s2t_rndr.depth_scale_loc, 16777215.0f);
    }
    glViewport(0, 0, read_width, read_height);
    glColorMask(true, true, true, true);
    glDisable(GL_DITHER);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    if (android_log_surface_download_errors(
            "surface_download_z24s8: pack_draw", surface)) {
        goto cleanup;
    }

    depth_pixels = g_malloc(read_width * read_height * 4);
    android_glo_readpixels(r, GL_RGBA, GL_UNSIGNED_BYTE, 4,
                           read_width * 4, read_width, read_height, flip,
                           depth_pixels);
    if (android_log_surface_download_errors(
            "surface_download_z24s8: post-read-depth-pack", surface)) {
        goto cleanup;
    }

    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           0, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, r->gl_framebuffer);
    glBindVertexArray(r->gl_vertex_array);
    glUseProgram(r->shader_binding ? r->shader_binding->gl_program : 0);

    glReadBuffer(GL_NONE);
    if (android_log_surface_download_errors(
            "surface_download_z24s8: pre-read-stencil", surface)) {
        goto cleanup;
    }

    stencil_pixels = g_malloc0(read_width * read_height);
    android_glo_readpixels(r, GL_STENCIL_INDEX, GL_UNSIGNED_BYTE, 1,
                           read_width, read_width, read_height, flip,
                           stencil_pixels);
    if (android_drain_gl_errors_silent()) {
        if (!warned_stencil_readback_unsupported) {
            warned_stencil_readback_unsupported = true;
            __android_log_print(
                ANDROID_LOG_WARN, "hakuX",
                "surface_download_z24s8: stencil readback unsupported on this "
                "GLES driver, substituting zero stencil");
        }
    }

    if (swizzle) {
        swizzle_buf = g_malloc(output_pitch * output_height);
        linear_guest = swizzle_buf;
    }

    for (unsigned int y = 0; y < output_height; y++) {
        uint8_t *dst_row = linear_guest + y * output_pitch;
        const unsigned int src_y = downscale ? y * scale : y;

#ifdef __aarch64__
        if (!downscale) {
            android_neon_pack_z24s8_row_to_guest(
                depth_pixels + src_y * read_width * 4,
                stencil_pixels + src_y * read_width,
                dst_row, output_width);
            continue;
        }
#endif

        for (unsigned int x = 0; x < output_width; x++) {
            const unsigned int src_x = downscale ? x * scale : x;
            const unsigned int src_idx = src_y * read_width + src_x;
            const uint8_t *src = depth_pixels + src_idx * 4;
            uint32_t depth24 = src[0] | (src[1] << 8) | (src[2] << 16);

            stl_le_p((uint32_t *)(dst_row + x * 4),
                     (depth24 << 8) | stencil_pixels[src_idx]);
        }
    }

    if (swizzle) {
        swizzle_rect(swizzle_buf, output_width, output_height, pixels,
                     output_pitch, surface->fmt.bytes_per_pixel);
    }

    ok = true;

cleanup:
    if (swizzle_buf) {
        g_free(swizzle_buf);
    }
    if (stencil_pixels) {
        g_free(stencil_pixels);
    }
    if (depth_pixels) {
        g_free(depth_pixels);
    }
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           0, 0);
    if (pack_texture) {
        glDeleteTextures(1, &pack_texture);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, r->gl_framebuffer);
    glBindVertexArray(r->gl_vertex_array);
    glUseProgram(r->shader_binding ? r->shader_binding->gl_program : 0);

    glReadBuffer((GLenum)prev_read_buffer);
    android_log_surface_download_errors(
        "surface_download_z24s8: cleanup_restore_read_buffer", surface);

    return ok;
}
#endif

static void surface_download_to_buffer(NV2AState *d, SurfaceBinding *surface,
                                       bool swizzle, bool flip, bool downscale,
                                       uint8_t *pixels)
{
    PGRAPHState *pg = &d->pgraph;
    bool ok = true;
#ifdef __ANDROID__
    GLint prev_read_buffer = GL_COLOR_ATTACHMENT0;
    bool restore_read_buffer = false;
#endif

    swizzle &= surface->swizzle;
    downscale &= (pg->surface_scale_factor != 1);

    if (!surface->width || !surface->height) {
        return;
    }

    trace_nv2a_pgraph_surface_download(
        surface->color ? "COLOR" : "ZETA",
        surface->swizzle ? "sz" : "lin", surface->vram_addr,
        surface->width, surface->height, surface->pitch,
        surface->fmt.bytes_per_pixel);

    /*  Bind destination surface to framebuffer */
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           0, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D,
                           0, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
                           GL_TEXTURE_2D, 0, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, surface->fmt.gl_attachment,
                           GL_TEXTURE_2D, surface->gl_buffer, 0);

    {
        GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (status != GL_FRAMEBUFFER_COMPLETE) {
#ifdef __ANDROID__
            const char *status_str = "unknown";
            switch (status) {
            case GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT:
                status_str = "incomplete_attachment";
                break;
            case GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT:
                status_str = "missing_attachment";
                break;
            case GL_FRAMEBUFFER_UNSUPPORTED:
                status_str = "unsupported";
                break;
            default:
                break;
            }
            fprintf(stderr, "nv2a: download FBO incomplete (%s=0x%x)\n",
                    status_str, status);
#endif
            ok = false;
        }
    }

    if (!ok) {
        if (pixels && surface->size) {
            memset(pixels, 0, surface->size);
        }
        goto cleanup;
    }

#ifdef __ANDROID__
    if (android_surface_download_depth16_to_guest(d, surface, swizzle, flip,
                                                  downscale, pixels)) {
        goto cleanup;
    }
    if (android_surface_download_z24s8_to_guest(d, surface, swizzle, flip,
                                                downscale, pixels)) {
        goto cleanup;
    }
#endif

    /* Read surface into memory */
#ifdef __ANDROID__
    glGetIntegerv(GL_READ_BUFFER, &prev_read_buffer);
    glReadBuffer(surface->color ? surface->fmt.gl_attachment : GL_NONE);
    restore_read_buffer = true;
    android_log_surface_download_errors("surface_download_to_buffer: pre-read",
                                        surface);
    if (android_surface_uses_rgba8_transfer(surface)) {
        const float factor = downscale ? pg->surface_scale_factor : 1.0f;
        const unsigned int read_width = (unsigned int)(surface->width * factor);
        const unsigned int read_height = (unsigned int)(surface->height * factor);
        const unsigned int rgba_stride = read_width * 4;
        uint8_t *rgba_pixels = g_malloc(read_height * rgba_stride);
        uint8_t *rgba_linear = rgba_pixels;
        uint8_t *linear_guest = pixels;

        android_glo_readpixels(pg->gl_renderer_state, GL_RGBA,
                               GL_UNSIGNED_BYTE, 4, rgba_stride,
                               read_width, read_height, flip, rgba_pixels);
        android_log_surface_download_errors(
            "surface_download_to_buffer: post-read_rgba8", surface);

        if (downscale) {
            rgba_linear = g_malloc(surface->width * surface->height * 4);
            for (unsigned int y = 0; y < surface->height; y++) {
                surface_copy_shrink_row(rgba_linear + y * surface->width * 4,
                                        rgba_pixels + (unsigned int)(y * rgba_stride * factor),
                                        surface->width, 4, (unsigned int)factor);
            }
        }

        if (swizzle) {
            linear_guest = g_malloc(surface->size);
        }

        android_surface_rgba8_to_guest(surface, rgba_linear, surface->width * 4,
                                       surface->width, surface->height,
                                       linear_guest, surface->pitch);

        if (swizzle) {
            swizzle_rect(linear_guest, surface->width, surface->height, pixels,
                         surface->pitch, surface->fmt.bytes_per_pixel);
            g_free(linear_guest);
        }

        if (rgba_linear != rgba_pixels) {
            g_free(rgba_linear);
        }
        g_free(rgba_pixels);
        goto cleanup;
    }
#endif

    uint8_t *gl_read_buf = pixels;

    uint8_t *swizzle_buf = pixels;
    if (swizzle) {
        /* FIXME: Allocate big buffer up front and re-alloc if necessary.
         * FIXME: Consider swizzle in shader
         */
        assert(pg->surface_scale_factor == 1 || downscale);
        swizzle_buf = (uint8_t *)g_malloc(surface->size);
        gl_read_buf = swizzle_buf;
    }

    if (downscale) {
        pg->scale_buf = (uint8_t *)g_realloc(
            pg->scale_buf, pg->surface_scale_factor * pg->surface_scale_factor *
                               surface->size);
        gl_read_buf = pg->scale_buf;
    }

#ifdef __ANDROID__
    android_glo_readpixels(pg->gl_renderer_state,
#else
    glo_readpixels(
#endif
        surface->fmt.gl_format, surface->fmt.gl_type, surface->fmt.bytes_per_pixel,
        pg->surface_scale_factor * surface->pitch,
        pg->surface_scale_factor * surface->width,
        pg->surface_scale_factor * surface->height, flip, gl_read_buf);
    android_log_surface_download_errors("surface_download_to_buffer: post-read",
                                        surface);

    /* FIXME: Replace this with a hw accelerated version */
    if (downscale) {
        assert(surface->pitch >= (surface->width * surface->fmt.bytes_per_pixel));
        uint8_t *out = swizzle_buf, *in = pg->scale_buf;
        for (unsigned int y = 0; y < surface->height; y++) {
            surface_copy_shrink_row(out, in, surface->width,
                                    surface->fmt.bytes_per_pixel,
                                    (unsigned int)pg->surface_scale_factor);
            in += (unsigned int)(surface->pitch * pg->surface_scale_factor *
                  pg->surface_scale_factor);
            out += surface->pitch;
        }
    }

    if (swizzle) {
        swizzle_rect(swizzle_buf, surface->width, surface->height, pixels,
                     surface->pitch, surface->fmt.bytes_per_pixel);
        g_free(swizzle_buf);
    }

    /* Re-bind original framebuffer target */
cleanup:
    bind_current_surface(d);
#ifdef __ANDROID__
    android_log_surface_download_errors(
        "surface_download_to_buffer: cleanup_rebind_surface", surface);
#endif
#ifdef __ANDROID__
    if (restore_read_buffer) {
        GLenum read_buffer = (GLenum)prev_read_buffer;
        if (read_buffer != GL_NONE &&
            !d->pgraph.gl_renderer_state->color_binding) {
            read_buffer = GL_NONE;
        }
        glReadBuffer(read_buffer);
        android_log_surface_download_errors(
            "surface_download_to_buffer: cleanup_restore_read_buffer",
            surface);
    }
#endif
}

static void surface_download(NV2AState *d, SurfaceBinding *surface, bool force)
{
    if (!(surface->download_pending || force) || !surface->width ||
        !surface->height) {
        return;
    }

    /* FIXME: Respect write enable at last TOU? */

    nv2a_profile_inc_counter(NV2A_PROF_SURF_DOWNLOAD);

    surface_download_to_buffer(d, surface, true, false, true,
                               d->vram_ptr + surface->vram_addr);

    memory_region_set_client_dirty(d->vram, surface->vram_addr,
                                   surface->pitch * surface->height,
                                   DIRTY_MEMORY_VGA);
    memory_region_set_client_dirty(d->vram, surface->vram_addr,
                                   surface->pitch * surface->height,
                                   DIRTY_MEMORY_NV2A_TEX);

    surface->download_pending = false;
    surface->draw_dirty = false;
}

void pgraph_gl_process_pending_downloads(NV2AState *d)
{
    PGRAPHState *pg = &d->pgraph;
    PGRAPHGLState *r = pg->gl_renderer_state;

    SurfaceBinding *surface;
    QTAILQ_FOREACH(surface, &r->surfaces, entry) {
        surface_download(d, surface, false);
    }

    qatomic_set(&r->downloads_pending, false);
    qemu_event_set(&r->downloads_complete);
}

void pgraph_gl_download_dirty_surfaces(NV2AState *d)
{
    PGRAPHState *pg = &d->pgraph;
    PGRAPHGLState *r = pg->gl_renderer_state;

    SurfaceBinding *surface;
    QTAILQ_FOREACH(surface, &r->surfaces, entry) {
        pgraph_gl_surface_download_if_dirty(d, surface);
    }

    qatomic_set(&r->download_dirty_surfaces_pending, false);
    qemu_event_set(&r->dirty_surfaces_download_complete);
}

static void surface_copy_expand_row(uint8_t *out, uint8_t *in,
                                    unsigned int width,
                                    unsigned int bytes_per_pixel,
                                    unsigned int factor)
{
    if (bytes_per_pixel == 4) {
        for (unsigned int x = 0; x < width; x++) {
            for (unsigned int i = 0; i < factor; i++) {
                *(uint32_t *)out = *(uint32_t *)in;
                out += bytes_per_pixel;
            }
            in += bytes_per_pixel;
        }
    } else if (bytes_per_pixel == 2) {
        for (unsigned int x = 0; x < width; x++) {
            for (unsigned int i = 0; i < factor; i++) {
                *(uint16_t *)out = *(uint16_t *)in;
                out += bytes_per_pixel;
            }
            in += bytes_per_pixel;
        }
    } else {
        for (unsigned int x = 0; x < width; x++) {
            for (unsigned int i = 0; i < factor; i++) {
                memcpy(out, in, bytes_per_pixel);
                out += bytes_per_pixel;
            }
            in += bytes_per_pixel;
        }
    }
}

static void surface_copy_expand(uint8_t *out, uint8_t *in, unsigned int width,
                                unsigned int height,
                                unsigned int bytes_per_pixel,
                                unsigned int factor)
{
    size_t out_pitch = width * bytes_per_pixel * factor;

    for (unsigned int y = 0; y < height; y++) {
        surface_copy_expand_row(out, in, width, bytes_per_pixel, factor);
        uint8_t *row_in = out;
        for (unsigned int i = 1; i < factor; i++) {
            out += out_pitch;
            memcpy(out, row_in, out_pitch);
        }
        in += width * bytes_per_pixel;
        out += out_pitch;
    }
}

void pgraph_gl_upload_surface_data(NV2AState *d, SurfaceBinding *surface,
                                bool force)
{
    if (!(surface->upload_pending || force)) {
        return;
    }

    nv2a_profile_inc_counter(NV2A_PROF_SURF_UPLOAD);

    trace_nv2a_pgraph_surface_upload(
                 surface->color ? "COLOR" : "ZETA",
                 surface->swizzle ? "sz" : "lin", surface->vram_addr,
                 surface->width, surface->height, surface->pitch,
                 surface->fmt.bytes_per_pixel);

    PGRAPHState *pg = &d->pgraph;

    surface->upload_pending = false;
    surface->draw_time = pg->draw_time;

    if (!surface->width || !surface->height) {
        return;
    }

    // FIXME: Don't query GL for texture binding
    GLint last_texture_binding;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &last_texture_binding);

    // FIXME: Replace with FBO to not disturb current state
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           0, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D,
                           0, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
                           GL_TEXTURE_2D, 0, 0);

    uint8_t *data = d->vram_ptr;
    uint8_t *buf = data + surface->vram_addr;

    if (surface->swizzle) {
        buf = (uint8_t*)g_malloc(surface->size);
        unswizzle_rect(data + surface->vram_addr,
                       surface->width, surface->height,
                       buf,
                       surface->pitch,
                       surface->fmt.bytes_per_pixel);
    }

    /* FIXME: Replace this scaling */

    // This is VRAM so we can't do this inplace!
    uint8_t *optimal_buf = buf;
    unsigned int optimal_pitch = surface->width * surface->fmt.bytes_per_pixel;

    if (surface->pitch != optimal_pitch) {
        optimal_buf = (uint8_t *)g_malloc(surface->height * optimal_pitch);

        uint8_t *src = buf;
        uint8_t *dst = optimal_buf;
        unsigned int irow;
        for (irow = 0; irow < surface->height; irow++) {
            memcpy(dst, src, optimal_pitch);
            src += surface->pitch;
            dst += optimal_pitch;
        }
    }

    uint8_t *gl_read_buf = optimal_buf;
    unsigned int width = surface->width, height = surface->height;

    if (pg->surface_scale_factor > 1) {
        pgraph_apply_scaling_factor(pg, &width, &height);
        pg->scale_buf = (uint8_t *)g_realloc(
            pg->scale_buf, width * height * surface->fmt.bytes_per_pixel);
        gl_read_buf = pg->scale_buf;
        uint8_t *out = gl_read_buf, *in = optimal_buf;
        surface_copy_expand(out, in, surface->width, surface->height,
                            surface->fmt.bytes_per_pixel,
                            d->pgraph.surface_scale_factor);
    }

    int prev_unpack_alignment;
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &prev_unpack_alignment);
    if (unlikely((width * surface->fmt.bytes_per_pixel) % 4 != 0)) {
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    } else {
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    }

    glBindTexture(GL_TEXTURE_2D, surface->gl_buffer);
#ifdef __ANDROID__
    {
        PGRAPHGLState *r = pg->gl_renderer_state;
        const uint8_t *upload_buf = gl_read_buf;
        GLint upload_ifmt;
        GLenum upload_fmt;
        GLenum upload_type;

        android_surface_get_storage_format(surface, &upload_ifmt, &upload_fmt,
                                           &upload_type);
        if (android_surface_uses_rgba8_transfer(surface)) {
            size_t needed = (size_t)width * height * 4;
            if (needed > r->android_conv_buf_size) {
                r->android_conv_buf = g_realloc(r->android_conv_buf, needed);
                r->android_conv_buf_size = needed;
            }
            android_surface_guest_to_rgba8(surface, gl_read_buf, width, height,
                                           width * surface->fmt.bytes_per_pixel,
                                           r->android_conv_buf);
            upload_buf = r->android_conv_buf;
        }
        glTexImage2D(GL_TEXTURE_2D, 0, upload_ifmt, width, height, 0,
                     upload_fmt, upload_type, upload_buf);
    }
#else
    glTexImage2D(GL_TEXTURE_2D, 0, surface->fmt.gl_internal_format, width,
                 height, 0, surface->fmt.gl_format, surface->fmt.gl_type,
                 gl_read_buf);
#endif
    glPixelStorei(GL_UNPACK_ALIGNMENT, prev_unpack_alignment);
    if (optimal_buf != buf) {
        g_free(optimal_buf);
    }
    if (surface->swizzle) {
        g_free(buf);
    }

    // Rebind previous framebuffer binding
    glBindTexture(GL_TEXTURE_2D, last_texture_binding);

    bind_current_surface(d);
}

static void compare_surfaces(SurfaceBinding *s1, SurfaceBinding *s2)
{
    #define DO_CMP(fld) \
        if (s1->fld != s2->fld) \
            trace_nv2a_pgraph_surface_compare_mismatch( \
                #fld, (long int)s1->fld, (long int)s2->fld);
    DO_CMP(shape.clip_x)
    DO_CMP(shape.clip_width)
    DO_CMP(shape.clip_y)
    DO_CMP(shape.clip_height)
    DO_CMP(gl_buffer)
    DO_CMP(fmt.bytes_per_pixel)
    DO_CMP(fmt.gl_attachment)
    DO_CMP(fmt.gl_internal_format)
    DO_CMP(fmt.gl_format)
    DO_CMP(fmt.gl_type)
    DO_CMP(color)
    DO_CMP(swizzle)
    DO_CMP(vram_addr)
    DO_CMP(width)
    DO_CMP(height)
    DO_CMP(pitch)
    DO_CMP(size)
    DO_CMP(dma_addr)
    DO_CMP(dma_len)
    DO_CMP(frame_time)
    DO_CMP(draw_time)
    #undef DO_CMP
}

static void populate_surface_binding_entry_sized(NV2AState *d, bool color,
                                                 unsigned int width,
                                                 unsigned int height,
                                                 SurfaceBinding *entry)
{
    PGRAPHState *pg = &d->pgraph;
    PGRAPHGLState *r = pg->gl_renderer_state;

    Surface *surface;
    hwaddr dma_address;
    SurfaceFormatInfo fmt;

    if (color) {
        surface = &pg->surface_color;
        dma_address = pg->dma_color;
        assert(pg->surface_shape.color_format != 0);
        assert(pg->surface_shape.color_format <
               ARRAY_SIZE(kelvin_surface_color_format_gl_map));
        fmt = kelvin_surface_color_format_gl_map[pg->surface_shape.color_format];
        if (fmt.bytes_per_pixel == 0) {
            fprintf(stderr, "nv2a: unimplemented color surface format 0x%x\n",
                    pg->surface_shape.color_format);
            abort();
        }
    } else {
        surface = &pg->surface_zeta;
        dma_address = pg->dma_zeta;
        assert(pg->surface_shape.zeta_format != 0);
        assert(pg->surface_shape.zeta_format <
               ARRAY_SIZE(kelvin_surface_zeta_float_format_gl_map));
        const SurfaceFormatInfo *map =
            pg->surface_shape.z_format ? kelvin_surface_zeta_float_format_gl_map :
                                         kelvin_surface_zeta_fixed_format_gl_map;
        fmt = map[pg->surface_shape.zeta_format];
    }

    DMAObject dma = nv_dma_load(d, dma_address);
    /* There's a bunch of bugs that could cause us to hit this function
     * at the wrong time and get a invalid dma object.
     * Check that it's sane. */
    assert(dma.dma_class == NV_DMA_IN_MEMORY_CLASS);
    // assert(dma.address + surface->offset != 0);
    assert(surface->offset <= dma.limit);
    assert(surface->offset + surface->pitch * height <= dma.limit + 1);
    assert(surface->pitch % fmt.bytes_per_pixel == 0);
    assert((dma.address & ~0x07FFFFFF) == 0);

    entry->shape = (color || !r->color_binding) ? pg->surface_shape :
                                                    r->color_binding->shape;
    entry->gl_buffer = 0;
    entry->fmt = fmt;
#ifdef __ANDROID__
    android_sanitize_surface_format(r, &entry->fmt);
#endif
    entry->color = color;
    entry->swizzle =
        (pg->surface_type == NV097_SET_SURFACE_FORMAT_TYPE_SWIZZLE);
    entry->vram_addr = dma.address + surface->offset;
    entry->width = width;
    entry->height = height;
    entry->pitch = surface->pitch;
    entry->size = height * MAX(surface->pitch, width * fmt.bytes_per_pixel);
    entry->upload_pending = true;
    entry->download_pending = false;
    entry->draw_dirty = false;
    entry->dma_addr = dma.address;
    entry->dma_len = dma.limit;
    entry->frame_time = pg->frame_time;
    entry->draw_time = pg->draw_time;
    entry->cleared = false;
}

static void populate_surface_binding_entry(NV2AState *d, bool color,
                                                  SurfaceBinding *entry)
{
    PGRAPHState *pg = &d->pgraph;
    PGRAPHGLState *r = pg->gl_renderer_state;

    unsigned int width, height;

    if (color || !r->color_binding) {
        surface_get_dimensions(pg, &width, &height);
        pgraph_apply_anti_aliasing_factor(pg, &width, &height);

        /* Since we determine surface dimensions based on the clipping
         * rectangle, make sure to include the surface offset as well.
         */
        if (pg->surface_type != NV097_SET_SURFACE_FORMAT_TYPE_SWIZZLE) {
            width += pg->surface_shape.clip_x;
            height += pg->surface_shape.clip_y;
        }
    } else {
        width = r->color_binding->width;
        height = r->color_binding->height;
    }

    populate_surface_binding_entry_sized(d, color, width, height, entry);
}

static void update_surface_part(NV2AState *d, bool upload, bool color)
{
    PGRAPHState *pg = &d->pgraph;
    PGRAPHGLState *r = pg->gl_renderer_state;

    SurfaceBinding entry;
    populate_surface_binding_entry(d, color, &entry);

    Surface *surface = color ? &pg->surface_color : &pg->surface_zeta;

    bool mem_dirty = !tcg_enabled() && memory_region_test_and_clear_dirty(
                                           d->vram, entry.vram_addr, entry.size,
                                           DIRTY_MEMORY_NV2A);

    if (upload && (surface->buffer_dirty || mem_dirty)) {
        pgraph_gl_unbind_surface(d, color);

        SurfaceBinding *found = pgraph_gl_surface_get(d, entry.vram_addr);
        if (found != NULL) {
            /* FIXME: Support same color/zeta surface target? In the mean time,
             * if the surface we just found is currently bound, just unbind it.
             */
            SurfaceBinding *other = (color ? r->zeta_binding
                                           : r->color_binding);
            if (found == other) {
                NV2A_UNIMPLEMENTED("Same color & zeta surface offset");
                pgraph_gl_unbind_surface(d, !color);
            }
        }

        trace_nv2a_pgraph_surface_target(
            color ? "COLOR" : "ZETA", entry.vram_addr,
            entry.swizzle ? "sz" : "ln",
            pg->surface_shape.anti_aliasing,
            pg->surface_shape.clip_x,
            pg->surface_shape.clip_width, pg->surface_shape.clip_y,
            pg->surface_shape.clip_height);

        bool should_create = true;

        if (found != NULL) {
            bool is_compatible =
                check_surface_compatibility(found, &entry, false);

#define TRACE_ARGS found->vram_addr, found->width, found->height, \
            found->swizzle ? "sz" : "ln", \
            found->shape.anti_aliasing, found->shape.clip_x, \
            found->shape.clip_width, found->shape.clip_y, \
            found->shape.clip_height, found->pitch
            if (found->color) {
                trace_nv2a_pgraph_surface_match_color(TRACE_ARGS);
            } else {
                trace_nv2a_pgraph_surface_match_zeta(TRACE_ARGS);
            }
#undef TRACE_ARGS

            assert(!(entry.swizzle && pg->clearing));

            if (found->swizzle != entry.swizzle) {
                /* Clears should only be done on linear surfaces. Avoid
                 * synchronization by allowing (1) a surface marked swizzled to
                 * be cleared under the assumption the entire surface is
                 * destined to be cleared and (2) a fully cleared linear surface
                 * to be marked swizzled. Strictly match size to avoid
                 * pathological cases.
                 */
                is_compatible &= (pg->clearing || found->cleared) &&
                    check_surface_compatibility(found, &entry, true);
                if (is_compatible) {
                    trace_nv2a_pgraph_surface_migrate_type(
                        entry.swizzle ? "swizzled" : "linear");
                }
            }

            if (is_compatible && color &&
                !check_surface_compatibility(found, &entry, true)) {
                SurfaceBinding zeta_entry;
                populate_surface_binding_entry_sized(
                    d, !color, found->width, found->height, &zeta_entry);
                hwaddr color_end = found->vram_addr + found->size;
                hwaddr zeta_end = zeta_entry.vram_addr + zeta_entry.size;
                is_compatible &= found->vram_addr >= zeta_end ||
                                 zeta_entry.vram_addr >= color_end;
            }

            if (is_compatible && !color && r->color_binding) {
                is_compatible &= (found->width == r->color_binding->width) &&
                                 (found->height == r->color_binding->height);
            }

            if (is_compatible) {
                /* FIXME: Refactor */
                pg->surface_binding_dim.width = found->width;
                pg->surface_binding_dim.clip_x = found->shape.clip_x;
                pg->surface_binding_dim.clip_width = found->shape.clip_width;
                pg->surface_binding_dim.height = found->height;
                pg->surface_binding_dim.clip_y = found->shape.clip_y;
                pg->surface_binding_dim.clip_height = found->shape.clip_height;
                found->upload_pending |= mem_dirty;
                pg->surface_zeta.buffer_dirty |= color;
                should_create = false;
            } else {
                trace_nv2a_pgraph_surface_evict_reason(
                    "incompatible", found->vram_addr);
                compare_surfaces(found, &entry);
                pgraph_gl_surface_download_if_dirty(d, found);
                pgraph_gl_surface_invalidate(d, found);
            }
        }

        if (should_create) {
            glGenTextures(1, &entry.gl_buffer);
            glBindTexture(GL_TEXTURE_2D, entry.gl_buffer);
            NV2A_GL_DLABEL(GL_TEXTURE, entry.gl_buffer,
                           "%s format: %0X, width: %d, height: %d "
                           "(addr %" HWADDR_PRIx ")",
                           color ? "color" : "zeta",
                           color ? pg->surface_shape.color_format
                                 : pg->surface_shape.zeta_format,
                           entry.width, entry.height, surface->offset);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            unsigned int width = entry.width ? entry.width : 1;
            unsigned int height = entry.height ? entry.height : 1;
            pgraph_apply_scaling_factor(pg, &width, &height);
#ifdef __ANDROID__
            {
                GLint storage_ifmt;
                GLenum storage_fmt;
                GLenum storage_type;

                android_surface_get_storage_format(&entry, &storage_ifmt,
                                                   &storage_fmt, &storage_type);
                glTexImage2D(GL_TEXTURE_2D, 0, storage_ifmt, width, height, 0,
                             storage_fmt, storage_type, NULL);
            }
#else
            glTexImage2D(GL_TEXTURE_2D, 0, entry.fmt.gl_internal_format, width,
                         height, 0, entry.fmt.gl_format, entry.fmt.gl_type,
                         NULL);
#endif
            found = surface_put(d, entry.vram_addr, &entry);

            /* FIXME: Refactor */
            pg->surface_binding_dim.width = entry.width;
            pg->surface_binding_dim.clip_x = entry.shape.clip_x;
            pg->surface_binding_dim.clip_width = entry.shape.clip_width;
            pg->surface_binding_dim.height = entry.height;
            pg->surface_binding_dim.clip_y = entry.shape.clip_y;
            pg->surface_binding_dim.clip_height = entry.shape.clip_height;

            if (color && r->zeta_binding && (r->zeta_binding->width != entry.width || r->zeta_binding->height != entry.height)) {
                pg->surface_zeta.buffer_dirty = true;
            }
        }

#define TRACE_ARGS found->vram_addr, found->width, found->height, \
                   found->swizzle ? "sz" : "ln", found->shape.anti_aliasing, \
                   found->shape.clip_x, found->shape.clip_width, \
                   found->shape.clip_y, found->shape.clip_height, found->pitch

        if (color) {
            if (should_create) {
                trace_nv2a_pgraph_surface_create_color(TRACE_ARGS);
            } else {
                trace_nv2a_pgraph_surface_hit_color(TRACE_ARGS);
            }

            r->color_binding = found;
        } else {
            if (should_create) {
                trace_nv2a_pgraph_surface_create_zeta(TRACE_ARGS);
            } else {
                trace_nv2a_pgraph_surface_hit_zeta(TRACE_ARGS);
            }
            r->zeta_binding = found;
        }
#undef TRACE_ARGS

        glFramebufferTexture2D(GL_FRAMEBUFFER, entry.fmt.gl_attachment,
                               GL_TEXTURE_2D, found->gl_buffer, 0);
        {
            GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
            if (status != GL_FRAMEBUFFER_COMPLETE) {
#ifdef __ANDROID__
                fprintf(stderr, "nv2a: surface FBO incomplete (0x%x)\n",
                        status);
#else
                assert(status == GL_FRAMEBUFFER_COMPLETE);
#endif
            }
        }

        surface->buffer_dirty = false;
    }

    if (!upload && surface->draw_dirty) {
        if (!tcg_enabled()) {
            /* FIXME: Cannot monitor for reads/writes; flush now */
            surface_download(d,
                             color ? r->color_binding :
                                     r->zeta_binding,
                             true);
        }

        surface->write_enabled_cache = false;
        surface->draw_dirty = false;
    }
}

void pgraph_gl_unbind_surface(NV2AState *d, bool color)
{
    PGRAPHState *pg = &d->pgraph;
    PGRAPHGLState *r = pg->gl_renderer_state;

    if (color) {
        if (r->color_binding) {
            glFramebufferTexture2D(GL_FRAMEBUFFER,
                                   GL_COLOR_ATTACHMENT0,
                                   GL_TEXTURE_2D, 0, 0);
            r->color_binding = NULL;
        }
    } else {
        if (r->zeta_binding) {
            glFramebufferTexture2D(GL_FRAMEBUFFER,
                                   GL_DEPTH_ATTACHMENT,
                                   GL_TEXTURE_2D, 0, 0);
            glFramebufferTexture2D(GL_FRAMEBUFFER,
                                   GL_DEPTH_STENCIL_ATTACHMENT,
                                   GL_TEXTURE_2D, 0, 0);
            r->zeta_binding = NULL;
        }
    }
}

void pgraph_gl_surface_update(NV2AState *d, bool upload, bool color_write,
                           bool zeta_write)
{
    PGRAPHState *pg = &d->pgraph;
    PGRAPHGLState *r = pg->gl_renderer_state;

    pg->surface_shape.z_format =
        GET_MASK(pgraph_reg_r(pg, NV_PGRAPH_SETUPRASTER),
                 NV_PGRAPH_SETUPRASTER_Z_FORMAT);

    color_write = color_write &&
            (pg->clearing || pgraph_color_write_enabled(pg));
    zeta_write = zeta_write && (pg->clearing || pgraph_zeta_write_enabled(pg));

    if (upload) {
        bool fb_dirty = framebuffer_dirty(pg);
        if (fb_dirty) {
            memcpy(&pg->last_surface_shape, &pg->surface_shape,
                   sizeof(SurfaceShape));
            pg->surface_color.buffer_dirty = true;
            pg->surface_zeta.buffer_dirty = true;
        }

        if (pg->surface_color.buffer_dirty) {
            pgraph_gl_unbind_surface(d, true);
        }

        if (color_write) {
            update_surface_part(d, true, true);
        }

        if (pg->surface_zeta.buffer_dirty) {
            pgraph_gl_unbind_surface(d, false);
        }

        if (zeta_write) {
            update_surface_part(d, true, false);
        }
    } else {
        if ((color_write || pg->surface_color.write_enabled_cache)
            && pg->surface_color.draw_dirty) {
            update_surface_part(d, false, true);
        }
        if ((zeta_write || pg->surface_zeta.write_enabled_cache)
            && pg->surface_zeta.draw_dirty) {
            update_surface_part(d, false, false);
        }
    }

    if (upload) {
        pg->draw_time++;
    }

    bool swizzle = (pg->surface_type == NV097_SET_SURFACE_FORMAT_TYPE_SWIZZLE);

    if (r->color_binding) {
        r->color_binding->frame_time = pg->frame_time;
        if (upload) {
            pgraph_gl_upload_surface_data(d, r->color_binding, false);
            r->color_binding->draw_time = pg->draw_time;
            r->color_binding->swizzle = swizzle;
        }
    }

    if (r->zeta_binding) {
        r->zeta_binding->frame_time = pg->frame_time;
        if (upload) {
            pgraph_gl_upload_surface_data(d, r->zeta_binding, false);
            r->zeta_binding->draw_time = pg->draw_time;
            r->zeta_binding->swizzle = swizzle;
        }
    }

    // Sanity check color and zeta dimensions match
    if (r->color_binding && r->zeta_binding) {
        assert((r->color_binding->width == r->zeta_binding->width)
               && (r->color_binding->height == r->zeta_binding->height));
    }

    surface_evict_old(d);
}

// FIXME: Move to common
static void surface_get_dimensions(PGRAPHState *pg, unsigned int *width,
                                   unsigned int *height)
{
    bool swizzle = (pg->surface_type == NV097_SET_SURFACE_FORMAT_TYPE_SWIZZLE);
    if (swizzle) {
        *width = 1 << pg->surface_shape.log_width;
        *height = 1 << pg->surface_shape.log_height;
    } else {
        *width = pg->surface_shape.clip_width;
        *height = pg->surface_shape.clip_height;
    }
}

void pgraph_gl_init_surfaces(PGRAPHState *pg)
{
    PGRAPHGLState *r = pg->gl_renderer_state;

    pgraph_gl_reload_surface_scale_factor(pg);
    glGenFramebuffers(1, &r->gl_framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, r->gl_framebuffer);
#ifdef __ANDROID__
    glGenBuffers(1, &r->gl_download_pbo);
    r->gl_download_pbo_size = 0;
    r->android_conv_buf = NULL;
    r->android_conv_buf_size = 0;
    r->android_s2t_conv_buf = NULL;
    r->android_s2t_conv_buf_size = 0;
#endif
    QTAILQ_INIT(&r->surfaces);
    r->downloads_pending = false;
    qemu_event_init(&r->downloads_complete, false);
    qemu_event_init(&r->dirty_surfaces_download_complete, false);

    init_render_to_texture(pg);
}

static void flush_surfaces(NV2AState *d)
{
    PGRAPHState *pg = &d->pgraph;
    PGRAPHGLState *r = pg->gl_renderer_state;

    /* Clear last surface shape to force recreation of buffers at next draw */
    pg->surface_color.draw_dirty = false;
    pg->surface_zeta.draw_dirty = false;
    memset(&pg->last_surface_shape, 0, sizeof(pg->last_surface_shape));
    pgraph_gl_unbind_surface(d, true);
    pgraph_gl_unbind_surface(d, false);

    SurfaceBinding *s, *next;
    QTAILQ_FOREACH_SAFE(s, &r->surfaces, entry, next) {
        // FIXME: We should download all surfaces to ram, but need to
        //        investigate corruption issue
        // pgraph_gl_surface_download_if_dirty(d, s);
        pgraph_gl_surface_invalidate(d, s);
    }
}

void pgraph_gl_finalize_surfaces(PGRAPHState *pg)
{
    NV2AState *d = container_of(pg, NV2AState, pgraph);
    PGRAPHGLState *r = pg->gl_renderer_state;

    flush_surfaces(d);
    glDeleteFramebuffers(1, &r->gl_framebuffer);
    r->gl_framebuffer = 0;

#ifdef __ANDROID__
    if (r->gl_download_pbo) {
        glDeleteBuffers(1, &r->gl_download_pbo);
        r->gl_download_pbo = 0;
        r->gl_download_pbo_size = 0;
    }
    g_free(r->android_conv_buf);
    r->android_conv_buf = NULL;
    r->android_conv_buf_size = 0;
    g_free(r->android_s2t_conv_buf);
    r->android_s2t_conv_buf = NULL;
    r->android_s2t_conv_buf_size = 0;
#endif

    finalize_render_to_texture(pg);
}

void pgraph_gl_surface_flush(NV2AState *d)
{
    PGRAPHState *pg = &d->pgraph;
    PGRAPHGLState *r = pg->gl_renderer_state;

    bool update_surface = (r->color_binding || r->zeta_binding);

    flush_surfaces(d);

    pgraph_gl_reload_surface_scale_factor(pg);

    if (update_surface) {
        pgraph_gl_surface_update(d, true, true, true);
    }
}

#pragma once

#include <stdbool.h>
#include <string.h>

#include <GLES3/gl3.h>
#include <GLES3/gl31.h>
#include <GLES3/gl32.h>
#include <GLES3/gl3ext.h>
#include <GLES2/gl2ext.h>

#ifndef GL_UNPACK_ROW_LENGTH_EXT
#define GL_UNPACK_ROW_LENGTH_EXT GL_UNPACK_ROW_LENGTH
#endif

#ifndef GL_BGRA
#define GL_BGRA 0x80E1
#endif

#ifndef GL_BGRA_EXT
#define GL_BGRA_EXT GL_BGRA
#endif

#ifndef GL_CLAMP_TO_BORDER
#define GL_CLAMP_TO_BORDER GL_CLAMP_TO_EDGE
#endif

#ifndef GL_UNSIGNED_SHORT_5_5_5_1
#define GL_UNSIGNED_SHORT_5_5_5_1 0x8034
#endif

#ifndef GL_UNSIGNED_SHORT_4_4_4_4
#define GL_UNSIGNED_SHORT_4_4_4_4 0x8033
#endif

#ifndef GL_UNSIGNED_INT_8_8_8_8
#define GL_UNSIGNED_INT_8_8_8_8 0x8035
#endif

#ifndef GL_RGB5_A1
#define GL_RGB5_A1 0x8057
#endif

#ifndef GL_RGB5
#define GL_RGB5 GL_RGB5_A1
#endif

#ifndef GL_UNSIGNED_SHORT_1_5_5_5_REV
#define GL_UNSIGNED_SHORT_1_5_5_5_REV GL_UNSIGNED_SHORT_5_5_5_1
#endif

#ifndef GL_UNSIGNED_SHORT_4_4_4_4_REV
#define GL_UNSIGNED_SHORT_4_4_4_4_REV GL_UNSIGNED_SHORT_4_4_4_4
#endif

#ifndef GL_UNSIGNED_INT_8_8_8_8_REV
#define GL_UNSIGNED_INT_8_8_8_8_REV GL_UNSIGNED_INT_8_8_8_8
#endif

#ifndef GL_COMPRESSED_RGBA_S3TC_DXT1_EXT
#define GL_COMPRESSED_RGBA_S3TC_DXT1_EXT 0x83F1
#endif

#ifndef GL_COMPRESSED_RGBA_S3TC_DXT3_EXT
#define GL_COMPRESSED_RGBA_S3TC_DXT3_EXT 0x83F2
#endif

#ifndef GL_COMPRESSED_RGBA_S3TC_DXT5_EXT
#define GL_COMPRESSED_RGBA_S3TC_DXT5_EXT 0x83F3
#endif

#ifndef GL_R16
#define GL_R16 0x822A
#endif

#ifndef GL_BGR
#define GL_BGR GL_RGB
#endif

#ifndef GL_FILL
#define GL_FILL 0x1B02
#endif

#ifndef GL_SAMPLES_PASSED
#define GL_SAMPLES_PASSED GL_ANY_SAMPLES_PASSED
#endif

#ifndef GL_SMOOTH_LINE_WIDTH_RANGE
#define GL_SMOOTH_LINE_WIDTH_RANGE GL_ALIASED_LINE_WIDTH_RANGE
#endif

#ifndef GL_PROGRAM_POINT_SIZE
#define GL_PROGRAM_POINT_SIZE 0x8642
#endif

#ifndef GL_TEXTURE_LOD_BIAS
#define GL_TEXTURE_LOD_BIAS 0x8501
#endif

#ifndef GL_TEXTURE_MAX_ANISOTROPY_EXT
#define GL_TEXTURE_MAX_ANISOTROPY_EXT 0x84FE
#endif

#ifndef GL_TEXTURE_SWIZZLE_RGBA
#define GL_TEXTURE_SWIZZLE_RGBA 0x8E46
#endif

#ifndef GL_TEXTURE_1D
#define GL_TEXTURE_1D 0x0DE0
#endif

#ifndef GL_FIRST_VERTEX_CONVENTION
#define GL_FIRST_VERTEX_CONVENTION 0x8E4D
#endif

#ifndef GL_POLYGON_OFFSET_LINE
#define GL_POLYGON_OFFSET_LINE GL_POLYGON_OFFSET_FILL
#endif

#ifndef GL_POLYGON_OFFSET_POINT
#define GL_POLYGON_OFFSET_POINT GL_POLYGON_OFFSET_FILL
#endif

#if !defined(GL_ES_VERSION_3_1)
static inline void glProgramUniform1i(GLuint program, GLint location, GLint v0)
{
    glUseProgram(program);
    glUniform1i(location, v0);
}
#endif

static inline void glClearDepth(GLfloat depth)
{
    glClearDepthf(depth);
}

static inline void glPolygonMode(GLenum face, GLenum mode)
{
    (void)face;
    (void)mode;
}

static inline void glProvokingVertex(GLenum mode)
{
    (void)mode;
}

static inline void glMultiDrawArrays(GLenum mode, const GLint *first,
                                     const GLsizei *count, GLsizei drawcount)
{
    for (GLsizei i = 0; i < drawcount; i++) {
        glDrawArrays(mode, first[i], count[i]);
    }
}

static inline int epoxy_gl_version(void)
{
    return 30;
}

static inline bool epoxy_has_gl_extension(const char *ext)
{
    if (!ext || !*ext) {
        return false;
    }
    const char *exts = (const char *)glGetString(GL_EXTENSIONS);
    if (!exts) {
        return false;
    }
    const char *start = exts;
    size_t ext_len = strlen(ext);
    while ((start = strstr(start, ext)) != NULL) {
        const char *end = start + ext_len;
        if ((start == exts || start[-1] == ' ') &&
            (*end == '\0' || *end == ' ')) {
            return true;
        }
        start = end;
    }
    return false;
}

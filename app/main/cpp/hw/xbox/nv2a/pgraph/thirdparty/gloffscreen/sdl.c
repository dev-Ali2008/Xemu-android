/*
 *  Offscreen OpenGL abstraction layer -- SDL based
 *
 *  Copyright (c) 2018-2024 Matt Borgerson
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdint.h>

#include "gloffscreen.h"

#include <SDL.h>
#include <SDL_syswm.h>

#ifdef __ANDROID__
#include <android/log.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#endif

struct _GloContext {
    SDL_Window    *window;
    SDL_GLContext gl_context;
#ifdef __ANDROID__
    EGLDisplay egl_display;
    EGLSurface egl_surface;
    EGLContext egl_context;
    bool egl_offscreen;
#endif
};

#ifdef __ANDROID__
static EGLDisplay g_android_egl_display = EGL_NO_DISPLAY;
static EGLContext g_android_egl_share = EGL_NO_CONTEXT;
static EGLConfig g_android_egl_config = NULL;
static EGLint g_android_egl_config_id = 0;
static SDL_Window *g_android_main_window = NULL;
static SDL_GLContext g_android_main_context = NULL;

void glo_android_cache_current_egl_state(void)
{
    g_android_egl_display = eglGetCurrentDisplay();
    g_android_egl_share = eglGetCurrentContext();
    g_android_egl_config = NULL;
    g_android_egl_config_id = 0;
    
    // Also cache the SDL window and context
    g_android_main_window = SDL_GL_GetCurrentWindow();
    g_android_main_context = SDL_GL_GetCurrentContext();

    EGLSurface cur_surface = eglGetCurrentSurface(EGL_DRAW);
    if (g_android_egl_display == EGL_NO_DISPLAY ||
        cur_surface == EGL_NO_SURFACE) {
        return;
    }

    eglQuerySurface(g_android_egl_display, cur_surface, EGL_CONFIG_ID,
                    &g_android_egl_config_id);
    if (g_android_egl_config_id == 0) {
        return;
    }

    EGLint num = 0;
    eglGetConfigs(g_android_egl_display, NULL, 0, &num);
    if (num <= 0) {
        return;
    }

    EGLConfig *configs = (EGLConfig *)malloc(sizeof(EGLConfig) * num);
    if (!configs) {
        return;
    }

    if (eglGetConfigs(g_android_egl_display, configs, num, &num)) {
        for (EGLint i = 0; i < num; ++i) {
            EGLint id = 0;
            eglGetConfigAttrib(g_android_egl_display, configs[i],
                               EGL_CONFIG_ID, &id);
            if (id == g_android_egl_config_id) {
                g_android_egl_config = configs[i];
                break;
            }
        }
    }

    free(configs);
}

static bool android_check_context_sharing(EGLDisplay dpy, EGLContext new_ctx,
                                          EGLSurface new_surf)
{
    // Save the previously current context so we can restore it after the test
    EGLContext old_ctx = eglGetCurrentContext();
    EGLDisplay old_dpy = eglGetCurrentDisplay();
    EGLSurface old_draw = eglGetCurrentSurface(EGL_DRAW);
    EGLSurface old_read = eglGetCurrentSurface(EGL_READ);

    if (eglMakeCurrent(dpy, new_surf, new_surf, new_ctx) != EGL_TRUE) {
        __android_log_print(ANDROID_LOG_ERROR, "hakuX",
                            "glo_context_create: eglMakeCurrent(test) failed");
        // Restore old context before returning
        if (old_ctx != EGL_NO_CONTEXT && old_dpy != EGL_NO_DISPLAY) {
            eglMakeCurrent(old_dpy, old_draw, old_read, old_ctx);
        }
        return false;
    }

    // Create a test texture to verify the context works
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    const uint32_t pixel = 0xffffffffu;
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, &pixel);
    glFlush();
    
    bool valid = (glIsTexture(tex) == GL_TRUE && glGetError() == GL_NO_ERROR);
    glDeleteTextures(1, &tex);

    // Restore the previous context
    if (old_ctx != EGL_NO_CONTEXT && old_dpy != EGL_NO_DISPLAY) {
        eglMakeCurrent(old_dpy, old_draw, old_read, old_ctx);
    }

    __android_log_print(ANDROID_LOG_INFO, "hakuX",
                        "glo_context_create: context creation test %s",
                        valid ? "PASSED" : "FAILED");
    return valid;
}
#endif

/* Create an OpenGL context */
GloContext *glo_context_create(void)
{
    GloContext *context = (GloContext *)malloc(sizeof(GloContext));
    assert(context != NULL);

    SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

    // Initialize rendering context
    SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, 1);
#ifdef __ANDROID__
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
                        SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
#else
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
                        SDL_GL_CONTEXT_PROFILE_CORE);
#endif

#ifdef __ANDROID__
    bool allow_egl_offscreen = true;
    const char *egl_env = SDL_getenv("XEMU_ANDROID_EGL_OFFSCREEN");
    if (egl_env && (strcmp(egl_env, "0") == 0 || strcmp(egl_env, "false") == 0 ||
                    strcmp(egl_env, "FALSE") == 0)) {
        allow_egl_offscreen = false;
    }
    if (g_android_egl_display == EGL_NO_DISPLAY ||
        g_android_egl_share == EGL_NO_CONTEXT) {
        glo_android_cache_current_egl_state();
    }
    if (g_android_egl_display == EGL_NO_DISPLAY ||
        g_android_egl_share == EGL_NO_CONTEXT) {
        allow_egl_offscreen = false;
    }
    context->egl_offscreen = false;
    context->egl_display = EGL_NO_DISPLAY;
    context->egl_surface = EGL_NO_SURFACE;
    context->egl_context = EGL_NO_CONTEXT;

    if (allow_egl_offscreen) {
        EGLDisplay dpy = g_android_egl_display;
        EGLContext share = g_android_egl_share;
        __android_log_print(ANDROID_LOG_INFO, "hakuX",
                            "%s: EGL dpy=%p share=%p", __func__,
                            (void *)dpy, (void *)share);
        if (dpy != EGL_NO_DISPLAY && share != EGL_NO_CONTEXT) {
            EGLConfig chosen = g_android_egl_config;
            if (chosen == NULL && g_android_egl_config_id != 0) {
                __android_log_print(ANDROID_LOG_INFO, "hakuX",
                                    "%s: looking up config by id=%d",
                                    __func__, (int)g_android_egl_config_id);
                EGLint num = 0;
                eglGetConfigs(dpy, NULL, 0, &num);
                if (num > 0) {
                    EGLConfig *configs = (EGLConfig *)malloc(sizeof(EGLConfig) * num);
                    if (configs && eglGetConfigs(dpy, configs, num, &num)) {
                        for (EGLint i = 0; i < num; ++i) {
                            EGLint id = 0;
                            eglGetConfigAttrib(dpy, configs[i], EGL_CONFIG_ID, &id);
                            if (id == g_android_egl_config_id) {
                                chosen = configs[i];
                                __android_log_print(ANDROID_LOG_INFO, "hakuX",
                                                    "%s: found config %p",
                                                    __func__, (void *)chosen);
                                break;
                            }
                        }
                    }
                    free(configs);
                }
            }

            if (chosen == NULL) {
                __android_log_print(ANDROID_LOG_INFO, "hakuX",
                                    "%s: choosing fallback config", __func__);
                const EGLint attribs[] = {
                    EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
                    EGL_SURFACE_TYPE, EGL_WINDOW_BIT | EGL_PBUFFER_BIT,
                    EGL_RED_SIZE, 8,
                    EGL_GREEN_SIZE, 8,
                    EGL_BLUE_SIZE, 8,
                    EGL_ALPHA_SIZE, 8,
                    EGL_DEPTH_SIZE, 24,
                    EGL_STENCIL_SIZE, 8,
                    EGL_NONE
                };
                EGLint num = 0;
                if (eglChooseConfig(dpy, attribs, &chosen, 1, &num) != EGL_TRUE || num <= 0) {
                    chosen = NULL;
                    __android_log_print(ANDROID_LOG_ERROR, "hakuX",
                                        "%s: eglChooseConfig failed", __func__);
                } else {
                    __android_log_print(ANDROID_LOG_INFO, "hakuX",
                                        "%s: chose config %p", __func__, (void *)chosen);
                }
            }

            if (chosen != NULL) {
                eglBindAPI(EGL_OPENGL_ES_API);
                const char *exts = eglQueryString(dpy, EGL_EXTENSIONS);
                bool surfaceless = exts && strstr(exts, "EGL_KHR_surfaceless_context");
                __android_log_print(ANDROID_LOG_INFO, "hakuX",
                                    "%s: surfaceless=%s", __func__,
                                    surfaceless ? "yes" : "no");
                // Prefer a small pbuffer surface even if surfaceless is supported.
                EGLSurface surf = EGL_NO_SURFACE;
                {
                    const EGLint pb_attribs[] = {
                        EGL_WIDTH, 16,
                        EGL_HEIGHT, 16,
                        EGL_NONE
                    };
                    __android_log_print(ANDROID_LOG_INFO, "hakuX",
                                        "%s: creating pbuffer surface...", __func__);
                    surf = eglCreatePbufferSurface(dpy, chosen, pb_attribs);
                    __android_log_print(ANDROID_LOG_INFO, "hakuX",
                                        "%s: pbuffer surface=%p", __func__,
                                        (void *)surf);
                }
                if (surf == EGL_NO_SURFACE && surfaceless) {
                    __android_log_print(ANDROID_LOG_INFO, "hakuX",
                                        "%s: pbuffer failed, using surfaceless", __func__);
                    // Fall back to surfaceless when pbuffer creation fails.
                    surf = EGL_NO_SURFACE;
                }

                __android_log_print(ANDROID_LOG_INFO, "hakuX",
                                    "%s: creating EGL context (ES3)...", __func__);
                const EGLint ctx_attribs[] = {
                    EGL_CONTEXT_CLIENT_VERSION, 3,
                    EGL_NONE
                };
                EGLContext ctx = eglCreateContext(dpy, chosen, share, ctx_attribs);
                if (ctx == EGL_NO_CONTEXT) {
                    __android_log_print(ANDROID_LOG_INFO, "hakuX",
                                        "%s: ES3 failed, trying ES2", __func__);
                    const EGLint ctx_attribs2[] = {
                        EGL_CONTEXT_CLIENT_VERSION, 2,
                        EGL_NONE
                    };
                    ctx = eglCreateContext(dpy, chosen, share, ctx_attribs2);
                }
                __android_log_print(ANDROID_LOG_INFO, "hakuX",
                                    "%s: EGL context=%p", __func__, (void *)ctx);

                if (ctx != EGL_NO_CONTEXT && (surf != EGL_NO_SURFACE || surfaceless)) {
                    // Test that the new context was created successfully
                    __android_log_print(ANDROID_LOG_INFO, "hakuX",
                                        "%s: testing context sharing...", __func__);
                    bool valid = android_check_context_sharing(dpy, ctx, surf);
                    __android_log_print(ANDROID_LOG_INFO, "hakuX",
                                        "%s: context test %s", __func__,
                                        valid ? "PASSED" : "FAILED");
                    
                    if (valid) {
                        context->egl_offscreen = true;
                        context->egl_display = dpy;
                        context->egl_surface = surf;
                        context->egl_context = ctx;
                        context->window = NULL;
                        context->gl_context = NULL;
                        __android_log_print(ANDROID_LOG_INFO, "hakuX",
                                            "%s: created EGL offscreen ctx=%p surf=%p",
                                            __func__, (void *)ctx, (void *)surf);
                        return context;
                    }
                    __android_log_print(ANDROID_LOG_WARN, "hakuX",
                                        "%s: EGL context test failed; falling back to SDL",
                                        __func__);
                }

                if (surf != EGL_NO_SURFACE) {
                    eglDestroySurface(dpy, surf);
                }
                if (ctx != EGL_NO_CONTEXT) {
                    eglDestroyContext(dpy, ctx);
                }
            } else {
                __android_log_print(ANDROID_LOG_ERROR, "hakuX",
                                    "%s: no EGL config available", __func__);
            }
        } else {
            __android_log_print(ANDROID_LOG_WARN, "hakuX",
                                "%s: EGL display or share context not available",
                                __func__);
        }
    } else {
        __android_log_print(ANDROID_LOG_INFO, "hakuX",
                            "%s: EGL offscreen disabled, using SDL context",
                            __func__);
    }

    // Fallback: Create a dedicated window for the offscreen context.
    SDL_Window *current = SDL_GL_GetCurrentWindow();
    __android_log_print(ANDROID_LOG_INFO, "hakuX",
                        "%s: SDL fallback, current window=%p",
                        __func__, (void *)current);
    context->window = SDL_CreateWindow(
        "SDL Offscreen Window",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        640, 480,
        SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);

    if (context->window == NULL) {
        // Fall back to the current window if Android disallows hidden windows.
        context->window = SDL_GL_GetCurrentWindow();
        __android_log_print(ANDROID_LOG_WARN, "hakuX",
                            "%s: hidden window failed, using current=%p",
                            __func__, (void *)context->window);
        if (context->window == NULL) {
            fprintf(stderr, "%s: Failed to create or reuse window\n", __func__);
            SDL_Quit();
            exit(1);
        }
    } else {
        __android_log_print(ANDROID_LOG_INFO, "hakuX",
                            "%s: created hidden window=%p (current=%p)",
                            __func__, (void *)context->window, (void *)current);
    }
#else
    // Create a dedicated window for the offscreen context.
    context->window = SDL_CreateWindow(
        "SDL Offscreen Window",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        640, 480,
        SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    if (context->window == NULL) {
        fprintf(stderr, "%s: Failed to create window\n", __func__);
        SDL_Quit();
        exit(1);
    }
#endif

    __android_log_print(ANDROID_LOG_INFO, "hakuX",
                        "%s: creating SDL GL context...", __func__);
    context->gl_context = SDL_GL_CreateContext(context->window);
    if (context->gl_context == NULL) {
        fprintf(stderr, "%s: Failed to create GL context\n", __func__);
        SDL_DestroyWindow(context->window);
        SDL_Quit();
        exit(1);
    }
    __android_log_print(ANDROID_LOG_INFO, "hakuX",
                        "%s: created SDL GL context=%p", __func__,
                        (void *)context->gl_context);
#ifdef __ANDROID__
    // Unbind any current context before making the new one current.
    __android_log_print(ANDROID_LOG_INFO, "hakuX",
                        "%s: unbinding current context", __func__);
    if (SDL_GL_MakeCurrent(NULL, NULL) != 0) {
        __android_log_print(ANDROID_LOG_ERROR, "hakuX",
                            "%s: SDL_GL_MakeCurrent(NULL) failed: %s",
                            __func__, SDL_GetError());
    }
#endif
#ifdef __ANDROID__
    __android_log_print(ANDROID_LOG_INFO, "hakuX",
                        "%s: created GL context=%p for window=%p",
                        __func__, (void *)context->gl_context,
                        (void *)context->window);
#endif

    __android_log_print(ANDROID_LOG_INFO, "hakuX",
                        "%s: making context current...", __func__);
    glo_set_current(context);
    __android_log_print(ANDROID_LOG_INFO, "hakuX",
                        "%s: context is current", __func__);

    return context;
}

/* Wrap an already-current OpenGL context */
GloContext *glo_context_wrap_current(void)
{
    SDL_Window *window = SDL_GL_GetCurrentWindow();
    SDL_GLContext gl_context = SDL_GL_GetCurrentContext();
    if (!window || !gl_context) {
        fprintf(stderr, "%s: No current GL context to wrap\n", __func__);
#ifdef __ANDROID__
        __android_log_print(ANDROID_LOG_ERROR, "hakuX",
                            "%s: no current GL context (window=%p, ctx=%p)",
                            __func__, (void *)window, (void *)gl_context);
#endif
        return NULL;
    }

    GloContext *context = (GloContext *)malloc(sizeof(GloContext));
    assert(context != NULL);
    context->window = window;
    context->gl_context = gl_context;
#ifdef __ANDROID__
    context->egl_display = EGL_NO_DISPLAY;
    context->egl_surface = EGL_NO_SURFACE;
    context->egl_context = EGL_NO_CONTEXT;
    context->egl_offscreen = false;
    glo_android_cache_current_egl_state();
#endif
#ifdef __ANDROID__
    __android_log_print(ANDROID_LOG_INFO, "hakuX",
                        "%s: wrapped window=%p ctx=%p",
                        __func__, (void *)window, (void *)gl_context);
    __android_log_print(ANDROID_LOG_INFO, "hakuX",
                        "%s: saved egl display=%p share=%p",
                        __func__, (void *)g_android_egl_display,
                        (void *)g_android_egl_share);
    if (g_android_egl_config_id != 0) {
        __android_log_print(ANDROID_LOG_INFO, "hakuX",
                            "%s: saved egl config id=%d", __func__,
                            (int)g_android_egl_config_id);
    }
#endif
    return context;
}

/* Set current context */
void glo_set_current(GloContext *context)
{
    if (context == NULL) {
#ifdef __ANDROID__
        EGLDisplay dpy = eglGetCurrentDisplay();
        if (dpy != EGL_NO_DISPLAY) {
            if (eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT) != EGL_TRUE) {
                __android_log_print(ANDROID_LOG_ERROR, "hakuX",
                                    "glo_set_current(NULL) eglMakeCurrent failed");
            }
            return;
        }
#endif
        if (SDL_GL_MakeCurrent(NULL, NULL) != 0) {
            fprintf(stderr, "glo_set_current(NULL) failed: %s\n",
                    SDL_GetError());
#ifdef __ANDROID__
            __android_log_print(ANDROID_LOG_ERROR, "hakuX",
                                "glo_set_current(NULL) failed: %s",
                                SDL_GetError());
#endif
        }
    } else {
#ifdef __ANDROID__
        if (context->egl_offscreen) {
            if (eglMakeCurrent(context->egl_display, context->egl_surface,
                               context->egl_surface, context->egl_context) != EGL_TRUE) {
                __android_log_print(ANDROID_LOG_ERROR, "hakuX",
                                    "glo_set_current(offscreen) failed");
            }
            return;
        }
#endif
        if (SDL_GL_MakeCurrent(context->window, context->gl_context) != 0) {
            fprintf(stderr, "glo_set_current(%p,%p) failed: %s\n",
                    (void *)context->window, (void *)context->gl_context,
                    SDL_GetError());
#ifdef __ANDROID__
            __android_log_print(ANDROID_LOG_ERROR, "hakuX",
                                "glo_set_current(%p,%p) failed: %s",
                                (void *)context->window,
                                (void *)context->gl_context,
                                SDL_GetError());
#endif
        }
    }
}

/* Destroy a previously created OpenGL context */
void glo_context_destroy(GloContext *context)
{
    if (!context) return;
#ifdef __ANDROID__
    if (context->egl_offscreen) {
        if (context->egl_display != EGL_NO_DISPLAY) {
            eglMakeCurrent(context->egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
            if (context->egl_surface != EGL_NO_SURFACE) {
                eglDestroySurface(context->egl_display, context->egl_surface);
            }
            if (context->egl_context != EGL_NO_CONTEXT) {
                eglDestroyContext(context->egl_display, context->egl_context);
            }
        }
        free(context);
        return;
    }
#endif
    glo_set_current(NULL);
    free(context);
}

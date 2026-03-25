#include "qemu/osdep.h"
#include "ui/xemu-snapshots.h"
#include "qapi/error.h"
#include "migration/snapshot.h"
#include "migration/qemu-file.h"
#include "system/runstate.h"
#include "xemu-xbe.h"

#include <SDL.h>
#include <SDL_atomic.h>
#include <SDL_system.h>
#include <GLES3/gl3.h>
#include <android/log.h>
#include <glib/gstdio.h>
#include <jni.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>

const char **g_snapshot_shortcut_index_key_map[] = { NULL };

static bool xemu_snapshots_dirty = true;
static GLuint g_snapshot_display_tex = 0;
static bool g_snapshot_display_flip = false;
static SDL_atomic_t g_snapshot_pending = { 0 };
static SDL_atomic_t g_reboot_pending = { 0 };

#define SNAPSHOT_PREVIEW_WIDTH  320
#define SNAPSHOT_PREVIEW_HEIGHT 240
#define SNAPSHOT_PREVIEW_VERSION 1

#define SNAP_LOGI(...) __android_log_print(ANDROID_LOG_INFO, "xemu-android", __VA_ARGS__)
#define SNAP_LOGW(...) __android_log_print(ANDROID_LOG_WARN, "xemu-android", __VA_ARGS__)

typedef struct SnapshotPreviewHeader {
    char magic[4];
    uint16_t version;
    uint16_t width;
    uint16_t height;
    uint16_t channels;
} SnapshotPreviewHeader;

static void sanitize_snapshot_name(const char *in, char *out, size_t out_len)
{
    size_t j = 0;

    if (!out || out_len == 0) {
        return;
    }

    if (!in || !in[0]) {
        g_strlcpy(out, "snapshot", out_len);
        return;
    }

    for (size_t i = 0; in[i] && j + 1 < out_len; ++i) {
        unsigned char c = (unsigned char)in[i];
        if (g_ascii_isalnum(c) || c == '_' || c == '-') {
            out[j++] = (char)c;
        } else {
            out[j++] = '_';
        }
    }

    if (j == 0) {
        g_strlcpy(out, "snapshot", out_len);
    } else {
        out[j] = '\0';
    }
}

static char *get_snapshot_preview_dir(void)
{
    const char *base = SDL_AndroidGetInternalStoragePath();
    char *dir;

    if (!base || !base[0]) {
        return NULL;
    }

    dir = g_strdup_printf("%s/xanite/snapshots", base);
    if (g_mkdir_with_parents(dir, 0700) != 0) {
        SNAP_LOGW("failed to create snapshot preview dir: %s", dir);
        g_free(dir);
        return NULL;
    }

    return dir;
}

static char *get_snapshot_title(void)
{
    struct xbe *xbe_data = xemu_get_xbe_info();
    char *title = NULL;

    if (xbe_data && xbe_data->cert) {
        glong items_written = 0;
        title = g_utf16_to_utf8((const gunichar2 *)xbe_data->cert->m_title_name,
                                40, NULL, &items_written, NULL);
        if (title) {
            g_strstrip(title);
            if (title[0]) {
                return title;
            }
            g_free(title);
            title = NULL;
        }
    }

    return g_strdup("Unknown Game");
}

static bool capture_snapshot_thumbnail(uint8_t **pixels_out, size_t *pixels_size_out)
{
    GLint viewport[4] = { 0, 0, 0, 0 };
    GLint prev_pack_alignment = 4;
    uint8_t *src_pixels = NULL;
    uint8_t *dst_pixels = NULL;
    bool ok = false;

    if (!pixels_out || !pixels_size_out) {
        return false;
    }

    *pixels_out = NULL;
    *pixels_size_out = 0;

    if (!SDL_GL_GetCurrentContext() || g_snapshot_display_tex == 0) {
        return false;
    }

    glGetIntegerv(GL_VIEWPORT, viewport);

    if (viewport[2] <= 0 || viewport[3] <= 0) {
        return false;
    }

    (void)g_snapshot_display_flip;

    {
        const int src_w = viewport[2];
        const int src_h = viewport[3];
        const size_t src_bytes = (size_t)src_w * (size_t)src_h * 4;
        const size_t dst_bytes = (size_t)SNAPSHOT_PREVIEW_WIDTH *
                                 (size_t)SNAPSHOT_PREVIEW_HEIGHT * 4;

        src_pixels = g_malloc(src_bytes);
        dst_pixels = g_malloc(dst_bytes);

        glGetIntegerv(GL_PACK_ALIGNMENT, &prev_pack_alignment);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(viewport[0], viewport[1], src_w, src_h,
                     GL_RGBA, GL_UNSIGNED_BYTE, src_pixels);
        glPixelStorei(GL_PACK_ALIGNMENT, prev_pack_alignment);
        if (glGetError() != GL_NO_ERROR) {
            goto cleanup;
        }

        for (int y = 0; y < SNAPSHOT_PREVIEW_HEIGHT; ++y) {
            const int src_y = (int)(((int64_t)y * src_h) / SNAPSHOT_PREVIEW_HEIGHT);
            for (int x = 0; x < SNAPSHOT_PREVIEW_WIDTH; ++x) {
                const int src_x = (int)(((int64_t)x * src_w) / SNAPSHOT_PREVIEW_WIDTH);
                const size_t src_off = ((size_t)src_y * (size_t)src_w + (size_t)src_x) * 4;
                const size_t dst_off =
                    ((size_t)y * (size_t)SNAPSHOT_PREVIEW_WIDTH + (size_t)x) * 4;
                memcpy(dst_pixels + dst_off, src_pixels + src_off, 4);
            }
        }
    }

    *pixels_out = dst_pixels;
    *pixels_size_out = (size_t)SNAPSHOT_PREVIEW_WIDTH *
                       (size_t)SNAPSHOT_PREVIEW_HEIGHT * 4;
    dst_pixels = NULL;
    ok = true;

cleanup:
    g_free(src_pixels);
    g_free(dst_pixels);
    return ok;
}

static void write_snapshot_preview_sidecar(const char *vm_name)
{
    char safe_name[128];
    char *dir = NULL;
    char *thumb_path = NULL;
    char *title_path = NULL;
    char *title = NULL;
    uint8_t *pixels = NULL;
    size_t pixels_size = 0;

    FILE *title_file = NULL;
    FILE *thumb_file = NULL;

    if (!vm_name || !vm_name[0]) {
        return;
    }

    sanitize_snapshot_name(vm_name, safe_name, sizeof(safe_name));

    dir = get_snapshot_preview_dir();
    if (!dir) {
        return;
    }

    thumb_path = g_strdup_printf("%s/%s.thm", dir, safe_name);
    title_path = g_strdup_printf("%s/%s.title", dir, safe_name);

    title = get_snapshot_title();
    if (title) {
        title_file = fopen(title_path, "wb");
        if (title_file) {
            fwrite(title, 1, strlen(title), title_file);
            fclose(title_file);
            title_file = NULL;
        }
    }

    if (!capture_snapshot_thumbnail(&pixels, &pixels_size)) {
        SNAP_LOGW("snapshot preview capture failed for %s", vm_name);
        goto cleanup;
    }

    {
        SnapshotPreviewHeader header;
        memcpy(header.magic, "X1TH", 4);
        header.version = SNAPSHOT_PREVIEW_VERSION;
        header.width = SNAPSHOT_PREVIEW_WIDTH;
        header.height = SNAPSHOT_PREVIEW_HEIGHT;
        header.channels = 4;

        thumb_file = fopen(thumb_path, "wb");
        if (!thumb_file) {
            SNAP_LOGW("failed to open snapshot preview file: %s", thumb_path);
            goto cleanup;
        }

        if (fwrite(&header, sizeof(header), 1, thumb_file) != 1 ||
            fwrite(pixels, 1, pixels_size, thumb_file) != pixels_size) {
            SNAP_LOGW("failed writing snapshot preview: %s", thumb_path);
        }

        fclose(thumb_file);
        thumb_file = NULL;
    }

cleanup:
    if (thumb_file) {
        fclose(thumb_file);
    }
    if (title_file) {
        fclose(title_file);
    }
    g_free(pixels);
    g_free(title);
    g_free(title_path);
    g_free(thumb_path);
    g_free(dir);
}

char *xemu_get_currently_loaded_disc_path(void)
{
    return NULL;
}

void xemu_snapshots_save(const char *vm_name, Error **err)
{
    save_snapshot(vm_name, true, NULL, false, NULL, err);
    xemu_snapshots_dirty = true;
}

void xemu_snapshots_load(const char *vm_name, Error **err)
{
    bool was_running = runstate_is_running();
    vm_stop(RUN_STATE_RESTORE_VM);
    if (load_snapshot(vm_name, NULL, false, NULL, err) && was_running) {
        vm_start();
    }
}

void xemu_snapshots_delete(const char *vm_name, Error **err)
{
    delete_snapshot(vm_name, false, NULL, err);
    xemu_snapshots_dirty = true;
}

void xemu_snapshots_mark_dirty(void)
{
    xemu_snapshots_dirty = true;
}

int xemu_snapshots_list(QEMUSnapshotInfo **info, XemuSnapshotData **extra_data,
                        Error **err)
{
    (void)err;
    if (info) {
        *info = NULL;
    }
    if (extra_data) {
        *extra_data = NULL;
    }
    return 0;
}

void xemu_snapshots_save_extra_data(QEMUFile *f)
{
    char *title = get_snapshot_title();
    size_t title_size = title ? strlen(title) : 0;

    if (title_size > 255) {
        title_size = 255;
    }

    qemu_put_be32(f, XEMU_SNAPSHOT_DATA_MAGIC);
    qemu_put_be32(f, XEMU_SNAPSHOT_DATA_VERSION);
    qemu_put_be32(f, 4 + 1 + title_size + 4);
    qemu_put_be32(f, 0);
    qemu_put_byte(f, (uint8_t)title_size);
    if (title_size) {
        qemu_put_buffer(f, (const uint8_t *)title, title_size);
    }
    qemu_put_be32(f, 0);

    g_free(title);
    xemu_snapshots_dirty = true;
}

bool xemu_snapshots_offset_extra_data(QEMUFile *f)
{
    unsigned int v;
    uint32_t size;

    v = qemu_get_be32(f);
    if (v != XEMU_SNAPSHOT_DATA_MAGIC) {
        qemu_file_skip(f, -4);
        return true;
    }

    qemu_get_be32(f);
    size = qemu_get_be32(f);

    {
        void *buf = g_malloc(size);
        qemu_get_buffer(f, buf, size);
        g_free(buf);
    }

    return true;
}

void xemu_snapshots_set_framebuffer_texture(GLuint tex, bool flip)
{
    g_snapshot_display_tex = tex;
    g_snapshot_display_flip = flip;
}

bool xemu_snapshots_load_png_to_texture(GLuint tex, void *buf, size_t size)
{
    (void)tex;
    (void)buf;
    (void)size;
    return false;
}

void *xemu_snapshots_create_framebuffer_thumbnail_png(size_t *size)
{
    if (size) {
        *size = 0;
    }
    return NULL;
}

typedef enum SnapOpType {
    SNAP_NONE,
    SNAP_SAVE,
    SNAP_LOAD,
} SnapOpType;

static struct {
    pthread_mutex_t lock;
    pthread_cond_t cond;
    SnapOpType type;
    char name[128];
    bool pending;
    bool done;
    bool success;
} g_snap_req = {
    .lock = PTHREAD_MUTEX_INITIALIZER,
    .cond = PTHREAD_COND_INITIALIZER,
};

void xemu_android_process_snapshot_request(void)
{
    if (SDL_AtomicCAS(&g_reboot_pending, 1, 0)) {
        qemu_system_reset_request(SHUTDOWN_CAUSE_GUEST_RESET);
        return;
    }

    if (SDL_AtomicGet(&g_snapshot_pending) == 0) {
        return;
    }

    if (pthread_mutex_trylock(&g_snap_req.lock) != 0) {
        return;
    }

    if (!g_snap_req.pending) {
        SDL_AtomicSet(&g_snapshot_pending, 0);
        pthread_mutex_unlock(&g_snap_req.lock);
        return;
    }

    Error *err = NULL;

    if (g_snap_req.type == SNAP_SAVE) {
        xemu_snapshots_save(g_snap_req.name, &err);
        if (!err) {
            write_snapshot_preview_sidecar(g_snap_req.name);
        }
    } else if (g_snap_req.type == SNAP_LOAD) {
        xemu_snapshots_load(g_snap_req.name, &err);
    }

    if (err) {
        SNAP_LOGW("snapshot op failed: %s", error_get_pretty(err));
        error_free(err);
        g_snap_req.success = false;
    } else {
        g_snap_req.success = true;
    }

    g_snap_req.pending = false;
    SDL_AtomicSet(&g_snapshot_pending, 0);
    g_snap_req.done = true;
    pthread_cond_signal(&g_snap_req.cond);
    pthread_mutex_unlock(&g_snap_req.lock);
}

static jboolean dispatch_snapshot(JNIEnv *env, jstring jname, SnapOpType type)
{
    const char *name = (*env)->GetStringUTFChars(env, jname, NULL);

    pthread_mutex_lock(&g_snap_req.lock);
    g_snap_req.type = type;
    g_snap_req.pending = true;
    SDL_AtomicSet(&g_snapshot_pending, 1);
    g_snap_req.done = false;
    strncpy(g_snap_req.name, name, sizeof(g_snap_req.name) - 1);
    g_snap_req.name[sizeof(g_snap_req.name) - 1] = '\0';
    (*env)->ReleaseStringUTFChars(env, jname, name);

    while (!g_snap_req.done) {
        pthread_cond_wait(&g_snap_req.cond, &g_snap_req.lock);
    }

    jboolean ok = (jboolean)g_snap_req.success;
    g_snap_req.type = SNAP_NONE;
    pthread_mutex_unlock(&g_snap_req.lock);
    return ok;
}

JNIEXPORT jboolean JNICALL
Java_Ali_Xanite_MainActivity_nativeSaveSnapshot(
        JNIEnv *env, jobject obj, jstring name)
{
    (void)obj;
    return dispatch_snapshot(env, name, SNAP_SAVE);
}

JNIEXPORT jboolean JNICALL
Java_Ali_Xanite_MainActivity_nativeLoadSnapshot(
        JNIEnv *env, jobject obj, jstring name)
{
    (void)obj;
    return dispatch_snapshot(env, name, SNAP_LOAD);
}

JNIEXPORT void JNICALL
Java_Ali_Xanite_MainActivity_nativeRebootSystem(
        JNIEnv *env, jobject obj)
{
    (void)env;
    (void)obj;
    SDL_AtomicSet(&g_reboot_pending, 1);
}

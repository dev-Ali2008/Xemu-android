#include "qemu/osdep.h"
#include "xui/xemu-hud.h"

extern void xemu_android_process_snapshot_request(void);

void xemu_hud_init(SDL_Window *window, void *sdl_gl_context)
{
    (void)window;
    (void)sdl_gl_context;
}

void xemu_hud_cleanup(void)
{
}

void xemu_hud_render(void)
{
    xemu_android_process_snapshot_request();
}

void xemu_hud_process_sdl_events(SDL_Event *event)
{
    (void)event;
}

void xemu_hud_should_capture_kbd_mouse(int *kbd, int *mouse)
{
    if (kbd) {
        *kbd = 0;
    }
    if (mouse) {
        *mouse = 0;
    }
}

void xemu_hud_set_framebuffer_texture(GLuint tex, bool flip)
{
    (void)tex;
    (void)flip;
}

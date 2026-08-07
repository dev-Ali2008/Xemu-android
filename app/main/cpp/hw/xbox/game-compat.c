/*
 * Xbox title-specific compatibility quirk registry.
 * Based on the title-ID-gated HakuX compatibility layer.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/xbox/game-compat.h"
#include "xemu-xbe.h"

#ifdef __ANDROID__
#include <android/log.h>
#define COMPAT_LOG(fmt, ...) \
    __android_log_print(ANDROID_LOG_INFO, "Xanite-compat", fmt, ##__VA_ARGS__)
#else
#define COMPAT_LOG(fmt, ...) qemu_log("game-compat: " fmt "\n", ##__VA_ARGS__)
#endif

struct GameQuirks g_game_quirks;

static uint32_t active_title_id;
static int check_countdown;
#define CHECK_INTERVAL 120

static void apply_fable_quirks(struct GameQuirks *quirks)
{
    quirks->fable_scene_graph_cycle_breaker = true;
}

typedef void (*QuirkApplicator)(struct GameQuirks *quirks);

typedef struct GameQuirkEntry {
    uint32_t title_id;
    const char *name;
    QuirkApplicator apply;
} GameQuirkEntry;

static const GameQuirkEntry game_quirks[] = {
    { 0x4D530080, "Fable", apply_fable_quirks },
    { 0x4D53000D, "Fable", apply_fable_quirks },
    { 0x4D5300D1, "Fable: The Lost Chapters", apply_fable_quirks },
};

void game_compat_check(void)
{
    if (active_title_id != 0 || check_countdown-- > 0) {
        return;
    }
    check_countdown = CHECK_INTERVAL;

    struct xbe *xbe = xemu_get_xbe_info();
    if (!xbe || !xbe->cert || xbe->cert->m_titleid == 0) {
        return;
    }

    active_title_id = xbe->cert->m_titleid;
    for (size_t i = 0; i < ARRAY_SIZE(game_quirks); i++) {
        if (game_quirks[i].title_id == active_title_id) {
            game_quirks[i].apply(&g_game_quirks);
            COMPAT_LOG("Detected '%s' (0x%08X); targeted quirks enabled",
                       game_quirks[i].name, active_title_id);
            return;
        }
    }

    COMPAT_LOG("Detected title 0x%08X; no native quirks required",
               active_title_id);
}

void game_compat_reset(void)
{
    memset(&g_game_quirks, 0, sizeof(g_game_quirks));
    active_title_id = 0;
    check_countdown = 0;
}

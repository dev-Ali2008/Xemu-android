/*
 * Xbox title-specific compatibility quirks.
 *
 * Quirks default to disabled and are enabled only after matching the XBE
 * certificate title ID. Keep every workaround narrowly gated here so fixes
 * for one title cannot silently alter the rest of the library.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_XBOX_GAME_COMPAT_H
#define HW_XBOX_GAME_COMPAT_H

#include <stdbool.h>

struct GameQuirks {
    /* Fable/Fable TLC: break a circular scene-query list at the game's own
     * skip flag instead of allowing the guest to spin forever. */
    bool fable_scene_graph_cycle_breaker;
};

extern struct GameQuirks g_game_quirks;

void game_compat_check(void);
void game_compat_reset(void);

#endif

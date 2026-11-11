/* Touch Alpha 1. SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef TOUCH_SHOCK_BRIDGE_H
#define TOUCH_SHOCK_BRIDGE_H
#include <SDL.h>
#include "touch_controls.h"
#include "touch_target_types.h"

void touch_bridge_init(void);
void touch_bridge_sync(void);
/* Resolve touch USE after the original UI queue has dispatched it. */
void touch_bridge_after_input(void);
void touch_bridge_event(const SDL_Event *event);
void touch_bridge_controller(bool connected);
void touch_bridge_cancel(void);
void touch_bridge_merge_movement(void);
void touch_bridge_consume_look(int *dx, int *dy);
void touch_bridge_draw(void);

/* INPUT_LIB implements only mouse-queue mechanics; the bridge owns game
   context/aim/mode decisions. No gameplay actions are called from INPUT_LIB. */
void touch_input_pointer(float x, float y);
void touch_input_button(bool left, bool down);
/* A completed control tap bypasses native mouse double-click buffering. */
void touch_input_tap(float x, float y);
void touch_input_drag_button(bool down);
void touch_input_cancel(void);
void touch_input_action(TouchAction action, bool down);
void touch_input_cancel_actions(void);
/* Game-owned adapter: snapshots inventory and revalidates before selection. */
int touch_game_radial_snapshot(TouchRadialCategory category,
                               TouchRadialEntry entries[TOUCH_RADIAL_MAX_ENTRIES]);
bool touch_game_radial_select(TouchRadialCategory category, int id);
/* Game/UI region query in input.c, using the same UI coordinate conversion. */
TouchCursorTarget touch_cursor_target_at(int x, int y);
#endif

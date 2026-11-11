/* Touch Alpha 1. SPDX-License-Identifier: GPL-3.0-or-later */
#include "touch_shock_bridge.h"
#include "touch_surface.h"
#include "player.h"
#include "physics.h"
#include "mainloop.h"
#include "mouselook.h"
#include "input.h"
#include "touch_visual_compare.h"
#include <stddef.h>
#include <stdio.h>
#ifdef RP5NP_QA_TOOLS
#include "Prefs.h"
#include "horror_visuals.h"
#include "qa_tools.h"
static int qa_pending=-1;
static int compare_pending=-1;
#endif
#if defined(__ANDROID__) && !defined(TOUCH_HEADLESS)
#include <jni.h>
#include <SDL_system.h>
#endif

extern SDL_Window *window;
extern SDL_Renderer *renderer;
extern int mlook_enabled;
extern bool TriggerRelMouseMode;
extern uchar game_paused, wrapper_panel_on, olh_overlay_on;
extern void middleize_mouse(void);

static TouchControls touch;
static TouchMovieTap movie_tap;
static bool movie_tap_enabled;
static bool initialized, syncing, controller_connected, background, unfocused;
static bool requested_look;
/* A touch USE from LOOK may put a world object on the original game cursor.
   Only that confirmed acquisition may trigger a return after release. */
static bool touch_use_pending, pending_auto_return;
static int output_width, output_height, window_width, window_height;
static byte applied[2];
static bool applied_axis[2];
static TouchRect game_viewport;
static float game_scale_x, game_scale_y;
static int game_width, game_height;
static bool settings_loaded, future_settings;
static char settings_path[1024];

static int populate_radial(void *user, TouchRadialCategory category,
                           TouchRadialEntry entries[TOUCH_RADIAL_MAX_ENTRIES]) {
    (void)user;
    return touch_game_radial_snapshot(category,entries);
}
static void platform_haptic(int level) {
#if defined(__ANDROID__) && !defined(TOUCH_HEADLESS)
    JNIEnv *env=(JNIEnv *)SDL_AndroidGetJNIEnv();
    jobject activity=(jobject)SDL_AndroidGetActivity();
    if (!env || !activity) return;
    jclass cls=(*env)->GetObjectClass(env,activity);
    if (cls) {
        jmethodID method=(*env)->GetMethodID(env,cls,"touchHaptic","(II)V");
        if (method) (*env)->CallVoidMethod(env,activity,method,
                                          (jint)touch.settings.haptic_strength,(jint)level);
        if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
        (*env)->DeleteLocalRef(env,cls);
    }
    (*env)->DeleteLocalRef(env,activity);
#else
    (void)level;
#endif
}

static void load_settings(void) {
    if (settings_loaded) return;
#if defined(__ANDROID__) && !defined(TOUCH_HEADLESS)
    const char *dir=SDL_AndroidGetInternalStoragePath();
    if (!dir) return;
    int n=snprintf(settings_path,sizeof(settings_path),"%s/touch-settings.cfg",dir);
    if (n<0 || (size_t)n>=sizeof(settings_path)) { settings_path[0]=0; return; }
    TouchLoadResult result=touch_settings_load(&touch.settings,settings_path);
    future_settings=result==TOUCH_LOAD_FUTURE;
    SDL_Log("TOUCH_SETTINGS load=%d path=%s",result,settings_path);
    touch.saved_settings=touch.settings;
#endif
    settings_loaded=true;
}

#if defined(__ANDROID__) && defined(TOUCH_DEBUG)
/* Temporary Touch Alpha visibility diagnostics: log transitions, not frames. */
static bool diagnostic_state_valid;
static unsigned diagnostic_state;
static short diagnostic_loop;
static unsigned long diagnostic_draw_calls;

static void log_touch_state(void) {
    unsigned state = (touch.enabled ? 1u : 0u) |
        (touch.gameplay_available ? 2u : 0u) | (touch.controller ? 4u : 0u) |
        (touch.mode == TOUCH_GAMEPLAY ? 8u : 0u) |
        (touch.effective_mode == TOUCH_GAMEPLAY ? 16u : 0u) |
        (global_fullmap ? 32u : 0u) | (global_fullmap && global_fullmap->cyber ? 64u : 0u) |
        ((_change_flag & GL_CHG_LOOP) ? 128u : 0u) | (game_paused ? 256u : 0u) |
        (wrapper_panel_on ? 512u : 0u) | (olh_overlay_on ? 1024u : 0u) |
        (physics_running ? 2048u : 0u) | (time_passes ? 4096u : 0u);
    if (diagnostic_state_valid && state == diagnostic_state && _current_loop == diagnostic_loop) return;
    diagnostic_state_valid = true;
    diagnostic_state = state;
    diagnostic_loop = _current_loop;
    SDL_Log("RP5NP_TOUCH_STATE mode=%s effective=%s gameplay=%s controller_suppression=%s overlay=%s enabled=%d draw_calls=%lu",
            touch.mode == TOUCH_GAMEPLAY ? "GAMEPLAY" : "CURSOR",
            touch.effective_mode == TOUCH_GAMEPLAY ? "GAMEPLAY" : "CURSOR",
            touch.gameplay_available ? "available" : "unavailable",
            touch.controller ? "active" : "inactive",
            touch.enabled && touch.gameplay_available && !touch.controller ? "visible" : "hidden",
            touch.enabled, diagnostic_draw_calls);
    SDL_Log("RP5NP_TOUCH_CONTEXT loop=%d map=%d cyber=%d changing_loop=%d paused=%d wrapper=%d help=%d physics=%d time=%d background=%d unfocused=%d window_flags=0x%x",
            _current_loop, global_fullmap != NULL, global_fullmap ? global_fullmap->cyber : -1,
            !!(_change_flag & GL_CHG_LOOP), game_paused, wrapper_panel_on, olh_overlay_on,
            physics_running, time_passes, background, unfocused, SDL_GetWindowFlags(window));
}
#endif

static void clear_movement(void) {
    for (int i = 0; i < 2; ++i) {
        byte current;
        if (applied_axis[i]) {
            physics_get_one_control(KEYBD_CONTROL_BANK, i, &current);
            if (current == applied[i]) physics_set_one_control(KEYBD_CONTROL_BANK, i, 0);
            applied_axis[i] = false;
        }
    }
}

static bool gameplay_available(void) {
    return global_fullmap && !global_fullmap->cyber &&
        (_current_loop == GAME_LOOP || _current_loop == FULLSCREEN_LOOP || _current_loop == SVGA_LOOP) &&
        !(_change_flag & GL_CHG_LOOP) && !game_paused && !wrapper_panel_on && !olh_overlay_on &&
        physics_running && time_passes;
}

static void request_mode(bool look) {
    if (look && !mlook_enabled) mouse_look_toggle();
    else if (!look && mlook_enabled) {
        /* The normal toggle explicitly warps to the window center after SDL
           disables relative mode. That queued motion can overwrite a touch
           position. Direct touch needs only the mode change, never that warp.
           Physical mouse/controller transitions keep the original toggle. */
        mlook_enabled = false;
        SDL_SetRelativeMouseMode(SDL_FALSE);
    }
    if (!look) TriggerRelMouseMode = false;
    requested_look = look;
}

static void pointer(float x, float y) {
    float gx, gy;
    touch_map_cursor(x, y, game_viewport, game_scale_x, game_scale_y,
                     game_width, game_height, &gx, &gy);
    touch_input_pointer(gx, gy);
}

static void output(void *user, TouchOutput event, float x, float y) {
    (void)user;
    switch (event) {
    case TOUCH_QA_COMMAND:
#ifdef RP5NP_QA_TOOLS
        if (touch.qa_enabled && touch.panel==TOUCH_UI_QA && qa_pending<0)
            qa_pending=(int)x;
#endif
        break;
    case TOUCH_VISUAL_COMPARE:
#ifdef RP5NP_QA_TOOLS
        if (touch.compare_enabled && touch.gameplay_available &&
            touch.panel==TOUCH_UI_CLOSED && compare_pending<0)
            compare_pending=(int)x;
#endif
        break;
    case TOUCH_CANCEL:
#ifdef RP5NP_QA_TOOLS
        qa_pending=-1;
        compare_pending=-1;
#endif
        clear_movement();
        touch_input_cancel();
        touch_input_cancel_actions();
        break;
    case TOUCH_SETTINGS_SAVE:
        touch.save_failed=future_settings || !touch_settings_save(&touch.settings,settings_path);
        break;
    case TOUCH_UI_CHANGED:
        /* A touch modal ends any pending item auto-return, just like the
           original game's modal contexts. The Part O pickup path is unchanged. */
        touch_use_pending=pending_auto_return=false;
        request_mode(touch.panel==TOUCH_UI_CLOSED && touch.enabled &&
                     touch.gameplay_available && !touch.controller &&
                     touch.mode==TOUCH_GAMEPLAY && object_on_cursor==OBJ_NULL &&
                     input_cursor_mode!=INPUT_OBJECT_CURSOR);
        break;
    case TOUCH_ACTION_DOWN:
    case TOUCH_ACTION_UP:
        touch_input_action((TouchAction)(int)x, event == TOUCH_ACTION_DOWN);
        break;
    case TOUCH_RADIAL_SELECT:
        if (touch.enabled && touch.gameplay_available && !touch.controller &&
            touch.panel==TOUCH_UI_CLOSED &&
            (touch.effective_mode==TOUCH_GAMEPLAY || (int)x==TOUCH_RADIAL_AMMO))
            touch_game_radial_select((TouchRadialCategory)(int)x,(int)y);
        break;
    case TOUCH_HAPTIC:
        if (touch.enabled && touch.gameplay_available && !touch.controller &&
            !background && !unfocused && touch.panel==TOUCH_UI_CLOSED &&
            touch.settings.haptics_enabled) platform_haptic((int)x);
        break;
    case TOUCH_MODE_CHANGED:
        request_mode(touch.effective_mode == TOUCH_GAMEPLAY);
        break;
    case TOUCH_FIRE_DOWN:
        /* Establish the same aim point used by mouse_look_physics, even on
           the very first action before a physics tick has centered it. */
        middleize_mouse();
        touch_input_button(false, true);
        break;
    case TOUCH_FIRE_UP:
        touch_input_button(false, false);
        break;
    case TOUCH_USE_PRESS:
        if (touch.effective_mode == TOUCH_GAMEPLAY && mlook_enabled &&
            input_cursor_mode != INPUT_OBJECT_CURSOR && object_on_cursor == OBJ_NULL)
            touch_use_pending = true;
        middleize_mouse();
        touch_input_button(true, true);
        touch_input_button(true, false);
        touch_input_button(true, true);
        touch_input_button(true, false);
        break;
    case TOUCH_POINTER_DOWN:
        request_mode(false);
        pointer(x, y);
        touch_input_drag_button(true);
        break;
    case TOUCH_POINTER_MOVE:
        request_mode(false);
        pointer(x, y);
        break;
    case TOUCH_POINTER_UP:
        pointer(x, y);
        touch_input_drag_button(false);
        break;
    case TOUCH_POINTER_TAP: {
        float gx, gy;
        request_mode(false);
        touch_map_cursor(x, y, game_viewport, game_scale_x, game_scale_y,
                         game_width, game_height, &gx, &gy);
        touch_input_pointer(gx, gy);
        /* Capture the target before the first press changes a panel or loop. */
        TouchCursorTarget target = touch_cursor_target_at((int)gx, (int)gy);
        if (target==TOUCH_TARGET_UI_CONTROL || target==TOUCH_TARGET_NONE) {
            touch_input_tap(gx,gy);
            break;
        }
        touch_input_button(true, true);
        touch_input_button(true, false);
        if (target == TOUCH_TARGET_WORLD || target == TOUCH_TARGET_UI_OBJECT) {
            /* The original UI queue turns the second down into LDOUBLE. */
            touch_input_button(true, true);
            touch_input_button(true, false);
        }
        break;
    }
    }
}

void touch_bridge_init(void) {
    if (initialized) touch_cancel(&touch);
    requested_look = controller_connected = background = unfocused = false;
    touch_use_pending = pending_auto_return = false;
    output_width = output_height = window_width = window_height = 0;
    /* Must precede SDL_Init: gameplay touches must never enter SDL's mouse
       state/relative accumulator. Physical mice still follow the old path. */
    SDL_SetHintWithPriority(SDL_HINT_TOUCH_MOUSE_EVENTS, "0", SDL_HINT_OVERRIDE);
    SDL_SetHintWithPriority(SDL_HINT_MOUSE_TOUCH_EVENTS, "0", SDL_HINT_OVERRIDE);
    touch_init(&touch, output, NULL);
    touch.radial_populate=populate_radial;
    settings_loaded=future_settings=false;
    settings_path[0]=0;
    touch_movie_tap_reset(&movie_tap);
    movie_tap_enabled = false;
    initialized = true;
#if defined(__ANDROID__) && defined(TOUCH_DEBUG)
    diagnostic_state_valid = false;
    diagnostic_draw_calls = 0;
    SDL_Log("RP5NP_TOUCH_INIT initialized=1 mode=GAMEPLAY effective=CURSOR");
#endif
}

void touch_bridge_cancel(void) {
    if (initialized) touch_cancel(&touch);
    touch_movie_tap_reset(&movie_tap);
    touch_use_pending = pending_auto_return = false;
}

void touch_bridge_after_input(void) {
#ifdef RP5NP_QA_TOOLS
    if (compare_pending>=0) {
        int button=compare_pending;
        compare_pending=-1;
        if (gameplay_available() && touch.compare_enabled &&
            touch.panel==TOUCH_UI_CLOSED && !controller_connected) {
            if (button==0)
                horror_visuals_set_mode(touch_compare_next_mode(gShockPrefs.doHorrorVisuals));
            else if (button==1) {
                TouchPostChoice next=touch_compare_next_post(player_struct.level,
                    gShockPrefs.postEnabled,gShockPrefs.postChoice);
                gShockPrefs.postEnabled=next.enabled;
                gShockPrefs.postChoice=next.choice;
                SavePrefs();
            }
            touch.compare_notice=button;
            touch.compare_notice_until=SDL_GetTicks()+1600;
        }
    }
    /* Run only after uiPoll has unwound. Level loads must not re-enter the
       SDL event dispatcher or execute while the renderer is drawing. */
    if (qa_pending>=0) {
        int command=qa_pending;
        qa_pending=-1;
        if (qa_tools_enabled() && gameplay_available() && touch.panel==TOUCH_UI_QA) {
            if (command==TU_QA_GOD) qa_tools_toggle_god();
            else if (command==TU_QA_HEAL) qa_tools_heal();
            else if (command==TU_QA_LOADOUT) qa_tools_full_loadout();
            else if (command>=TU_QA_DECK_FIRST && command<=TU_QA_DECK_LAST) {
                int deck=command-TU_QA_DECK_FIRST;
                touch_bridge_cancel();
                qa_tools_teleport(deck);
            }
        }
    }
#endif
    if (touch_use_pending) {
        pending_auto_return = touch.enabled && touch.gameplay_available &&
            !controller_connected && input_cursor_mode == INPUT_OBJECT_CURSOR &&
            object_on_cursor != OBJ_NULL;
        touch_use_pending = false;
    }
    touch_bridge_sync();
}

void touch_bridge_sync(void) {
    if (!initialized || syncing || !renderer || !window) return;
    syncing = true;
    load_settings();
    touch_surface_init();
    int w = 0, h = 0, ow = 0, oh = 0, ww = 0, wh = 0;
    SDL_RenderGetLogicalSize(renderer, &w, &h);
    SDL_GetRendererOutputSize(renderer, &ow, &oh);
    SDL_GetWindowSize(window, &ww, &wh);
    /* SDL exposes the exact (possibly rounded) viewport origin through this
       query. Convert it back to output pixels, then use one explicit transform
       for every pointer event. Do not approximate a centered 4:3 rectangle. */
    float origin_x, origin_y, sx, sy;
    SDL_RenderGetScale(renderer, &sx, &sy);
    SDL_RenderWindowToLogical(renderer, 0, 0, &origin_x, &origin_y);
    TouchRect viewport = {-origin_x * sx, -origin_y * sy, w * sx, h * sy};
    if (w != game_width || h != game_height || sx != game_scale_x || sy != game_scale_y ||
        viewport.x != game_viewport.x || viewport.y != game_viewport.y) touch_bridge_cancel();
    game_width = w; game_height = h; game_scale_x = sx; game_scale_y = sy;
    game_viewport = viewport;
    if (ow != output_width || oh != output_height || ww != window_width || wh != window_height) {
        touch_bridge_cancel();
        output_width = ow; output_height = oh; window_width = ww; window_height = wh;
    }
    bool available = gameplay_available();
#ifdef RP5NP_QA_TOOLS
    touch.compare_enabled=true; /* QA application only; normal release omits this branch. */
    touch.compare_mode=gShockPrefs.doHorrorVisuals;
    touch.compare_post_enabled=gShockPrefs.postEnabled;
    touch.compare_post_choice=gShockPrefs.postChoice;
    touch.compare_level=player_struct.level;
    bool qa_enabled=qa_tools_enabled();
    if (!qa_enabled && touch.panel==TOUCH_UI_QA) touch_bridge_cancel();
    touch.qa_enabled=qa_enabled;
    touch.qa_god=qa_tools_god();
    if (touch.panel==TOUCH_UI_QA) touch.qa_status=qa_tools_status();
#endif
    Uint32 flags = SDL_GetWindowFlags(window);
    bool enabled = !background && !unfocused && (flags & SDL_WINDOW_INPUT_FOCUS) &&
                   !(flags & (SDL_WINDOW_HIDDEN | SDL_WINDOW_MINIMIZED));
    enabled = enabled && w > 0 && h > 0 && sx > 0 && sy > 0;
    if (!available || !enabled || controller_connected || touch.panel!=TOUCH_UI_CLOSED)
        touch_use_pending = pending_auto_return = false;
    if (pending_auto_return && input_cursor_mode != INPUT_OBJECT_CURSOR &&
        object_on_cursor == OBJ_NULL) {
        pending_auto_return = false;
        if (touch.mode == TOUCH_GAMEPLAY) request_mode(true);
        else touch_set_mode(&touch, TOUCH_GAMEPLAY);
    }
    /* Original game interactions may deliberately leave freelook. Respect
       that transition and require a fresh toggle before gameplay gestures. */
    if (requested_look && !mlook_enabled && available && !controller_connected) {
        requested_look = false;
        touch_set_mode(&touch, TOUCH_CURSOR);
    }
    bool movie = enabled && _current_loop == CUTSCENE_LOOP && !(_change_flag & GL_CHG_LOOP);
    if (movie != movie_tap_enabled) touch_movie_tap_reset(&movie_tap);
    movie_tap_enabled = movie;
    touch_configure(&touch, (float)ow, (float)oh, enabled, available, controller_connected);
    touch_tick(&touch,SDL_GetTicks());
    if (touch.radial.open) {
        TouchRadialEntry fresh[TOUCH_RADIAL_MAX_ENTRIES]={{0}};
        int count=touch_game_radial_snapshot(touch.radial.category,fresh);
        touch_radial_refresh(&touch.radial,fresh,count);
    }
    touch.pause_available = global_fullmap && !global_fullmap->cyber &&
        (_current_loop == GAME_LOOP || _current_loop == FULLSCREEN_LOOP || _current_loop == SVGA_LOOP) &&
        !(_change_flag & GL_CHG_LOOP) && !wrapper_panel_on && !olh_overlay_on &&
        !controller_connected;
    /* Preserve Alpha 1 look sensitivity: surface deltas become the same
       logical pixel deltas as before, without clamping motion in the bars. */
    touch.look_scale = sx > 0 ? 1.0f / sx : 1.0f;
    if (touch.stick_reference_height!=h*sy) {
        touch.stick_reference_height=h*sy;
        touch_layout_refresh(&touch);
    }
    if (!available || controller_connected) requested_look = false;
#if defined(__ANDROID__) && defined(TOUCH_DEBUG)
    log_touch_state();
#endif
    syncing = false;
}

void touch_bridge_controller(bool connected) {
    touch_bridge_cancel(); /* includes replacement controllers with no gap */
    controller_connected = connected;
    touch_bridge_sync();
}

void touch_bridge_event(const SDL_Event *event) {
    TouchSurfaceEvent surface;
    if (touch_surface_failed()) {
        /* Lost releases must never leave a button or movement held. Drain the
           remaining raw batch as well; only new input can acquire ownership. */
        touch_bridge_cancel();
        if (touch_surface_read(event, &surface)) return;
    }
    if (touch_surface_read(event, &surface)) {
        touch_bridge_sync();
        if (surface.phase == TOUCH_ABORT || surface.width != output_width || surface.height != output_height) {
            touch_bridge_cancel();
            return;
        }
        if (movie_tap_enabled) {
            /* The cutscene's existing key handler skips on Escape down. Send
               one ordered key pair after a completed raw-surface tap. */
            if (touch_movie_tap_event(&movie_tap, surface.phase, surface.device,
                                      surface.finger, surface.x, surface.y,
                                      surface.sample_ms, (float)output_height*(8.0f/768.0f))) {
#if defined(__ANDROID__) && !defined(TOUCH_HEADLESS)
                SDL_Log("RP5NP_CUTSCENE_TOUCH_SKIP tap=1");
#endif
                touch_input_action(TOUCH_PAUSE, true);
                touch_input_action(TOUCH_PAUSE, false);
            }
            return;
        }
        if (surface.phase == TOUCH_DOWN && touch.enabled && touch.effective_mode == TOUCH_GAMEPLAY &&
            touch.panel==TOUCH_UI_CLOSED &&
            touch_compare_hit(&touch,surface.x,surface.y)<0)
            request_mode(true);
        TouchMode before_mode = touch.mode;
        touch_finger(&touch, surface.phase, surface.device, surface.finger,
                     surface.x, surface.y, SDL_GetTicks());
        if (touch.mode != before_mode)
            touch_use_pending = pending_auto_return = false;
        if (touch.move_owner < 0) clear_movement();
        return;
    }
    switch (event->type) {
    case SDL_APP_WILLENTERBACKGROUND:
    case SDL_APP_DIDENTERBACKGROUND:
    case SDL_APP_TERMINATING:
        background = true; touch_bridge_cancel(); break;
    case SDL_APP_WILLENTERFOREGROUND:
    case SDL_APP_DIDENTERFOREGROUND:
        background = event->type != SDL_APP_DIDENTERFOREGROUND;
        touch_bridge_cancel(); break;
    case SDL_DISPLAYEVENT:
        if (event->display.event == SDL_DISPLAYEVENT_ORIENTATION) touch_bridge_cancel();
        break;
    case SDL_WINDOWEVENT:
        switch (event->window.event) {
        case SDL_WINDOWEVENT_FOCUS_LOST:
            unfocused = true; touch_bridge_cancel(); break;
        case SDL_WINDOWEVENT_FOCUS_GAINED:
            unfocused = false; touch_bridge_cancel(); break;
        case SDL_WINDOWEVENT_SIZE_CHANGED:
        case SDL_WINDOWEVENT_RESIZED:
        case SDL_WINDOWEVENT_DISPLAY_CHANGED:
        case SDL_WINDOWEVENT_MINIMIZED:
        case SDL_WINDOWEVENT_HIDDEN:
            touch_bridge_cancel(); break;
        }
        break;
    /* SDL_FINGER events are intentionally ignored: their x/y can already be
       clamped by the renderer. Android supplies the raw surface events above. */
    }
    touch_bridge_sync();
}

void touch_bridge_merge_movement(void) {
    /* process_motion_keys has just replaced this bank. Forget last frame's
       contributions before context cancellation can clear any physical keys. */
    applied_axis[0] = applied_axis[1] = false;
    touch_bridge_sync();
    if (!touch.enabled || touch.effective_mode != TOUCH_GAMEPLAY) return;
    byte bx, by;
    physics_get_one_control(KEYBD_CONTROL_BANK, CONTROL_XVEL, &bx);
    physics_get_one_control(KEYBD_CONTROL_BANK, CONTROL_YVEL, &by);
    int x = bx, y = by;
    touch_merge_axes(&touch, CONTROL_MAX_VAL / 2, &x, &y);
    if (!bx && x) {
        applied_axis[0] = true; applied[0] = (byte)x;
        physics_set_one_control(KEYBD_CONTROL_BANK, CONTROL_XVEL, (byte)x);
    }
    if (!by && y) {
        applied_axis[1] = true; applied[1] = (byte)y;
        physics_set_one_control(KEYBD_CONTROL_BANK, CONTROL_YVEL, (byte)y);
    }
}

void touch_bridge_consume_look(int *dx, int *dy) {
    touch_bridge_sync();
    *dx = *dy = 0;
    if (!touch.enabled || touch.effective_mode != TOUCH_GAMEPLAY || !mlook_enabled) return;
    touch_consume_look(&touch, dx, dy);
}

void touch_bridge_draw(void) {
#if defined(__ANDROID__) && defined(TOUCH_DEBUG)
    if (++diagnostic_draw_calls == 1)
        SDL_Log("RP5NP_TOUCH_DRAW_CALL count=1 initialized=%d renderer=%d", initialized, renderer != NULL);
#endif
    touch_bridge_sync();
#if defined(__ANDROID__) && defined(TOUCH_DEBUG)
    if (renderer) {
        static bool valid;
        static SDL_Rect last_viewport;
        static int last_w, last_h, last_ow, last_oh;
        SDL_Rect viewport;
        int w, h;
        SDL_RenderGetLogicalSize(renderer, &w, &h);
        SDL_RenderGetViewport(renderer, &viewport);
        if (!valid || w != last_w || h != last_h || output_width != last_ow || output_height != last_oh ||
            viewport.x != last_viewport.x || viewport.y != last_viewport.y ||
            viewport.w != last_viewport.w || viewport.h != last_viewport.h) {
            SDL_Log("RP5NP_TOUCH_RENDERER logical=%dx%d viewport=%d,%d,%d,%d output=%dx%d window=%dx%d",
                    w, h, viewport.x, viewport.y, viewport.w, viewport.h,
                    output_width, output_height, window_width, window_height);
            valid = true; last_w = w; last_h = h;
            last_ow = output_width; last_oh = output_height; last_viewport = viewport;
        }
    }
#endif
    if (renderer) touch_draw(&touch, renderer);
}

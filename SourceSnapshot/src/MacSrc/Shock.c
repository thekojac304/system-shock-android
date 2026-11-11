#include "GpuWorld.h"
/*

Copyright (C) 2015-2018 Night Dive Studios, LLC.

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.

*/
//====================================================================================
//
//		System Shock - ©1994-1995 Looking Glass Technologies, Inc.
//
//		Shock.c	-	Mac-specific initialization and main event loop.
//
//====================================================================================

//--------------------
//  Includes
//--------------------
#include <math.h>
#include <SDL.h>
#ifdef RP5NP_GPU_WORLD
#include <EGL/egl.h>
#endif
#ifdef __ANDROID__
#include <SDL_system.h>
#include "touch_shock_bridge.h"
#include <unistd.h>
#endif
#include "InitMac.h"
#include "Modding.h"
#include "OpenGL.h"
#include "Prefs.h"
#include "Shock.h"
#include "ShockBitmap.h"

#include "amaploop.h"
#include "gr2ss.h"
#include "hkeyfunc.h"
#include "mainloop.h"
#include "setup.h"
#include "shockolate_version.h"
#include "status.h"
#include "version.h"
#if defined(RP5NP_QA_TOOLS) && defined(RP5NP_GPU_WORLD)
#include <stdio.h>
#include <string.h>
#include "palfx.h"
#endif

//--------------------
//  Globals
//--------------------
bool gPlayingGame;

grs_screen *cit_screen;
SDL_Window *window;
SDL_Palette *sdlPalette;
SDL_Renderer *renderer;

#ifdef RP5NP_GPU_WORLD
/* SDL's Android event pump replaces a lost EGL context, but its GLES renderer
 * still owns the original context and object names. Retire that whole owner.
 * All of this runs on the render thread, outside SDL event watchers. */
static SDL_GLContext presenter_context, reset_context, cleanup_context;
static int presenter_reset_pending, qa_invalidate_context;
static Uint32 presenter_retry_at;

int SDLPresenterReady(void) {
    if(renderer && !presenter_reset_pending && presenter_context!=SDL_GL_GetCurrentContext())
        SDLRequestPresenterReset();
    return renderer != NULL && !presenter_reset_pending;
}

void SDLRequestPresenterReset(void) {
    if(presenter_reset_pending)return;
    reset_context=SDL_GL_GetCurrentContext();
    presenter_reset_pending=1;presenter_retry_at=0;
    /* Restore the indexed world before dropping its GPU composition metadata.
     * SDL still owns its target texture until old-renderer retirement below. */
    gpu_world_presenter_lost();
    SDL_Log("GPU2 presenter reset requested original=%p current=%p controlled=%d; Original selected",
            presenter_context,reset_context,qa_invalidate_context);
}

static int presenter_detach(void) {
    /* SDL_EGL_MakeCurrent's null branch does not propagate EGL's result.
     * Check the actual EGL binding as well as SDL's thread-local bookkeeping. */
    return SDL_GL_MakeCurrent(window,NULL)==0 &&
           eglGetCurrentContext()==EGL_NO_CONTEXT &&
           SDL_GL_GetCurrentContext()==NULL;
}

static SDL_GLContext presenter_unshared_context(void) {
    int share=0;
    if(SDL_GL_GetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT,&share)<0)return NULL;
    if(SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT,0)<0)return NULL;
    SDL_GLContext created=SDL_GL_CreateContext(window);
    SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT,share);
    return created;
}

static int presenter_recover(void) {
    if(!presenter_reset_pending)return renderer!=NULL;
    Uint32 now=SDL_GetTicks();
    if(presenter_retry_at && (Sint32)(now-presenter_retry_at)<0)return 0;
    presenter_retry_at=now+1000;
    if(renderer){
        if(!presenter_detach()){
            SDL_Log("GPU2 presenter reset deferred: EGL detach failed; old owner retained");
            return 0;
        }
        /* SDL's GLES destructor ignores a failed activation of its old context.
         * Give it an empty, unshared namespace for that case. Do not rely on
         * calling GL without a context, or delete old names in the new presenter.
         * The old context may still activate: its own deletes are then valid. */
        cleanup_context=presenter_unshared_context();
        if(!cleanup_context){
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,2);
            cleanup_context=presenter_unshared_context();
        }
        if(!cleanup_context){
            SDL_Log("GPU2 presenter reset deferred: cleanup context unavailable: %s",SDL_GetError());
            return 0;
        }
        if(qa_invalidate_context){
            /* The old context stayed alive while both distinct temporary
             * contexts were allocated, preventing handle reuse in this test.
             * No context allocation occurs before SDL retires the old owner. */
            SDL_GL_DeleteContext(presenter_context);
            SDL_Log("GPU2 QA controlled context replacement: original invalidated before SDL teardown; not genuine EGL loss");
        }
        SDL_GLContext retired=presenter_context;
        SDL_DestroyRenderer(renderer);renderer=NULL;presenter_context=NULL;
        /* If a truly lost native handle was reused for the empty cleanup
         * context, the SDL owner just destroyed that context too. */
        if(cleanup_context==retired)cleanup_context=NULL;
        if(reset_context==retired)reset_context=NULL;
        qa_invalidate_context=0;
        SDL_Log("GPU2 presenter old SDL owner retired");
    }
    if(!presenter_detach()){
        SDL_Log("GPU2 presenter reset deferred: temporary-context detach failed");
        return 0;
    }
    if(reset_context==cleanup_context)reset_context=NULL;
    if(cleanup_context){SDL_GL_DeleteContext(cleanup_context);cleanup_context=NULL;}
    if(reset_context){SDL_GL_DeleteContext(reset_context);reset_context=NULL;}
    renderer=SDL_CreateRenderer(window,-1,SDL_RENDERER_ACCELERATED|SDL_RENDERER_PRESENTVSYNC);
    if(!renderer){
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,2);
        renderer=SDL_CreateRenderer(window,-1,SDL_RENDERER_ACCELERATED|SDL_RENDERER_PRESENTVSYNC);
    }
    if(!renderer){
        SDL_Log("GPU2 presenter reset deferred: renderer creation failed: %s",SDL_GetError());
        return 0;
    }
    presenter_context=SDL_GL_GetCurrentContext();
    int logical_w=drawSurface?drawSurface->w:grd_cap->w;
    int logical_h=drawSurface?drawSurface->h:grd_cap->h;
    if(SDL_RenderSetLogicalSize(renderer,logical_w,logical_h)<0){
        SDL_Log("GPU2 presenter reset deferred: logical size failed: %s",SDL_GetError());
        SDL_DestroyRenderer(renderer);renderer=NULL;presenter_context=NULL;
        return 0;
    }
    presenter_reset_pending=0;presenter_retry_at=0;
    gShockPrefs.doHorrorVisuals=0;
    SDL_Log("GPU2 presenter recreation ready context=%p logical=%dx%d mode=Original; GPU requires explicit retry",
            presenter_context,logical_w,logical_h);
    return 1;
}

void SDLInjectPresenterReset(void) {
    if(!SDLPresenterReady() || !presenter_context)return;
    if(SDL_RenderFlush(renderer)<0 || SDL_GL_GetCurrentContext()!=presenter_context){
        SDL_Log("GPU2 QA context replacement skipped: original presenter not current");
        return;
    }
    SDL_GLContext replacement=presenter_unshared_context();
    if(!replacement){
        SDL_GL_MakeCurrent(window,presenter_context);
        SDL_Log("GPU2 QA context replacement failed: %s",SDL_GetError());
        return;
    }
    /* Keep the original alive until the cleanup context also exists. */
    qa_invalidate_context=1;
    SDL_Log("GPU2 QA controlled replacement allocated original=%p replacement=%p",
            presenter_context,replacement);
    SDL_Event event;SDL_zero(event);event.type=SDL_RENDER_DEVICE_RESET;
    gpu_world_event(&event);
}
#endif

SDL_AudioDeviceID device;

int num_args;
char **arg_values;

extern grs_screen *svga_screen;
extern frc *svga_render_context;

//--------------------
//  Prototypes
//--------------------
extern void init_all(void);
extern void inv_change_fullscreen(uchar on);
extern void object_data_flush(void);
extern errtype load_da_palette(void);

// see Prefs.c
extern void CreateDefaultKeybindsFile(void);
extern void LoadHotkeyKeybinds(void);
extern void LoadMoveKeybinds(void);

//------------------------------------------------------------------------------------
//		Main function.
//------------------------------------------------------------------------------------
int main(int argc, char **argv) {
#ifdef __ANDROID__
    const char *rp5np_storage = SDL_AndroidGetInternalStoragePath();
    if (rp5np_storage == NULL || chdir(rp5np_storage) != 0) {
        SDL_Log("RP5NP_NATIVE_MAIN_STORAGE_FAIL");
        return 73;
    }
    SDL_Log("RP5NP_NATIVE_MAIN_ENTRY storage=%s", rp5np_storage);
#endif

    // Save the arguments for later

    num_args = argc;
    arg_values = argv;

    // FIXME externalize this
    log_set_quiet(0);
    log_set_level(LOG_INFO);

    INFO("Logger initialized");

    // init mac managers

    InitMac();

    // Initialize the preferences file.

    SetDefaultPrefs();
    LoadPrefs();

    // see Prefs.c
    CreateDefaultKeybindsFile(); // only if it doesn't already exist
    // even if keybinds file still doesn't exist, defaults will be set here
    LoadHotkeyKeybinds();
    LoadMoveKeybinds();

    // Process some startup arguments

    bool show_splash = !CheckArgument("-nosplash");

    // CC: Modding support! This is so exciting.

    ProcessModArgs(argc, argv);

    // Initialize

    init_all();
    setup_init();

    gPlayingGame = true;

    load_da_palette();
    gr_clear(0xFF);

    // Draw the splash screen

    INFO("Showing splash screen");
    splash_draw(show_splash);

    // Start in the Main Menu loop

    _new_mode = _current_loop = SETUP_LOOP;
    loopmode_enter(SETUP_LOOP);

    // Start the main loop

    INFO("Showing main menu, starting game loop");
    mainloop(argc, argv);

    status_bio_end();
    stop_music();

    return 0;
}

bool CheckArgument(char *arg) {
    if (arg == NULL)
        return false;

    for (int i = 1; i < num_args; i++) {
        if (strcmp(arg_values[i], arg) == 0) {
            return true;
        }
    }

    return false;
}

void InitSDL() {
#ifdef __ANDROID__
    touch_bridge_init();
    SDL_Log("RP5NP_NATIVE_INITSDL_BEGIN");
#endif
    SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");
#ifdef __ANDROID__
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "opengles2");
    SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft");
    SDL_Log("RP5NP_ORIENTATION_HINT=LandscapeLeft");
    SDL_SetHint(SDL_HINT_ANDROID_BLOCK_ON_PAUSE, "1");
    SDL_SetHint(SDL_HINT_RENDER_LOGICAL_SIZE_MODE, "letterbox");
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
    const Uint32 rp5np_sdl_init_flags =
        SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_AUDIO | SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER;
#else
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "opengl");
    const Uint32 rp5np_sdl_init_flags = SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_AUDIO;
#endif
    if (SDL_Init(rp5np_sdl_init_flags) < 0) {
#ifdef __ANDROID__
        SDL_Log("RP5NP_NATIVE_SDL_INIT_FAIL error=%s", SDL_GetError());
#endif
        DEBUG("%s: Init failed", __FUNCTION__);
    } else {
#ifdef __ANDROID__
        SDL_Log("RP5NP_NATIVE_SDL_INIT_OK video=%s", SDL_GetCurrentVideoDriver());
#endif
    }

#ifndef __ANDROID__
    // Desktop GL attributes are irrelevant to the Android SDL_Renderer/GLES path.
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_BUFFER_SIZE, 32);
#endif

    gr_init();

    extern short svga_mode_data[];
#ifdef __ANDROID__
    extern short mode_id;
    gShockPrefs.doVideoMode = 4;
    gShockPrefs.doResolution = 0;
    mode_id = 4;
    SDL_Log("RP5NP_ANDROID_VIDEO_MODE=4 internal=1024x768");
#endif
    gr_set_mode(svga_mode_data[gShockPrefs.doVideoMode], TRUE);

    INFO("Setting up screen and render contexts");

    // Create a canvas to draw to

    SetupOffscreenBitmaps(grd_cap->w, grd_cap->h);

    // Open our window!
    char window_title[128];
    sprintf(window_title, "System Shock - %s", SHOCKOLATE_VERSION);

#ifdef __ANDROID__
#ifdef RP5NP_GPU_WORLD
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,0);
#endif
    // Android owns fullscreen/immersive policy. Let SDL bind to the one native Surface.
    window = SDL_CreateWindow(window_title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, grd_cap->w, grd_cap->h,
                              SDL_WINDOW_ALLOW_HIGHDPI
#ifdef RP5NP_GPU_WORLD
                              | SDL_WINDOW_OPENGL
#endif
                              );
#ifdef RP5NP_GPU_WORLD
    if (!window) {
        SDL_Log("GPU1B ES3 window failed: %s; retry software presenter ES2",SDL_GetError());
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,2);
        window=SDL_CreateWindow(window_title,SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,grd_cap->w,grd_cap->h,SDL_WINDOW_ALLOW_HIGHDPI);
    }
#endif
    if (window == NULL) {
        SDL_Log("RP5NP_NATIVE_WINDOW_FAIL error=%s", SDL_GetError());
    } else {
        int rp5np_w = 0, rp5np_h = 0;
        SDL_GetWindowSize(window, &rp5np_w, &rp5np_h);
        SDL_Log("RP5NP_NATIVE_WINDOW_OK size=%dx%d flags=0x%x", rp5np_w, rp5np_h, SDL_GetWindowFlags(window));
    }
#else
    window = SDL_CreateWindow(window_title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, grd_cap->w, grd_cap->h,
                              SDL_WINDOW_HIDDEN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_OPENGL);
#endif

    // Create the palette

    sdlPalette = SDL_AllocPalette(256);

    // Setup the screen

    svga_screen = cit_screen = gr_alloc_screen(grd_cap->w, grd_cap->h);
    gr_set_screen(svga_screen);

    gr_alloc_ipal();

    SDL_ShowCursor(SDL_DISABLE);

    atexit(SDL_Quit);

    SDL_RaiseWindow(window);

#ifdef __ANDROID__
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
#ifdef RP5NP_GPU_WORLD
    if (!renderer && window) {
        SDL_Log("GPU1B ES3 presenter failed: %s; retry software presenter ES2", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
        window = SDL_CreateWindow(window_title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                  grd_cap->w, grd_cap->h, SDL_WINDOW_ALLOW_HIGHDPI);
        if (window) renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
        gShockPrefs.doHorrorVisuals = 0;
    }
#endif
#else
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_PRESENTVSYNC);
#endif
    if (renderer == NULL) {
#ifdef __ANDROID__
        SDL_Log("RP5NP_NATIVE_RENDERER_FAIL error=%s", SDL_GetError());
#endif
        return;
    }
#ifdef RP5NP_GPU_WORLD
    presenter_context=SDL_GL_GetCurrentContext();
#endif

#ifdef __ANDROID__
    SDL_RendererInfo rp5np_renderer_info;
    SDL_zero(rp5np_renderer_info);
    if (SDL_GetRendererInfo(renderer, &rp5np_renderer_info) == 0) {
        SDL_Log("RP5NP_NATIVE_RENDERER_OK name=%s flags=0x%x textures=%u",
                rp5np_renderer_info.name ? rp5np_renderer_info.name : "(null)",
                rp5np_renderer_info.flags,
                rp5np_renderer_info.num_texture_formats);
    }

    // Presentation probe. If this is visible, Android Surface + SDL GLES presentation
    // are proven independently of Shockolate's game framebuffer.
    int rp5np_output_w = 0, rp5np_output_h = 0;
    SDL_GetRendererOutputSize(renderer, &rp5np_output_w, &rp5np_output_h);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    SDL_RenderPresent(renderer);
    SDL_Log("RP5NP_NATIVE_PRESENTATION_PRIMED output=%dx%d", rp5np_output_w, rp5np_output_h);
#endif
    if (SDL_RenderSetLogicalSize(renderer, grd_cap->w, grd_cap->h) < 0) {
#ifdef __ANDROID__
        SDL_Log("RP5NP_LOGICAL_SIZE_FAIL error=%s", SDL_GetError());
#endif
    } else {
#ifdef __ANDROID__
        SDL_Log("RP5NP_LOGICAL_SIZE_OK logical=%dx%d", grd_cap->w, grd_cap->h);
#endif
    }

    // Startup OpenGL

    init_opengl();

    SDLDraw();

#ifdef __ANDROID__
    SDL_Log("RP5NP_NATIVE_FIRST_GAME_FRAME_SUBMITTED");
#endif

    SDL_ShowWindow(window);
#ifdef __ANDROID__
    SDL_Log("RP5NP_NATIVE_INITSDL_END");
#endif
}

SDL_Color gamePalette[256];
bool UseCutscenePalette = FALSE; // see cutsloop.c
void SetSDLPalette(int index, int count, uchar *pal) {
    gpu_world_palette(index,count,pal);
    static bool gammalut_init = 0;
    static uchar gammalut[100 - 10 + 1][256];
    if (!gammalut_init) {
        double factor = (use_opengl() ? 1.0 : 2.2); // OpenGL uses 2.2
        int i, j;
        for (i = 10; i <= 100; i++) {
            double gamma = (double)i * 1.0 / 100;
            gamma = 1 - gamma;
            gamma *= gamma;
            gamma = 1 - gamma;
            gamma = 1 / (gamma * factor);
            for (j = 0; j < 256; j++)
                gammalut[i - 10][j] = (uchar)(pow((double)j / 255, gamma) * 255);
        }
        gammalut_init = 1;
        INFO("Gamma LUT init\'ed");
    }

    int gam = gShockPrefs.doGamma;
    if (gam < 10)
        gam = 10;
    if (gam > 100)
        gam = 100;
    gam -= 10;

    for (int i = index; i < index + count; i++) {
        gamePalette[i].r = gammalut[gam][*pal++];
        gamePalette[i].g = gammalut[gam][*pal++];
        gamePalette[i].b = gammalut[gam][*pal++];
        gamePalette[i].a = 0xff;
    }

    if (!UseCutscenePalette) {
        // Hack black!
        gamePalette[255].r = 0x0;
        gamePalette[255].g = 0x0;
        gamePalette[255].b = 0x0;
        gamePalette[255].a = 0xff;
    }

    SDL_SetPaletteColors(sdlPalette, gamePalette, 0, 256);
    SDL_SetSurfacePalette(drawSurface, sdlPalette);
    SDL_SetSurfacePalette(offscreenDrawSurface, sdlPalette);

    if (should_opengl_swap())
        opengl_change_palette();
}

#if defined(RP5NP_QA_TOOLS) && defined(RP5NP_GPU_WORLD)
/* Controlled original palette fade on an already presented GPU world. The
 * marker is sampled once on the first eligible bright normal-game frame. */
static void qa_gpu_palette_fade(int gpu_composed) {
    static int checked, running, last_composed;
    uchar saved[768], observed[768];
    unsigned brightest=0;
    if(running){last_composed=gpu_composed;return;}
    if(checked||!gpu_composed||UseCutscenePalette||
       (_current_loop!=GAME_LOOP&&_current_loop!=FULLSCREEN_LOOP))return;
    gr_get_pal(0,256,saved);
    for(size_t i=0;i<sizeof(saved);i++)if(saved[i]>brightest)brightest=saved[i];
    if(!brightest)return;
    checked=1;
    FILE *marker=fopen("gpu-render4-test-fade.txt","r");
    if(!marker)return;
    fclose(marker);
    /* finish_pal_effect calls SDLDraw recursively; arm before the original API. */
    running=1;last_composed=gpu_composed;
    SDL_Log("GPU4 QA FADE begin gpu=%d palette_max=%u",gpu_composed,brightest);
    palfx_fade_down();
    last_composed=0;
    SDLDraw();
    gr_get_pal(0,256,observed);brightest=0;
    for(size_t i=0;i<sizeof(observed);i++)if(observed[i]>brightest)brightest=observed[i];
    SDL_Log("GPU4 QA FADE legacy_end gpu=%d palette_max=%u hold_ms=500",last_composed,brightest);
    SDL_Delay(500);
    /* Separately test exact black; do not change the original fade endpoint. */
    memset(observed,0,sizeof(observed));
    gr_set_pal(0,256,observed);
    last_composed=0;
    SDLDraw();
    gr_get_pal(0,256,observed);brightest=0;
    for(size_t i=0;i<sizeof(observed);i++)if(observed[i]>brightest)brightest=observed[i];
    SDL_Log("GPU4 QA FADE exact_black gpu=%d palette_max=%u hold_ms=500",last_composed,brightest);
    SDL_Delay(500);
    gr_set_pal(0,256,saved);
    last_composed=0;
    SDLDraw();
    gr_get_pal(0,256,observed);
    SDL_Log("GPU4 QA FADE end gpu=%d palette_restored=%d",last_composed,memcmp(saved,observed,sizeof(saved))==0);
    running=0;
}
#endif

void SDLDraw() {
#ifdef RP5NP_GPU_WORLD
    if(renderer && !presenter_reset_pending && presenter_context!=SDL_GL_GetCurrentContext())
        SDLRequestPresenterReset();
    if(!presenter_recover())return;
#endif
    extern uint64_t horror_visuals_clock(void);
    extern void horror_visuals_present_measure(uint64_t,uint64_t,uint64_t);
    uint64_t visual_start=horror_visuals_clock();
    if (should_opengl_swap()) {
        // We want the UI background to be transparent!
        sdlPalette->colors[255].a = 0x00;

        // Draw the OpenGL view
        opengl_swap_and_restore(drawSurface);

        // Set the palette back, and we are done
        sdlPalette->colors[255].a = 0xff;
        return;
    }

    // Clear the screen!
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    int gpu_composed=gpu_world_compose();
    SDL_Color transparent=sdlPalette->colors[255],original=transparent;
    if(gpu_composed){transparent.a=0;SDL_SetPaletteColors(sdlPalette,&transparent,255,1);}
    SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, drawSurface);
    if(gpu_composed){SDL_SetPaletteColors(sdlPalette,&original,255,1);SDL_SetTextureBlendMode(texture,SDL_BLENDMODE_BLEND);}
    uint64_t visual_converted=visual_start ? horror_visuals_clock() : 0;

    // Blit to the screen by drawing the surface 
    SDL_Rect srcRect = {0, 0, gScreenWide, gScreenHigh};
    SDL_RenderCopy(renderer, texture, &srcRect, NULL);
    SDL_DestroyTexture(texture);

    // Touch overlay uses the full output surface and restores game render state.
#ifdef __ANDROID__
    touch_bridge_draw();
#endif
    // Show everything we've drawn
    uint64_t visual_submitted=visual_start ? horror_visuals_clock() : 0;
    SDL_RenderPresent(renderer);
    horror_visuals_present_measure(visual_start,visual_converted,visual_submitted);
#if defined(RP5NP_QA_TOOLS) && defined(RP5NP_GPU_WORLD)
    qa_gpu_palette_fade(gpu_composed);
#endif
}

bool MouseCaptured = FALSE;

extern int mlook_enabled;

void CaptureMouse(bool capture) {
    MouseCaptured = (capture && gShockPrefs.goCaptureMouse);

    if (!MouseCaptured && mlook_enabled && SDL_GetRelativeMouseMode() == SDL_TRUE) {
        SDL_SetRelativeMouseMode(SDL_FALSE);

        int w, h;
        SDL_GetWindowSize(window, &w, &h);
        SDL_WarpMouseInWindow(window, w / 2, h / 2);
    } else
        SDL_SetRelativeMouseMode(MouseCaptured ? SDL_TRUE : SDL_FALSE);
}

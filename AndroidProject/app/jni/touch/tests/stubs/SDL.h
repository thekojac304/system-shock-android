/* Minimal host doubles for the bridge, never on the Android include path. */
#ifndef TOUCH_TEST_SDL_H
#define TOUCH_TEST_SDL_H
#include <stdbool.h>
#include <stdint.h>
typedef uint32_t Uint32;
typedef struct SDL_Window { int unused; } SDL_Window;
typedef struct SDL_Renderer { int unused; } SDL_Renderer;
typedef union {
    Uint32 type;
    struct { Uint32 type; int event; } window;
    struct { Uint32 type; int event; } display;
    struct { Uint32 type; int64_t touchId, fingerId; float x, y; } tfinger;
    unsigned char padding[56];
} SDL_Event;
enum {
    SDL_APP_WILLENTERBACKGROUND=1, SDL_APP_DIDENTERBACKGROUND, SDL_APP_TERMINATING,
    SDL_APP_WILLENTERFOREGROUND, SDL_APP_DIDENTERFOREGROUND, SDL_WINDOWEVENT,
    SDL_FINGERDOWN, SDL_FINGERMOTION, SDL_FINGERUP, SDL_DISPLAYEVENT
};
enum {
    SDL_WINDOWEVENT_FOCUS_LOST=1, SDL_WINDOWEVENT_FOCUS_GAINED, SDL_WINDOWEVENT_SIZE_CHANGED,
    SDL_WINDOWEVENT_RESIZED, SDL_WINDOWEVENT_DISPLAY_CHANGED, SDL_WINDOWEVENT_MINIMIZED,
    SDL_WINDOWEVENT_HIDDEN
};
#define SDL_WINDOW_INPUT_FOCUS 1
#define SDL_WINDOW_HIDDEN 2
#define SDL_WINDOW_MINIMIZED 4
#define SDL_MOUSE_TOUCHID -1
#define SDL_FALSE 0
#define SDL_DISPLAYEVENT_ORIENTATION 1
#define SDL_HINT_OVERRIDE 2
#define SDL_HINT_TOUCH_MOUSE_EVENTS "touch_mouse"
#define SDL_HINT_MOUSE_TOUCH_EVENTS "mouse_touch"
int SDL_SetHintWithPriority(const char *, const char *, int);
void SDL_RenderGetLogicalSize(SDL_Renderer *, int *, int *);
int SDL_GetRendererOutputSize(SDL_Renderer *, int *, int *);
void SDL_GetWindowSize(SDL_Window *, int *, int *);
Uint32 SDL_GetWindowFlags(SDL_Window *);
void SDL_RenderGetScale(SDL_Renderer *, float *, float *);
void SDL_RenderWindowToLogical(SDL_Renderer *, int, int, float *, float *);
int SDL_SetRelativeMouseMode(int);
Uint32 SDL_GetTicks(void);
#endif

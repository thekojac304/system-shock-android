/* Explicit doubles for unused SDL platform entry points. This keeps the real
   queue adapter test independent of GCC's whole-program optimization. Any
   accidental use fails instead of silently performing platform work. */
#define UNUSED_PLATFORM() assert(!"unexpected SDL platform call in queue test")
Uint32 SDL_WasInit(Uint32 f) { (void)f; UNUSED_PLATFORM(); return 0; }
int SDL_NumJoysticks(void) { UNUSED_PLATFORM(); return 0; }
SDL_bool SDL_IsGameController(int i) { (void)i; UNUSED_PLATFORM(); return SDL_FALSE; }
int SDL_PollEvent(SDL_Event *e) { (void)e; UNUSED_PLATFORM(); return 0; }
SDL_bool SDL_GetRelativeMouseMode(void) { UNUSED_PLATFORM(); return SDL_FALSE; }
int SDL_SetRelativeMouseMode(SDL_bool b) { (void)b; UNUSED_PLATFORM(); return 0; }
Uint32 SDL_GetRelativeMouseState(int *x,int *y) { (void)x; (void)y; UNUSED_PLATFORM(); return 0; }
int SDL_GetRendererOutputSize(SDL_Renderer *r,int *w,int *h) { (void)r; (void)w; (void)h; UNUSED_PLATFORM(); return 0; }
int SDL_RenderSetViewport(SDL_Renderer *r,const SDL_Rect *v) { (void)r; (void)v; UNUSED_PLATFORM(); return 0; }
int SDL_RenderSetLogicalSize(SDL_Renderer *r,int w,int h) { (void)r; (void)w; (void)h; UNUSED_PLATFORM(); return 0; }
SDL_bool SDL_IsScreenKeyboardShown(SDL_Window *w) { (void)w; UNUSED_PLATFORM(); return SDL_FALSE; }
int SDL_SetWindowFullscreen(SDL_Window *w,Uint32 f) { (void)w; (void)f; UNUSED_PLATFORM(); return 0; }
void SDL_SetWindowPosition(SDL_Window *w,int x,int y) { (void)w; (void)x; (void)y; UNUSED_PLATFORM(); }
void SDL_GameControllerClose(SDL_GameController *c) { (void)c; UNUSED_PLATFORM(); }
Sint16 SDL_GameControllerGetAxis(SDL_GameController *c,SDL_GameControllerAxis a) { (void)c; (void)a; UNUSED_PLATFORM(); return 0; }
SDL_GameControllerButtonBind SDL_GameControllerGetBindForAxis(SDL_GameController *c,SDL_GameControllerAxis a) {
    (void)c; (void)a; UNUSED_PLATFORM(); return (SDL_GameControllerButtonBind){0};
}
SDL_GameControllerButtonBind SDL_GameControllerGetBindForButton(SDL_GameController *c,SDL_GameControllerButton b) {
    (void)c; (void)b; UNUSED_PLATFORM(); return (SDL_GameControllerButtonBind){0};
}
SDL_Joystick *SDL_GameControllerGetJoystick(SDL_GameController *c) { (void)c; UNUSED_PLATFORM(); return NULL; }
Uint16 SDL_GameControllerGetProduct(SDL_GameController *c) { (void)c; UNUSED_PLATFORM(); return 0; }
Uint16 SDL_GameControllerGetVendor(SDL_GameController *c) { (void)c; UNUSED_PLATFORM(); return 0; }
SDL_bool SDL_GameControllerHasAxis(SDL_GameController *c,SDL_GameControllerAxis a) { (void)c; (void)a; UNUSED_PLATFORM(); return SDL_FALSE; }
SDL_bool SDL_GameControllerHasButton(SDL_GameController *c,SDL_GameControllerButton b) { (void)c; (void)b; UNUSED_PLATFORM(); return SDL_FALSE; }
char *SDL_GameControllerMappingForDeviceIndex(int i) { (void)i; UNUSED_PLATFORM(); return NULL; }
const char *SDL_GameControllerName(SDL_GameController *c) { (void)c; UNUSED_PLATFORM(); return NULL; }
SDL_GameController *SDL_GameControllerOpen(int i) { (void)i; UNUSED_PLATFORM(); return NULL; }
const char *SDL_GetError(void) { UNUSED_PLATFORM(); return ""; }
SDL_JoystickID SDL_JoystickInstanceID(SDL_Joystick *j) { (void)j; UNUSED_PLATFORM(); return 0; }
void SDL_FlushEvents(Uint32 first,Uint32 last) { (void)first; (void)last; UNUSED_PLATFORM(); }
void SDL_free(void *p) { (void)p; UNUSED_PLATFORM(); }
void touch_bridge_event(const SDL_Event *e) { (void)e; UNUSED_PLATFORM(); }
void touch_bridge_controller(bool connected) { (void)connected; UNUSED_PLATFORM(); }
void touch_bridge_consume_look(int *x,int *y) { (void)x; (void)y; UNUSED_PLATFORM(); }
#undef UNUSED_PLATFORM

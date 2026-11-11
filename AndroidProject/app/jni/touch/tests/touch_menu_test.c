/* Exercise the real source queue, uiPoll, double-click state machine, and
   region dispatch together. Only window/keyboard/cursor platform calls are
   doubled; a down-only menu callback observes actual dispatched events. */
#include "SourceSnapshot/src/Libraries/INPUT/Source/sdl_events.c"
#include "SourceSnapshot/src/Libraries/UI/Source/event.c"
#include <assert.h>
#include <stdio.h>
#include "touch_input_platform_stubs.h"

SDL_Window *window;
SDL_Renderer *renderer;
bool MouseCaptured, TriggerRelMouseMode;
short mouseInstantButts;
ubyte mouseMask;
uiSlab *uiCurrentSlab;
static uint32_t test_mouse_time;
static int downs, ups, doubles, activations, drag_downs;
static bool cancel_on_press;
static LGRect menu_bounds={{0,0},{1024,768}};
static LGRegion menu_region;
static uiSlab menu_slab;
static focus_link menu_focus;
static uiEventHandler menu_handler;
static handler_chain menu_chain;

void SDL_Log(const char *format, ...) {(void)format;}
void touch_bridge_sync(void) {}
void touch_bridge_cancel(void) {touch_input_cancel();}
uint32_t mouse_get_time(void) {return test_mouse_time;}
SDL_Keymod SDL_GetModState(void) {return KMOD_NONE;}
void SDL_RenderGetLogicalSize(SDL_Renderer *r,int *w,int *h) {(void)r; *w=1024; *h=768;}
void SDL_GetWindowSize(SDL_Window *w,int *x,int *y) {(void)w; *x=1024; *y=768;}
Uint32 SDL_GetWindowFlags(SDL_Window *w) {(void)w; return SDL_WINDOW_INPUT_FOCUS;}
int SDL_ShowCursor(int toggle) {(void)toggle; return SDL_DISABLE;}
errtype ui_mouse_do_conversion(short *x,short *y,uchar down) {(void)x; (void)y; (void)down; return OK;}
errtype kb_cook(kbs_event e,ushort *code,uchar *result) {(void)e; (void)code; *result=FALSE; return OK;}
void ui_update_cursor(LGPoint pos) {(void)pos;}
/* Region/focus structures are explicitly prepared below. Unexpected lifetime
   setup calls fail rather than silently changing the tested dispatch path. */
errtype array_init(Array *a,int size,int count) {(void)a; (void)size; (void)count; assert(!"unexpected array setup"); return ERR_NOEFFECT;}
errtype array_newelem(Array *a,int *index) {(void)a; (void)index; assert(!"unexpected focus setup"); return ERR_NOEFFECT;}
errtype array_dropelem(Array *a,int index) {(void)a; (void)index; assert(!"unexpected focus removal"); return ERR_NOEFFECT;}
errtype array_destroy(Array *a) {(void)a; assert(!"unexpected array destruction"); return ERR_NOEFFECT;}
errtype uiSetCurrentSlab(uiSlab *slab) {(void)slab; assert(!"unexpected UI initialization"); return ERR_NOEFFECT;}
errtype ui_shutdown_cursors(void) {assert(!"unexpected cursor shutdown"); return ERR_NOEFFECT;}
errtype mouse_shutdown(void) {assert(!"unexpected mouse shutdown"); return ERR_NOEFFECT;}

static uchar observe_menu(uiEvent *e,LGRegion *r,intptr_t state) {
    (void)r; (void)state;
    if(e->type!=UI_EVENT_MOUSE) return TRUE;
    assert(e->pos.x==123 && e->pos.y==234);
    if(e->mouse_data.action & MOUSE_LDOWN) {
        downs++;
        if(e->mouse_data.modifiers & MOUSE_MOD_TOUCH_DRAG) drag_downs++;
        else activations++; /* Same press-only semantics as title/menu buttons. */
        if(cancel_on_press) touch_input_cancel();
    }
    if(e->mouse_data.action & MOUSE_LUP) ups++;
    if(e->mouse_data.action & UI_MOUSE_LDOUBLE) doubles++;
    return TRUE;
}

static void reset(void) {
    nextMouseEvent=nextKBevent=0;
    touchNeedsRelease=touchDirectNeedsRelease=touchEventModifiers=deliveredMouseButtons=0;
    memset(&sourceButtons,0,sizeof(sourceButtons));
    memset(&latestMouseEvent,0,sizeof(latestMouseEvent));
    ui_purge_mouse_events();
    uiDoubleClicksOn[MOUSE_LBUTTON]=TRUE;
    uiDoubleClickDelay=8; uiDoubleClickTime=45;
    test_mouse_time=10; MouseX=123; MouseY=234;
    downs=ups=doubles=activations=drag_downs=0; cancel_on_press=false;
    menu_handler=(uiEventHandler){.typemask=UI_EVENT_MOUSE|UI_EVENT_MOUSE_MOVE,
        .proc=observe_menu,.next=CHAIN_END};
    menu_chain=(handler_chain){.front=0};
    menu_chain.chain.vec=(void *)&menu_handler;
    menu_region=(LGRegion){.r=&menu_bounds,.status_flags=EVENT_CB,
        .event_order=1,.handler=&menu_chain};
    menu_focus=(focus_link){.reg=&menu_region,.evmask=ALL_EVENTS,.next=CHAIN_END};
    menu_slab=(uiSlab){0}; menu_slab.fchain.chain.vec=(void *)&menu_focus;
    menu_slab.fchain.curfocus=0; uiCurrentSlab=&menu_slab;
}

static void completed_taps(void) {
    reset();
    for(int i=0;i<3;i++) touch_input_tap(123,234);
    uiPoll();
    assert(activations==3 && downs==3 && ups==3 && doubles==0);
    assert(last_down_events[0].type==UI_EVENT_NULL);
    test_mouse_time=1000; uiPoll();
    assert(activations==3 && ups==3); /* No delayed second activation. */
    reset(); cancel_on_press=true; touch_input_tap(123,234); uiPoll();
    assert(activations==1 && downs==1 && ups==1 && doubles==0);
    assert(!latestMouseEvent.buttons && !touchNeedsRelease);
}

static void native_and_world_clicks(void) {
    /* Before: the two world/object pairs become LDOUBLE, which the title
       callback ignores, rather than the LDOWN required to select a menu. */
    reset();
    for(int i=0;i<2;i++) {touch_input_button(true,true); touch_input_button(true,false);}
    uiPoll(); assert(activations==0 && doubles==1 && ups==1);
    for(int source=TOUCH_MOUSE_PHYSICAL;source<=TOUCH_MOUSE_CONTROLLER;source++) {
        reset(); source_mouse_button((TouchMouseSource)source,true,true);
        source_mouse_button((TouchMouseSource)source,true,false);
        uiPoll(); assert(downs==0 && ups==0);
        test_mouse_time=50; uiPoll();
        assert(activations==1 && downs==1 && ups==1 && doubles==0);
    }
    /* Touch controls leave an earlier physical double-click window intact. */
    reset(); source_mouse_button(TOUCH_MOUSE_PHYSICAL,true,true);
    source_mouse_button(TOUCH_MOUSE_PHYSICAL,true,false);
    uiPoll(); touch_input_tap(123,234); uiPoll();
    assert(activations==1 && last_down_events[0].type!=UI_EVENT_NULL);
    test_mouse_time=50; uiPoll(); assert(activations==2 && ups==2);
}

static void drag_semantics(void) {
    reset(); touch_input_drag_button(true); touch_input_pointer(123,234);
    touch_input_drag_button(false); uiPoll();
    assert(downs==1 && ups==1 && drag_downs==1 && activations==0 && doubles==0);
    assert(last_down_events[0].type==UI_EVENT_NULL);
    test_mouse_time=1000; uiPoll(); assert(activations==0 && ups==1);
}

int main(void) {
    completed_taps(); native_and_world_clicks(); drag_semantics();
    puts("TOUCH_MENU_TESTS=PASS (real UI polling: immediate single taps, native double clicks, cancel release, drags)");
    return 0;
}

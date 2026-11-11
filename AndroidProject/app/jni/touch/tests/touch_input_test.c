/* Compile the real SDL adapter into this test TU to exercise its internal
   source routing and queued cancellation without a window or SDL runtime. */
#include "SourceSnapshot/src/Libraries/INPUT/Source/sdl_events.c"
#include <assert.h>
#include <stdio.h>
#include "touch_input_platform_stubs.h"

SDL_Window *window;
SDL_Renderer *renderer;
bool MouseCaptured, TriggerRelMouseMode;
void SDL_Log(const char *format, ...) {(void)format;}
void touch_bridge_sync(void) {}
void touch_bridge_cancel(void) {touch_input_cancel();}
uint32_t mouse_get_time(void) {return 1;}
static SDL_Keymod mod_state;
SDL_Keymod SDL_GetModState(void) {return mod_state;}
void SDL_RenderGetLogicalSize(SDL_Renderer *r,int *w,int *h) {(void)r; *w=1024; *h=768;}
void SDL_GetWindowSize(SDL_Window *w,int *x,int *y) {(void)w; *x=1024; *y=768;}
Uint32 SDL_GetWindowFlags(SDL_Window *w) {(void)w; return SDL_WINDOW_INPUT_FOCUS;}
int SDL_ShowCursor(int toggle) {(void)toggle; return SDL_DISABLE;}

static void reset(void) {
    nextMouseEvent=0; touchNeedsRelease=touchDirectNeedsRelease=deliveredMouseButtons=0;
    touchEventModifiers=0;
    memset(&sourceButtons,0,sizeof(sourceButtons));
    memset(&latestMouseEvent,0,sizeof(latestMouseEvent));
    MouseX=512; MouseY=384;
}
static ss_mouse_event next(void) {
    ss_mouse_event e; assert(mouse_next(&e)==OK); return e;
}
static void empty(void) {ss_mouse_event e; assert(mouse_next(&e)==ERR_DUNDERFLOW);}
static void taps(void) {
    reset(); touch_input_button(false,true); touch_input_button(false,false);
    ss_mouse_event e=next(); assert(e.type==MOUSE_RDOWN && e.buttons==2);
    e=next(); assert(e.type==MOUSE_RUP && !e.buttons); empty();
    assert(!touchNeedsRelease);
}
static void release_pending_at_cancel(void) {
    reset(); touch_input_button(false,true); assert(next().type==MOUSE_RDOWN);
    touch_input_button(false,false); /* finger released, game hasn't seen it */
    touch_input_cancel(); touch_input_cancel();
    assert(next().type==MOUSE_RUP); empty(); assert(!latestMouseEvent.buttons);
    reset(); touch_input_button(true,true); touch_input_button(true,false);
    touch_input_cancel(); assert(next().type==MOUSE_LUP); empty();
    /* Two short taps queued together; cancel after the second press. */
    reset();
    for(int i=0;i<2;i++) {touch_input_button(false,true); touch_input_button(false,false);}
    assert(next().type==MOUSE_RDOWN); assert(next().type==MOUSE_RUP);
    assert(next().type==MOUSE_RDOWN);
    touch_input_cancel(); assert(next().type==MOUSE_RUP); empty();
    reset(); touch_input_button(false,true); assert(next().type==MOUSE_RDOWN);
    touch_input_button(false,false); mouse_flush(); assert(next().type==MOUSE_RUP); empty();
}
static void physical_survives(void) {
    for(int source=0;source<2;++source) for(int left=0;left<2;++left) {
        reset();
        source_mouse_button((TouchMouseSource)source,left,true);
        ss_mouse_event e=next(); uchar held=e.buttons;
        touch_input_button(left,true); touch_input_button(left,false); empty();
        touch_input_cancel(); empty(); assert(latestMouseEvent.buttons==held);
        source_mouse_button((TouchMouseSource)source,left,false);
        e=next(); assert(!e.buttons); empty();
        reset(); touch_input_button(left,true); /* still waiting in queue */
        source_mouse_button((TouchMouseSource)source,left,true);
        touch_input_cancel(); e=next();
        assert(e.buttons==held && e.type==(left ? MOUSE_LDOWN : MOUSE_RDOWN)); empty();
        source_mouse_button((TouchMouseSource)source,left,false);
        e=next(); assert(!e.buttons); empty();
        /* Touch goes down first, physical button overlaps it and outlives it. */
        reset(); touch_input_button(left,true); next();
        source_mouse_button((TouchMouseSource)source,left,true);
        touch_input_cancel(); empty(); assert(latestMouseEvent.buttons==held);
        source_mouse_button((TouchMouseSource)source,left,false);
        e=next(); assert(!e.buttons); empty();
    }
}
static void cancel_preserves_native_queue(void) {
    reset(); touch_input_button(false,true);
    source_mouse_button(TOUCH_MOUSE_PHYSICAL,true,true);
    touch_input_pointer(123,234);
    touch_input_cancel();
    ss_mouse_event e=next(); assert(e.type==MOUSE_LDOWN && e.buttons==1);
    e=next(); assert(e.type==MOUSE_RUP && e.buttons==1); empty();
    assert(latestMouseEvent.buttons==1);
}
static void queue_overflow(void) {
    reset(); touch_input_button(false,true);
    for(int i=0;i<kNumMouseEvents+5;i++) touch_input_pointer(100+i,200);
    touch_input_cancel(); assert(next().type==MOUSE_RUP); empty();
}
static void cursor_edges(void) {
    reset(); touch_input_pointer(123,234); touch_input_button(true,true);
    touch_input_pointer(456,567); touch_input_button(true,false);
    ss_mouse_event e=next(); assert(e.type==MOUSE_MOTION && e.x==123 && e.y==234 && !e.buttons);
    e=next(); assert(e.type==MOUSE_LDOWN && e.x==123 && e.y==234 && e.buttons==1);
    e=next(); assert(e.type==MOUSE_MOTION && e.x==456 && e.y==567 && e.buttons==1);
    e=next(); assert(e.type==MOUSE_LUP && e.x==456 && e.y==567 && !e.buttons); empty();
    touch_input_pointer(-50,-20); e=next(); assert(e.x==0 && e.y==0);
    touch_input_pointer(1024,768); e=next(); assert(e.x==1023 && e.y==767);
    reset(); touch_input_pointer(123,234); touch_input_cancel(); empty(); /* pending tap */
    /* Real left-button drag press was dispatched, release must survive cancel. */
    touch_input_button(true,true); assert(next().type==MOUSE_LDOWN);
    touch_input_button(true,false); touch_input_cancel();
    e=next(); assert(e.type==MOUSE_LUP && !e.buttons); empty();
}
static void control_taps_and_drags(void) {
    reset(); touch_input_tap(123,234);
    ss_mouse_event e=next(); assert(e.type==MOUSE_MOTION && e.x==123 && e.y==234);
    e=next(); assert(e.type==MOUSE_LDOWN && e.buttons==1 && e.modifiers==MOUSE_MOD_TOUCH_TAP);
    e=next(); assert(e.type==MOUSE_LUP && !e.buttons && e.modifiers==MOUSE_MOD_TOUCH_TAP);
    empty(); assert(!touchDirectNeedsRelease);
    reset(); touch_input_tap(123,234); next(); next();
    touch_input_cancel(); touch_input_cancel();
    e=next(); assert(e.type==MOUSE_LUP && !e.buttons && e.modifiers==MOUSE_MOD_TOUCH_TAP);
    empty();
    reset(); touch_input_tap(123,234); touch_input_tap(123,234);
    next(); next(); next(); next(); next(); /* Second press dispatched. */
    touch_input_cancel();
    e=next(); assert(e.type==MOUSE_LUP && e.modifiers==MOUSE_MOD_TOUCH_TAP); empty();
    reset(); touch_input_drag_button(true); touch_input_pointer(456,567);
    touch_input_drag_button(false);
    e=next(); assert(e.type==MOUSE_LDOWN && e.modifiers==MOUSE_MOD_TOUCH_DRAG);
    e=next(); assert(e.type==MOUSE_MOTION && e.buttons==1 && !e.modifiers);
    e=next(); assert(e.type==MOUSE_LUP && e.modifiers==MOUSE_MOD_TOUCH_DRAG);
    empty();
    /* A completed touch tap cannot release an overlapping native hold. */
    reset(); source_mouse_button(TOUCH_MOUSE_PHYSICAL,true,true); next();
    touch_input_tap(123,234); assert(next().type==MOUSE_MOTION); empty();
    assert(latestMouseEvent.buttons==1);
    source_mouse_button(TOUCH_MOUSE_PHYSICAL,true,false); assert(next().type==MOUSE_LUP); empty();
}
static void sprint_sources(void) {
    mod_state=KMOD_NONE;
    rp5np_set_shift(false);
    touch_input_cancel_actions();
    sshockKeyStates[42]=KB_MOD_PRESSED;
    touch_input_action(TOUCH_SPRINT,true);
    assert(rp5np_effective_shift() && (sshockKeyStates[42]&KB_MOD_SHIFT));
    rp5np_set_shift(true); /* controller L3 outlives touch */
    touch_input_cancel_actions();
    assert(rp5np_effective_shift() && (sshockKeyStates[42]&KB_MOD_SHIFT));
    rp5np_set_shift(false);
    assert(!rp5np_effective_shift() && !(sshockKeyStates[42]&KB_MOD_SHIFT));
    mod_state=KMOD_LSHIFT; /* physical keyboard outlives touch */
    touch_input_action(TOUCH_SPRINT,true);
    touch_input_cancel_actions();
    assert(rp5np_effective_shift() && (sshockKeyStates[42]&KB_MOD_SHIFT));
    mod_state=KMOD_NONE;
    rp5np_refresh_shift();
    assert(!rp5np_effective_shift());
}
int main(void) {
    taps(); release_pending_at_cancel(); physical_survives();
    cancel_preserves_native_queue(); queue_overflow(); cursor_edges(); sprint_sources();
    control_taps_and_drags();
    puts("TOUCH_INPUT_TESTS=PASS (8 groups, real SDL adapter including completed menu taps)"); return 0;
}

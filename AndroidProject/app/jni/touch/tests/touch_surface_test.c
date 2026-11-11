/* Real surface producer/consumer against SDL's by-value event queue contract. */
#include "../touch_surface.c"
#include <assert.h>
#include <stdio.h>
static SDL_Event queued;
static int pushed, flushed, fail;
int SDL_AtomicGet(SDL_atomic_t *a) {return a->value;}
int SDL_AtomicSet(SDL_atomic_t *a,int value) {int old=a->value; a->value=value; return old;}
Uint32 SDL_RegisterEvents(int n) {assert(n==1); return SDL_USEREVENT+1;}
int SDL_PushEvent(SDL_Event *e) {
    if(fail) return -1;
    /* This is what SDL_PushEvent does: Android sample time must survive it. */
    e->common.timestamp=987; queued=*e; pushed++; return 1;
}
void SDL_FlushEvent(Uint32 type) {assert(type==SDL_USEREVENT+1); flushed++;}
int main(void) {
    TouchSurfaceEvent s;
    surface_push(5,7,0,40,400,1920,1080,100); assert(!pushed);
    touch_surface_init(); touch_surface_init();
    surface_push(5,7,0,40,400,1920,1080,100);
    assert(touch_surface_read(&queued,&s));
    assert(s.phase==TOUCH_DOWN && s.x==40 && s.width==1920 && s.sample_ms==100 && s.timestamp==987);
    surface_push(5,8,5,1880,400,1920,1080,110);
    assert(touch_surface_read(&queued,&s) && s.finger==8 && s.x==1880 && s.phase==TOUCH_DOWN);
    surface_push(5,8,2,1900,401,1920,1080,120);
    assert(touch_surface_read(&queued,&s) && s.phase==TOUCH_MOTION);
    surface_push(5,8,6,1900,401,1920,1080,130);
    assert(touch_surface_read(&queued,&s) && s.phase==TOUCH_UP);
    surface_push(5,0,3,0,0,1920,1080,140);
    assert(touch_surface_read(&queued,&s) && s.phase==TOUCH_ABORT);
    queued.type=SDL_FINGERDOWN; queued.tfinger.x=0;
    assert(!touch_surface_read(&queued,&s)); /* reject clamped SDL finger path */
    fail=1; surface_push(5,8,1,0,0,1920,1080,150);
    assert(touch_surface_failed() && flushed==1 && !touch_surface_failed());
    puts("TOUCH_SURFACE_TESTS=PASS (raw queue, timestamps, multi-pointer actions, cancel, overflow)");
    return 0;
}

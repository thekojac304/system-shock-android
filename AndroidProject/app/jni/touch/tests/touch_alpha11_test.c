#include "touch_controls.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

static TouchControls t;
static struct {TouchOutput type; float x,y;} events[64];
static int count;
static void record(void *user,TouchOutput e,float x,float y) {
    (void)user; assert(count<64);
    events[count].type=e; events[count].x=x; events[count++].y=y;
}
static void near(float a,float b) {assert(fabsf(a-b)<.001f);}
static void reset(bool cursor) {
    touch_init(&t,record,NULL); touch_configure(&t,1920,1080,true,true,false);
    if(cursor) touch_set_mode(&t,TOUCH_CURSOR);
    count=0;
}
static void finger(TouchPhase p,int id,float x,float y,uint32_t ms) {
    touch_finger(&t,p,5,id,x,y,ms);
}
static void bars(void) {
    reset(false); /* 4:3 viewport occupies x=240..1680 */
    finger(TOUCH_DOWN,1,40,600,0); assert(t.move_owner>=0); near(t.anchor_x,40);
    finger(TOUCH_MOTION,1,100,600,10); assert(t.movement_x>.5f);
    finger(TOUCH_DOWN,2,1880,400,0); assert(t.look_owner>=0);
    finger(TOUCH_MOTION,2,1800,420,10); near(t.look_x,-80); near(t.look_y,20);
    /* Ownership is never clamped even for coordinates beyond the surface. */
    reset(false); finger(TOUCH_DOWN,1,-5,600,0); near(t.anchor_x,-5);
    reset(false); finger(TOUCH_DOWN,1,1930,400,0);
    near(t.fingers[t.look_owner].x,1930);
    /* Wide surfaces naturally place FIRE/USE/toggle in the right bar. */
    reset(false); touch_configure(&t,3840,1080,true,true,false); count=0;
    assert(t.layout.fire.x>2640 && t.layout.use.x>2640 && t.layout.toggle.x>2640);
    finger(TOUCH_DOWN,1,t.layout.fire.x,t.layout.fire.y,0); assert(t.fire_count==1);
    finger(TOUCH_DOWN,2,t.layout.use.x,t.layout.use.y,0); assert(t.use_count==1);
    finger(TOUCH_DOWN,3,t.layout.toggle.x+2,t.layout.toggle.y+2,0);
    assert(t.mode==TOUCH_CURSOR && !t.fire_count && t.cursor_owner<0);
}
static void transform(void) {
    float x,y;
    TouchRect v={240,0,1440,1080};
    touch_map_cursor(240,0,v,1.40625f,1.40625f,1024,768,&x,&y); near(x,0); near(y,0);
    touch_map_cursor(1680,1080,v,1.40625f,1.40625f,1024,768,&x,&y); near(x,1023); near(y,767);
    touch_map_cursor(960,540,v,1.40625f,1.40625f,1024,768,&x,&y); near(x,512); near(y,384);
    touch_map_cursor(0,540,v,1.40625f,1.40625f,1024,768,&x,&y); near(x,0); near(y,384);
    touch_map_cursor(1920,540,v,1.40625f,1.40625f,1024,768,&x,&y); near(x,1023);
    v=(TouchRect){0,555,1080,810}; /* portrait top/bottom bars */
    touch_map_cursor(540,0,v,1.0546875f,1.0546875f,1024,768,&x,&y); near(x,512); near(y,0);
    touch_map_cursor(540,1920,v,1.0546875f,1.0546875f,1024,768,&x,&y); near(x,512); near(y,767);
    touch_map_cursor(0,555,v,1.0546875f,1.0546875f,1024,768,&x,&y); near(x,0); near(y,0);
    touch_map_cursor(1080,1365,v,1.0546875f,1.0546875f,1024,768,&x,&y); near(x,1023); near(y,767);
}
static void taps(void) {
    reset(true);
    finger(TOUCH_DOWN,1,600,400,100); assert(count==1 && events[0].type==TOUCH_POINTER_MOVE);
    finger(TOUCH_MOTION,1,603,397,130); assert(count==1);
    finger(TOUCH_UP,1,604,403,180);
    assert(count==2 && events[1].type==TOUCH_POINTER_TAP);
    near(events[1].x,600); near(events[1].y,400);
    /* Region-specific click synthesis happens in the bridge. */
    finger(TOUCH_DOWN,2,600,400,220); finger(TOUCH_UP,2,600,400,270);
    assert(count==4 && events[3].type==TOUCH_POINTER_TAP);
    reset(true); finger(TOUCH_DOWN,1,600,400,100);
    finger(TOUCH_UP,1,600,400,100+TOUCH_TAP_MS+1); assert(count==1);
    reset(true); finger(TOUCH_DOWN,1,600,400,UINT32_MAX-50);
    finger(TOUCH_UP,1,600,400,20); assert(count==2); /* timestamp wrap */
    reset(true); finger(TOUCH_DOWN,1,600,400,0);
    finger(TOUCH_UP,1,600+t.cursor_slop,400,TOUCH_TAP_MS); assert(count==2);
}
static void drags(void) {
    reset(true); finger(TOUCH_DOWN,1,600,400,100);
    finger(TOUCH_MOTION,1,620,410,130);
    assert(count==3 && events[1].type==TOUCH_POINTER_DOWN && events[2].type==TOUCH_POINTER_MOVE);
    near(events[1].x,600); near(events[1].y,400); near(events[2].x,620);
    finger(TOUCH_MOTION,1,601,400,150); /* returning inside slop stays a drag */
    assert(events[3].type==TOUCH_POINTER_MOVE);
    finger(TOUCH_UP,1,800,450,700); assert(events[count-1].type==TOUCH_POINTER_UP);
    near(events[count-1].x,800); assert(t.cursor_owner<0);
    reset(true); finger(TOUCH_DOWN,1,600,400,0); finger(TOUCH_UP,1,900,400,200);
    assert(count==4 && events[1].type==TOUCH_POINTER_DOWN && events[3].type==TOUCH_POINTER_UP);
}
static void menu_taps(void) {
    reset(true); touch_configure(&t,1920,1080,true,false,false); count=0;
    finger(TOUCH_DOWN,1,600,400,100);
    finger(TOUCH_MOTION,1,603,401,600);
    finger(TOUCH_UP,1,603,401,100+TOUCH_MENU_TAP_MS);
    assert(count==2 && events[1].type==TOUCH_POINTER_TAP);
    near(events[1].x,600); near(events[1].y,400);
    count=0; finger(TOUCH_DOWN,2,600,400,2000);
    finger(TOUCH_UP,2,600,400,2000+TOUCH_MENU_TAP_MS+1);
    assert(count==1); /* a long stationary hold is not a menu activation */
    count=0; finger(TOUCH_DOWN,3,600,400,4000);
    finger(TOUCH_MOTION,3,630,400,4300);
    finger(TOUCH_UP,3,600,400,4500);
    assert(count==5 && events[1].type==TOUCH_POINTER_DOWN &&
           events[4].type==TOUCH_POINTER_UP); /* returning to anchor stays a drag */
}
static void ownership_and_cancel(void) {
    for(int dragging=0;dragging<2;dragging++) {
        reset(true); finger(TOUCH_DOWN,1,600,400,0);
        if(dragging) finger(TOUCH_MOTION,1,650,400,20);
        int n=count, owner=t.cursor_owner;
        finger(TOUCH_DOWN,2,800,600,30); finger(TOUCH_MOTION,2,850,600,40);
        assert(count==n && t.cursor_owner==owner);
        finger(TOUCH_UP,1,650,400,50); n=count;
        finger(TOUCH_MOTION,2,900,600,60); finger(TOUCH_UP,2,900,600,70);
        assert(count==n && t.cursor_owner<0);
        finger(TOUCH_DOWN,3,800,600,80); assert(t.cursor_owner>=0);
    }
    for(int reason=0;reason<4;reason++) for(int dragging=0;dragging<2;dragging++) {
        reset(true); finger(TOUCH_DOWN,1,600,400,0);
        if(dragging) finger(TOUCH_MOTION,1,650,400,20);
        switch(reason) {
        case 0: finger(TOUCH_ABORT,1,0,0,30); break;
        case 1: touch_set_mode(&t,TOUCH_GAMEPLAY); break;
        case 2: touch_configure(&t,1080,1920,true,true,false); break;
        case 3: finger(TOUCH_DOWN,2,t.layout.toggle.x+1,t.layout.toggle.y+1,30); break;
        }
        assert(t.cursor_owner<0); int n=count;
        finger(TOUCH_MOTION,1,700,400,40); finger(TOUCH_UP,1,700,400,50);
        assert(count==n); /* cancelled owner/toggle cannot leak a click */
    }
}
int main(void) {
    bars(); transform(); taps(); drags(); menu_taps(); ownership_and_cancel();
    puts("TOUCH_ALPHA11_TESTS=PASS (6 groups: surface, transform, taps, drags, menu taps, ownership/cancel)");
    return 0;
}

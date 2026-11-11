/* Deterministic gesture/source-ownership tests. No SDL window or game data. */
#include "touch_controls.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

static TouchControls t;
static TouchOutput events[256];
static int count;
static void record(void *user, TouchOutput e, float x, float y) {
    (void)user; (void)x; (void)y;
    assert(count < 256); events[count++] = e;
}
static void reset(void) {
    count = 0; touch_init(&t, record, NULL);
    touch_configure(&t, 1024, 768, true, true, false); count = 0;
}
static void finger(TouchPhase p, int64_t dev, int64_t id, float x, float y) {
    touch_finger(&t, p, dev, id, x, y, 0);
}
static void near(float a, float b) { assert(fabsf(a-b) < .0001f); }
static void neutral(void) {
    near(t.movement_x, 0); near(t.movement_y, 0);
    near(t.look_x, 0); near(t.look_y, 0);
    assert(t.move_owner == -1 && t.look_owner == -1 && t.cursor_owner == -1);
    assert(!t.fire_count && !t.use_count);
}
static void movement(void) {
    reset(); float r = t.layout.move_radius;
    finger(TOUCH_DOWN, 4, 91, 100, 200);
    near(t.anchor_x, 100); near(t.anchor_y, 200);
    finger(TOUCH_MOTION, 4, 91, 100+r/2, 200-r*.4f);
    near(t.movement_x, .5f); near(t.movement_y, -.4f);
    int x=0,y=0; touch_merge_axes(&t,50,&x,&y); assert(x==25 && y==20);
    x=-50; y=0; touch_merge_axes(&t,50,&x,&y); assert(x==-50 && y==20);
    x=0; y=100; touch_merge_axes(&t,50,&x,&y); assert(x==25 && y==100);
    finger(TOUCH_MOTION, 4, 91, 800, 700);
    near(t.movement_x*t.movement_x+t.movement_y*t.movement_y,1);
    assert(!t.fire_count && t.look_owner==-1); /* cannot change role */
    finger(TOUCH_UP, 4, 91, 800, 700); neutral();
    finger(TOUCH_DOWN, 4, 92, 210, 310); near(t.anchor_x,210); near(t.anchor_y,310);
}
static void look_and_identity(void) {
    reset();
    finger(TOUCH_DOWN, INT64_C(1)<<40, -7, 600, 100);
    finger(TOUCH_DOWN, 2, -7, 100, 100); /* same finger ID, different device */
    assert(t.look_owner>=0 && t.move_owner>=0);
    finger(TOUCH_MOTION, INT64_C(1)<<40, -7, 610.25f, 95.5f);
    finger(TOUCH_MOTION, INT64_C(1)<<40, -7, 615.5f, 91.75f);
    int dx,dy; touch_consume_look(&t,&dx,&dy); assert(dx==15 && dy==-8);
    touch_consume_look(&t,&dx,&dy); assert(dx==0 && dy==0);
    finger(TOUCH_MOTION, INT64_C(1)<<40, -7, 616, 91);
    touch_consume_look(&t,&dx,&dy); assert(dx==1 && dy==-1);
    finger(TOUCH_MOTION, 99, -7, 900, 600); touch_consume_look(&t,&dx,&dy);
    assert(dx==0 && dy==0);
    finger(TOUCH_UP, INT64_C(1)<<40, -7, 616, 91); assert(t.move_owner>=0);
}
static void multitouch_fire_drag(void) {
    reset();
    finger(TOUCH_DOWN, 1, 1, 100, 600);
    finger(TOUCH_DOWN, 1, 2, t.layout.fire.x, t.layout.fire.y);
    finger(TOUCH_DOWN, 1, 3, t.layout.use.x, t.layout.use.y);
    assert(t.move_owner>=0 && t.look_owner>=0 && t.fire_count==1 && t.use_count==1);
    assert(count==2 && events[0]==TOUCH_FIRE_DOWN && events[1]==TOUCH_USE_PRESS);
    finger(TOUCH_MOTION, 1, 2, 500, 250); /* leaves hitbox, still fires/aims */
    assert(t.fire_count==1 && t.look_x < -100 && t.look_y < -100);
    finger(TOUCH_DOWN, 1, 4, t.layout.fire.x, t.layout.fire.y);
    assert(t.fire_count==2 && count==2);
    finger(TOUCH_UP, 1, 2, 500, 250);
    assert(t.fire_count==1 && count==2 && t.look_owner==-1);
    finger(TOUCH_MOTION, 1, 4, 700, 300); assert(t.look_owner==-1); /* no promotion */
    finger(TOUCH_UP, 1, 4, 700, 300); assert(events[count-1]==TOUCH_FIRE_UP);
    assert(t.move_owner>=0 && t.use_count==1);
}
static void look_not_stolen(void) {
    reset();
    finger(TOUCH_DOWN,1,1,600,200); int owner=t.look_owner;
    finger(TOUCH_DOWN,1,2,t.layout.fire.x,t.layout.fire.y);
    finger(TOUCH_MOTION,1,2,900,400);
    assert(t.look_owner==owner); near(t.look_x,0); near(t.look_y,0);
    finger(TOUCH_UP,1,1,600,200);
    finger(TOUCH_MOTION,1,2,950,500); assert(t.look_owner==-1);
    finger(TOUCH_DOWN,1,3,600,200); assert(t.look_owner>=0);
}
static void tap_edges(void) {
    reset();
    finger(TOUCH_DOWN,1,1,t.layout.fire.x,t.layout.fire.y);
    finger(TOUCH_UP,1,1,t.layout.fire.x,t.layout.fire.y);
    assert(count==2 && events[0]==TOUCH_FIRE_DOWN && events[1]==TOUCH_FIRE_UP);
    finger(TOUCH_DOWN,1,2,t.layout.use.x,t.layout.use.y);
    finger(TOUCH_MOTION,1,2,600,300);
    finger(TOUCH_UP,1,2,600,300);
    assert(count==3 && events[2]==TOUCH_USE_PRESS);
}
static void cursor_and_toggle(void) {
    reset();
    finger(TOUCH_DOWN,1,8,t.layout.toggle.x+5,t.layout.toggle.y+5);
    assert(t.mode==TOUCH_CURSOR && t.cursor_owner==-1);
    assert(events[0]==TOUCH_CANCEL && events[1]==TOUCH_MODE_CHANGED);
    finger(TOUCH_MOTION,1,8,400,400); assert(count==2);
    finger(TOUCH_DOWN,1,1,100,200); int owner=t.cursor_owner;
    finger(TOUCH_DOWN,1,2,200,300); assert(t.cursor_owner==owner);
    finger(TOUCH_MOTION,1,2,500,500); assert(count==3);
    finger(TOUCH_MOTION,1,1,300,400); assert(events[count-1]==TOUCH_POINTER_MOVE);
    finger(TOUCH_UP,1,1,320,420); assert(events[count-1]==TOUCH_POINTER_UP);
    int n=count; finger(TOUCH_MOTION,1,2,510,510); assert(count==n && t.cursor_owner==-1);
    finger(TOUCH_UP,1,8,400,400);
    finger(TOUCH_DOWN,1,9,t.layout.toggle.x+5,t.layout.toggle.y+5);
    assert(t.mode==TOUCH_GAMEPLAY && t.cursor_owner==-1);
    finger(TOUCH_MOTION,1,2,700,500); neutral();
}
static void cancellation(void) {
    for (int reason=0;reason<6;++reason) {
        reset();
        finger(TOUCH_DOWN,1,1,100,500);
        finger(TOUCH_MOTION,1,1,120,520);
        finger(TOUCH_DOWN,1,2,t.layout.fire.x,t.layout.fire.y);
        finger(TOUCH_MOTION,1,2,900,500);
        switch(reason) {
        case 0: touch_cancel(&t); break;
        case 1: touch_configure(&t,1024,768,false,true,false); break;
        case 2: touch_configure(&t,800,600,true,true,false); break;
        case 3: touch_configure(&t,1024,768,true,false,false); break;
        case 4: touch_configure(&t,1024,768,true,true,true); break;
        case 5: touch_set_mode(&t,TOUCH_CURSOR); break;
        }
        neutral();
        finger(TOUCH_MOTION,1,1,600,500); finger(TOUCH_UP,1,2,900,500); neutral();
    }
    reset(); touch_configure(&t,1024,768,true,true,true);
    finger(TOUCH_DOWN,1,1,t.layout.fire.x,t.layout.fire.y);
    assert(t.cursor_owner>=0 && !t.fire_count);
    touch_configure(&t,1024,768,true,true,false); neutral();
}
static void overflow_and_duplicates(void) {
    reset();
    for(int i=0;i<TOUCH_MAX_FINGERS+4;++i) finger(TOUCH_DOWN,1,i,100,100);
    int owner=t.move_owner;
    finger(TOUCH_DOWN,1,0,t.layout.fire.x,t.layout.fire.y);
    assert(t.move_owner==owner && !t.fire_count);
    finger(TOUCH_UP,1,0,100,100);
    for(int i=1;i<TOUCH_MAX_FINGERS+4;++i) finger(TOUCH_MOTION,1,i,200,200);
    assert(t.move_owner==-1);
}
static void mouse_source_ownership(void) {
    for(int bit=1;bit<=2;bit*=2) for(int other=0;other<2;++other) {
        TouchMouseButtons b={{0,0,0}};
        assert(touch_mouse_button(&b,(TouchMouseSource)other,(uint8_t)bit,true));
        assert(!touch_mouse_button(&b,TOUCH_MOUSE_TOUCH,(uint8_t)bit,true));
        assert(!touch_mouse_button(&b,TOUCH_MOUSE_TOUCH,(uint8_t)bit,false));
        assert(touch_mouse_buttons(&b)==bit);
        assert(touch_mouse_button(&b,(TouchMouseSource)other,(uint8_t)bit,false));
        assert(!touch_mouse_buttons(&b));
        assert(touch_mouse_button(&b,TOUCH_MOUSE_TOUCH,(uint8_t)bit,true));
        assert(!touch_mouse_button(&b,(TouchMouseSource)other,(uint8_t)bit,true));
        assert(!touch_mouse_button(&b,TOUCH_MOUSE_TOUCH,(uint8_t)bit,false));
        assert(touch_mouse_buttons(&b)==bit);
        assert(touch_mouse_button(&b,(TouchMouseSource)other,(uint8_t)bit,false));
    }
}
static void key_source_ownership(void) {
    for (int other=TOUCH_KEY_PHYSICAL; other<=TOUCH_KEY_CONTROLLER; ++other) {
        TouchKeySources key={{false,false,false}};
        assert(touch_key_source(&key,(TouchKeySource)other,true));
        assert(!touch_key_source(&key,TOUCH_KEY_TOUCH,true));
        assert(!touch_key_source(&key,TOUCH_KEY_TOUCH,false));
        assert(touch_key_held(&key));
        assert(touch_key_source(&key,(TouchKeySource)other,false));
        assert(!touch_key_held(&key));
        assert(touch_key_source(&key,TOUCH_KEY_TOUCH,true));
        assert(!touch_key_source(&key,(TouchKeySource)other,true));
        assert(!touch_key_source(&key,TOUCH_KEY_TOUCH,false));
        assert(touch_key_held(&key));
        assert(touch_key_source(&key,(TouchKeySource)other,false));
    }
}
int main(void) {
    movement(); look_and_identity(); multitouch_fire_drag(); look_not_stolen();
    tap_edges(); cursor_and_toggle(); cancellation(); overflow_and_duplicates(); mouse_source_ownership(); key_source_ownership();
    puts("TOUCH_CONTROLS_TESTS=PASS (10 groups)"); return 0;
}

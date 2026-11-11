#include "touch_controls.h"
#include <assert.h>
#include <stdio.h>

static TouchControls t;
static struct { TouchOutput output; int action; } edges[128];
static int count;
static void record(void *user, TouchOutput output, float x, float y) {
    (void)user; (void)y;
    assert(count < 128);
    edges[count].output = output;
    edges[count].action = (int)x;
    count++;
}
static void reset(float w, float h) {
    touch_init(&t, record, NULL);
    touch_configure(&t, w, h, true, true, false);
    count = 0;
}
static void action(TouchPhase phase, int finger, TouchAction a) {
    TouchCircle c = t.layout.action[a];
    touch_finger(&t, phase, 1, finger, c.x, c.y, 0);
}
static void all_actions_and_taps(void) {
    reset(1920, 1080);
    for (int a = 0; a < TOUCH_ACTION_COUNT; ++a) {
        if (a == TOUCH_SPRINT) continue;
        int before = count;
        action(TOUCH_DOWN, a+1, (TouchAction)a);
        action(TOUCH_UP, a+1, (TouchAction)a);
        assert(edges[before].output == TOUCH_ACTION_DOWN && edges[before].action == a);
        assert(edges[before+1].output == TOUCH_ACTION_UP && edges[before+1].action == a);
        assert(t.action_count[a] == 0);
    }
    assert(count == (TOUCH_ACTION_COUNT-1)*2);
    /* Both edges of every short action survive taps between game polls. */
    for (int a = 0; a < TOUCH_ACTION_COUNT; ++a) {
        if (a == TOUCH_SPRINT) continue;
        count = 0;
        for (int i = 0; i < 3; ++i) {
            action(TOUCH_DOWN, 20+i, (TouchAction)a);
            action(TOUCH_UP, 20+i, (TouchAction)a);
        }
        assert(count == 6);
        for (int i = 0; i < 6; ++i) {
            assert(edges[i].action == a);
            assert(edges[i].output == (i%2 ? TOUCH_ACTION_UP : TOUCH_ACTION_DOWN));
        }
    }
}
static void simultaneous_and_stable(void) {
    reset(1920, 1080);
    touch_finger(&t, TOUCH_DOWN, 1, 1, 100, 700, 0);
    action(TOUCH_DOWN, 2, TOUCH_SPRINT);
    touch_finger(&t, TOUCH_DOWN, 1, 3, 1200, 400, 0);
    touch_finger(&t, TOUCH_DOWN, 1, 4, t.layout.fire.x, t.layout.fire.y, 0);
    action(TOUCH_DOWN, 5, TOUCH_JUMP);
    action(TOUCH_DOWN, 6, TOUCH_WEAPON_NEXT);
    assert(t.move_owner >= 0 && t.look_owner >= 0 && t.fire_count == 1);
    assert(t.action_count[TOUCH_SPRINT] && t.action_count[TOUCH_JUMP]);
    touch_finger(&t, TOUCH_MOTION, 1, 6, t.layout.action[TOUCH_EXPLOSIVE].x,
                 t.layout.action[TOUCH_EXPLOSIVE].y, 1);
    assert(t.action_count[TOUCH_WEAPON_NEXT] == 1 && !t.action_count[TOUCH_EXPLOSIVE]);
    action(TOUCH_UP, 6, TOUCH_WEAPON_NEXT);
    action(TOUCH_UP, 5, TOUCH_JUMP);
    action(TOUCH_UP, 2, TOUCH_SPRINT);
    assert(!t.action_count[TOUCH_SPRINT] && t.fire_count == 1 && t.move_owner >= 0);
}
static void cancellation_and_bars(void) {
    reset(3840, 1080); /* 4:3 image is x=1200..2640; controls use side bars. */
    assert(t.layout.action[TOUCH_JUMP].x > 2640);
    assert(t.layout.action[TOUCH_PAUSE].x < 1200);
    action(TOUCH_DOWN, 1, TOUCH_SPRINT);
    action(TOUCH_DOWN, 2, TOUCH_JUMP);
    touch_cancel(&t);
    assert(!t.action_count[TOUCH_SPRINT] && !t.action_count[TOUCH_JUMP]);
    int n=count;
    action(TOUCH_UP, 1, TOUCH_SPRINT);
    assert(count == n);
    action(TOUCH_DOWN, 3, TOUCH_SPRINT);
    touch_set_mode(&t, TOUCH_CURSOR);
    assert(!t.action_count[TOUCH_SPRINT]);
    touch_configure(&t, 3840, 1080, true, true, true);
    assert(!t.action_count[TOUCH_SPRINT]);
}
static void sprint_toggle(void) {
    reset(1920, 1080);
    action(TOUCH_DOWN, 1, TOUCH_SPRINT);
    assert(!t.sprint_latched && count == 0);
    action(TOUCH_UP, 1, TOUCH_SPRINT);
    assert(t.sprint_latched && count == 1 && edges[0].output == TOUCH_ACTION_DOWN);
    touch_finger(&t, TOUCH_DOWN, 1, 2, 100, 700, 0);
    touch_finger(&t, TOUCH_MOTION, 1, 2, 170, 650, 1);
    assert(t.move_owner >= 0 && t.movement_x > 0 && t.sprint_latched);
    action(TOUCH_DOWN, 3, TOUCH_SPRINT);
    assert(t.sprint_latched && count == 1);
    action(TOUCH_UP, 3, TOUCH_SPRINT);
    assert(!t.sprint_latched && count == 2 && edges[1].output == TOUCH_ACTION_UP);
    action(TOUCH_DOWN, 4, TOUCH_SPRINT);
    action(TOUCH_UP, 4, TOUCH_SPRINT);
    assert(t.sprint_latched);
    touch_set_mode(&t, TOUCH_CURSOR);
    assert(!t.sprint_latched);
    touch_set_mode(&t, TOUCH_GAMEPLAY);
    action(TOUCH_DOWN, 5, TOUCH_SPRINT);
    action(TOUCH_UP, 5, TOUCH_SPRINT);
    touch_configure(&t, 1920, 1080, true, true, true);
    assert(!t.sprint_latched);
}
static void movie_taps(void) {
    TouchMovieTap tap;
    touch_movie_tap_reset(&tap);
    assert(!touch_movie_tap_event(&tap,TOUCH_DOWN,1,1,100,200,10,8));
    assert(!touch_movie_tap_event(&tap,TOUCH_MOTION,1,1,104,203,20,8));
    assert(touch_movie_tap_event(&tap,TOUCH_UP,1,1,105,204,40,8));
    assert(!touch_movie_tap_event(&tap,TOUCH_UP,1,1,105,204,40,8));
    assert(!touch_movie_tap_event(&tap,TOUCH_DOWN,1,2,100,200,50,8));
    assert(!touch_movie_tap_event(&tap,TOUCH_MOTION,1,2,130,200,60,8));
    assert(!touch_movie_tap_event(&tap,TOUCH_UP,1,2,100,200,70,8));
    assert(!touch_movie_tap_event(&tap,TOUCH_DOWN,1,3,100,200,80,8));
    assert(!touch_movie_tap_event(&tap,TOUCH_DOWN,1,4,120,200,81,8));
    assert(!touch_movie_tap_event(&tap,TOUCH_UP,1,3,100,200,90,8));
    assert(!touch_movie_tap_event(&tap,TOUCH_UP,1,4,120,200,91,8));
    assert(!touch_movie_tap_event(&tap,TOUCH_DOWN,1,5,100,200,100,8));
    assert(!touch_movie_tap_event(&tap,TOUCH_ABORT,1,5,0,0,110,8));
    assert(!touch_movie_tap_event(&tap,TOUCH_UP,1,5,100,200,120,8));
    assert(!touch_movie_tap_event(&tap,TOUCH_DOWN,1,6,100,200,130,8));
    assert(!touch_movie_tap_event(&tap,TOUCH_UP,1,6,100,200,130+TOUCH_TAP_MS+1,8));
}
int main(void) {
    all_actions_and_taps(); simultaneous_and_stable(); cancellation_and_bars(); sprint_toggle(); movie_taps();
    puts("TOUCH_ALPHA2_TESTS=PASS (actions, rapid taps, ownership, cancellation, bars, movie taps)");
    return 0;
}

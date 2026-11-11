/* Ammo gestures use the real control/radial core. Native inventory mutation is
   tested separately by the game adapter tests. SPDX-License-Identifier: GPL-3.0-or-later */
#include "touch_controls.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static TouchControls t;
static int downs,ups,selections,last_id,pointers;
static int slot,gun,ammo_types;
static unsigned reserve[3],loaded;
static bool ammo_weapon;

static void output(void *user,TouchOutput e,float x,float y) {
    (void)user;
    if (e==TOUCH_ACTION_DOWN && (int)x==TOUCH_AMMO) downs++;
    if (e==TOUCH_ACTION_UP && (int)x==TOUCH_AMMO) ups++;
    if (e==TOUCH_RADIAL_SELECT) {
        assert((int)x==TOUCH_RADIAL_AMMO);
        selections++; last_id=(int)y;
    }
    if (e==TOUCH_POINTER_DOWN || e==TOUCH_POINTER_UP || e==TOUCH_POINTER_TAP) pointers++;
}
static int populate(void *user,TouchRadialCategory category,
                    TouchRadialEntry entries[TOUCH_RADIAL_MAX_ENTRIES]) {
    (void)user;
    if (category!=TOUCH_RADIAL_AMMO || !ammo_weapon) return 0;
    int count=1;
    entries[0]=(TouchRadialEntry){.id=TOUCH_AMMO_RELOAD_ID(slot,gun),.label="RELOAD",
        .available=loaded || reserve[0] || reserve[1] || reserve[2]};
    for (int i=0;i<ammo_types;++i) {
        unsigned rounds=reserve[i]+(i==0 ? loaded : 0);
        if (!rounds) continue;
        entries[count++]=(TouchRadialEntry){.id=TOUCH_AMMO_TYPE_ID(slot,gun,i),
            .available=true,.current=i==0,.count=rounds};
        snprintf(entries[count-1].label,sizeof(entries[count-1].label),"AMMO %d",i);
    }
    return count;
}
static void init(bool cursor) {
    downs=ups=selections=pointers=0; last_id=-1;
    slot=2; gun=5; ammo_types=2; loaded=8; ammo_weapon=true;
    reserve[0]=20; reserve[1]=10; reserve[2]=0;
    touch_init(&t,output,NULL); t.radial_populate=populate;
    touch_configure(&t,1920,1080,true,true,false);
    if (cursor) touch_set_mode(&t,TOUCH_CURSOR);
}
static void finger(TouchPhase phase,int id,float x,float y,uint32_t ms) {
    touch_finger(&t,phase,9,id,x,y,ms);
}
static void press(int id,uint32_t ms) {
    TouchCircle c=t.layout.action[TOUCH_AMMO];
    finger(TOUCH_DOWN,id,c.x,c.y,ms);
}
static void open(int id,uint32_t ms) {
    press(id,ms); touch_tick(&t,ms+TOUCH_RADIAL_HOLD_MS);
    assert(t.radial.open && t.radial.category==TOUCH_RADIAL_AMMO);
}
static void sector_point(int sector,float *x,float *y) {
    float angle=6.283185307f*sector/t.radial.count-1.570796327f;
    *x=t.radial.cx+cosf(angle)*t.radial.outer_radius*.7f;
    *y=t.radial.cy+sinf(angle)*t.radial.outer_radius*.7f;
}
static void release_sector(int finger_id,int sector,uint32_t ms) {
    float x,y; sector_point(sector,&x,&y);
    finger(TOUCH_UP,finger_id,x,y,ms);
}
static void legacy_taps(void) {
    init(false);
    TouchCircle c=t.layout.action[TOUCH_AMMO];
    assert(touch_control_hit(&t,TC_AMMO,c.x,c.y));
    press(1,100); assert(!downs && !ups && !t.radial.open);
    touch_tick(&t,100+TOUCH_RADIAL_HOLD_MS-1); assert(!t.radial.open);
    finger(TOUCH_UP,1,c.x,c.y,100+TOUCH_RADIAL_HOLD_MS-1);
    assert(downs==1 && ups==1 && !selections && !t.action_count[TOUCH_AMMO]);
    for (int i=0;i<20;++i) {
        press(2,500+i*50); finger(TOUCH_UP,2,c.x,c.y,510+i*50);
    }
    assert(downs==21 && ups==21 && !selections && !pointers);
    /* The unsigned timestamp subtraction preserves holds over SDL wrap. */
    press(3,UINT32_MAX-100); touch_tick(&t,219);
    assert(t.radial.open && downs==21);
    finger(TOUCH_UP,3,t.radial.cx,t.radial.cy,220);
    assert(!t.radial.open && downs==21 && !selections);
}
static void selection_and_cancel(void) {
    init(false); open(1,0);
    assert(!downs && !ups && !selections && t.radial.highlighted==-1);
    assert(t.radial.count==3 && t.radial.entries[1].current && t.radial.entries[1].count==28);
    finger(TOUCH_UP,1,t.radial.cx,t.radial.cy,330);
    assert(!t.radial.open && !downs && !ups && !selections);
    open(2,400); release_sector(2,0,730);
    assert(selections==1 && last_id==TOUCH_AMMO_RELOAD_ID(slot,gun));
    open(3,800); release_sector(3,2,1130);
    assert(selections==2 && last_id==TOUCH_AMMO_TYPE_ID(slot,gun,1));
    finger(TOUCH_UP,3,100,100,1140); assert(selections==2); /* duplicate release */
    open(4,1200); release_sector(4,1,1530);
    assert(selections==3 && last_id==TOUCH_AMMO_TYPE_ID(slot,gun,0));
    assert(!downs && !ups && !t.action_count[TOUCH_AMMO]);
}
static void availability_and_context(void) {
    init(false); ammo_types=1; reserve[1]=0; loaded=0;
    open(1,0); assert(t.radial.count==2 && t.radial.entries[0].available);
    release_sector(1,0,330); assert(last_id==TOUCH_AMMO_RELOAD_ID(slot,gun));
    init(false); reserve[1]=0;
    open(1,0); assert(t.radial.count==2); /* unavailable alternate hidden */
    finger(TOUCH_UP,1,t.radial.cx,t.radial.cy,330);
    init(false); loaded=0; reserve[0]=reserve[1]=0;
    open(1,0); assert(t.radial.count==1 && !t.radial.entries[0].available);
    release_sector(1,0,330); assert(!selections && !downs);
    init(false); open(1,0);
    TouchRadialEntry fresh[TOUCH_RADIAL_MAX_ENTRIES]={{0}};
    reserve[1]=0;
    int count=populate(NULL,TOUCH_RADIAL_AMMO,fresh);
    touch_radial_refresh(&t.radial,fresh,count);
    assert(t.radial.count==3 && !t.radial.entries[2].available); /* positions stay stable */
    release_sector(1,2,330); assert(!selections && !downs);
    for (int changed=0;changed<2;++changed) {
        init(false); open(1,0);
        if (changed) gun++; else slot++;
        count=populate(NULL,TOUCH_RADIAL_AMMO,fresh);
        touch_radial_refresh(&t.radial,fresh,count);
        for (int i=0;i<t.radial.count;++i) assert(!t.radial.entries[i].available);
        release_sector(1,0,330); assert(!selections && !downs);
    }
    init(false); ammo_weapon=false; press(1,0); touch_tick(&t,320);
    assert(!t.radial.open);
    finger(TOUCH_UP,1,t.layout.action[TOUCH_AMMO].x,t.layout.action[TOUCH_AMMO].y,330);
    assert(!downs && !ups && !selections); /* a failed hold never falls through to a tap */
}
static void cursor_and_look(void) {
    init(true); open(1,0);
    assert(t.mode==TOUCH_CURSOR && t.effective_mode==TOUCH_CURSOR && t.cursor_owner<0);
    finger(TOUCH_DOWN,2,t.radial.cx,t.radial.cy,325);
    finger(TOUCH_UP,2,t.radial.cx,t.radial.cy,326);
    assert(t.radial.open && t.cursor_owner<0 && !pointers);
    release_sector(1,2,330);
    assert(selections==1 && !pointers && t.mode==TOUCH_CURSOR);
    init(true); finger(TOUCH_DOWN,2,1200,400,0); press(1,10); touch_tick(&t,330);
    assert(!t.radial.open && t.cursor_owner>=0);
    finger(TOUCH_UP,1,t.layout.action[TOUCH_AMMO].x,t.layout.action[TOUCH_AMMO].y,340);
    assert(!downs && !selections);
    finger(TOUCH_UP,2,1200,400,350); assert(pointers==1);
    init(false); finger(TOUCH_DOWN,2,1200,400,0);
    finger(TOUCH_MOTION,2,1220,400,10); assert(t.look_x==20);
    open(1,20); assert(t.look_owner<0 && !t.look_x);
    finger(TOUCH_MOTION,2,1250,400,350); assert(!t.look_x);
    release_sector(1,0,360); assert(selections==1 && t.mode==TOUCH_GAMEPLAY);
    finger(TOUCH_MOTION,2,1260,400,370); assert(!t.look_x);
}
static void interruptions(void) {
    for (int kind=0;kind<4;++kind) {
        init(false); open(1,0);
        switch (kind) {
        case 0: touch_cancel(&t); break;
        case 1: touch_set_mode(&t,TOUCH_CURSOR); break;
        case 2: touch_configure(&t,1920,1080,true,true,true); break;
        case 3: touch_ui_open(&t); break;
        }
        assert(!t.radial.open);
        finger(TOUCH_UP,1,960,400,330); assert(!selections && !downs && !ups);
    }
    init(false); press(1,0);
    TouchCircle c=t.layout.action[TOUCH_AMMO];
    finger(TOUCH_MOTION,1,c.x+c.radius,c.y,20); touch_tick(&t,320);
    finger(TOUCH_UP,1,c.x,c.y,330); assert(!t.radial.open && !downs && !selections);
    init(false); finger(TOUCH_DOWN,2,t.layout.fire.x,t.layout.fire.y,0);
    press(1,10); touch_tick(&t,330);
    finger(TOUCH_UP,1,c.x,c.y,340); assert(!t.radial.open && !downs && !selections);
    init(false); open(1,0); press(2,325);
    finger(TOUCH_UP,2,c.x,c.y,326);
    assert(t.radial.open && !downs); release_sector(1,0,330); assert(selections==1);
    init(false); t.settings.wheel_enabled[0]=true;
    finger(TOUCH_DOWN,2,t.layout.action[TOUCH_WEAPON_NEXT].x,t.layout.action[TOUCH_WEAPON_NEXT].y,0);
    press(1,10); touch_tick(&t,320);
    assert(t.radial.open && t.radial.category==TOUCH_RADIAL_WEAPON);
    finger(TOUCH_UP,1,c.x,c.y,330); assert(!downs && !ups);
    touch_cancel(&t);
}
static void preferences(const char *path) {
    /* New placements are appended. Existing on-disk names/indices survive. */
    assert(TOUCH_PAUSE==8 && TC_STICK==13 && TOUCH_RADIAL_EXPLOSIVE==2);
    FILE *f=fopen(path,"wb"); assert(f);
    assert(fputs("version=3\nfire.x=0.8\nnext.y=0.85\nweapon_wheel=1\n",f)>=0);
    assert(fclose(f)==0);
    TouchSettings s,r;
    assert(touch_settings_load(&s,path)==TOUCH_LOAD_OK);
    assert(fabsf(s.control[TC_FIRE].x-.8f)<.001f && s.wheel_enabled[0]);
    assert(s.control[TC_AMMO].x==touch_defaults[TC_AMMO].x);
    s.control[TC_AMMO]=(TouchPlacement){.4f,.2f,1.2f};
    assert(touch_settings_save(&s,path));
    assert(touch_settings_load(&r,path)==TOUCH_LOAD_OK);
    assert(fabsf(r.control[TC_AMMO].scale-1.2f)<.001f && r.wheel_enabled[0]);
    remove(path);
}
int main(int argc,char **argv) {
    assert(argc==2);
    legacy_taps(); selection_and_cancel(); availability_and_context();
    cursor_and_look(); interruptions(); preferences(argv[1]);
    puts("TOUCH_AMMO_TESTS=PASS (tap/hold, reload/select/cancel, availability/context, cursor/look, interrupts, preferences)");
    return 0;
}

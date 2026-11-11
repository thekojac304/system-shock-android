#include "touch_controls.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static TouchControls t;
static int down[TOUCH_ACTION_COUNT],up[TOUCH_ACTION_COUNT];
static int light,strong,selected,selected_category,pointers,fire_down,fire_up;
static int available[3][3]={{1,1,0},{1,1,0},{1,1,0}};
static void out(void *user,TouchOutput e,float x,float y) {
    (void)user;
    if (e==TOUCH_ACTION_DOWN) down[(int)x]++;
    if (e==TOUCH_ACTION_UP) up[(int)x]++;
    if (e==TOUCH_HAPTIC) { if ((int)x==1) light++; else strong++; }
    if (e==TOUCH_RADIAL_SELECT) { selected=(int)y; selected_category=(int)x; }
    if (e==TOUCH_POINTER_DOWN || e==TOUCH_POINTER_UP || e==TOUCH_POINTER_TAP) pointers++;
    if (e==TOUCH_FIRE_DOWN) fire_down++;
    if (e==TOUCH_FIRE_UP) fire_up++;
}
static int populate(void *user,TouchRadialCategory category,TouchRadialEntry entries[TOUCH_RADIAL_MAX_ENTRIES]) {
    (void)user;
    for (int i=0;i<3;++i) {
        entries[i].id=i+10;
        snprintf(entries[i].label,sizeof(entries[i].label),"ITEM %d",i);
        entries[i].available=available[category][i]!=0;
        entries[i].current=i==1;
        entries[i].count=entries[i].available ? 1 : 0;
    }
    return 3;
}
static void init(void) {
    memset(down,0,sizeof(down)); memset(up,0,sizeof(up));
    light=strong=pointers=fire_down=fire_up=0;
    selected=-1; selected_category=-1;
    touch_init(&t,out,NULL);
    t.radial_populate=populate;
    touch_configure(&t,1920,1080,true,true,false);
}
static TouchCircle host(TouchAction a) { return t.layout.action[a]; }
static void finger(TouchPhase p,int id,float x,float y,unsigned ms) {
    touch_finger(&t,p,7,id,x,y,ms);
}
static void settings(const char *path) {
    TouchSettings s,r;
    touch_settings_defaults(&s);
    assert(s.version==3 && !s.haptics_enabled && s.haptic_strength==1);
    for (int i=0;i<3;++i) assert(!s.wheel_enabled[i]);
    s.haptics_enabled=true; s.haptic_strength=3;
    s.wheel_enabled[0]=true; s.wheel_enabled[2]=true;
    assert(touch_settings_save(&s,path));
    assert(touch_settings_load(&r,path)==TOUCH_LOAD_OK);
    assert(r.haptics_enabled && r.haptic_strength==3);
    assert(r.wheel_enabled[0] && !r.wheel_enabled[1] && r.wheel_enabled[2]);
    FILE *f=fopen(path,"wb"); assert(f);
    assert(fputs("version=2\nopacity=0.5\n",f)>=0); assert(fclose(f)==0);
    assert(touch_settings_load(&r,path)==TOUCH_LOAD_MIGRATED);
    assert(!r.haptics_enabled && r.haptic_strength==1);
    for (int i=0;i<3;++i) assert(!r.wheel_enabled[i]);
    remove(path);
}
static void settings_ui(void) {
    init(); touch_ui_open(&t);
    touch_ui_action(&t,TU_PAGE);
    assert(t.settings_page==1);
    assert(touch_ui_bounds(&t,TU_WHEEL_WEAPON).w>0);
    assert(touch_ui_bounds(&t,TU_EDIT).w==0);
    touch_ui_action(&t,TU_HAPTICS);
    touch_ui_action(&t,TU_HAPTIC_STRENGTH);
    touch_ui_action(&t,TU_WHEEL_WEAPON);
    touch_ui_action(&t,TU_WHEEL_EXPLOSIVE);
    assert(t.settings.haptics_enabled && t.settings.haptic_strength==2);
    assert(t.settings.wheel_enabled[0] && !t.settings.wheel_enabled[1] && t.settings.wheel_enabled[2]);
    touch_ui_action(&t,TU_SAVE);
    assert(t.panel==TOUCH_UI_CLOSED);
    touch_ui_open(&t);
    assert(t.settings.wheel_enabled[0] && t.settings.wheel_enabled[2]);
    touch_ui_action(&t,TU_CANCEL);
}
static void haptics(void) {
    init(); t.settings.haptics_enabled=true;
    TouchCircle c=host(TOUCH_JUMP);
    finger(TOUCH_DOWN,1,c.x,c.y,0);
    assert(light==1 && strong==0 && down[TOUCH_JUMP]==1);
    finger(TOUCH_MOTION,1,c.x,c.y,800);
    finger(TOUCH_UP,1,c.x,c.y,900);
    assert(light==1);
    finger(TOUCH_DOWN,2,t.layout.fire.x,t.layout.fire.y,1000);
    finger(TOUCH_MOTION,2,t.layout.fire.x+2,t.layout.fire.y,2000);
    finger(TOUCH_UP,2,t.layout.fire.x+2,t.layout.fire.y,2100);
    assert(fire_down==1 && fire_up==1 && light==2);
    c=host(TOUCH_SPRINT);
    finger(TOUCH_DOWN,3,c.x,c.y,2200); finger(TOUCH_UP,3,c.x,c.y,2250);
    assert(strong==1 && t.sprint_latched);
    finger(TOUCH_DOWN,4,c.x,c.y,2300); finger(TOUCH_UP,4,c.x,c.y,2350);
    assert(strong==2 && !t.sprint_latched);
    TouchRect b=t.layout.toggle;
    finger(TOUCH_DOWN,5,b.x+b.w/2,b.y+b.h/2,2400);
    assert(strong==3 && t.mode==TOUCH_CURSOR);
    finger(TOUCH_UP,5,b.x+b.w/2,b.y+b.h/2,2450);
    assert(strong==3);
    touch_set_mode(&t,TOUCH_GAMEPLAY);
    t.settings.haptics_enabled=false;
    finger(TOUCH_DOWN,6,c.x,c.y,2500); finger(TOUCH_UP,6,c.x,c.y,2550);
    assert(strong==3);
    t.settings.haptics_enabled=true;
    touch_configure(&t,1920,1080,false,true,false);
    finger(TOUCH_DOWN,7,c.x,c.y,2600);
    assert(strong==3);
    touch_configure(&t,1920,1080,true,true,true);
    finger(TOUCH_DOWN,8,c.x,c.y,2700);
    assert(strong==3);
}
static void category(TouchRadialCategory cat,TouchAction action) {
    init();
    TouchCircle c=host(action);
    finger(TOUCH_DOWN,1,c.x,c.y,100);
    assert(down[action]==1); /* disabled means legacy action on down */
    finger(TOUCH_UP,1,c.x,c.y,150);
    assert(up[action]==1);
    t.settings.wheel_enabled[cat]=true;
    finger(TOUCH_DOWN,2,c.x,c.y,200);
    assert(down[action]==1);
    touch_tick(&t,519); assert(!t.radial.open);
    finger(TOUCH_UP,2,c.x,c.y,519);
    assert(down[action]==2 && up[action]==2);
    finger(TOUCH_DOWN,3,c.x,c.y,600);
    touch_tick(&t,920); assert(t.radial.open && t.radial.owner>=0);
    assert(t.radial.category==cat && down[action]==2);
    assert(t.radial.entries[1].current && !t.radial.entries[2].available);
    TouchRadialEntry fresh[1]={{.id=11,.available=true,.count=2,.current=true}};
    touch_radial_refresh(&t.radial,fresh,1);
    assert(!t.radial.entries[0].available && t.radial.entries[1].count==2);
    int owner=t.radial.owner;
    float R=t.radial.outer_radius,cx=t.radial.cx,cy=t.radial.cy;
    finger(TOUCH_DOWN,9,c.x,c.y,925);
    finger(TOUCH_UP,9,c.x,c.y,926);
    assert(t.radial.open && down[action]==2);
    /* Other finger cannot steer or close the owner wheel. */
    finger(TOUCH_DOWN,4,cx,cy,930);
    finger(TOUCH_MOTION,4,cx+R*.8f,cy,940);
    assert(t.radial.open && t.radial.owner==owner);
    finger(TOUCH_UP,4,cx+R*.8f,cy,950);
    /* Unavailable third segment is inert; center release cancels. */
    finger(TOUCH_MOTION,3,cx-R*.7f,cy+R*.35f,960);
    assert(t.radial.highlighted==-1);
    finger(TOUCH_UP,3,cx,cy,970);
    assert(!t.radial.open && selected==-1 && down[action]==2);
    finger(TOUCH_DOWN,5,c.x,c.y,1000); touch_tick(&t,1320);
    finger(TOUCH_MOTION,5,cx,cy-R*.7f,1330);
    assert(t.radial.highlighted==0);
    finger(TOUCH_UP,5,cx,cy-R*.7f,1340);
    assert(selected==10 && selected_category==(int)cat && down[action]==2);
}
static void ownership_and_cancel(void) {
    init(); t.settings.wheel_enabled[0]=true;
    TouchCircle c=host(TOUCH_WEAPON_NEXT);
    finger(TOUCH_DOWN,1,100,800,0);
    finger(TOUCH_MOTION,1,170,800,10);
    float move=t.movement_x; assert(move>0);
    finger(TOUCH_DOWN,2,c.x,c.y,20); touch_tick(&t,340);
    assert(t.radial.open && t.move_owner>=0 && t.movement_x==move);
    finger(TOUCH_MOTION,1,190,800,350); assert(t.movement_x>move);
    finger(TOUCH_DOWN,3,t.layout.fire.x,t.layout.fire.y,360);
    assert(fire_down==0);
    finger(TOUCH_UP,3,t.layout.fire.x,t.layout.fire.y,370);
    assert(pointers==0);
    touch_cancel(&t);
    assert(!t.radial.open && t.move_owner==-1 && selected==-1);
    finger(TOUCH_UP,2,c.x,c.y,400); assert(selected==-1);
    finger(TOUCH_DOWN,4,c.x,c.y,500); touch_tick(&t,820);
    touch_configure(&t,1920,1080,true,true,true);
    assert(!t.radial.open);
    finger(TOUCH_UP,4,c.x,c.y,840); assert(selected==-1);
}
int main(int argc,char **argv) {
    assert(argc==2);
    settings(argv[1]); settings_ui(); haptics();
    category(TOUCH_RADIAL_WEAPON,TOUCH_WEAPON_NEXT);
    category(TOUCH_RADIAL_CONSUMABLE,TOUCH_CONSUMABLE);
    category(TOUCH_RADIAL_EXPLOSIVE,TOUCH_EXPLOSIVE);
    ownership_and_cancel();
    puts("TOUCH_ALPHA4_TESTS=PASS");
    return 0;
}

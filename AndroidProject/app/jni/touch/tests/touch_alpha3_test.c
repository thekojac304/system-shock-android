#include "touch_controls.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static TouchControls t;
static const char *path;
static int gameplay_edges, saves;
static bool fail_save;
static void near(float a,float b) {
    float tolerance=1e-6f*fmaxf(1,fabsf(b));
    if (fabsf(a-b)>=tolerance) fprintf(stderr,"expected %.9g, got %.9g\n",b,a);
    assert(fabsf(a-b)<tolerance);
}
static void output(void *user,TouchOutput e,float x,float y) {
    (void)user; (void)x; (void)y;
    if (e==TOUCH_SETTINGS_SAVE) {
        saves++;
        t.save_failed=fail_save || !touch_settings_save(&t.settings,path);
    } else if (e!=TOUCH_CANCEL && e!=TOUCH_UI_CHANGED && e!=TOUCH_MODE_CHANGED) gameplay_edges++;
}
static void init(void) {
    touch_init(&t,output,NULL);
    touch_configure(&t,1920,1080,true,true,false);
    gameplay_edges=saves=0; fail_save=false;
}
static void fixture(const char *contents) {
    FILE *f=fopen(path,"wb"); assert(f); assert(fputs(contents,f)>=0); assert(fclose(f)==0);
}
static void same(const TouchSettings *a,const TouchSettings *b) {
    assert(a->version==b->version && a->invert_y==b->invert_y);
    near(a->opacity,b->opacity); near(a->sensitivity_x,b->sensitivity_x); near(a->sensitivity_y,b->sensitivity_y);
    for (int i=0;i<TC_COUNT;i++) {
        near(a->control[i].x,b->control[i].x); near(a->control[i].y,b->control[i].y);
        near(a->control[i].scale,b->control[i].scale);
    }
}
static void persistence(void) {
    TouchSettings a,b,defaults;
    touch_settings_defaults(&defaults);
    remove(path);
    assert(touch_settings_load(&a,path)==TOUCH_LOAD_DEFAULTS); same(&a,&defaults);
    near(a.sensitivity_x,1); near(a.sensitivity_y,1); near(a.control[TC_STICK].scale,1);
    assert(!a.invert_y); near(a.opacity,1);
    a.opacity=.4f; a.sensitivity_x=1.8f; a.sensitivity_y=.7f; a.invert_y=true;
    for (int i=0;i<TC_COUNT;i++) a.control[i]=(TouchPlacement){.1f+i*.04f,.9f-i*.04f,.6f+i*.08f};
    assert(touch_settings_save(&a,path));
    assert(touch_settings_load(&b,path)==TOUCH_LOAD_OK); same(&a,&b);
    a.opacity=.8f; assert(touch_settings_save(&a,path)); /* replace existing file */
    assert(touch_settings_load(&b,path)==TOUCH_LOAD_OK); same(&a,&b);
    assert(!touch_settings_save(&a,""));
    fixture("version=1\nopacity=0.7\nsensitivity_x=2\ninvert_y=1\nstick_size=1.4\n");
    assert(touch_settings_load(&b,path)==TOUCH_LOAD_MIGRATED);
    near(b.opacity,.7f); near(b.sensitivity_x,2); near(b.sensitivity_y,1);
    near(b.control[TC_STICK].scale,1.4f); near(b.control[TC_FIRE].x,.86f);
    assert(b.invert_y && b.version==TOUCH_SETTINGS_VERSION);
    fixture("version=2\nopacity=nan\nsensitivity_x=100\nsensitivity_y=-2\ninvert_y=7\nfire.x=inf\nfire.y=-4\nuse.scale=99\nfire.scale=garbage\n");
    assert(touch_settings_load(&b,path)==TOUCH_LOAD_MIGRATED);
    near(b.opacity,1); near(b.sensitivity_x,3); near(b.sensitivity_y,.25f);
    near(b.control[TC_FIRE].x,.86f); near(b.control[TC_FIRE].y,0);
    near(b.control[TC_FIRE].scale,1); near(b.control[TC_USE].scale,1.8f); assert(!b.invert_y);
    fixture("broken contents\nopacity=0.4\n");
    assert(touch_settings_load(&b,path)==TOUCH_LOAD_INVALID); same(&b,&defaults);
    fixture("version=999\nopacity=0.3\n");
    assert(touch_settings_load(&b,path)==TOUCH_LOAD_FUTURE); same(&b,&defaults);
    FILE *f=fopen(path,"wb"); assert(f);
    for (int i=0;i<17000;i++) fputc('x',f);
    fclose(f); assert(touch_settings_load(&b,path)==TOUCH_LOAD_INVALID);
    same(&b,&defaults);
}
static void tap(TouchUiAction a) {
    TouchRect r=touch_ui_bounds(&t,a); assert(r.w>0);
    touch_finger(&t,TOUCH_DOWN,1,80,r.x+r.w/2,r.y+r.h/2,0);
    touch_finger(&t,TOUCH_UP,1,80,r.x+r.w/2,r.y+r.h/2,20);
}
static void editor(void) {
    init();
    TouchSettings original=t.settings;
    touch_finger(&t,TOUCH_DOWN,1,1,t.layout.fire.x,t.layout.fire.y,0);
    assert(t.fire_count==1);
    touch_finger(&t,TOUCH_DOWN,1,2,t.layout.settings.x,t.layout.settings.y,0);
    assert(t.panel==TOUCH_UI_SETTINGS && t.fire_count==0 && t.effective_mode==TOUCH_CURSOR);
    gameplay_edges=0;
    touch_finger(&t,TOUCH_MOTION,1,1,1700,1000,20);
    touch_finger(&t,TOUCH_UP,1,1,1700,1000,40);
    assert(gameplay_edges==0);
    tap(TU_EDIT); assert(t.panel==TOUCH_UI_EDITOR);
    TouchCircle c=t.layout.fire;
    touch_finger(&t,TOUCH_DOWN,2,4,c.x+10,c.y,0);
    assert(t.selected==TC_FIRE && t.ui_drag);
    touch_finger(&t,TOUCH_DOWN,3,4,t.layout.use.x,t.layout.use.y,0);
    touch_finger(&t,TOUCH_MOTION,3,4,40,40,10);
    near(t.layout.fire.x,c.x); /* device/finger pair owns the drag */
    touch_finger(&t,TOUCH_MOTION,2,4,510,400,20);
    near(t.layout.fire.x,500); near(t.layout.fire.y,400);
    touch_finger(&t,TOUCH_UP,2,4,510,400,30);
    assert(!t.ui_owned && !t.fire_count && gameplay_edges==0);
    for (int i=0;i<40;i++) touch_ui_action(&t,TU_SCALE_UP);
    near(t.settings.control[TC_FIRE].scale,1.8f);
    for (int i=0;i<40;i++) touch_ui_action(&t,TU_SCALE_DOWN);
    near(t.settings.control[TC_FIRE].scale,.6f);
    touch_editor_position(&t,TC_FIRE,-100,-100);
    TouchRect b=touch_control_bounds(&t,TC_FIRE); near(b.x,0); near(b.y,0);
    touch_editor_position(&t,TC_MODE,100000,100000);
    b=touch_control_bounds(&t,TC_MODE); near(b.x+b.w,t.layout.width); near(b.y+b.h,t.layout.height);
    tap(TU_DOCK); assert(t.toolbar_top);
    tap(TU_OPACITY_DOWN); tap(TU_BACK); tap(TU_X_UP); tap(TU_Y_DOWN); tap(TU_INVERT); tap(TU_STICK_UP);
    assert(t.panel==TOUCH_UI_SETTINGS && t.settings.invert_y);
    TouchSettings draft=t.settings;
    fail_save=true; tap(TU_SAVE);
    assert(t.save_failed && t.panel==TOUCH_UI_SETTINGS);
    fail_save=false; tap(TU_SAVE);
    assert(t.panel==TOUCH_UI_CLOSED && t.effective_mode==TOUCH_GAMEPLAY && saves==2);
    TouchSettings loaded; assert(touch_settings_load(&loaded,path)==TOUCH_LOAD_OK); same(&loaded,&draft);
    touch_ui_open(&t); tap(TU_RESET); same(&t.settings,&original);
    tap(TU_CANCEL); same(&t.settings,&draft);
    assert(touch_settings_load(&loaded,path)==TOUCH_LOAD_OK); same(&loaded,&draft);
    touch_ui_open(&t); tap(TU_RESET); tap(TU_SAVE);
    assert(touch_settings_load(&loaded,path)==TOUCH_LOAD_OK); same(&loaded,&original);
}
static void scaled_hits_and_sensitivity(void) {
    init();
    for (int id=0;id<TC_COUNT;id++) {
        t.settings.control[id].scale=1.7f; touch_layout_refresh(&t);
        TouchRect b=touch_control_bounds(&t,id);
        float x=b.x+b.w/2,y=b.y+b.h/2;
        assert(touch_control_hit(&t,id,x,y));
        assert(touch_control_hit(&t,id,x+b.w*.499f,y));
        assert(!touch_control_hit(&t,id,x+b.w*.501f,y));
        if (id!=TC_MODE) assert(!touch_control_hit(&t,id,b.x,b.y));
    }
    init(); t.settings.sensitivity_x=2; t.settings.sensitivity_y=.5f; t.settings.invert_y=true;
    t.look_scale=.5f;
    touch_finger(&t,TOUCH_DOWN,1,1,1200,350,0);
    int x,y; touch_consume_look(&t,&x,&y); assert(x==0 && y==0);
    touch_finger(&t,TOUCH_MOTION,1,1,1220,390,16);
    touch_consume_look(&t,&x,&y); assert(x==20 && y==-10);
    touch_finger(&t,TOUCH_MOTION,1,1,1230,410,1000);
    touch_finger(&t,TOUCH_MOTION,1,1,1240,430,1001);
    touch_consume_look(&t,&x,&y); assert(x==20 && y==-10);
    touch_finger(&t,TOUCH_UP,1,1,1240,430,1100);
    t.settings.control[TC_STICK].scale=1.5f; touch_layout_refresh(&t);
    float radius=t.layout.move_radius; near(radius,1080*.11f*1.5f);
    touch_finger(&t,TOUCH_DOWN,1,2,5,600,0);
    touch_finger(&t,TOUCH_MOTION,1,2,5+radius*.5f,600,0); near(t.movement_x,.5f);
    touch_finger(&t,TOUCH_MOTION,1,2,5+radius*2,600+radius*2,0);
    near(hypotf(t.movement_x,t.movement_y),1);
    touch_finger(&t,TOUCH_UP,1,2,5+radius*2,600+radius*2,0);
    assert(t.movement_x==0 && t.movement_y==0);
}
static void resets_and_controller(void) {
    for (int reason=0;reason<5;reason++) for (int editor_mode=0;editor_mode<2;editor_mode++) {
        init(); t.settings.control[TC_FIRE].x=.77f; t.settings.opacity=.55f;
        TouchSettings saved=t.settings;
        touch_ui_open(&t); if (editor_mode) tap(TU_EDIT);
        touch_ui_action(&t,TU_OPACITY_UP);
        switch (reason) {
        case 0: touch_cancel(&t); break;
        case 1: touch_configure(&t,1920,1080,true,true,true); break;
        case 2: touch_configure(&t,1920,1080,false,true,false); break;
        case 3: touch_configure(&t,1080,1920,true,true,false); break;
        case 4: touch_set_mode(&t,TOUCH_CURSOR); break;
        }
        assert(t.panel==TOUCH_UI_CLOSED); same(&t.settings,&saved);
        touch_configure(&t,1920,1080,true,true,false);
        gameplay_edges=0;
        touch_finger(&t,TOUCH_UP,1,80,1500,700,50);
        touch_finger(&t,TOUCH_MOTION,1,80,1700,800,60);
        assert(!gameplay_edges && !t.fire_count && !t.sprint_latched);
        same(&t.settings,&saved);
    }
}
int main(int argc,char **argv) {
    assert(argc==2); path=argv[1];
    persistence(); editor(); scaled_hits_and_sensitivity(); resets_and_controller();
    remove(path);
    puts("TOUCH_ALPHA3_TESTS=PASS (persistence, migration, editor, bounds, sensitivity, stick, resets)");
    return 0;
}

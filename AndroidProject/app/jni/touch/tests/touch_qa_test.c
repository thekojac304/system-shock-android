/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "touch_controls.h"
#include "touch_visual_compare.h"
#include "qa_spawn.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int command=-1,commands,game_edges,compare_command=-1,compare_commands;
static void output(void *u,TouchOutput e,float x,float y) {
    (void)u; (void)y;
    if (e==TOUCH_QA_COMMAND) { command=(int)x; ++commands; }
    if (e==TOUCH_VISUAL_COMPARE) { compare_command=(int)x; ++compare_commands; }
    if (e==TOUCH_FIRE_DOWN || e==TOUCH_ACTION_DOWN || e==TOUCH_POINTER_DOWN ||
        e==TOUCH_POINTER_TAP || e==TOUCH_USE_PRESS) ++game_edges;
}
static void tap(TouchControls *t,TouchRect r,int finger) {
    assert(r.w>0 && r.h>0);
    touch_finger(t,TOUCH_DOWN,1,finger,r.x+r.w/2,r.y+r.h/2,1);
    touch_finger(t,TOUCH_UP,1,finger,r.x+r.w/2,r.y+r.h/2,2);
}
int main(int argc,char **argv) {
    static uint8_t tiles[64*64*16];
    if (argc==4) {
        FILE *f=fopen(argv[1],"rb"); assert(f);
        assert(fread(tiles,1,sizeof(tiles),f)==sizeof(tiles)); fclose(f);
        int x,y,z;
        assert(qa_spawn_find(tiles,atoi(argv[2]),atoi(argv[3])!=0,&x,&y,&z));
        printf("SPAWN=%d,%d,%d\n",x,y,z);
        return 0;
    }
    int x,y,z;
    assert(!qa_spawn_find(tiles,3,false,&x,&y,&z));
    for (int yy=20;yy<=22;++yy) for (int xx=20;xx<=22;++xx)
        tiles[(yy*64+xx)*16]=1;
    assert(qa_spawn_find(tiles,3,false,&x,&y,&z) && x==21 && y==21 && z==23);
    tiles[(21*64+21)*16+1]=128; /* radiation/bio and objects rejected */
    assert(!qa_spawn_tile(tiles,21,21,3,false));
    assert(qa_spawn_tile(tiles,21,21,3,true));
    tiles[(21*64+21)*16+1]=0;
    tiles[(21*64+21)*16+4]=1;
    assert(!qa_spawn_tile(tiles,21,21,3,false));
    tiles[(21*64+21)*16+4]=0;
    tiles[(21*64+21)*16+2]=30; /* ceiling down from 31, not absolute */
    assert(!qa_spawn_tile(tiles,21,21,3,false));
    int mode=0;
    const int modes[]={2,3,4,0};
    for (int i=0;i<4;++i) assert((mode=touch_compare_next_mode(mode))==modes[i]);
    assert(touch_compare_next_mode(1)==2);
    for (int level=1;level<=6;level+=5) {
        int enabled=0,choice=0,first=level==1?1:4;
        const int choices[]={0,first,first+1,first+2,0};
        const int flags[]={1,1,1,1,0};
        for (int i=0;i<5;++i) {
            TouchPostChoice next=touch_compare_next_post(level,enabled,choice);
            enabled=next.enabled; choice=next.choice;
            assert(enabled==flags[i] && choice==choices[i]);
        }
    }
    TouchPostChoice neutral=touch_compare_next_post(2,0,3);
    assert(neutral.enabled && neutral.choice==0);
    neutral=touch_compare_next_post(2,neutral.enabled,neutral.choice);
    assert(!neutral.enabled && neutral.choice==0);
    assert(!strcmp(touch_compare_mode_label(4),"GPU GENERATED"));
    assert(!strcmp(touch_compare_post_label(1,1,1),"MED A"));
    assert(!strcmp(touch_compare_post_label(6,1,4),"EXEC A"));
    assert(!strcmp(touch_compare_post_label(6,1,1),"POST AUTO"));
    assert(!strcmp(touch_compare_post_label(2,1,0),"AUTO RESEARCH"));
    assert(!strcmp(touch_compare_post_label(5,1,0),"AUTO FLIGHT"));
    assert(!strcmp(touch_compare_post_label(9,1,0),"AUTO BRIDGE"));
    assert(!strcmp(touch_compare_post_label(11,1,0),"AUTO GROVE"));
    assert(!strcmp(touch_compare_post_label(12,1,0),"AUTO GROVE"));
    assert(!strcmp(touch_compare_post_label(13,1,0),"AUTO GROVE"));
    assert(!strcmp(touch_compare_post_label(10,1,0),"POST AUTO"));
    for (int orientation=0;orientation<2;++orientation) {
        TouchControls t;
        touch_init(&t,output,NULL);
        touch_configure(&t,orientation ? 960 : 1920,orientation ? 1920 : 960,true,true,false);
        t.compare_enabled=true;
        int before_compare=compare_commands, before_game=game_edges;
        for (int button=0;button<2;++button) {
            TouchRect b=touch_compare_bounds(&t,button);
            assert(b.x>=0 && b.y>=0 && b.x+b.w<=t.layout.width &&
                   b.y+b.h<=t.layout.height);
            tap(&t,b,button+20);
            assert(compare_commands==before_compare+button+1 && compare_command==button);
            assert(game_edges==before_game && t.move_owner<0 && t.look_owner<0 &&
                   t.cursor_owner<0 && t.fire_count==0 && t.use_count==0);
        }
        TouchRect compare=touch_compare_bounds(&t,0);
        touch_finger(&t,TOUCH_DOWN,1,30,compare.x+compare.w/2,compare.y+compare.h/2,1);
        touch_finger(&t,TOUCH_UP,1,30,compare.x+compare.w/2+compare.w,compare.y+compare.h/2,2);
        assert(compare_commands==before_compare+2 && game_edges==before_game);
        touch_set_mode(&t,TOUCH_CURSOR);
        tap(&t,touch_compare_bounds(&t,1),31);
        assert(compare_commands==before_compare+3 && game_edges==before_game);
        touch_set_mode(&t,TOUCH_GAMEPLAY);
        TouchLayout normal=t.layout;
        tap(&t,touch_control_bounds(&t,TC_SETTINGS),1);
        assert(t.panel==TOUCH_UI_SETTINGS && touch_ui_bounds(&t,TU_QA).w==0);
        touch_ui_action(&t,TU_QA);
        assert(t.panel==TOUCH_UI_SETTINGS);
        t.qa_enabled=true;
        tap(&t,touch_ui_bounds(&t,TU_QA),2);
        assert(t.panel==TOUCH_UI_QA && !t.sprint_latched && !t.radial.open);
        int before=game_edges;
        for (int a=TU_QA_GOD;a<=TU_QA_DECK_LAST;++a) {
            if (a==TU_QA_CLOSE) continue;
            TouchRect r=touch_ui_bounds(&t,(TouchUiAction)a);
            assert(r.x>0 && r.y>0 && r.x+r.w<t.layout.width && r.y+r.h<t.layout.height);
            int count=commands;
            tap(&t,r,3);
            assert(commands==count+1 && command==a);
        }
        /* Multi-finger taps over gameplay controls cannot acquire ownership. */
        touch_finger(&t,TOUCH_DOWN,1,7,1,1,4);
        tap(&t,touch_control_bounds(&t,TC_FIRE),8);
        touch_finger(&t,TOUCH_UP,1,7,1,1,5);
        assert(game_edges==before && t.fire_count==0 && t.move_owner<0);
        tap(&t,touch_ui_bounds(&t,TU_QA_CLOSE),4);
        assert(t.panel==TOUCH_UI_CLOSED && t.mode==TOUCH_GAMEPLAY);
        assert(!memcmp(&normal,&t.layout,sizeof(normal)));
        tap(&t,touch_control_bounds(&t,TC_FIRE),5);
        assert(game_edges>before);
        tap(&t,touch_control_bounds(&t,TC_SETTINGS),1);
        tap(&t,touch_ui_bounds(&t,TU_QA),2);
        touch_configure(&t,t.layout.width,t.layout.height,false,true,false);
        assert(t.panel==TOUCH_UI_CLOSED); /* lifecycle cancellation */
    }
    puts("QA1_TESTS=PASS (gating, touch-only workflow, all decks, capture, lifecycle, spawn rejection)");
    return 0;
}

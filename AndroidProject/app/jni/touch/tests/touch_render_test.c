/* Real overlay drawing code against an SDL command recorder: state restoration
   and the shared draw/hit-test geometry are deterministic without a GPU. */
#include <SDL.h>
#include "touch_controls.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static Uint8 color[4]={12,34,56,78};
static SDL_BlendMode blend=SDL_BLENDMODE_ADD;
static SDL_Rect clip={11,22,33,44};
static SDL_bool clipped=SDL_TRUE;
static int draws, fire_center_lines, ammo_center_lines;
static Uint8 max_alpha;
static FILE *capture;
static TouchControls t;
static float sx=1.25f, sy=1.25f;
static SDL_Rect viewport={320,0,1280,960};
Uint32 SDL_GetTicks(void) { return 1000; }
void SDL_RenderGetScale(SDL_Renderer *r,float *x,float *y) {(void)r; *x=sx; *y=sy;}
int SDL_RenderSetScale(SDL_Renderer *r,float x,float y) {(void)r; sx=x; sy=y; return 0;}
void SDL_RenderGetViewport(SDL_Renderer *r,SDL_Rect *v) {
    (void)r; *v=(SDL_Rect){(int)(viewport.x/sx),(int)(viewport.y/sy),
        (int)(viewport.w/sx),(int)(viewport.h/sy)};
}
int SDL_RenderSetViewport(SDL_Renderer *r,const SDL_Rect *v) {
    (void)r;
    viewport=v ? (SDL_Rect){(int)(v->x*sx),(int)(v->y*sy),(int)(v->w*sx),(int)(v->h*sy)}
               : (SDL_Rect){0,0,1920,960}; return 0;
}
SDL_bool SDL_RenderIsClipEnabled(SDL_Renderer *r) {(void)r; return clipped;}
void SDL_RenderGetClipRect(SDL_Renderer *r,SDL_Rect *c) {(void)r; *c=clip;}
int SDL_RenderSetClipRect(SDL_Renderer *r,const SDL_Rect *c) {
    (void)r; clipped=c ? SDL_TRUE : SDL_FALSE;
    if(c) clip=*c; else memset(&clip,0,sizeof(clip)); return 0;
}
int SDL_GetRenderDrawColor(SDL_Renderer *r,Uint8 *red,Uint8 *g,Uint8 *b,Uint8 *a) {
    (void)r; *red=color[0]; *g=color[1]; *b=color[2]; *a=color[3]; return 0;
}
int SDL_SetRenderDrawColor(SDL_Renderer *r,Uint8 red,Uint8 g,Uint8 b,Uint8 a) {
    (void)r; color[0]=red; color[1]=g; color[2]=b; color[3]=a; return 0;
}
int SDL_GetRenderDrawBlendMode(SDL_Renderer *r,SDL_BlendMode *b) {(void)r; *b=blend; return 0;}
int SDL_SetRenderDrawBlendMode(SDL_Renderer *r,SDL_BlendMode b) {(void)r; blend=b; return 0;}
int SDL_RenderDrawLineF(SDL_Renderer *r,float x1,float y1,float x2,float y2) {
    (void)r; assert(!clipped && blend==SDL_BLENDMODE_BLEND); draws++;
    if (color[3]>max_alpha) max_alpha=color[3];
    if (capture) fprintf(capture,"[\"line\",%u,%u,%u,%u,%.3f,%.3f,%.3f,%.3f]\n",
        color[0],color[1],color[2],color[3],x1,y1,x2,y2);
    assert(sx==1 && sy==1 && viewport.x==0 && viewport.w==1920);
    if(y1==t.layout.fire.y && y2==y1 && x1<t.layout.fire.x && x2>t.layout.fire.x) fire_center_lines++;
    TouchCircle ammo=t.layout.action[TOUCH_AMMO];
    if(y1==ammo.y && y2==y1 && x1<ammo.x && x2>ammo.x) ammo_center_lines++;
    return 0;
}
int SDL_RenderFillRectF(SDL_Renderer *r,const SDL_FRect *rect) {
    (void)r; (void)rect; assert(!clipped && blend==SDL_BLENDMODE_BLEND); draws++;
    if (color[3]>max_alpha) max_alpha=color[3];
    if (capture) fprintf(capture,"[\"fill\",%u,%u,%u,%u,%.3f,%.3f,%.3f,%.3f]\n",
        color[0],color[1],color[2],color[3],rect->x,rect->y,rect->w,rect->h);
    return 0;
}
int SDL_RenderDrawRectF(SDL_Renderer *r,const SDL_FRect *rect) {
    (void)r; draws++;
    if (color[3]>max_alpha) max_alpha=color[3];
    if (capture) fprintf(capture,"[\"outline\",%u,%u,%u,%u,%.3f,%.3f,%.3f,%.3f]\n",
        color[0],color[1],color[2],color[3],rect->x,rect->y,rect->w,rect->h);
    return 0;
}
static void restored(void) {
    assert(color[0]==12 && color[1]==34 && color[2]==56 && color[3]==78);
    assert(clipped && clip.x==11 && clip.y==22 && clip.w==33 && clip.h==44);
    assert(blend==SDL_BLENDMODE_ADD);
    assert(sx==1.25f && sy==1.25f && viewport.x==320 && viewport.w==1280 && viewport.h==960);
}
int main(int argc,char **argv) {
    touch_init(&t,NULL,NULL); touch_configure(&t,1920,960,true,true,false);
    touch_draw(&t,NULL); assert(draws>0 && fire_center_lines>0); restored();
    assert(t.layout.fire.x>1600); /* fire is drawn/hit in the right black bar */
    touch_finger(&t,TOUCH_DOWN,1,1,t.layout.fire.x,t.layout.fire.y,0);
    assert(t.fire_count==1); /* drawn center is also the touch hit target */
    touch_set_mode(&t,TOUCH_CURSOR); draws=fire_center_lines=ammo_center_lines=0;
    touch_draw(&t,NULL); assert(draws>0 && !fire_center_lines && ammo_center_lines>0); restored();
    assert(touch_control_hit(&t,TC_AMMO,t.layout.action[TOUCH_AMMO].x,t.layout.action[TOUCH_AMMO].y));
    touch_configure(&t,1024,768,true,true,true); draws=0;
    touch_draw(&t,NULL); assert(!draws); restored();
    touch_configure(&t,1024,768,true,false,false); draws=0;
    touch_draw(&t,NULL); assert(!draws); restored(); /* movie/menu: no overlay */
    touch_configure(&t,1920,960,true,true,false);
    t.settings.opacity=.25f; max_alpha=0;
    /* Low-opacity controls retain readable labels at a 60% alpha floor. */
    touch_draw(&t,NULL); assert(max_alpha>=140 && max_alpha<=150); restored();
    touch_ui_open(&t); max_alpha=0;
    touch_draw(&t,NULL); assert(max_alpha==255); restored(); /* panel remains readable */
    touch_ui_action(&t,TU_EDIT); t.selected=TC_FIRE; draws=fire_center_lines=0;
    touch_ui_action(&t,TU_SCALE_UP);
    touch_draw(&t,NULL); assert(fire_center_lines>0); restored();
    touch_ui_action(&t,TU_DOCK);
    touch_draw(&t,NULL); restored();
    if (argc==2) {
        static const char *names[]={"gameplay","settings","editor-center","editor-top","radial","options","qa-settings","qa","ammo-look","ammo-cursor"};
        for (int i=0;i<10;i++) {
            char filename[1024];
            snprintf(filename,sizeof(filename),"%s-%s.jsonl",argv[1],names[i]);
            capture=fopen(filename,"wb"); assert(capture);
            touch_init(&t,NULL,NULL); touch_configure(&t,1920,960,true,true,false);
            if (i>0 && i<4) touch_ui_open(&t);
            if (i>1 && i<4) touch_ui_action(&t,TU_EDIT);
            if (i==3) touch_ui_action(&t,TU_DOCK);
            if (i==5) { touch_ui_open(&t); touch_ui_action(&t,TU_PAGE); }
            if (i==6 || i==7) {
                t.qa_enabled=true;
                touch_ui_open(&t);
                if (i==7) touch_ui_action(&t,TU_QA);
            }
            if (i==4) {
                TouchRadialEntry entries[3]={{.id=1,.label="PISTOL",.available=true,.current=true},
                    {.id=2,.label="SPARQ",.available=true},
                    {.id=3,.label="RIFLE",.available=false}};
                assert(touch_radial_open(&t.radial,0,TOUCH_RADIAL_WEAPON,1920,960,entries,3));
                touch_radial_highlight(&t.radial,0,t.radial.cx,t.radial.cy-t.radial.outer_radius*.7f);
            }
            if (i>=8) {
                if (i==9) touch_set_mode(&t,TOUCH_CURSOR);
                TouchRadialEntry entries[3]={
                    {.id=TOUCH_AMMO_RELOAD_ID(2,5),.label="RELOAD",.available=true},
                    {.id=TOUCH_AMMO_TYPE_ID(2,5,0),.label="STANDARD",.available=true,.current=true,.count=180},
                    {.id=TOUCH_AMMO_TYPE_ID(2,5,1),.label="PENETRATOR",.available=false,.count=0}};
                assert(touch_radial_open(&t.radial,0,TOUCH_RADIAL_AMMO,1920,960,entries,3));
                touch_radial_highlight(&t.radial,0,t.radial.cx,t.radial.cy-t.radial.outer_radius*.7f);
            }
            touch_draw(&t,NULL); restored();
            fclose(capture); capture=NULL;
        }
    }
    puts("TOUCH_RENDER_TESTS=PASS (surface restore, geometry, suppression, opacity, settings/editor)"); return 0;
}

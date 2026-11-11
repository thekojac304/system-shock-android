/* Full-surface touch UI; original logical rendering is restored after drawing.
   SPDX-License-Identifier: GPL-3.0-or-later */
#include "touch_controls.h"
#include "touch_visual_compare.h"
#include <SDL.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static void text(SDL_Renderer *r, const char *s, float cx, float cy, float size, float max_width) {
    static const char letters[]="ABCDEFGHIJKLMNOPQRSTUVWXYZ<>0123456789.-+%/";
    static const unsigned char glyph[][5]={
        {2,5,7,5,5},{6,5,6,5,6},{3,4,4,4,3},{6,5,5,5,6},
        {7,4,6,4,7},{7,4,6,4,4},{3,4,5,5,3},{5,5,7,5,5},
        {7,2,2,2,7},{1,1,1,5,2},{5,5,6,5,5},{4,4,4,4,7},
        {5,7,7,5,5},{5,7,7,7,5},{2,5,5,5,2},{6,5,6,4,4},
        {2,5,5,7,3},{6,5,6,5,5},{3,4,2,1,6},{7,2,2,2,2},
        {5,5,5,5,7},{5,5,5,5,2},{5,5,7,7,5},{5,5,2,5,5},
        {5,5,2,2,2},{7,1,2,4,7},{1,2,4,2,1},{4,2,1,2,4},
        {7,5,5,5,7},{2,6,2,2,7},{6,1,2,4,7},{6,1,2,1,6},
        {5,5,7,1,1},{7,4,6,1,6},{3,4,7,5,7},{7,1,2,2,2},
        {7,5,7,5,7},{7,5,7,1,6},{0,0,0,0,2},{0,0,7,0,0},
        {0,2,7,2,0},{5,1,2,4,5},{1,1,2,4,4}
    };
    size=fminf(size,max_width/fmaxf(1,(float)strlen(s)*4));
    float x=cx-(float)strlen(s)*4*size/2;
    for (; *s; ++s,x+=4*size) {
        const char *p=strchr(letters,*s);
        if (!p) continue;
        for (int y=0;y<5;++y) for (int col=0;col<3;++col)
            if (glyph[p-letters][y] & (1u<<(2-col))) {
                SDL_FRect rect={x+col*size,cy-2.5f*size+y*size,size,size};
                SDL_RenderFillRectF(r,&rect);
            }
    }
}
static void color(SDL_Renderer *r, int red, int green, int blue, int alpha, float opacity) {
    SDL_SetRenderDrawColor(r,(Uint8)red,(Uint8)green,(Uint8)blue,(Uint8)lroundf(alpha*opacity));
}
static void rect(SDL_Renderer *r, TouchRect b, bool fill) {
    SDL_FRect v={b.x,b.y,b.w,b.h};
    if (fill) SDL_RenderFillRectF(r,&v); else SDL_RenderDrawRectF(r,&v);
}
static void circle(SDL_Renderer *r, TouchCircle c, bool held, bool selected, float opacity, float stroke) {
    color(r,selected ? 86 : held ? 35 : 12,
          selected ? 62 : held ? 127 : 39,
          selected ? 27 : held ? 143 : 53,
          selected ? 205 : held ? 230 : 145,opacity);
    for (int y=-(int)c.radius;y<=(int)c.radius;++y) {
        float span=sqrtf(fmaxf(0,c.radius*c.radius-y*y));
        SDL_RenderDrawLineF(r,c.x-span,c.y+y,c.x+span,c.y+y);
    }
    color(r,selected ? 255 : held ? 235 : 173,
          selected ? 211 : held ? 250 : 226,
          selected ? 99 : held ? 252 : 232,selected ? 255 : 235,opacity);
    for (int line=0;line<(int)stroke;++line) for (int i=0;i<64;++i) {
        float a=i*6.2831853f/64,b=(i+1)*6.2831853f/64,rad=c.radius-line;
        SDL_RenderDrawLineF(r,c.x+cosf(a)*rad,c.y+sinf(a)*rad,
                           c.x+cosf(b)*rad,c.y+sinf(b)*rad);
    }
}
static void radial(const TouchControls *t,SDL_Renderer *r) {
    const TouchRadialSelector *wheel=&t->radial;
    if (!wheel->open) return;
    float R=wheel->outer_radius,inner=wheel->inner_radius;
    TouchCircle outer={wheel->cx,wheel->cy,R};
    color(r,8,22,30,205,fmaxf(.65f,t->settings.opacity));
    for (int y=-(int)R;y<=(int)R;++y) {
        float span=sqrtf(fmaxf(0,R*R-y*y));
        SDL_RenderDrawLineF(r,outer.x-span,outer.y+y,outer.x+span,outer.y+y);
    }
    if (wheel->count) for (int i=0;i<wheel->count;++i) {
        float angle=6.2831853f*i/wheel->count-1.5707963f;
        float half=3.14159265f/wheel->count;
        const TouchRadialEntry *entry=&wheel->entries[i];
        if (i==wheel->highlighted || entry->current) {
            bool chosen=i==wheel->highlighted;
            color(r,chosen ? 39 : 54,chosen ? 100 : 81,chosen ? 116 : 85,
                  chosen ? 225 : 170,fmaxf(.7f,t->settings.opacity));
            for (float a=angle-half+.008f;a<angle+half-.008f;a+=1.0f/R)
                SDL_RenderDrawLineF(r,wheel->cx+cosf(a)*inner,wheel->cy+sinf(a)*inner,
                                    wheel->cx+cosf(a)*(R-2),wheel->cy+sinf(a)*(R-2));
        }
        color(r,entry->available ? 188 : 95,entry->available ? 231 : 115,
              entry->available ? 235 : 120,220,fmaxf(.7f,t->settings.opacity));
        float edge=angle-half;
        SDL_RenderDrawLineF(r,wheel->cx+cosf(edge)*inner,wheel->cy+sinf(edge)*inner,
                            wheel->cx+cosf(edge)*R,wheel->cy+sinf(edge)*R);
        char label[sizeof(entry->label)];
        snprintf(label,sizeof(label),"%s",entry->label);
        for (char *p=label;*p;++p) if (*p>='a' && *p<='z') *p=(char)(*p-'a'+'A');
        float x=wheel->cx+cosf(angle)*R*.68f,y=wheel->cy+sinf(angle)*R*.68f;
        color(r,entry->available ? 237 : 132,entry->available ? 249 : 145,
              entry->available ? 247 : 150,255,fmaxf(.7f,t->settings.opacity));
        text(r,label,x,y,t->layout.height/270,R*.64f);
        if (entry->current) {
            color(r,255,213,110,255,fmaxf(.7f,t->settings.opacity));
            text(r,"ACTIVE",x,y-R*.13f,t->layout.height/410,R*.5f);
        }
        if (wheel->category!=TOUCH_RADIAL_WEAPON &&
            !(wheel->category==TOUCH_RADIAL_AMMO && TOUCH_AMMO_IS_RELOAD(entry->id))) {
            char count[16];
            snprintf(count,sizeof(count),"X%u",entry->count);
            color(r,217,232,232,245,fmaxf(.7f,t->settings.opacity));
            text(r,count,x,y+R*.13f,t->layout.height/390,R*.42f);
        }
    }
    color(r,179,220,224,245,fmaxf(.7f,t->settings.opacity));
    for (int i=0;i<128;++i) {
        float a=i*6.2831853f/128,b=(i+1)*6.2831853f/128;
        SDL_RenderDrawLineF(r,wheel->cx+cosf(a)*R,wheel->cy+sinf(a)*R,
                            wheel->cx+cosf(b)*R,wheel->cy+sinf(b)*R);
    }
    circle(r,(TouchCircle){wheel->cx,wheel->cy,inner},false,false,1,fmaxf(1,R/110));
    color(r,241,243,228,255,1);
    text(r,"CANCEL",wheel->cx,wheel->cy,t->layout.height/290,inner*1.6f);
}
static bool held(const TouchControls *t,int id) {
    if (id==TC_FIRE) return t->fire_count>0;
    if (id==TC_USE) return t->use_count>0;
    if (id==TC_STICK) return t->move_owner>=0;
    if (id==TC_AMMO) {
        for (int i=0;i<TOUCH_MAX_FINGERS;++i)
            if (t->fingers[i].active && t->fingers[i].action==TOUCH_AMMO &&
                (t->fingers[i].radial_pending || t->fingers[i].radial_opened)) return true;
        return false;
    }
    if (id==TC_SPRINT) return t->sprint_latched;
    return id>=TC_JUMP && id<=TC_PAUSE && t->action_count[id-TC_JUMP]>0;
}
static void controls(const TouchControls *t,SDL_Renderer *r) {
    bool preview=t->panel!=TOUCH_UI_CLOSED;
    float h=t->layout.height,stroke=fmaxf(1,roundf(h/384));
    for (int id=0;id<TC_COUNT;++id) {
        if (!preview) {
            if (id==TC_PAUSE && !t->pause_available) continue;
            if (id!=TC_PAUSE && id!=TC_SETTINGS && !t->gameplay_available) continue;
            if (id!=TC_PAUSE && id!=TC_SETTINGS && id!=TC_MODE && id!=TC_AMMO &&
                t->effective_mode!=TOUCH_GAMEPLAY) continue;
        }
        TouchRect b=touch_control_bounds(t,id);
        bool selected=t->panel==TOUCH_UI_EDITOR && t->selected==id;
        float opacity=selected ? fmaxf(.85f,t->settings.opacity) : t->settings.opacity;
        bool down=!preview && held(t,id);
        const char *name=touch_defaults[id].label;
        if (id==TC_MODE) {
            color(r,t->mode==TOUCH_GAMEPLAY ? 15 : 59,
                  t->mode==TOUCH_GAMEPLAY ? 58 : 59,
                  t->mode==TOUCH_GAMEPLAY ? 70 : 82,190,opacity); rect(r,b,true);
            bool cursor=t->mode==TOUCH_CURSOR;
            color(r,selected || cursor ? 255 : 173,selected || cursor ? 211 : 226,
                  selected || cursor ? 99 : 232,240,opacity);
            for (int line=0;line<(int)stroke;++line) {
                TouchRect outline={b.x+line,b.y+line,b.w-2*line,b.h-2*line}; rect(r,outline,false);
            }
            name=t->mode==TOUCH_GAMEPLAY ? "LOOK" : "CURSOR";
        } else {
            TouchCircle c={b.x+b.w/2,b.y+b.h/2,b.w/2};
            if (id==TC_STICK && !preview && t->move_owner>=0) { c.x=t->anchor_x; c.y=t->anchor_y; }
            circle(r,c,down,selected,opacity,stroke);
            if (id==TC_STICK) {
                TouchCircle nub={c.x+t->movement_x*c.radius,c.y+t->movement_y*c.radius,c.radius*.37f};
                circle(r,nub,down,false,opacity,stroke);
                if (!preview) continue;
            }
            if (id==TC_SPRINT && down) name="RUN ON";
        }
        color(r,selected ? 255 : 226,selected ? 224 : 245,selected ? 135 : 245,245,
              fmaxf(.6f,opacity));
        text(r,name,b.x+b.w/2,b.y+b.h/2,h/240*t->settings.control[id].scale,b.w*.86f);
    }
}
static void visual_compare(const TouchControls *t,SDL_Renderer *r) {
    if (!t->compare_enabled || !t->gameplay_available || t->controller ||
        t->panel!=TOUCH_UI_CLOSED) return;
    float h=t->layout.height;
    for (int button=0;button<2;++button) {
        TouchRect b=touch_compare_bounds(t,button);
        bool down=t->compare_pressed==button;
        float opacity=fmaxf(.42f,t->settings.opacity*.55f);
        color(r,down?35:12,down?127:39,down?143:53,down?220:145,opacity);
        rect(r,b,true);
        color(r,down?235:173,down?250:226,down?252:232,down?255:235,opacity);
        rect(r,b,false);
        color(r,235,247,246,255,fmaxf(.65f,opacity));
        text(r,button?"POST":"MODE",b.x+b.w*.5f,b.y+b.h*.5f,
             h/230,b.w*.84f);
    }
    Uint32 now=SDL_GetTicks();
    int32_t remaining=(int32_t)(t->compare_notice_until-now);
    if (t->compare_notice<0 || remaining<=0) return;
    float fade=fminf(1.0f,(float)remaining/350.0f);
    const char *label=t->compare_notice==0 ?
        touch_compare_mode_label(t->compare_mode) :
        touch_compare_post_label(t->compare_level,t->compare_post_enabled,
                                 t->compare_post_choice);
    TouchRect b=touch_compare_bounds(t,t->compare_notice);
    TouchRect banner={b.x+b.w+h*.014f,b.y,b.w*1.6f,b.h};
    color(r,8,22,30,170,fade); rect(r,banner,true);
    color(r,230,247,245,255,fade);
    text(r,label,banner.x+banner.w*.5f,banner.y+banner.h*.5f,
         h/250,banner.w*.9f);
}
static const char *ui_label(const TouchControls *t,int action) {
    static const char *decks[]={"MEDICAL","RESEARCH","REACTOR","MAINTENANCE",
        "STORAGE","FLIGHT DECK","EXECUTIVE","ALPHA GROVE","BETA GROVE",
        "DELTA GROVE","ENGINEERING","SECURITY","BRIDGE"};
    if (action==TU_QA) return "QA TOOLS";
    if (action==TU_QA_GOD) return t->qa_god ? "GOD MODE ON" : "GOD MODE OFF";
    if (action==TU_QA_HEAL) return "FULL HEALTH / ENERGY";
    if (action==TU_QA_LOADOUT) return "FULL QA LOADOUT";
    if (action==TU_QA_CLOSE) return "CLOSE";
    if (action>=TU_QA_DECK_FIRST && action<=TU_QA_DECK_LAST) return decks[action-TU_QA_DECK_FIRST];
    static const char *names[TU_COUNT]={"EDIT LAYOUT","-","+","-","+","-","+",
        "INVERT Y OFF","-","+","RESET TOUCH SETTINGS","CANCEL","SAVE / DONE",
        "<",">","SIZE -","SIZE +","SETTINGS","MOVE TOOLBAR","MORE >",
        "HAPTICS OFF","HAPTIC STRENGTH 1","WEAPON WHEEL OFF",
        "CONSUMABLE WHEEL OFF","EXPLOSIVE WHEEL OFF"};
    if (action==TU_INVERT && t->settings.invert_y) return "INVERT Y ON";
    if (action==TU_PAGE) return t->settings_page ? "< BACK" : "MORE >";
    if (action==TU_HAPTICS) return t->settings.haptics_enabled ? "HAPTICS ON" : "HAPTICS OFF";
    if (action==TU_HAPTIC_STRENGTH) {
        static const char *levels[]={"HAPTIC STRENGTH 1","HAPTIC STRENGTH 2","HAPTIC STRENGTH 3"};
        return levels[t->settings.haptic_strength-1];
    }
    if (action==TU_WHEEL_WEAPON) return t->settings.wheel_enabled[0] ? "WEAPON WHEEL ON" : "WEAPON WHEEL OFF";
    if (action==TU_WHEEL_CONSUMABLE) return t->settings.wheel_enabled[1] ? "CONSUMABLE WHEEL ON" : "CONSUMABLE WHEEL OFF";
    if (action==TU_WHEEL_EXPLOSIVE) return t->settings.wheel_enabled[2] ? "EXPLOSIVE WHEEL ON" : "EXPLOSIVE WHEEL OFF";
    if (t->panel==TOUCH_UI_EDITOR) {
        if (action==TU_OPACITY_DOWN) return "OPACITY -";
        if (action==TU_OPACITY_UP) return "OPACITY +";
        if (action==TU_RESET) return "RESET DEFAULTS";
    }
    return names[action];
}
static void panel(const TouchControls *t,SDL_Renderer *r) {
    float w=t->layout.width,h=t->layout.height;
    char value[100];
    if (t->panel==TOUCH_UI_QA) {
        color(r,4,10,17,245,1); rect(r,(TouchRect){0,0,w,h},true);
        color(r,208,241,244,255,1);
        text(r,"QA TOOLS",w*.5f,h*.07f,h/185,w*.5f);
        text(r,t->qa_status ? t->qa_status : "",w*.5f,h*.12f,h/300,w*.80f);
        text(r,"TELEPORT",w*.5f,h*.275f,h/300,w*.5f);
    } else if (t->panel==TOUCH_UI_SETTINGS) {
        color(r,4,10,17,160,1); rect(r,(TouchRect){0,0,w,h},true);
        color(r,15,28,40,250,1); rect(r,(TouchRect){w*.18f,h*.035f,w*.64f,h*.94f},true);
        color(r,208,241,244,255,1);
        text(r,"TOUCH SETTINGS",w*.5f,h*.09f,h/185,w*.5f);
        const float values[4]={t->settings.opacity*100,t->settings.sensitivity_x,
                              t->settings.sensitivity_y,t->settings.control[TC_STICK].scale};
        const char *names[4]={"OPACITY","LOOK X","LOOK Y","STICK SIZE"};
        const float rows[4]={.3075f,.4075f,.5075f,.7075f};
        for (int i=0;i<4 && !t->settings_page;++i) {
            if (i==0) snprintf(value,sizeof(value),"%s %.0f%%",names[i],values[i]);
            else snprintf(value,sizeof(value),"%s %.2f",names[i],values[i]);
            text(r,value,w*.5f,h*rows[i],h/255,w*.33f);
        }
    } else {
        if (t->ui_drag) return; /* captured drag can cross either toolbar edge */
        float y=t->toolbar_top ? 0 : h*.28f;
        color(r,11,23,34,248,1); rect(r,(TouchRect){0,y,w,h*.17f},true);
        color(r,240,231,190,255,1);
        snprintf(value,sizeof(value),"%s %.1f",touch_defaults[t->selected].key,t->settings.control[t->selected].scale);
        /* Keys are lowercase on disk; labels remain readable in the bitmap font. */
        for (char *p=value;*p;++p) if (*p>='a' && *p<='z') *p=(char)(*p-'a'+'A');
        text(r,value,w*.175f,y+h*.0445f,h/290,w*.19f);
        color(r,213,239,241,220,1);
        snprintf(value,sizeof(value),"DRAG A CONTROL   OPACITY %.0f%%",t->settings.opacity*100);
        text(r,value,w*.5f,t->toolbar_top ? h*.20f : h*.25f,h/330,w*.75f);
    }
    for (int a=0;a<TU_COUNT;++a) {
        TouchRect b=touch_ui_bounds(t,(TouchUiAction)a);
        if (b.w<=0) continue;
        bool down=t->ui_owned && t->ui_action==a;
        color(r,down ? 43 : 26,down ? 119 : 51,down ? 129 : 65,255,1); rect(r,b,true);
        color(r,a==TU_SAVE ? 105 : 115,a==TU_SAVE ? 222 : 161,190,255,1); rect(r,b,false);
        color(r,227,244,245,255,1);
        text(r,ui_label(t,a),b.x+b.w/2,b.y+b.h/2,h/270,b.w*.88f);
    }
    if (t->save_failed) {
        color(r,255,153,132,255,1);
        text(r,"SAVE FAILED - RETRY OR CANCEL",w*.5f,h*.125f,h/310,w*.60f);
    }
}
void touch_draw(const TouchControls *t,SDL_Renderer *r) {
    if (!t->enabled || (!t->gameplay_available && !t->pause_available) || t->controller) return;
    Uint8 red,green,blue,alpha;
    SDL_BlendMode blend;
    SDL_Rect clip,viewport;
    float sx,sy;
    SDL_bool clipped=SDL_RenderIsClipEnabled(r);
    SDL_RenderGetClipRect(r,&clip);
    SDL_GetRenderDrawColor(r,&red,&green,&blue,&alpha);
    SDL_GetRenderDrawBlendMode(r,&blend);
    SDL_RenderGetScale(r,&sx,&sy);
    SDL_RenderSetScale(r,1,1);
    SDL_RenderGetViewport(r,&viewport);
    SDL_RenderSetViewport(r,NULL);
    SDL_RenderSetClipRect(r,NULL);
    SDL_SetRenderDrawBlendMode(r,SDL_BLENDMODE_BLEND);
    controls(t,r);
    if (t->panel==TOUCH_UI_CLOSED) visual_compare(t,r);
    if (t->panel==TOUCH_UI_CLOSED) radial(t,r);
    if (t->panel!=TOUCH_UI_CLOSED) panel(t,r);
    SDL_SetRenderDrawBlendMode(r,blend);
    SDL_SetRenderDrawColor(r,red,green,blue,alpha);
    SDL_RenderSetViewport(r,&viewport);
    SDL_RenderSetScale(r,sx,sy);
    SDL_RenderSetClipRect(r,clipped ? &clip : NULL);
}

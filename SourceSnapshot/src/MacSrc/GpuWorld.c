/* SPDX-License-Identifier: GPL-3.0-or-later
 * Optional ordered GLES world backend. Software capture runs once and
 * remains authoritative until the entire GPU view has succeeded. No readback. */
#include "GpuWorld.h"
#include "GpuTextureCache.h"
#include "GpuGenerated.h"
#include "GpuPost.h"
#include "GpuFlicker.h"
#include "GpuGeometry.h"
#include "GlobalV.h"
#include "Prefs.h"
#include "Shock.h"
#include "ShockBitmap.h"
#include "frtypes.h"
#include "frintern.h"
#include "gameobj.h"
#include "otrip.h"
#include "frflags.h"
#include "textmaps.h"
#include "horror_profile.h"
#include "map.h"
#include "mapflags.h"
#include "player.h"
#include "drugs.h"
#include "mainloop.h"
#include "faketime.h"
#include "gr2ss.h"
#include <GLES3/gl3.h>
#include <EGL/egl.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

extern SDL_Renderer *renderer;
extern bool UseCutscenePalette, DoubleSize;
extern uchar full_game_3d;
extern char curr_clut_table;
extern uchar view360_is_rendering;
extern uchar view360_render_on;
extern uchar wrapper_panel_on;
extern int _game_fr_tmap;
extern int secret_render_fx;

enum { MAX_DRAWS=4096, MAX_VERTICES=32768, MAX_STAR_PIXELS=16384, MAX_TEXTURES=GW_CACHE_MAX_SLOTS };
typedef struct { float clip[4], world[3], uv[2], darkness; } Vertex;
typedef struct { int first, count, texture, cutout, emissive, receivers, material_id,mask_row,ambient_tile,source_light; float source_multiplier; GpuSurfaceId surface; const GgSurface *generated_surface; } Draw;
typedef struct { float x,y,ceiling_offset,radius,rgb[3],intensity; int tile_x,tile_y; } Light;
static const Light medical_lights[2] = {
    {30.0f,23.8f,0.35f,4.2f,{0.13f,0.8f,1.0f},2.4f,30,23},
    {26.5f,20.5f,0.25f,3.2f,{1.0f,0.48f,0.015f},6.0f,26,20}
};
static const float medical_ambient[3]={0.065f,0.075f,0.085f};
static Vertex vertices[MAX_VERTICES];
static Vertex star_pixels[MAX_STAR_PIXELS];
static int nstar_pixels;
static Draw draws[MAX_DRAWS];
static GwTextureCache texture_cache;
static GLuint texture_gl[MAX_TEXTURES];
static unsigned char texture_retired[MAX_TEXTURES];
static int cache_initialized;
static int active, failed, object_id=-1, object_lit=1, ndraw, nvertex, ready, disabled;
static GpuSurfaceId current_surface;
static int medical_profile;
static GgProfile *generated_profile;
static int generated_level=-1,generated_load_attempted,generated_active;
static unsigned frame, fallbacks, uploads, upload_bytes;
static unsigned generated_draws,generated_refs,generated_max_refs,generated_partials;
static int retained, command_valid, view_w,view_h, gpu_w,gpu_h;
static unsigned char *software_world;
static size_t software_capacity;
static SDL_Rect destination;
static SDL_Texture *world;
static GLuint program,star_program,vao,vbo,palette,mask_texture,depth;
static GLuint post_program,post_vao,post_source;
static SDL_Texture *post_world;
static int post_ready,post_failed;
static GpuFlicker transient_state[512];
static float source_multiplier[512];
static int uploaded_masks_level=-1;
static SDL_GLContext context;
static unsigned char palette_rgb[1024];
static unsigned palette_generation=1, uploaded_palette;
static float inverse[9],eye[3],light_pos[16],light_color[16];
static uint64_t capture_start;
static const char *failure;
static const GpuPostPreset post_presets[] = {
    {"NEUTRAL",1.f,1.f,0.f,0.f,1.f,{1,1,1},{1,1,1},0,0,0,.8f,.3f,0},
    {"MED CLINICAL",1.02f,1.13f,.035f,.12f,.94f,{.78f,.90f,1.05f},{1.02f,1.02f,1.f},.10f,.025f,.10f,.72f,.25f,.007f,
        {.53f,.63f,.68f},2.f,9.f,.070f,.070f,.030f,{-.020f,.004f,.025f},18.f,.004f,
        0.f,0.f,0.f,1.f,1.f,0.f,0.f,0.f,0.f,{0.f,0.f,0.f}},
    {"MED DISEASED",.99f,1.18f,.055f,.16f,.83f,{.77f,1.02f,1.f},{1.f,1.01f,.98f},.14f,.025f,.13f,.69f,.26f,.012f},
    {"MED CINEMATIC",1.03f,1.22f,.04f,.20f,.94f,{.78f,.88f,1.06f},{1.08f,1.03f,.92f},.13f,.08f,.14f,.73f,.26f,.006f},
    {"EXEC DECAY",1.02f,1.17f,.04f,.17f,.94f,{.85f,.91f,1.02f},{1.07f,1.01f,.89f},.10f,.08f,.11f,.74f,.25f,.006f,
        {.47f,.42f,.38f},3.f,12.f,.080f,.050f,.025f,{.005f,-.010f,.012f},26.f,.003f,
        .55f,.030f,.08f,.88f,1.05f,1.55f,.9f,.045f,.04f,{.45f,.42f,.39f}},
    {"EXEC GOLD",1.01f,1.20f,.05f,.22f,.87f,{.92f,.94f,1.f},{1.08f,.99f,.80f},.07f,.11f,.13f,.72f,.25f,.008f},
    {"EXEC CORPORATE",1.f,1.16f,.04f,.18f,.91f,{.86f,.94f,1.03f},{1.06f,1.02f,.95f},.11f,.055f,.09f,.75f,.25f,.005f},
    /* Station-wide Auto archetypes share the Medical/Executive tone philosophy.
     * Generated-light multipliers remain neutral until reviewed light profiles
     * exist for these levels; the post grade still selects by deck. */
    {"REACTOR INDUSTRIAL",1.01f,1.16f,.04f,.16f,.91f,{.98f,.91f,.86f},{1.06f,1.01f,.93f},.08f,.055f,.10f,.74f,.25f,.006f,
        {.49f,.41f,.35f},2.5f,10.f,.085f,.045f,.03f,{.012f,-.006f,-.008f},23.f,.003f,
        .38f,.018f,.045f,1.f,1.f,1.55f,.9f,.055f,.05f,{.48f,.40f,.34f}},
    {"RESEARCH CLINICAL",1.03f,1.11f,.03f,.11f,.96f,{.83f,.98f,1.02f},{.98f,1.04f,1.02f},.08f,.04f,.08f,.74f,.25f,.005f,
        {.50f,.62f,.63f},2.5f,10.f,.06f,.045f,.02f,{-.012f,.012f,.014f},24.f,.002f,
        .25f,.012f,.03f,1.f,1.f,0.f,0.f,0.f,0.f,{0.f,0.f,0.f}},
    {"MAINTENANCE UTILITY",1.01f,1.14f,.04f,.15f,.88f,{.91f,.95f,.85f},{1.02f,1.00f,.93f},.09f,.035f,.11f,.72f,.25f,.007f,
        {.45f,.47f,.39f},2.5f,9.f,.07f,.055f,.025f,{.004f,.002f,-.010f},27.f,.002f,
        .32f,.022f,.05f,1.f,1.f,1.55f,.9f,.025f,.025f,{.43f,.45f,.39f}},
    {"STORAGE WAREHOUSE",1.02f,1.15f,.04f,.15f,.91f,{.86f,.93f,1.02f},{1.00f,1.02f,1.02f},.09f,.03f,.11f,.72f,.25f,.006f,
        {.44f,.52f,.58f},3.f,11.f,.065f,.06f,.025f,{-.010f,.001f,.014f},29.f,.002f,
        .34f,.016f,.055f,1.f,1.f,0.f,0.f,0.f,0.f,{0.f,0.f,0.f}},
    {"FLIGHT METALLIC",1.03f,1.11f,.03f,.12f,.96f,{.88f,.96f,1.03f},{1.02f,1.04f,1.02f},.075f,.045f,.08f,.76f,.25f,.005f,
        {.48f,.57f,.63f},3.5f,13.f,.075f,.055f,.025f,{-.007f,.004f,.013f},31.f,.002f,
        .30f,.018f,.035f,1.f,1.f,0.f,0.f,0.f,0.f,{0.f,0.f,0.f}},
    {"ENGINEERING MACHINERY",1.02f,1.17f,.04f,.16f,.93f,{.88f,.94f,1.01f},{1.06f,1.02f,.92f},.085f,.06f,.10f,.73f,.25f,.006f,
        {.46f,.45f,.42f},2.5f,10.f,.065f,.045f,.025f,{.001f,-.005f,.004f},22.f,.003f,
        .42f,.025f,.05f,1.f,1.f,1.55f,.9f,.03f,.03f,{.43f,.42f,.39f}},
    {"SECURITY SEVERE",1.01f,1.19f,.045f,.18f,.90f,{.89f,.91f,.98f},{1.07f,.97f,.94f},.08f,.05f,.12f,.71f,.25f,.006f,
        {.42f,.44f,.49f},3.f,11.f,.06f,.05f,.02f,{.004f,-.008f,.008f},21.f,.002f,
        .46f,.018f,.055f,1.f,1.f,0.f,0.f,0.f,0.f,{0.f,0.f,0.f}},
    {"BRIDGE ENDGAME",1.02f,1.20f,.045f,.18f,.94f,{.85f,.92f,1.02f},{1.06f,1.04f,1.01f},.09f,.06f,.115f,.73f,.25f,.005f,
        {.42f,.48f,.54f},3.f,12.f,.07f,.05f,.025f,{-.009f,.001f,.013f},30.f,.002f,
        .50f,.022f,.06f,1.f,1.f,1.55f,.9f,.03f,.03f,{.41f,.47f,.53f}},
    {"GROVE BOTANICAL",1.02f,1.12f,.03f,.12f,.97f,{.87f,1.00f,.96f},{1.02f,1.04f,.98f},.075f,.04f,.085f,.76f,.25f,.005f,
        {.44f,.57f,.54f},2.5f,11.f,.075f,.045f,.025f,{-.006f,.007f,.002f},28.f,.002f,
        .28f,.014f,.035f,1.f,1.f,1.55f,.9f,.025f,.025f,{.42f,.52f,.48f}}
};
_Static_assert(sizeof(post_presets)/sizeof(post_presets[0])==16,
    "Every ordinary deck and the separate Groves need an Auto archetype");
const GpuPostPreset *gpu_post_preset(int level,int choice) {
    return &post_presets[gpu_post_resolve(level,choice)];
}
const char *gpu_post_choice_name(int choice) {
    static const char *names[]={"AUTO","MED A","MED B","MED C","EXEC A","EXEC B","EXEC C"};
    return choice>=0&&choice<=6?names[choice]:names[0];
}
static double capture_samples[300],submit_samples[300];
static unsigned timing_samples;
static double post_samples[300];
static unsigned post_timing_samples;
static uint64_t terrain_types;
static int diagnostic_due=1,logged_level=-1;
#ifdef RP5NP_QA_TOOLS
/* Flight's original four-frame terrain family only. This observes the existing
 * selected bitmap/cache entry; it never advances animation, reads pixels back,
 * changes resources, or treats palette changes as texture-frame transitions.
 * A mode selection starts a new bounded probe; a level change/save-time rewind
 * also rearms it. Captured metadata follows retained commands through replay. */
typedef struct {
    int valid,level,x,y,base_slot,group,group_frame,selected_slot,selected_master;
    int texture,width,height,draw_index,bounds[4];
    unsigned capture_frame;
    uint32_t game_time;
    uint64_t hash;
} QaAnimationSample;
static QaAnimationSample qa_animation_pending,qa_animation_last;
static int qa_animation_level=-1;
static unsigned qa_animation_reports;
static uint32_t qa_animation_time;
static void qa_animation_reset(void) {
    memset(&qa_animation_pending,0,sizeof(qa_animation_pending));
    memset(&qa_animation_last,0,sizeof(qa_animation_last));
    qa_animation_level=player_struct.level;
    qa_animation_time=player_struct.game_time;
    qa_animation_reports=0;
}
static void qa_animation_begin(void) {
    if(qa_animation_level!=player_struct.level||player_struct.game_time<qa_animation_time)
        qa_animation_reset();
    qa_animation_time=player_struct.game_time;
    qa_animation_pending.valid=0;
}
static void qa_animation_capture(int texture,int n,const int32_t (*xy)[2]) {
    if(qa_animation_reports>=16||qa_animation_pending.valid||object_id>=0||
       player_struct.level!=5||_game_fr_tmap<0||_game_fr_tmap>=NUM_LOADED_TEXTURES||
       loved_textures[_game_fr_tmap]!=237)return;
    int group=textprops[_game_fr_tmap].anim_group;
    if(group<=0||group>=NUM_ANIM_TEXTURE_GROUPS||textprops[_game_fr_tmap].group_pos!=0||
       animtextures[group].num_frames!=4)return;
    int phase=ANIMTEXT_FRAME(_game_fr_tmap),selected=_game_fr_tmap+phase;
    if(phase<0||phase>=4||selected<0||selected>=NUM_LOADED_TEXTURES)return;
    const GwTextureEntry *entry=&texture_cache.entries[texture];
    /* Flat-color terrain also reaches gpu_world_face with a 1x1 index bitmap. */
    if(entry->w<=1||entry->h<=1)return;
    QaAnimationSample *sample=&qa_animation_pending;
    sample->valid=1;sample->level=player_struct.level;sample->x=_fdt_x;sample->y=_fdt_y;
    sample->base_slot=_game_fr_tmap;sample->group=group;
    sample->group_frame=animtextures[group].current_frame;
    sample->selected_slot=selected;sample->selected_master=loved_textures[selected];
    sample->texture=texture;sample->width=entry->w;sample->height=entry->h;
    sample->hash=entry->hash;sample->draw_index=ndraw-1;
    sample->capture_frame=frame;sample->game_time=player_struct.game_time;
    sample->bounds[0]=sample->bounds[2]=xy[0][0];
    sample->bounds[1]=sample->bounds[3]=xy[0][1];
    for(int i=1;i<n;i++){
        if(xy[i][0]<sample->bounds[0])sample->bounds[0]=xy[i][0];
        if(xy[i][1]<sample->bounds[1])sample->bounds[1]=xy[i][1];
        if(xy[i][0]>sample->bounds[2])sample->bounds[2]=xy[i][0];
        if(xy[i][1]>sample->bounds[3])sample->bounds[3]=xy[i][1];
    }
}
static void qa_animation_composed(void) {
    const QaAnimationSample *sample=&qa_animation_pending,*last=&qa_animation_last;
    if(!sample->valid||qa_animation_reports>=16)return;
    /* Palette generation deliberately does not participate in this comparison. */
    if(last->valid&&sample->group_frame==last->group_frame&&
       sample->selected_slot==last->selected_slot&&sample->selected_master==last->selected_master&&
       sample->width==last->width&&sample->height==last->height&&sample->hash==last->hash)return;
    SDL_Log("GPU2 ANIM composed=1 sample=%u/16 frame=%u game_time=%u level=%d terrain_tile=%d,%d base_slot=%d base_master=237 group=%d group_frame=%d frames=4 selected_slot=%d selected_master=%d cache_slot=%d size=%dx%d index_hash=%016llx palette_generation=%u draw=%d bounds_px=%.6f,%.6f,%.6f,%.6f",
        ++qa_animation_reports,sample->capture_frame,(unsigned)sample->game_time,sample->level,
        sample->x,sample->y,sample->base_slot,sample->group,sample->group_frame,
        sample->selected_slot,sample->selected_master,sample->texture,sample->width,sample->height,
        (unsigned long long)sample->hash,palette_generation,sample->draw_index,
        sample->bounds[0]/65536.,sample->bounds[1]/65536.,sample->bounds[2]/65536.,sample->bounds[3]/65536.);
    qa_animation_last=*sample;
}
#else
#define qa_animation_begin() ((void)0)
#define qa_animation_capture(texture,n,xy) ((void)0)
#define qa_animation_composed() ((void)0)
#define qa_animation_reset() ((void)0)
#endif
static int compare_sample(const void *a,const void *b) {
    double x=*(const double*)a,y=*(const double*)b;return (x>y)-(x<y);
}
static void measure_view(uint64_t begin,uint64_t captured,uint64_t submitted) {
    double scale=1000./SDL_GetPerformanceFrequency();
    capture_samples[timing_samples]=(captured-begin)*scale;
    submit_samples[timing_samples]=(submitted-captured)*scale;
    if(++timing_samples<300)return;
    double cap=0,sub=0;for(int i=0;i<300;i++){cap+=capture_samples[i];sub+=submit_samples[i];}
    qsort(capture_samples,300,sizeof(double),compare_sample);qsort(submit_samples,300,sizeof(double),compare_sample);
    SDL_Log("GPU2 TIMING samples=300 capture_software_mean_ms=%.3f p95=%.3f p99=%.3f submit_cpu_mean_ms=%.3f p95=%.3f p99=%.3f GPU_execution_ms=unknown",cap/300,capture_samples[284],capture_samples[296],sub/300,submit_samples[284],submit_samples[296]);
    timing_samples=0;
}

/* Only state actually changed by this pass is saved. Vertex attributes and
 * EBO live in our private VAO; SDL's VAO/attribute pointers are never edited. */
typedef struct {
    GLint program,vao,vbo,active,tex[3],viewport[4],scissor[4],unpack,row,skiprows,skippixels,pbo;
    GLint draw_fbo,read_fbo,renderbuffer;
    GLboolean blend,depth,stencil,scissor_on,cull,mask[4],depthmask;
    GLfloat clear[4];
} Bridge;
static void bridge_save(Bridge *s) {
    SDL_RenderFlush(renderer);
    glGetIntegerv(GL_CURRENT_PROGRAM,&s->program); glGetIntegerv(GL_VERTEX_ARRAY_BINDING,&s->vao);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING,&s->vbo); glGetIntegerv(GL_ACTIVE_TEXTURE,&s->active);
    for(int i=0;i<3;i++){glActiveTexture(GL_TEXTURE0+i);glGetIntegerv(GL_TEXTURE_BINDING_2D,&s->tex[i]);}
    glGetIntegerv(GL_VIEWPORT,s->viewport);glGetIntegerv(GL_SCISSOR_BOX,s->scissor);
    glGetIntegerv(GL_UNPACK_ALIGNMENT,&s->unpack);glGetIntegerv(GL_UNPACK_ROW_LENGTH,&s->row);
    glGetIntegerv(GL_UNPACK_SKIP_ROWS,&s->skiprows);glGetIntegerv(GL_UNPACK_SKIP_PIXELS,&s->skippixels);
    glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING,&s->pbo);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&s->draw_fbo);glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&s->read_fbo);
    glGetIntegerv(GL_RENDERBUFFER_BINDING,&s->renderbuffer);
    s->blend=glIsEnabled(GL_BLEND);s->depth=glIsEnabled(GL_DEPTH_TEST);s->stencil=glIsEnabled(GL_STENCIL_TEST);
    s->scissor_on=glIsEnabled(GL_SCISSOR_TEST);s->cull=glIsEnabled(GL_CULL_FACE);
    glGetBooleanv(GL_COLOR_WRITEMASK,s->mask);glGetBooleanv(GL_DEPTH_WRITEMASK,&s->depthmask);
    glGetFloatv(GL_COLOR_CLEAR_VALUE,s->clear);
}
static void enable(GLenum c,GLboolean on){if(on)glEnable(c);else glDisable(c);}
static void bridge_restore(const Bridge *s) {
    glUseProgram(s->program);glBindVertexArray(s->vao);glBindBuffer(GL_ARRAY_BUFFER,s->vbo);
    for(int i=0;i<3;i++){glActiveTexture(GL_TEXTURE0+i);glBindTexture(GL_TEXTURE_2D,s->tex[i]);}
    glActiveTexture(s->active);glViewport(s->viewport[0],s->viewport[1],s->viewport[2],s->viewport[3]);
    glScissor(s->scissor[0],s->scissor[1],s->scissor[2],s->scissor[3]);
    glPixelStorei(GL_UNPACK_ALIGNMENT,s->unpack);glPixelStorei(GL_UNPACK_ROW_LENGTH,s->row);
    glPixelStorei(GL_UNPACK_SKIP_ROWS,s->skiprows);glPixelStorei(GL_UNPACK_SKIP_PIXELS,s->skippixels);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER,s->pbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER,s->draw_fbo);glBindFramebuffer(GL_READ_FRAMEBUFFER,s->read_fbo);
    glBindRenderbuffer(GL_RENDERBUFFER,s->renderbuffer);
    enable(GL_BLEND,s->blend);enable(GL_DEPTH_TEST,s->depth);enable(GL_STENCIL_TEST,s->stencil);
    enable(GL_SCISSOR_TEST,s->scissor_on);enable(GL_CULL_FACE,s->cull);
    glColorMask(s->mask[0],s->mask[1],s->mask[2],s->mask[3]);glDepthMask(s->depthmask);
    glClearColor(s->clear[0],s->clear[1],s->clear[2],s->clear[3]);
}
static const char *vs =
    "#version 300 es\nprecision highp float;\n"
    "layout(location=0) in vec4 pos;layout(location=1) in vec3 wp;"
    "layout(location=2) in vec2 uv;layout(location=3) in float darkness;"
    "out vec3 W;out vec2 U;out float D;"
    "void main(){gl_Position=pos;W=wp;U=uv;D=darkness;}";
static const char *fs =
    "#version 300 es\nprecision highp float;precision highp usampler2D;\n"
    "uniform usampler2D indices,maskTex;uniform sampler2D pal;"
    "uniform int cutout,emissive,receivers,generated,maskRow,genCount;uniform float emissiveFactor,emissiveMultiplier;"
    "uniform vec4 lights[4],colors[4],maskPlanes[64];uniform ivec4 maskCounts[4];"
    "uniform vec3 ambient,cameraWorld,heightFogColor;uniform float gammaExponent;uniform vec4 heightFog;"
    "in vec3 W;in vec2 U;in float D;out vec4 result;"
    "void main(){ivec2 sz=textureSize(indices,0);"
    "ivec2 tc=ivec2(floor(fract(U)*vec2(sz)));"
    "uint idx=texelFetch(indices,tc,0).r;if(cutout!=0&&idx==0u)discard;"
    "vec3 encoded=texelFetch(pal,ivec2(int(idx),0),0).rgb;"
    "vec3 base=pow(encoded,vec3(2.2));"
    "float s=pow(clamp(1.0-D/16.0,0.0,1.0),2.0);"
    "vec3 illumination=(generated!=0?ambient:(receivers!=0?ambient:vec3(1.0)))*s;"
    "if(generated!=0){for(int j=0;j<4;j++){if(j>=genCount)break;"
    "bool permitted=true;if(maskCounts[j].w==2){permitted=false;"
    "for(int p=0;p<2;p++){if(p>=maskCounts[j].x)break;bool inside=true;"
    "int count=p==0?maskCounts[j].y:maskCounts[j].z;"
    "for(int e=0;e<8;e++){if(e<count&&dot(vec4(W,1.0),maskPlanes[j*16+p*8+e])<0.0)inside=false;}"
    "permitted=permitted||inside;}}"
    "if(permitted){float a=max(0.0,1.0-distance(W,lights[j].xyz)/lights[j].w);"
    "illumination+=colors[j].rgb*colors[j].a*a*a;}}}"
    "else{for(int j=0;j<2;j++){if((receivers&(1<<j))!=0){"
    "float a=max(0.0,1.0-distance(W,lights[j].xyz)/lights[j].w);"
    "illumination+=colors[j].rgb*colors[j].a*a*a;}}}"
    "bool selflit=maskRow>=0?texelFetch(maskTex,ivec2(int(idx),maskRow),0).r!=0u:"
    "(idx==1u||(idx>=3u&&idx<=7u)||(idx>=11u&&idx<=31u));"
    "float fixtureScale=(maskRow>=0&&selflit)?emissiveMultiplier:1.0;"
    "vec3 linear=(emissive!=0||selflit)?base*emissiveFactor*fixtureScale:base*illumination;"
    "float dist=distance(W,cameraWorld);vec3 color=pow(clamp(linear,0.0,1.0),vec3(gammaExponent/2.2));"
    "float low=clamp((heightFog.x-W.z)/max(heightFog.y,.001),0.0,1.0);low=low*low*(3.0-2.0*low);"
    "float far=smoothstep(2.0,10.0,dist);float fog=min(heightFog.w,low*far*heightFog.z);"
    "color=mix(color,heightFogColor,fog);result=vec4(color,clamp(dist/24.0,0.0,1.0));}";
/* Star positions and palette indices come from the original indexed renderer.
 * It accepts a pixel only when the completed software world still contains
 * sky index 255, so no approximate depth or new sky coverage rule is needed. */
static const char *star_vs =
    "#version 300 es\nprecision highp float;"
    "layout(location=0) in vec4 pos;layout(location=2) in vec2 uv;"
    "flat out int index;void main(){gl_Position=pos;gl_PointSize=1.0;index=int(uv.x);}";
static const char *star_fs =
    "#version 300 es\nprecision highp float;"
    "uniform sampler2D pal;flat in int index;out vec4 result;"
    "void main(){result=vec4(texelFetch(pal,ivec2(index,0),0).rgb,0.0);}";

static int inject(const char *name) {
    char path[96];snprintf(path,sizeof(path),"gpu-render1b-fail-%s.txt",name);
    FILE *f=fopen(path,"r");if(!f)return 0;fclose(f);return 1;
}
void gpu_world_reject(const char *reason) {
    if(active&&!failed){failed=1;failure=reason;}
}
int gpu_world_active(void){return active&&!failed;}
static void log_fallback(const char *reason) {
    static char last[160];++fallbacks;
    if(strcmp(last,reason)||fallbacks%300==0){SDL_Log("GPU1B whole-view SOFTWARE reason=%s count=%u level=%d flags=%08x camera=%g,%g",reason,fallbacks,player_struct.level,_fr_curflags,fr_camera_last[0]/65536.,fr_camera_last[1]/65536.);snprintf(last,sizeof(last),"%s",reason);}
}
static GLuint shader(GLenum type,const char *source) {
    GLuint id=glCreateShader(type);glShaderSource(id,1,&source,NULL);glCompileShader(id);
    GLint ok;char log[2048];glGetShaderiv(id,GL_COMPILE_STATUS,&ok);glGetShaderInfoLog(id,sizeof(log),NULL,log);
    SDL_Log("GPU1B shader type=%x compile=%d log=%s",type,ok,log);
    if(!ok){glDeleteShader(id);return 0;}return id;
}
static const char *post_vs=
    "#version 300 es\nprecision highp float;"
    "void main(){vec2 p=vec2(gl_VertexID==1?3.0:-1.0,gl_VertexID==2?3.0:-1.0);gl_Position=vec4(p,0.0,1.0);}";
static const char *post_fs=
    "#version 300 es\nprecision highp float;"
    "uniform sampler2D sourceWorld;uniform vec4 gradeA,gradeB;"
    "uniform vec3 shadowTint,highlightTint,hazeColor,shadowBias;"
    "uniform vec4 tintAndFx,atmosphere,toneZones;uniform vec2 distanceFx;uniform vec3 vignette;"
    "out vec4 result;"
    "void main(){ivec2 xy=ivec2(gl_FragCoord.xy);vec2 size=vec2(textureSize(sourceWorld,0));"
    "vec2 uv=(vec2(xy)+.5)/size;vec4 src=texelFetch(sourceWorld,xy,0);"
    "vec3 c=src.rgb*gradeA.x;"
    "float lum=dot(c,vec3(.2126,.7152,.0722));"
    "c=mix(vec3(lum),c,gradeB.x);"
    "float sw=1.0-smoothstep(.12,.62,lum),hw=smoothstep(.45,.9,lum);"
    "c*=mix(vec3(1),shadowTint,sw*tintAndFx.x);c*=mix(vec3(1),highlightTint,hw*tintAndFx.y);"
    "c+=shadowBias*lum*sw;c*=1.0+distanceFx.y*sw;"
    "c=pow(max(c,vec3(0)),vec3(gradeA.y));c=mix(c,sqrt(c),gradeA.z);"
    "c=c/(1.0+gradeA.w*max(c-.68,vec3(0)));"
    "c*=1.0+toneZones.z*(1.0-smoothstep(.02,.16,c));"
    "float mid=dot(c,vec3(.2126,.7152,.0722));"
    "float mw=smoothstep(.03,.16,mid)*(1.0-smoothstep(.36,.68,mid));"
    "c+=vec3(toneZones.y*mw);"
    "vec3 excess=max(c-vec3(.64),vec3(0));"
    "c=min(c,vec3(.64))+excess/(1.0+2.0*toneZones.x*excess);"
    "float far=smoothstep(atmosphere.x,atmosphere.y,src.a*24.0)*step(0.001,src.a);"
    "float y=dot(c,vec3(.2126,.7152,.0722));"
    "c=mix(c,vec3(y),far*atmosphere.w);"
    "c=vec3(.25)+(c-vec3(.25))*(1.0-far*distanceFx.x);"
    "c=mix(c,hazeColor,far*atmosphere.z);"
    "float v=smoothstep(vignette.y,vignette.y+vignette.z,length((uv-.5)*vec2(1.2,1.0))*2.0);"
    "c*=1.0-v*vignette.x;"
    "float noise=fract(sin(dot(vec2(xy),vec2(12.9898,78.233)))*43758.5453)-.5;"
    "c+=noise*tintAndFx.z;result=vec4(clamp(c,0.0,1.0),1.0);}";
static void sampling(void);
static void post_disable(const char *why) {
    if(!post_failed)SDL_Log("POST1 fallback to ungraded GPU world: %s",why);
    post_failed=1;post_ready=0;
}
static void post_initialize(void) {
    if(post_ready||post_failed)return;
    Bridge state;bridge_save(&state);
    GLuint vert=shader(GL_VERTEX_SHADER,inject("post-shader")?"#version 300 es\n intentional failure":post_vs);
    GLuint frag=shader(GL_FRAGMENT_SHADER,post_fs);
    GLint ok=0;
    if(vert&&frag){post_program=glCreateProgram();glAttachShader(post_program,vert);glAttachShader(post_program,frag);glLinkProgram(post_program);glGetProgramiv(post_program,GL_LINK_STATUS,&ok);}
    if(vert)glDeleteShader(vert);if(frag)glDeleteShader(frag);
    if(ok){glGenVertexArrays(1,&post_vao);glGenTextures(1,&post_source);
        glBindTexture(GL_TEXTURE_2D,post_source);sampling();
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,view_w,view_h,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
        if(glGetError()!=GL_NO_ERROR)ok=0;
    }
    bridge_restore(&state);
    if(!ok){post_disable("shader/texture creation");return;}
    post_world=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_ABGR8888,SDL_TEXTUREACCESS_TARGET,view_w,view_h);
    if(!post_world){post_disable("target allocation");return;}
    SDL_SetTextureBlendMode(post_world,SDL_BLENDMODE_NONE);SDL_SetTextureScaleMode(post_world,SDL_ScaleModeNearest);
    SDL_Texture *old=SDL_GetRenderTarget(renderer);
    if(SDL_SetRenderTarget(renderer,post_world)<0){post_disable("target bind");return;}
    bridge_save(&state);
    GLenum status=glCheckFramebufferStatus(GL_FRAMEBUFFER),error=glGetError();
    bridge_restore(&state);SDL_SetRenderTarget(renderer,old);
    if(status!=GL_FRAMEBUFFER_COMPLETE||error||inject("post-fbo")){post_disable("incomplete target");return;}
    post_ready=1;
    SDL_Log("POST1 target %dx%d shader PASS",view_w,view_h);
}
/* Called only on the render thread. Lost SDL textures remain on their old
 * renderer's ownership list; the presenter retires that entire owner safely. */
static void resources_dispose(int lost) {
    retained=ready=0;
    if(!lost){if(program)glDeleteProgram(program);if(star_program)glDeleteProgram(star_program);if(post_program)glDeleteProgram(post_program);glDeleteVertexArrays(1,&vao);glDeleteVertexArrays(1,&post_vao);glDeleteBuffers(1,&vbo);glDeleteTextures(1,&palette);glDeleteTextures(1,&mask_texture);glDeleteTextures(1,&post_source);glDeleteRenderbuffers(1,&depth);}
    program=star_program=vao=vbo=palette=mask_texture=depth=0;uploaded_masks_level=-1;
    post_program=post_vao=post_source=0;post_ready=post_failed=0;
    for(int i=0;i<MAX_TEXTURES;i++){if(!lost)glDeleteTextures(1,&texture_gl[i]);texture_gl[i]=0;texture_retired[i]=0;}
    if(world&&!lost)SDL_DestroyTexture(world);world=NULL;uploaded_palette=0;
    if(post_world&&!lost)SDL_DestroyTexture(post_world);post_world=NULL;
}
static int initialize(void) {
    if(disabled){failure="GPU initialization disabled until mode retry";return 0;}
    if(inject("es3")){failure="injected ES3 unavailable";return 0;}
    const char *version=(const char*)glGetString(GL_VERSION);
    if(!version||!strstr(version,"OpenGL ES 3")){failure="current context does not support GLES3";return 0;}
    SDL_Log("GPU1B EGL=%s GL=%s GLSL=%s vendor=%s renderer=%s",
        eglQueryString(eglGetCurrentDisplay(),EGL_VERSION),version,glGetString(GL_SHADING_LANGUAGE_VERSION),glGetString(GL_VENDOR),glGetString(GL_RENDERER));
    Bridge state;bridge_save(&state);
    GLuint vert=shader(GL_VERTEX_SHADER,inject("shader")?"#version 300 es\n intentional failure":vs);
    GLuint frag=shader(GL_FRAGMENT_SHADER,fs);
    GLint ok=0;
    if(vert&&frag){program=glCreateProgram();glAttachShader(program,vert);glAttachShader(program,frag);glLinkProgram(program);glGetProgramiv(program,GL_LINK_STATUS,&ok);}
    glDeleteShader(vert);glDeleteShader(frag);
    if(ok){
        GLuint sv=shader(GL_VERTEX_SHADER,star_vs),sf=shader(GL_FRAGMENT_SHADER,star_fs);
        ok=0;
        if(sv&&sf){star_program=glCreateProgram();glAttachShader(star_program,sv);glAttachShader(star_program,sf);
            glLinkProgram(star_program);glGetProgramiv(star_program,GL_LINK_STATUS,&ok);}
        if(sv)glDeleteShader(sv);if(sf)glDeleteShader(sf);
    }
    char info[2048]={0};if(program)glGetProgramInfoLog(program,sizeof(info),NULL,info);
    SDL_Log("GPU1B program link=%d log=%s",ok,info);
    if(ok){
        glGenVertexArrays(1,&vao);glBindVertexArray(vao);glGenBuffers(1,&vbo);glBindBuffer(GL_ARRAY_BUFFER,vbo);
        const int sizes[]={4,3,2,1};const size_t offsets[]={0,4,7,9};
        for(int i=0;i<4;i++){glEnableVertexAttribArray(i);glVertexAttribPointer(i,sizes[i],GL_FLOAT,GL_FALSE,sizeof(Vertex),(void*)(offsets[i]*sizeof(float)));}
        glGenTextures(1,&palette);glGenTextures(1,&mask_texture);glGenRenderbuffers(1,&depth);
    }
    bridge_restore(&state);
    if(!ok){failure="shader compilation/link failure";return 0;}
    world=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_ABGR8888,SDL_TEXTUREACCESS_TARGET,view_w,view_h);
    if(!world){failure="SDL world target allocation failure";return 0;}
    SDL_SetTextureBlendMode(world,SDL_BLENDMODE_NONE);SDL_SetTextureScaleMode(world,SDL_ScaleModeNearest);
    SDL_Texture *old=SDL_GetRenderTarget(renderer);
    if(SDL_SetRenderTarget(renderer,world)<0){failure="SDL world target bind failure";return 0;}
    bridge_save(&state);
    glBindRenderbuffer(GL_RENDERBUFFER,depth);glRenderbufferStorage(GL_RENDERBUFFER,GL_DEPTH24_STENCIL8,view_w,view_h);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_STENCIL_ATTACHMENT,GL_RENDERBUFFER,depth);
    GLenum complete=glCheckFramebufferStatus(GL_FRAMEBUFFER),err=glGetError();
    GLint red=0,green=0,blue=0,alpha=0;
    glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_FRAMEBUFFER_ATTACHMENT_RED_SIZE,&red);
    glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_FRAMEBUFFER_ATTACHMENT_GREEN_SIZE,&green);
    glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_FRAMEBUFFER_ATTACHMENT_BLUE_SIZE,&blue);
    glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_FRAMEBUFFER_ATTACHMENT_ALPHA_SIZE,&alpha);
    SDL_Log("GPU1B selected world color bits=%d/%d/%d/%d",red,green,blue,alpha);
    SDL_Log("GPU1B FBO=%x color=RGBA8 depth=24 stencil=8 size=%dx%d error=%x painter-order depth-test=OFF",complete,view_w,view_h,err);
    bridge_restore(&state);SDL_SetRenderTarget(renderer,old);
    if(inject("fbo")){failure="injected framebuffer creation failure";return 0;}
    if(complete!=GL_FRAMEBUFFER_COMPLETE||err){failure="incomplete framebuffer or allocation GL error";return 0;}
    ready=1;gpu_w=view_w;gpu_h=view_h;context=SDL_GL_GetCurrentContext();return 1;
}
static void retire_texture(void *unused,int slot) {
    (void)unused;
    /* Capture never issues native GL commands behind SDL's state cache. Names
     * are deleted in the next guarded submission, before any new allocation. */
    texture_retired[slot]=1;
}
static int texture_index(grs_bitmap *source) {
    grs_bitmap decoded,*bm=source;
    if(bm->type==BMT_RSD8){if(gr_rsd8_convert(bm,&decoded)!=GR_UNPACK_RSD8_OK){gpu_world_reject("RSD decode");return -1;}bm=&decoded;}
    if(bm->type!=BMT_FLAT8||(bm->flags&BMF_TLUC8)||bm->w<=0||bm->h<=0||bm->w>1024||bm->h>1024||bm->row<bm->w){gpu_world_reject("bitmap format/translucency/dimensions");return -1;}
    int slot=gw_cache_acquire(&texture_cache,bm->bits,bm->w,bm->h,bm->row);
    if(slot<0)gpu_world_reject(slot==GW_CACHE_CAPACITY?"texture current-view working set exceeds budget":slot==GW_CACHE_ALLOC?"texture CPU allocation":"invalid indexed texture");
    return slot;
}
static void world_position(const g3s_point *p,float *out) {
    float v[]={p->gX/65536.f,p->gY/65536.f,p->gZ/65536.f},w[3];
    for(int i=0;i<3;i++)w[i]=eye[i]+inverse[i*3]*v[0]+inverse[i*3+1]*v[1]+inverse[i*3+2]*v[2];
    out[0]=w[0];out[1]=w[2];out[2]=-w[1];
}
static float clut_darkness(const unsigned char *clut) {
    if(!clut)return 0;
    uintptr_t c=(uintptr_t)clut,b=(uintptr_t)shading_table;
    if(c<b||c>=b+4096||((c-b)&255)){gpu_world_reject("non-original CLUT");return 0;}
    return (float)(c-b)/256.f;
}
static uint32_t map_geometry_key(const MapElem *m){
    return (uint32_t)me_tiletype(m)|((uint32_t)me_height_flr(m)<<6)|
        ((uint32_t)me_height_ceil(m)<<11)|((uint32_t)me_param(m)<<16)|
        ((uint32_t)me_bits_mirror(m)<<24);
}
static void generated_prepare(void){
    generated_active=0;
    int level=player_struct.level;
    if(level!=generated_level){
        gg_profile_free(generated_profile);generated_profile=NULL;
        generated_level=level;generated_load_attempted=0;
        for(int i=0;i<512;i++)gpu_flicker_reset(&transient_state[i]);
    }
    if(!gg_profile_supported(level))return; /* neutral GPU on unlisted levels */
    if(!generated_load_attempted){
        uint64_t start=SDL_GetPerformanceCounter();generated_load_attempted=1;
        generated_profile=gg_profile_load(level);
        SDL_Log("GENERATED1 profile level=%d loaded=%d reason=%s load_ms=%.3f lights=%d surfaces=%d policy=portal-crossing-generated-light-disabled",
            level,generated_profile!=NULL,gg_profile_error(),
            (SDL_GetPerformanceCounter()-start)*1000./SDL_GetPerformanceFrequency(),
            generated_profile?generated_profile->light_count:0,
            generated_profile?generated_profile->surface_count:0);
    }
    if(!generated_profile||!global_fullmap||global_fullmap->x_size!=64||global_fullmap->y_size!=64)return;
    uint32_t keys[4096];
    for(int y=0;y<64;y++)for(int x=0;x<64;x++)keys[y*64+x]=map_geometry_key(MAP_GET_XY(x,y));
    generated_active=gg_profile_map_matches(generated_profile,keys);
    static int mismatch_level=-1;
    if(!generated_active&&mismatch_level!=level){
        SDL_Log("GENERATED1 neutral GPU level=%d reason=stale-map-geometry",level);
        mismatch_level=level;
    }
    if(generated_active)for(int i=0;i<generated_profile->light_count;i++){
        const GgLight *light=&generated_profile->lights[i];
        int x=(int)floorf(light->position[0]),y=(int)floorf(light->position[1]);
        source_multiplier[i]=1.f;
        if(x<0||x>=64||y<0||y>=64||light->source_object>=0)continue;
        MapElem *tile=MAP_GET_XY(x,y);
        float floor_z=_fr_fhgt_list[me_height_flr(tile)]/65536.f;
        float ceil_z=_fr_fhgt_list[32-me_height_ceil(tile)]/65536.f;
        int ceiling=fabsf(light->position[2]-ceil_z)<fabsf(light->position[2]-floor_z);
        unsigned live=ceiling?me_templight_ceil(tile):me_templight_flr(tile);
        int permanent=ceiling?me_light_ceil(tile):me_light_flr(tile);
        /* A stable transient value cannot identify a flickering fixture. Only
         * observed changes at this exact source tile enable modulation. */
        unsigned previous_changes=transient_state[i].changes;
        source_multiplier[i]=gpu_flicker_sample(&transient_state[i],live,permanent);
        if(previous_changes<2&&transient_state[i].changes==2&&
           transient_state[i].low<transient_state[i].high&&source_multiplier[i]!=1.f){
            static unsigned reports;
            if(reports<24){SDL_Log("POST1 flicker source level=%d id=%d tile=%d,%d material=%d live=%u permanent=%d multiplier=%.3f",
                level,i,x,y,light->material,live,permanent,source_multiplier[i]);reports++;}
        }
    }
}
static void metadata(Draw *d) {
    int x=_fdt_x,y=_fdt_y,material=-1;
    if(object_id>=0){x=objs[object_id].loc.x>>8;y=objs[object_id].loc.y>>8;d->emissive=!object_lit;}
    else {material=loved_textures[_game_fr_tmap];d->emissive=material==35||material==216;
        unsigned type=me_tiletype(MAP_GET_XY(x,y));if(type<64)terrain_types|=UINT64_C(1)<<type;}
    d->material_id=material;
    d->surface=object_id>=0?(GpuSurfaceId){0}:current_surface;
    d->mask_row=generated_active?gg_mask_row(material):-1;
    d->ambient_tile=(x>=0&&x<64&&y>=0&&y<64)?y*64+x:-1;
    d->generated_surface=generated_active?gg_profile_find(generated_profile,d->surface):NULL;
    d->source_light=-1;d->source_multiplier=1.f;
    if(generated_active&&material>=0&&x>=0&&x<64&&y>=0&&y<64){
        for(int i=0;i<generated_profile->light_count;i++){
            const GgLight *l=&generated_profile->lights[i];
            if(l->source_object<0&&l->material==material&&
               (int)floorf(l->position[0])==x&&(int)floorf(l->position[1])==y){
                if(d->source_light>=0){d->source_light=-1;break;} /* ambiguous */
                d->source_light=i;
            }
        }
        if(d->source_light>=0){
            d->source_multiplier=source_multiplier[d->source_light];
            if(player_struct.level==1&&x>=24&&x<=33&&y>=18&&y<=27&&d->source_multiplier<.99f){
                static unsigned char observed[512];
                if(!observed[d->source_light]){
                    observed[d->source_light]=1;
                    SDL_Log("POST1 emissive receiver matched level=1 source=%d tile=%d,%d material=%d multiplier=%.3f",
                        d->source_light,x,y,material,d->source_multiplier);
                }
            }
        }
    }
    if(generated_active){d->emissive=object_id>=0&&!object_lit;d->receivers=0;return;}
    const HvTile *tile=medical_profile?hv_region_tile(x,y):NULL;
    if(medical_profile&&!tile&&object_id>=0){if(!(objs[object_id].loc.y&255))tile=hv_region_tile(x,y-1);if(!tile&&!(objs[object_id].loc.x&255))tile=hv_region_tile(x-1,y);}
    /* Authored receiver membership prevents influence in adjacent unrelated
     * rooms. Both lights reach the landing; only amber enters the supply room.
     * No general visibility/shadow claim. Closed portal leakage is gated below. */
    d->receivers=tile?(tile->bank==HV_WARM?2:tile->bank==HV_ORIGINAL?3:1):0;
    /* Door 5 is the runtime supply portal at (26.5,21.9961). Its closed
     * animation frame blocks amber spill onto the Medical landing. */
    if(tile&&tile->bank!=HV_WARM&&objs[5].info.current_frame==0)d->receivers&=~2;
}
static Draw *new_draw(int n,int texture,int cutout) {
    if(ndraw>=MAX_DRAWS||nvertex+n>MAX_VERTICES){gpu_world_reject("command capacity");return NULL;}
    Draw *d=&draws[ndraw++];memset(d,0,sizeof(*d));d->first=nvertex;d->count=n;d->texture=texture;d->cutout=cutout;metadata(d);nvertex+=n;return d;
}
static void point_vertex(Vertex *v,g3s_phandle p,int lit) {
    float z=p->gZ/65536.f;
    /* Same software projection including fixed-point bias; invert Y for SDL's
     * render-target texture convention (its texel row zero is framebuffer low Y). */
    v->clip[0]=(p->sx/65536.f*2/view_w-1)*z;
    v->clip[1]=(p->sy/65536.f*2/view_h-1)*z;v->clip[2]=0;v->clip[3]=z;
    world_position(p,v->world);
    /* Match draw_tmap_common's one-unit inward UV bias before texel scaling. */
    v->uv[0]=(p->uv.u+(p->uv.u<=0?1:-1))/256.f;
    v->uv[1]=(p->uv.v+(p->uv.v<=0?1:-1))/256.f;
    v->darkness=lit?p->i/256.f:0;
    if(gr_get_fill_type()==FILL_CLUT)v->darkness=clut_darkness((const unsigned char*)gr_get_fill_parm());
}
static void log_polygon_topology(int n,g3s_phandle *p,const int32_t (*xy)[2]) {
    static unsigned reports;
    if(reports>=12)return;
    unsigned report=++reports;
    int winding=0,edge=-1,vertex=-1;
    double failed_cross=0,failed_tolerance=0,distance_pixels=0;
    /* Diagnostic replay only, after the production helper has rejected. Keep
     * its decision/tolerance unchanged while gathering exact device fixtures. */
    for(int i=0;i<n&&edge<0;i++){
        int next=(i+1)%n;
        double dx=(double)xy[next][0]-(double)xy[i][0];
        double dy=(double)xy[next][1]-(double)xy[i][1];
        double tolerance=2.0*(fabs(dx)+fabs(dy));
        for(int j=0;j<n;j++){
            double px=(double)xy[j][0]-(double)xy[i][0];
            double py=(double)xy[j][1]-(double)xy[i][1];
            double cross=dx*py-dy*px;
            int sign=(cross>tolerance)-(cross<-tolerance);
            if(sign&&winding&&sign!=winding){
                edge=i;vertex=j;failed_cross=cross;failed_tolerance=tolerance;
                distance_pixels=cross/(65536.0*hypot(dx,dy));
                break;
            }
            if(sign)winding=sign;
        }
    }
    SDL_Log("GPU2 topology rejection report=%u frame=%u level=%d object=%d tile=%d,%d texture_slot=%d vertices=%d viewport=%dx%d camera=%.9g,%.9g,%.9g edge=%d vertex=%d expected_winding=%d cross=%.17g tolerance=%.17g distance_pixels=%.9g",
        report,frame,player_struct.level,object_id,_fdt_x,_fdt_y,_game_fr_tmap,n,view_w,view_h,
        fr_camera_last[0]/65536.,fr_camera_last[1]/65536.,fr_camera_last[2]/65536.,
        edge,vertex,winding,failed_cross,failed_tolerance,distance_pixels);
    for(int i=0;i<n;i++)
        SDL_Log("GPU2 topology vertex report=%u i=%d raw_xy=%d,%d gpu_xy=%d,%d raw_xyz=%d,%d,%d flags=0x%x codes=0x%x",
            report,i,(int)p[i]->sx,(int)p[i]->sy,(int)xy[i][0],(int)xy[i][1],(int)p[i]->gX,(int)p[i]->gY,(int)p[i]->gZ,
            (unsigned)p[i]->p3_flags,(unsigned)p[i]->codes);
}
void gpu_world_face(int n,g3s_phandle *p,grs_bitmap *bm,int lit) {
    if(!gpu_world_active())return;
    if(n<3||n>32){gpu_world_reject("textured polygon count");return;}
    /* Terrain retains its strict convex guard. Model lists may be concave;
     * finish their bounded triangulation before allocating any draw command. */
    int32_t xy[32][2],xyz[32][3];
    unsigned char clipped[32];
    for(int i=0;i<n;i++){
        if(p[i]->gZ<=0){gpu_world_reject("unclipped near-plane vertex");return;}
        /* Ordinary projection sets this only after both coordinates succeed;
         * clip intersections also set it before their final screen projection.
         * Bit 16 in codes is ambiguous: overflow for ordinary vertices, but
         * the clipper's deliberate CC_OFF_X marker for PF_CLIPPNT vertices. */
        if(!(p[i]->p3_flags&PF_PROJECTED)){
            static unsigned projection_reports;
            if(projection_reports<12){
                ++projection_reports;
                SDL_Log("GPU2 projection rejection frame=%u object=%d tile=%d,%d vertex=%d viewport=%dx%d raw_xyz=%d,%d,%d flags=0x%x codes=0x%x",
                    frame,object_id,_fdt_x,_fdt_y,i,view_w,view_h,(int)p[i]->gX,(int)p[i]->gY,(int)p[i]->gZ,
                    (unsigned)p[i]->p3_flags,(unsigned)p[i]->codes);
            }
            gpu_world_reject("unprojected/overflow polygon vertex");return;
        }
        xy[i][0]=p[i]->sx;xy[i][1]=p[i]->sy;
        xyz[i][0]=p[i]->gX;xyz[i][1]=p[i]->gY;xyz[i][2]=p[i]->gZ;
        clipped[i]=(p[i]->p3_flags&PF_CLIPPNT)!=0;
    }
    int canonicalized=gw_polygon_canonicalize_clipped_positions(n,xyz,clipped,view_w,view_h,xy);
    if(canonicalized<0){gpu_world_reject("invalid polygon projection viewport");return;}
    unsigned char triangles[30][3];
    int triangle_count=n-2;
    if(object_id>=0){
        triangle_count=gw_model_triangulate(n,xy,triangles);
        if(!triangle_count){
            log_polygon_topology(n,p,xy);
            gpu_world_reject("unsupported model polygon topology");return;
        }
    }else if(!gw_polygon_is_convex(n,xy)){
        log_polygon_topology(n,p,xy);
        gpu_world_reject("non-convex polygon topology");return;
    }else{
        for(int i=0;i<triangle_count;i++){
            triangles[i][0]=0;triangles[i][1]=(unsigned char)(i+1);
            triangles[i][2]=(unsigned char)(i+2);
        }
    }
    /* Build6's measured boundary fixture emitted equal xyz with sx separated
     * by 387 raw units through the two projection formulas. Canonicalize only
     * GPU-local copies; retain each UV/light value and every source vertex. */
    g3s_point captured_points[32];g3s_phandle captured[32];
    if(canonicalized){
        static unsigned canonical_reports;
        int report=canonical_reports<12;if(report)++canonical_reports;
        for(int i=0;i<n;i++){
            captured[i]=p[i];
            if(xy[i][0]==p[i]->sx&&xy[i][1]==p[i]->sy)continue;
            if(report)SDL_Log("GPU2 projection canonicalized frame=%u level=%d object=%d tile=%d,%d vertex=%d viewport=%dx%d raw_xy=%d,%d gpu_xy=%d,%d raw_xyz=%d,%d,%d",
                frame,player_struct.level,object_id,_fdt_x,_fdt_y,i,view_w,view_h,(int)p[i]->sx,(int)p[i]->sy,
                (int)xy[i][0],(int)xy[i][1],(int)p[i]->gX,(int)p[i]->gY,(int)p[i]->gZ);
            captured_points[i]=*p[i];captured_points[i].sx=xy[i][0];captured_points[i].sy=xy[i][1];
            captured[i]=&captured_points[i];
        }
        p=captured;
    }
    int tex=texture_index(bm);if(tex<0)return;
    Draw *d=new_draw(triangle_count*3,tex,(bm->flags&BMF_TRANS)!=0);if(!d)return;
    for(int i=0,k=d->first;i<triangle_count;i++)
        for(int j=0;j<3;j++)point_vertex(&vertices[k++],p[triangles[i][j]],lit);
    qa_animation_capture(tex,n,xy);
}
void gpu_world_polygon(long color,int n,g3s_phandle *p,int gouraud) {
    if(!gpu_world_active())return;
    if(gouraud||color<0||color>255){gpu_world_reject("translucent/gouraud polygon");return;}
    unsigned char index=(unsigned char)color;grs_bitmap bm;memset(&bm,0,sizeof(bm));bm.bits=&index;bm.w=bm.h=bm.row=1;bm.type=BMT_FLAT8;
    gpu_world_face(n,p,&bm,0);
}
void gpu_world_star_pixel(int x,int y,unsigned char index) {
    if(!gpu_world_active())return;
    if(x<0||y<0||x>=view_w||y>=view_h){gpu_world_reject("star pixel outside view");return;}
    if(nstar_pixels>=MAX_STAR_PIXELS){gpu_world_reject("star pixel capacity");return;}
    Vertex *v=&star_pixels[nstar_pixels++];memset(v,0,sizeof(*v));
    v->clip[0]=((float)x+.5f)*2.f/view_w-1.f;
    v->clip[1]=((float)y+.5f)*2.f/view_h-1.f;
    v->clip[3]=1.f;v->uv[0]=(float)index;
}
void gpu_world_sprite(grs_bitmap *bm,grs_vertex **p,g3s_phandle anchor,const unsigned char *clut) {
    if(!gpu_world_active())return;
    int tex=texture_index(bm);if(tex<0)return;
    Draw *d=new_draw(6,tex,(bm->flags&BMF_TRANS)!=0);if(!d)return;
    const int order[]={0,1,2,0,2,3};const float uv[4][2]={{0,0},{0.99999f,0},{0.99999f,0.99999f},{0,0.99999f}};
    for(int i=0;i<6;i++){int j=order[i];Vertex *v=&vertices[d->first+i];v->clip[0]=p[j]->x/65536.f*2/view_w-1;v->clip[1]=p[j]->y/65536.f*2/view_h-1;v->clip[2]=0;v->clip[3]=1;world_position(anchor,v->world);memcpy(v->uv,uv[j],sizeof(v->uv));v->darkness=clut_darkness(clut);}
}
void gpu_world_object(int id,int type,int lit) {
    if(!active)return;object_id=id;object_lit=lit;
    static unsigned char seen[65536];
    if(id>=0&&id<65536&&!seen[id]){seen[id]=1;SDL_Log("GPU1B object id=%d representation=%d class=%d frame=%d xyz=%g,%g,%g",id,type,objs[id].obclass,objs[id].info.current_frame,objs[id].loc.x/256.,objs[id].loc.y/256.,objs[id].loc.z/256.);}
    /* Bitmap, multiview, critter, panels, models and ordinary cube specials
     * reach common primitive capture. Destination-dependent and voxel paths do not. */
    if(type==FAUBJ_TL_POLY||type==FAUBJ_VOX||ID2TRIP(id)==INVISO_CRIT_TRIPLE||
        (type==FAUBJ_SPECIAL && ID2TRIP(id)!=BRIDGE_TRIPLE && ID2TRIP(id)!=PILLAR_TRIPLE &&
         ID2TRIP(id)!=CATWALK_TRIPLE && ID2TRIP(id)!=SML_CRT_TRIPLE && ID2TRIP(id)!=LG_CRT_TRIPLE &&
         ID2TRIP(id)!=SECURE_CONTR_TRIPLE && ID2TRIP(id)!=BARRICADE_TRIPLE))gpu_world_reject("unsupported object representation");
}
void gpu_world_object_end(void){object_id=-1;object_lit=1;}
void gpu_world_surface_set(int x,int y,int kind,int face,int subface,int terrain_type,
        uint32_t geometry_key,uint32_t neighbor_geometry_key) {
    current_surface=gpu_surface_id_make(player_struct.level,x,y,kind,face,subface,
        terrain_type,geometry_key,neighbor_geometry_key);
}
void gpu_world_surface_subface(int subface) {
    if(subface<0||subface>7)current_surface.valid=0;
    else if(current_surface.valid)current_surface.subface=(uint8_t)subface;
}
void gpu_world_surface_clear(void){current_surface=(GpuSurfaceId){0};}

int gpu_world_begin(void) {
    int previous=active;active=0;
    if(gShockPrefs.doHorrorVisuals!=3&&gShockPrefs.doHorrorVisuals!=4)return previous;
    /* Secondary and picking canvases never own the retained primary commands.
     * In particular a sequential rear view must not discard a primary image
     * after its software world has been replaced by the UI sentinel. */
    if(previous||(_fr_curflags&(FR_PICKUPM_MASK|FR_HACKCAM_MASK|FR_CURVIEW_MASK))||view360_is_rendering)return previous;
    /* The options wrapper snapshots/restores indexed screen pixels. Its entry
     * sets this flag before render_run, so supply a fresh software background
     * rather than letting the snapshot retain index255 GPU world placeholders. */
    if(wrapper_panel_on){retained=command_valid=0;log_fallback("menu requires indexed software background");return previous;}
    if(view360_render_on){retained=command_valid=0;log_fallback("sensaround requires whole-frame software");return previous;}
    if(secret_render_fx){retained=command_valid=0;log_fallback("software framebuffer transition");return previous;}
    unsigned excluded=FR_PICKUPM_MASK|FR_HACKCAM_MASK|FR_PALETTE_MASK|FR_NORENDR_MASK|FR_SOLIDFR_MASK|FR_SFX_MASK|FR_OVERLAY_MASK|FR_CURVIEW_MASK;
    if(previous||(_fr_curflags&excluded)||view360_is_rendering||UseCutscenePalette||curr_clut_table||DoubleSize||
        !global_fullmap||global_fullmap->cyber||player_struct.drug_status[DRUG_LSD]||player_struct.drug_status[DRUG_SIGHT]){
        if(!(_fr_curflags&(FR_PICKUPM_MASK|FR_HACKCAM_MASK))&&!previous)retained=command_valid=0;
        if(!(_fr_curflags&FR_PICKUPM_MASK))log_fallback("excluded view/effect/detail/level");
        return previous;
    }
    /* Geometry support is independent of the optional Medical light fixture.
     * Its signature gates only those authored lights, never normal terrain. */
    medical_profile=gShockPrefs.doHorrorVisuals==3&&player_struct.level==1;
    for(size_t i=0;medical_profile&&i<hv_medical_tile_count;i++){
        const HvTile *t=&hv_medical_tiles[i];MapElem *m=MAP_GET_XY(t->x,t->y);
        if(me_tiletype(m)!=t->type||me_height_flr(m)!=t->floor_height||me_height_ceil(m)!=t->ceiling_height||loved_textures[me_tmap_wall(m)]!=t->wall||loved_textures[me_tmap_flr(m)]!=t->floor||loved_textures[me_tmap_ceil(m)]!=t->ceiling)medical_profile=0;
    }
    generated_active=0;
    if(gShockPrefs.doHorrorVisuals==4)generated_prepare();
    active=1;failed=0;failure=NULL;retained=command_valid=0;ndraw=nvertex=nstar_pixels=0;object_id=-1;
    gpu_world_surface_clear();++frame;
    qa_animation_begin();
    if(!cache_initialized){gw_cache_init(&texture_cache,MAX_TEXTURES,GW_CACHE_DEFAULT_BYTES,retire_texture,NULL);cache_initialized=1;}
    gw_cache_begin(&texture_cache);
    uploads=upload_bytes=0;terrain_types=0;capture_start=SDL_GetPerformanceCounter();
    view_w=_fr->draw_canvas.bm.w;view_h=_fr->draw_canvas.bm.h;
    destination.x=full_game_3d?0:SCONV_X(28);destination.y=full_game_3d?0:SCONV_Y(24);destination.w=view_w;destination.h=view_h;
    /* Invert the exact primary camera transform once, before object instancing.
     * view_matrix is column-major relative to do_rotate's dot products. */
    const fix *m=(const fix*)&view_matrix;float a[9];for(int r=0;r<3;r++)for(int c=0;c<3;c++)a[r*3+c]=m[c*3+r]/65536.f;
    float det=a[0]*(a[4]*a[8]-a[5]*a[7])-a[1]*(a[3]*a[8]-a[5]*a[6])+a[2]*(a[3]*a[7]-a[4]*a[6]);
    if(fabsf(det)<1e-8f){gpu_world_reject("singular camera matrix");return previous;}
    inverse[0]=(a[4]*a[8]-a[5]*a[7])/det;inverse[1]=(a[2]*a[7]-a[1]*a[8])/det;inverse[2]=(a[1]*a[5]-a[2]*a[4])/det;
    inverse[3]=(a[5]*a[6]-a[3]*a[8])/det;inverse[4]=(a[0]*a[8]-a[2]*a[6])/det;inverse[5]=(a[2]*a[3]-a[0]*a[5])/det;
    inverse[6]=(a[3]*a[7]-a[4]*a[6])/det;inverse[7]=(a[1]*a[6]-a[0]*a[7])/det;inverse[8]=(a[0]*a[4]-a[1]*a[3])/det;
    eye[0]=_view_position.gX/65536.f;eye[1]=_view_position.gY/65536.f;eye[2]=_view_position.gZ/65536.f;
    memset(light_pos,0,sizeof(light_pos));memset(light_color,0,sizeof(light_color));
    for(int j=0;medical_profile&&j<2;j++){
        const Light *l=&medical_lights[j];MapElem *tile=MAP_GET_XY(l->tile_x,l->tile_y);
        light_pos[j*4]=l->x;light_pos[j*4+1]=l->y;
        light_pos[j*4+2]=_fr_fhgt_list[32-me_height_ceil(tile)]/65536.f-l->ceiling_offset;light_pos[j*4+3]=l->radius;
        memcpy(light_color+j*4,l->rgb,3*sizeof(float));light_color[j*4+3]=l->intensity;
    }
    return previous;
}
static void sampling(void){glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);}
static int submit(void) {
    if(!SDLPresenterReady()){failure="presenter reset pending";return 0;}
    SDL_GLContext now=SDL_GL_GetCurrentContext();
    if(ready&&context!=now){SDLRequestPresenterReset();failure="presenter context changed";return 0;}
    if(ready&&(gpu_w!=view_w||gpu_h!=view_h))resources_dispose(0);
    if(!ready&&!initialize()){resources_dispose(0);disabled=1;gShockPrefs.doHorrorVisuals=0;return 0;}
    SDL_Texture *old=SDL_GetRenderTarget(renderer);if(SDL_SetRenderTarget(renderer,world)<0){failure="GPU world target bind failure";return 0;}
    Bridge state;bridge_save(&state);
    for(int i=0;i<MAX_TEXTURES;i++)if(texture_retired[i]){glDeleteTextures(1,&texture_gl[i]);texture_gl[i]=0;texture_retired[i]=0;}
    glDisable(GL_BLEND);glDisable(GL_DEPTH_TEST);glDisable(GL_STENCIL_TEST);glDisable(GL_SCISSOR_TEST);glDisable(GL_CULL_FACE);
    glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);glDepthMask(GL_FALSE);glViewport(0,0,view_w,view_h);
    glClearColor(0,0,0,0);glClear(GL_COLOR_BUFFER_BIT);glUseProgram(program);glBindVertexArray(vao);glBindBuffer(GL_ARRAY_BUFFER,vbo);
    glBufferData(GL_ARRAY_BUFFER,nvertex*sizeof(Vertex),vertices,GL_STREAM_DRAW);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER,0);glPixelStorei(GL_UNPACK_ALIGNMENT,1);glPixelStorei(GL_UNPACK_ROW_LENGTH,0);glPixelStorei(GL_UNPACK_SKIP_ROWS,0);glPixelStorei(GL_UNPACK_SKIP_PIXELS,0);
    glActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_2D,palette);sampling();
    if(uploaded_palette!=palette_generation){glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,256,1,0,GL_RGBA,GL_UNSIGNED_BYTE,palette_rgb);uploaded_palette=palette_generation;}
    glActiveTexture(GL_TEXTURE2);glBindTexture(GL_TEXTURE_2D,mask_texture);sampling();
    if(uploaded_masks_level<0){
        static const unsigned char empty_masks[256*GG_MASKS]={0};
        glTexImage2D(GL_TEXTURE_2D,0,GL_R8UI,256,GG_MASKS,0,GL_RED_INTEGER,GL_UNSIGNED_BYTE,empty_masks);
        uploaded_masks_level=0;
    }
    if(generated_active&&uploaded_masks_level!=generated_profile->level){
        glTexImage2D(GL_TEXTURE_2D,0,GL_R8UI,256,GG_MASKS,0,GL_RED_INTEGER,GL_UNSIGNED_BYTE,generated_profile->masks);
        uploaded_masks_level=generated_profile->level;
    }
    glUniform1i(glGetUniformLocation(program,"indices"),0);glUniform1i(glGetUniformLocation(program,"pal"),1);
    glUniform1i(glGetUniformLocation(program,"maskTex"),2);
    glUniform1i(glGetUniformLocation(program,"generated"),generated_active);
    if(!generated_active){
        glUniform4fv(glGetUniformLocation(program,"lights"),4,light_pos);
        glUniform4fv(glGetUniformLocation(program,"colors"),4,light_color);
        glUniform3fv(glGetUniformLocation(program,"ambient"),1,medical_ambient);
    }
    float g=fmaxf(.1f,fminf(1.f,gShockPrefs.doGamma/100.f)),exponent=1.f/((1-(1-g)*(1-g))*2.2f);
    glUniform1f(glGetUniformLocation(program,"gammaExponent"),exponent);
    glUniform3f(glGetUniformLocation(program,"cameraWorld"),eye[0],eye[2],-eye[1]);
    GLint cut=glGetUniformLocation(program,"cutout"),em=glGetUniformLocation(program,"emissive"),rec=glGetUniformLocation(program,"receivers");
    GLint row=glGetUniformLocation(program,"maskRow"),gen_count=glGetUniformLocation(program,"genCount");
    GLint light_loc=glGetUniformLocation(program,"lights"),color_loc=glGetUniformLocation(program,"colors");
    GLint ambient_loc=glGetUniformLocation(program,"ambient"),counts_loc=glGetUniformLocation(program,"maskCounts");
    GLint planes_loc=glGetUniformLocation(program,"maskPlanes");
    GLint emissive_factor_loc=glGetUniformLocation(program,"emissiveFactor");
    const GpuPostPreset *polish=gpu_post_preset(player_struct.level,gShockPrefs.postChoice);
    int post_active=gShockPrefs.postEnabled&&gShockPrefs.doHorrorVisuals==4&&
        polish!=&post_presets[0]&&!post_failed;
    int polish_active=generated_active&&post_active;
    float emissive_multiplier=polish_active?gpu_post_multiplier(polish->emissive_multiplier):1.f;
    float light_multiplier=polish_active?gpu_post_multiplier(polish->generated_light_multiplier):1.f;
    int height_enabled=post_active&&gShockPrefs.postFxMode>=3;
    glUniform1f(glGetUniformLocation(program,"emissiveMultiplier"),emissive_multiplier);
    glUniform4f(glGetUniformLocation(program,"heightFog"),polish->height_fog_base,
        height_enabled?polish->height_fog_falloff:0.f,
        height_enabled?polish->height_fog_density:0.f,
        height_enabled?polish->height_fog_max:0.f);
    glUniform3fv(glGetUniformLocation(program,"heightFogColor"),1,polish->height_fog_color);
    generated_draws=generated_refs=generated_max_refs=generated_partials=0;
    glActiveTexture(GL_TEXTURE0);
    for(int i=0;i<ndraw;i++){
        Draw *d=&draws[i];GwTextureEntry *t=&texture_cache.entries[d->texture];GLuint *name=&texture_gl[d->texture];
        if(!*name){glGenTextures(1,name);glBindTexture(GL_TEXTURE_2D,*name);sampling();glTexImage2D(GL_TEXTURE_2D,0,GL_R8UI,t->w,t->h,0,GL_RED_INTEGER,GL_UNSIGNED_BYTE,t->pixels);++uploads;upload_bytes+=t->w*t->h;}
        else glBindTexture(GL_TEXTURE_2D,*name);
        glUniform1i(cut,d->cutout);glUniform1i(em,d->emissive);glUniform1i(rec,d->receivers);
        glUniform1f(emissive_factor_loc,d->source_multiplier);
        glUniform1i(row,d->mask_row);
        if(generated_active){
            float positions[16]={0},colors[16]={0},planes[64*4]={0};
            GLint counts[16]={0};
            int n=d->generated_surface?d->generated_surface->count:0,plane_end=0;
            const float *ambient=gg_profile_ambient(generated_profile,d->ambient_tile);
            glUniform3fv(ambient_loc,1,ambient);
            for(int j=0;j<n;j++){
                const GgReceiver *r=&d->generated_surface->lights[j];
                const GgLight *l=&generated_profile->lights[r->light_index];
                memcpy(positions+j*4,l->position,3*sizeof(float));positions[j*4+3]=l->radius;
                memcpy(colors+j*4,l->rgb,3*sizeof(float));colors[j*4+3]=l->intensity*source_multiplier[r->light_index]*light_multiplier;
                counts[j*4]=r->polygons;counts[j*4+1]=r->plane_counts[0];
                counts[j*4+2]=r->plane_counts[1];counts[j*4+3]=r->kind;
                if(r->kind==2)generated_partials++;
                for(int p=0;p<r->polygons;p++)for(int e=0;e<r->plane_counts[p];e++){
                    int slot=j*16+p*8+e;
                    memcpy(planes+slot*4,r->planes[p][e],4*sizeof(float));
                    if(slot+1>plane_end)plane_end=slot+1;
                }
            }
            glUniform1i(gen_count,n);
            if(n){glUniform4fv(light_loc,4,positions);glUniform4fv(color_loc,4,colors);
                glUniform4iv(counts_loc,4,counts);
                if(plane_end)glUniform4fv(planes_loc,plane_end,planes);}
            generated_draws++;generated_refs+=(unsigned)n;
            if((unsigned)n>generated_max_refs)generated_max_refs=(unsigned)n;
        }
        glDrawArrays(GL_TRIANGLES,d->first,d->count);
    }
    if(nstar_pixels){
        glUseProgram(star_program);glUniform1i(glGetUniformLocation(star_program,"pal"),1);
        glBufferData(GL_ARRAY_BUFFER,(size_t)nstar_pixels*sizeof(Vertex),star_pixels,GL_STREAM_DRAW);
        glDrawArrays(GL_POINTS,0,nstar_pixels);
    }
    GLenum error=glGetError();bridge_restore(&state);SDL_SetRenderTarget(renderer,old);
    if(inject("submit"))error=GL_OUT_OF_MEMORY;
    if(error){
        /* An allocated name does not prove its upload succeeded. Never reuse
         * potentially incomplete textures, palettes or old VBO contents after
         * a failed submission. The untouched software world owns this frame. */
        SDL_Log("GPU2 submit GL error=%x: discard GPU resources; software until mode retry",error);
        resources_dispose(0);disabled=1;gShockPrefs.doHorrorVisuals=0;
        failure="GPU submission/upload failure";return 0;
    }return 1;
}
static void log_gpu_tile_change(void) {
    static int seen,last_level,last_x,last_y,last_floor,last_ceiling;
    int x=fr_camera_last[0]>>16,y=fr_camera_last[1]>>16;
    if(!global_fullmap||x<0||x>=64||y<0||y>=64)return;
    MapElem *tile=MAP_GET_XY(x,y);
    int floor_height=me_height_flr(tile),ceiling_height=me_height_ceil(tile);
    if(seen&&last_level==player_struct.level&&last_x==x&&last_y==y&&
        last_floor==floor_height&&last_ceiling==ceiling_height)return;
    /* Successful GPU views only. Integer height fields are observed, never
     * changed; stationary views incur no repeated logs or filesystem access. */
    SDL_Log("GPU2 TILE frame=%u level=%d tile=%d,%d type=%d floor_raw=%d ceiling_raw=%d draws=%d camera=%.17g,%.17g,%.17g heading=%.17g pitch=%.17g raw_camera=%d,%d,%d,%d,%d",
        frame,player_struct.level,x,y,me_tiletype(tile),floor_height,ceiling_height,ndraw,
        fr_camera_last[0]/65536.,fr_camera_last[1]/65536.,fr_camera_last[2]/65536.,
        fr_camera_last[3]/256.,fr_camera_last[4]/256.,(int)fr_camera_last[0],(int)fr_camera_last[1],
        (int)fr_camera_last[2],(int)fr_camera_last[3],(int)fr_camera_last[4]);
    seen=1;last_level=player_struct.level;last_x=x;last_y=y;
    last_floor=floor_height;last_ceiling=ceiling_height;
}
void gpu_world_end(int previous) {
    if(active){
        uint64_t capture_end=SDL_GetPerformanceCounter();
        size_t size=(size_t)view_w*view_h;
        if(!failed&&software_capacity<size){unsigned char *p=realloc(software_world,size);if(!p)gpu_world_reject("software fallback allocation");else {software_world=p;software_capacity=size;}}
        if(!failed&&!submit()){failed=1;if(!failure)failure="GPU submission failure";}
        if(!failed)measure_view(capture_start,capture_end,SDL_GetPerformanceCounter());
        if(failed){retained=0;log_fallback(failure?failure:"unknown");}
        else {
            retained=command_valid=1;
            log_gpu_tile_change();
            for(int y=0;y<view_h;y++)memcpy(software_world+y*view_w,_fr->draw_canvas.bm.bits+y*_fr->draw_canvas.bm.row,view_w);
            /* Explicit world/UI boundary: discard completed software world only
             * after successful submission. Overlays are subsequently drawn by the
             * unmodified fr_send_view callback/copy pipeline. */
            for(int y=0;y<_fr->draw_canvas.bm.h;y++)memset(_fr->draw_canvas.bm.bits+y*_fr->draw_canvas.bm.row,255,_fr->draw_canvas.bm.w);
            if(frame==1||frame%300==0||diagnostic_due||logged_level!=player_struct.level){double ms=1000./SDL_GetPerformanceFrequency();SDL_Log("GPU1B LIVE frame=%u draws=%d triangles=%d capture_with_software_ms=%.3f submit_cpu_ms=%.3f uploads=%u bytes=%u palette_generation=%u fallbacks=%u GPU_execution_ms=unknown lights_z=%g,%g viewport=%d,%d,%d,%d",frame,ndraw,nvertex/3,(capture_end-capture_start)*ms,(SDL_GetPerformanceCounter()-capture_end)*ms,uploads,upload_bytes,palette_generation,fallbacks,light_pos[2],light_pos[6],destination.x,destination.y,destination.w,destination.h);}
            if(frame==1||frame%300==0||diagnostic_due||logged_level!=player_struct.level)SDL_Log("GPU2 CACHE level=%d camera=%g,%g,%g heading=%g pitch=%g types=%llx entries=%u bytes=%zu peak_bytes=%zu hits=%llu misses=%llu evictions=%llu rejections=%llu",player_struct.level,fr_camera_last[0]/65536.,fr_camera_last[1]/65536.,fr_camera_last[2]/65536.,fr_camera_last[3]/256.,fr_camera_last[4]/256.,(unsigned long long)terrain_types,texture_cache.resident_count,texture_cache.resident_bytes,texture_cache.high_water_bytes,(unsigned long long)texture_cache.hits,(unsigned long long)texture_cache.misses,(unsigned long long)texture_cache.evictions,(unsigned long long)texture_cache.rejections);
            if(generated_active&&(frame==1||frame%300==0||diagnostic_due||logged_level!=player_struct.level))
                SDL_Log("GENERATED1 frame=%u level=%d draws=%u refs=%u mean_refs=%.3f max_refs=%u partial_relations=%u portal-crossing-generated-light-disabled",
                    frame,player_struct.level,generated_draws,generated_refs,
                    generated_draws?(double)generated_refs/generated_draws:0.,generated_max_refs,generated_partials);
        }
    }
    if(active&&!failed){diagnostic_due=0;logged_level=player_struct.level;}
    active=previous;
}
void gpu_world_palette(int index,int count,const unsigned char *rgb) {
    for(int i=index;i<index+count&&i<256;i++){memcpy(palette_rgb+i*4,rgb,3);palette_rgb[i*4+3]=255;rgb+=3;}
    /* SetSDLPalette reserves display index 255 for black in normal gameplay.
     * Cutscenes own their software palette and never use this world target. */
    memset(palette_rgb+255*4,0,3);palette_rgb[255*4+3]=255;++palette_generation;
}
void gpu_world_mode_changed(void){
    if(inject("context")){SDLInjectPresenterReset();return;}
    retained=command_valid=0;disabled=0;diagnostic_due=1;
    qa_animation_reset();
    if(ready&&(inject("es3")||inject("shader")||inject("fbo")||post_failed))resources_dispose(0);
    if(ready&&inject("reset")){
        resources_dispose(0);
        SDL_Log("GPU1B QA controlled resource reset: deleted all GL resources; CPU metadata retained");
    }
}
void gpu_world_event(const SDL_Event *e) {
    if(e->type==SDL_RENDER_DEVICE_RESET)SDLRequestPresenterReset();
    if(e->type==SDL_RENDER_TARGETS_RESET){retained=0;SDL_Log("GPU1B target-reset: invalidate retained world");}
    if(e->type==SDL_APP_DIDENTERFOREGROUND){retained=0;SDL_Log("GPU1B foreground: require fresh primary view");}
}
static int restore_software_world(const char *reason) {
    log_fallback(reason);
    /* Fill only the world sentinel below the unmodified software UI. */
    for(int y=0;y<view_h;y++)for(int x=0;x<view_w;x++){
        int sx=destination.x+x,sy=destination.y+y;
        if(sx>=0&&sy>=0&&sx<drawSurface->w&&sy<drawSurface->h){unsigned char *p=(unsigned char*)drawSurface->pixels+sy*drawSurface->pitch+sx;if(*p==255)*p=software_world[y*view_w+x];}
    }
    retained=command_valid=0;gShockPrefs.doHorrorVisuals=0;return 0;
}
void gpu_world_presenter_lost(void) {
    if(command_valid&&software_world&&!UseCutscenePalette&&
       (_current_loop==GAME_LOOP||_current_loop==FULLSCREEN_LOOP))
        restore_software_world("presenter context reset");
    resources_dispose(1);context=NULL;
    active=retained=command_valid=0;disabled=1;diagnostic_due=1;
    gShockPrefs.doHorrorVisuals=0;
}
static SDL_Texture *post_process(void) {
    const GpuPostPreset *preset=gpu_post_preset(player_struct.level,gShockPrefs.postChoice);
    if(!gShockPrefs.postEnabled||gShockPrefs.doHorrorVisuals!=4||preset==&post_presets[0])return world;
    uint64_t started=SDL_GetPerformanceCounter();
    post_initialize();
    if(!post_ready)return world;
    SDL_Texture *old=SDL_GetRenderTarget(renderer);
    if(SDL_SetRenderTarget(renderer,world)<0){post_disable("world source bind");return world;}
    Bridge state;bridge_save(&state);
    glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,post_source);
    glCopyTexSubImage2D(GL_TEXTURE_2D,0,0,0,0,0,view_w,view_h);
    GLenum err=glGetError();bridge_restore(&state);
    SDL_SetRenderTarget(renderer,old);
    if(err){post_disable("source copy");return world;}
    if(SDL_SetRenderTarget(renderer,post_world)<0){post_disable("output bind");return world;}
    bridge_save(&state);
    glDisable(GL_BLEND);glDisable(GL_DEPTH_TEST);glDisable(GL_STENCIL_TEST);glDisable(GL_SCISSOR_TEST);glDisable(GL_CULL_FACE);
    glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);glViewport(0,0,view_w,view_h);
    glUseProgram(post_program);glBindVertexArray(post_vao);
    glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,post_source);
    glUniform1i(glGetUniformLocation(post_program,"sourceWorld"),0);
    glUniform4f(glGetUniformLocation(post_program,"gradeA"),preset->exposure,preset->contrast,preset->toe,preset->shoulder);
    glUniform4f(glGetUniformLocation(post_program,"gradeB"),preset->saturation,0,0,0);
    glUniform4f(glGetUniformLocation(post_program,"toneZones"),preset->highlight_rolloff,
        preset->midtone_lift,preset->nearblack_preserve,0.f);
    glUniform3fv(glGetUniformLocation(post_program,"shadowTint"),1,preset->shadow);
    glUniform3fv(glGetUniformLocation(post_program,"highlightTint"),1,preset->highlight);
    glUniform4f(glGetUniformLocation(post_program,"tintAndFx"),preset->shadow_strength,preset->highlight_strength,
        gShockPrefs.postGrain?preset->grain:0.f,0.f);
    glUniform3f(glGetUniformLocation(post_program,"vignette"),gShockPrefs.postVignette?preset->vignette:0.f,preset->radius,preset->softness);
    int fx=gShockPrefs.postFxMode;
    glUniform4f(glGetUniformLocation(post_program,"atmosphere"),preset->haze_start,
        fmaxf(preset->haze_end,preset->haze_start+.001f),
        fx>=1?preset->haze_strength:0.f,fx>=2?preset->far_desaturation:0.f);
    glUniform2f(glGetUniformLocation(post_program,"distanceFx"),fx>=2?preset->far_contrast:0.f,
        fx>=3?gpu_post_drift(player_struct.game_time/(float)CIT_CYCLE,preset->drift_period,preset->drift_strength):0.f);
    glUniform3fv(glGetUniformLocation(post_program,"hazeColor"),1,preset->haze_color);
    if(fx>=3)glUniform3fv(glGetUniformLocation(post_program,"shadowBias"),1,preset->shadow_bias);
    else glUniform3f(glGetUniformLocation(post_program,"shadowBias"),0.f,0.f,0.f);
    glDrawArrays(GL_TRIANGLES,0,3);
    err=glGetError();bridge_restore(&state);SDL_SetRenderTarget(renderer,old);
    if(err||inject("post-submit")){post_disable("fullscreen pass");return world;}
    post_samples[post_timing_samples++]=(SDL_GetPerformanceCounter()-started)*1000./SDL_GetPerformanceFrequency();
    if(post_timing_samples==300){
        double total=0;for(int i=0;i<300;i++)total+=post_samples[i];
        qsort(post_samples,300,sizeof(double),compare_sample);
        SDL_Log("POST1 TIMING samples=300 cpu_mean_ms=%.3f p95=%.3f GPU_execution_ms=unknown preset=%s fx=%d",total/300,post_samples[284],preset->name,fx);
        post_timing_samples=0;
    }
    return post_world;
}
int gpu_world_compose(void) {
    if((gShockPrefs.doHorrorVisuals!=3&&gShockPrefs.doHorrorVisuals!=4)||!command_valid||UseCutscenePalette||(_current_loop!=GAME_LOOP&&_current_loop!=FULLSCREEN_LOOP))return 0;
    /* Retained commands include owned pixels and copied vertices: rebuild a
     * paused view after reset/foreground without re-running simulation/traversal.
     * Palette cycling can update that same view without re-uploading indexed art. */
    if(!retained||!ready||context!=SDL_GL_GetCurrentContext()||uploaded_palette!=palette_generation){
        if(!submit())return restore_software_world("retained view restoration failure");
        retained=1;
    }
    SDL_Texture *composed=post_process();
    if(inject("compose")||SDL_RenderCopy(renderer,composed,NULL,&destination)<0)
        return restore_software_world("GPU world composition failure");
    qa_animation_composed();
    return 1;
}

/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef SHOCK_GPU_POST_H
#define SHOCK_GPU_POST_H
#include <math.h>

/* Centralized art-direction table. Choice zero selects the level default;
 * manual choices 1..3 and 4..6 retain the Medical/Executive QA comparisons. */
typedef struct {
    const char *name;
    float exposure, contrast, toe, shoulder, saturation;
    float shadow[3], highlight[3];
    float shadow_strength, highlight_strength;
    float vignette, radius, softness, grain;
    float haze_color[3], haze_start, haze_end, haze_strength;
    float far_desaturation, far_contrast;
    float shadow_bias[3];
    float drift_period, drift_strength;
    /* POLISH1 fields default to zero in older presets. A zero multiplier is
     * interpreted as one so their generated lighting stays unchanged. */
    float highlight_rolloff, midtone_lift, nearblack_preserve;
    float emissive_multiplier, generated_light_multiplier;
    float height_fog_base, height_fog_falloff, height_fog_density, height_fog_max;
    float height_fog_color[3];
} GpuPostPreset;

/* Distance is packed into the world target's otherwise unused alpha channel.
 * A zero-alpha clear pixel has no atmosphere, even at the edge of the view. */
#define GPU_POST_DISTANCE_RANGE 24.f
static inline float gpu_post_clamp(float x,float low,float high) {
    return fmaxf(low,fminf(high,x));
}
static inline float gpu_post_distance_factor(float distance,float start,float end) {
    if(end<=start||distance<=start)return 0.f;
    float t=gpu_post_clamp((distance-start)/(end-start),0.f,1.f);
    return t*t*(3.f-2.f*t);
}
static inline float gpu_post_shadow_weight(float lum) {
    return 1.f-gpu_post_distance_factor(gpu_post_clamp(lum,0.f,1.f),.12f,.62f);
}
static inline float gpu_post_highlight(float value,float strength) {
    value=gpu_post_clamp(value,0.f,1.f);
    float excess=fmaxf(value-.64f,0.f);
    return value<=.64f?value:.64f+excess/(1.f+2.f*gpu_post_clamp(strength,0.f,1.f)*excess);
}
static inline float gpu_post_midtone_weight(float lum) {
    return gpu_post_distance_factor(lum,.03f,.16f)*
        (1.f-gpu_post_distance_factor(lum,.36f,.68f));
}
static inline float gpu_post_nearblack(float value,float strength) {
    return value*(1.f+gpu_post_clamp(strength,0.f,.25f)*
        (1.f-gpu_post_distance_factor(value,.02f,.16f)));
}
static inline float gpu_post_multiplier(float value) {
    return value>0.f?gpu_post_clamp(value,.5f,1.5f):1.f;
}
static inline float gpu_post_height_fog(float height,float distance,float base,float falloff,float density,float max_value) {
    if(falloff<=0.f||density<=0.f||max_value<=0.f)return 0.f;
    float low=gpu_post_clamp((base-height)/falloff,0.f,1.f);
    low=low*low*(3.f-2.f*low);
    return fminf(gpu_post_clamp(max_value,0.f,.1f),
        low*gpu_post_distance_factor(distance,2.f,10.f)*gpu_post_clamp(density,0.f,.1f));
}
static inline float gpu_post_drift(float game_seconds,float period,float strength) {
    if(period<=0.f||strength<=0.f)return 0.f;
    return sinf(6.28318530718f*fmodf(game_seconds,period)/period)*
        gpu_post_clamp(strength,0.f,.02f);
}

static inline int gpu_post_resolve(int level,int choice) {
    /* Citadel archive order. Gamma is within Executive (6); Delta, Alpha,
     * and Beta are separate maps (11..13). Cyber/special maps stay neutral. */
    static const unsigned char auto_by_level[10]={7,1,8,9,10,11,4,12,13,14};
    if(choice<0||choice>6)choice=0;
    if(level==1)return choice>=1&&choice<=3?choice:1;
    if(level==6)return choice>=4&&choice<=6?choice:4;
    if(level>=0&&level<10)return auto_by_level[level];
    return level>=11&&level<=13?15:0;
}

const GpuPostPreset *gpu_post_preset(int level, int choice);
const char *gpu_post_choice_name(int choice);

#endif

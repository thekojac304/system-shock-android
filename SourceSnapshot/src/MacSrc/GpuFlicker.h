/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef SHOCK_GPU_FLICKER_H
#define SHOCK_GPU_FLICKER_H
#include <stdint.h>

typedef struct { uint8_t low,high,last,changes; } GpuFlicker;
static inline void gpu_flicker_reset(GpuFlicker *s) {
    s->low=255;s->high=0;s->last=255;s->changes=0;
}
/* An unchanged transient sample is not proof that a source is dynamic. Two
 * actual transitions are required before the original scalar response is used. */
static inline float gpu_flicker_sample(GpuFlicker *s,unsigned live,unsigned permanent) {
    if(live<s->low)s->low=(uint8_t)live;
    if(live>s->high)s->high=(uint8_t)live;
    if(s->last!=255&&s->last!=live&&s->changes<255)s->changes++;
    s->last=(uint8_t)live;
    if(s->changes<2||s->low==s->high)return 1.f;
    float brightness=(15.f-(float)permanent+(float)live)/15.f;
    return brightness<0.f?0.f:brightness>1.f?1.f:brightness;
}
#endif

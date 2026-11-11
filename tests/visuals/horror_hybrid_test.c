/* Focused pure logic checks; built for the same Android ABI as the QA app. */
#include "GpuFlicker.h"
#include "GpuPost.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

int main(void) {
    GpuFlicker state;
    gpu_flicker_reset(&state);
    for(int i=0;i<300;i++)assert(gpu_flicker_sample(&state,4,10)==1.f);
    assert(state.changes==0); /* Stable source never changes its emissive. */
    assert(gpu_flicker_sample(&state,10,10)==1.f);
    assert(gpu_flicker_sample(&state,4,10)==9.f/15.f);
    assert(gpu_flicker_sample(&state,10,10)==1.f);
    gpu_flicker_reset(&state);
    assert(gpu_flicker_sample(&state,0,15)==1.f);
    assert(gpu_flicker_sample(&state,15,15)==1.f);
    assert(gpu_flicker_sample(&state,0,15)==0.f);
    assert(gpu_post_resolve(1,0)==1);
    assert(gpu_post_resolve(6,0)==4);
    /* Archive level order, including Reactor at zero and Executive Groves. */
    static const int auto_presets[10]={7,1,8,9,10,11,4,12,13,14};
    for(int level=0;level<10;level++){
        assert(gpu_post_resolve(level,0)==auto_presets[level]);
        assert(gpu_post_resolve(level,99)==auto_presets[level]);
    }
    assert(gpu_post_resolve(1,3)==3);
    assert(gpu_post_resolve(6,6)==6);
    assert(gpu_post_resolve(6,99)==4);
    assert(gpu_post_resolve(6,2)==4);
    assert(gpu_post_resolve(1,5)==1);
    assert(gpu_post_resolve(2,3)==8);
    assert(gpu_post_resolve(0,0)==7);
    assert(gpu_post_resolve(9,0)==14);
    assert(gpu_post_resolve(10,0)==0); /* Special/cyber levels remain neutral. */
    for(int level=11;level<=13;level++)assert(gpu_post_resolve(level,0)==15);
    assert(gpu_post_resolve(14,0)==0);
    assert(gpu_post_resolve(-1,0)==0);
    assert(gpu_post_distance_factor(2.f,3.f,12.f)==0.f);
    assert(gpu_post_distance_factor(15.f,3.f,12.f)==1.f);
    assert(fabsf(gpu_post_distance_factor(7.5f,3.f,12.f)-.5f)<.0001f);
    assert(gpu_post_distance_factor(8.f,12.f,3.f)==0.f);
    for(int i=-100;i<=100;i++){
        float distance=i*.25f;
        float factor=gpu_post_distance_factor(distance,2.5f,13.f);
        assert(factor>=0.f&&factor<=1.f);
        assert(factor*.06f<=.06f); /* Far saturation reduction. */
        assert(factor*.035f<=.035f); /* Far contrast reduction. */
    }
    assert(gpu_post_shadow_weight(0.f)==1.f);
    assert(gpu_post_shadow_weight(1.f)==0.f);
    assert(gpu_post_shadow_weight(.2f)>gpu_post_shadow_weight(.8f));
    assert(gpu_post_shadow_weight(0.f)*0.f==0.f); /* Pure black is not tinted. */
    /* Executive high-only rolloff does not touch midtones. */
    assert(gpu_post_highlight(.4f,.55f)==.4f);
    assert(gpu_post_highlight(1.f,.55f)<.91f);
    assert(gpu_post_highlight(1.f,.55f)>.8f);
    for(int i=0;i<=100;i++){
        float x=i*.01f;
        float y=gpu_post_highlight(x,.55f);
        assert(y>=0.f&&y<=1.f);
        if(i)assert(y>=gpu_post_highlight((i-1)*.01f,.55f));
    }
    /* Bounded midtone zone and black-preserving near-black soft knee. */
    assert(gpu_post_midtone_weight(0.f)==0.f);
    assert(gpu_post_midtone_weight(.25f)>.9f);
    assert(gpu_post_midtone_weight(.8f)==0.f);
    assert(gpu_post_nearblack(0.f,.08f)==0.f);
    assert(gpu_post_nearblack(.05f,.08f)>.05f);
    assert(gpu_post_nearblack(.2f,.08f)==.2f);
    /* The reviewed fixture and generated receiver scales are bounded. */
    assert(gpu_post_multiplier(0.f)==1.f); /* Older presets are neutral. */
    assert(fabsf(gpu_post_multiplier(.88f)-.88f)<.00001f);
    assert(fabsf(gpu_post_multiplier(1.05f)-1.05f)<.00001f);
    assert(gpu_post_multiplier(99.f)==1.5f);
    /* Existing shadow/highlight weights remain separate and pure black safe. */
    assert(gpu_post_shadow_weight(.1f)>gpu_post_shadow_weight(.7f));
    assert(gpu_post_distance_factor(.9f,.45f,.9f)==1.f);
    /* Fog affects low distant geometry only and caps its contribution. */
    assert(gpu_post_height_fog(.1f,10.f,.85f,.9f,.045f,.04f)>0.f);
    assert(gpu_post_height_fog(2.f,10.f,.85f,.9f,.045f,.04f)==0.f);
    assert(gpu_post_height_fog(.1f,1.f,.85f,.9f,.045f,.04f)==0.f);
    assert(gpu_post_height_fog(.1f,20.f,.85f,.9f,1.f,.04f)<=.04f);
    assert(gpu_post_height_fog(.1f,20.f,.85f,0.f,.045f,.04f)==0.f);
    assert(gpu_post_drift(123.f,18.f,0.f)==0.f);
    assert(gpu_post_drift(123.f,0.f,.004f)==0.f);
    for(int i=0;i<10000;i++)assert(fabsf(gpu_post_drift(i*.01f,18.f,.004f))<=.004001f);
    assert(fabsf(gpu_post_drift(3600.f,18.f,.004f))<.00001f);
    puts("HORROR_HYBRID_TEST=PASS");
    return 0;
}

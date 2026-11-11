/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "horror_profile.h"
#include <string.h>
#include <limits.h>

/* VIS1 / medical-healing-suite-v1. Inspected ARCHIVE.DAT resources 4105/4107.
 * Level default = Original; no inferred rules in VIS1. Named tile mask below,
 * then surface/material rules, then the adapter's special-vision override.
 * The neutral landing separates the cool bay and warm supply alcove. */
const HvTile hv_medical_tiles[] = {
    {26,20,6,4,20,219,37,216,HV_WARM},
    {26,21,7,4,20,219,37,216,HV_WARM},
    {27,21,9,11,13,51,35,219,HV_COOL},
    {28,21,1,11,13,51,35,219,HV_COOL},
    {26,22,7,4,13,219,37,216,HV_ORIGINAL},
    {27,22,7,4,13,38,37,219,HV_COOL},
    {28,22,7,8,13,38,37,219,HV_COOL},
    {29,22,1,11,13,108,37,219,HV_COOL},
    {30,22,1,11,13,45,37,219,HV_COOL},
    {26,23,7,4,13,219,37,216,HV_ORIGINAL},
    {27,23,7,4,13,38,37,219,HV_COOL},
    {28,23,7,8,13,38,37,219,HV_COOL},
    {29,23,1,11,13,108,37,219,HV_COOL},
    {30,23,1,11,13,45,37,219,HV_COOL},
    {26,24,7,4,20,219,35,219,HV_ORIGINAL},
    {27,24,9,11,13,51,35,219,HV_COOL},
    {28,24,1,11,13,51,35,219,HV_COOL}
};
const size_t hv_medical_tile_count = sizeof(hv_medical_tiles)/sizeof(hv_medical_tiles[0]);
static const HvLightPool medical_pools[] = {
    {30*256,24*256,3*256,4*256,3*256},
    {26*256+128,20*256+128,2*256,3*256,2*256}
};

const HvTile *hv_region_tile(int x, int y) {
    for (size_t i=0; i<hv_medical_tile_count; ++i)
        if (hv_medical_tiles[i].x==x && hv_medical_tiles[i].y==y) return &hv_medical_tiles[i];
    return NULL;
}

int hv_resolve_bank(const HvTile *tile, int surface, int texture_id) {
    if (!tile) return HV_ORIGINAL;
    if (texture_id==35 || texture_id==216) return HV_ORIGINAL; /* inspected luminous panels */
    /* Floor family stays neutral: navigation, painted hazard colors and pickup
     * silhouettes keep their identity. Scalar lighting still applies. */
    if (surface==HV_FLOOR && (texture_id==37 || texture_id==35)) return HV_ORIGINAL;
    return tile->bank;
}

static int clampi(int v, int lo, int hi) { return v<lo ? lo : v>hi ? hi : v; }
static int pool(int x, int y, int cx, int cy, int radius, int strength) {
    int dx=x-cx, dy=y-cy;
    int d=(dx<0 ? -dx : dx)+(dy<0 ? -dy : dy);
    return d>=radius ? 0 : strength*(radius-d)/radius;
}

int hv_darkness(int permanent, int ceiling, int x, int y) {
    /* 8.8 darkness, evaluated before original transient/lamp/global lighting.
     * Roll off deep rows to retain detail, then sculpt two stationary pools.
     * No camera dependence, RNG, world writes or interpolated palette indices. */
    int d=permanent;
    if (d>8*256) d=8*256+(d-8*256)*3/4;
    d += ceiling ? 1152 : 320;
    for (size_t i=0;i<sizeof(medical_pools)/sizeof(medical_pools[0]);++i) {
        const HvLightPool *p=&medical_pools[i];
        d-=pool(x,y,p->x,p->y,p->radius,ceiling ? p->ceiling_strength : p->floor_strength);
    }
    return clampi(d,0,(ceiling ? 12 : 11)*256);
}

/* Corner-sampled source pools in the SAME mask: display wall, luminous bed
 * edges, supply ceiling. Strongest squared Manhattan falloff wins. */
static const HvLightPool horror_pools[] = {
    {30*256,24*256,384,13*256,10*256},
    {28*256,21*256,384,10*256,4*256},
    {28*256,24*256,384,10*256,4*256},
    {26*256,22*256+128,384,10*256,16*256},
    {26*256+128,20*256+128,512,26*256,22*256}
};

int hv_horror_darkness(int permanent, int surface, int ceiling, int x, int y) {
    /* Accelerate deep darkness. Saturate ambient BEFORE subtracting pools so
     * fixtures can lift even originally dark cells. Functional lights follow. */
    int d=permanent<=4*256 ? permanent : 4*256+(permanent-4*256)*7/4;
    d+=ceiling ? 10*256 : surface==HV_FLOOR ? 1152 : 1664;
    d=clampi(d,0,15*256);
    int lift=0;
    for (size_t i=0;i<sizeof(horror_pools)/sizeof(horror_pools[0]);++i) {
        const HvLightPool *p=&horror_pools[i];
        int weight=pool(x,y,p->x,p->y,p->radius,256);
        int strength=ceiling ? p->ceiling_strength : p->floor_strength;
        int candidate=strength*weight*weight/65536;
        if (candidate>lift) lift=candidate;
    }
    d-=lift;
    /* Keep the ramp/landing and object silhouettes readable. Walls and ceiling
     * recesses can reach the deepest original shade row. */
    int cap=surface==HV_OBJECT ? 11*256 : surface==HV_FLOOR ? 13*256 : 15*256;
    if (surface==HV_FLOOR && y>=22*256 && y<=24*256) cap=11*256;
    /* Keep authored warm-pool highlights on row 1 for amber bank identity.
     * Real full-bright/lamp terms and exempt fixtures still reach Original 0. */
    int warm=x>=26*256 && x<=27*256 && y>=20*256 && y<=21*256;
    return clampi(d,warm ? 256 : 0,cap);
}

void hv_build_banks(uint8_t banks[HV_BANK_COUNT][4096], const uint8_t base[4096],
                    const uint8_t pal[768]) {
    uint8_t protected[256];
    for (int c=0;c<256;++c) {
        int invariant=1;
        for (int row=0;row<16;++row) if (base[row*256+c]!=c) invariant=0;
        protected[c]=(c<32 || c==255 || invariant);
    }
    memcpy(banks[HV_ORIGINAL],base,4096);
    for (int bank=HV_COOL;bank<HV_BANK_COUNT;++bank)
        for (int row=0;row<16;++row) for (int c=0;c<256;++c) {
            int out=base[row*256+c];
            /* Row zero includes full-bright objects/flash-lit vertices. Never
             * alter reserved sources OR map ordinary pixels into reserved ramps. */
            if (!row || protected[c] || protected[out]) {
                banks[bank][row*256+c]=(uint8_t)out;
                continue;
            }
            int r=pal[3*out],g=pal[3*out+1],b=pal[3*out+2];
            int hi=r>g ? r : g; if (b>hi) hi=b;
            int lo=r<g ? r : g; if (b<lo) lo=b;
            /* Restrict chroma change on colored art/skin. Bright clinical whites
             * remain close to neutral; most direction belongs to midtones. */
            int amount=hi>190 ? 24 : (hi-lo>45 ? 28 : 96);
            if (row==1) amount/=2;
            int tr,tg,tb;
            if (bank>=HV_HORROR_COOL) {
                int luminance=(2*r+4*g+b)/7;
                amount=bank==HV_HORROR_WARM ? 128 : row==1 ? 48 : row==2 ? 88 : 128;
                /* Source hue retains warning red / indicator green identity
                 * even where the original shade row has already lost chroma. */
                int sr=pal[3*c],sg=pal[3*c+1],sb=pal[3*c+2];
                if ((sr>sg*2 && sr>sb*2) || (sg>sr*2 && sg>sb*2)) amount/=4;
                /* Amber/gold controls and orange/skin ramps must not turn cyan.
                 * Their original shading remains; only the hue push is limited. */
                if (sr>=sg && sr>sb*2 && sg>sb*2) amount/=8;
                /* Owned-palette rose/skin and leather ramps; do not recolor
                 * living objects cyan just to strengthen neutral architecture. */
                if ((c>=48 && c<=63) || (c>=128 && c<=159)) amount/=4;
                if (bank==HV_HORROR_COOL) {
                    tr=luminance*30/100; tg=luminance*85/100; tb=luminance*125/100;
                } else {
                    tr=luminance*185/100; tg=luminance*90/100; tb=luminance*18/100;
                }
            }
            else if (bank==HV_COOL) { tr=r*82/100; tg=g+2; tb=b*112/100+4; }
            else {
                /* Slate artwork already carries blue chroma. A small blue
                 * multiplier quantizes back to that same ramp, so target a
                 * low-saturation amber response by luminance in this bank. */
                int luminance=(2*r+4*g+b)/7;
                tr=luminance*125/100+5;
                tg=luminance;
                tb=luminance*70/100;
            }
            tr=clampi(r+(tr-r)*amount/128,0,255);
            tg=clampi(g+(tg-g)*amount/128,0,255);
            tb=clampi(b+(tb-b)*amount/128,0,255);
            int best=out, error=INT_MAX;
            for (int k=32;k<255;++k) if (!protected[k]) {
                int dr=pal[3*k]-tr,dg=pal[3*k+1]-tg,db=pal[3*k+2]-tb;
                int e=2*dr*dr+4*dg*dg+3*db*db;
                if (e<error || (e==error && k==out)) { error=e; best=k; }
            }
            banks[bank][row*256+c]=(uint8_t)best;
        }
}

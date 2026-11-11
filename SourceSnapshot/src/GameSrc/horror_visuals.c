#include "GpuWorld.h"
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "horror_visuals.h"
#include "map.h"
#include "mapflags.h"
#include "textmaps.h"
#include "frintern.h"
#include "frflags.h"
#include "player.h"
#include "drugs.h"
#include "Prefs.h"
#include "OpenGL.h"
#include "SDL.h"
#include <string.h>
#include <stdio.h>

extern unsigned char ppall[];
extern char curr_clut_table;
extern bool UseCutscenePalette;
static unsigned char banks[HV_BANK_COUNT][4096];
static const HvTile *tiles[64*64];
static int ready, compatible, view_active, scalar_active;
static int banks_seen;

void horror_visuals_init(void) {
    hv_build_banks(banks,shading_table,ppall);
    ready=1;
    SDL_Log("VIS1B banks=medical-v1b ready; Original / Conservative / Horror; palette unchanged");
}

void horror_visuals_map_loaded(void) {
    compatible=0;
    memset(tiles,0,sizeof(tiles));
    if (player_struct.level!=1 || !global_fullmap || global_fullmap->cyber ||
        global_fullmap->x_size!=64 || global_fullmap->y_size!=64) return;
    /* Validate static geometry/material identity, never transient light, object
     * chains, quest state, door state or other mutable save data. Fail closed. */
    for (size_t i=0;i<hv_medical_tile_count;++i) {
        const HvTile *t=&hv_medical_tiles[i];
        const MapElem *m=MAP_GET_XY(t->x,t->y);
        if (me_tiletype(m)!=t->type || me_height_flr(m)!=t->floor_height ||
            me_height_ceil(m)!=t->ceiling_height || loved_textures[me_tmap_wall(m)]!=t->wall ||
            loved_textures[me_tmap_flr(m)]!=t->floor || loved_textures[me_tmap_ceil(m)]!=t->ceiling) {
            SDL_Log("VIS1 incompatible Medical signature at %u,%u; Original fallback",t->x,t->y);
            return;
        }
    }
    for (size_t i=0;i<hv_medical_tile_count;++i) {
        const HvTile *t=&hv_medical_tiles[i];
        tiles[t->y*64+t->x]=t;
    }
    compatible=1;
    SDL_Log("VIS1 region=medical-healing-suite-v1 level=1 tiles=%u validated",(unsigned)hv_medical_tile_count);
}

HorrorScope horror_visuals_begin_view(void) {
    HorrorScope previous={0};
    previous.previous_view=view_active;
    previous.previous_seen=banks_seen;
    banks_seen=0;
    /* A nested camera/picking draw must not inherit a parent's bank even when
     * the nested view is deliberately excluded from VIS1. */
    if (view_active) {
        previous.changed=1;
        previous.previous_scalar=scalar_active;
        previous.light=grd_screen->ltab;
        previous.clut=_fr_clut_list[0];
        previous.fill_type=gr_get_fill_type();
        previous.fill_parm=gr_get_fill_parm();
        gr_set_light_tab(curr_clut_table ? _fr_clut_list[(unsigned char)curr_clut_table] : shading_table);
        _fr_clut_list[0]=shading_table;
        scalar_active=0;
        if (previous.fill_type==FILL_CLUT) gr_set_fill_parm(grd_screen->ltab);
    }
    view_active=gShockPrefs.doHorrorVisuals>0 && gShockPrefs.doHorrorVisuals<3 && ready && compatible && player_struct.level==1 &&
        global_fullmap && !global_fullmap->cyber && !UseCutscenePalette && !use_opengl() &&
        !curr_clut_table && grd_screen->ltab==shading_table &&
        !player_struct.drug_status[DRUG_LSD] && !player_struct.drug_status[DRUG_SIGHT] &&
        !(_fr_curflags & (FR_PICKUPM_MASK|FR_HACKCAM_MASK|FR_PALETTE_MASK|FR_NORENDR_MASK|FR_SOLIDFR_MASK));
    return previous;
}
void horror_visuals_end_view(HorrorScope previous) {
    static int last_seen=-1;
    if (view_active && !previous.previous_view && banks_seen!=last_seen) {
        SDL_Log("VIS1 visible region=medical-healing-suite-v1 banks_mask=%d (VIS1 cool=2 warm=4; VIS1B cool=8 warm=16); camera tile=%d,%d",
                banks_seen,fr_camera_last[0]>>16,fr_camera_last[1]>>16);
        last_seen=banks_seen;
    }
    horror_visuals_restore(previous);
    view_active=previous.previous_view;
    banks_seen=previous.previous_seen;
}

HorrorScope horror_visuals_bind(int x, int y, int surface, int texture_id, int exempt) {
    HorrorScope scope={0};
    if (!view_active) return scope;
    const HvTile *tile=(x>=0 && x<64 && y>=0 && y<64) ? tiles[y*64+x] : NULL;
    if (!tile && !scalar_active) return scope;
    /* Inspected emissive texture families: protect fixture brightness as well
     * as hue. Surrounding surfaces carry the authored contrast. */
    if (texture_id==35 || texture_id==216) exempt=1;
    scope.changed=1;
    scope.previous_scalar=scalar_active;
    scope.light=grd_screen->ltab;
    scope.clut=_fr_clut_list[0];
    scope.fill_type=gr_get_fill_type();
    scope.fill_parm=gr_get_fill_parm();
    scalar_active=tile && !exempt ? surface+1 : 0;
    int bank=exempt ? HV_ORIGINAL : hv_resolve_bank(tile,surface,texture_id);
    /* Horror grades the landing cold and lets the alcove floor carry warmth;
     * Conservative retains its original neutral landing/floor treatment. */
    if (gShockPrefs.doHorrorVisuals==2 && tile && !exempt)
        bank=tile->bank==HV_WARM ? HV_HORROR_WARM : HV_HORROR_COOL;
    if (tile) banks_seen|=1<<bank;
    unsigned char *table=bank==HV_ORIGINAL ? shading_table : banks[bank];
    gr_set_light_tab(table);
    _fr_clut_list[0]=table;
    return scope;
}
void horror_visuals_restore(HorrorScope scope) {
    if (!scope.changed) return;
    gr_set_light_tab(scope.light);
    _fr_clut_list[0]=scope.clut;
    gr_set_fill_type(scope.fill_type);
    gr_set_fill_parm(scope.fill_parm);
    scalar_active=scope.previous_scalar;
}
int horror_visuals_scalar_active(void) { return view_active && scalar_active; }
int horror_visuals_active(void) { return view_active; }
int horror_visuals_light(int permanent, int transient, int ceiling, int x, int y) {
    int d=(gShockPrefs.doHorrorVisuals==2 ?
        hv_horror_darkness(permanent*256,scalar_active-1,ceiling,x*256,y*256) :
        hv_darkness(permanent*256,ceiling,x*256,y*256))-transient*256;
    return d<0 ? 0 : d;
}
void horror_visuals_set_mode(int mode) {
#ifdef RP5NP_GPU_WORLD
    gShockPrefs.doHorrorVisuals=mode>=0 && mode<=4 ? mode : 0;
#else
    gShockPrefs.doHorrorVisuals=mode>=0 && mode<=2 ? mode : 0;
#endif
#ifdef RP5NP_GPU_WORLD
    gpu_world_mode_changed();
#endif
    SavePrefs();
    SDL_Log("VIS1 mode=%s; applies at next world draw; gamma=%d",
            mode==4 ? "GPU GENERATED" : mode==3 ? "GPU PROOF" : mode==2 ? "VIS1B HORROR" : mode==1 ? "VIS1 CONSERVATIVE" : "ORIGINAL",gShockPrefs.doGamma);
}

uint64_t horror_visuals_clock(void) {
#ifdef RP5NP_QA_TOOLS
    static int enabled=-1;
    if (enabled<0) {
        FILE *f=fopen("vis1-diagnostics.txt","r");
        enabled=f!=NULL || SDL_getenv("SHOCK_VIS1_TIMING")!=NULL;
        if (f) fclose(f);
    }
    return enabled ? SDL_GetPerformanceCounter() : 0;
#else
    return 0;
#endif
}

void horror_visuals_measure(uint64_t start) {
#ifdef RP5NP_QA_TOOLS
    static int count, last_mode=-1;
    static uint64_t total, maximum;
    if (!start) return;
    int mode=gShockPrefs.doHorrorVisuals;
    if (mode!=last_mode) { count=0; total=maximum=0; last_mode=mode; }
    uint64_t elapsed=SDL_GetPerformanceCounter()-start;
    total+=elapsed; if (elapsed>maximum) maximum=elapsed;
    if (++count==300) {
        double ms=1000.0/SDL_GetPerformanceFrequency();
        SDL_Log("VIS1 timing mode=%d world_mean_ms=%.3f world_max_ms=%.3f n=%d",mode,total*ms/count,maximum*ms,count);
        count=0; total=maximum=0;
    }
#else
    (void)start;
#endif
}

void horror_visuals_present_measure(uint64_t start, uint64_t converted, uint64_t submitted) {
#ifdef RP5NP_QA_TOOLS
    static int count,last_mode=-1,slow;
    static uint64_t conversion,presentation,interval,previous,maximum;
    if (!start) return;
    uint64_t now=SDL_GetPerformanceCounter(),freq=SDL_GetPerformanceFrequency();
    if (last_mode!=gShockPrefs.doHorrorVisuals) {
        count=slow=0; conversion=presentation=interval=maximum=previous=0;
        last_mode=gShockPrefs.doHorrorVisuals;
    }
    if (previous) {
        uint64_t dt=start-previous;
        interval+=dt; if(dt>maximum) maximum=dt;
        if(dt>freq/50) ++slow; /* >20 ms, a diagnostic count, not a vsync verdict */
        conversion+=converted-start; presentation+=now-submitted;
        if (++count==300) {
            double ms=1000.0/freq;
            SDL_Log("VIS1 presentation mode=%d n=%d conversion_upload_ms=%.3f present_ms=%.3f interval_ms=%.3f max_interval_ms=%.3f over20ms=%d",
                last_mode,count,conversion*ms/count,presentation*ms/count,interval*ms/count,maximum*ms,slow);
            count=slow=0; conversion=presentation=interval=maximum=0;
        }
    }
    previous=start;
#else
    (void)start; (void)converted; (void)submitted;
#endif
}

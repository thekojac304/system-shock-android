/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef SHOCK_GPU_GENERATED_H
#define SHOCK_GPU_GENERATED_H
#include "GpuSurfaceId.h"
#include <stdint.h>

enum { GG_MAX_LIGHTS=4, GG_MAX_POLYGONS=2, GG_MAX_PLANES=8, GG_MASKS=15 };
typedef struct {
    float position[3],rgb[3],intensity,radius;
    int32_t material,source_object;
} GgLight;
typedef struct {
    uint16_t light_index;
    uint8_t kind,polygons,plane_counts[GG_MAX_POLYGONS];
    uint64_t portal_dependencies;
    float planes[GG_MAX_POLYGONS][GG_MAX_PLANES][4];
} GgReceiver;
typedef struct {
    GpuSurfaceId id;
    uint8_t count;
    GgReceiver lights[GG_MAX_LIGHTS];
} GgSurface;
typedef struct {
    int level,light_count,surface_count,region_count;
    uint32_t map_keys[4096];
    uint16_t region_for_tile[4096];
    float default_ambient[3],(*region_ambient)[3];
    GgLight *lights;
    GgSurface *surfaces;
    uint8_t masks[GG_MASKS][256];
} GgProfile;

GgProfile *gg_profile_load(int level);
int gg_profile_supported(int level);
void gg_profile_free(GgProfile *profile);
const char *gg_profile_error(void);
const GgSurface *gg_profile_find(const GgProfile *profile,GpuSurfaceId id);
int gg_profile_map_matches(const GgProfile *profile,const uint32_t *keys);
const float *gg_profile_ambient(const GgProfile *profile,int tile);
int gg_mask_row(int material);
#endif

/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef SHOCK_GPU_SURFACE_ID_H
#define SHOCK_GPU_SURFACE_ID_H
#include <stdint.h>

/* Source terrain identity, independent of GPU buffers and traversal order.
 * A changed terrain/neighbor geometry key must not match a generated receiver.
 * Objects and unsupported terrain capture carry valid=0 and keep neutral GPU.
 */
enum { GWS_FLOOR=1, GWS_CEILING=2, GWS_EXTERNAL_WALL=3, GWS_INTERNAL_WALL=4 };
typedef struct {
    uint32_t geometry_key;
    uint32_t neighbor_geometry_key;
    uint8_t level,x,y,kind,face,subface,terrain_type,valid;
} GpuSurfaceId;

static inline GpuSurfaceId gpu_surface_id_make(int level,int x,int y,int kind,
        int face,int subface,int terrain_type,uint32_t geometry_key,
        uint32_t neighbor_geometry_key) {
    GpuSurfaceId id={0};
    if(level<0||level>15||x<0||x>63||y<0||y>63||
       kind<GWS_FLOOR||kind>GWS_INTERNAL_WALL||face<0||face>63||
       subface<0||subface>7||terrain_type<1||terrain_type>50)return id;
    id.geometry_key=geometry_key;id.neighbor_geometry_key=neighbor_geometry_key;
    id.level=(uint8_t)level;
    id.x=(uint8_t)x;id.y=(uint8_t)y;id.kind=(uint8_t)kind;
    id.face=(uint8_t)face;id.subface=(uint8_t)subface;
    id.terrain_type=(uint8_t)terrain_type;id.valid=1;
    return id;
}
static inline int gpu_surface_id_equal(GpuSurfaceId a,GpuSurfaceId b) {
    return a.valid&&b.valid&&a.geometry_key==b.geometry_key&&
        a.neighbor_geometry_key==b.neighbor_geometry_key&&
        a.level==b.level&&a.x==b.x&&a.y==b.y&&a.kind==b.kind&&
        a.face==b.face&&a.subface==b.subface&&a.terrain_type==b.terrain_type;
}
#ifdef RP5NP_GPU_WORLD
void gpu_world_surface_set(int x,int y,int kind,int face,int subface,int terrain_type,
        uint32_t geometry_key,uint32_t neighbor_geometry_key);
void gpu_world_surface_subface(int subface);
void gpu_world_surface_clear(void);
#else
#define gpu_world_surface_set(x,y,k,f,s,t,g,n) ((void)0)
#define gpu_world_surface_subface(s) ((void)0)
#define gpu_world_surface_clear() ((void)0)
#endif
#endif

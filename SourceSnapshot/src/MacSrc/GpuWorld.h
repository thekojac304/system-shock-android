/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef SHOCK_GPU_WORLD_H
#define SHOCK_GPU_WORLD_H
#include "3d.h"
#include "GpuSurfaceId.h"
#include <SDL.h>
#ifdef RP5NP_GPU_WORLD
int gpu_world_begin(void);
void gpu_world_end(int previous);
int gpu_world_active(void);
void gpu_world_reject(const char *reason);
void gpu_world_face(int n, g3s_phandle *p, grs_bitmap *bm, int lit);
void gpu_world_polygon(long color, int n, g3s_phandle *p, int gouraud);
void gpu_world_star_pixel(int x, int y, unsigned char index);
void gpu_world_sprite(grs_bitmap *bm, grs_vertex **p, g3s_phandle anchor, const unsigned char *clut);
void gpu_world_object(int id, int type, int lit);
void gpu_world_object_end(void);
void gpu_world_palette(int index, int count, const unsigned char *rgb);
void gpu_world_event(const SDL_Event *event);
int gpu_world_compose(void);
void gpu_world_mode_changed(void);
/* Presenter owns SDL texture retirement on a replaced context. No GL calls. */
void gpu_world_presenter_lost(void);
#else
#define gpu_world_begin() 0
#define gpu_world_end(previous) ((void)(previous))
#define gpu_world_active() 0
#define gpu_world_reject(reason) ((void)0)
#define gpu_world_face(n,p,bm,lit) ((void)0)
#define gpu_world_polygon(c,n,p,g) ((void)0)
#define gpu_world_star_pixel(x,y,index) ((void)0)
#define gpu_world_sprite(bm,p,a,c) ((void)0)
#define gpu_world_object(id,type,lit) ((void)0)
#define gpu_world_object_end() ((void)0)
#define gpu_world_palette(i,n,p) ((void)0)
#define gpu_world_event(e) ((void)0)
#define gpu_world_compose() 0
#define gpu_world_mode_changed() ((void)0)
#define gpu_world_presenter_lost() ((void)0)
#endif
#endif

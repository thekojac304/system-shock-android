/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef QA_SPAWN_H
#define QA_SPAWN_H
#include <stdbool.h>
#include <stdint.h>
/* Original archive tile encoding: 64x64, 16 bytes per tile. No engine state. */
static bool qa_spawn_tile(const uint8_t *tiles, int x, int y, int zshift, bool allow_hazard) {
    if (x<1 || x>62 || y<1 || y>62 || zshift<0 || zshift>5) return false;
    const uint8_t *p=tiles+(y*64+x)*16;
    /* Ceiling is stored as a distance DOWN from the top of the map. */
    int floor=p[1]&31, ceiling=32-(p[2]&31);
    return p[0]==1 && (allow_hazard || (!(p[1]&128) && !(p[2]&128))) &&
        p[4]==0 && p[5]==0 && ceiling-floor>=(1<<zshift);
}
/* Require an empty, flat tile with standing clearance and at least one empty
   same-height exit. Prefer the most open surroundings, then the map centre.
   Re-evaluate the current save; never fall back to a solid/occupied tile. */
static bool qa_spawn_find(const uint8_t *tiles, int zshift, bool allow_hazard, int *x, int *y, int *z) {
    int best=100000;
    for (int ty=2; ty<62; ++ty) for (int tx=2; tx<62; ++tx) {
        const int dx[]={0,-1,1,0,0}, dy[]={0,0,0,-1,1};
        int floor=tiles[(ty*64+tx)*16+1]&31;
        if (!qa_spawn_tile(tiles,tx,ty,zshift,allow_hazard)) continue;
        int exits=0;
        for (int i=1;i<5;++i) {
            int nx=tx+dx[i],ny=ty+dy[i];
            if (qa_spawn_tile(tiles,nx,ny,zshift,allow_hazard) &&
                (tiles[(ny*64+nx)*16+1]&31)==floor) ++exits;
        }
        int distance=(4-exits)*10000+(tx-32)*(tx-32)+(ty-32)*(ty-32);
        if (exits>0 && distance<best) {
            best=distance; *x=tx; *y=ty;
            /* Same standing centre as player_create_initial: half of the
               standard pelvis height (0xbd00 fixed-point), in object units. */
            *z=floor*8+(0x5e80>>(13-zshift));
        }
    }
    return best<100000;
}
#endif

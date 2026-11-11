/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef SHOCK_GPU_TEXTURE_CACHE_H
#define SHOCK_GPU_TEXTURE_CACHE_H

#include <stddef.h>
#include <stdint.h>

enum {
    GW_CACHE_MAX_SLOTS = 512,
    GW_CACHE_MAX_DIMENSION = 1024,
    GW_CACHE_INVALID = -1,
    GW_CACHE_CAPACITY = -2,
    GW_CACHE_ALLOC = -3
};
#define GW_CACHE_DEFAULT_BYTES ((size_t)16 * 1024 * 1024)

/* Entries have stable indices, matching captured Draw.texture values. Pixels
 * belong to the cache, never to the original resource or RSD decode scratch. */
typedef struct {
    unsigned char *pixels;
    uint64_t hash;
    int w, h;
    int pinned;
    int older, newer;
} GwTextureEntry;

/* Called before a resident entry is discarded. The callback must only mark or
 * queue retirement of renderer state: acquire/clear may run outside a GL pass.
 * The renderer must retire old names before uploading replacements, and must
 * forget queued names (without deletion) when their GL context is lost. */
typedef void (*GwTextureRetire)(void *context, int slot);

typedef struct {
    GwTextureEntry entries[GW_CACHE_MAX_SLOTS];
    size_t byte_limit, resident_bytes, high_water_bytes;
    unsigned slot_limit, resident_count, high_water_count;
    uint64_t hits, misses, evictions, rejections;
    int oldest, newest;
    GwTextureRetire retire;
    void *retire_context;
} GwTextureCache;

/* Initialize once, or only after clear. Invalid limits create a disabled cache.
 * There are no GL calls, threads, file accesses or source-content mutations. */
void gw_cache_init(GwTextureCache *cache, unsigned slots, size_t bytes,
                   GwTextureRetire retire, void *context);

/* Release pins ONLY after all prior commands are invalidated. Do not call for
 * paused replay, palette changes, picking/secondary views or GL reconstruction.
 * All acquired entries remain pinned until the next begin or clear. */
void gw_cache_begin(GwTextureCache *cache);

/* Hash + dimensions + full row equality determine identity. Stride padding is
 * ignored. Every hit/new entry is pinned immediately. Source bytes must remain
 * valid throughout this call and must not alias cache-owned storage. Returns a
 * stable slot, or one of the negative errors above. If the pinned working set
 * cannot fit, no resident entry is changed. Allocation failure may retire
 * unpinned entries; existing pinned commands remain valid in every failure. */
int gw_cache_acquire(GwTextureCache *cache, const unsigned char *pixels,
                     int width, int height, int stride);

/* Invalidate captured commands before clear. Statistics and limits survive. */
void gw_cache_clear(GwTextureCache *cache);

#endif

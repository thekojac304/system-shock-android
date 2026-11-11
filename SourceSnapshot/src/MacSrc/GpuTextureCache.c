/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "GpuTextureCache.h"

#include <stdlib.h>
#include <string.h>

static void unlink_entry(GwTextureCache *cache, int slot) {
    GwTextureEntry *entry = &cache->entries[slot];
    if (entry->older >= 0) cache->entries[entry->older].newer = entry->newer;
    else cache->oldest = entry->newer;
    if (entry->newer >= 0) cache->entries[entry->newer].older = entry->older;
    else cache->newest = entry->older;
}

static void make_newest(GwTextureCache *cache, int slot) {
    GwTextureEntry *entry = &cache->entries[slot];
    entry->older = cache->newest;
    entry->newer = -1;
    if (cache->newest >= 0) cache->entries[cache->newest].newer = slot;
    else cache->oldest = slot;
    cache->newest = slot;
}

static void discard_entry(GwTextureCache *cache, int slot) {
    GwTextureEntry *entry = &cache->entries[slot];
    if (cache->retire) cache->retire(cache->retire_context, slot);
    unlink_entry(cache, slot);
    cache->resident_bytes -= (size_t)entry->w * (size_t)entry->h;
    --cache->resident_count;
    free(entry->pixels);
    memset(entry, 0, sizeof(*entry));
    entry->older = entry->newer = -1;
}

void gw_cache_init(GwTextureCache *cache, unsigned slots, size_t bytes,
                   GwTextureRetire retire, void *context) {
    memset(cache, 0, sizeof(*cache));
    if (slots > 0 && slots <= GW_CACHE_MAX_SLOTS && bytes > 0) {
        cache->slot_limit = slots;
        cache->byte_limit = bytes;
    }
    cache->oldest = cache->newest = -1;
    cache->retire = retire;
    cache->retire_context = context;
}

void gw_cache_begin(GwTextureCache *cache) {
    for (unsigned i = 0; i < cache->slot_limit; ++i)
        cache->entries[i].pinned = 0;
}

int gw_cache_acquire(GwTextureCache *cache, const unsigned char *pixels,
                     int width, int height, int stride) {
    if (!pixels || width <= 0 || height <= 0 ||
        width > GW_CACHE_MAX_DIMENSION || height > GW_CACHE_MAX_DIMENSION ||
        stride < width || (size_t)(height - 1) > (SIZE_MAX - (size_t)width) / (size_t)stride) {
        ++cache->rejections;
        return GW_CACHE_INVALID;
    }
    const size_t bytes = (size_t)width * (size_t)height;
    uint64_t hash = UINT64_C(1469598103934665603);
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
            hash ^= pixels[(size_t)y * (size_t)stride + (size_t)x];
            hash *= UINT64_C(1099511628211);
        }

    int free_slot = -1;
    size_t pinned_bytes = 0;
    unsigned pinned_count = 0;
    for (unsigned i = 0; i < cache->slot_limit; ++i) {
        GwTextureEntry *entry = &cache->entries[i];
        if (!entry->pixels) {
            if (free_slot < 0) free_slot = (int)i;
            continue;
        }
        if (entry->hash == hash && entry->w == width && entry->h == height) {
            int same = 1;
            for (int y = 0; y < height; ++y)
                if (memcmp(entry->pixels + (size_t)y * (size_t)width,
                           pixels + (size_t)y * (size_t)stride, (size_t)width)) {
                    same = 0;
                    break;
                }
            if (same) {
                entry->pinned = 1;
                unlink_entry(cache, (int)i);
                make_newest(cache, (int)i);
                ++cache->hits;
                return (int)i;
            }
        }
        if (entry->pinned) {
            ++pinned_count;
            pinned_bytes += (size_t)entry->w * (size_t)entry->h;
        }
    }
    ++cache->misses;
    if (bytes > cache->byte_limit || pinned_count >= cache->slot_limit ||
        pinned_bytes > cache->byte_limit - bytes) {
        ++cache->rejections;
        return GW_CACHE_CAPACITY;
    }

    /* Preflight above guarantees enough evictable storage. Free before malloc
     * so the owned-pixel budget also holds during replacement, not just after.
     * Linked LRU order avoids wraparound of frame/usage counters. */
    int candidate = cache->oldest;
    while (free_slot < 0 || cache->resident_bytes > cache->byte_limit - bytes) {
        while (candidate >= 0 && cache->entries[candidate].pinned)
            candidate = cache->entries[candidate].newer;
        if (candidate < 0) {
            ++cache->rejections;
            return GW_CACHE_CAPACITY;
        }
        int next = cache->entries[candidate].newer;
        discard_entry(cache, candidate);
        ++cache->evictions;
        if (free_slot < 0) free_slot = candidate;
        candidate = next;
    }

    unsigned char *owned = malloc(bytes);
    if (!owned) {
        ++cache->rejections;
        return GW_CACHE_ALLOC;
    }
    for (int y = 0; y < height; ++y)
        memcpy(owned + (size_t)y * (size_t)width,
               pixels + (size_t)y * (size_t)stride, (size_t)width);
    GwTextureEntry *entry = &cache->entries[free_slot];
    entry->pixels = owned;
    entry->hash = hash;
    entry->w = width;
    entry->h = height;
    entry->pinned = 1;
    make_newest(cache, free_slot);
    cache->resident_bytes += bytes;
    ++cache->resident_count;
    if (cache->resident_bytes > cache->high_water_bytes)
        cache->high_water_bytes = cache->resident_bytes;
    if (cache->resident_count > cache->high_water_count)
        cache->high_water_count = cache->resident_count;
    return free_slot;
}

void gw_cache_clear(GwTextureCache *cache) {
    for (unsigned i = 0; i < cache->slot_limit; ++i)
        if (cache->entries[i].pixels) discard_entry(cache, (int)i);
}

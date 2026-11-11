/* SPDX-License-Identifier: GPL-3.0-or-later
 * Isolated production-cache tests; no game assets, GL context or device needed.
 * Build: cc -std=c11 -Wall -Wextra -Werror -O2 gpu_texture_cache_test.c -o cache-test
 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fail_allocation;
static void *cache_test_malloc(size_t bytes) {
    if (fail_allocation) return NULL;
    return malloc(bytes);
}

/* Compile the exact production implementation with only its allocator replaced
 * to exercise failure deterministically. No copied cache logic is tested. */
#define malloc cache_test_malloc
#include "../../SourceSnapshot/src/MacSrc/GpuTextureCache.c"
#undef malloc

typedef struct {
    GwTextureCache cache;
    unsigned retires, retired[GW_CACHE_MAX_SLOTS];
    int clearing;
} Fixture;

static void retire(void *context, int slot) {
    Fixture *fixture = context;
    assert(slot >= 0 && slot < (int)fixture->cache.slot_limit);
    assert(fixture->cache.entries[slot].pixels);
    assert(fixture->clearing || !fixture->cache.entries[slot].pinned);
    ++fixture->retires;
    ++fixture->retired[slot];
}

static void setup(Fixture *fixture, unsigned slots, size_t bytes) {
    memset(fixture, 0, sizeof(*fixture));
    gw_cache_init(&fixture->cache, slots, bytes, retire, fixture);
}

static void verify(const Fixture *fixture) {
    const GwTextureCache *cache = &fixture->cache;
    unsigned count = 0, linked = 0;
    size_t bytes = 0;
    unsigned char seen[GW_CACHE_MAX_SLOTS] = {0};
    for (unsigned i = 0; i < cache->slot_limit; ++i) {
        const GwTextureEntry *entry = &cache->entries[i];
        if (entry->pixels) {
            ++count;
            bytes += (size_t)entry->w * (size_t)entry->h;
        }
    }
    int previous = -1;
    for (int i = cache->oldest; i >= 0; i = cache->entries[i].newer) {
        assert(i < (int)cache->slot_limit && !seen[i]);
        seen[i] = 1;
        assert(cache->entries[i].pixels);
        assert(cache->entries[i].older == previous);
        previous = i;
        ++linked;
    }
    assert(previous == cache->newest);
    assert(linked == count && count == cache->resident_count);
    assert(bytes == cache->resident_bytes);
    assert(bytes <= cache->byte_limit && count <= cache->slot_limit);
    assert(cache->high_water_bytes <= cache->byte_limit);
    assert(cache->high_water_count <= cache->slot_limit);
}

static void finish(Fixture *fixture) {
    fixture->clearing = 1;
    gw_cache_clear(&fixture->cache);
    assert(fixture->cache.resident_count == 0);
    assert(fixture->cache.resident_bytes == 0);
    verify(fixture);
}

static void identity_and_ownership(void) {
    Fixture f;
    setup(&f, 8, 128);
    unsigned char stride[] = {1, 2, 99, 3, 4, 98};
    unsigned char packed[] = {1, 2, 3, 4};
    int first = gw_cache_acquire(&f.cache, stride, 2, 2, 3);
    assert(first >= 0);
    assert(gw_cache_acquire(&f.cache, packed, 2, 2, 2) == first);
    stride[2] = 10;
    stride[5] = 11;
    assert(gw_cache_acquire(&f.cache, stride, 2, 2, 3) == first);
    stride[0] = 7;
    int changed = gw_cache_acquire(&f.cache, stride, 2, 2, 3);
    assert(changed >= 0 && changed != first);
    assert(!memcmp(f.cache.entries[first].pixels, packed, sizeof(packed)));
    assert(gw_cache_acquire(&f.cache, packed, 1, 4, 1) != first);
    assert(f.cache.hits == 2 && f.cache.misses == 3);
    verify(&f);
    finish(&f);
}

static void collision_is_not_identity(void) {
    Fixture f;
    setup(&f, 4, 64);
    const unsigned char first[] = {1, 2, 3, 4};
    const unsigned char second[] = {4, 3, 2, 1};
    int a = gw_cache_acquire(&f.cache, first, 2, 2, 2);
    int b = gw_cache_acquire(&f.cache, second, 2, 2, 2);
    assert(a >= 0 && b >= 0 && a != b);
    /* Force the precise collision case without weakening production hashing. */
    f.cache.entries[a].hash = f.cache.entries[b].hash;
    assert(gw_cache_acquire(&f.cache, second, 2, 2, 2) == b);
    assert(!memcmp(f.cache.entries[a].pixels, first, sizeof(first)));
    verify(&f);
    finish(&f);
}

static void lru_and_retained_pins(void) {
    Fixture f;
    setup(&f, 3, 12);
    unsigned char a[] = {1, 1, 1, 1}, b[] = {2, 2, 2, 2};
    unsigned char c[] = {3, 3, 3, 3}, d[] = {4, 4, 4, 4};
    int ia = gw_cache_acquire(&f.cache, a, 2, 2, 2);
    int ib = gw_cache_acquire(&f.cache, b, 2, 2, 2);
    int ic = gw_cache_acquire(&f.cache, c, 2, 2, 2);
    assert(ia >= 0 && ib >= 0 && ic >= 0);
    for (int replay = 0; replay < 100; ++replay) {
        /* Paused replay/context rebuilding never begins another command set. */
        assert(gw_cache_acquire(&f.cache, d, 2, 2, 2) == GW_CACHE_CAPACITY);
        assert(!memcmp(f.cache.entries[ia].pixels, a, sizeof(a)));
        assert(!memcmp(f.cache.entries[ib].pixels, b, sizeof(b)));
        assert(!memcmp(f.cache.entries[ic].pixels, c, sizeof(c)));
    }
    assert(f.retires == 0);
    gw_cache_begin(&f.cache);
    assert(gw_cache_acquire(&f.cache, a, 2, 2, 2) == ia);
    /* B is now the oldest unpinned entry. A's active command remains stable. */
    assert(gw_cache_acquire(&f.cache, d, 2, 2, 2) == ib);
    assert(f.retired[ib] == 1 && f.retired[ia] == 0 && f.retired[ic] == 0);
    verify(&f);
    finish(&f);
}

static void streaming_and_slot_pressure(void) {
    Fixture f;
    setup(&f, GW_CACHE_MAX_SLOTS, GW_CACHE_DEFAULT_BYTES);
    const unsigned char hot[] = {255, 255, 255, 255};
    int hot_slot = gw_cache_acquire(&f.cache, hot, 2, 2, 2);
    for (unsigned i = 0; i < 10000; ++i) {
        unsigned char animation[] = {(unsigned char)i, (unsigned char)(i >> 8), 42, 7};
        gw_cache_begin(&f.cache);
        assert(gw_cache_acquire(&f.cache, hot, 2, 2, 2) == hot_slot);
        int slot = gw_cache_acquire(&f.cache, animation, 2, 2, 2);
        assert(slot >= 0 && slot != hot_slot);
        assert(!memcmp(f.cache.entries[slot].pixels, animation, sizeof(animation)));
        verify(&f);
    }
    assert(f.cache.resident_count == GW_CACHE_MAX_SLOTS);
    assert(f.cache.evictions == 10001 - GW_CACHE_MAX_SLOTS);
    assert(f.retired[hot_slot] == 0);
    gw_cache_begin(&f.cache);
    for (unsigned i = 0; i < f.cache.slot_limit; ++i) {
        unsigned char copy[4];
        memcpy(copy, f.cache.entries[i].pixels, sizeof(copy));
        assert(gw_cache_acquire(&f.cache, copy, 2, 2, 2) == (int)i);
    }
    const unsigned char additional[] = {1, 2, 3, 4};
    uint64_t evictions = f.cache.evictions;
    assert(gw_cache_acquire(&f.cache, additional, 2, 2, 2) == GW_CACHE_CAPACITY);
    assert(f.cache.evictions == evictions);
    verify(&f);
    finish(&f);
}

static void byte_pressure_and_allocation_failure(void) {
    Fixture f;
    setup(&f, 8, 16);
    unsigned char first[] = {1, 1, 1, 1}, second[] = {2, 2, 2, 2};
    unsigned char third[] = {3, 3, 3, 3}, large[12] = {9};
    int pinned = gw_cache_acquire(&f.cache, first, 2, 2, 2);
    assert(gw_cache_acquire(&f.cache, second, 2, 2, 2) >= 0);
    assert(gw_cache_acquire(&f.cache, third, 2, 2, 2) >= 0);
    assert(gw_cache_acquire(&f.cache, large, 3, 4, 3) == GW_CACHE_CAPACITY);
    assert(f.retires == 0); /* Failed preflight is transactional. */
    gw_cache_begin(&f.cache);
    assert(gw_cache_acquire(&f.cache, first, 2, 2, 2) == pinned);
    fail_allocation = 1;
    assert(gw_cache_acquire(&f.cache, large, 3, 4, 3) == GW_CACHE_ALLOC);
    fail_allocation = 0;
    assert(f.retires == 2 && f.cache.resident_bytes == 4);
    assert(!memcmp(f.cache.entries[pinned].pixels, first, sizeof(first)));
    assert(gw_cache_acquire(&f.cache, large, 3, 4, 3) >= 0);
    assert(f.cache.resident_bytes == 16);
    verify(&f);
    finish(&f);
}

static void full_budget_and_invalid_inputs(void) {
    Fixture f;
    setup(&f, GW_CACHE_MAX_SLOTS, GW_CACHE_DEFAULT_BYTES);
    unsigned char *image = malloc(1024 * 1024);
    assert(image);
    memset(image, 0, 1024 * 1024);
    for (int i = 0; i < 16; ++i) {
        image[0] = (unsigned char)i;
        assert(gw_cache_acquire(&f.cache, image, 1024, 1024, 1024) >= 0);
    }
    image[0] = 16;
    assert(gw_cache_acquire(&f.cache, image, 1024, 1024, 1024) == GW_CACHE_CAPACITY);
    assert(f.cache.resident_bytes == GW_CACHE_DEFAULT_BYTES);
    gw_cache_begin(&f.cache);
    assert(gw_cache_acquire(&f.cache, image, 1024, 1024, 1024) >= 0);
    assert(f.cache.resident_bytes == GW_CACHE_DEFAULT_BYTES && f.retires == 1);
    assert(gw_cache_acquire(&f.cache, NULL, 1, 1, 1) == GW_CACHE_INVALID);
    assert(gw_cache_acquire(&f.cache, image, 0, 1, 1) == GW_CACHE_INVALID);
    assert(gw_cache_acquire(&f.cache, image, 1, -1, 1) == GW_CACHE_INVALID);
    assert(gw_cache_acquire(&f.cache, image, 1025, 1, 1025) == GW_CACHE_INVALID);
    assert(gw_cache_acquire(&f.cache, image, 2, 2, 1) == GW_CACHE_INVALID);
    assert(gw_cache_acquire(&f.cache, image, 2, 2, -1) == GW_CACHE_INVALID);
    verify(&f);
    free(image);
    finish(&f);
    setup(&f, GW_CACHE_MAX_SLOTS + 1, 16);
    unsigned char pixel = 1;
    assert(gw_cache_acquire(&f.cache, &pixel, 1, 1, 1) == GW_CACHE_CAPACITY);
    finish(&f);
}

int main(void) {
    identity_and_ownership();
    collision_is_not_identity();
    lru_and_retained_pins();
    streaming_and_slot_pressure();
    byte_pressure_and_allocation_failure();
    full_budget_and_invalid_inputs();
    puts("PASS: production indexed texture cache: 10,000 animation frames, LRU, slot/byte bounds, retained pins, ownership, stride, dimensions, collisions and allocation failure");
    return 0;
}

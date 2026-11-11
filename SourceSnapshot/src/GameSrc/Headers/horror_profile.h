/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef HORROR_PROFILE_H
#define HORROR_PROFILE_H
#include <stdint.h>
#include <stddef.h>

enum { HV_ORIGINAL, HV_COOL, HV_WARM, HV_HORROR_COOL, HV_HORROR_WARM, HV_BANK_COUNT };
enum { HV_WALL, HV_FLOOR, HV_CEILING, HV_OBJECT };
/* Backend-independent authoring records. Texture IDs, not level-local slots.
 * Generated/external authoring can emit these records without renderer edits. */
typedef struct {
    uint8_t x, y, type, floor_height, ceiling_height;
    uint16_t wall, floor, ceiling;
    uint8_t bank;
} HvTile;
/* All coordinates/strengths use 8.8 units. A generator can emit the same
 * records; no renderer code needs to know which fixture suggested a pool. */
typedef struct {
    int x, y, radius, floor_strength, ceiling_strength;
} HvLightPool;
extern const HvTile hv_medical_tiles[];
extern const size_t hv_medical_tile_count;
const HvTile *hv_region_tile(int x, int y);
int hv_resolve_bank(const HvTile *tile, int surface, int texture_id);
int hv_darkness(int permanent_8_8, int ceiling, int x_8_8, int y_8_8);
int hv_horror_darkness(int permanent_8_8, int surface, int ceiling, int x_8_8, int y_8_8);
void hv_build_banks(uint8_t banks[HV_BANK_COUNT][4096], const uint8_t base[4096],
                    const uint8_t palette[768]);
#endif

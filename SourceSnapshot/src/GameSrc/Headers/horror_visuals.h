/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef HORROR_VISUALS_H
#define HORROR_VISUALS_H
#include <stdint.h>
#include "horror_profile.h"

/* Stack-owned draw scopes: all early returns are inside the wrapped operation.
 * Inactive scopes do not read or write renderer state. */
typedef struct {
    int changed, previous_scalar, previous_view, previous_seen;
    unsigned char *light, *clut;
    long fill_type;
    intptr_t fill_parm;
} HorrorScope;
void horror_visuals_init(void);
void horror_visuals_map_loaded(void);
HorrorScope horror_visuals_begin_view(void);
void horror_visuals_end_view(HorrorScope previous);
HorrorScope horror_visuals_bind(int x, int y, int surface, int texture_id, int exempt);
void horror_visuals_restore(HorrorScope scope);
int horror_visuals_light(int permanent, int transient, int ceiling, int sample_x, int sample_y);
int horror_visuals_scalar_active(void);
int horror_visuals_active(void);
void horror_visuals_set_mode(int mode);
void horror_visuals_measure(uint64_t start);
uint64_t horror_visuals_clock(void);
void horror_visuals_present_measure(uint64_t start, uint64_t converted, uint64_t submitted);
#endif

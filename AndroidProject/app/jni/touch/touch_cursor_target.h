/* Cursor target decisions use the game's UI region tree, in UI coordinates. */
#ifndef TOUCH_CURSOR_TARGET_H
#define TOUCH_CURSOR_TARGET_H

#include <stdbool.h>
#include "region.h"
#include "touch_target_types.h"

typedef bool (*TouchRegionTest)(const LGRegion *region, int x, int y);

static inline bool touch_region_contains(const LGRegion *r, int x, int y) {
    return r && r->r && !(r->status_flags & INVISIBLE_FLAG) &&
        x >= r->abs_x && y >= r->abs_y &&
        x < r->abs_x + RectWidth(r->r) && y < r->abs_y + RectHeight(r->r);
}

/* Fullscreen overlays are normally raised from z=0 to z=2 above the z=1
   view. Some permanent controls (the side icons) stay at z=0, however, and
   are still mouse-active. The caller identifies those original UI regions. */
static inline TouchCursorTarget touch_classify_view_region(
    const LGRegion *view, int x, int y, TouchRegionTest permanent_control,
    TouchRegionTest object_region) {
    if (!touch_region_contains(view, x, y)) return TOUCH_TARGET_NONE;
    for (const LGRegion *child = view->sub_region; child; child = child->next_region) {
        if (!touch_region_contains(child, x, y)) continue;
        if (child->z <= view->z &&
            !(permanent_control && permanent_control(child, x, y))) continue;
        return object_region && object_region(child, x, y)
            ? TOUCH_TARGET_UI_OBJECT : TOUCH_TARGET_UI_CONTROL;
    }
    return TOUCH_TARGET_WORLD;
}

#endif

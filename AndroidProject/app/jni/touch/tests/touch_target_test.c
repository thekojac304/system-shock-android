#include "touch_cursor_target.h"
#include <assert.h>
#include <stdio.h>

static const LGRegion *icon, *object;
static bool permanent(const LGRegion *r, int x, int y) {
    (void)x; (void)y; return r == icon;
}
static bool item(const LGRegion *r, int x, int y) {
    (void)x; (void)y; return r == object;
}
int main(void) {
    LGRect bounds = {{0,0},{200,200}}, control_rect={{20,20},{60,60}};
    LGRect object_rect={{80,80},{130,130}};
    LGRegion view={.r=&bounds,.z=1};
    LGRegion control={.abs_x=20,.abs_y=20,.r=&control_rect,.z=0};
    LGRegion gump={.abs_x=80,.abs_y=80,.r=&object_rect,.z=2};
    control.next_region=&gump; view.sub_region=&control;
    icon=&control; object=&gump;
    assert(touch_classify_view_region(&view,30,30,permanent,item)==TOUCH_TARGET_UI_CONTROL);
    assert(touch_classify_view_region(&view,90,90,permanent,item)==TOUCH_TARGET_UI_OBJECT);
    assert(touch_classify_view_region(&view,70,70,permanent,item)==TOUCH_TARGET_WORLD);
    assert(touch_classify_view_region(&view,201,70,permanent,item)==TOUCH_TARGET_NONE);
    icon=NULL;
    assert(touch_classify_view_region(&view,30,30,permanent,item)==TOUCH_TARGET_WORLD);
    icon=&control;
    gump.status_flags=INVISIBLE_FLAG;
    assert(touch_classify_view_region(&view,90,90,permanent,item)==TOUCH_TARGET_WORLD);
    gump.status_flags=0; object=NULL;
    assert(touch_classify_view_region(&view,90,90,permanent,item)==TOUCH_TARGET_UI_CONTROL);
    puts("TOUCH_TARGET_TESTS=PASS (permanent control, raised object, world, hidden overlay)");
    return 0;
}

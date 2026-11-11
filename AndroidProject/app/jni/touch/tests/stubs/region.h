#ifndef TOUCH_TEST_REGION_H
#define TOUCH_TEST_REGION_H
typedef struct {int x, y;} LGPoint;
typedef struct {LGPoint ul, lr;} LGRect;
typedef struct _Region {
    int abs_x, abs_y, z;
    unsigned long status_flags;
    LGRect *r;
    struct _Region *sub_region, *next_region;
} LGRegion;
#define INVISIBLE_FLAG 0x80000000UL
#define RectWidth(r) ((r)->lr.x - (r)->ul.x)
#define RectHeight(r) ((r)->lr.y - (r)->ul.y)
#endif

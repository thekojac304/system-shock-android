/* QA visual comparison choices. SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef TOUCH_VISUAL_COMPARE_H
#define TOUCH_VISUAL_COMPARE_H

typedef struct { int enabled, choice; } TouchPostChoice;

static inline int touch_compare_next_mode(int mode) {
    return mode==0 || mode==1 ? 2 : mode==2 ? 3 : mode==3 ? 4 : 0;
}

static inline TouchPostChoice touch_compare_next_post(int level, int enabled, int choice) {
    int first=level==1 ? 1 : level==6 ? 4 : 0;
    if (!enabled) return (TouchPostChoice){1,0};
    if (!first) return (TouchPostChoice){0,0};
    if (choice==0 || choice<first || choice>first+2)
        return (TouchPostChoice){1,first};
    if (choice<first+2) return (TouchPostChoice){1,choice+1};
    return (TouchPostChoice){0,0};
}

static inline const char *touch_compare_mode_label(int mode) {
    switch (mode) {
    case 1: return "VIS1 CONS";
    case 2: return "VIS1B HORROR";
    case 3: return "GPU PROOF";
    case 4: return "GPU GENERATED";
    default: return "ORIGINAL";
    }
}

static inline const char *touch_compare_post_label(int level, int enabled, int choice) {
    if (!enabled) return "POST OFF";
    if (!choice) {
        static const char *const auto_labels[10]={
            "AUTO REACTOR","AUTO MEDICAL","AUTO RESEARCH","AUTO MAINTENANCE",
            "AUTO STORAGE","AUTO FLIGHT","AUTO EXECUTIVE","AUTO ENGINEERING",
            "AUTO SECURITY","AUTO BRIDGE"};
        return level>=0&&level<10?auto_labels[level]:
            level>=11&&level<=13?"AUTO GROVE":"POST AUTO";
    }
    if (level==1) {
        if (choice==1) return "MED A";
        if (choice==2) return "MED B";
        if (choice==3) return "MED C";
    }
    if (level==6) {
        if (choice==4) return "EXEC A";
        if (choice==5) return "EXEC B";
        if (choice==6) return "EXEC C";
    }
    return "POST AUTO"; /* Cross-deck choices resolve to the deck default. */
}
#endif

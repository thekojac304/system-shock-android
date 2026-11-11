/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef QA_TOOLS_H
#define QA_TOOLS_H
#include <stdbool.h>
bool qa_tools_enabled(void);
bool qa_tools_god(void);
void qa_tools_reset(void);
void qa_tools_toggle_god(void);
void qa_tools_heal(void);
void qa_tools_full_loadout(void);
void qa_tools_teleport(int index);
const char *qa_tools_status(void);
#endif

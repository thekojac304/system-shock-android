#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include "Prefs.h"
#include "ExternalMusic.h"
#define INFO(...) ((void)0)
ShockPrefs gShockPrefs;
int which_lang, curr_vol_lev=75, sfx_on=1, curr_sfx_vol=100, curr_alog_vol=100;
int mode_id=4, DoubleSize, _fr_global_detail=3, audiolog_setting=1;
static char *GetPrefsPathFilename(void) { return "/data/local/tmp/shock-vis1-tests/prefs-test.txt"; }
static void SetShockGlobals(void) { curr_vol_lev=gShockPrefs.soMusicVolume; }

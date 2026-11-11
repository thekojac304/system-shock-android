/* Renderer boundaries doubled; the test compiles the production adapter body. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "horror_visuals.h"
typedef unsigned char uchar;
#include "frflags.h"
typedef struct { int type,floor,ceil,wtex,ftex,ctex; } MapElem;
static MapElem map[4096];
static struct { int x_size,y_size,cyber; } fullmap={64,64,0}, *global_fullmap=&fullmap;
#define MAP_GET_XY(x,y) (&map[(y)*64+(x)])
#define me_tiletype(m) ((m)->type)
#define me_height_flr(m) ((m)->floor)
#define me_height_ceil(m) ((m)->ceil)
#define me_tmap_wall(m) ((m)->wtex)
#define me_tmap_flr(m) ((m)->ftex)
#define me_tmap_ceil(m) ((m)->ctex)
static int loved_textures[256];
static struct {int level; int drug_status[7];} player_struct={1,{0}};
static struct {int doHorrorVisuals,doGamma;} gShockPrefs={0,28};
static unsigned char shading_table[4096],other[4096];
unsigned char ppall[768];
char curr_clut_table;
bool UseCutscenePalette;
static unsigned int _fr_curflags;
static unsigned char *_fr_clut_list[4];
static struct {unsigned char *ltab;} screen,*grd_screen=&screen;
static int filltype,gl,saves;
static intptr_t fillparm;
#define FILL_CLUT 2
#define DRUG_LSD 2
#define DRUG_SIGHT 1
#define gr_set_light_tab(p) (grd_screen->ltab=(p))
#define gr_get_fill_type() filltype
#define gr_get_fill_parm() fillparm
#define gr_set_fill_type(v) (filltype=(v))
#define gr_set_fill_parm(v) (fillparm=(intptr_t)(v))
#define use_opengl() gl
#define SavePrefs() (++saves)
#define SDL_Log(...) ((void)0)

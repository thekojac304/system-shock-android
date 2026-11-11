#include "player.h"
#define GAME_LOOP 0
#define FULLSCREEN_LOOP 1
#define CUTSCENE_LOOP 6
#define SVGA_LOOP 7
#define GL_CHG_LOOP 0x8000
extern short _current_loop;
extern unsigned _change_flag;
extern uchar physics_running, time_passes;

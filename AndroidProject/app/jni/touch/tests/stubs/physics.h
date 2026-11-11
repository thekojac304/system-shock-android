#include "player.h"
#define KEYBD_CONTROL_BANK 1
#define CONTROL_XVEL 0
#define CONTROL_YVEL 1
#define CONTROL_MAX_VAL 100
void physics_get_one_control(int, int, byte *);
void physics_set_one_control(int, int, byte);

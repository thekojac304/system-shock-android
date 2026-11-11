/* Test native byte-counter conservation and QA allocation boundaries. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "otrip.h"
#include "touch_radial.h"
typedef unsigned char ubyte;
typedef unsigned char uchar;
typedef unsigned short ushort;
typedef short ObjID;
typedef short ObjSpecID;
#define TRUE 1
#define FALSE 0
#define OBJ_NULL 0
#define EMPTY_WEAPON_SLOT 255
#define NUM_WEAPON_SLOTS 7
#define NUM_GUN 16
#define NUM_AMMO_TYPES 15
#define NUM_GRENADEZ 7
#define NUM_DRUGZ 7
#define PLAYER_MAX_HP 255
#define MAX_ENERGY 255
#define ACTIVE_WEAPON 0
#define CLASS_GUN 0
#define CLASS_AMMO 1
#define GUN_SUBCLASS_BEAM 4
#define GUN_SUBCLASS_HANDTOHAND 3
#define GUN_SUBCLASS_BEAMPROJ 5
#define DEMO_CONTEXT 0
#define INVENTORY_UPDATE 1
#define VITALS_UPDATE 2
#define MFD_WEAPON_FUNC 1
#define MFD_WEAPON_SLOT 0
#define MFD_ACTIVE 1
#define SFX_RELOAD_1 1
#define SFX_RELOAD_2 2
#define TRIP2CL(t) ((t)>>16)
#define TRIP2SC(t) (((t)>>8)&255)
#define TRIP2TY(t) ((t)&255)
#define MAKETRIP(c,s,t) (((c)<<16)|((s)<<8)|(t))
static int gun_base[]={0,5,7,9,11,14}, ammo_base[]={0,2,4,7,9,11,13};
static int gun_types[]={5,2,2,2,3,2}, ammo_types[]={2,2,3,2,2,2,2};
#define CPTRIP(t) ((TRIP2CL(t)==CLASS_GUN ? gun_base[TRIP2SC(t)] : ammo_base[TRIP2SC(t)])+TRIP2TY(t))
static ubyte num_subclasses[]={6,7};
static int num_types(int c,int s) { return c==CLASS_GUN ? gun_types[s] : ammo_types[s]; }
#define AMMOTYPE_SUBCLASS(x) ((x)>=112 ? 6 : (x)>>4)
#define AMMOTYPE_TYPE(x) ((x)&15)
#define is_energy_weapon(t) ((t)==4)
#define is_handtohand_weapon(t) ((t)==3)
typedef struct { ubyte type,subtype,ammo,ammo_type,make_info; } weapon_slot;
static struct {
    weapon_slot weapons[7];
    ubyte cartridges[15],partial_clip[15],grenades[NUM_GRENADEZ],drugs[7],actives[1],hit_points,energy;
} player_struct;
static struct { bool cyber; } map;
static void *map_ref;
#define global_fullmap ((typeof_map *)map_ref)
typedef struct { bool cyber; } typeof_map;
static struct { ubyte useable_ammo_type; } GunProps[16];
static struct { ubyte cartridge_size; } AmmoProps[15];
static struct { ObjSpecID specID; int triple; } objs[2];
static struct { ubyte ammo_type,ammo_count; } objGuns[2];
static uchar obj_autodelete=TRUE;
static short inv_last_page;
static const char *status;
static bool enabled=true,allocation_fail,admission_fail;
static int creates,destroys,live,notifications,sounds,flags;
static bool qa_tools_enabled(void) { return enabled; }
static void chg_set_flg(int f) { flags|=f; }
static void message_info(const char *s) { (void)s; }
static void SDL_Log(const char *s,...) { (void)s; }
static void mfd_notify_func(int a,int b,int c,int d,int e) { ++notifications; }
static void play_digi_fx(int s,int loops) { ++sounds; }
static void get_object_short_name(int t,char *s,size_t n) { snprintf(s,n,"AMMO %d",TRIP2TY(t)); }
static void unload_current_weapon(void);
static uchar change_ammo_type(ubyte type);
static uchar reload_weapon_hotkey(ushort key,uint32_t context,intptr_t data);
static ObjID obj_create_base(int triple) {
    assert(!obj_autodelete && !live);
    ++creates;
    if (allocation_fail) return OBJ_NULL;
    live=1; objs[1].specID=1; objs[1].triple=triple;
    objGuns[1].ammo_count=0; objGuns[1].ammo_type=TRIP2SC(triple)>=4 ? 30 : 0;
    return 1;
}
static void obj_destroy(ObjID obj) { assert(obj==1 && live); live=0; ++destroys; }
static uchar inventory_add_object(ObjID obj,uchar select) {
    assert(obj==1 && live && !select);
    if (admission_fail) return FALSE;
    for (int s=0;s<7;++s) if (player_struct.weapons[s].type==EMPTY_WEAPON_SLOT) {
        player_struct.weapons[s]=(weapon_slot){TRIP2SC(objs[1].triple),TRIP2TY(objs[1].triple),
                                             objGuns[1].ammo_count,objGuns[1].ammo_type,0};
        obj_destroy(obj);
        return TRUE;
    }
    return FALSE;
}
/* PRODUCTION_ROUTINES */
static void reset(void) {
    memset(&player_struct,0,sizeof(player_struct));
    for (int i=0;i<7;++i) player_struct.weapons[i].type=EMPTY_WEAPON_SLOT;
    player_struct.hit_points=100; player_struct.energy=40;
    map.cyber=false; map_ref=&map;
    enabled=true; allocation_fail=admission_fail=false;
    creates=destroys=live=notifications=sounds=flags=0; obj_autodelete=TRUE;
    for (int i=0;i<15;++i) AmmoProps[i].cartridge_size=10;
    for (int i=0;i<16;++i) GunProps[i].useable_ammo_type=0;
    GunProps[CPTRIP(MAGNUM_TRIPLE)].useable_ammo_type=0x27;
    GunProps[CPTRIP(MAGPULSE_TRIPLE)].useable_ammo_type=0x61;
    GunProps[CPTRIP(RAILGUN_TRIPLE)].useable_ammo_type=0x62;
    GunProps[CPTRIP(FLECHETTE_TRIPLE)].useable_ammo_type=0x43;
    GunProps[CPTRIP(MINIPISTOL_TRIPLE)].useable_ammo_type=0x03;
}
static int rounds(void) {
    int total=player_struct.weapons[0].ammo;
    for (int i=0;i<15;++i) total+=player_struct.cartridges[i]*AmmoProps[i].cartridge_size+player_struct.partial_clip[i];
    return total;
}
static void ammo_test(void) {
    reset();
    player_struct.weapons[0]=(weapon_slot){0,2,6,0,0};
    player_struct.cartridges[4]=2; player_struct.cartridges[5]=3;
    TouchRadialEntry e[7]={{0}};
    assert(touch_ammo_snapshot(e)==3 && !strcmp(e[0].label,"RELOAD"));
    assert(e[1].current && e[1].count==26 && !e[2].current && e[2].count==30);
    int total=rounds();
    assert(touch_ammo_select(TOUCH_AMMO_TYPE_ID(0,2,1)));
    assert(player_struct.weapons[0].ammo_type==1 && player_struct.weapons[0].ammo==10 && rounds()==total);
    assert(sounds==1); /* Exactly one native reload, not duplicate hotkey + select. */
    assert(!touch_ammo_select(TOUCH_AMMO_TYPE_ID(0,2,2)) && rounds()==total);
    assert(!touch_ammo_select(TOUCH_AMMO_TYPE_ID(1,2,1)) && rounds()==total);
    assert(!touch_ammo_select(TOUCH_AMMO_TYPE_ID(0,1,1)) && rounds()==total);
    player_struct.weapons[0].ammo=0;
    total=rounds(); touch_game_radial_short_ammo();
    assert(player_struct.weapons[0].ammo==10 && rounds()==total);
    assert(touch_ammo_select(TOUCH_AMMO_RELOAD_ID(0,2)) && rounds()==total);
    reset(); player_struct.weapons[0]=(weapon_slot){2,0,0,0,0}; player_struct.cartridges[13]=2;
    assert(touch_ammo_snapshot(e)==2);
    total=rounds(); touch_game_radial_short_ammo();
    assert(player_struct.weapons[0].ammo==10 && rounds()==total); /* Single ammo reload. */
    reset(); player_struct.weapons[0]=(weapon_slot){0,2,0,0,0};
    assert(touch_ammo_snapshot(e)==1 && !e[0].available);
    assert(!touch_ammo_select(TOUCH_AMMO_RELOAD_ID(0,2)));
    player_struct.weapons[0].ammo=4;
    assert(touch_ammo_snapshot(e)==2 && e[1].current && e[1].count==4);
    total=rounds(); assert(touch_ammo_select(TOUCH_AMMO_TYPE_ID(0,2,0)) && rounds()==total);
    player_struct.weapons[0].type=4; player_struct.weapons[0].subtype=0;
    assert(touch_ammo_snapshot(e)==0); touch_game_radial_short_ammo();
    player_struct.weapons[0].type=3; assert(touch_ammo_snapshot(e)==0);
    player_struct.weapons[0].type=5; assert(touch_ammo_snapshot(e)==0); /* Native setting 30 isn't an ammo ID. */
    player_struct.weapons[0].type=255; assert(touch_ammo_snapshot(e)==0);
    player_struct.actives[0]=255; assert(touch_ammo_snapshot(e)==0);
    reset(); player_struct.weapons[0]=(weapon_slot){0,2,10,0,0}; player_struct.cartridges[4]=255;
    total=rounds(); assert(touch_ammo_snapshot(e)==0); touch_game_radial_short_ammo(); assert(rounds()==total);
}
static void loadout_test(void) {
    reset();
    player_struct.weapons[0]=(weapon_slot){3,0,0,0,42};
    player_struct.cartridges[0]=250; player_struct.partial_clip[0]=3;
    player_struct.grenades[0]=254; player_struct.drugs[0]=255;
    qa_tools_full_loadout();
    assert(creates==6 && destroys==6 && !live && obj_autodelete);
    assert(player_struct.weapons[0].make_info==42 && player_struct.weapons[0].type==3);
    for (int i=1;i<7;++i) assert(player_struct.weapons[i].type==TRIP2SC(qa_loadout_weapons[i-1]) &&
                               player_struct.weapons[i].subtype==TRIP2TY(qa_loadout_weapons[i-1]));
    assert(player_struct.cartridges[0]==250 && player_struct.partial_clip[0]==3);
    for (int i=1;i<15;++i) assert(player_struct.cartridges[i]==50);
    assert(player_struct.grenades[0]==254 && player_struct.drugs[0]==255);
    assert(player_struct.hit_points==255 && player_struct.energy==255);
    unsigned char saved[sizeof(player_struct)]; memcpy(saved,&player_struct,sizeof(saved));
    qa_tools_full_loadout(); assert(creates==6 && !memcmp(saved,&player_struct,sizeof(saved)));
    reset(); allocation_fail=true; qa_tools_full_loadout();
    assert(creates==16 && !live && destroys==0 && obj_autodelete && strstr(status,"PARTIAL"));
    reset(); admission_fail=true; obj_autodelete=FALSE; qa_tools_full_loadout();
    assert(creates==16 && destroys==16 && !live && !obj_autodelete);
    reset(); enabled=false; qa_tools_full_loadout(); assert(!creates && player_struct.energy==40);
    enabled=true; map.cyber=true; qa_tools_full_loadout(); assert(!creates);
    map.cyber=false; player_struct.hit_points=0; qa_tools_full_loadout(); assert(!creates);
    player_struct.hit_points=100; map_ref=NULL; qa_tools_full_loadout(); assert(!creates);
    reset(); qa_tools_full_loadout();
    assert(creates==7 && destroys==7 && player_struct.weapons[6].type==0 && player_struct.weapons[6].subtype==0);
}
int main(void) {
    ammo_test(); loadout_test();
    puts("TOUCH_GAME_TESTS=PASS (production reload conservation, ammo context/availability/bounds, QA idempotence/capacity/gates/allocation cleanup)");
    return 0;
}

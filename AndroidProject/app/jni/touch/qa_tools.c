/* Android QA only. SPDX-License-Identifier: GPL-3.0-or-later */
#include "qa_tools.h"
#include "qa_spawn.h"
#include "Prefs.h"
#include "player.h"
#include "gamesys.h"
#include "gamewrap.h"
#include "trigger.h"
#include "tools.h"
#include "mainloop.h"
#include "physics.h"
#include "res.h"
#include "objects.h"
#include "objsim.h"
#include "invent.h"
#include "otrip.h"
#include "weapons.h"
#include "mfdint.h"
#include "mfdext.h"
#ifdef RP5NP_AUDIO_DIAGNOSTICS
#include "ExternalMusic.h"
#include "musicai.h"
extern int score_playing;
extern char old_bits;
#endif
#include <SDL.h>
#include <stdlib.h>

static bool god;
extern uchar obj_autodelete;
static const char *status="SELECT A TOOL OR DESTINATION";
bool qa_tools_enabled(void) { return gShockPrefs.qaCheats; }
bool qa_tools_god(void) { return qa_tools_enabled() && god; }
const char *qa_tools_status(void) { return status; }
void qa_tools_reset(void) { god=false; status="SELECT A TOOL OR DESTINATION"; }
void qa_tools_toggle_god(void) {
    if (!qa_tools_enabled()) return;
    god=!god;
    status=god ? "DAMAGE GUARD ENABLED" : "NORMAL DAMAGE RESTORED";
    SDL_Log("QA1 god=%d",god);
}
void qa_tools_heal(void) {
    if (!qa_tools_enabled() || !global_fullmap || global_fullmap->cyber) return;
    player_struct.hit_points=PLAYER_MAX_HP;
    player_struct.energy=MAX_ENERGY;
    chg_set_flg(VITALS_UPDATE);
    status="HEALTH AND ENERGY RESTORED";
    SDL_Log("QA1 heal hp=%d energy=%d",player_struct.hit_points,player_struct.energy);
}

/* QA loadout deliberately uses the native seven-slot pickup path. Existing
   weapons stay intact; missing candidates fill empty slots in this order.
   The first seven cover multi-ammo, single-ammo, beam and projectile weapons. */
static const int qa_loadout_weapons[]={
    MAGNUM_TRIPLE, MAGPULSE_TRIPLE, RAILGUN_TRIPLE, PHASER_TRIPLE,
    FLECHETTE_TRIPLE, PLASMABEAM_TRIPLE, MINIPISTOL_TRIPLE,
    DARTPISTOL_TRIPLE, ASSAULTRFL_TRIPLE, RIOTGUN_TRIPLE, SKORPION_TRIPLE,
    BATON_TRIPLE, LASERAPIER_TRIPLE, BLASTER_TRIPLE, IONBEAM_TRIPLE, STUNGUN_TRIPLE
};
void qa_tools_full_loadout(void) {
    if (!qa_tools_enabled() || !global_fullmap || global_fullmap->cyber ||
        player_struct.hit_points==0) return;
    int added=0,failed=0;
    for (unsigned candidate=0;candidate<sizeof(qa_loadout_weapons)/sizeof(qa_loadout_weapons[0]);++candidate) {
        int triple=qa_loadout_weapons[candidate], empty=0;
        bool owned=false;
        for (int slot=0;slot<NUM_WEAPON_SLOTS;++slot) {
            weapon_slot *ws=&player_struct.weapons[slot];
            if (ws->type==EMPTY_WEAPON_SLOT) ++empty;
            else if (ws->type==TRIP2SC(triple) && ws->subtype==TRIP2TY(triple)) owned=true;
        }
        if (owned) continue;
        if (!empty) break;
        /* Object allocation normally evicts remote objects when exhausted.
           QA grants must never delete world objects to make room. */
        uchar old_autodelete=obj_autodelete;
        obj_autodelete=FALSE;
        ObjID obj=obj_create_base(triple);
        obj_autodelete=old_autodelete;
        if (obj==OBJ_NULL) { ++failed; continue; }
        ObjSpecID spec=objs[obj].specID;
        objGuns[spec].ammo_count=0;
        int type=TRIP2SC(triple);
        if (type!=GUN_SUBCLASS_BEAM && type!=GUN_SUBCLASS_BEAMPROJ) {
            unsigned mask=AMMOTYPE_TYPE(GunProps[CPTRIP(triple)].useable_ammo_type);
            int ammo=0;
            while (ammo<2 && !(mask&(1u<<ammo))) ++ammo;
            objGuns[spec].ammo_type=type==GUN_SUBCLASS_HANDTOHAND ? 0 : ammo;
        }
        if (!inventory_add_object(obj,FALSE)) {
            obj_destroy(obj);
            ++failed;
            continue;
        }
        ++added;
        SDL_Log("QA2 loadout weapon=0x%x granted",triple);
    }
    /* Top up bounded byte counters instead of adding to them. Fifty clips
       leaves space for native unload/reload to return a current magazine. */
    for (int ammo=0;ammo<NUM_AMMO_TYPES;++ammo)
        if (player_struct.cartridges[ammo]<50) player_struct.cartridges[ammo]=50;
    for (int grenade=0;grenade<NUM_GRENADEZ;++grenade)
        if (player_struct.grenades[grenade]<10) player_struct.grenades[grenade]=10;
    for (int drug=0;drug<NUM_DRUGZ;++drug)
        if (player_struct.drugs[drug]<10) player_struct.drugs[drug]=10;
    player_struct.hit_points=PLAYER_MAX_HP;
    player_struct.energy=MAX_ENERGY;
    inv_last_page=-1;
    chg_set_flg(INVENTORY_UPDATE|VITALS_UPDATE);
    mfd_notify_func(MFD_WEAPON_FUNC,MFD_WEAPON_SLOT,FALSE,MFD_ACTIVE,TRUE);
    status=failed ? "PARTIAL LOADOUT - OBJECT LIMIT" : "LOADOUT READY - 7 WEAPON LIMIT";
    message_info(status);
    SDL_Log("QA2 loadout added=%d failed=%d clips>=50 ammo_types=%d grenades>=10 patches>=10 hp=%d energy=%d",
            added,failed,NUM_AMMO_TYPES,player_struct.hit_points,player_struct.energy);
}

static unsigned read32(const uint8_t *p) {
    return (unsigned)p[0]|((unsigned)p[1]<<8)|((unsigned)p[2]<<16)|((unsigned)p[3]<<24);
}
void qa_tools_teleport(int index) {
    static const int levels[]={1,2,0,3,4,5,6,12,13,11,7,8,9};
    if (!qa_tools_enabled() || index<0 || index>=13 || !global_fullmap ||
        global_fullmap->cyber || player_struct.hit_points==0) return;
    int target=levels[index], from=player_struct.level;
    if (target==from) { status="ALREADY ON THIS DECK"; message_info(status); return; }
    /* Beta Grove's entire terrain is biohazardous. Require an explicit guard
       rather than silently changing god mode or dropping an unprotected QA
       player into it. Delta=11, Alpha=12, Beta=13 (level_to_floor in mfdfunc). */
    if (target==13 && !qa_tools_god()) {
        status="BETA GROVE REQUIRES GOD MODE";
        message_info(status);
        return;
    }
    /* Inspect the saved destination before touching the active world. Read raw
       fixed-size resources so mismatched data is rejected, not decoded into an
       undersized buffer. CurrentGame contains both visited and pristine decks. */
    int fd=ResOpenFile(CURRENT_GAME_FNAME);
    uint8_t info[58];
    uint8_t *tiles=malloc(64*64*16);
    int x=0,y=0,z=0;
    Id base=ResIdFromLevel(target);
    bool safe=fd>=0 && tiles && base+3<=resDescMax && ResInUse(base+2) && ResInUse(base+3) &&
        RESDESC(base+2)->fsize==sizeof(info) && RESDESC(base+3)->fsize==64*64*16;
    if (safe) safe=ResExtract(base+2,FORMAT_RAW,info)!=NULL &&
                   ResExtract(base+3,FORMAT_RAW,tiles)!=NULL;
    if (safe) safe=read32(info)==64 && read32(info+4)==64 &&
        read32(info+8)==6 && read32(info+12)==6 && read32(info+16)<=5 &&
        info[24]==0 && qa_spawn_find(tiles,(int)read32(info+16),target==13 && qa_tools_god(),&x,&y,&z);
    free(tiles);
    if (fd>=0) ResCloseFile(fd);
    if (!safe) {
        status="NO SAFE DESTINATION - DECK UNCHANGED";
        message_info("QA: no safe destination; deck unchanged");
        SDL_Log("QA1 teleport rejected from=%d to=%d",from,target);
        return;
    }
    physics_zero_all_controls();
    /* The very same path used by elevators: saves old deck, loads destination,
       restores inventory/art/physics, runs level-entry triggers, then moves via
       obj_move_to. No direct level/global rewrite and no separate music path. */
#ifdef RP5NP_AUDIO_DIAGNOSTICS
    SDL_Log("ELEVTRACE qa-before from=%d to=%d loop=%d panel=%d tile=%d,%d dest=%d,%d,%d score=%d old_bits=%d music=%d external=%d god=%d",
            from,target,_current_loop,player_struct.panel_ref,PLAYER_BIN_X,PLAYER_BIN_Y,
            x,y,z,score_playing,old_bits,music_on,ExternalMusicActive(),qa_tools_god());
#endif
    errtype result=trap_teleport_func(x,y,z,target);
#ifdef RP5NP_AUDIO_DIAGNOSTICS
    SDL_Log("ELEVTRACE qa-after from=%d to=%d actual=%d result=%d loop=%d panel=%d tile=%d,%d score=%d old_bits=%d music=%d external=%d",
            from,target,player_struct.level,result,_current_loop,player_struct.panel_ref,
            PLAYER_BIN_X,PLAYER_BIN_Y,score_playing,old_bits,music_on,ExternalMusicActive());
#endif
    physics_zero_all_controls();
    status=result==OK && player_struct.level==target ? "DECK ENTERED" : "DECK TRANSITION FAILED";
    message_info(status);
    SDL_Log("QA1 teleport from=%d to=%d actual=%d tile=%d,%d z=%d result=%d",
            from,target,player_struct.level,x,y,z,result);
}

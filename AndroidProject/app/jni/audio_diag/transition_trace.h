/* Focused elevator/QA transition diagnostics. No playback or world mutations. */
#ifndef RP5NP_TRANSITION_TRACE_H
#define RP5NP_TRANSITION_TRACE_H
#ifdef RP5NP_AUDIO_DIAGNOSTICS
#include "audio_trace.h"
#include "ExternalMusic.h"
extern int score_playing;
extern char old_bits;
/* Include after player/mapflags/mainloop/musicai/Prefs declarations. */
#define TRANSITION_TRACE(stage, target) \
    AUDIO_TRACE("transition stage=%s level=%d target=%d tile=%d,%d z=%d panel=%d " \
                "ph=%d loop=%d cyber=%d music=%d soundtrack=%d external=%d score=%d bits=%d", \
                stage, player_struct.level, (int)(target), PLAYER_BIN_X, PLAYER_BIN_Y, \
                objs[PLAYER_OBJ].loc.z, player_struct.panel_ref, objs[PLAYER_OBJ].info.ph, \
                _current_loop, global_fullmap ? global_fullmap->cyber : -1, music_on, gShockPrefs.soSoundtrack, \
                ExternalMusicActive(), score_playing, old_bits)
#else
#define TRANSITION_TRACE(stage, target) ((void)0)
#endif
#endif

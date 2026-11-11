#ifndef SHOCK_EXTERNAL_MUSIC_H
#define SHOCK_EXTERNAL_MUSIC_H

/* Main/game thread only. No gameplay state is owned by this module. */
enum { SOUNDTRACK_ORIGINAL, SOUNDTRACK_REMAKE };
int ExternalMusicMode(const char *name);
const char *ExternalMusicModeName(int mode);
const char *ExternalMusicPath(int mode, int level);
int ExternalMusicUpdate(int mode, int level, int gameplay, int volume);
int ExternalMusicActive(void);
void ExternalMusicSuspend(void);
void ExternalMusicVolume(int volume);
void ExternalMusicShutdown(void);

#endif

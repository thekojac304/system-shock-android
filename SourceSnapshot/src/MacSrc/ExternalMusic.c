#include "ExternalMusic.h"
#include <ctype.h>
#include <stddef.h>

/* A pack owns its deck table; playback owns only the selected entry. Additional
 * packs and (later) per-entry loop/intensity metadata belong here, not in the
 * lifecycle controller. Paths are relative to Android's private files/. */
typedef struct {
    int level;
    const char *path;
} ExternalMusicEntry;
typedef struct {
    const char *name;
    const ExternalMusicEntry *entries;
    size_t count;
} ExternalMusicPack;

static const ExternalMusicEntry remake[] = {
    {0, "music/remake/reactor.mp3"},
    {1, "music/remake/medical.mp3"},
    {2, "music/remake/research.mp3"},
    {3, "music/remake/maintenance.mp3"},
    {4, "music/remake/storage.mp3"},
    {5, "music/remake/flightdeck.mp3"},
    {6, "music/remake/executive.mp3"},
    {7, "music/remake/engineering.mp3"},
    {8, "music/remake/security.mp3"},
    {9, "music/remake/bridge.mp3"},
    {11, "music/remake/groves.mp3"}, /* Delta */
    {12, "music/remake/groves.mp3"}, /* Alpha */
    {13, "music/remake/groves.mp3"}  /* Beta */
};
static const ExternalMusicPack packs[] = {
    {"original", NULL, 0},
    {"remake", remake, sizeof(remake) / sizeof(remake[0])}
};

static int valid_mode(int mode) {
    return mode >= 0 && (size_t)mode < sizeof(packs) / sizeof(packs[0]) ? mode : SOUNDTRACK_ORIGINAL;
}
int ExternalMusicMode(const char *name) {
    if (name) for (size_t i = 0; i < sizeof(packs) / sizeof(packs[0]); ++i) {
        const unsigned char *a = (const unsigned char *)name;
        const char *b = packs[i].name;
        while (*a && *b && tolower(*a) == *b) { ++a; ++b; }
        if (!*a && !*b) return (int)i;
    }
    return SOUNDTRACK_ORIGINAL;
}
const char *ExternalMusicModeName(int mode) { return packs[valid_mode(mode)].name; }
const char *ExternalMusicPath(int mode, int level) {
    const ExternalMusicPack *pack = &packs[valid_mode(mode)];
    for (size_t i = 0; i < pack->count; ++i)
        if (pack->entries[i].level == level) return pack->entries[i].path;
    return NULL;
}

#ifdef USE_SDL_MIXER
#include <SDL.h>
#include <SDL_mixer.h>
#include <stdlib.h>

static const char *selected_path;
static int selected_mode = -1, selected_level = -1;
static Mix_Music *external_music;
static int active;
static int failed;
static int cleanup_registered;

struct MusicDevice;
extern struct MusicDevice *MusicDev;
extern void MusicCallback(void *userdata, Uint8 *stream, int len);
extern void StopTheMusic(void);

int ExternalMusicActive(void) { return active; }

void ExternalMusicVolume(int volume) {
    if (active) {
        if (volume < 0) volume = 0;
        if (volume > 100) volume = 100;
        Mix_VolumeMusic(volume * MIX_MAX_VOLUME / 100);
    }
}

void ExternalMusicSuspend(void) {
    if (!active) return;
    Mix_PauseMusic();
    /* Restore the synth hook for menu/death/cutscene music. Keep the decoder
     * position so a same-deck theme reload cannot restart the long track. */
    Mix_HookMusic(MusicCallback, &MusicDev);
    Mix_VolumeMusic(MIX_MAX_VOLUME);
    active = 0;
    SDL_Log("ExternalMusic suspend: %s", selected_path);
}

void ExternalMusicShutdown(void) {
    ExternalMusicSuspend();
    if (external_music) {
        Mix_HaltMusic();
        Mix_FreeMusic(external_music);
        external_music = NULL;
        SDL_Log("ExternalMusic stop: %s", selected_path);
    }
}

static void fallback(const char *reason) {
    SDL_Log("ExternalMusic fallback: level=%d path=%s: %s", selected_level, selected_path, reason);
    ExternalMusicShutdown();
    /* Retry on the next deck visit or process restart, never every frame. */
    failed = 1;
}

int ExternalMusicUpdate(int mode, int level, int gameplay, int volume) {
    mode = valid_mode(mode);
    if (mode != selected_mode || level != selected_level) {
        const char *next_path = ExternalMusicPath(mode, level);
        if (mode != selected_mode)
            SDL_Log("ExternalMusic mode: %s", ExternalMusicModeName(mode));
        if (selected_path && next_path && external_music)
            SDL_Log("ExternalMusic switch: level=%d -> %d, %s -> %s",
                    selected_level, level, selected_path, next_path);
        ExternalMusicShutdown();
        selected_mode = mode;
        selected_level = level;
        selected_path = next_path;
        failed = 0;
        if (mode != SOUNDTRACK_ORIGINAL)
            SDL_Log("ExternalMusic mapping: level=%d -> %s", level, selected_path ? selected_path : "stock");
    }
    if (!selected_path) return 0;
    if (!gameplay) {
        ExternalMusicSuspend();
        return 0;
    }
    if (failed) return 0;
    if (external_music && !Mix_PlayingMusic()) {
        fallback("stream stopped unexpectedly; restoring stock music");
        return 0;
    }
    if (active) {
        ExternalMusicVolume(volume);
        return 1;
    }
    if (!external_music) {
        /* Do not suppress stock music until the file has opened successfully. */
        external_music = Mix_LoadMUS(selected_path);
        if (!external_music) {
            fallback(Mix_GetError()); /* includes missing/unreadable file or decoder error */
            return 0;
        }
        if (Mix_GetMusicType(external_music) != MUS_MP3) {
            fallback("unsupported format: soundtrack packs require an MP3 stream");
            return 0;
        }
        if (!cleanup_registered) {
            atexit(ExternalMusicShutdown);
            cleanup_registered = 1;
        }
        StopTheMusic();
        /* A custom music hook replaces SDL_mixer's stream decoder. Unhook it
         * only for external playback; SFX and the auxiliary postmix remain. */
        Mix_HookMusic(NULL, NULL);
        active = 1;
        ExternalMusicVolume(volume);
        if (Mix_PlayMusic(external_music, -1) < 0) {
            fallback(Mix_GetError());
            return 0;
        }
        SDL_Log("ExternalMusic start: level=%d path=%s (full-track loop)", selected_level, selected_path);
    } else {
        StopTheMusic();
        Mix_HookMusic(NULL, NULL);
        active = 1;
        ExternalMusicVolume(volume);
        Mix_ResumeMusic();
        SDL_Log("ExternalMusic resume: %s", selected_path);
    }
    return 1;
}
#else
int ExternalMusicUpdate(int enabled, int level, int gameplay, int volume) {
    (void)enabled; (void)level; (void)gameplay; (void)volume;
    return 0;
}
int ExternalMusicActive(void) { return 0; }
void ExternalMusicSuspend(void) {}
void ExternalMusicVolume(int volume) { (void)volume; }
void ExternalMusicShutdown(void) {}
#endif

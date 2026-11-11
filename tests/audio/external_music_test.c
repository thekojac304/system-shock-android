/* Exercise the production module with a deterministic mixer boundary.
 * No audio assets or device are needed. SFX APIs intentionally aren't linked. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "ExternalMusic.h"

#ifdef USE_SDL_MIXER
#include <SDL.h>
#include <SDL_mixer.h>
#include <string.h>

struct Mix_Music { int unused; };
static struct Mix_Music track;
struct MusicDevice *MusicDev;
static int loads, plays, pauses, resumes, halts, frees, stops, logs, hooks;
static int load_fail, play_fail, wrong_type, playing, paused, volume = MIX_MAX_VOLUME;
static const char *last_path;
static void (*hook)(void *, Uint8 *, int);

void MusicCallback(void *unused, Uint8 *stream, int len) {
    (void)unused; (void)stream; (void)len;
}
void StopTheMusic(void) { ++stops; ExternalMusicSuspend(); }
Mix_Music *Mix_LoadMUS(const char *path) {
    assert(!playing); /* Never overlap the previous deck's stream. */
    last_path = path;
    ++loads;
    return load_fail ? NULL : &track;
}
Mix_MusicType Mix_GetMusicType(const Mix_Music *music) {
    assert(music == &track);
    return wrong_type ? MUS_WAV : MUS_MP3;
}
int Mix_PlayMusic(Mix_Music *music, int loops) {
    assert(music == &track && loops == -1 && hook == NULL);
    ++plays;
    playing = !play_fail;
    paused = 0;
    return play_fail ? -1 : 0;
}
void Mix_PauseMusic(void) { ++pauses; paused = 1; }
void Mix_ResumeMusic(void) { ++resumes; paused = 0; }
int Mix_PlayingMusic(void) { return playing; }
int Mix_HaltMusic(void) { ++halts; playing = paused = 0; return 0; }
void Mix_FreeMusic(Mix_Music *music) { assert(music == &track); ++frees; }
void Mix_HookMusic(void (*callback)(void *, Uint8 *, int), void *data) {
    assert(callback == NULL || (callback == MusicCallback && data == &MusicDev));
    hook = callback;
    ++hooks;
}
int Mix_VolumeMusic(int value) { int old = volume; volume = value; return old; }
const char *SDL_GetError(void) { return "test open/decode/play failure"; }
void SDL_Log(const char *fmt, ...) { (void)fmt; ++logs; }

static void reset(void) {
    ExternalMusicUpdate(0, 1, 1, 75);
    loads = plays = pauses = resumes = halts = frees = stops = logs = hooks = 0;
    load_fail = play_fail = wrong_type = playing = paused = 0;
    hook = MusicCallback;
    volume = MIX_MAX_VOLUME;
}

int main(void) {
    reset();
    for (int i = 0; i < 100; ++i) assert(!ExternalMusicUpdate(0, 1, 1, 75));
    assert(!loads && !hooks && !stops && !logs); /* default is a no-op */
    assert(!ExternalMusicUpdate(1, 10, 1, 75)); /* unmapped/special save */
    assert(!loads);

    assert(ExternalMusicUpdate(1, 1, 1, 75)); /* Medical new/load game */
    assert(loads == 1 && plays == 1 && stops == 1 && playing && hook == NULL);
    assert(volume == 96);
    for (int i = 0; i < 1000; ++i) assert(ExternalMusicUpdate(1, 1, 1, 75));
    assert(loads == 1 && plays == 1 && resumes == 0);
    /* Game pause keeps music, just like stock. SDL background freezes the
     * device without clearing its music object; next update must be a no-op. */
    assert(ExternalMusicUpdate(1, 1, 1, 75));
    assert(plays == 1 && resumes == 0);
    ExternalMusicVolume(20); /* audiolog ducking / music slider */
    assert(volume == 25);

    for (int i = 0; i < 20; ++i) { /* repeated theme reload / resume */
        ExternalMusicSuspend();
        ExternalMusicSuspend();
        assert(paused && !ExternalMusicActive() && hook == MusicCallback);
        assert(volume == MIX_MAX_VOLUME);
        assert(ExternalMusicUpdate(1, 1, 1, 75));
    }
    assert(plays == 1 && loads == 1 && resumes == 20 && pauses == 20);
    assert(!ExternalMusicUpdate(1, 1, 0, 75)); /* menu/death/cutscene */
    assert(paused && hook == MusicCallback);
    assert(ExternalMusicUpdate(1, 1, 1, 75)); /* resurrection */
    assert(plays == 1);
    assert(!ExternalMusicUpdate(1, 10, 1, 75)); /* special-area load */
    assert(!playing && frees == 1 && hook == MusicCallback && volume == MIX_MAX_VOLUME);
    assert(ExternalMusicUpdate(1, 1, 1, 75)); /* next visit reopens */
    assert(plays == 2 && loads == 2);
    assert(!ExternalMusicUpdate(0, 1, 1, 75)); /* music/config off */
    assert(!playing && hook == MusicCallback);

    reset();
    load_fail = 1;
    for (int i = 0; i < 1000; ++i) assert(!ExternalMusicUpdate(1, 1, 1, 75));
    assert(loads == 1 && plays == 0 && stops == 0 && hooks == 0);
    int logged = logs;
    assert(!ExternalMusicUpdate(1, 1, 1, 75));
    assert(logs == logged); /* failed visit never logs every update */
    load_fail = 0;
    assert(ExternalMusicUpdate(1, 2, 1, 75)); /* failure is Medical-only */
    assert(ExternalMusicUpdate(1, 1, 1, 75)); /* retry after missing file */

    reset();
    wrong_type = 1;
    assert(!ExternalMusicUpdate(1, 1, 1, 75));
    assert(frees == 1 && !stops && hook == MusicCallback);

    reset();
    play_fail = 1;
    assert(!ExternalMusicUpdate(1, 1, 1, 75));
    assert(!ExternalMusicActive() && hook == MusicCallback && volume == MIX_MAX_VOLUME);
    assert(frees == 1);
    assert(!ExternalMusicUpdate(1, 1, 1, 75));
    assert(plays == 1);

    reset();
    assert(ExternalMusicUpdate(1, 1, 1, 75));
    playing = 0; /* decoder failure after opening */
    assert(!ExternalMusicUpdate(1, 1, 1, 75));
    assert(!ExternalMusicActive() && hook == MusicCallback && frees == 1);
    assert(!ExternalMusicUpdate(1, 1, 1, 75));
    assert(plays == 1);
    reset();
    assert(ExternalMusicUpdate(1, 1, 1, 75));
    assert(strcmp(last_path, "music/remake/medical.mp3") == 0);
    assert(ExternalMusicUpdate(1, 2, 1, 75));
    assert(strcmp(last_path, "music/remake/research.mp3") == 0);
    for (int i = 0; i < 1000; ++i) assert(ExternalMusicUpdate(1, 2, 1, 75));
    assert(plays == 2 && frees == 1);
    assert(ExternalMusicUpdate(1, 0, 1, 75));
    assert(strcmp(last_path, "music/remake/reactor.mp3") == 0);
    assert(plays == 3 && frees == 2);
    load_fail = 1;
    assert(!ExternalMusicUpdate(1, 2, 1, 75));
    assert(hook == MusicCallback && !playing);
    load_fail = 0;
    assert(ExternalMusicUpdate(1, 1, 1, 75));
    assert(plays == 4); /* previous good deck works after Research fallback */
    assert(ExternalMusicUpdate(1, 2, 1, 75)); /* fixed file retries next visit */
    assert(plays == 5);
    assert(!ExternalMusicUpdate(1, 2, 0, 75)); /* cyber flag / menu / death */
    assert(hook == MusicCallback && paused);
    assert(ExternalMusicUpdate(1, 2, 1, 20));
    assert(plays == 5 && volume == 25); /* retained decoder + ducking */
    ExternalMusicVolume(-1); assert(volume == 0);
    ExternalMusicVolume(200); assert(volume == MIX_MAX_VOLUME);
    assert(!ExternalMusicUpdate(999, 2, 1, 75)); /* invalid -> stock */
    assert(!playing && hook == MusicCallback);
    reset();
    for (int level = 0; level <= 15; ++level) {
        const char *path = ExternalMusicPath(SOUNDTRACK_REMAKE, level);
        int previous_plays = plays;
        assert(ExternalMusicUpdate(1, level, 1, 75) == (path != NULL));
        if (path) {
            assert(strcmp(path, last_path) == 0);
            assert(plays == previous_plays + 1);
            int previous_logs = logs;
            for (int i = 0; i < 100; ++i) assert(ExternalMusicUpdate(1, level, 1, 75));
            assert(plays == previous_plays + 1 && logs == previous_logs);
        }
    }
    assert(plays == 13);
    reset();
    puts("AUDIO2 pack mapping / switching / fallback / AUDIO1 lifecycle: PASS");
    return 0;
}
#else
int main(void) {
    assert(!ExternalMusicUpdate(1, 1, 1, 75));
    assert(!ExternalMusicActive());
    ExternalMusicVolume(50);
    ExternalMusicSuspend();
    ExternalMusicShutdown();
    puts("AUDIO1 no-mixer portability: PASS");
    return 0;
}
#endif

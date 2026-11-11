/* Optional real SDL_mixer smoke test. Run in a scratch working directory
 * containing generated test tones at music/remake/{medical,research,reactor}.mp3. */
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <SDL_mixer.h>
#include <assert.h>
#include <stdio.h>
#include "ExternalMusic.h"

struct MusicDevice *MusicDev;
static SDL_atomic_t stock_callbacks, postmix_callbacks;
void MusicCallback(void *data, Uint8 *stream, int len) {
    (void)data;
    SDL_memset(stream, 0, (size_t)len);
    SDL_AtomicAdd(&stock_callbacks, 1);
}
void StopTheMusic(void) { ExternalMusicSuspend(); }
static void postmix(void *data, Uint8 *stream, int len) {
    (void)data; (void)stream; (void)len;
    SDL_AtomicAdd(&postmix_callbacks, 1);
}

int main(void) {
    SDL_SetMainReady();
    SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
    assert(SDL_Init(SDL_INIT_AUDIO | SDL_INIT_TIMER) == 0);
    assert((Mix_Init(MIX_INIT_MP3) & MIX_INIT_MP3) != 0);
    assert(Mix_OpenAudio(48000, AUDIO_S16SYS, 2, 1024) == 0);
    Mix_HookMusic(MusicCallback, &MusicDev);
    Mix_SetPostMix(postmix, NULL);
    /* A separate generated PCM chunk stands in for the existing SFX path. */
    Uint8 pcm[4096] = {0};
    Mix_Chunk *sfx = Mix_QuickLoad_RAW(pcm, sizeof(pcm));
    assert(sfx && Mix_PlayChannel(0, sfx, -1) == 0);
    assert(!ExternalMusicUpdate(0, 1, 1, 75));
    SDL_Delay(60);
    assert(SDL_AtomicGet(&stock_callbacks) > 0);
    assert(ExternalMusicUpdate(1, 1, 1, 75));
    /* Fixture is 0.25 seconds; run through several real decoder loop points. */
    for (int i = 0; i < 100; ++i) {
        assert(ExternalMusicUpdate(1, 1, 1, 75));
        SDL_Delay(10);
    }
    assert(Mix_PlayingMusic() && Mix_Playing(0));
    assert(SDL_AtomicGet(&postmix_callbacks) > 10);
    int stock_count = SDL_AtomicGet(&stock_callbacks);
    SDL_Delay(60);
    assert(SDL_AtomicGet(&stock_callbacks) == stock_count);
    ExternalMusicSuspend();
    assert(Mix_PausedMusic());
    SDL_Delay(60);
    assert(SDL_AtomicGet(&stock_callbacks) > stock_count);
    assert(ExternalMusicUpdate(1, 1, 1, 75));
    assert(!Mix_PausedMusic());
    /* SDL device-level pause/resume preserves the stream and SFX channels. */
    Mix_PauseAudio(1);
    SDL_Delay(60);
    Mix_PauseAudio(0);
    assert(ExternalMusicUpdate(1, 1, 1, 75));
    assert(Mix_PlayingMusic() && Mix_Playing(0));
    assert(ExternalMusicUpdate(1, 2, 1, 75));
    assert(ExternalMusicUpdate(1, 0, 1, 75));
    assert(!ExternalMusicUpdate(1, 10, 1, 75));
    assert(!Mix_PlayingMusic() && Mix_Playing(0));
    stock_count = SDL_AtomicGet(&stock_callbacks);
    SDL_Delay(60);
    assert(SDL_AtomicGet(&stock_callbacks) > stock_count);
    /* Missing file must leave the stock callback operating. */
    assert(rename("music/remake/medical.mp3", "music/remake/tone.mp3") == 0);
    assert(!ExternalMusicUpdate(1, 1, 1, 75));
    assert(rename("music/remake/tone.mp3", "music/remake/medical.mp3") == 0);
    ExternalMusicShutdown();
    Mix_HaltChannel(0);
    Mix_FreeChunk(sfx);
    Mix_CloseAudio();
    Mix_Quit();
    SDL_Quit();
    puts("AUDIO2 real MP3 / deck switches / loops / callback / SFX / postmix / device pause: PASS");
    return 0;
}

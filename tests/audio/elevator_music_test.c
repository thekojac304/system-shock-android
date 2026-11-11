/* Exercise the production external-music owner across repeated Maintenance
 * elevator routes. Mixer calls are deterministic boundaries from the pack
 * test, so this detects duplicate streams/silence but does not exercise MIDI
 * threads, device callbacks, the engine or physical elevator interaction. */
#define main existing_audio_regression_main
#include "external_music_test.c"
#undef main

int main(void) {
#ifdef USE_SDL_MIXER
    const int route[] = {3, 2, 1, 2, 3, 0, 3, 4, 3, 5, 3, 6, 3};
    reset();
    for (int cycle = 0; cycle < 20; ++cycle)
        for (size_t i = 0; i < sizeof(route) / sizeof(route[0]); ++i)
            assert(!ExternalMusicUpdate(SOUNDTRACK_ORIGINAL, route[i], 1, 75));
    assert(!loads && !plays && !stops && !ExternalMusicActive());

    reset();
    int transitions = 0;
    for (int cycle = 0; cycle < 20; ++cycle) {
        for (size_t i = 0; i < sizeof(route) / sizeof(route[0]); ++i) {
            int old_plays = plays;
            assert(ExternalMusicUpdate(SOUNDTRACK_REMAKE, route[i], 1, 75));
            if (cycle == 0 || i != 0) ++transitions;
            assert(plays == transitions && ExternalMusicActive() && playing && !paused && hook == NULL);
            assert(strcmp(last_path, ExternalMusicPath(SOUNDTRACK_REMAKE, route[i])) == 0);
            /* Standing on an elevator tile keeps the same deck owner. */
            for (int frame = 0; frame < 30; ++frame)
                assert(ExternalMusicUpdate(SOUNDTRACK_REMAKE, route[i], 1, 75));
            assert(plays == transitions);
            /* Stock elevator theme reload calls StopTheMusic; the next AI
             * update resumes the same decoder rather than opening another. */
            StopTheMusic();
            assert(!ExternalMusicActive() && paused && hook == MusicCallback);
            assert(ExternalMusicUpdate(SOUNDTRACK_REMAKE, route[i], 1, 75));
            assert(plays == transitions && playing && !paused && hook == NULL);
            assert(plays - old_plays <= 1);
        }
    }
    assert(frees == transitions - 1 && loads == transitions && resumes == 260);
    reset();
    puts("ELEVATOR_AUDIO=PASS Original / Remake ownership, 20 Maintenance route cycles, theme resume");
#else
    assert(!ExternalMusicUpdate(SOUNDTRACK_ORIGINAL, 3, 1, 75));
    assert(!ExternalMusicUpdate(SOUNDTRACK_REMAKE, 3, 1, 75));
    assert(!ExternalMusicActive());
    puts("ELEVATOR_AUDIO=PASS no-mixer stock fallback");
#endif
    return 0;
}

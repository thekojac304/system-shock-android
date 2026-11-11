/* TEMPORARY AUDLIFE diagnostics. Compiled only with RP5NP_AUDIO_DIAGNOSTICS. */
#ifndef RP5NP_AUDIO_TRACE_H
#define RP5NP_AUDIO_TRACE_H
#include <SDL.h>
#include <time.h>
static inline unsigned long long audio_trace_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (unsigned long long)ts.tv_sec * 1000000ULL + ts.tv_nsec / 1000;
}
#define AUDIO_TRACE(fmt, ...) SDL_Log("AUDLIFE t_us=%llu " fmt, audio_trace_us(), ##__VA_ARGS__)
extern DECLSPEC void rp5np_audio_diag_start(void);
extern DECLSPEC void rp5np_audio_diag_aux(int bytes);
#endif

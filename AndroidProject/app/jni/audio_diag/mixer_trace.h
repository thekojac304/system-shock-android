/* Included inside the QA-generated SDL_mixer translation unit, after its
 * private declarations. Read-only instrumentation; no mixer state changes.
 * Callback telemetry uses a bounded SPSC ring, never log I/O or allocation. */
#include "audio_trace.h"
#define TRACE_CHANNELS 256
#define TRACE_RECORDS 512
typedef struct {
    unsigned long long accepted, first;
    unsigned sequence;
    int channel;
    const void *chunk;
} AudioTraceRecord;
static AudioTraceRecord trace_pending[TRACE_CHANNELS], trace_records[TRACE_RECORDS];
static unsigned trace_sequence, trace_write, trace_read, trace_dropped;
static unsigned long long trace_last_start, trace_last_end, trace_count, trace_frames;
static unsigned long long trace_max_gap, trace_max_work, trace_max_music;
static int trace_aux_bytes, trace_stop;
static SDL_Thread *trace_thread;
#define TRACE_LOAD(p) __atomic_load_n(&(p), __ATOMIC_RELAXED)
#define TRACE_STORE(p,v) __atomic_store_n(&(p), (v), __ATOMIC_RELAXED)
static void trace_max(unsigned long long *p, unsigned long long value) {
    unsigned long long old = __atomic_load_n(p, __ATOMIC_RELAXED);
    while (value > old && !__atomic_compare_exchange_n(p, &old, value, 0,
            __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {}
}
static void trace_accept(int channel, Mix_Chunk *chunk) {
    /* Caller holds mixer lock. Callback cannot race this marker. */
    if (channel < TRACE_CHANNELS) {
        AudioTraceRecord *r = &trace_pending[channel];
        r->accepted = audio_trace_us();
        r->first = 0;
        r->sequence = ++trace_sequence;
        r->channel = channel;
        r->chunk = chunk;
        AUDIO_TRACE("accept seq=%u channel=%d chunk=%p", r->sequence, channel, (void *)chunk);
    }
}
static void trace_first_mix(int channel) {
    if (channel < TRACE_CHANNELS && trace_pending[channel].sequence) {
        unsigned w = __atomic_load_n(&trace_write, __ATOMIC_RELAXED);
        unsigned r = __atomic_load_n(&trace_read, __ATOMIC_ACQUIRE);
        if (w - r < TRACE_RECORDS) {
            trace_records[w % TRACE_RECORDS] = trace_pending[channel];
            trace_records[w % TRACE_RECORDS].first = audio_trace_us();
            __atomic_store_n(&trace_write, w + 1, __ATOMIC_RELEASE);
        } else {
            __atomic_fetch_add(&trace_dropped, 1, __ATOMIC_RELAXED);
        }
        trace_pending[channel].sequence = 0;
    }
}
static void trace_callback_begin(void) {
    unsigned long long now = audio_trace_us(), previous = TRACE_LOAD(trace_last_start);
    if (previous) trace_max(&trace_max_gap, now - previous);
    TRACE_STORE(trace_last_start, now);
}
static void trace_callback_end(int len) {
    unsigned long long now = audio_trace_us();
    trace_max(&trace_max_work, now - TRACE_LOAD(trace_last_start));
    TRACE_STORE(trace_last_end, now);
    __atomic_fetch_add(&trace_count, 1, __ATOMIC_RELAXED);
    __atomic_fetch_add(&trace_frames, len / (mixer.channels * (SDL_AUDIO_BITSIZE(mixer.format) / 8)), __ATOMIC_RELAXED);
}
static void trace_snapshot(const char *reason) {
    /* SDL_GetAudioDeviceStatus is an atomic state query. No audio locks here:
     * foreground app events are sent BEFORE SDL unlocks/restarts the backend. */
    AUDIO_TRACE("state reason=%s device=%u status=%d callbacks=%llu last_begin_us=%llu last_end_us=%llu aux_bytes=%d playing=%d paused=%d music_paused=%d queue_api=callback_not_applicable",
        reason, audio_device, audio_device ? SDL_GetAudioDeviceStatus(audio_device) : SDL_AUDIO_STOPPED,
        TRACE_LOAD(trace_count), TRACE_LOAD(trace_last_start), TRACE_LOAD(trace_last_end), TRACE_LOAD(trace_aux_bytes),
        Mix_Playing(-1), Mix_Paused(-1), Mix_PausedMusic());
}
static int SDLCALL trace_event(void *unused, SDL_Event *e) {
    const char *name = NULL;
    (void)unused;
    switch (e->type) {
    case SDL_APP_WILLENTERBACKGROUND: name = "APP_WILLENTERBACKGROUND"; break;
    case SDL_APP_DIDENTERBACKGROUND: name = "APP_DIDENTERBACKGROUND"; break;
    case SDL_APP_WILLENTERFOREGROUND: name = "APP_WILLENTERFOREGROUND"; break;
    case SDL_APP_DIDENTERFOREGROUND: name = "APP_DIDENTERFOREGROUND"; break;
    case SDL_APP_TERMINATING: name = "APP_TERMINATING"; break;
    case SDL_AUDIODEVICEADDED: name = "AUDIODEVICEADDED"; break;
    case SDL_AUDIODEVICEREMOVED: name = "AUDIODEVICEREMOVED"; break;
    case SDL_WINDOWEVENT:
        if (e->window.event == SDL_WINDOWEVENT_FOCUS_LOST) name = "FOCUS_LOST";
        if (e->window.event == SDL_WINDOWEVENT_FOCUS_GAINED) name = "FOCUS_GAINED";
        break;
    }
    if (name) trace_snapshot(name);
    return 1;
}
static void trace_drain(void) {
    unsigned r = __atomic_load_n(&trace_read, __ATOMIC_RELAXED);
    unsigned w = __atomic_load_n(&trace_write, __ATOMIC_ACQUIRE);
    while (r != w) {
        AudioTraceRecord v = trace_records[r % TRACE_RECORDS];
        AUDIO_TRACE("first_mix seq=%u channel=%d chunk=%p accepted_us=%llu first_us=%llu wait_us=%llu",
            v.sequence, v.channel, v.chunk, v.accepted, v.first, v.first - v.accepted);
        ++r;
    }
    __atomic_store_n(&trace_read, r, __ATOMIC_RELEASE);
}
static int SDLCALL trace_worker(void *unused) {
    unsigned long long last = audio_trace_us(), frames = 0, count = 0;
    (void)unused;
    while (!TRACE_LOAD(trace_stop)) {
        unsigned long long now = audio_trace_us();
        trace_drain();
        if (now - last >= 5000000) {
            unsigned long long f = TRACE_LOAD(trace_frames), c = TRACE_LOAD(trace_count);
            AUDIO_TRACE("callback_window wall_us=%llu callbacks=%llu frames=%llu max_gap_us=%llu max_work_us=%llu max_music_us=%llu last_begin_us=%llu last_end_us=%llu aux_bytes=%d dropped=%u",
                now - last, c - count, f - frames,
                __atomic_exchange_n(&trace_max_gap, 0, __ATOMIC_RELAXED),
                __atomic_exchange_n(&trace_max_work, 0, __ATOMIC_RELAXED),
                __atomic_exchange_n(&trace_max_music, 0, __ATOMIC_RELAXED),
                TRACE_LOAD(trace_last_start), TRACE_LOAD(trace_last_end), TRACE_LOAD(trace_aux_bytes), TRACE_LOAD(trace_dropped));
            last = now; frames = f; count = c;
        }
        SDL_Delay(100);
    }
    trace_drain();
    return 0;
}
void DECLSPEC rp5np_audio_diag_aux(int bytes) { TRACE_STORE(trace_aux_bytes, bytes); }
void DECLSPEC rp5np_audio_diag_start(void) {
    if (trace_thread) return;
    TRACE_STORE(trace_stop, 0);
    AUDIO_TRACE("config backend=%s freq=%d format=0x%x channels=%d samples=%d bytes=%u nominal_ms=%.3f open_count=%d allocated=%d playing=%d paused=%d music_playing=%d music_paused=%d custom_music=%d block_on_pause=%s pause_audio_hint=%s",
        SDL_GetCurrentAudioDriver(), mixer.freq, mixer.format, mixer.channels, mixer.samples, mixer.size,
        mixer.samples * 1000.0 / mixer.freq, audio_opened, num_channels, Mix_Playing(-1), Mix_Paused(-1),
        Mix_PlayingMusic(), Mix_PausedMusic(), mix_music != music_mixer,
        SDL_GetHint(SDL_HINT_ANDROID_BLOCK_ON_PAUSE),
        SDL_GetHint(SDL_HINT_ANDROID_BLOCK_ON_PAUSE_PAUSEAUDIO));
    SDL_AddEventWatch(trace_event, NULL);
    trace_thread = SDL_CreateThread(trace_worker, "AudioTrace", NULL);
    if (!trace_thread) AUDIO_TRACE("logger_failed error=%s", SDL_GetError());
}
static void trace_shutdown(void) {
    SDL_DelEventWatch(trace_event, NULL);
    TRACE_STORE(trace_stop, 1);
    if (trace_thread) SDL_WaitThread(trace_thread, NULL);
    trace_thread = NULL;
}

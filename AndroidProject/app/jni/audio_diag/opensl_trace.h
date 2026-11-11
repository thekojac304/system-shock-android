/* TEMPORARY read-only OpenSL diagnostics. Queue count is NOT total DAC latency. */
#include "audio_trace.h"
static void trace_opensl(const char *reason, SLresult operation) {
    SLAndroidSimpleBufferQueueState q = {0, 0};
    SLuint32 state = 0;
    SLmillisecond position = 0;
    SLresult qr = SL_RESULT_PRECONDITIONS_VIOLATED;
    SLresult sr = qr, pr = qr;
    if (bqPlayerBufferQueue) qr = (*bqPlayerBufferQueue)->GetState(bqPlayerBufferQueue, &q);
    if (bqPlayerPlay) {
        sr = (*bqPlayerPlay)->GetPlayState(bqPlayerPlay, &state);
        pr = (*bqPlayerPlay)->GetPosition(bqPlayerPlay, &position);
    }
    AUDIO_TRACE("opensl reason=%s op=%u state=%u state_rc=%u queued=%u queue_index=%u queue_rc=%u capacity=%d position_ms=%u position_rc=%u",
        reason, operation, state, sr, q.count, q.index, qr, NUM_BUFFERS, position, pr);
}

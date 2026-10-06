/*
 * pron_progress.h - heavy assessment with progress reporting and cancellation, for background
 * processing queues in the apps. Exported by the same shared library as pron_engine.h.
 *
 * The callback is invoked on the calling (worker) thread, at least every ~0.5 s of work and at
 * every stage change. Do not call engine functions from inside it.
 *
 *   stage     "vad" | "asr" | "phoneme" | "assess" | "done"
 *   fraction  overall progress 0..1 (monotonic; stage weights are calibrated from measured timings)
 *   eta_sec   estimated seconds remaining (< 0 while unknown, e.g. during the first second)
 *
 * The phoneme model runs over speech segments of at most ~15 s cut at VAD pauses (faster and
 * lower memory than one pass over a long recording); whisper reports progress via its own
 * callback and can be aborted.
 *
 * pron_engine_cancel() may be called from ANY thread; the running assess call then returns NULL
 * soon with pron_engine_last_error() == "cancelled". The flag is cleared at the start of each
 * assess call.
 *
 * Threading: one pron_engine handle may be used from several threads at once. A heavy
 * pron_engine_assess_*_progress call on a worker thread can run concurrently with
 * pron_live_start/feed/finish/free, pron_engine_tts and pron_engine_lookup on another thread;
 * pron_engine_cancel, pron_engine_estimate_seconds and pron_engine_status may be called from any
 * thread. Two heavy assess calls on the same handle are serialized (the second waits). Each TTS
 * voice has its own lock (two syntheses with the same voice serialize). pron_engine_last_error()
 * is per calling thread. pron_engine_destroy() must not race with any other call, and every
 * pron_live session must be freed BEFORE the engine is destroyed.
 * The progress callback runs while the assess call holds the heavy-call lock (and no other engine
 * lock): it may call status / estimate_seconds / cancel / lookup / tts (also via another thread it
 * waits for), but NOT another assess on the same engine (rejected with "re-entrant assess call").
 * A cancel() issued before an assess call has started is discarded by that call. Input sample rates
 * outside 4000..384000 Hz are rejected ("invalid sample rate"); NaN/inf samples are treated as 0
 * and values are clamped to [-1, 1].
 *
 * The result JSON is the same as pron_engine_assess_*; every word object carries "recognized"
 * (what was heard in that slot, "" if omitted) so the UI can show "you said X".
 */
#ifndef PRON_PROGRESS_H
#define PRON_PROGRESS_H

#include <stddef.h>
#include <stdint.h>

#include "pron/pron_engine.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*pron_progress_fn)(void* user, const char* stage, double fraction, double eta_sec);

PRON_API char* pron_engine_assess_pcm16_progress(pron_engine* e, const int16_t* samples, size_t count,
                                                 int sample_rate, const char* reference_utf8,
                                                 pron_progress_fn cb, void* user);
PRON_API char* pron_engine_assess_f32_progress(pron_engine* e, const float* samples, size_t count,
                                               int sample_rate, const char* reference_utf8,
                                               pron_progress_fn cb, void* user);
PRON_API void pron_engine_cancel(pron_engine* e);

/* Rough processing-time estimate in seconds for a recording of the given length on this device,
 * based on timings measured by previous assess calls in this process (a built-in default before
 * the first call). Lets the UI show "≈ 0:45" for queued items. */
PRON_API double pron_engine_estimate_seconds(pron_engine* e, double audio_seconds);

#ifdef __cplusplus
}
#endif

#endif /* PRON_PROGRESS_H */

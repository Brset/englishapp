/*
 * pron_engine.h - one-call C API for the apps: audio in -> assessment JSON out, text in -> speech out.
 *
 * Built as ONE shared library (pron_engine.dll / libpron_engine.so) that also exports the whole
 * pron_c.h API, so each app loads a single native library.
 *
 * Models directory layout (created by tools/fetch_models.py, bundled with the apps):
 *   <models>/whisper/ggml-*.bin (one file, e.g. ggml-base.en-q5_1.bin)
 *   <models>/vad/silero_vad.onnx
 *   <models>/phoneme/model.onnx + <models>/phoneme/vocab.json   (wav2vec2 CTC, espeak IPA labels)
 *   <models>/cmudict/cmudict.dict
 *   <models>/tts/us/{model.onnx,tokens.txt,espeak-ng-data/}   (sherpa-onnx Piper voice en_US lessac)
 *   <models>/tts/gb/{model.onnx,tokens.txt,espeak-ng-data/}   (sherpa-onnx Piper voice en_GB alan)
 * Any component may be missing: the engine still works with reduced output (see pron_engine_status).
 *
 * Conventions as in pron_c.h: UTF-8 strings, returned char* freed with pron_free_string(),
 * NULL on error + pron_engine_last_error(). One engine handle must not be used from several
 * threads at once.
 */
#ifndef PRON_ENGINE_H
#define PRON_ENGINE_H

#include <stddef.h>
#include <stdint.h>

#include "pron/pron_c.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct pron_engine pron_engine;

/* Loads every component found under models_dir. n_threads <= 0 = auto. Never returns NULL
 * for a missing model; returns NULL only if models_dir itself is unusable. */
PRON_API pron_engine* pron_engine_create(const char* models_dir_utf8, int n_threads);
PRON_API void pron_engine_destroy(pron_engine* e);
PRON_API const char* pron_engine_last_error(const pron_engine* e);

/* JSON: {"version":"..","asr":true,"vad":true,"phoneme":false,"cmudict":true,
 *        "tts":{"us":true,"gb":false},"errors":{"phoneme":"file not found: ..."}} */
PRON_API char* pron_engine_status(pron_engine* e);

PRON_API void pron_engine_set_strictness(pron_engine* e, int strictness); /* PRON_STRICTNESS_* */

/* Accent of the reference pronunciation: "us", "gb" or "any" (default). Returns 0, -1 if unknown.
 * Status JSON also carries "phoneme_vocab":{"size":N,"mapped":M,"unmapped":[first 40 labels]};
 * the assessment JSON carries "phoneme_debug":{"frames":T,"classes":C,"used":bool,"reason":".."}. */
PRON_API int pron_engine_set_accent(pron_engine* e, const char* accent);

/* Full pipeline on one recording: resample to 16 kHz -> VAD trim -> whisper words+timestamps ->
 * wav2vec2 posteriors -> pron_assess. Returns the pron_assess JSON with extra top-level fields
 * "recognized_text" (string), "speech_segments" ([[start,end],...]) and "timings_ms"
 * ({"vad":..,"asr":..,"phoneme":..,"assess":..}). */
PRON_API char* pron_engine_assess_pcm16(pron_engine* e, const int16_t* samples, size_t count,
                                        int sample_rate, const char* reference_utf8);
PRON_API char* pron_engine_assess_f32(pron_engine* e, const float* samples, size_t count,
                                      int sample_rate, const char* reference_utf8);

/* Speech synthesis. voice: "us" or "gb". speed 0.5..1.5 (1 = normal).
 * Returns mono float PCM in [-1,1] (free with pron_engine_free_audio), sets *out_count and
 * *out_sample_rate; NULL on error. */
PRON_API float* pron_engine_tts(pron_engine* e, const char* text_utf8, const char* voice, float speed,
                                size_t* out_count, int* out_sample_rate);
PRON_API void pron_engine_free_audio(float* samples);

/* Word lookup / tokenization through the engine's loaded dictionary (same JSON as pron_c.h). */
PRON_API char* pron_engine_lookup(pron_engine* e, const char* word_utf8);

#ifdef __cplusplus
}
#endif

#endif /* PRON_ENGINE_H */

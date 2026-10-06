/*
 * pron_live.h - live reading tracker: highlights words as the user reads aloud and tells the UI
 * where to scroll. Exported by the same shared library as pron_engine.h.
 *
 * Uses a small streaming recognizer (sherpa-onnx streaming Zipformer, English) from
 *   <models>/live/{encoder.onnx,decoder.onnx,joiner.onnx,tokens.txt}
 * and matches the partial transcript against the reference text. It is only for live feedback:
 * the detailed scoring after the reading still goes through pron_engine_assess_*.
 *
 * Typical use (all calls on one worker thread, ~every 100-200 ms of audio):
 *   s = pron_live_start(engine, text);
 *   loop: json = pron_live_feed_pcm16(s, chunk, n, 16000); update UI; pron_free_string(json);
 *   json = pron_live_finish(s);   // final state (flushes the recognizer)
 *   pron_live_free(s);
 *
 * State JSON returned by feed/finish:
 * {
 *   "cursor": 12,               // index of the next word expected (== number of words when done)
 *   "scroll_to": 12,            // word index the UI should keep visible (usually == cursor)
 *   "done": false,              // true when the last word has been read
 *   "partial": "the three brot",// current raw recognizer hypothesis (for debugging/UI hint)
 *   "words": [                  // one entry per reference word, same indexing as pron_tokenize()
 *     {"i":0,"state":"read"},   // read     - heard (or a short function word between heard words)
 *     {"i":1,"state":"skipped"},// skipped  - cursor moved past it but it was not heard
 *     {"i":2,"state":"current"},// current  - the word at the cursor
 *     {"i":3,"state":"pending"} // pending  - not reached yet
 *   ],
 *   "changed_from": 9           // smallest word index whose state changed since the previous call
 * }
 * Word objects carry the same "u16_begin"/"u16_end" offsets as pron_tokenize() so the UI can
 * colour spans directly.
 */
#ifndef PRON_LIVE_H
#define PRON_LIVE_H

#include <stddef.h>
#include <stdint.h>

#include "pron/pron_engine.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct pron_live pron_live;

/* NULL if the live model is missing (see pron_engine_status: "live": false) or on error. */
PRON_API pron_live* pron_live_start(pron_engine* e, const char* reference_utf8);
PRON_API char* pron_live_feed_pcm16(pron_live* s, const int16_t* samples, size_t count, int sample_rate);
PRON_API char* pron_live_feed_f32(pron_live* s, const float* samples, size_t count, int sample_rate);
PRON_API char* pron_live_finish(pron_live* s);
/* Jump the cursor manually (user tapped a word to restart reading from there). */
PRON_API void pron_live_set_cursor(pron_live* s, int word_index);
PRON_API void pron_live_free(pron_live* s);

#ifdef __cplusplus
}
#endif

#endif /* PRON_LIVE_H */

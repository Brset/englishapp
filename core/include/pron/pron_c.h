/*
 * pron_c.h - plain C API of pron_core for Windows (C# P/Invoke, C++) and Android (JNI).
 *
 * Conventions:
 *  - All strings are UTF-8, NUL-terminated.
 *  - Functions returning char* allocate the string; release it with pron_free_string().
 *    NULL is returned on error; pron_last_error() then describes the problem.
 *  - Handles are opaque. One handle must not be used from several threads at the same
 *    time; different handles are independent.
 *  - Times are in seconds from the start of the recording.
 */
#ifndef PRON_C_H
#define PRON_C_H

#include <stddef.h>

#if defined(PRON_SHARED)
#  if defined(_WIN32)
#    if defined(PRON_BUILDING)
#      define PRON_API __declspec(dllexport)
#    else
#      define PRON_API __declspec(dllimport)
#    endif
#  else
#    define PRON_API __attribute__((visibility("default")))
#  endif
#else
#  define PRON_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct pron_assessor pron_assessor;

/* One recognized word from the ASR (e.g. whisper.cpp). */
typedef struct pron_word {
    const char* text;    /* UTF-8, punctuation allowed */
    double start;        /* seconds */
    double end;          /* seconds */
    float probability;   /* 0..1, use 1 if unknown */
} pron_word;

/* Strictness presets for phoneme scoring. */
enum {
    PRON_STRICTNESS_LENIENT = 0,
    PRON_STRICTNESS_NORMAL = 1,
    PRON_STRICTNESS_STRICT = 2
};

/* Library version string, e.g. "0.1.0". Static storage, do not free. */
PRON_API const char* pron_version(void);

PRON_API pron_assessor* pron_assessor_create(void);
PRON_API void pron_assessor_destroy(pron_assessor* a);

/* Last error message for this handle ("" if none). Valid until the next call on the handle. */
PRON_API const char* pron_last_error(const pron_assessor* a);

/* Load CMUdict (cmudict-0.7b or cmudict.dict format). Entries are merged into what is
 * already loaded. Return the number of pronunciations loaded, or -1 on error. */
PRON_API int pron_assessor_load_cmudict_file(pron_assessor* a, const char* path_utf8);
PRON_API int pron_assessor_load_cmudict_text(pron_assessor* a, const char* text, size_t length);

/* Add a single pronunciation, e.g. ("covid", "K OW1 V IH0 D"). Returns 0 on success. */
PRON_API int pron_assessor_add_pronunciation(pron_assessor* a, const char* word, const char* arpabet);

/* Output labels of the phoneme model (IPA strings in vocab order) and its CTC blank index.
 * Returns the number of labels that could not be mapped to the phoneme inventory
 * (special tokens like <pad>/<unk> are expected to be unmapped), or -1 on error. */
PRON_API int pron_assessor_set_phoneme_vocab(pron_assessor* a, const char* const* labels, int count,
                                             int blank_index);

PRON_API void pron_assessor_set_strictness(pron_assessor* a, int strictness);
PRON_API void pron_assessor_set_long_pause(pron_assessor* a, double seconds);

/* Assess one reading.
 *  reference       - reference text (UTF-8)
 *  words, n_words  - ASR words with timestamps (may be NULL / 0)
 *  log_posteriors  - optional row-major [n_frames x n_classes] log-softmax output of the
 *                    phoneme model for the whole recording (NULL to skip phoneme scoring);
 *                    n_classes must equal the vocab size
 *  frame_seconds   - frame hop of the posteriors (0.02 for wav2vec2)
 * Returns a JSON document (see core/README.md) or NULL on error. */
PRON_API char* pron_assess(pron_assessor* a, const char* reference, const pron_word* words, int n_words,
                           const float* log_posteriors, int n_frames, int n_classes, double frame_seconds);

/* Tokenize a text: JSON array of words with byte and UTF-16 offsets (for highlighting). */
PRON_API char* pron_tokenize(const char* text);

/* Expected pronunciation of a word as JSON:
 * {"word":..,"source":"cmudict|g2p|none","arpabet":"TH IH1 NG K","ipa":"ˈθɪŋk","stress_syllable":0} */
PRON_API char* pron_assessor_lookup(pron_assessor* a, const char* word);

PRON_API void pron_free_string(char* s);

#ifdef __cplusplus
}
#endif

#endif /* PRON_C_H */

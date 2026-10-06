// Fluency metrics from recognized word timestamps.
#pragma once

#include <string>
#include <vector>

namespace pron {

struct TimedWord {
    std::string text;  // normalized word
    double start = 0.0;
    double end = 0.0;
};

struct FluencyOptions {
    double long_pause_seconds = 0.5;   // gap between words counted as a long pause
    // Pauses at sentence boundaries (after a reference word followed by . ! ? ;) get this
    // extra allowance before being counted as long.
    double sentence_pause_bonus = 0.5;
    double target_wpm_min = 90.0;      // comfortable reading speed for learners
    double target_wpm_max = 170.0;
    double penalty_per_long_pause = 6.0;     // points per event (per 20 words; longer texts are normalized)
    double penalty_per_repetition = 5.0;
    double penalty_per_hesitation = 4.0;
    std::vector<std::string> fillers = {"uh", "um", "uhm", "er", "erm", "ah", "eh", "hmm", "mm", "mhm", "ehm"};
};

struct Pause {
    int after_word = -1;  // index into the word list (-1 = before the first word)
    double start = 0.0;
    double end = 0.0;
    double duration() const { return end - start; }
};

struct FluencyResult {
    int word_count = 0;          // spoken words excluding fillers / repetitions
    double speech_seconds = 0.0; // first word start .. last word end
    double articulation_seconds = 0.0;  // speech_seconds minus long pauses
    double words_per_minute = 0.0;      // word_count / speech_seconds
    double articulation_wpm = 0.0;      // word_count / articulation_seconds
    std::vector<Pause> long_pauses;
    std::vector<int> repetitions;   // indices of words that repeat the previous word(s)
    std::vector<int> hesitations;   // indices of filler words
    double score = 0.0;             // 0..100
};

bool is_filler(const std::string& word, const FluencyOptions& opt);

// `sentence_end_after[i]` (optional, same size as words) marks words after which a sentence
// ends, so longer pauses there are not penalized.
FluencyResult compute_fluency(const std::vector<TimedWord>& words, const FluencyOptions& opt = {},
                              const std::vector<bool>& sentence_end_after = {});

}  // namespace pron

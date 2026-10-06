#include "pron/fluency.h"

#include <algorithm>

namespace pron {

bool is_filler(const std::string& word, const FluencyOptions& opt) {
    return std::find(opt.fillers.begin(), opt.fillers.end(), word) != opt.fillers.end();
}

FluencyResult compute_fluency(const std::vector<TimedWord>& words, const FluencyOptions& opt,
                              const std::vector<bool>& sentence_end_after) {
    FluencyResult r;
    const int n = static_cast<int>(words.size());
    if (n == 0) return r;

    // Hesitations (fillers) and repetitions. A repetition is a word equal to the previous
    // non-filler word ("the the"), or a repeated bigram ("in the in the").
    std::vector<bool> excluded(n, false);
    int prev = -1, prev2 = -1;  // last two non-filler, non-excluded word indices
    for (int i = 0; i < n; ++i) {
        if (is_filler(words[i].text, opt)) {
            r.hesitations.push_back(i);
            excluded[i] = true;
            continue;
        }
        if (prev >= 0 && words[i].text == words[prev].text) {
            r.repetitions.push_back(i);
            excluded[i] = true;
            continue;
        }
        // Bigram repetition: words i, i+1 equal prev2, prev (with fillers skipped).
        if (prev2 >= 0 && words[i].text == words[prev2].text) {
            int next = i + 1;
            while (next < n && is_filler(words[next].text, opt)) ++next;
            if (next < n && words[next].text == words[prev].text) {
                r.repetitions.push_back(i);
                r.repetitions.push_back(next);
                excluded[i] = excluded[next] = true;
                for (int k = i + 1; k < next; ++k) {
                    if (!excluded[k]) {
                        r.hesitations.push_back(k);
                        excluded[k] = true;
                    }
                }
                i = next;
                continue;
            }
        }
        prev2 = prev;
        prev = i;
    }
    std::sort(r.hesitations.begin(), r.hesitations.end());

    for (int i = 0; i < n; ++i)
        if (!excluded[i]) ++r.word_count;

    r.speech_seconds = std::max(0.0, words.back().end - words.front().start);
    double long_total = 0.0;
    for (int i = 0; i + 1 < n; ++i) {
        double gap = words[i + 1].start - words[i].end;
        double limit = opt.long_pause_seconds;
        if (i < static_cast<int>(sentence_end_after.size()) && sentence_end_after[i]) limit += opt.sentence_pause_bonus;
        if (gap > limit) {
            r.long_pauses.push_back(Pause{i, words[i].end, words[i + 1].start});
            long_total += gap;
        }
    }
    r.articulation_seconds = std::max(0.0, r.speech_seconds - long_total);
    if (r.speech_seconds > 0) r.words_per_minute = r.word_count * 60.0 / r.speech_seconds;
    if (r.articulation_seconds > 0) r.articulation_wpm = r.word_count * 60.0 / r.articulation_seconds;

    // Score: start at 100, penalize speed outside target range and disfluencies
    // (normalized per 20 words so long texts are not punished for their length).
    double score = 100.0;
    double wpm = r.words_per_minute;
    if (r.word_count >= 2 && r.speech_seconds > 0) {
        if (wpm < opt.target_wpm_min) score -= 60.0 * (opt.target_wpm_min - wpm) / opt.target_wpm_min;
        if (wpm > opt.target_wpm_max) score -= 40.0 * (wpm - opt.target_wpm_max) / opt.target_wpm_max;
    }
    double scale = 20.0 / std::max(20, n);
    score -= scale * (opt.penalty_per_long_pause * r.long_pauses.size() +
                      opt.penalty_per_repetition * (r.repetitions.size()) +
                      opt.penalty_per_hesitation * r.hesitations.size());
    r.score = std::max(0.0, std::min(100.0, score));
    return r;
}

}  // namespace pron

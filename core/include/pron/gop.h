// Goodness of Pronunciation (GOP) scoring on inventory-space log-posteriors.
#pragma once

#include <vector>

#include "pron/ctc_align.h"
#include "pron/posteriors.h"

namespace pron {

enum class Strictness { Lenient, Normal, Strict };

struct GopOptions {
    // GOP (in nats) mapped linearly to 0..100: gop >= good_gop -> 100, gop <= bad_gop -> 0.
    double good_gop = 0.5;
    double bad_gop = -4.0;
    // A phone is reported as substituted when its score is below this and the most likely
    // competitor is more probable than the expected phone on average.
    double substitution_score = 60.0;

    static GopOptions preset(Strictness s);
};

struct PhonemeScore {
    int phoneme_id = -1;          // expected phoneme (inventory id)
    double gop = 0.0;             // mean over frames of log p(expected) - max_{q != expected} log p(q)
    double score = 0.0;           // 0..100
    double mean_log_prob = 0.0;   // mean log p(expected) over the frames
    int likely_id = -1;           // most probable competing phoneme (by mean log-prob)
    double likely_margin = 0.0;   // mean log p(likely) - mean log p(expected); > 0 means likely won
    bool substituted = false;
    int start_frame = 0, end_frame = 0;
};

// Linear mapping of GOP to 0..100 using the options' thresholds.
double gop_to_score(double gop, const GopOptions& opt);

// Score one phoneme over frames [start, end) of an inventory-space matrix (column 0 = blank,
// excluded from competitors).
PhonemeScore score_phoneme(const LogPosteriors& inv, int phoneme_id, int start, int end,
                           const GopOptions& opt);

// Like score_phoneme, but the expected phone is column `target_col` of `m` (which may be an extra
// merged column beyond the first `inventory_cols` columns, e.g. log-sum-exp of the allowed
// variants) and the columns in `equivalent` (inventory columns) are never counted as competitors.
// `expected_id` is stored in the result.
PhonemeScore score_column(const LogPosteriors& m, int inventory_cols, int target_col, int expected_id,
                          const std::vector<int>& equivalent, int start, int end, const GopOptions& opt);

// Score every span of a forced alignment computed on the same inventory-space matrix.
std::vector<PhonemeScore> score_alignment(const LogPosteriors& inv, const CtcAlignment& al,
                                          const GopOptions& opt);

}  // namespace pron

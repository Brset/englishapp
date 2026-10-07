// CTC forced alignment (Viterbi) of an expected label sequence to frame log-posteriors.
#pragma once

#include <functional>
#include <vector>

#include "pron/posteriors.h"

namespace pron {

struct PhoneSpan {
    int index = 0;        // position in the target sequence
    int column = 0;       // target column in the posterior matrix
    int start_frame = 0;  // first frame where the best path emits this label
    int end_frame = 0;    // one past the last emitting frame; end_frame > start_frame
    // Contiguous region for timing: from start_frame up to the next label's start_frame
    // (trailing blanks are attributed to the preceding phone). For the last phone the
    // region ends at the last frame of the path.
    int region_end = 0;
};

struct CtcAlignment {
    bool ok = false;           // false if the sequence cannot fit into the frames, or empty input
    double log_prob = 0.0;     // log-probability of the best path
    std::vector<PhoneSpan> spans;
};

// Minimum number of frames needed to emit `targets` with CTC (repeats need a blank between).
int ctc_min_frames(const std::vector<int>& targets);

// Viterbi forced alignment with the standard CTC topology (blank, l1, blank, l2, ..., blank).
// `targets` are column indices into `lp` and must not contain `blank`.
CtcAlignment ctc_force_align(const LogPosteriors& lp, const std::vector<int>& targets, int blank);

// Banded variant: target i may only be emitted in frames [lo[i], hi[i]) (windows around rough ASR
// word times), so memory and time scale with the band and a whole long recording can be aligned at
// once. Log-probabilities come from emit(t, i) for target i, emit(t, -1) for blank. `targets` are
// ids used only to detect repeats (equal neighbours need a blank between them). Spans' `column`
// holds the id.
using CtcEmitFn = std::function<double(int t, int i)>;
CtcAlignment ctc_force_align_banded(int frames, const std::vector<int>& targets, const CtcEmitFn& emit,
                                    const std::vector<int>& lo, const std::vector<int>& hi);

}  // namespace pron

// Splitting of the speech region into phoneme-model segments (<= ~15 s, cut at pauses) and
// stitching the per-segment posteriors back into one timeline aligned to the original audio.
#pragma once

#include <cstddef>
#include <vector>

#include "pron/backends.h"
#include "pron/posteriors.h"

namespace pron_internal {

struct PhonemeSegment {
    size_t pad_begin = 0, pad_end = 0;    // samples fed to the model (core +- padding, clamped to the speech region)
    size_t core_begin = 0, core_end = 0;  // samples whose frames are kept in the stitched result
};

// audio: 16 kHz mono; [begin,end) = speech region (leading/trailing silence trimmed);
// vad: speech segments in seconds (may be empty: the whole region is then one candidate).
// Segments are at most max_sec long including padding; long speech is cut at the lowest-energy
// point near the limit, short neighbouring VAD segments are merged. pad_sec of context (clamped
// to the speech region) surrounds every segment.
std::vector<PhonemeSegment> plan_phoneme_segments(const float* audio, size_t n, size_t begin, size_t end,
                                                  const std::vector<pron::SpeechSegment>& vad,
                                                  double max_sec = 15.0, double pad_sec = 0.3);

// Collects per-segment posteriors into one matrix on the original audio's frame timeline;
// frames not covered by any segment are blank-dominant.
class PosteriorStitcher {
public:
    PosteriorStitcher(int classes, int blank_index) : classes_(classes), blank_(blank_index) {}
    // lp: model output for samples [seg.pad_begin, seg.pad_end). is_last: keep frames up to the end.
    void add(const PhonemeSegment& seg, const pron::LogPosteriors& lp, bool is_last);
    pron::LogPosteriors finish();
    bool empty() const { return frames_ == 0; }
private:
    void grow(int frames);
    int classes_, blank_;
    int frames_ = 0;
    double frame_seconds_ = 0.02;
    std::vector<float> data_;
};

}  // namespace pron_internal

#include "phoneme_segments.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace pron_internal {

namespace {
constexpr int kSr = 16000;
constexpr size_t kHop = 320;  // 20 ms
}

std::vector<PhonemeSegment> plan_phoneme_segments(const float* audio, size_t n, size_t begin, size_t end,
                                                  const std::vector<pron::SpeechSegment>& vad,
                                                  double max_sec, double pad_sec) {
    std::vector<PhonemeSegment> out;
    end = std::min(end, n);
    if (begin >= end) return out;
    const size_t max_core = size_t(std::max(1.0, (max_sec - 2 * pad_sec)) * kSr);
    const size_t pad = size_t(pad_sec * kSr);

    // 1. VAD segments -> sample ranges inside [begin,end)
    std::vector<std::pair<size_t, size_t>> r;
    for (const auto& s : vad) {
        size_t a = std::max(begin, size_t(std::max(0.0, s.start) * kSr));
        size_t b = std::min(end, size_t(std::ceil(std::max(0.0, s.end) * kSr)));
        if (b > a) r.push_back({a, b});
    }
    if (r.empty()) r.push_back({begin, end});

    // 2. split pieces longer than max_core at the lowest-energy 20 ms frame in the last 4 s
    std::vector<std::pair<size_t, size_t>> pieces;
    for (auto [a, b] : r) {
        while (b - a > max_core) {
            const size_t lo = a + (max_core > size_t(4 * kSr) ? max_core - 4 * kSr : max_core / 2);
            const size_t hi = a + max_core;
            size_t best = hi;
            double best_e = std::numeric_limits<double>::infinity();
            for (size_t p = lo; p + kHop <= hi; p += kHop) {
                double e = 0;
                for (size_t i = p; i < p + kHop; ++i) e += double(audio[i]) * audio[i];
                if (e <= best_e) { best_e = e; best = p + kHop / 2; }
            }
            pieces.push_back({a, best});
            a = best;
        }
        pieces.push_back({a, b});
    }

    // 3. greedy merge of neighbours while the core stays <= max_core
    std::vector<std::pair<size_t, size_t>> groups;
    for (auto p : pieces) {
        if (!groups.empty() && p.second - groups.back().first <= max_core) groups.back().second = p.second;
        else groups.push_back(p);
    }

    // 4. cores extended into pauses (up to pad / half the gap), model input = core +- pad
    for (size_t i = 0; i < groups.size(); ++i) {
        const size_t s = groups[i].first, e = groups[i].second;
        const size_t prev_end = i ? groups[i - 1].second : begin;
        const size_t next_beg = i + 1 < groups.size() ? groups[i + 1].first : end;
        PhonemeSegment g;
        g.core_begin = s - std::min(pad, (s - prev_end) / 2);
        g.core_end = e + std::min(pad, (next_beg - e) / 2);
        g.pad_begin = s > begin + pad ? s - pad : begin;
        g.pad_end = std::min(end, e + pad);
        if (g.pad_begin > begin) g.pad_begin -= g.pad_begin % kHop;  // keep the frame grid of the original audio
        out.push_back(g);
    }
    out.front().core_begin = begin;  // nothing before the first segment but trimmed silence
    return out;
}

void PosteriorStitcher::grow(int frames) {
    if (frames <= frames_) return;
    data_.resize(size_t(frames) * classes_);
    for (int t = frames_; t < frames; ++t)
        for (int c = 0; c < classes_; ++c) data_[size_t(t) * classes_ + c] = (c == blank_) ? 0.0f : -30.0f;
    frames_ = frames;
}

void PosteriorStitcher::add(const PhonemeSegment& seg, const pron::LogPosteriors& lp, bool is_last) {
    if (!lp.valid() || lp.classes != classes_ || lp.frames <= 0) return;
    frame_seconds_ = lp.frame_seconds;
    const double fs = lp.frame_seconds;
    const long first = std::lround(double(seg.pad_begin) / kSr / fs);
    const long lo = std::lround(double(seg.core_begin) / kSr / fs);
    const long hi = is_last ? std::numeric_limits<long>::max() : std::lround(double(seg.core_end) / kSr / fs);
    // Skip frames below the core only when the segment is not the first one (padding context).
    const long k0 = std::max(0L, lo - first);
    const long k1 = std::min<long>(lp.frames, hi - first);
    if (k1 <= k0) return;
    grow(int(first + k1));
    for (long k = k0; k < k1; ++k)
        std::copy(lp.data.begin() + size_t(k) * classes_, lp.data.begin() + size_t(k + 1) * classes_,
                  data_.begin() + size_t(first + k) * classes_);
}

pron::LogPosteriors PosteriorStitcher::finish() {
    pron::LogPosteriors o(0, classes_, frame_seconds_);
    o.frames = frames_;
    o.data = std::move(data_);
    return o;
}

}  // namespace pron_internal

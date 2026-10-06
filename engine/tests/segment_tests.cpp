// Unit tests of phoneme segment planning / posterior stitching with a deterministic mock model.
#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

#include "phoneme_segments.h"

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); ++g_fail; } } while (0)

// Frame t depends only on samples [320t, 320t+400): like wav2vec2's receptive field, no longer context.
static pron::LogPosteriors mock_compute(const float* x, size_t n) {
    if (n < 400) return pron::LogPosteriors(0, 3);
    const int T = int((n - 400) / 320 + 1);
    pron::LogPosteriors lp(T, 3);
    for (int t = 0; t < T; ++t) {
        double s = 0;
        for (size_t i = size_t(t) * 320; i < size_t(t) * 320 + 400; ++i) s += double(x[i]) * x[i];
        lp.at(t, 0) = float(-s); lp.at(t, 1) = float(x[size_t(t) * 320]); lp.at(t, 2) = float(-1.0 - s * 0.5);
    }
    return lp;
}

int main() {
    using namespace pron_internal;
    std::mt19937 rng(7);
    std::normal_distribution<float> nd(0.f, 0.2f);
    const int sr = 16000;
    const size_t n = size_t(40.0 * sr);
    std::vector<float> a(n);
    for (auto& v : a) v = nd(rng);
    // 0.3 s pauses (near silence) every ~3 s, as VAD segments
    std::vector<pron::SpeechSegment> vad;
    double t = 0.0;
    while (t + 3.0 < 40.0) {
        vad.push_back({t, t + 2.7});
        for (size_t i = size_t(t + 2.7) * sr; i < size_t((t + 3.0) * sr); ++i) a[i] *= 0.001f;
        t += 3.0;
    }
    vad.push_back({t, 40.0});

    auto plan = plan_phoneme_segments(a.data(), n, 0, n, vad);
    std::printf("segments: %zu\n", plan.size());
    CHECK(plan.size() >= 3);
    for (size_t i = 0; i < plan.size(); ++i) {
        CHECK(plan[i].pad_end - plan[i].pad_begin <= size_t(15.0 * sr));
        CHECK(plan[i].pad_begin <= plan[i].core_begin && plan[i].core_end <= plan[i].pad_end);
        if (i) CHECK(plan[i].core_begin >= plan[i - 1].core_end - 1);
        if (i + 1 < plan.size()) {
            // cut inside a pause
            const double cut = double(plan[i].core_end) / sr;
            bool in_pause = false;
            for (size_t j = 0; j + 1 < vad.size(); ++j) in_pause |= cut >= vad[j].end - 0.151 && cut <= vad[j + 1].start + 0.151;
            CHECK(in_pause);
        }
    }

    // Stitched posteriors == single pass over the whole recording.
    const pron::LogPosteriors single = mock_compute(a.data(), n);
    PosteriorStitcher st(3, 0);
    for (size_t i = 0; i < plan.size(); ++i)
        st.add(plan[i], mock_compute(a.data() + plan[i].pad_begin, plan[i].pad_end - plan[i].pad_begin), i + 1 == plan.size());
    const pron::LogPosteriors seg = st.finish();
    CHECK(seg.valid() && seg.frames == single.frames);
    int diff = 0;
    for (int f = 0; f < std::min(seg.frames, single.frames); ++f)
        for (int c = 0; c < 3; ++c) if (std::fabs(seg.at(f, c) - single.at(f, c)) > 1e-5f) { ++diff; break; }
    std::printf("frames %d (single %d), differing %d\n", seg.frames, single.frames, diff);
    CHECK(diff == 0);

    // One endless VAD segment is cut at the quietest spot near the limit.
    std::vector<pron::SpeechSegment> one{{0.0, 40.0}};
    auto p1 = plan_phoneme_segments(a.data(), n, 0, n, one);
    CHECK(p1.size() >= 3);
    for (const auto& g : p1) CHECK(g.pad_end - g.pad_begin <= size_t(15.0 * sr));
    // No VAD at all behaves the same.
    CHECK(plan_phoneme_segments(a.data(), n, 0, n, {}).size() == p1.size());

    // Short input: a single segment, exactly the speech region (no extra context -> identical to the old single pass).
    auto p2 = plan_phoneme_segments(a.data(), n, 8000, 8000 + 5 * sr, {{0.5, 5.5}});
    CHECK(p2.size() == 1 && p2[0].pad_begin == 8000 && p2[0].pad_end == 8000 + 5 * sr);
    CHECK(plan_phoneme_segments(a.data(), n, 10, 10, {}).empty());

    // A gap > 0.6 s between segments is blank-filled.
    std::vector<float> b(size_t(20.0 * sr));
    for (auto& v : b) v = nd(rng);
    std::vector<pron::SpeechSegment> v2{{0.0, 8.0}, {10.0, 20.0}};  // 2 s pause, 18 s of speech -> 2 segments
    auto p3 = plan_phoneme_segments(b.data(), b.size(), 0, b.size(), v2, 15.0, 0.3);
    CHECK(p3.size() == 2);
    PosteriorStitcher s3(3, 0);
    for (size_t i = 0; i < p3.size(); ++i)
        s3.add(p3[i], mock_compute(b.data() + p3[i].pad_begin, p3[i].pad_end - p3[i].pad_begin), i + 1 == p3.size());
    auto r3 = s3.finish();
    const int mid = int(9.0 / 0.02);
    CHECK(r3.frames > mid && r3.at(mid, 0) == 0.0f && r3.at(mid, 1) == -30.0f);

    std::printf(g_fail ? "FAILED\n" : "OK\n");
    return g_fail ? 1 : 0;
}

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <random>

#include "pron/onnx_backends.h"

static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)

static bool exists(const char* p) { return std::ifstream(p).good(); }

int main() {
    try {
        if (!exists(SILERO_MODEL)) { std::printf("silero model missing\n"); return 1; }
        pron::SileroVad vad(SILERO_MODEL);
        std::vector<float> silence(16000 * 2, 0.0f);
        CHECK(vad.detect({silence.data(), silence.size(), 16000}).empty());

        std::mt19937 rng(1);
        std::normal_distribution<float> nd(0.f, 0.3f);
        std::vector<float> noise(16000, 0.0f);
        for (size_t i = 0; i < noise.size(); ++i)
            if ((i / 3200) % 2 == 0) noise[i] = nd(rng);
        auto segs = vad.detect({noise.data(), noise.size(), 16000});
        std::printf("noise segments: %zu\n", segs.size());
        for (auto& s : segs) CHECK(s.start >= 0 && s.end > s.start && s.end <= 1.0 + 1e-6);

        pron::Wav2Vec2PhonemeModel m(FAKE_MODEL, FAKE_VOCAB);
        auto labels = m.labels();
        CHECK(labels.size() == 6);
        CHECK(m.blank_index() == 0);
        CHECK(labels[4] == "\xC9\x99");      // schwa from ə escape
        CHECK(labels[5] == "\xCE\xB8");      // raw UTF-8 theta
        std::vector<float> a(8000);
        for (auto& v : a) v = nd(rng);
        auto lp = m.compute({a.data(), a.size(), 16000});
        CHECK(lp.valid() && lp.frames == 5 && lp.classes == 6);
        for (int t = 0; t < lp.frames; ++t) {
            double s = 0;
            for (int c = 0; c < lp.classes; ++c) s += std::exp(double(lp.at(t, c)));
            CHECK(std::fabs(s - 1.0) < 1e-5);
        }
        CHECK(lp.at(0, 5) > lp.at(0, 0));
    } catch (const std::exception& e) {
        std::printf("exception: %s\n", e.what());
        return 1;
    }
    return fails ? 1 : 0;
}

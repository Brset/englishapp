#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

#include <onnxruntime_cxx_api.h>

#include "pron/onnx_backends.h"

namespace pron {

#ifdef _WIN32
#define PRON_ORT_PATH(s) std::wstring((s).begin(), (s).end()).c_str()
#else
#define PRON_ORT_PATH(s) (s).c_str()
#endif

namespace {
constexpr int kChunk = 512;    // samples per step at 16 kHz (32 ms)
constexpr int kContext = 64;   // v5/v6 models expect 64 samples of previous context prepended
constexpr int kStateSize = 2 * 1 * 128;
}  // namespace

struct SileroVad::Impl {
    Ort::Env env{ORT_LOGGING_LEVEL_WARNING, "pron_vad"};
    Ort::Session session{nullptr};
    SileroVadOptions opt;
};

SileroVad::SileroVad(const std::string& model_path, SileroVadOptions opt) : impl_(new Impl) {
    impl_->opt = opt;
    Ort::SessionOptions so;
    so.SetIntraOpNumThreads(1);
    so.SetInterOpNumThreads(1);
    impl_->session = Ort::Session(impl_->env, PRON_ORT_PATH(model_path), so);
}
SileroVad::~SileroVad() = default;

std::vector<SpeechSegment> SileroVad::detect(const AudioView& audio) {
    std::vector<SpeechSegment> segs;
    if (!audio.samples || audio.count == 0 || audio.sample_rate <= 0) return segs;

    // Resample to 16 kHz (linear) if needed.
    std::vector<float> res;
    const float* s = audio.samples;
    size_t n = audio.count;
    if (audio.sample_rate != 16000) {
        n = size_t(double(audio.count) * 16000.0 / audio.sample_rate);
        res.resize(n);
        for (size_t i = 0; i < n; ++i) {
            double pos = double(i) * audio.sample_rate / 16000.0;
            size_t k = size_t(pos);
            double fr = pos - double(k);
            float a = audio.samples[std::min(k, audio.count - 1)];
            float b = audio.samples[std::min(k + 1, audio.count - 1)];
            res[i] = float(a + (b - a) * fr);
        }
        s = res.data();
    }

    const auto& o = impl_->opt;
    const double sr = 16000.0;
    const size_t min_speech = size_t(o.min_speech_ms * sr / 1000);
    const size_t min_silence = size_t(o.min_silence_ms * sr / 1000);
    const size_t pad = size_t(o.speech_pad_ms * sr / 1000);
    const float neg_thr = std::max(o.threshold - 0.15f, 0.01f);

    std::array<float, kStateSize> state{};
    std::array<float, kContext> ctx{};
    std::vector<float> buf(kContext + kChunk);
    int64_t sr_val = 16000;

    Ort::MemoryInfo mi = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    int64_t in_shape[2] = {1, kContext + kChunk};
    int64_t st_shape[3] = {2, 1, 128};
    const char* in_names[] = {"input", "state", "sr"};
    const char* out_names[] = {"output", "stateN"};

    bool triggered = false;
    size_t speech_start = 0, temp_end = 0;
    struct Raw { size_t a, b; };
    std::vector<Raw> raw;

    for (size_t pos = 0; pos < n; pos += kChunk) {
        std::copy(ctx.begin(), ctx.end(), buf.begin());
        size_t take = std::min<size_t>(kChunk, n - pos);
        std::copy(s + pos, s + pos + take, buf.begin() + kContext);
        std::fill(buf.begin() + kContext + take, buf.end(), 0.0f);

        Ort::Value ins[3] = {
            Ort::Value::CreateTensor<float>(mi, buf.data(), buf.size(), in_shape, 2),
            Ort::Value::CreateTensor<float>(mi, state.data(), state.size(), st_shape, 3),
            Ort::Value::CreateTensor<int64_t>(mi, &sr_val, 1, nullptr, 0)};
        auto out = impl_->session.Run(Ort::RunOptions{nullptr}, in_names, ins, 3, out_names, 2);
        float prob = out[0].GetTensorData<float>()[0];
        const float* ns = out[1].GetTensorData<float>();
        std::copy(ns, ns + kStateSize, state.begin());
        std::copy(buf.end() - kContext, buf.end(), ctx.begin());

        const size_t cur = pos + kChunk;
        if (prob >= o.threshold && temp_end) temp_end = 0;
        if (prob >= o.threshold && !triggered) {
            triggered = true;
            speech_start = pos;
        } else if (prob < neg_thr && triggered) {
            if (!temp_end) temp_end = cur;
            if (cur - temp_end >= min_silence) {
                if (temp_end - speech_start >= min_speech) raw.push_back({speech_start, temp_end});
                triggered = false;
                temp_end = 0;
            }
        }
    }
    if (triggered && n - speech_start >= min_speech) raw.push_back({speech_start, n});

    // Pad and merge overlapping segments.
    for (auto& r : raw) {
        size_t a = r.a > pad ? r.a - pad : 0;
        size_t b = std::min(n, r.b + pad);
        if (!segs.empty() && a * 1.0 / sr <= segs.back().end) segs.back().end = b / sr;
        else segs.push_back({a / sr, b / sr});
    }
    return segs;
}

}  // namespace pron

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

#include <onnxruntime_cxx_api.h>

#include "ort_path.h"
#include "pron/onnx_backends.h"

namespace pron {

struct Wav2Vec2PhonemeModel::Impl {
    Ort::Env env{ORT_LOGGING_LEVEL_WARNING, "pron_w2v"};
    Ort::Session session{nullptr};
    std::vector<std::string> labels;
    int blank = 0;
    std::string in_name, out_name;
};

Wav2Vec2PhonemeModel::Wav2Vec2PhonemeModel(const std::string& model_path, const std::string& vocab_path)
    : impl_(new Impl) {
#ifdef _WIN32
    std::ifstream f(std::filesystem::u8path(vocab_path), std::ios::binary);  // UTF-8 path
#else
    std::ifstream f(vocab_path, std::ios::binary);
#endif
    if (!f) throw std::runtime_error("cannot open " + vocab_path);
    std::stringstream ss; ss << f.rdbuf();
    impl_->labels = parse_vocab_json(ss.str());
    // CTC blank: HF wav2vec2 tokenizers call it "<pad>" (espeak models) or "[PAD]" (e.g. gruut/
    // ljspeech fine-tunes); a few use "<blank>"/"<eps>". Fall back to id 0, the usual blank slot.
    impl_->blank = -1;
    for (const char* name : {"<pad>", "[PAD]", "<blank>", "[blank]", "<eps>", "[pad]"}) {
        auto it = std::find(impl_->labels.begin(), impl_->labels.end(), name);
        if (it != impl_->labels.end()) { impl_->blank = int(it - impl_->labels.begin()); break; }
    }
    if (impl_->blank < 0) {
        if (impl_->labels.empty()) throw std::runtime_error("vocab.json is empty");
        impl_->blank = 0;
    }
    Ort::SessionOptions so;
    so.SetIntraOpNumThreads(2);
    impl_->session = Ort::Session(impl_->env, ort_path(model_path).c_str(), so);
    Ort::AllocatorWithDefaultOptions a;
    impl_->in_name = impl_->session.GetInputNameAllocated(0, a).get();
    impl_->out_name = impl_->session.GetOutputNameAllocated(0, a).get();
}
Wav2Vec2PhonemeModel::~Wav2Vec2PhonemeModel() = default;
std::vector<std::string> Wav2Vec2PhonemeModel::labels() const { return impl_->labels; }
int Wav2Vec2PhonemeModel::blank_index() const { return impl_->blank; }

LogPosteriors Wav2Vec2PhonemeModel::compute(const AudioView& audio) {
    if (audio.sample_rate != 16000) throw std::invalid_argument("Wav2Vec2PhonemeModel expects 16 kHz audio");
    if (!audio.samples || audio.count == 0) return LogPosteriors(0, int(impl_->labels.size()));
    const size_t n = audio.count;
    double mean = 0;
    for (size_t i = 0; i < n; ++i) mean += audio.samples[i];
    mean /= double(n);
    double var = 0;
    for (size_t i = 0; i < n; ++i) { double d = audio.samples[i] - mean; var += d * d; }
    var /= double(n);
    const float inv = float(1.0 / std::sqrt(var + 1e-7));
    std::vector<float> x(n);
    for (size_t i = 0; i < n; ++i) x[i] = float(audio.samples[i] - mean) * inv;

    Ort::MemoryInfo mi = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    int64_t shape[2] = {1, int64_t(n)};
    Ort::Value in = Ort::Value::CreateTensor<float>(mi, x.data(), n, shape, 2);
    const char* in_names[] = {impl_->in_name.c_str()};
    const char* out_names[] = {impl_->out_name.c_str()};
    auto out = impl_->session.Run(Ort::RunOptions{nullptr}, in_names, &in, 1, out_names, 1);
    auto info = out[0].GetTensorTypeAndShapeInfo();
    auto dims = info.GetShape();
    if (dims.size() != 3 || dims[2] != int64_t(impl_->labels.size()))
        throw std::runtime_error("unexpected logits shape (vocab size mismatch?)");
    const int T = int(dims[1]), V = int(dims[2]);
    const float* p = out[0].GetTensorData<float>();
    LogPosteriors lp(T, V);
    for (int t = 0; t < T; ++t) {
        const float* r = p + size_t(t) * V;
        float* o = &lp.data[size_t(t) * V];
        float mx = *std::max_element(r, r + V);
        double s = 0;
        for (int c = 0; c < V; ++c) s += std::exp(double(r[c] - mx));
        const float lse = mx + float(std::log(s));
        for (int c = 0; c < V; ++c) o[c] = r[c] - lse;
    }
    return lp;
}

}  // namespace pron

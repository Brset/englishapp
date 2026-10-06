#include "pron/piper_tts.h"

#include <cstring>
#include <stdexcept>

#include "sherpa-onnx/c-api/c-api.h"

namespace pron {

struct PiperTts::Impl {
    const SherpaOnnxOfflineTts* tts = nullptr;
};

PiperTts::PiperTts(const std::string& voice_onnx, const std::string& tokens, const std::string& espeak_data_dir,
                   int num_threads)
    : impl_(new Impl) {
    SherpaOnnxOfflineTtsConfig cfg;
    std::memset(&cfg, 0, sizeof(cfg));
    cfg.model.vits.model = voice_onnx.c_str();
    cfg.model.vits.tokens = tokens.c_str();
    cfg.model.vits.data_dir = espeak_data_dir.c_str();
    cfg.model.vits.noise_scale = 0.667f;
    cfg.model.vits.noise_scale_w = 0.8f;
    cfg.model.vits.length_scale = 1.0f;  // speed is applied per call (length_scale = 1/speed)
    cfg.model.num_threads = num_threads;
    cfg.model.provider = "cpu";
    cfg.max_num_sentences = 1;
    impl_->tts = SherpaOnnxCreateOfflineTts(&cfg);
    if (!impl_->tts) throw std::runtime_error("PiperTts: failed to load voice " + voice_onnx);
}

PiperTts::~PiperTts() {
    if (impl_ && impl_->tts) SherpaOnnxDestroyOfflineTts(impl_->tts);
}

int PiperTts::sample_rate() const { return SherpaOnnxOfflineTtsSampleRate(impl_->tts); }

std::vector<float> PiperTts::synthesize(const std::string& text, float speed) {
    if (!(speed > 0.0f)) speed = 1.0f;
    const SherpaOnnxGeneratedAudio* a = SherpaOnnxOfflineTtsGenerate(impl_->tts, text.c_str(), 0, speed);
    if (!a) return {};
    std::vector<float> out(a->samples, a->samples + (a->n > 0 ? a->n : 0));
    SherpaOnnxDestroyOfflineTtsGeneratedAudio(a);
    return out;
}

}  // namespace pron

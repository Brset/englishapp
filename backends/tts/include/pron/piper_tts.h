// Piper (VITS) text-to-speech via the sherpa-onnx offline TTS C API.
#pragma once
#include <memory>
#include <string>
#include <vector>

#include "pron/backends.h"

namespace pron {

class PiperTts : public ITts {
public:
    // voice_onnx: e.g. en_US-lessac-medium.onnx; tokens: tokens.txt; espeak_data_dir: espeak-ng-data.
    // Throws std::runtime_error if the voice cannot be loaded.
    PiperTts(const std::string& voice_onnx, const std::string& tokens, const std::string& espeak_data_dir,
             int num_threads = 2);
    ~PiperTts() override;
    int sample_rate() const override;
    // speed 1.0 = normal; length_scale = 1/speed. Returns empty on failure.
    std::vector<float> synthesize(const std::string& text, float speed = 1.0f) override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// TODO: EspeakG2P : IG2P. The sherpa-onnx C API does not expose phonemization; implement with
// libespeak-ng directly (espeak_TextToPhonemes) + phoneme_id_from_ipa().

}  // namespace pron

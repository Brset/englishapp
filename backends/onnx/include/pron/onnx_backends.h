// ONNX Runtime implementations of IVad and IPhonemeModel.
#pragma once
#include <memory>
#include <string>
#include <vector>

#include "pron/backends.h"

namespace pron {

struct SileroVadOptions {
    float threshold = 0.5f;
    int min_speech_ms = 250;
    int min_silence_ms = 100;
    int speech_pad_ms = 30;
};

class SileroVad : public IVad {
public:
    explicit SileroVad(const std::string& model_path, SileroVadOptions opt = {});
    ~SileroVad() override;
    std::vector<SpeechSegment> detect(const AudioView& audio) override;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

class Wav2Vec2PhonemeModel : public IPhonemeModel {
public:
    Wav2Vec2PhonemeModel(const std::string& model_path, const std::string& vocab_json_path);
    ~Wav2Vec2PhonemeModel() override;
    std::vector<std::string> labels() const override;
    int blank_index() const override;
    LogPosteriors compute(const AudioView& audio) override;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Parses a flat {"token": id} JSON object; returns tokens ordered by id. Throws on error.
std::vector<std::string> parse_vocab_json(const std::string& text);

}  // namespace pron

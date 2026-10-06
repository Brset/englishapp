// Abstract interfaces for the neural / native backends. pron_core contains NO implementation
// of these: the platform layers (Windows, Android) or later core modules provide them.
// All audio is mono float PCM in [-1, 1].
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "pron/phonemes.h"
#include "pron/posteriors.h"

namespace pron {

struct AudioView {
    const float* samples = nullptr;
    std::size_t count = 0;
    int sample_rate = 16000;
    double duration_seconds() const { return sample_rate > 0 ? double(count) / sample_rate : 0.0; }
};

struct SpeechSegment {
    double start = 0.0;  // seconds
    double end = 0.0;
};

// Voice activity detection.
// Planned implementation: Silero VAD (ONNX model) running on onnxruntime.
class IVad {
public:
    virtual ~IVad() = default;
    // Returns speech segments ordered by time.
    virtual std::vector<SpeechSegment> detect(const AudioView& audio) = 0;
};

struct AsrWord {
    std::string text;        // as produced by the recognizer (may contain punctuation / leading space)
    double start = 0.0;      // seconds
    double end = 0.0;
    float probability = 1.0f;  // recognizer confidence 0..1
};

struct AsrOptions {
    std::string language = "en";
    // Reference text may be passed as an initial prompt to bias decoding (optional, use with care:
    // a strong prompt makes the recognizer "hear" the reference instead of what was said).
    std::string initial_prompt;
};

// Speech recognition with word-level timestamps.
// Planned implementation: whisper.cpp (ggml Whisper models, token timestamps / DTW).
class IAsr {
public:
    virtual ~IAsr() = default;
    virtual std::vector<AsrWord> transcribe(const AudioView& audio, const AsrOptions& opt) = 0;
};

// Frame-level phoneme recognizer.
// Planned implementation: wav2vec2 CTC phoneme model (e.g. facebook/wav2vec2-lv-60-espeak-cv-ft
// or a smaller English phoneme fine-tune) exported to ONNX and run on onnxruntime.
class IPhonemeModel {
public:
    virtual ~IPhonemeModel() = default;
    // Output labels (IPA strings as in the model's vocab.json) and the CTC blank index.
    virtual std::vector<std::string> labels() const = 0;
    virtual int blank_index() const = 0;
    // Per-frame log-softmax posteriors, frames x labels().size(), typically 20 ms per frame.
    virtual LogPosteriors compute(const AudioView& audio) = 0;
};

// Text-to-speech for reference playback of words / sentences.
// Planned implementation: Piper (VITS ONNX voices on onnxruntime, phonemization by espeak-ng).
class ITts {
public:
    virtual ~ITts() = default;
    virtual int sample_rate() const = 0;
    // Synthesizes UTF-8 text; speed 1.0 = normal, < 1 slower.
    virtual std::vector<float> synthesize(const std::string& text, float speed = 1.0f) = 0;
};

// Grapheme-to-phoneme fallback for words missing from CMUdict.
// Planned implementation: espeak-ng (libespeak-ng, en-us voice, IPA output mapped with
// phoneme_id_from_ipa()).
class IG2P {
public:
    virtual ~IG2P() = default;
    // Returns false if no pronunciation can be produced.
    virtual bool pronounce(const std::string& word, Pronunciation& out) = 0;
};

}  // namespace pron

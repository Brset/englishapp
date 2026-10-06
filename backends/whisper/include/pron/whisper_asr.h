#pragma once
#include <memory>
#include <string>
#include <vector>

#include "pron/backends.h"

namespace pron {

class WhisperAsr : public IAsr {
public:
    // Throws std::runtime_error if the model cannot be loaded.
    // DTW token timestamps are enabled automatically when the file name contains "base.en"
    // (uses the base.en alignment-heads preset), unless use_dtw is set to false explicitly.
    explicit WhisperAsr(const std::string& model_path, int n_threads = 4, int use_dtw = -1);
    ~WhisperAsr() override;
    WhisperAsr(const WhisperAsr&) = delete;
    WhisperAsr& operator=(const WhisperAsr&) = delete;

    std::vector<AsrWord> transcribe(const AudioView& audio, const AsrOptions& opt) override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace pron

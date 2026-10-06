#pragma once
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "pron/backends.h"

namespace pron {

// Thrown by WhisperAsr::transcribe when the abort hook returned true.
struct AsrCancelled : std::runtime_error {
    AsrCancelled() : std::runtime_error("cancelled") {}
};

class WhisperAsr : public IAsr {
public:
    // Throws std::runtime_error if the model cannot be loaded.
    // DTW token timestamps are enabled automatically when the file name contains "base.en"
    // (uses the base.en alignment-heads preset), unless use_dtw is set to false explicitly.
    explicit WhisperAsr(const std::string& model_path, int n_threads = 4, int use_dtw = -1);
    ~WhisperAsr() override;
    WhisperAsr(const WhisperAsr&) = delete;
    WhisperAsr& operator=(const WhisperAsr&) = delete;

    // Optional hooks (empty = off; existing callers are unaffected). Both run on the thread that
    // called transcribe() unless noted. progress receives whisper's own 0..100 value; abort is
    // polled by whisper (possibly from its worker threads, so it must be thread-safe and cheap)
    // and makes transcribe() throw AsrCancelled when it returns true.
    void set_progress_callback(std::function<void(int percent)> cb);
    void set_abort_callback(std::function<bool()> cb);

    std::vector<AsrWord> transcribe(const AudioView& audio, const AsrOptions& opt) override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace pron

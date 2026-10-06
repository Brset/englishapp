// Private to the engine library: engine state shared between engine.cpp and live.cpp.
#pragma once

#include <atomic>
#include <cstddef>
#include <mutex>
#include <filesystem>
#include <map>
#include <thread>
#include <unordered_map>
#include <memory>
#include <string>
#include <vector>

#include "pron/backends.h"
#include "pron/pron_engine.h"

// Default processing seconds per audio second, per stage (vad, asr, phoneme, assess); sums to 0.6.
constexpr double kDefaultRatio[4] = {0.02, 0.28, 0.28, 0.02};

// Per-calling-thread last error: concurrent calls from different threads never see each other's message.
class ErrSlot {
public:
    ErrSlot& operator=(const std::string& s) { slot() = s; return *this; }
    ErrSlot& operator=(const char* s) { slot() = s ? s : ""; return *this; }
    void clear() { slot().clear(); }
    const char* c_str() const { return const_cast<ErrSlot*>(this)->slot().c_str(); }
    operator std::string() const { return const_cast<ErrSlot*>(this)->slot(); }
private:
    std::string& slot() {
        std::lock_guard<std::mutex> lk(mu_);
        return m_[std::this_thread::get_id()];  // node-based map: references stay valid
    }
    std::mutex mu_;
    std::unordered_map<std::thread::id, std::string> m_;
};

struct pron_engine {
    pron_assessor* assessor = nullptr;
    std::unique_ptr<pron::IAsr> asr;
    std::unique_ptr<pron::IVad> vad;
    std::unique_ptr<pron::IPhonemeModel> phoneme;
    std::unique_ptr<pron::ITts> tts_us, tts_gb;
    bool cmudict = false;
    std::map<std::string, std::string> errors;  // component -> message
    ErrSlot last_error;
    // Threading: heavy_mu serializes assess calls (asr + phoneme model); assessor_mu guards the core
    // assessor (pron_assess, lookup, settings) and is only held briefly; live_mu guards lazy creation of
    // the live recognizer; one mutex per TTS voice; errors_mu guards `errors`.
    std::mutex heavy_mu, assessor_mu, live_mu, tts_us_mu, tts_gb_mu, errors_mu;
    int n_threads = 2;
    std::atomic<bool> cancel{false};
    std::mutex ratio_mu;  // guards ratio / ratio_seen (estimate_seconds may run on another thread)
    double ratio[4] = {0, 0, 0, 0};
    bool ratio_seen[4] = {false, false, false, false};
    // live reading tracker: model files present; recognizer created lazily by pron_live_start
    bool live_available = false;
    std::filesystem::path models_dir;
    std::shared_ptr<void> live_recognizer;  // owned by live.cpp (type-erased; sessions hold a reference)
    ~pron_engine() { if (assessor) pron_assessor_destroy(assessor); }
};

namespace pron_internal {
char* dup_string(const std::string& s);
std::string c_api_path(const std::filesystem::path& p);
std::vector<float> resample_16k(const float* x, size_t n, int sr);
std::string path_str(const std::filesystem::path& p);
bool file_exists(const std::filesystem::path& p);
}  // namespace pron_internal

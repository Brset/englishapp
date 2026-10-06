// Private to the engine library: engine state shared between engine.cpp and live.cpp.
#pragma once

#include <cstddef>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "pron/backends.h"
#include "pron/pron_engine.h"

struct pron_engine {
    pron_assessor* assessor = nullptr;
    std::unique_ptr<pron::IAsr> asr;
    std::unique_ptr<pron::IVad> vad;
    std::unique_ptr<pron::IPhonemeModel> phoneme;
    std::unique_ptr<pron::ITts> tts_us, tts_gb;
    bool cmudict = false;
    std::map<std::string, std::string> errors;  // component -> message
    std::string last_error;
    int n_threads = 2;
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

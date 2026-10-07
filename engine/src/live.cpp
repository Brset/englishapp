// pron_live_*: streaming recognizer (sherpa-onnx online Zipformer) + core LiveTracker. See pron_live.h.
#include "pron/pron_live.h"

#include <algorithm>
#include <cstring>
#include <exception>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "engine_internal.h"
#include "pron/live_tracker.h"
#include "sherpa-onnx/c-api/c-api.h"

namespace fs = std::filesystem;

namespace {

struct Recognizer {
    const SherpaOnnxOnlineRecognizer* rec = nullptr;
    ~Recognizer() { if (rec) SherpaOnnxDestroyOnlineRecognizer(rec); }
};

std::shared_ptr<Recognizer> get_recognizer(pron_engine* e) {
    std::lock_guard<std::mutex> lk(e->live_mu);
    if (e->live_recognizer) return std::static_pointer_cast<Recognizer>(e->live_recognizer);
    if (!e->live_available) { e->last_error = "live model not available"; return nullptr; }
    const fs::path d = e->models_dir / "live";
    const std::string enc = pron_internal::c_api_path(d / "encoder.onnx");
    const std::string dec = pron_internal::c_api_path(d / "decoder.onnx");
    const std::string joi = pron_internal::c_api_path(d / "joiner.onnx");
    const std::string tok = pron_internal::c_api_path(d / "tokens.txt");
    SherpaOnnxOnlineRecognizerConfig cfg;
    std::memset(&cfg, 0, sizeof(cfg));
    cfg.feat_config.sample_rate = 16000;
    cfg.feat_config.feature_dim = 80;
    cfg.model_config.transducer.encoder = enc.c_str();
    cfg.model_config.transducer.decoder = dec.c_str();
    cfg.model_config.transducer.joiner = joi.c_str();
    cfg.model_config.tokens = tok.c_str();
    cfg.model_config.num_threads = std::max(1, std::min(2, e->n_threads));
    cfg.model_config.provider = "cpu";
    cfg.decoding_method = "greedy_search";
    cfg.enable_endpoint = 0;  // we track continuously
    auto r = std::make_shared<Recognizer>();
    r->rec = SherpaOnnxCreateOnlineRecognizer(&cfg);
    if (!r->rec) {
        e->last_error = "failed to create live recognizer";
        { std::lock_guard<std::mutex> elk(e->errors_mu); e->errors["live"] = std::string(e->last_error); }
        return nullptr;
    }
    e->live_recognizer = r;
    return r;
}

}  // namespace

struct pron_live {
    pron_engine* engine = nullptr;
    std::shared_ptr<Recognizer> rec;
    const SherpaOnnxOnlineStream* stream = nullptr;
    std::unique_ptr<pron::LiveTracker> tracker;
    std::string partial;
    bool finished = false;
    ~pron_live() { if (stream) SherpaOnnxDestroyOnlineStream(stream); }

    // decode what is ready, return the current hypothesis words
    std::vector<std::string> decode() {
        while (SherpaOnnxIsOnlineStreamReady(rec->rec, stream)) SherpaOnnxDecodeOnlineStream(rec->rec, stream);
        std::vector<std::string> words;
        const SherpaOnnxOnlineRecognizerResult* r = SherpaOnnxGetOnlineStreamResult(rec->rec, stream);
        partial.clear();
        if (r) {
            if (r->text) partial = r->text;
            SherpaOnnxDestroyOnlineRecognizerResult(r);
        }
        std::istringstream is(partial);
        std::string w;
        while (is >> w) words.push_back(w);
        return words;
    }
    std::string state_json(bool final) {
        tracker->update(decode(), final);
        return pron::live_state_json(*tracker, partial);
    }
};

namespace {

char* feed_impl(pron_live* s, const float* x, size_t n, int sr) {
    if (!s) return nullptr;
    pron_engine* e = s->engine;
    try {
        e->last_error.clear();
        if (!pron_internal::valid_sample_rate(sr) || (!x && n > 0)) { e->last_error = "invalid arguments"; return nullptr; }
        if (n > 0 && !s->finished) {
            std::vector<float> a = pron_internal::resample_16k(x, n, sr);
            if (!a.empty()) SherpaOnnxOnlineStreamAcceptWaveform(s->stream, 16000, a.data(), static_cast<int32_t>(a.size()));
        }
        if (s->finished) return pron_internal::dup_string(pron::live_state_json(*s->tracker, s->partial));
        return pron_internal::dup_string(s->state_json(false));
    } catch (const std::exception& ex) {
        e->last_error = ex.what();
    } catch (...) {
        e->last_error = "unknown error";
    }
    return nullptr;
}

}  // namespace

extern "C" {

PRON_API pron_live* pron_live_start(pron_engine* e, const char* reference_utf8) {
    if (!e) return nullptr;
    try {
        e->last_error.clear();
        if (!reference_utf8) { e->last_error = "reference is NULL"; return nullptr; }
        auto rec = get_recognizer(e);
        if (!rec) return nullptr;
        auto s = std::make_unique<pron_live>();
        s->engine = e;
        s->rec = rec;
        s->tracker = std::make_unique<pron::LiveTracker>(reference_utf8);
        s->stream = SherpaOnnxCreateOnlineStream(rec->rec);
        if (!s->stream) { e->last_error = "failed to create live stream"; return nullptr; }
        return s.release();
    } catch (const std::exception& ex) {
        e->last_error = ex.what();
    } catch (...) {
        e->last_error = "unknown error";
    }
    return nullptr;
}

PRON_API char* pron_live_feed_pcm16(pron_live* s, const int16_t* samples, size_t count, int sample_rate) {
    if (!s) return nullptr;
    try {
        if (!samples && count > 0) { s->engine->last_error = "samples is NULL"; return nullptr; }
        std::vector<float> f(count);
        for (size_t i = 0; i < count; ++i) f[i] = static_cast<float>(samples[i]) / 32768.0f;
        return feed_impl(s, f.data(), count, sample_rate);
    } catch (...) {
        s->engine->last_error = "unknown error";
        return nullptr;
    }
}

PRON_API char* pron_live_feed_f32(pron_live* s, const float* samples, size_t count, int sample_rate) {
    return feed_impl(s, samples, count, sample_rate);
}

PRON_API char* pron_live_finish(pron_live* s) {
    if (!s) return nullptr;
    pron_engine* e = s->engine;
    try {
        e->last_error.clear();
        if (!s->finished) {
            // 0.8 s of silence so the last frames leave the encoder's look-ahead, then flush
            std::vector<float> pad(12800, 0.0f);
            SherpaOnnxOnlineStreamAcceptWaveform(s->stream, 16000, pad.data(), static_cast<int32_t>(pad.size()));
            SherpaOnnxOnlineStreamInputFinished(s->stream);
            s->state_json(true);
            s->finished = true;
        }
        return pron_internal::dup_string(pron::live_state_json(*s->tracker, s->partial));
    } catch (const std::exception& ex) {
        e->last_error = ex.what();
    } catch (...) {
        e->last_error = "unknown error";
    }
    return nullptr;
}

PRON_API void pron_live_set_cursor(pron_live* s, int word_index) {
    if (!s) return;
    try { s->tracker->set_cursor(word_index); } catch (...) {}
}

PRON_API void pron_live_free(pron_live* s) {
    try { delete s; } catch (...) {}
}

}  // extern "C"

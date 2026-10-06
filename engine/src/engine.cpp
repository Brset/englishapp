// pron_engine: audio in -> assessment JSON out, text in -> speech out. See pron_engine.h.
#ifdef _WIN32
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#endif
#include "pron/pron_engine.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "pron/backends.h"
#include "pron/onnx_backends.h"
#include "pron/piper_tts.h"
#include "pron/whisper_asr.h"

namespace fs = std::filesystem;

#include "engine_internal.h"
#include "phoneme_segments.h"
#include "pron/pron_progress.h"

namespace pron_internal {

char* dup_string(const std::string& s);
std::string c_api_path(const fs::path& p);
std::vector<float> resample_16k(const float* x, size_t n, int sr);
std::string path_str(const fs::path& p);
bool file_exists(const fs::path& p);
}  // namespace pron_internal

namespace {

using Clock = std::chrono::steady_clock;
double ms_since(Clock::time_point t) {
    return std::chrono::duration<double, std::milli>(Clock::now() - t).count();
}

std::string json_escape(const std::string& s) {
    std::string o;
    o.reserve(s.size() + 2);
    for (unsigned char c : s) {
        switch (c) {
            case '"': o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n"; break;
            case '\r': o += "\\r"; break;
            case '\t': o += "\\t"; break;
            default:
                if (c < 0x20) { char b[8]; std::snprintf(b, sizeof b, "\\u%04x", c); o += b; }
                else o += char(c);
        }
    }
    return o;
}

char* dup_string(const std::string& s) {
    char* p = static_cast<char*>(std::malloc(s.size() + 1));
    if (!p) return nullptr;
    std::memcpy(p, s.c_str(), s.size() + 1);
    return p;
}

// UTF-8 (never the ANSI code page: generic_string()/string() would mangle or throw on Windows).
std::string path_str(const fs::path& p) {
    const auto u = p.generic_u8string();
    return std::string(reinterpret_cast<const char*>(u.data()), u.size());
}

// Path for third-party C APIs that take char* (sherpa-onnx/espeak-ng): on Windows they may use the ANSI
// code page, so a non-ASCII path is replaced by its ASCII 8.3 short form when the volume provides one.
std::string c_api_path(const fs::path& p) {
    std::string u = path_str(p);
#ifdef _WIN32
    bool ascii = true;
    for (unsigned char c : u) if (c >= 0x80) { ascii = false; break; }
    if (!ascii) {
        const std::wstring w = p.native();
        const DWORD n = GetShortPathNameW(w.c_str(), nullptr, 0);
        if (n > 0) {
            std::wstring sh(n, L'\0');
            const DWORD m = GetShortPathNameW(w.c_str(), &sh[0], n);
            if (m > 0 && m < n) {
                sh.resize(m);
                std::string out;
                for (wchar_t c : sh) { if (c >= 0x80) { out.clear(); break; } out += static_cast<char>(c); }
                if (!out.empty()) return out;
            }
        }
    }
#endif
    return u;
}

fs::path make_path(const std::string& utf8) {
#if defined(__cpp_char8_t)
    return fs::path(std::u8string(reinterpret_cast<const char8_t*>(utf8.data()), utf8.size()));
#else
    return fs::u8path(utf8);
#endif
}

bool file_exists(const fs::path& p) { std::error_code ec; return fs::is_regular_file(p, ec); }

// Linear resampling of mono float PCM to 16 kHz.
std::vector<float> resample_16k(const float* x, size_t n, int sr) {
    std::vector<float> out;
    if (sr == 16000) { out.assign(x, x + n); return out; }
    const double ratio = double(sr) / 16000.0;
    const size_t m = static_cast<size_t>(std::floor(double(n) / ratio));
    out.resize(m);
    for (size_t i = 0; i < m; ++i) {
        const double pos = double(i) * ratio;
        const size_t i0 = static_cast<size_t>(pos);
        const size_t i1 = std::min(i0 + 1, n - 1);
        const float f = float(pos - double(i0));
        out[i] = x[i0] * (1.0f - f) + x[i1] * f;
    }
    return out;
}

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

template <class T, class F>
void load_component(pron_engine* e, const char* name, const fs::path& required, std::unique_ptr<T>& slot, F&& make) {
    if (!file_exists(required)) {
        e->errors[name] = "file not found: " + path_str(required);
        return;
    }
    try {
        slot = make();
    } catch (const std::exception& ex) {
        e->errors[name] = ex.what();
    } catch (...) {
        e->errors[name] = "unknown error";
    }
}

void load_voice(pron_engine* e, const fs::path& models, const char* id, std::unique_ptr<pron::ITts>& slot) {
    const fs::path d = models / "tts" / id;
    const std::string key = std::string("tts_") + id;
    const fs::path onnx = d / "model.onnx", tokens = d / "tokens.txt", espeak = d / "espeak-ng-data";
    std::error_code ec;
    if (!file_exists(onnx)) { e->errors[key] = "file not found: " + path_str(onnx); return; }
    if (!file_exists(tokens)) { e->errors[key] = "file not found: " + path_str(tokens); return; }
    if (!fs::is_directory(espeak, ec)) { e->errors[key] = "directory not found: " + path_str(espeak); return; }
    try {
        slot = std::make_unique<pron::PiperTts>(c_api_path(onnx), c_api_path(tokens), c_api_path(espeak), std::min(e->n_threads, 4));
    } catch (const std::exception& ex) {
        e->errors[key] = ex.what();
    } catch (...) {
        e->errors[key] = "unknown error";
    }
}

std::string ints(double v) { char b[32]; std::snprintf(b, sizeof b, "%.1f", v); return b; }
std::string num(double v) { char b[40]; std::snprintf(b, sizeof b, "%.4f", v); return b; }

enum Stage { kVad = 0, kAsr = 1, kPhoneme = 2, kAssess = 3, kStages = 4 };
const char* const kStageNames[kStages] = {"vad", "asr", "phoneme", "assess"};

bool stage_present(const pron_engine* e, int s) {
    return s == kVad ? bool(e->vad) : s == kAsr ? bool(e->asr) : s == kPhoneme ? bool(e->phoneme) : true;
}

// Seconds of processing per second of audio, per stage (moving average; defaults sum to 0.6).
double stage_ratio(pron_engine* e, int s) {
    return e->ratio_seen[s] ? e->ratio[s] : kDefaultRatio[s];
}
double total_ratio(pron_engine* e) {
    std::lock_guard<std::mutex> lk(e->ratio_mu);
    double t = 0;
    for (int s = 0; s < kStages; ++s) t += stage_ratio(e, s);
    return t;
}

// Progress reporting for one assess call; runs on the calling thread.
struct Reporter {
    pron_progress_fn cb = nullptr;
    void* user = nullptr;
    Clock::time_point t0 = Clock::now(), last_emit = Clock::now();
    double w[kStages] = {0, 0, 0, 0};  // normalized stage weights
    double est_total = 0;              // per-device estimate of the whole call, seconds
    double last_frac = 0;
    int stage = -1;
    double before = 0;
    bool started = false;

    void init(pron_engine* e, double audio_sec) {
        double r[kStages], sum = 0;
        {
            std::lock_guard<std::mutex> lk(e->ratio_mu);
            for (int s = 0; s < kStages; ++s) { r[s] = stage_present(e, s) ? stage_ratio(e, s) : 0.0; sum += r[s]; }
        }
        for (int s = 0; s < kStages; ++s) w[s] = sum > 0 ? r[s] / sum : (s == kAssess ? 1.0 : 0.0);
        est_total = sum * audio_sec;
    }
    void fire(const char* name, double f, bool force) {
        if (!cb) return;
        const auto now = Clock::now();
        if (!force && std::chrono::duration<double>(now - last_emit).count() < 0.5) return;
        last_emit = now;
        const double el = std::chrono::duration<double>(now - t0).count();
        double eta;
        if (f > 0.05) eta = std::max(0.0, el / f - el);
        else eta = std::max(0.0, est_total - el);
        try { cb(user, name, f, eta); } catch (...) {}
    }
    void begin(int s) {
        stage = s;
        before = 0;
        for (int i = 0; i < s; ++i) before += w[i];
        fire(kStageNames[s], last_frac = std::max(last_frac, std::min(0.99, before)), true);
    }
    // within: 0..1 progress inside the current stage
    void update(double within) {
        if (stage < 0) return;
        const double f = std::max(last_frac, std::min(0.99, before + std::max(0.0, std::min(1.0, within)) * w[stage]));
        last_frac = f;
        fire(kStageNames[stage], f, false);
    }
    void done() {
        if (!cb) return;
        last_frac = 1.0;
        fire("done", 1.0, true);
    }
};

char* assess_f32_impl(pron_engine* e, const float* samples, size_t count, int sample_rate, const char* reference,
                      pron_progress_fn cb, void* user) {
    std::lock_guard<std::mutex> heavy(e->heavy_mu);  // concurrent heavy calls serialize
    e->last_error.clear();
    e->cancel.store(false);
    const auto cancelled = [&]() -> char* { e->last_error = "cancelled"; return nullptr; };
    if (!reference) { e->last_error = "reference is NULL"; return nullptr; }
    if (sample_rate <= 0) { e->last_error = "invalid sample rate"; return nullptr; }
    if (!samples && count > 0) { e->last_error = "samples is NULL"; return nullptr; }

    std::vector<float> audio;
    if (count > 0) audio = resample_16k(samples, count, sample_rate);
    for (float& v : audio) v = std::max(-1.0f, std::min(1.0f, v));
    const double audio_sec = double(audio.size()) / 16000.0;

    Reporter rep;
    rep.cb = cb;
    rep.user = user;
    rep.init(e, audio_sec);

    double t_vad = 0, t_asr = 0, t_ph = 0, t_as = 0;
    bool ran_vad = false, ran_asr = false, ran_ph = false;

    // VAD: trim leading/trailing silence only, inner pauses stay.
    std::vector<pron::SpeechSegment> segments;
    size_t b = 0, en = audio.size();
    bool have_speech = !audio.empty();
    if (e->vad && !audio.empty()) {
        rep.begin(kVad);
        auto t0 = Clock::now();
        pron::AudioView av{audio.data(), audio.size(), 16000};
        segments = e->vad->detect(av);
        t_vad = ms_since(t0);
        ran_vad = true;
        if (segments.empty()) {
            have_speech = false;
        } else {
            const double s = std::max(0.0, segments.front().start), f = std::max(s, segments.back().end);
            b = std::min(audio.size(), static_cast<size_t>(s * 16000.0));
            en = std::min(audio.size(), static_cast<size_t>(std::ceil(f * 16000.0)));
            if (en <= b) have_speech = false;
        }
    }
    if (e->cancel.load()) return cancelled();
    const double offset = double(b) / 16000.0;

    std::vector<pron::AsrWord> asr_words;
    std::vector<std::string> word_text;
    std::vector<pron_word> words;
    std::string recognized;
    if (have_speech && e->asr) {
        rep.begin(kAsr);
        auto t0 = Clock::now();
        pron::AudioView av{audio.data() + b, en - b, 16000};
        auto* wasr = dynamic_cast<pron::WhisperAsr*>(e->asr.get());
        double asr_within = 0;
        const auto worker = std::this_thread::get_id();
        if (wasr) {
            wasr->set_progress_callback([&](int pct) { asr_within = pct / 100.0; rep.update(asr_within); });
            // whisper polls this (also from its compute threads): cancel flag, plus a heartbeat on our own thread.
            wasr->set_abort_callback([&]() {
                if (e->cancel.load(std::memory_order_relaxed)) return true;
                if (std::this_thread::get_id() == worker) rep.update(asr_within);
                return false;
            });
        }
        bool was_cancelled = false;
        try {
            asr_words = e->asr->transcribe(av, pron::AsrOptions{});
        } catch (const pron::AsrCancelled&) {
            was_cancelled = true;
        } catch (const std::exception& ex) {
            { std::lock_guard<std::mutex> lk(e->errors_mu); e->errors["asr_run"] = ex.what(); }
        }
        if (wasr) { wasr->set_progress_callback(nullptr); wasr->set_abort_callback(nullptr); }
        if (was_cancelled || e->cancel.load()) return cancelled();
        t_asr = ms_since(t0);
        ran_asr = true;
        word_text.reserve(asr_words.size());
        for (const auto& w : asr_words) word_text.push_back(trim(w.text));
        for (size_t i = 0; i < asr_words.size(); ++i) {
            if (word_text[i].empty()) continue;
            pron_word w;
            w.text = word_text[i].c_str();
            w.start = asr_words[i].start + offset;
            w.end = asr_words[i].end + offset;
            w.probability = asr_words[i].probability;
            words.push_back(w);
            if (!recognized.empty()) recognized += ' ';
            recognized += word_text[i];
        }
    }

    pron::LogPosteriors post;
    bool have_post = false;
    size_t n_ph_segments = 0;
    if (have_speech) {
        const auto plan = pron_internal::plan_phoneme_segments(audio.data(), audio.size(), b, en, segments);
        n_ph_segments = plan.size();
        if (e->phoneme) {
            rep.begin(kPhoneme);
            auto t0 = Clock::now();
            try {
                size_t total = 0, donelen = 0;
                for (const auto& g : plan) total += g.pad_end - g.pad_begin;
                pron_internal::PosteriorStitcher st(int(e->phoneme->labels().size()), e->phoneme->blank_index());
                for (size_t i = 0; i < plan.size(); ++i) {
                    if (e->cancel.load()) return cancelled();
                    const auto& g = plan[i];
                    pron::AudioView av{audio.data() + g.pad_begin, g.pad_end - g.pad_begin, 16000};
                    pron::LogPosteriors lp = e->phoneme->compute(av);
                    st.add(g, lp, i + 1 == plan.size());
                    donelen += g.pad_end - g.pad_begin;
                    rep.update(total ? double(donelen) / double(total) : 1.0);
                }
                post = st.finish();
                if (post.valid() && post.frames > 0 && post.classes > 0) have_post = true;
            } catch (const std::exception& ex) {
                { std::lock_guard<std::mutex> lk(e->errors_mu); e->errors["phoneme_run"] = ex.what(); }
            }
            t_ph = ms_since(t0);
            ran_ph = true;
        }
    }
    if (e->cancel.load()) return cancelled();

    rep.begin(kAssess);
    auto t0 = Clock::now();
    std::unique_lock<std::mutex> alk(e->assessor_mu);
    char* res = pron_assess(e->assessor, reference, words.empty() ? nullptr : words.data(), int(words.size()),
                            have_post ? post.data.data() : nullptr, have_post ? post.frames : 0,
                            have_post ? post.classes : 0, have_post ? post.frame_seconds : 0.02);
    t_as = ms_since(t0);
    if (!res) {
        const char* le = pron_last_error(e->assessor);
        e->last_error = (le && *le) ? le : "pron_assess failed";
        return nullptr;
    }
    alk.unlock();

    std::string json = res;
    pron_free_string(res);
    const size_t close = json.rfind('}');
    if (close == std::string::npos) { e->last_error = "unexpected assessment JSON"; return nullptr; }
    std::ostringstream x;
    x << ",\"recognized_text\":\"" << json_escape(recognized) << "\",\"speech_segments\":[";
    for (size_t i = 0; i < segments.size(); ++i) {
        if (i) x << ',';
        x << '[' << num(segments[i].start) << ',' << num(segments[i].end) << ']';
    }
    x << "],\"phoneme_segments\":" << n_ph_segments << ",\"timings_ms\":{\"vad\":" << ints(t_vad) << ",\"asr\":" << ints(t_asr) << ",\"phoneme\":" << ints(t_ph)
      << ",\"assess\":" << ints(t_as) << "}";
    json.insert(close, x.str());

    // Moving average of processing time per audio second, per stage that ran.
    if (audio_sec > 0.5) {
        std::lock_guard<std::mutex> lk(e->ratio_mu);
        const double ms[kStages] = {t_vad, t_asr, t_ph, t_as};
        const bool ran[kStages] = {ran_vad, ran_asr, ran_ph, true};
        for (int s = 0; s < kStages; ++s) {
            if (!ran[s]) continue;
            const double r = ms[s] / 1000.0 / audio_sec;
            e->ratio[s] = e->ratio_seen[s] ? 0.7 * e->ratio[s] + 0.3 * r : r;
            e->ratio_seen[s] = true;
        }
    }
    rep.done();
    return dup_string(json);
}

template <class F>
auto guard(pron_engine* e, F&& f) -> decltype(f()) {
    try {
        return f();
    } catch (const std::exception& ex) {
        if (e) e->last_error = ex.what();
    } catch (...) {
        if (e) e->last_error = "unknown error";
    }
    return decltype(f())();
}

}  // namespace

namespace pron_internal {
char* dup_string(const std::string& s) { return ::dup_string(s); }
std::string c_api_path(const fs::path& p) { return ::c_api_path(p); }
std::vector<float> resample_16k(const float* x, size_t n, int sr) { return ::resample_16k(x, n, sr); }
std::string path_str(const fs::path& p) { return ::path_str(p); }
bool file_exists(const fs::path& p) { return ::file_exists(p); }
}  // namespace pron_internal

extern "C" {

PRON_API pron_engine* pron_engine_create(const char* models_dir_utf8, int n_threads) {
    try {
        if (!models_dir_utf8) return nullptr;
        const fs::path models = make_path(models_dir_utf8);
        std::error_code ec;
        if (!fs::is_directory(models, ec)) return nullptr;

        auto e = std::make_unique<pron_engine>();
        e->n_threads = n_threads > 0 ? n_threads : static_cast<int>(std::max(1u, std::min(4u, std::thread::hardware_concurrency())));
        e->assessor = pron_assessor_create();
        if (!e->assessor) return nullptr;

        const fs::path dict = models / "cmudict" / "cmudict.dict";
        if (!file_exists(dict)) {
            e->errors["cmudict"] = "file not found: " + path_str(dict);
        } else {
            const int n = pron_assessor_load_cmudict_file(e->assessor, path_str(dict).c_str());
            if (n > 0) e->cmudict = true;
            else e->errors["cmudict"] = pron_last_error(e->assessor);
        }

        fs::path wm = models / "whisper" / "ggml-base.en.bin";
        if (!file_exists(wm)) {  // fall back to any other ggml model (e.g. tiny.en)
            std::error_code ec2;
            for (fs::directory_iterator it(models / "whisper", ec2), end; !ec2 && it != end; it.increment(ec2)) {
                const std::string n = path_str(it->path().filename());
                if (n.find("ggml-") != std::string::npos && it->path().extension() == ".bin") { wm = it->path(); break; }
            }
        }
        load_component(e.get(), "asr", wm, e->asr, [&] {
            return std::make_unique<pron::WhisperAsr>(path_str(wm), e->n_threads);
        });

        const fs::path vm = models / "vad" / "silero_vad.onnx";
        load_component(e.get(), "vad", vm, e->vad, [&] { return std::make_unique<pron::SileroVad>(path_str(vm)); });

        const fs::path pm = models / "phoneme" / "model.onnx", pv = models / "phoneme" / "vocab.json";
        if (!file_exists(pv)) {
            e->errors["phoneme"] = "file not found: " + path_str(pv);
        } else {
            load_component(e.get(), "phoneme", pm, e->phoneme, [&] {
                return std::make_unique<pron::Wav2Vec2PhonemeModel>(path_str(pm), path_str(pv));
            });
        }
        if (e->phoneme) {
            const std::vector<std::string> labels = e->phoneme->labels();
            std::vector<const char*> cs;
            for (const auto& l : labels) cs.push_back(l.c_str());
            if (pron_assessor_set_phoneme_vocab(e->assessor, cs.data(), int(cs.size()), e->phoneme->blank_index()) < 0) {
                e->errors["phoneme"] = pron_last_error(e->assessor);
                e->phoneme.reset();
            }
        }

        e->models_dir = models;
        {
            const fs::path ld = models / "live";
            std::string missing;
            for (const char* f : {"encoder.onnx", "decoder.onnx", "joiner.onnx", "tokens.txt"})
                if (!file_exists(ld / f)) { missing = path_str(ld / f); break; }
            if (missing.empty()) e->live_available = true;
            else e->errors["live"] = "file not found: " + missing;
        }
        load_voice(e.get(), models, "us", e->tts_us);
        load_voice(e.get(), models, "gb", e->tts_gb);
        return e.release();
    } catch (...) {
        return nullptr;
    }
}

PRON_API void pron_engine_destroy(pron_engine* e) {
    try { delete e; } catch (...) {}
}

PRON_API const char* pron_engine_last_error(const pron_engine* e) { return e ? e->last_error.c_str() : ""; }

PRON_API char* pron_engine_status(pron_engine* e) {
    if (!e) return nullptr;
    return guard(e, [&]() -> char* {
        std::ostringstream o;
        o << "{\"version\":\"" << json_escape(pron_version()) << "\",\"asr\":" << (e->asr ? "true" : "false")
          << ",\"vad\":" << (e->vad ? "true" : "false") << ",\"phoneme\":" << (e->phoneme ? "true" : "false")
          << ",\"live\":" << (e->live_available ? "true" : "false") << ",\"cmudict\":" << (e->cmudict ? "true" : "false") << ",\"tts\":{\"us\":" << (e->tts_us ? "true" : "false")
          << ",\"gb\":" << (e->tts_gb ? "true" : "false") << "},\"errors\":{";
        bool first = true;
        std::lock_guard<std::mutex> elk(e->errors_mu);
        for (const auto& kv : e->errors) {
            if (!first) o << ',';
            first = false;
            o << '"' << json_escape(kv.first) << "\":\"" << json_escape(kv.second) << '"';
        }
        o << "},\"phoneme_vocab\":";
        char* vj;
        { std::lock_guard<std::mutex> alk(e->assessor_mu); vj = pron_assessor_phoneme_vocab_json(e->assessor); }
        if (vj) { o << vj; pron_free_string(vj); }
        else o << "{\"size\":0,\"mapped\":0,\"unmapped\":[]}";
        o << "}";
        return dup_string(o.str());
    });
}

PRON_API void pron_engine_set_strictness(pron_engine* e, int strictness) {
    if (e && e->assessor) { std::lock_guard<std::mutex> lk(e->assessor_mu); pron_assessor_set_strictness(e->assessor, strictness); }
}

PRON_API int pron_engine_set_accent(pron_engine* e, const char* accent) {
    if (!e || !e->assessor) return -1;
    std::lock_guard<std::mutex> lk(e->assessor_mu);
    const int rc = pron_assessor_set_accent(e->assessor, accent);
    if (rc < 0) e->last_error = pron_last_error(e->assessor);
    return rc;
}

PRON_API char* pron_engine_assess_f32(pron_engine* e, const float* samples, size_t count, int sample_rate,
                                      const char* reference_utf8) {
    if (!e) return nullptr;
    return guard(e, [&] { return assess_f32_impl(e, samples, count, sample_rate, reference_utf8, nullptr, nullptr); });
}

PRON_API char* pron_engine_assess_f32_progress(pron_engine* e, const float* samples, size_t count, int sample_rate,
                                               const char* reference_utf8, pron_progress_fn cb, void* user) {
    if (!e) return nullptr;
    return guard(e, [&] { return assess_f32_impl(e, samples, count, sample_rate, reference_utf8, cb, user); });
}

PRON_API char* pron_engine_assess_pcm16_progress(pron_engine* e, const int16_t* samples, size_t count,
                                                 int sample_rate, const char* reference_utf8, pron_progress_fn cb,
                                                 void* user) {
    if (!e) return nullptr;
    return guard(e, [&]() -> char* {
        if (!samples && count > 0) { e->last_error = "samples is NULL"; return nullptr; }
        std::vector<float> f(count);
        for (size_t i = 0; i < count; ++i) f[i] = float(samples[i]) / 32768.0f;
        return assess_f32_impl(e, f.data(), count, sample_rate, reference_utf8, cb, user);
    });
}

PRON_API void pron_engine_cancel(pron_engine* e) {
    if (e) e->cancel.store(true);
}

PRON_API double pron_engine_estimate_seconds(pron_engine* e, double audio_seconds) {
    if (!e || !(audio_seconds > 0)) return 0.0;
    return audio_seconds * total_ratio(e);
}

PRON_API char* pron_engine_assess_pcm16(pron_engine* e, const int16_t* samples, size_t count, int sample_rate,
                                        const char* reference_utf8) {
    if (!e) return nullptr;
    return guard(e, [&]() -> char* {
        if (!samples && count > 0) { e->last_error = "samples is NULL"; return nullptr; }
        std::vector<float> f(count);
        for (size_t i = 0; i < count; ++i) f[i] = float(samples[i]) / 32768.0f;
        return assess_f32_impl(e, f.data(), count, sample_rate, reference_utf8, nullptr, nullptr);
    });
}

PRON_API float* pron_engine_tts(pron_engine* e, const char* text_utf8, const char* voice, float speed,
                                size_t* out_count, int* out_sample_rate) {
    if (!e) return nullptr;
    return guard(e, [&]() -> float* {
        e->last_error.clear();
        if (out_count) *out_count = 0;
        if (!text_utf8 || !out_count || !out_sample_rate) { e->last_error = "invalid arguments"; return nullptr; }
        const std::string v = voice ? voice : "us";
        pron::ITts* t = v == "us" ? e->tts_us.get() : v == "gb" ? e->tts_gb.get() : nullptr;
        if (v != "us" && v != "gb") { e->last_error = "unknown voice: " + v; return nullptr; }
        if (!t) {
            std::lock_guard<std::mutex> elk(e->errors_mu);
            auto it = e->errors.find("tts_" + v);
            e->last_error = "voice not loaded: " + v + (it != e->errors.end() ? " (" + it->second + ")" : "");
            return nullptr;
        }
        speed = std::max(0.5f, std::min(1.5f, speed));
        std::vector<float> pcm;
        {
            std::lock_guard<std::mutex> lk(v == "us" ? e->tts_us_mu : e->tts_gb_mu);
            pcm = t->synthesize(text_utf8, speed);
        }
        if (pcm.empty()) { e->last_error = "synthesis failed"; return nullptr; }
        float* out = static_cast<float*>(std::malloc(pcm.size() * sizeof(float)));
        if (!out) { e->last_error = "out of memory"; return nullptr; }
        std::memcpy(out, pcm.data(), pcm.size() * sizeof(float));
        *out_count = pcm.size();
        *out_sample_rate = t->sample_rate();
        return out;
    });
}

PRON_API void pron_engine_free_audio(float* samples) { std::free(samples); }

PRON_API char* pron_engine_lookup(pron_engine* e, const char* word_utf8) {
    if (!e || !word_utf8) return nullptr;
    return guard(e, [&] {
        std::lock_guard<std::mutex> lk(e->assessor_mu);
        char* r = pron_assessor_lookup(e->assessor, word_utf8);
        if (!r) e->last_error = pron_last_error(e->assessor);
        return r;
    });
}

}  // extern "C"

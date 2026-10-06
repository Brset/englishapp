// Engine robustness tests (hostile input, cancel in every stage, threading, memory). argv[1] = test data dir
// (models/ assembled by fetch_models.cmake: silero VAD + TTS voice + whisper test model [+ stand-in phoneme model]).
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <future>
#include <limits>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "pron/pron_engine.h"
#include "pron/pron_live.h"
#include "pron/pron_progress.h"

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); ++g_fail; } } while (0)

static bool valid_utf8(const std::string& s) {
    size_t i = 0, n = s.size();
    while (i < n) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        size_t len = c < 0x80 ? 1 : (c >= 0xC2 && c <= 0xDF) ? 2 : (c >= 0xE0 && c <= 0xEF) ? 3 : (c >= 0xF0 && c <= 0xF4) ? 4 : 0;
        if (!len || i + len > n) return false;
        uint32_t cp = len == 1 ? c : (c & (0xFF >> (len + 1)));
        for (size_t k = 1; k < len; ++k) {
            unsigned char cc = static_cast<unsigned char>(s[i + k]);
            if ((cc & 0xC0) != 0x80) return false;
            cp = (cp << 6) | (cc & 0x3F);
        }
        if ((len == 3 && cp < 0x800) || (len == 4 && cp < 0x10000) || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return false;
        i += len;
    }
    return true;
}
struct J {
    const char* p; const char* e;
    void ws() { while (p < e && (*p == ' ' || *p == '\n' || *p == '\t' || *p == '\r')) ++p; }
    bool lit(const char* s) { size_t n = std::strlen(s); if (size_t(e - p) < n || std::strncmp(p, s, n)) return false; p += n; return true; }
    bool str() {
        if (p >= e || *p != '"') return false;
        ++p;
        while (p < e && *p != '"') {
            if (static_cast<unsigned char>(*p) < 0x20) return false;
            if (*p == '\\') {
                ++p;
                if (p >= e) return false;
                if (*p == 'u') { for (int k = 0; k < 4; ++k) { ++p; if (p >= e || !std::isxdigit((unsigned char)*p)) return false; } }
                else if (!std::strchr("\"\\/bfnrt", *p)) return false;
            }
            ++p;
        }
        if (p >= e) return false;
        ++p;
        return true;
    }
    bool num() { const char* s = p; if (p < e && *p == '-') ++p; while (p < e && (std::isdigit((unsigned char)*p) || *p == '.' || *p == 'e' || *p == 'E' || *p == '+' || *p == '-')) ++p; return p > s; }
    bool val() {
        ws();
        if (p >= e) return false;
        if (*p == '{') {
            ++p; ws();
            if (p < e && *p == '}') { ++p; return true; }
            for (;;) {
                ws(); if (!str()) return false; ws();
                if (p >= e || *p++ != ':') return false;
                if (!val()) return false;
                ws();
                if (p < e && *p == ',') { ++p; continue; }
                if (p < e && *p == '}') { ++p; return true; }
                return false;
            }
        }
        if (*p == '[') {
            ++p; ws();
            if (p < e && *p == ']') { ++p; return true; }
            for (;;) {
                if (!val()) return false;
                ws();
                if (p < e && *p == ',') { ++p; continue; }
                if (p < e && *p == ']') { ++p; return true; }
                return false;
            }
        }
        if (*p == '"') return str();
        if (lit("true") || lit("false") || lit("null")) return true;
        return num();
    }
};
static bool valid_json(const std::string& s) { if (!valid_utf8(s)) return false; J j{s.data(), s.data() + s.size()}; if (!j.val()) return false; j.ws(); return j.p == j.e; }
static bool has(const std::string& s, const char* sub) { return s.find(sub) != std::string::npos; }

static long rss_kb(const char* key = "VmRSS:") {
    std::ifstream f("/proc/self/status");
    std::string l;
    while (std::getline(f, l))
        if (l.compare(0, std::strlen(key), key) == 0) return std::atol(l.c_str() + std::strlen(key));
    return -1;
}
using Clock = std::chrono::steady_clock;
static double since(Clock::time_point t) { return std::chrono::duration<double>(Clock::now() - t).count(); }

static std::string take(char* s) { std::string o = s ? s : ""; std::free(s); return o; }

// Linear resample of mono float audio.
static std::vector<float> resample(const std::vector<float>& x, int from, int to) {
    if (from == to) return x;
    const double r = double(from) / to;
    std::vector<float> o(size_t(double(x.size()) / r));
    for (size_t i = 0; i < o.size(); ++i) {
        const double p = i * r; const size_t i0 = size_t(p); const size_t i1 = std::min(i0 + 1, x.size() - 1);
        const float f = float(p - double(i0));
        o[i] = x[i0] * (1 - f) + x[i1] * f;
    }
    return o;
}

struct Ev { std::string stage; double f, eta; };
struct Log { std::vector<Ev> ev; };
static void on_progress(void* u, const char* stage, double f, double eta) { static_cast<Log*>(u)->ev.push_back({stage, f, eta}); }

static std::string words_text(int n) {
    static const char* kW[] = {"the", "three", "brothers", "walked", "along", "a", "winding", "road", "and", "they", "were", "very", "tired", "cat", "dog"};
    std::string t;
    for (int i = 0; i < n; ++i) { t += kW[i % 15]; t += (i % 11 == 10) ? ". " : " "; }
    return t;
}

int main(int argc, char** argv) {
    namespace fs = std::filesystem;
    const std::string data = argc > 1 ? argv[1] : ".";
    const std::string models = data + "/models";
    const std::string kRef = "Think about the weather.";

    // ---- engine without usable models: every entry point survives bad input -------------------------------
    {
        const std::string bad = data + "/m\xff\xfe_\"q\"\\";  // invalid UTF-8 + quote + backslash in the path
        fs::create_directories(bad);
        pron_engine* e = pron_engine_create(bad.c_str(), 1);
        CHECK(e != nullptr);
        if (e) {
            const std::string st = take(pron_engine_status(e));
            CHECK(valid_json(st));  // error messages contain the (invalid UTF-8) path
            std::printf("status(bad path): %.200s\n", st.c_str());
            pron_engine_destroy(e);  // destroy while idle, nothing ever ran
        }
        CHECK(pron_engine_create(nullptr, 0) == nullptr);
        pron_engine_destroy(nullptr);
        pron_engine_cancel(nullptr);
        CHECK(pron_engine_estimate_seconds(nullptr, 1.0) == 0.0);
        CHECK(pron_engine_status(nullptr) == nullptr);
    }

    pron_engine* e = pron_engine_create(models.c_str(), 2);
    CHECK(e != nullptr);
    if (!e) return 1;
    const std::string st0 = take(pron_engine_status(e));
    const bool vad = has(st0, "\"vad\":true"), tts = has(st0, "\"tts\":{\"us\":true");
    const bool asr = has(st0, "\"asr\":true"), ph = has(st0, "\"phoneme\":true"), live = has(st0, "\"live\":true");
    std::printf("models: vad=%d tts=%d asr=%d phoneme=%d live=%d\n", vad, tts, asr, ph, live);
    if (!vad || !tts) { std::printf("SKIP: silero/voice not available\n"); pron_engine_destroy(e); return 77; }

    size_t n = 0; int sr = 0;
    float* a = pron_engine_tts(e, kRef.c_str(), "us", 1.0f, &n, &sr);
    CHECK(a && n > 8000);
    if (!a) return 1;
    const std::vector<float> speech(a, a + n);
    pron_engine_free_audio(a);
    const int kSr = sr;  // voice rate (22050)

    auto assess = [&](const std::vector<float>& x, int rate, const std::string& ref, std::string* err = nullptr) {
        char* r = pron_engine_assess_f32(e, x.empty() ? nullptr : x.data(), x.size(), rate, ref.c_str());
        if (!r && err) *err = pron_engine_last_error(e);
        return take(r);
    };

    // ---- argument validation ------------------------------------------------------------------------------
    {
        std::string err;
        for (int bad_rate : {0, -1, 1, 3999, 384001, std::numeric_limits<int>::max(), std::numeric_limits<int>::min()}) {
            const std::string j = assess(speech, bad_rate, kRef, &err);
            CHECK(j.empty());
            CHECK(err == "invalid sample rate");
        }
        CHECK(pron_engine_assess_f32(e, nullptr, 0, 16000, nullptr) == nullptr);
        CHECK(pron_engine_assess_f32(e, nullptr, 100, 16000, "x") == nullptr);
        CHECK(std::strcmp(pron_engine_last_error(e), "samples is NULL") == 0);
        CHECK(pron_engine_assess_pcm16(e, nullptr, 100, 16000, "x") == nullptr);
        { char* z = pron_engine_assess_pcm16(e, nullptr, 0, 16000, "x"); CHECK(z != nullptr); std::free(z); }
        size_t cnt = 5; int r = 0;
        CHECK(pron_engine_tts(e, nullptr, "us", 1.0f, &cnt, &r) == nullptr);
        CHECK(pron_engine_tts(e, "x", "us", 1.0f, nullptr, &r) == nullptr);
        CHECK(pron_engine_tts(e, "x", "klingon", 1.0f, &cnt, &r) == nullptr);
        CHECK(pron_engine_lookup(e, nullptr) == nullptr);
        CHECK(pron_engine_estimate_seconds(e, -1.0) == 0.0);
        CHECK(pron_engine_estimate_seconds(e, std::nan("")) == 0.0);
        CHECK(valid_json(take(pron_engine_status(e))));
        // TTS: empty / emoji / NaN speed must not crash
        for (const char* t : {"", " ", "\xF0\x9F\x98\x80", "\xD0\x9F\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82", "!!!", "\xff\xfe"})
            for (float sp : {1.0f, std::nanf(""), -3.0f, 100.0f}) {
                cnt = 0;
                float* w = pron_engine_tts(e, t, "us", sp, &cnt, &r);
                pron_engine_free_audio(w);
            }
    }

    // ---- audio shapes ---------------------------------------------------------------------------------------
    {
        std::vector<float> empty;
        std::string j = assess(empty, 16000, kRef);
        CHECK(valid_json(j) && has(j, "\"speech_segments\":[]"));
        std::vector<float> s01(speech.begin(), speech.begin() + kSr / 10);  // 0.1 s
        j = assess(s01, kSr, kRef); CHECK(valid_json(j));
        std::vector<float> one(1, 0.5f);
        j = assess(one, 16000, kRef); CHECK(valid_json(j));
        j = assess(one, 48000, kRef); CHECK(valid_json(j));
        std::vector<float> sil(16000 * 3, 0.0f);
        j = assess(sil, 16000, kRef); CHECK(valid_json(j) && has(j, "\"speech_segments\":[]") && has(j, "\"recognized_text\":\"\""));
        j = assess(std::vector<float>(48000 * 3, 1.0f), 48000, kRef); CHECK(valid_json(j));  // full-scale DC
        // clipping / NaN / inf / absurd magnitudes
        std::vector<float> nasty = speech;
        for (size_t i = 0; i < nasty.size(); ++i) {
            if (i % 97 == 0) nasty[i] = std::nanf("");
            else if (i % 89 == 0) nasty[i] = std::numeric_limits<float>::infinity();
            else if (i % 83 == 0) nasty[i] = -std::numeric_limits<float>::infinity();
            else if (i % 79 == 0) nasty[i] = 1e30f;
            else nasty[i] *= 40.0f;  // heavy clipping
        }
        j = assess(nasty, kSr, kRef); CHECK(valid_json(j));
        std::vector<float> allnan(16000, std::nanf(""));
        j = assess(allnan, 16000, kRef); CHECK(valid_json(j));
        std::mt19937 rng(3);
        std::vector<float> noise(16000 * 3);
        for (auto& v : noise) v = float(int(rng() % 2001) - 1000) / 1000.0f;  // full-scale white noise
        j = assess(noise, 16000, kRef); CHECK(valid_json(j));
        // sample rates
        for (int rate : {4000, 8000, 11025, 16000, 22050, 44100, 48000, 96000, 384000}) {
            std::vector<float> x = resample(speech, kSr, rate);
            j = assess(x, rate, kRef);
            CHECK(valid_json(j));
            if (rate >= 16000) CHECK(!has(j, "\"speech_segments\":[]"));
            std::printf("rate %6d: %zu samples ok\n", rate, x.size());
        }
        // pcm16 incl. extreme values
        std::vector<int16_t> pcm(kSr * 2);
        for (size_t i = 0; i < pcm.size(); ++i) pcm[i] = (i & 1) ? INT16_MAX : INT16_MIN;
        j = take(pron_engine_assess_pcm16(e, pcm.data(), pcm.size(), kSr, kRef.c_str())); CHECK(valid_json(j));
    }

    // ---- references -------------------------------------------------------------------------------------------
    {
        std::vector<float> clip(16000 / 2, 0.0f);
        clip.insert(clip.end(), speech.begin(), speech.end());
        const std::string refs[] = {"", "   \t\n ", "!!! ... ???", "\xF0\x9F\x98\x80\xF0\x9F\x98\x80 \xF0\x9F\x8E\x89",
                                    "\xD0\x9F\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82 \xD0\xBC\xD0\xB8\xD1\x80",
                                    "say \"hi\" \\ back\nslash\ttab \x01\x02", "\xff\xfe broken \xF0\x9F", "12345678901234567890 and 3rd and 1,234.5",
                                    "Think\0about", words_text(300)};
        for (const auto& r : refs) {
            std::string j = assess(clip, kSr, r);
            CHECK(valid_json(j));
            if (j.empty()) std::printf("  err: %s\n", pron_engine_last_error(e));
        }
        // huge text (5000 words) against real speech
        const auto t0 = Clock::now();
        std::string j = assess(clip, kSr, words_text(5000));
        CHECK(valid_json(j));
        CHECK(has(j, "\"completeness\""));
        std::printf("5000-word reference assessed in %.1f s\n", since(t0));
    }

    // ---- JSON from ASR text: recognized_text always escaped + valid UTF-8 (whisper on noise often emits odd text)
    {
        std::mt19937 rng(11);
        for (int k = 0; k < 3; ++k) {
            std::vector<float> x(16000 * 5);
            for (auto& v : x) v = float(int(rng() % 2001) - 1000) / 4000.0f;
            for (size_t i = 0; i < speech.size() && i < x.size(); ++i) x[i] += speech[i];
            CHECK(valid_json(assess(x, 16000, kRef)));
        }
    }

    // ---- cancel in every stage ----------------------------------------------------------------------------------
    {
        // ~35 s of speech so that each stage takes a while
        std::vector<float> longa;
        const std::string sentence = "Learning to speak a new language takes patience, and a little practice every single day. When you read aloud, try to listen carefully to the sounds that you make.";
        size_t ln = 0; int lsr = 0;
        float* la = pron_engine_tts(e, sentence.c_str(), "us", 1.0f, &ln, &lsr);
        CHECK(la && ln > 0);
        if (la) { longa.assign(la, la + ln); pron_engine_free_audio(la); }
        struct Ctl { pron_engine* e; std::string at; std::vector<std::string> seen; bool fired = false; };
        auto cb = [](void* u, const char* stage, double, double) {
            auto* c = static_cast<Ctl*>(u);
            c->seen.push_back(stage);
            if (!c->fired && c->at == stage) { c->fired = true; pron_engine_cancel(c->e); }  // cancel from inside the callback
        };
        for (const char* stage : {"vad", "asr", "phoneme", "assess"}) {
            if ((std::strcmp(stage, "asr") == 0 && !asr) || (std::strcmp(stage, "phoneme") == 0 && !ph)) continue;
            Ctl c{e, stage, {}};
            char* r = pron_engine_assess_f32_progress(e, longa.data(), longa.size(), lsr, sentence.c_str(), cb, &c);
            const std::string j = take(r);
            const std::string err = pron_engine_last_error(e);
            std::printf("cancel at %-7s -> %s (%s)\n", stage, j.empty() ? "cancelled" : "completed", err.c_str());
            CHECK(c.fired);
            if (std::strcmp(stage, "assess") == 0) CHECK(j.empty() ? err == "cancelled" : valid_json(j));  // too late to cancel: either is fine
            else { CHECK(j.empty()); CHECK(err == "cancelled"); }
            for (const auto& s : c.seen) CHECK(s != "done" || !j.empty());
            // engine fully usable afterwards, flag cleared
            CHECK(valid_json(assess(speech, kSr, kRef)));
        }
        // cancel before the call starts: the flag is reset by the call, so it must complete
        pron_engine_cancel(e);
        CHECK(valid_json(assess(speech, kSr, kRef)));
        // cancel from another thread at assorted delays (never crash, never hang, always consistent outcome)
        for (int delay_ms : {0, 1, 5, 20, 80, 200, 500, 1000}) {
            std::thread th([&] { std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms)); pron_engine_cancel(e); });
            Log lg;
            char* r = pron_engine_assess_f32_progress(e, longa.data(), longa.size(), lsr, sentence.c_str(), on_progress, &lg);
            th.join();
            const std::string j = take(r);
            if (j.empty()) CHECK(std::strcmp(pron_engine_last_error(e), "cancelled") == 0); else CHECK(valid_json(j));
        }
        CHECK(valid_json(assess(speech, kSr, kRef)));
    }

    // ---- threading: app threads call status/estimate/lookup/tts from inside the progress callback; no re-entrancy
    {
        struct Ctl { pron_engine* e; int calls = 0; bool ok = true; bool reentrant_rejected = false; std::string reerr; };
        Ctl c{e};
        auto cb = [](void* u, const char* stage, double, double) {
            auto* c = static_cast<Ctl*>(u);
            if (c->calls++ > 0 && std::strcmp(stage, "done") != 0) return;  // only once, on the first event
            // another app thread (UI handler) while the worker is inside the callback holding the heavy lock
            std::promise<bool> pr;
            auto fut = pr.get_future();
            std::thread th([&] {
                bool ok = true;
                char* s = pron_engine_status(c->e); ok = ok && s && std::strstr(s, "\"asr\"") != nullptr; std::free(s);
                ok = ok && pron_engine_estimate_seconds(c->e, 10.0) > 0.0;
                char* l = pron_engine_lookup(c->e, "weather"); std::free(l);
                size_t n = 0; int sr = 0;
                float* w = pron_engine_tts(c->e, "Hi.", "us", 1.0f, &n, &sr); ok = ok && w && n > 0; pron_engine_free_audio(w);
                pron_engine_set_strictness(c->e, 1);
                ok = ok && pron_engine_set_accent(c->e, "us") == 0;
                pr.set_value(ok);
            });
            if (fut.wait_for(std::chrono::seconds(60)) != std::future_status::ready) {
                std::printf("DEADLOCK: app thread blocked while progress callback runs\n");
                std::fflush(stdout);
                std::_Exit(3);
            }
            if (!fut.get()) c->ok = false;
            th.join();
            // assess from inside the callback (same thread) must be rejected, not deadlock
            std::vector<float> tiny(16000, 0.0f);
            char* r = pron_engine_assess_f32(c->e, tiny.data(), tiny.size(), 16000, "x");
            if (r) { c->reentrant_rejected = false; std::free(r); }
            else { c->reentrant_rejected = true; c->reerr = pron_engine_last_error(c->e); }
        };
        char* r = pron_engine_assess_f32_progress(e, speech.data(), speech.size(), kSr, kRef.c_str(), cb, &c);
        CHECK(valid_json(take(r)));
        CHECK(c.ok);
        CHECK(c.reentrant_rejected);
        CHECK(c.reerr.find("re-entrant") != std::string::npos);
    }
    // two heavy calls from two threads serialize; a third thread hammers the light calls.
    {
        std::atomic<int> okc{0};
        std::atomic<bool> stop{false};
        std::thread light([&] { while (!stop) { std::free(pron_engine_status(e)); (void)pron_engine_estimate_seconds(e, 3.0); std::free(pron_engine_lookup(e, "think")); } });
        std::vector<std::thread> ts;
        for (int k = 0; k < 3; ++k) ts.emplace_back([&] { for (int i = 0; i < 3; ++i) { char* r = pron_engine_assess_f32(e, speech.data(), speech.size(), kSr, kRef.c_str()); if (r && valid_json(r)) ++okc; std::free(r); } });
        for (auto& t : ts) t.join();
        stop = true;
        light.join();
        CHECK(okc.load() == 9);
    }

    // ---- 10 minutes of 48 kHz audio: bounded memory (segmented phoneme model input) --------------------------------
    {
        std::vector<float> sp48 = resample(speech, kSr, 48000);
        std::vector<float> big;
        const size_t total = size_t(48000) * 600;
        big.reserve(total);
        while (big.size() < total) {
            big.insert(big.end(), sp48.begin(), sp48.end());
            big.insert(big.end(), size_t(48000) * 3 / 2, 0.0f);
        }
        big.resize(total);
        const long before = rss_kb(), hwm0 = rss_kb("VmHWM:");
        Log lg;
        const auto t0 = Clock::now();
        char* r = pron_engine_assess_f32_progress(e, big.data(), big.size(), 48000, words_text(1500).c_str(), on_progress, &lg);
        const double sec = since(t0);
        const std::string j = take(r);
        const long hwm = rss_kb("VmHWM:");
        std::printf("10 min @48k: %s in %.1f s, peak RSS growth %ld MB (rss before %ld MB, hwm before %ld MB)\n", j.empty() ? "FAILED" : "ok", sec,
                    (hwm - before) / 1024, before / 1024, hwm0 / 1024);
        CHECK(valid_json(j));
        if (j.empty()) std::printf("  err: %s\n", pron_engine_last_error(e));
        CHECK((hwm - before) / 1024 < 900);  // input is 115 MB; working set must stay a small multiple of it
        double prev = -1; bool mono = true;
        for (const auto& x : lg.ev) { if (x.f < prev - 1e-12 || x.f < 0 || x.f > 1) mono = false; prev = x.f; }
        CHECK(mono);
        // pcm16 variant of the same length
        std::vector<int16_t> p16(big.size());
        for (size_t i = 0; i < big.size(); ++i) p16[i] = int16_t(std::max(-1.0f, std::min(1.0f, big[i])) * 32767.0f);
        big.clear(); big.shrink_to_fit();
        char* r2 = pron_engine_assess_pcm16(e, p16.data(), p16.size(), 48000, kRef.c_str());
        CHECK(valid_json(take(r2)));
    }

    // ---- 100 repeated assess + live sessions: no growth --------------------------------------------------------------
    {
        std::vector<float> clip(speech.begin(), speech.end());
        for (int i = 0; i < 10; ++i) std::free(pron_engine_assess_f32(e, clip.data(), clip.size(), kSr, kRef.c_str()));  // warm-up
        const long r0 = rss_kb();
        for (int i = 0; i < 100; ++i) {
            char* r = pron_engine_assess_f32(e, clip.data(), clip.size(), kSr, kRef.c_str());
            if (!r) { CHECK(false); break; }
            std::free(r);
            if (live) {
                pron_live* lv = pron_live_start(e, kRef.c_str());
                CHECK(lv != nullptr);
                if (lv) {
                    std::vector<int16_t> ch(1600, 0);
                    for (int k = 0; k < 3; ++k) std::free(pron_live_feed_pcm16(lv, ch.data(), ch.size(), 16000));
                    std::free(pron_live_feed_f32(lv, nullptr, 0, 16000));
                    CHECK(pron_live_feed_f32(lv, nullptr, 5, 16000) == nullptr);
                    CHECK(pron_live_feed_f32(lv, ch.size() ? reinterpret_cast<const float*>(ch.data()) : nullptr, 0, 0) == nullptr);
                    pron_live_set_cursor(lv, -3);
                    pron_live_set_cursor(lv, 1 << 30);
                    std::free(pron_live_finish(lv));
                    std::free(pron_live_finish(lv));  // twice
                    std::free(pron_live_feed_pcm16(lv, ch.data(), ch.size(), 16000));  // after finish
                    pron_live_free(lv);
                }
            }
        }
        const long r1 = rss_kb();
        std::printf("100 assess%s sessions: RSS %ld -> %ld MB\n", live ? "+live" : "", r0 / 1024, r1 / 1024);
        CHECK((r1 - r0) / 1024 < 60);
    }
    if (live) {  // live tracker with hostile references / chunks
        for (const std::string r : {std::string(""), std::string("   "), std::string("!!!"), std::string("\xF0\x9F\x98\x80"), std::string("\xD0\x9F\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82"), words_text(5000)}) {
            pron_live* lv = pron_live_start(e, r.c_str());
            CHECK(lv != nullptr);
            if (!lv) continue;
            for (int rate : {8000, 16000, 44100, 48000}) {
                std::vector<float> x = resample(speech, kSr, rate);
                x[0] = std::nanf("");
                for (size_t off = 0; off < x.size(); off += rate / 5) {
                    const size_t len = std::min<size_t>(rate / 5, x.size() - off);
                    const std::string j = take(pron_live_feed_f32(lv, x.data() + off, len, rate));
                    CHECK(valid_json(j));
                }
            }
            CHECK(valid_json(take(pron_live_finish(lv))));
            pron_live_free(lv);
        }
        CHECK(pron_live_start(e, nullptr) == nullptr);
        pron_live_free(nullptr);
    }

    // ---- destroy while idle, after use ----------------------------------------------------------------------------
    pron_engine_destroy(e);
    for (int i = 0; i < 5; ++i) {
        pron_engine* e2 = pron_engine_create(models.c_str(), 1);
        CHECK(e2 != nullptr);
        if (e2 && i % 2) std::free(pron_engine_assess_f32(e2, speech.data(), speech.size(), kSr, kRef.c_str()));
        pron_engine_destroy(e2);
    }
    std::printf(g_fail ? "FAILED (%d)\n" : "OK\n", g_fail);
    return g_fail ? 1 : 0;
}

// Engine tests. argv[1] = test data dir (contains models/ assembled by fetch_models.cmake).
#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstring>
#include <thread>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#include "pron/pron_engine.h"
#include "pron/pron_live.h"
#include "pron/pron_progress.h"

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); ++g_fail; } } while (0)

// Minimal strict JSON validator.
struct J {
    const char* p; const char* e;
    void ws() { while (p < e && (*p == ' ' || *p == '\n' || *p == '\t' || *p == '\r')) ++p; }
    bool lit(const char* s) { size_t n = std::char_traits<char>::length(s); if (size_t(e - p) < n || std::string(p, n) != s) return false; p += n; return true; }
    bool str() {
        if (p >= e || *p != '"') return false; ++p;
        while (p < e && *p != '"') { if (static_cast<unsigned char>(*p) < 0x20) return false; if (*p == '\\') { ++p; if (p >= e) return false; } ++p; }
        if (p >= e) return false; ++p; return true;
    }
    bool num() { const char* s = p; if (p < e && *p == '-') ++p; while (p < e && (std::isdigit((unsigned char)*p) || *p == '.' || *p == 'e' || *p == 'E' || *p == '+' || *p == '-')) ++p; return p > s; }
    bool val() {
        ws(); if (p >= e) return false;
        if (*p == '{') { ++p; ws(); if (p < e && *p == '}') { ++p; return true; }
            for (;;) { ws(); if (!str()) return false; ws(); if (p >= e || *p++ != ':') return false; if (!val()) return false; ws(); if (p < e && *p == ',') { ++p; continue; } if (p < e && *p == '}') { ++p; return true; } return false; } }
        if (*p == '[') { ++p; ws(); if (p < e && *p == ']') { ++p; return true; }
            for (;;) { if (!val()) return false; ws(); if (p < e && *p == ',') { ++p; continue; } if (p < e && *p == ']') { ++p; return true; } return false; } }
        if (*p == '"') return str();
        if (lit("true") || lit("false") || lit("null")) return true;
        return num();
    }
};
static bool valid_json(const std::string& s) { J j{s.data(), s.data() + s.size()}; if (!j.val()) return false; j.ws(); return j.p == j.e; }
static bool has(const std::string& s, const char* sub) { return s.find(sub) != std::string::npos; }

struct ProgLog {
    struct Ev { std::string stage; double f, eta; };
    std::vector<Ev> ev;
    std::atomic<bool> trigger{false};
};
static void on_progress(void* u, const char* stage, double f, double eta) {
    auto* l = static_cast<ProgLog*>(u);
    l->ev.push_back({stage, f, eta});
    l->trigger = true;
}

static std::vector<int16_t> tone(int sr, double sec) {
    std::vector<int16_t> v(size_t(sr * sec));
    for (size_t i = 0; i < v.size(); ++i) v[i] = int16_t(3000 * std::sin(2 * 3.14159265 * 220.0 * double(i) / sr) + (std::rand() % 200 - 100));
    return v;
}

int main(int argc, char** argv) {
    const std::string data = argc > 1 ? argv[1] : ".";
    namespace fs = std::filesystem;

    // 1. Empty models dir.
    const std::string empty = data + "/empty_models";
    fs::create_directories(empty);
    CHECK(pron_engine_create("/definitely/not/a/dir", 0) == nullptr);
    pron_engine* e = pron_engine_create(empty.c_str(), 0);
    CHECK(e != nullptr);
    if (!e) return 1;
    char* st = pron_engine_status(e);
    CHECK(st && valid_json(st));
    std::printf("status(empty): %s\n", st ? st : "(null)");
    CHECK(st && has(st, "\"asr\":false") && has(st, "\"vad\":false") && has(st, "\"phoneme\":false") && has(st, "\"errors\":{\"asr\""));
    pron_free_string(st);

    auto pcm = tone(44100, 1.0);
    char* r = pron_engine_assess_pcm16(e, pcm.data(), pcm.size(), 44100, "Think about the weather.");
    CHECK(r && valid_json(r));
    if (r) {
        std::printf("assess(empty): %.300s\n", r);
        CHECK(has(r, "\"recognized_text\":\"\"") && has(r, "\"speech_segments\":[]") && has(r, "\"timings_ms\""));
    } else std::printf("err: %s\n", pron_engine_last_error(e));
    pron_free_string(r);
    size_t n = 0; int sr = 0;
    CHECK(pron_engine_tts(e, "hello", "us", 1.0f, &n, &sr) == nullptr);
    CHECK(pron_engine_assess_f32(e, nullptr, 0, 16000, "hello") != nullptr);  // empty audio is valid
    pron_engine_destroy(e);

    // 2. With models.
    const std::string models = data + "/models";
    e = pron_engine_create(models.c_str(), 2);
    CHECK(e != nullptr);
    if (!e) return 1;
    st = pron_engine_status(e);
    CHECK(st && valid_json(st));
    std::printf("status: %s\n", st ? st : "(null)");
    CHECK(std::fabs(pron_engine_estimate_seconds(e, 10.0) - 6.0) < 1e-6);  // default ratio 0.6 before the first call
    const bool vad = st && has(st, "\"vad\":true"), tts = st && has(st, "\"tts\":{\"us\":true");
    const bool asr = st && has(st, "\"asr\":true");
    pron_free_string(st);

    if (!vad || !tts) { std::printf("SKIP: silero/voice not available\n"); pron_engine_destroy(e); return g_fail ? 1 : 77; }

    float* a = pron_engine_tts(e, "Think about the weather.", "us", 1.0f, &n, &sr);
    CHECK(a && n > 8000 && sr > 0);
    { size_t n2 = 0; int sr2 = 0; CHECK(pron_engine_tts(e, "x", "gb", 1.0f, &n2, &sr2) == nullptr); }
    if (a) {
        std::printf("tts: %zu samples @ %d Hz\n", n, sr);
        // 0.7 s silence + speech + 0.7 s silence, to check trimming and original-time offsets.
        const size_t lead = size_t(0.7 * sr);
        std::vector<float> rec(lead + n + lead, 0.0f);
        for (size_t i = 0; i < n; ++i) rec[lead + i] = a[i];
        char* j = pron_engine_assess_f32(e, rec.data(), rec.size(), sr, "Think about the weather.");
        CHECK(j && valid_json(j));
        if (j) {
            std::printf("assess: %.400s\n", j);
            CHECK(has(j, "\"speech_segments\":[["));
            CHECK(!has(j, "\"speech_segments\":[]"));
        } else std::printf("err: %s\n", pron_engine_last_error(e));
        pron_free_string(j);
        // Time lag regression: 1.5 s of leading silence + 44.1 kHz input. Word starts must be the unpadded
        // word starts + 1.5 s (within 0.15 s), i.e. relative to the ORIGINAL submitted audio.
        {
            const char* kRef = "Think about the weather.";
            auto word_starts = [&](const char* js) {
                std::vector<double> v;
                const char* p = std::strstr(js, "\"words\":[");
                while (p && (p = std::strstr(p, "\"status\":\"")) != nullptr) {
                    const char* q = std::strstr(p, "\"start\":");
                    const char* nx = std::strstr(p + 8, "\"status\":\"");
                    if (q && (!nx || q < nx) && q[8] != 'n') v.push_back(std::atof(q + 8)); else v.push_back(-1.0);
                    p += 8;
                }
                return v;
            };
            char* j0 = pron_engine_assess_f32(e, a, n, sr, kRef);
            const size_t lead2 = size_t(1.5 * sr);
            std::vector<float> pad(lead2 + n, 0.0f);
            for (size_t i = 0; i < n; ++i) pad[lead2 + i] = a[i];
            // resample the padded signal to 44.1 kHz (linear)
            const int sr2 = 44100;
            std::vector<float> up(size_t(double(pad.size()) * sr2 / sr));
            for (size_t i = 0; i < up.size(); ++i) {
                const double pos = double(i) * sr / sr2; const size_t i0 = size_t(pos);
                const size_t i1 = std::min(i0 + 1, pad.size() - 1); const float f = float(pos - double(i0));
                up[i] = pad[std::min(i0, pad.size() - 1)] * (1 - f) + pad[i1] * f;
            }
            char* j1 = pron_engine_assess_f32(e, up.data(), up.size(), sr2, kRef);
            CHECK(j0 && j1);
            if (j0 && j1) {
                auto s0 = word_starts(j0), s1 = word_starts(j1);
                CHECK(s0.size() == s1.size());
                int compared = 0;
                for (size_t i = 0; i < s0.size() && i < s1.size(); ++i) {
                    if (s0[i] < 0 || s1[i] < 0) continue;
                    ++compared;
                    std::printf("word %zu: plain %.3f padded %.3f\n", i, s0[i], s1[i]);
                    CHECK(std::fabs(s1[i] - (s0[i] + 1.5)) < 0.15);
                }
                std::printf("timing-offset words compared: %d\n", compared);
                // Speech segments (always present) must also be on the original timeline.
                const char* g0 = std::strstr(j0, "\"speech_segments\":[[");
                const char* g1 = std::strstr(j1, "\"speech_segments\":[[");
                CHECK(g0 && g1);
                if (g0 && g1) {
                    const double a0 = std::atof(g0 + 20), a1 = std::atof(g1 + 20);
                    std::printf("first speech start: plain %.3f padded %.3f\n", a0, a1);
                    CHECK(std::fabs(a1 - (a0 + 1.5)) < 0.15);
                }
            }
            pron_free_string(j0);
            pron_free_string(j1);
        }
        pron_engine_free_audio(a);
    }
    char* lk = pron_engine_lookup(e, "think");
    CHECK(lk && valid_json(lk));
    pron_free_string(lk);
    std::printf("asr loaded (tiny test model, no-crash only): %d\n", asr);
    // 3. Progress, cancel, long input (progress API).
    {
        const char* kPara =
            "Learning to speak a new language takes patience, and a little practice every single day. "
            "When you read aloud, try to listen carefully to the sounds that you make, and compare them with the sounds "
            "that native speakers produce. Think about the weather, and talk about the three brothers who were walking "
            "very fast along the road to the village. They stopped near the old bridge, because the river was high, "
            "and the water was moving much faster than anyone had expected after such a quiet and sunny morning. "
            "In the end they decided to wait, to drink some tea, and to tell each other a few stories about their "
            "childhood, while the clouds slowly drifted away across the wide blue sky above the hills.";
        size_t ln = 0; int lsr = 0;
        float* la = pron_engine_tts(e, kPara, "us", 1.0f, &ln, &lsr);
        CHECK(la && ln > 0);
        if (la) {
            const double secs = double(ln) / lsr;
            std::printf("long tts: %.1f s\n", secs);
            CHECK(secs > 30.0);
            ProgLog log;
            char* j = pron_engine_assess_f32_progress(e, la, ln, lsr, kPara, on_progress, &log);
            CHECK(j && valid_json(j));
            if (!j) std::printf("err: %s\n", pron_engine_last_error(e));
            if (j) {
                CHECK(has(j, "\"recognized\":"));
                const char* ps = std::strstr(j, "\"phoneme_segments\":");
                const int nseg = ps ? std::atoi(ps + 19) : 0;
                std::printf("phoneme segments: %d\n", nseg);
                CHECK(nseg >= 3);
                pron_free_string(j);
            }
            CHECK(log.ev.size() >= 3);
            double prev = -1;
            bool mono = true;
            for (const auto& x : log.ev) {
                std::printf("  %-8s %.3f eta %.2f\n", x.stage.c_str(), x.f, x.eta);
                if (x.f < prev - 1e-12 || x.f < 0 || x.f > 1) mono = false;
                prev = x.f;
            }
            CHECK(mono);
            CHECK(!log.ev.empty() && log.ev.front().stage == "vad");
            CHECK(!log.ev.empty() && log.ev.back().stage == "done" && log.ev.back().f == 1.0);
            for (size_t i = 0; i + 1 < log.ev.size(); ++i) CHECK(log.ev[i].stage != "done" && log.ev[i].f < 1.0);
            const double est1 = pron_engine_estimate_seconds(e, 10.0);
            std::printf("estimate after a call for 10 s: %.2f s\n", est1);
            CHECK(est1 > 0.0);

            // Concurrency: heavy assess on this thread while another thread does TTS, lookup, status, estimate (+ live if present).
            {
                std::atomic<int> tts_ok{0}, look_ok{0}, live_ok{0}, started{0};
                std::atomic<bool> stop{false};
                const bool live_here = [&] { char* s = pron_engine_status(e); bool v = s && has(s, "\"live\":true"); pron_free_string(s); return v; }();
                std::thread other([&] {
                    started = 1;
                    while (!stop.load()) {
                        size_t n2 = 0; int sr2 = 0;
                        float* w = pron_engine_tts(e, "Think about the weather.", "us", 1.0f, &n2, &sr2);
                        if (w && n2 > 1000) ++tts_ok;
                        pron_engine_free_audio(w);
                        for (int k = 0; k < 20; ++k) {
                            char* l = pron_engine_lookup(e, "weather");
                            if (l && has(l, "\"ipa\"")) ++look_ok;
                            pron_free_string(l);
                        }
                        char* s = pron_engine_status(e); pron_free_string(s);
                        (void)pron_engine_estimate_seconds(e, 5.0);
                        if (live_here) {
                            pron_live* lv = pron_live_start(e, "Think about the weather.");
                            if (lv) {
                                std::vector<int16_t> ch(2560, 0);
                                for (int k = 0; k < 5; ++k) { char* r = pron_live_feed_pcm16(lv, ch.data(), ch.size(), 16000); pron_free_string(r); }
                                char* r = pron_live_finish(lv);
                                if (r) ++live_ok;
                                pron_free_string(r);
                                pron_live_free(lv);
                            }
                        }
                    }
                });
                while (!started.load()) std::this_thread::yield();
                ProgLog clog2;
                char* cj2 = pron_engine_assess_f32_progress(e, la, ln, lsr, kPara, on_progress, &clog2);
                stop = true;
                other.join();
                std::printf("concurrent: assess %s, tts %d, lookup %d, live %d (live model %s)\n", cj2 ? "ok" : "FAILED",
                            tts_ok.load(), look_ok.load(), live_ok.load(), live_here ? "present" : "absent");
                CHECK(cj2 != nullptr);
                CHECK(tts_ok.load() >= 1 && look_ok.load() >= 1);
                if (live_here) CHECK(live_ok.load() >= 1);
                pron_free_string(cj2);
            }

            // Cancel from another thread, shortly after the call started.
            ProgLog clog;
            std::thread th([&] {
                while (!clog.trigger.load()) std::this_thread::yield();
                pron_engine_cancel(e);
            });
            char* cj = pron_engine_assess_f32_progress(e, la, ln, lsr, kPara, on_progress, &clog);
            th.join();
            CHECK(cj == nullptr);
            CHECK(std::strcmp(pron_engine_last_error(e), "cancelled") == 0);
            for (const auto& x : clog.ev) CHECK(x.stage != "done");
            pron_free_string(cj);

            // The flag is cleared by the next call; thin wrappers (no callback) still work.
            char* again = pron_engine_assess_f32(e, la, ln, lsr, kPara);
            CHECK(again != nullptr);
            pron_free_string(again);
            pron_engine_free_audio(la);
        }
    }

    pron_engine_destroy(e);
    std::printf(g_fail ? "FAILED\n" : "OK\n");
    return g_fail ? 1 : 0;
}

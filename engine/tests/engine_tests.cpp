// Engine tests. argv[1] = test data dir (contains models/ assembled by fetch_models.cmake).
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#include "pron/pron_engine.h"

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
        pron_engine_free_audio(a);
    }
    char* lk = pron_engine_lookup(e, "think");
    CHECK(lk && valid_json(lk));
    pron_free_string(lk);
    std::printf("asr loaded (tiny test model, no-crash only): %d\n", asr);
    pron_engine_destroy(e);
    std::printf(g_fail ? "FAILED\n" : "OK\n");
    return g_fail ? 1 : 0;
}

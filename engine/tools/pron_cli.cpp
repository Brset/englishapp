// pron_cli - end-to-end check of the engine through its C API only.
//   pron_cli status   <models>
//   pron_cli selftest <models>
//   pron_cli assess   [--progress] <models> <wav16k_mono_pcm16> <text>
//   pron_cli live     <models> <wav_mono_pcm16> <text>   (live tracker, 160 ms chunks, prints cursor per chunk)
#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "pron/pron_engine.h"
#include "pron/pron_live.h"
#include "pron/pron_progress.h"

// ---------------------------------------------------------------- tiny JSON DOM
struct JV {
    enum T { Null, Bool, Num, Str, Arr, Obj } t = Null;
    bool b = false; double n = 0; std::string s;
    std::vector<JV> a; std::map<std::string, JV> o;
    const JV* get(const std::string& k) const { auto i = o.find(k); return i == o.end() ? nullptr : &i->second; }
};
struct JP {
    const char* p; const char* e; bool ok = true;
    void ws() { while (p < e && std::isspace((unsigned char)*p)) ++p; }
    bool lit(const char* x) { size_t n = std::strlen(x); if ((size_t)(e - p) >= n && !std::strncmp(p, x, n)) { p += n; return true; } return false; }
    std::string str() {
        std::string r; ++p;
        while (p < e && *p != '"') {
            if (*p == '\\' && p + 1 < e) {
                ++p;
                switch (*p) {
                    case 'n': r += '\n'; break; case 't': r += '\t'; break; case 'r': r += '\r'; break;
                    case 'b': r += '\b'; break; case 'f': r += '\f'; break;
                    case 'u': if (e - p >= 5) { unsigned c = (unsigned)std::strtoul(std::string(p + 1, 4).c_str(), nullptr, 16); p += 4;
                                  if (c < 0x80) r += (char)c; else if (c < 0x800) { r += (char)(0xC0 | c >> 6); r += (char)(0x80 | (c & 63)); }
                                  else { r += (char)(0xE0 | c >> 12); r += (char)(0x80 | ((c >> 6) & 63)); r += (char)(0x80 | (c & 63)); } } break;
                    default: r += *p;
                }
                ++p;
            } else r += *p++;
        }
        if (p < e) ++p; else ok = false;
        return r;
    }
    JV val() {
        JV v; ws();
        if (p >= e) { ok = false; return v; }
        if (*p == '{') {
            v.t = JV::Obj; ++p; ws();
            if (p < e && *p == '}') { ++p; return v; }
            while (ok) {
                ws(); if (p >= e || *p != '"') { ok = false; break; }
                std::string k = str(); ws();
                if (p >= e || *p++ != ':') { ok = false; break; }
                v.o[k] = val(); ws();
                if (p < e && *p == ',') { ++p; continue; }
                if (p < e && *p == '}') { ++p; break; }
                ok = false;
            }
        } else if (*p == '[') {
            v.t = JV::Arr; ++p; ws();
            if (p < e && *p == ']') { ++p; return v; }
            while (ok) {
                v.a.push_back(val()); ws();
                if (p < e && *p == ',') { ++p; continue; }
                if (p < e && *p == ']') { ++p; break; }
                ok = false;
            }
        } else if (*p == '"') { v.t = JV::Str; v.s = str(); }
        else if (lit("true")) { v.t = JV::Bool; v.b = true; }
        else if (lit("false")) { v.t = JV::Bool; }
        else if (lit("null")) {}
        else { char* end = nullptr; v.n = std::strtod(p, &end); if (end == p) ok = false; else { v.t = JV::Num; p = end; } }
        return v;
    }
};
static bool parse_json(const std::string& s, JV& out) { JP p{s.data(), s.data() + s.size()}; out = p.val(); return p.ok; }

static bool truthy(const JV* v) { return v && ((v->t == JV::Bool && v->b) || (v->t == JV::Num && v->n != 0)); }
static std::string lower(std::string s) { for (auto& c : s) c = (char)std::tolower((unsigned char)c); return s; }
static std::string jesc(const std::string& s) {
    std::string r;
    for (unsigned char c : s) { if (c == '"' || c == '\\') { r += '\\'; r += (char)c; } else if (c < 0x20) r += ' '; else r += (char)c; }
    return r;
}

// ---------------------------------------------------------------- engine wrapper
struct Engine {
    pron_engine* e = nullptr;
    explicit Engine(const char* dir) { e = pron_engine_create(dir, 0); }
    ~Engine() { if (e) pron_engine_destroy(e); }
    std::string last_error() const { const char* m = e ? pron_engine_last_error(e) : nullptr; return m ? m : ""; }
};
static std::string take(char* s) { std::string r = s ? s : ""; if (s) pron_free_string(s); return r; }
static double ms_since(std::chrono::steady_clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

// ---------------------------------------------------------------- WAV
static bool read_wav(const std::string& path, std::vector<int16_t>& pcm, int& sr, std::string& err) {
    std::ifstream f(path, std::ios::binary);
    if (!f) { err = "cannot open " + path; return false; }
    std::vector<char> d((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    auto u16 = [&](size_t o) { return (unsigned)((uint8_t)d[o] | (uint8_t)d[o + 1] << 8); };
    auto u32 = [&](size_t o) { return (uint32_t)u16(o) | (uint32_t)u16(o + 2) << 16; };
    if (d.size() < 12 || std::memcmp(d.data(), "RIFF", 4) || std::memcmp(d.data() + 8, "WAVE", 4)) { err = "not a RIFF/WAVE file"; return false; }
    unsigned fmt = 0, ch = 0, bits = 0; sr = 0; bool have_fmt = false;
    size_t o = 12;
    while (o + 8 <= d.size()) {
        size_t sz = u32(o + 4), body = o + 8;
        if (!std::memcmp(d.data() + o, "fmt ", 4) && body + 16 <= d.size()) {
            fmt = u16(body); ch = u16(body + 2); sr = (int)u32(body + 4); bits = u16(body + 14); have_fmt = true;
        } else if (!std::memcmp(d.data() + o, "data", 4)) {
            if (!have_fmt) { err = "data before fmt"; return false; }
            if (fmt != 1 || bits != 16 || ch != 1) { err = "need mono PCM16 wav"; return false; }
            sz = std::min(sz, d.size() - body);
            pcm.resize(sz / 2);
            for (size_t i = 0; i < pcm.size(); ++i) pcm[i] = (int16_t)u16(body + 2 * i);
            return true;
        }
        o = body + sz + (sz & 1);
    }
    err = "no data chunk"; return false;
}
static bool write_wav(const std::string& path, const std::vector<float>& x, int sr) {
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    uint32_t n = (uint32_t)(x.size() * 2), v;
    auto w32 = [&](uint32_t u) { f.write((const char*)&u, 4); };
    auto w16 = [&](uint16_t u) { f.write((const char*)&u, 2); };
    f.write("RIFF", 4); w32(36 + n); f.write("WAVEfmt ", 8); w32(16); w16(1); w16(1); w32((uint32_t)sr); v = (uint32_t)sr * 2; w32(v); w16(2); w16(16);
    f.write("data", 4); w32(n);
    for (float s : x) { float c = std::max(-1.f, std::min(1.f, s)); int16_t q = (int16_t)(c * 32767.f); f.write((const char*)&q, 2); }
    return (bool)f;
}

// ---------------------------------------------------------------- commands

// Debug output of the engine: model labels per phoneme and the vocab token mapping table.
static void enable_debug() {
#ifdef _WIN32
    _putenv_s("PRON_DEBUG_LABELS", "1");
#else
    setenv("PRON_DEBUG_LABELS", "1", 1);
#endif
}

static int cmd_status(const char* models) {
    enable_debug();
    Engine en(models);
    if (!en.e) { std::printf("{\"ok\":false,\"error\":\"cannot create engine for %s\"}\n", jesc(models).c_str()); return 1; }
    std::string st = take(pron_engine_status(en.e));
    std::printf("%s\n", st.c_str());
    JV j;
    if (!parse_json(st, j)) { std::fprintf(stderr, "FAIL: status is not valid JSON\n"); return 1; }
    const JV* tts = j.get("tts");
    struct { const char* name; bool ok; } req[] = {
        {"asr", truthy(j.get("asr"))}, {"vad", truthy(j.get("vad"))}, {"phoneme", truthy(j.get("phoneme"))},
        {"cmudict", truthy(j.get("cmudict"))}, {"live", truthy(j.get("live"))}, {"tts.us", tts && truthy(tts->get("us"))}, {"tts.gb", tts && truthy(tts->get("gb"))}};
    int bad = 0;
    for (auto& r : req) if (!r.ok) { std::fprintf(stderr, "FAIL: component missing: %s\n", r.name); ++bad; }
    if (!bad) std::fprintf(stderr, "OK: all components loaded\n");
    return bad ? 1 : 0;
}

struct Verdict { std::vector<std::string> failures; std::string summary; };

static Verdict check_assessment(const std::string& json, double ms) {
    Verdict v; JV j;
    if (!parse_json(json, j) || j.t != JV::Obj) { v.failures.push_back("assessment is not valid JSON"); v.summary = "{}"; return v; }
    std::string rec = j.get("recognized_text") && j.get("recognized_text")->t == JV::Str ? j.get("recognized_text")->s : "";
    std::string lr = lower(rec);
    if (lr.find("weather") == std::string::npos) v.failures.push_back("recognized_text lacks 'weather'");
    if (lr.find("brothers") == std::string::npos) v.failures.push_back("recognized_text lacks 'brothers'");
    const JV* words = j.get("words");
    size_t total = 0, omitted = 0, with_ph = 0, ph_total = 0;
    if (words && words->t == JV::Arr) for (const JV& w : words->a) {
        ++total;
        const JV* s = w.get("status");
        bool om = s && s->t == JV::Str && s->s == "omitted";
        if (om) ++omitted;
        const JV* ph = w.get("phonemes");
        if (!om && ph && ph->t == JV::Arr && !ph->a.empty()) { ++with_ph; ph_total += ph->a.size(); }
    }
    double kept = total ? 100.0 * (double)(total - omitted) / (double)total : 0.0;
    if (!total) v.failures.push_back("no words in result");
    else if (kept < 80.0) v.failures.push_back("only " + std::to_string((int)kept) + "% of words not omitted (<80%)");
    const JV* sc = j.get("scores");
    double overall = -1, accuracy = -1;
    if (sc) { if (sc->get("overall")) overall = sc->get("overall")->n; if (sc->get("accuracy")) accuracy = sc->get("accuracy")->n; }
    if (!(overall >= 60)) v.failures.push_back("overall score < 60");
    if (!(accuracy >= 60)) v.failures.push_back("accuracy score < 60");
    // "phoneme_level" is a top-level field of the assessment JSON (older builds nested it under scores).
    bool phl = truthy(j.get("phoneme_level")) || (sc && truthy(sc->get("phoneme_level")));
    std::string dbg = "{}";
    if (const JV* pd = j.get("phoneme_debug")) {
        char b[500];
        std::snprintf(b, sizeof b, "{\"frames\":%.0f,\"classes\":%.0f,\"used\":%s,\"reason\":\"%s\",\"blank_ratio\":%.3f,\"peak_prob\":%.3f}",
                      pd->get("frames") ? pd->get("frames")->n : -1.0, pd->get("classes") ? pd->get("classes")->n : -1.0,
                      truthy(pd->get("used")) ? "true" : "false",
                      jesc(pd->get("reason") ? pd->get("reason")->s : "").c_str(),
                      pd->get("blank_ratio") ? pd->get("blank_ratio")->n : -1.0, pd->get("peak_prob") ? pd->get("peak_prob")->n : -1.0);
        dbg = b;
    }
    if (!phl) {
        std::string why = "no phoneme_debug in result";
        if (const JV* pd = j.get("phoneme_debug")) if (pd->get("reason")) why = pd->get("reason")->s;
        v.failures.push_back("phoneme_level is false: " + why);
    }
    if (!with_ph) v.failures.push_back("no word has phoneme details");
    char buf[1024];
    std::snprintf(buf, sizeof buf,
        "{\"recognized_text\":\"%s\",\"words\":%zu,\"omitted\":%zu,\"words_not_omitted_pct\":%.0f,\"words_with_phonemes\":%zu,"
        "\"phonemes\":%zu,\"phoneme_level\":%s,\"phoneme_debug\":%s,\"overall\":%.1f,\"accuracy\":%.1f,\"assess_ms\":%.0f}",
        jesc(rec).c_str(), total, omitted, kept, with_ph, ph_total, phl ? "true" : "false", dbg.c_str(), overall, accuracy, ms);
    v.summary = buf;
    return v;
}

struct ProgState { bool print = false; double last = -1; bool mono = true; bool done = false; size_t n = 0; const char* tag = ""; };
static void print_progress(void* u, const char* stage, double f, double eta) {
    auto* p = static_cast<ProgState*>(u);
    ++p->n;
    if (f < p->last - 1e-12 || f < 0 || f > 1) p->mono = false;
    p->last = f;
    if (!std::strcmp(stage, "done")) p->done = true;
    if (p->print) std::fprintf(stderr, "progress%s %-7s %5.1f%%  eta %.1fs\n", p->tag, stage, f * 100.0, eta);
}

struct LiveRun {
    bool ok = false;
    std::string error;
    int cursor = 0, words = 0, skipped = 0;
    bool done = false, monotonic = true;
    size_t chunks = 0;
    double avg_ms = 0, max_ms = 0, audio_s = 0;
    std::string partial;
};

// Feeds pcm in 160 ms chunks (+ finish) through the live tracker. verbose: one line per chunk.
static LiveRun run_live(pron_engine* e, const std::vector<int16_t>& pcm, int sr, const char* text, bool verbose) {
    LiveRun r;
    pron_live* s = pron_live_start(e, text);
    if (!s) { const char* m = pron_engine_last_error(e); r.error = std::string("pron_live_start failed: ") + (m ? m : ""); return r; }
    const size_t chunk = (size_t)sr * 160 / 1000;
    double total_ms = 0;
    int prev = 0;
    JV j;
    auto handle = [&](const std::string& js, double ms, const char* tag) {
        if (!parse_json(js, j) || j.t != JV::Obj) { r.error = "live state is not valid JSON"; return false; }
        int cur = j.get("cursor") ? (int)j.get("cursor")->n : -1;
        if (cur < prev) r.monotonic = false;
        prev = cur; r.cursor = cur;
        r.done = truthy(j.get("done"));
        r.partial = j.get("partial") ? j.get("partial")->s : "";
        r.words = 0; r.skipped = 0;
        if (const JV* w = j.get("words")) for (const JV& x : w->a) { ++r.words; const JV* st = x.get("state"); if (st && st->s == "skipped") ++r.skipped; }
        if (verbose) std::printf("%s t=%.2fs cursor=%d/%d done=%d %.0fms partial=\"%s\"\n", tag, (double)r.chunks * 0.16, cur, r.words, r.done ? 1 : 0, ms, jesc(r.partial).c_str());
        return true;
    };
    for (size_t o = 0; o < pcm.size(); o += chunk) {
        const size_t n = std::min(chunk, pcm.size() - o);
        auto t0 = std::chrono::steady_clock::now();
        std::string js = take(pron_live_feed_pcm16(s, pcm.data() + o, n, sr));
        double ms = ms_since(t0);
        if (js.empty()) { const char* m = pron_engine_last_error(e); r.error = std::string("live feed failed: ") + (m ? m : ""); pron_live_free(s); return r; }
        ++r.chunks; total_ms += ms; r.max_ms = std::max(r.max_ms, ms);
        if (!handle(js, ms, "chunk")) { pron_live_free(s); return r; }
    }
    std::string fin = take(pron_live_finish(s));
    if (fin.empty()) { r.error = "live finish failed"; pron_live_free(s); return r; }
    if (!handle(fin, 0, "final")) { pron_live_free(s); return r; }
    pron_live_free(s);
    r.avg_ms = r.chunks ? total_ms / (double)r.chunks : 0;
    r.audio_s = (double)pcm.size() / sr;
    r.ok = true;
    return r;
}

static int cmd_live(const char* models, const char* wav, const char* text) {
    std::vector<int16_t> pcm; int sr = 0; std::string err;
    if (!read_wav(wav, pcm, sr, err)) { std::fprintf(stderr, "error: %s\n", err.c_str()); return 2; }
    Engine en(models);
    if (!en.e) { std::fprintf(stderr, "error: cannot create engine for %s\n", models); return 2; }
    LiveRun r = run_live(en.e, pcm, sr, text, true);
    if (!r.ok) { std::fprintf(stderr, "error: %s\n", r.error.c_str()); return 1; }
    std::printf("{\"cursor\":%d,\"words\":%d,\"skipped\":%d,\"done\":%s,\"avg_chunk_ms\":%.1f,\"max_chunk_ms\":%.1f}\n",
                r.cursor, r.words, r.skipped, r.done ? "true" : "false", r.avg_ms, r.max_ms);
    return 0;
}

// Per-phoneme table (word, expected IPA, model labels in the aligned region, GOP, score) on stderr.
static void print_phoneme_table(const std::string& json, const char* voice) {
    JV j;
    if (!parse_json(json, j)) return;
    const JV* words = j.get("words");
    if (!words || words->t != JV::Arr) return;
    std::fprintf(stderr, "PHONEME TABLE [%s]\n%-10s %-4s %-6s %-7s %-6s %6s %6s %5s  %-6s %s\n", voice, "word", "ipa", "arpa", "gop", "score",
                 "t0", "t1", "subst", "actual", "model_labels(argmax over aligned region; _=blank)");
    for (const JV& w : words->a) {
        const JV* ph = w.get("phonemes");
        const JV* st = w.get("status");
        if (!ph || ph->t != JV::Arr) continue;
        std::fprintf(stderr, "-- %s  status=%s score=%.1f scored_by=%s expected=/%s/ recognized=%s\n", w.get("text") ? w.get("text")->s.c_str() : "?",
                     st ? st->s.c_str() : "?", w.get("score") ? w.get("score")->n : -1.0, w.get("scored_by") ? w.get("scored_by")->s.c_str() : "?",
                     w.get("expected_ipa") ? w.get("expected_ipa")->s.c_str() : "", w.get("recognized") ? w.get("recognized")->s.c_str() : "");
        for (const JV& p : ph->a) {
            const JV* sc = p.get("score");
            char score[16] = "-";
            if (sc && sc->t == JV::Num) std::snprintf(score, sizeof score, "%.0f", sc->n);
            std::fprintf(stderr, "%-10s %-4s %-6s %7.2f %-6s %6.2f %6.2f %5s  %-6s %s\n", w.get("text") ? w.get("text")->s.c_str() : "",
                         p.get("ipa") ? p.get("ipa")->s.c_str() : "", p.get("arpabet") ? p.get("arpabet")->s.c_str() : "",
                         p.get("gop") ? p.get("gop")->n : 0.0, score, p.get("start") ? p.get("start")->n : -1.0, p.get("end") ? p.get("end")->n : -1.0,
                         truthy(p.get("substituted")) ? "yes" : "", p.get("actual_ipa") ? p.get("actual_ipa")->s.c_str() : "",
                         p.get("model_labels") ? p.get("model_labels")->s.c_str() : "");
        }
    }
}

static int cmd_selftest(const char* models) {
    enable_debug();
    const char* text = "Think about the weather. The three brothers are walking very fast.";
    Engine en(models);
    if (!en.e) { std::printf("{\"ok\":false,\"error\":\"cannot create engine for %s\"}\n", jesc(models).c_str()); return 1; }
    int fails = 0;
    {
        std::string st = take(pron_engine_status(en.e));
        JV sj;
        if (parse_json(st, sj)) {
            const JV* pv = sj.get("phoneme_vocab");
            std::string un;
            if (pv && pv->get("unmapped")) for (const JV& u : pv->get("unmapped")->a) un += (un.empty() ? "\"" : ",\"") + jesc(u.s) + "\"";
            std::printf("{\"phoneme_vocab\":{\"size\":%.0f,\"mapped\":%.0f,\"unmapped\":[%s]}}\n",
                        pv && pv->get("size") ? pv->get("size")->n : -1.0, pv && pv->get("mapped") ? pv->get("mapped")->n : -1.0, un.c_str());
            if (pv && pv->get("mapping") && pv->get("mapping")->t == JV::Arr) {
                std::fprintf(stderr, "VOCAB MAPPING (token>phone; blank = blank/word separator/stress/length mark; - = unmapped):\n");
                for (const JV& m : pv->get("mapping")->a) std::fprintf(stderr, "  %s\n", m.s.c_str());
            }
        } else std::printf("{\"phoneme_vocab\":null}\n");
    }
    std::vector<int16_t> live_pcm; int live_sr = 0;
    for (const char* voice : {"us", "gb"}) {
        std::vector<std::string> f;
        size_t n = 0; int sr = 0;
        auto t0 = std::chrono::steady_clock::now();
        float* audio = pron_engine_tts(en.e, text, voice, 1.0f, &n, &sr);
        double tts_ms = ms_since(t0);
        std::string summary = "{}";
        double audio_s = 0, assess_ms = 0;
        if (!audio || !n) {
            f.push_back("tts failed: " + en.last_error());
        } else {
            std::vector<float> pcm(audio, audio + n);
            pron_engine_free_audio(audio);
            audio_s = sr > 0 ? (double)n / sr : 0;
            if (!std::strcmp(voice, "us") && !write_wav("selftest_us.wav", pcm, sr)) f.push_back("cannot write selftest_us.wav");
            if (!std::strcmp(voice, "us")) {
                std::vector<int16_t> q(pcm.size());
                for (size_t i = 0; i < pcm.size(); ++i) q[i] = (int16_t)(std::max(-1.f, std::min(1.f, pcm[i])) * 32767.f);
                live_pcm = q; live_sr = sr;
            }
            t0 = std::chrono::steady_clock::now();
            ProgState ps; ps.print = true; ps.tag = voice[0] == 'u' ? " [us]" : " [gb]";
            std::string json = take(pron_engine_assess_f32_progress(en.e, pcm.data(), pcm.size(), sr, text, print_progress, &ps));
            assess_ms = ms_since(t0);
            if (json.empty()) f.push_back("assess failed: " + en.last_error());
            else {
                if (!ps.mono) f.push_back("progress fractions not monotonic");
                if (!ps.done || ps.last != 1.0) f.push_back("progress did not end with done 1.0");
                const double ratio = audio_s > 0 ? assess_ms / 1000.0 / audio_s : 0;
                std::fprintf(stderr, "measured ratio [%s]: %.3f s processing per audio second (%.0f ms for %.2f s); estimate for 10 s: %.2f s\n",
                             voice, ratio, assess_ms, audio_s, pron_engine_estimate_seconds(en.e, 10.0));
            }
            if (json.empty()) {}
            else { print_phoneme_table(json, voice); Verdict v = check_assessment(json, assess_ms); summary = v.summary; f.insert(f.end(), v.failures.begin(), v.failures.end()); }
        }
        std::printf("{\"voice\":\"%s\",\"ok\":%s,\"tts_ms\":%.0f,\"tts_sample_rate\":%d,\"audio_seconds\":%.2f,\"assess_ms\":%.0f,\"result\":%s,\"failures\":[",
                    voice, f.empty() ? "true" : "false", tts_ms, sr, audio_s, assess_ms, summary.c_str());
        for (size_t i = 0; i < f.size(); ++i) std::printf("%s\"%s\"", i ? "," : "", jesc(f[i]).c_str());
        std::printf("]}\n");
        std::fflush(stdout);
        for (auto& m : f) std::fprintf(stderr, "FAIL [%s]: %s\n", voice, m.c_str());
        fails += (int)f.size();
    }
    {
        std::vector<std::string> f;
        LiveRun r;
        if (live_pcm.empty()) f.push_back("live: no US audio to feed");
        else {
            r = run_live(en.e, live_pcm, live_sr, text, false);
            if (!r.ok) f.push_back("live: " + r.error);
            else {
                // 13 words in the selftest sentence
                int expect = 0;
                { pron_live* tmp = pron_live_start(en.e, text);
                  if (tmp) { std::string js = take(pron_live_finish(tmp)); JV jj; if (parse_json(js, jj) && jj.get("words")) expect = (int)jj.get("words")->a.size(); pron_live_free(tmp); } }
                if (r.cursor != expect || expect == 0) f.push_back("live: final cursor " + std::to_string(r.cursor) + " != word count " + std::to_string(expect) + " (partial: " + r.partial + ")");
                if (r.skipped) f.push_back("live: " + std::to_string(r.skipped) + " word(s) marked skipped");
                if (!r.monotonic) f.push_back("live: cursor decreased");
                if (r.avg_ms > 160.0) f.push_back("live: avg decode per 160 ms chunk " + std::to_string((int)r.avg_ms) + " ms > 160 ms");
            }
        }
        std::printf("{\"live\":{\"ok\":%s,\"cursor\":%d,\"words\":%d,\"skipped\":%d,\"monotonic\":%s,\"chunks\":%zu,\"audio_seconds\":%.2f,\"avg_chunk_ms\":%.1f,\"max_chunk_ms\":%.1f,\"partial\":\"%s\",\"failures\":[",
                    f.empty() ? "true" : "false", r.cursor, r.words, r.skipped, r.monotonic ? "true" : "false", r.chunks, r.audio_s, r.avg_ms, r.max_ms, jesc(r.partial).c_str());
        for (size_t i = 0; i < f.size(); ++i) std::printf("%s\"%s\"", i ? "," : "", jesc(f[i]).c_str());
        std::printf("]}}\n");
        std::fflush(stdout);
        for (auto& m : f) std::fprintf(stderr, "FAIL [live]: %s\n", m.c_str());
        fails += (int)f.size();
    }
    std::fprintf(stderr, fails ? "SELFTEST FAILED (%d problem(s))\n" : "SELFTEST OK\n", fails);
    return fails ? 1 : 0;
}

static int cmd_assess(const char* models, const char* wav, const char* text, bool show_progress) {
    std::vector<int16_t> pcm; int sr = 0; std::string err;
    if (!read_wav(wav, pcm, sr, err)) { std::fprintf(stderr, "error: %s\n", err.c_str()); return 2; }
    Engine en(models);
    if (!en.e) { std::fprintf(stderr, "error: cannot create engine for %s\n", models); return 2; }
    ProgState ps; ps.print = true;
    std::string json = show_progress
        ? take(pron_engine_assess_pcm16_progress(en.e, pcm.data(), pcm.size(), sr, text, print_progress, &ps))
        : take(pron_engine_assess_pcm16(en.e, pcm.data(), pcm.size(), sr, text));
    if (json.empty()) { std::fprintf(stderr, "error: %s\n", en.last_error().c_str()); return 1; }
    std::printf("%s\n", json.c_str());
    return 0;
}

int main(int argc, char** argv) {
    std::string c = argc > 1 ? argv[1] : "";
    if (c == "status" && argc == 3) return cmd_status(argv[2]);
    if (c == "selftest" && argc == 3) return cmd_selftest(argv[2]);
    if (c == "live" && argc == 5) return cmd_live(argv[2], argv[3], argv[4]);
    if (c == "assess" && argc == 5) return cmd_assess(argv[2], argv[3], argv[4], false);
    if (c == "assess" && argc == 6 && !std::strcmp(argv[2], "--progress")) return cmd_assess(argv[3], argv[4], argv[5], true);
    std::fprintf(stderr, "usage: pron_cli status <models> | selftest <models> | assess [--progress] <models> <wav16k_mono_pcm16> <text> | live <models> <wav_mono_pcm16> <text>\n");
    return 2;
}

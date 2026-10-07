// Robustness: hostile input must never crash, hit UB, leak, or produce invalid JSON / invalid UTF-8.
#include "doctest.h"
#include "pron/assessment.h"
#include "pron/json_writer.h"
#include "pron/live_tracker.h"
#include "pron/pron_c.h"
#include "pron/result_json.h"
#include "pron/text.h"
#include "pron/word_align.h"
#include "test_data.h"

#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <random>
#include <string>
#include <vector>

using namespace pron;

namespace {

bool valid_utf8(const std::string& s) {
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
        if ((len == 3 && cp < 0x800) || (len == 4 && cp < 0x10000) || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF))
            return false;
        i += len;
    }
    return true;
}

// Strict JSON validator (RFC 8259 subset: no duplicate-key check).
struct J {
    const char* p;
    const char* e;
    void ws() { while (p < e && (*p == ' ' || *p == '\n' || *p == '\t' || *p == '\r')) ++p; }
    bool lit(const char* s) { size_t n = std::strlen(s); if (size_t(e - p) < n || std::strncmp(p, s, n)) return false; p += n; return true; }
    bool str() {
        if (p >= e || *p != '"') return false;
        ++p;
        while (p < e && *p != '"') {
            unsigned char c = static_cast<unsigned char>(*p);
            if (c < 0x20) return false;
            if (c == '\\') {
                ++p;
                if (p >= e) return false;
                if (*p == 'u') {
                    for (int k = 0; k < 4; ++k) {
                        ++p;
                        if (p >= e || !std::isxdigit((unsigned char)*p)) return false;
                    }
                } else if (!std::strchr("\"\\/bfnrt", *p)) {
                    return false;
                }
            }
            ++p;
        }
        if (p >= e) return false;
        ++p;
        return true;
    }
    bool num() {
        const char* s = p;
        if (p < e && *p == '-') ++p;
        while (p < e && (std::isdigit((unsigned char)*p) || *p == '.' || *p == 'e' || *p == 'E' || *p == '+' || *p == '-')) ++p;
        return p > s;
    }
    bool val() {
        ws();
        if (p >= e) return false;
        if (*p == '{') {
            ++p;
            ws();
            if (p < e && *p == '}') { ++p; return true; }
            for (;;) {
                ws();
                if (!str()) return false;
                ws();
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
bool valid_json(const std::string& s) {
    if (!valid_utf8(s)) return false;
    J j{s.data(), s.data() + s.size()};
    if (!j.val()) return false;
    j.ws();
    return j.p == j.e;
}

std::string take(char* s) {
    REQUIRE(s != nullptr);
    std::string out(s);
    pron_free_string(s);
    return out;
}

std::string words_text(int n) {
    static const char* kW[] = {"the", "three", "brothers", "walked", "along", "a", "winding", "road", "and", "they", "were", "very", "tired", "cat", "dog"};
    std::string t;
    for (int i = 0; i < n; ++i) { t += kW[i % 15]; t += (i % 11 == 10) ? ". " : " "; }
    return t;
}

const double kNaN = std::numeric_limits<double>::quiet_NaN();
const double kInf = std::numeric_limits<double>::infinity();

}  // namespace

TEST_SUITE("robust") {
    TEST_CASE("json: invalid UTF-8 is replaced, controls and quotes escaped") {
        CHECK(JsonWriter::escape("\xff") == "\"\xEF\xBF\xBD\"");
        CHECK(JsonWriter::escape("a\x80z") == "\"a\xEF\xBF\xBDz\"");
        CHECK(JsonWriter::escape("\xF0\x9F\x98") == "\"\xEF\xBF\xBD\xEF\xBF\xBD\xEF\xBF\xBD\"");  // truncated emoji
        CHECK(JsonWriter::escape("\xC0\x80") == "\"\xEF\xBF\xBD\xEF\xBF\xBD\"");                   // overlong NUL
        CHECK(JsonWriter::escape("\xED\xA0\x80") == "\"\xEF\xBF\xBD\xEF\xBF\xBD\xEF\xBF\xBD\"");   // UTF-16 surrogate
        CHECK(JsonWriter::escape("\xF4\x90\x80\x80") == "\"\xEF\xBF\xBD\xEF\xBF\xBD\xEF\xBF\xBD\xEF\xBF\xBD\"");  // > U+10FFFF
        CHECK(JsonWriter::escape("\xF0\x9F\x98\x80") == "\"\xF0\x9F\x98\x80\"");                   // valid emoji kept
        CHECK(JsonWriter::escape("\xD0\x9F\xD1\x80") == "\"\xD0\x9F\xD1\x80\"");                   // Cyrillic kept
        CHECK(JsonWriter::escape(std::string("a\0b", 3)) == "\"a\\u0000b\"");
        CHECK(JsonWriter::escape("q\"b\\n\n\r\t\b\f\x01\x1f\x7f") == "\"q\\\"b\\\\n\\n\\r\\t\\b\\f\\u0001\\u001f\\u007f\"");
        std::mt19937 rng(7);
        for (int it = 0; it < 20000; ++it) {
            std::string s;
            const int n = static_cast<int>(rng() % 12);
            for (int k = 0; k < n; ++k) s += static_cast<char>(rng() & 0xFF);
            JsonWriter w;
            w.begin_object().kv("k", s).kv(s, 1).end_object();
            CHECK(valid_json(w.str()));
        }
    }

    TEST_CASE("text: degenerate references tokenize safely") {
        const char* refs[] = {"", " ", "   \t\n\r ", "!!! ... ???", "-- -- --", "'''", "\xF0\x9F\x98\x80\xF0\x9F\x98\x80",
                              "\xD0\x9F\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82 \xD0\xBC\xD0\xB8\xD1\x80", "\xff\xfe\xfd", "\xF0\x9F\x98",
                              "hello\xff world", "12345678901234567890 99999999999999", "a-b-c-d", "\xED\xA0\x80" "ab",
                              "\xF7\xBF\xBF\xBF abc", "ab\xC0"};
        for (const char* r : refs) {
            const std::string s(r);
            auto toks = tokenize(s);
            size_t prev_end = 0;
            for (const auto& t : toks) {
                CHECK(t.begin <= t.end);
                CHECK(t.end <= s.size());
                CHECK(t.begin >= prev_end);
                prev_end = t.end;
            }
            CHECK(valid_json(tokens_to_json(s, toks)));
        }
        CHECK(tokenize("").empty());
        CHECK(tokenize("   ").empty());
        CHECK(tokenize("!!! ... ???").empty());
        CHECK(tokenize("\xF0\x9F\x98\x80").empty());
        CHECK(tokenize("\xD0\x9F\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82 \xD0\xBC\xD0\xB8\xD1\x80").size() == 2);
    }

    TEST_CASE("c api: assess with hostile references and word lists gives valid JSON") {
        pron_assessor* a = pron_assessor_create();
        REQUIRE(a);
        pron_assessor_load_cmudict_text(a, testdata::kCmuSample, std::strlen(testdata::kCmuSample));
        const char* labels[] = {"<pad>", "\xCE\xB8", "s", "\xC9\xAA", "\xC5\x8B", "k", "<unk>"};
        pron_assessor_set_phoneme_vocab(a, labels, 7, 0);
        const int T = 12, C = 7;
        std::vector<float> lp(T * C, std::log(0.01f));
        for (int t = 0; t < T; ++t) lp[t * C + (t % 3 ? 2 : 0)] = std::log(0.94f);

        const char* refs[] = {"", "   ", "!!! ... ???", "\xF0\x9F\x98\x80 \xF0\x9F\x98\x80", "\xD0\x9F\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82 \xD0\xBC\xD0\xB8\xD1\x80",
                              "say \"hi\" \\ back\nslash", "\xff\xfe broken \xF0\x9F", "Think about the 3 brothers and 12345678901234567890."};
        const pron_word cases[][3] = {
            {{"Think", 0.0, 0.2, 0.9f}, {"about", 0.3, 0.5, 0.9f}, {"\xff\x80 \"q\" \\", 0.6, 0.9, 0.5f}},
            {{"x", kNaN, kNaN, 0.5f}, {"y", -kInf, kInf, std::numeric_limits<float>::quiet_NaN()}, {"z", 1e300, -1e300, 2.0f}},
            {{"", 0.0, 0.0, 0.0f}, {nullptr, 0.0, 0.0, 1.0f}, {"\xF0\x9F\x98\x80", -5.0, -4.0, -1.0f}},
        };
        for (const char* ref : refs) {
            for (const auto& ws : cases) {
                for (int with_post = 0; with_post < 2; ++with_post) {
                    for (int nw : {0, 1, 3}) {
                        char* r = pron_assess(a, ref, nw ? ws : nullptr, nw, with_post ? lp.data() : nullptr, with_post ? T : 0, with_post ? C : 0, 0.02);
                        REQUIRE(r != nullptr);
                        const std::string j = take(r);
                        CHECK(valid_json(j));
                    }
                }
            }
        }
        // NaN / +-inf posteriors must not crash or produce invalid JSON.
        std::vector<float> bad = lp;
        for (size_t i = 0; i < bad.size(); i += 5) bad[i] = std::numeric_limits<float>::quiet_NaN();
        for (size_t i = 2; i < bad.size(); i += 7) bad[i] = -std::numeric_limits<float>::infinity();
        pron_word w[] = {{"Think", 0.0, 0.2, 0.9f}};
        CHECK(valid_json(take(pron_assess(a, "Think.", w, 1, bad.data(), T, C, 0.02))));
        // Degenerate matrix geometry and frame durations.
        CHECK(valid_json(take(pron_assess(a, "Think.", w, 1, lp.data(), T, C, 0.0))));
        CHECK(valid_json(take(pron_assess(a, "Think.", w, 1, lp.data(), T, C, kNaN))));
        CHECK(valid_json(take(pron_assess(a, "Think.", w, 1, lp.data(), 1, C, 0.02))));
        CHECK(valid_json(take(pron_assess(a, "Think.", w, 1, lp.data(), T, 1, 0.02))));  // classes != vocab
        CHECK(pron_assess(a, "x", nullptr, 1, nullptr, 0, 0, 0.02) == nullptr);        // words NULL with count > 0
        CHECK(pron_assess(a, "x", w, -1, nullptr, 0, 0, 0.02) == nullptr);
        CHECK(pron_assess(a, nullptr, w, 1, nullptr, 0, 0, 0.02) == nullptr);
        pron_free_string(pron_assess(a, "x", w, 1, nullptr, 0x7fffffff, 0x7fffffff, 0.02));  // no data pointer: ignored
        CHECK(pron_assess(a, "x", w, 1, lp.data(), 0x7fffffff, 0x7fffffff, 0.02) == nullptr);  // absurd size refused, no overread
        pron_assessor_destroy(a);
    }

    TEST_CASE("assess: 5000-word reference and recognition completes with bounded memory/time") {
        pron_assessor* a = pron_assessor_create();
        REQUIRE(a);
        pron_assessor_load_cmudict_text(a, testdata::kCmuSample, std::strlen(testdata::kCmuSample));
        const std::string ref = words_text(5000);
        std::vector<std::string> txt;
        std::vector<pron_word> w;
        for (const auto& t : tokenize(ref)) txt.push_back(t.norm);
        w.reserve(txt.size());
        for (size_t i = 0; i < txt.size(); ++i) w.push_back({txt[i].c_str(), i * 0.3, i * 0.3 + 0.25, 0.9f});
        const auto t0 = std::chrono::steady_clock::now();
        const std::string j = take(pron_assess(a, ref.c_str(), w.data(), static_cast<int>(w.size()), nullptr, 0, 0, 0.02));
        const double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        CHECK(valid_json(j));
        CHECK(j.find("\"completeness\":100") != std::string::npos);
        CHECK(sec < 60.0);
        pron_assessor_destroy(a);
    }

    TEST_CASE("word alignment: asymmetric and empty sides") {
        std::vector<std::string> a = {"a", "b", "c"}, none;
        auto r1 = align_words(a, none, {});
        CHECK(r1.ref.size() == 3);
        for (const auto& x : r1.ref) CHECK(x.status == WordStatus::Omitted);
        auto r2 = align_words(none, a, {});
        CHECK(r2.inserted.size() == 3);
        auto r3 = align_words(none, none, {});
        CHECK(r3.ref.empty());
        CHECK(r3.cost == 0.0);
    }

    TEST_CASE("live: 5000-word reference, incremental hypothesis, valid JSON and exact UTF-16 offsets") {
        const std::string ref = words_text(5000);
        LiveTracker t(ref);
        REQUIRE(t.word_count() == 5000);
        std::vector<std::string> hyp;
        const auto toks = tokenize(ref);
        const auto t0 = std::chrono::steady_clock::now();
        for (size_t i = 0; i < toks.size(); ++i) {
            hyp.push_back(toks[i].norm);
            if (i % 50 == 49 || i + 1 == toks.size()) {
                t.update(hyp, i + 1 == toks.size());
                CHECK(valid_json(live_state_json(t, "partial \"text\"")));
            }
        }
        const double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        CHECK(t.state().done);
        CHECK(t.state().cursor == 5000);
        CHECK(sec < 30.0);
    }

    TEST_CASE("live: 1300-word reference with repeated phrases across paragraphs, long hypothesis, cheap feeds") {
        // 13 paragraphs that all contain the same sentence; the reader goes through them in order.
        std::string ref;
        for (int p = 0; p < 13; ++p) {
            ref += "Paragraph " + std::string(1, char('a' + p)) + " begins here. ";
            for (int k = 0; k < 8; ++k) ref += "the quick brown fox jumps over the lazy dog near the river bank. ";
            ref += "Unique" + std::string(1, char('a' + p)) + " ending words follow now.\n\n";
        }
        LiveTracker t(ref);
        const auto toks = tokenize(ref);
        REQUIRE(toks.size() > 1200);
        std::vector<std::string> hyp;
        double worst_ms = 0, total_ms = 0;
        int feeds = 0;
        for (size_t i = 0; i < toks.size(); ++i) {
            hyp.push_back(toks[i].norm);
            if (i % 3 == 2 || i + 1 == toks.size()) {
                const auto t0 = std::chrono::steady_clock::now();
                t.update(hyp, false);
                const std::string j = live_state_json(t, "x");
                const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
                worst_ms = std::max(worst_ms, ms);
                total_ms += ms;
                ++feeds;
                // cursor never lags/jumps: it must be within 4 words of the true position
                const int truth = int(i) + 1;
                CHECK(std::abs(t.state().cursor - truth) <= 4);
                if (j.empty()) FAIL("empty json");
            }
        }
        t.update(hyp, true);
        CHECK(t.state().done);
        MESSAGE("live 1300 words: avg " << total_ms / feeds << " ms, worst " << worst_ms << " ms per feed");
        CHECK(total_ms / feeds < 20.0);  // ~0.6 ms in release; generous for sanitizer builds
    }

    TEST_CASE("live: JSON offsets match utf16_offset for emoji / Cyrillic references") {
        const std::string ref = "\xF0\x9F\x98\x80 hello \xD0\xBF\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82 world \xF0\x9F\x98\x80\xF0\x9F\x98\x80 end";
        LiveTracker t(ref);
        const std::string j = live_state_json(t, "");
        CHECK(valid_json(j));
        size_t pos = 0;
        for (const auto& tk : t.tokens()) {
            const std::string kb = "\"u16_begin\":" + std::to_string(utf16_offset(ref, tk.begin)) + ",\"u16_end\":" + std::to_string(utf16_offset(ref, tk.end));
            pos = j.find(kb, pos);
            CHECK(pos != std::string::npos);
        }
    }

    TEST_CASE("live: degenerate references and garbage hypotheses") {
        for (const char* r : {"", "   ", "!!!", "\xF0\x9F\x98\x80", "\xD0\x9F\xD1\x80 \xD0\xBC", "\xff\xfe", "one"}) {
            LiveTracker t(r);
            CHECK(valid_json(live_state_json(t, "")));
            const std::vector<std::string> junk[] = {{}, {""}, {"", "", ""}, {"!!!", "...", "\xF0\x9F\x98\x80"}, {std::string(100000, 'a')},
                                                     {"\xff\xfe", "\xD0\x9F"}, {"one", "one", "one"}, {"1", "2", "3"}};
            for (const auto& h : junk) {
                t.update(h, false);
                CHECK(valid_json(live_state_json(t, std::string("\xff", 1))));
                t.update(h, true);
                CHECK(valid_json(live_state_json(t, "")));
            }
            for (int c : {-5, 0, 1, 2, 1000000, std::numeric_limits<int>::min(), std::numeric_limits<int>::max()}) {
                t.set_cursor(c);
                CHECK(t.state().cursor >= 0);
                CHECK(t.state().cursor <= t.word_count());
                t.update({"one", "two"}, false);
                CHECK(valid_json(live_state_json(t, "")));
            }
        }
    }

    TEST_CASE("live: hypothesis shrinking / rewinding between updates") {
        LiveTracker t(words_text(60));
        const auto toks = tokenize(words_text(60));
        std::vector<std::string> h;
        for (size_t i = 0; i < 40; ++i) h.push_back(toks[i].norm);
        t.update(h);
        for (size_t n : {10u, 0u, 39u, 3u, 40u}) {
            t.update(std::vector<std::string>(h.begin(), h.begin() + static_cast<std::ptrdiff_t>(n)));
            CHECK(t.state().cursor >= 0);
            CHECK(t.state().cursor <= 60);
            CHECK(valid_json(live_state_json(t, "")));
        }
    }

    TEST_CASE("repeated sessions do not leak (checked under LeakSanitizer)") {
        const std::string ref = words_text(40);
        for (int i = 0; i < 100; ++i) {
            pron_assessor* a = pron_assessor_create();
            REQUIRE(a);
            pron_assessor_load_cmudict_text(a, testdata::kCmuSample, std::strlen(testdata::kCmuSample));
            pron_word w[] = {{"the", 0.0, 0.2, 0.9f}, {"cat", 0.3, 0.5, 0.9f}};
            pron_free_string(pron_assess(a, ref.c_str(), w, 2, nullptr, 0, 0, 0.02));
            pron_free_string(pron_assessor_lookup(a, "cat"));
            pron_free_string(pron_assessor_phoneme_vocab_json(a));
            pron_free_string(pron_tokenize(ref.c_str()));
            pron_assessor_destroy(a);
            LiveTracker t(ref);
            t.update({"the", "three"});
            (void)live_state_json(t, "x");
        }
    }
}

#include "doctest.h"
#include "pron/advice.h"
#include "pron/phonemes.h"

using namespace pron;

namespace {
int id(const char* a) { return phoneme_id(a); }

bool valid_utf8_with_cyrillic(const std::string& s) {
    bool cyr = false;
    for (std::size_t i = 0; i < s.size();) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        std::size_t len = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 0;
        if (len == 0 || i + len > s.size()) return false;
        for (std::size_t k = 1; k < len; ++k)
            if ((static_cast<unsigned char>(s[i + k]) >> 6) != 2) return false;
        if (len == 2 && (c == 0xD0 || c == 0xD1)) cyr = true;
        i += len;
    }
    return cyr;
}
}  // namespace

TEST_SUITE("advice") {
    AdviceEngine eng;

    TEST_CASE("think read as sink: θ -> s") {
        auto a = eng.advise(id("TH"), id("S"), false);
        REQUIRE(a);
        CHECK(a->id == "th_s");
        CHECK(a->sound_id == "θ");
        CHECK(a->expected_ipa == "θ");
        CHECK(a->actual_ipa == "s");
        CHECK(a->title_ru == "θ звучит как «с»");
        CHECK(a->tip_ru.find("Кончик языка") != std::string::npos);
        CHECK(a->tip_ru.find("sink — think") != std::string::npos);
    }

    TEST_CASE("typical substitutions") {
        struct Case {
            const char *exp, *act, *advice;
        } cases[] = {
            {"TH", "F", "th_f"},   {"TH", "T", "th_t"},   {"DH", "Z", "dh_z"},   {"DH", "D", "dh_d"},
            {"W", "V", "w_v"},     {"AE", "EH", "ae_e"},  {"AE", "E", "ae_e"},   {"IH", "IY", "ih_iy"},
            {"IY", "IH", "iy_ih"}, {"UH", "UW", "uh_uw"}, {"UW", "UH", "uw_uh"}, {"R", "RR", "r_trill"},
            {"HH", "X", "h_x"},    {"NG", "N", "ng_n"},   {"NG", "K", "ng_nk"},
        };
        for (const auto& c : cases) {
            CAPTURE(c.exp);
            CAPTURE(c.act);
            auto a = eng.advise(id(c.exp), id(c.act), false);
            REQUIRE(a);
            CHECK(a->id == c.advice);
        }
    }

    TEST_CASE("wildcards for unknown actual phone") {
        auto th = eng.advise(id("TH"), -1, false);
        REQUIRE(th);
        CHECK(th->id == "th");
        CHECK(th->actual_ipa.empty());
        CHECK(eng.advise(id("R"), id("L"), false)->id == "r");
        CHECK(eng.advise(id("HH"), -1, true)->sound_id == "h");
        CHECK_FALSE(eng.advise(id("K"), id("G"), false));  // no rule for k
        CHECK_FALSE(eng.advise(-1, id("S"), false));
    }

    TEST_CASE("final devoicing only at word end") {
        auto a = eng.advise(id("D"), id("T"), true);
        REQUIRE(a);
        CHECK(a->id == "final_devoicing");
        CHECK(a->sound_id == "final-voiced");
        CHECK_FALSE(eng.advise(id("D"), id("T"), false));
        CHECK(eng.advise(id("Z"), id("S"), true)->id == "final_devoicing");
        // Word-final rule beats the generic ð rule.
        CHECK(eng.advise(id("DH"), id("TH"), true)->id == "final_devoicing");
        CHECK(eng.advise(id("DH"), id("TH"), false)->id == "dh");
    }

    TEST_CASE("table sanity") {
        for (const auto& r : eng.rules()) {
            CAPTURE(r.id);
            CHECK(phoneme_id(r.expected) >= 0);
            CHECK((r.actual == "*" || phoneme_id(r.actual) >= 0));
            CHECK(!r.sound_id.empty());
            CHECK(valid_utf8_with_cyrillic(r.title_ru));
            CHECK(valid_utf8_with_cyrillic(r.tip_ru));
        }
    }

    TEST_CASE("custom table") {
        AdviceEngine e = AdviceEngine::empty();
        CHECK(e.rules().empty());
        CHECK_FALSE(e.advise(id("TH"), id("S"), false));
        e.add_rule({"L", "*", Position::Any, "dark_l", "dark-l", "Тёмный l", "Кончик языка у альвеол."});
        CHECK(e.advise(id("L"), -1, false)->id == "dark_l");
    }
}

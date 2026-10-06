#include "doctest.h"
#include "pron/cmudict.h"
#include "pron/lexicon.h"
#include "test_data.h"

#include <sstream>

using namespace pron;

namespace {
class FakeG2P : public IG2P {
public:
    int calls = 0;
    bool pronounce(const std::string& word, Pronunciation& out) override {
        ++calls;
        if (word != "zorb") return false;
        return parse_arpabet("Z AO1 R B", out);
    }
};
}  // namespace

TEST_SUITE("cmudict") {
    TEST_CASE("load sample with comments, variants and bad lines") {
        CmuDict d;
        auto st = d.load_from_string(testdata::kCmuSample);
        CHECK(st.entries == 33);
        CHECK(st.skipped == 2);
        CHECK(d.word_count() == 30);
        CHECK(d.pronunciation_count() == 33);
        const auto* the = d.find("THE");
        REQUIRE(the);
        REQUIRE(the->size() == 3);
        CHECK(to_arpabet_string((*the)[0]) == "DH AH0");
        CHECK(to_arpabet_string((*the)[2]) == "DH IY0");
        CHECK(d.contains("think"));
        CHECK(d.contains("Think"));
        CHECK_FALSE(d.contains("brokenline"));
        CHECK_FALSE(d.contains("badphone"));
        CHECK_FALSE(d.contains("zebra"));
        // Curly apostrophe in lookup.
        CHECK(d.contains("don\xE2\x80\x99t"));
        // Stress kept.
        const auto* ab = d.find("abandon");
        REQUIRE(ab);
        CHECK(primary_stress_syllable(ab->front()) == 1);
    }

    TEST_CASE("cmudict.dict format: lowercase, (2) variants, inline comments, CRLF") {
        std::istringstream in("read r iy1 d\r\nread(2) r eh1 d # past tense\r\nlive l ih1 v\n");
        CmuDict d;
        auto st = d.load(in);
        CHECK(st.entries == 3);
        const auto* r = d.find("Read");
        REQUIRE(r);
        REQUIRE(r->size() == 2);
        CHECK(to_arpabet_string((*r)[1]) == "R EH1 D");
        // Duplicates are not added twice.
        d.add("live", (*d.find("live"))[0]);
        CHECK(d.find("live")->size() == 1);
    }

    TEST_CASE("load_file") {
        CmuDict d;
        CHECK_FALSE(d.load_file("/nonexistent/path/cmudict.dict"));
    }

    TEST_CASE("lexicon: dictionary, possessive, G2P fallback") {
        CmuDict d;
        d.load_from_string(testdata::kCmuSample);
        FakeG2P g2p;
        Lexicon lex(&d, &g2p);
        Pronunciation p;
        CHECK(lex.lookup("think", p) == PronSource::CmuDict);
        CHECK(to_arpabet_string(p) == "TH IH1 NG K");
        CHECK(lex.lookup("teacher's", p) == PronSource::CmuDict);
        CHECK(to_arpabet_string(p) == "T IY1 CH ER0 Z");
        CHECK(lex.lookup("cat's", p) == PronSource::CmuDict);
        CHECK(to_arpabet_string(p) == "K AE1 T S");
        CHECK(lex.lookup("house's", p) == PronSource::CmuDict);
        CHECK(to_arpabet_string(p) == "HH AW1 S IH0 Z");
        CHECK(lex.lookup("zorb", p) == PronSource::G2P);
        CHECK(to_arpabet_string(p) == "Z AO1 R B");
        CHECK(lex.lookup("qwerty", p) == PronSource::None);
        CHECK(p.empty());
        CHECK(g2p.calls == 2);
        Lexicon no_g2p(&d);
        CHECK(no_g2p.lookup("zorb", p) == PronSource::None);
        CHECK(std::string(to_string(PronSource::G2P)) == "g2p");
    }
}

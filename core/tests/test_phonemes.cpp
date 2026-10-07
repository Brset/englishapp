#include <algorithm>
#include "doctest.h"
#include "pron/phonemes.h"
#include "pron/posteriors.h"

#include <cmath>

using namespace pron;

TEST_SUITE("phonemes") {
    TEST_CASE("inventory covers the 39 CMUdict phonemes") {
        const char* cmu[] = {"AA", "AE", "AH", "AO", "AW", "AY", "B",  "CH", "D",  "DH", "EH", "ER", "EY",
                             "F",  "G",  "HH", "IH", "IY", "JH", "K",  "L",  "M",  "N",  "NG", "OW", "OY",
                             "P",  "R",  "S",  "SH", "T",  "TH", "UH", "UW", "V",  "W",  "Y",  "Z",  "ZH"};
        int english = 0;
        for (const char* p : cmu) {
            int id = phoneme_id(p);
            REQUIRE(id >= 0);
            CHECK(phoneme_info(id).english);
            CHECK(std::string(phoneme_info(id).ipa).size() > 0);
            // IPA -> ARPAbet round trip
            CHECK(ipa_to_arpabet(phoneme_info(id).ipa) == p);
        }
        for (int i = 0; i < phoneme_count(); ++i)
            if (phoneme_info(i).english) ++english;
        CHECK(english == 39);
        CHECK(inventory_columns() == phoneme_count() + 1);
    }

    TEST_CASE("ARPAbet <-> IPA") {
        CHECK(arpabet_to_ipa("TH") == "θ");
        CHECK(arpabet_to_ipa("dh") == "ð");
        CHECK(arpabet_to_ipa("IH1") == "ɪ");
        CHECK(arpabet_to_ipa("NG") == "ŋ");
        CHECK(arpabet_to_ipa("AE") == "æ");
        CHECK(arpabet_to_ipa("QQ").empty());
        CHECK(ipa_to_arpabet("θ") == "TH");
        CHECK(ipa_to_arpabet("iː") == "IY");
        CHECK(ipa_to_arpabet("ˈiː") == "IY");
        CHECK(ipa_to_arpabet("ə") == "AH");
        CHECK(ipa_to_arpabet("ɚ") == "ER");
        CHECK(ipa_to_arpabet("g") == "G");
        CHECK(ipa_to_arpabet("t͡ʃ") == "CH");
        CHECK(ipa_to_arpabet("x") == "X");
        CHECK(ipa_to_arpabet("r") == "RR");
        CHECK(ipa_to_arpabet("ɹ") == "R");
        CHECK(ipa_to_arpabet("e") == "E");
        CHECK(ipa_to_arpabet("ʕ").empty());
        CHECK(phoneme_id("AX") == phoneme_id("AH"));
        CHECK_FALSE(phoneme_info(phoneme_id("X")).english);
        CHECK(phoneme_info(phoneme_id("IY")).vowel);
        CHECK_FALSE(phoneme_info(phoneme_id("S")).voiced);
        CHECK(phoneme_info(phoneme_id("Z")).voiced);
    }

    TEST_CASE("parse ARPAbet with stress") {
        Pronunciation p;
        REQUIRE(parse_arpabet("AH0 B AE1 N D AH0 N", p));
        REQUIRE(p.size() == 7);
        CHECK(p[0].symbol == "AH");
        CHECK(p[0].stress == 0);
        CHECK(p[2].stress == 1);
        CHECK(p[1].stress == -1);
        CHECK(primary_stress_syllable(p) == 1);
        CHECK(syllable_count(p) == 3);
        CHECK(to_arpabet_string(p) == "AH0 B AE1 N D AH0 N");
        CHECK(to_arpabet_string(p, false) == "AH B AE N D AH N");
        CHECK(to_ipa_string(p) == "əbændən");
        CHECK(to_ipa_string(p, true) == "əˈbændən");
        Pronunciation q;
        REQUIRE(parse_arpabet("IH0 K S P L EY1 N", q));  // explain: s+p is an onset, k stays behind
        CHECK(to_ipa_string(q, true) == "ɪkˈspleɪn");
        REQUIRE(parse_arpabet("AH0 B S T R AE1 K T", q));  // abstract: "str" onset
        CHECK(to_ipa_string(q, true) == "əbˈstɹækt");
        REQUIRE(parse_arpabet("AH0 D M IH1 T", q));  // admit: "dm" is not an onset
        CHECK(to_ipa_string(q, true) == "ədˈmɪt");
        REQUIRE(parse_arpabet("W IH1 N D OW0", q));
        CHECK(to_ipa_string(q, true) == "ˈwɪndoʊ");
        CHECK_FALSE(parse_arpabet("TH QQ1 K", p));
        CHECK(p.empty());
        REQUIRE(parse_arpabet("th ih ng k", p));
        CHECK(p[1].stress == 0);  // vowel without digit
        CHECK(primary_stress_syllable(p) == -1);
    }

    TEST_CASE("model vocab mapping and collapse") {
        // wav2vec2-style IPA vocab with special tokens; "i" and "iː" both map to IY.
        PhonemeVocab v({"<pad>", "<unk>", "|", "θ", "s", "i", "iː", "ʕ"}, 0);
        CHECK(v.phoneme_of_label(0) == -1);
        CHECK(v.phoneme_of_label(3) == phoneme_id("TH"));
        CHECK(v.phoneme_of_label(5) == phoneme_id("IY"));
        CHECK(v.phoneme_of_label(6) == phoneme_id("IY"));
        CHECK(v.unmapped_labels() == std::vector<std::string>{"<unk>", "|", "ʕ"});

        LogPosteriors m(1, 8);
        const float probs[] = {0.1f, 0.0f, 0.0f, 0.2f, 0.1f, 0.3f, 0.3f, 0.0f};
        for (int c = 0; c < 8; ++c) m.at(0, c) = probs[c] > 0 ? std::log(probs[c]) : -1e9f;
        LogPosteriors inv = v.collapse(m);
        CHECK(inv.classes == inventory_columns());
        CHECK(std::exp(inv.at(0, kBlankColumn)) == doctest::Approx(0.1).epsilon(1e-4));
        CHECK(std::exp(inv.at(0, column_of_phoneme(phoneme_id("IY")))) == doctest::Approx(0.6).epsilon(1e-4));
        CHECK(std::exp(inv.at(0, column_of_phoneme(phoneme_id("TH")))) == doctest::Approx(0.2).epsilon(1e-4));
        CHECK(inv.at(0, column_of_phoneme(phoneme_id("K"))) < -100.0f);
    }

    TEST_CASE("espeak IPA aliases and accent variants") {
        CHECK(phoneme_id_from_ipa("\xC9\xA1") == phoneme_id("G"));  // U+0261 script g
        CHECK(phoneme_id_from_ipa("g") == phoneme_id("G"));
        CHECK(phoneme_id_from_ipa("ɹ") == phoneme_id("R"));
        CHECK(phoneme_id_from_ipa("ɚ") == phoneme_id("ER"));
        CHECK(phoneme_id_from_ipa("oʊ") == phoneme_id("OW"));
        CHECK(phoneme_id_from_ipa("əʊ") == phoneme_id("OW"));
        CHECK(phoneme_id_from_ipa("ɐ") == phoneme_id("AH"));
        CHECK(phoneme_id_from_ipa("ᵻ") == phoneme_id("IH"));
        CHECK(phoneme_id_from_ipa("ɾ") == phoneme_id("T"));
        CHECK(phoneme_id_from_ipa("ɒ") == phoneme_id("AA"));
        CHECK(phoneme_id_from_ipa("ɜː") == phoneme_id("ER"));
        CHECK(phoneme_id_from_ipa("uː") == phoneme_id("UW"));
        CHECK(phoneme_id_from_ipa("ː") == -1);
        Accent a;
        CHECK(parse_accent("GB", a));
        CHECK(a == Accent::Gb);
        CHECK_FALSE(parse_accent("martian", a));

        Pronunciation car, weather, bath;
        REQUIRE(parse_arpabet("K AA1 R", car));
        REQUIRE(parse_arpabet("W EH1 DH ER0", weather));
        REQUIRE(parse_arpabet("B AE1 TH", bath));
        CHECK(phoneme_variants(car, 2, Accent::Gb).allow_blank);
        CHECK(phoneme_variants(car, 2, Accent::Any).allow_blank);
        CHECK_FALSE(phoneme_variants(car, 2, Accent::Us).allow_blank);
        auto er = phoneme_variants(weather, 3, Accent::Gb);
        CHECK(er.ids == std::vector<int>{phoneme_id("ER"), phoneme_id("AH")});
        CHECK(phoneme_variants(bath, 1, Accent::Gb).ids.size() == 2);
        CHECK(phoneme_variants(bath, 1, Accent::Us).ids.size() == 1);
    }

    TEST_CASE("gruut-style character-level vocab: marks fold into blank, components, summary") {
        const std::vector<std::string> labels = {"[PAD]", "[UNK]", "|", "ˈ", "ˌ", "ː", "θ", "ð", "ŋ", "ɡ", "ɹ", "ɾ",
                                                 "a", "e", "o", "i", "u", "ɪ", "ʊ", "ɔ", "ɑ", "ɚ", "ə", "t", "ʃ"};
        PhonemeVocab v(labels, 0);
        auto L = [&](const char* s) { return static_cast<int>(std::find(labels.begin(), labels.end(), std::string(s)) - labels.begin()); };
        for (const char* m : {"ˈ", "ˌ", "ː"}) {
            CHECK(is_ipa_mark_only(m));
            CHECK(v.is_ignored(L(m)));
            CHECK(v.phoneme_of_label(L(m)) == -1);
        }
        CHECK_FALSE(is_ipa_mark_only("ɪ"));
        CHECK_FALSE(is_ipa_mark_only("|"));
        CHECK(v.is_delimiter(L("|")));
        CHECK_FALSE(v.is_ignored(L("|")));
        CHECK(v.phoneme_of_label(L("a")) == phoneme_id("AA"));
        CHECK(v.phoneme_of_label(L("e")) == phoneme_id("E"));
        CHECK(v.phoneme_of_label(L("o")) == phoneme_id("O"));
        CHECK(v.phoneme_of_label(L("ɪ")) == phoneme_id("IH"));
        CHECK(v.phoneme_of_label(L("ʊ")) == phoneme_id("UH"));
        CHECK(v.phoneme_of_label(L("ɹ")) == phoneme_id("R"));
        CHECK(v.phoneme_of_label(L("ɾ")) == phoneme_id("T"));
        CHECK(v.phoneme_of_label(L("ɚ")) == phoneme_id("ER"));
        // No single token for diphthongs / affricates in this vocab.
        for (const char* d : {"EY", "AY", "AW", "OW", "OY", "CH", "JH"})
            CHECK_FALSE(v.has_column(column_of_phoneme(phoneme_id(d))));
        CHECK(phoneme_components(phoneme_id("EY")) == std::vector<int>{phoneme_id("E"), phoneme_id("IH")});
        CHECK(phoneme_components(phoneme_id("AW")) == std::vector<int>{phoneme_id("AA"), phoneme_id("UH")});
        CHECK(phoneme_components(phoneme_id("CH")) == std::vector<int>{phoneme_id("T"), phoneme_id("SH")});
        CHECK(phoneme_components(phoneme_id("TH")).empty());

        auto sum = v.mapping_summary();
        REQUIRE(sum.size() == labels.size());
        CHECK(sum[0] == "[PAD]>blank");
        CHECK(sum[1] == "[UNK]>-");
        CHECK(sum[2] == "|>blank");
        CHECK(sum[3] == "ˈ>blank");
        CHECK(sum[6] == "θ>TH");

        // A stress-mark frame is blank mass, not lost mass.
        LogPosteriors m(1, static_cast<int>(labels.size()));
        for (int c = 0; c < m.classes; ++c) m.at(0, c) = -20.0f;
        m.at(0, L("ˈ")) = std::log(0.9f);
        m.at(0, 0) = std::log(0.05f);
        LogPosteriors inv = v.collapse(m);
        CHECK(std::exp(inv.at(0, kBlankColumn)) == doctest::Approx(0.95).epsilon(1e-3));
    }

    TEST_CASE("posterior slice") {
        LogPosteriors m(5, 2);
        for (int t = 0; t < 5; ++t) m.at(t, 0) = static_cast<float>(t);
        LogPosteriors s = m.slice(1, 3);
        CHECK(s.frames == 2);
        CHECK(s.at(0, 0) == 1.0f);
        CHECK(s.at(1, 0) == 2.0f);
        CHECK(m.slice(4, 100).frames == 1);
        CHECK(m.slice(-5, 0).frames == 0);
    }
}

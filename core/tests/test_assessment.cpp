#include "doctest.h"
#include "pron/assessment.h"
#include "pron/result_json.h"
#include "test_data.h"

#include <cmath>
#include <map>

using namespace pron;

namespace {

void setup(Assessor& a) { a.dict().load_from_string(testdata::kCmuSample); }

std::vector<AsrWord> words(std::initializer_list<std::tuple<const char*, double, double>> list) {
    std::vector<AsrWord> out;
    for (const auto& w : list) out.push_back({std::get<0>(w), std::get<1>(w), std::get<2>(w), 1.0f});
    return out;
}

// Model vocabulary in IPA, wav2vec2 style.
const std::vector<std::string> kLabels = {"<pad>", "θ", "s", "ɪ", "ŋ", "k", "ð", "ə", "æ", "t", "<unk>"};

// Frames described as {label: prob}; remaining mass spread uniformly over unspecified labels.
LogPosteriors frames(const std::vector<std::map<int, double>>& spec) {
    const int C = static_cast<int>(kLabels.size());
    LogPosteriors lp(static_cast<int>(spec.size()), C, 0.02);
    for (int t = 0; t < lp.frames; ++t) {
        const auto& m = spec[static_cast<std::size_t>(t)];
        double used = 0;
        for (const auto& kv : m) used += kv.second;
        double rest = (1.0 - used) / (C - static_cast<int>(m.size()));
        for (int c = 0; c < C; ++c) {
            auto it = m.find(c);
            lp.at(t, c) = static_cast<float>(std::log(it != m.end() ? it->second : rest));
        }
    }
    return lp;
}

const std::map<int, double> kBlank = {{0, 0.95}};
std::map<int, double> ph(int label) { return {{label, 0.9}, {0, 0.05}}; }

}  // namespace

TEST_SUITE("assessment") {
    TEST_CASE("think read as sink (ASR only): θ→s advice, omitted word, totals") {
        Assessor a;
        setup(a);
        auto r = a.assess("I think the cat sat on the mat.",
                          words({{"I", 0.0, 0.2}, {"sink", 0.3, 0.6}, {"the", 0.7, 0.8}, {"cat", 0.9, 1.2},
                                 {"on", 1.3, 1.4}, {"the", 1.5, 1.6}, {"mat.", 1.7, 2.0}}));
        REQUIRE(r.words.size() == 8);
        CHECK_FALSE(r.phoneme_level);

        const WordResult& think = r.words[1];
        CHECK(think.text == "think");
        CHECK(think.status == WordStatus::Substituted);
        CHECK(think.recognized == "sink");
        CHECK(think.scored_by == ScoredBy::Diff);
        CHECK(think.expected_ipa == "ˈθɪŋk");
        REQUIRE(think.phonemes.size() == 4);
        const PhonemeResult& th = think.phonemes[0];
        CHECK(th.arpabet == "TH");
        CHECK(th.ipa == "θ");
        CHECK(th.substituted);
        CHECK(th.actual_arpabet == "S");
        CHECK(th.actual_ipa == "s");
        CHECK(th.score == 0.0);
        REQUIRE(th.advice);
        CHECK(th.advice->id == "th_s");
        CHECK(th.advice->title_ru == "θ звучит как «с»");
        CHECK(th.advice->tip_ru.find("sink — think") != std::string::npos);
        CHECK(think.phonemes[1].score == 100.0);
        CHECK_FALSE(think.phonemes[1].advice);
        CHECK(think.score == doctest::Approx(60.0));  // capped
        CHECK(think.band == ScoreBand::Fair);
        CHECK(think.start == doctest::Approx(0.3));

        const WordResult& sat = r.words[4];
        CHECK(sat.status == WordStatus::Omitted);
        CHECK(sat.band == ScoreBand::Omitted);
        CHECK(sat.score == 0.0);
        CHECK(sat.start < 0);

        CHECK(r.words[0].band == ScoreBand::Good);
        CHECK(r.words[7].byte_begin == 27);
        CHECK(r.words[7].byte_end == 30);
        CHECK(r.completeness == doctest::Approx(87.5));
        CHECK(r.accuracy == doctest::Approx((6 * 100.0 + 60.0) / 7));
        CHECK(r.fluency_score > 0);
        CHECK(r.overall > 0);
        CHECK(r.overall <= 100);

        REQUIRE(r.advice.size() == 1);
        CHECK(r.advice[0].advice.id == "th_s");
        CHECK(r.advice[0].count == 1);
        CHECK(r.advice[0].words == std::vector<int>{1});
        CHECK(r.inserted.empty());
        CHECK(r.warnings.empty());
    }

    TEST_CASE("think heard as think by ASR but posteriors show s: GOP detects θ→s") {
        Assessor a;
        setup(a);
        a.set_vocab(PhonemeVocab(kLabels, 0));
        // 0.10-0.30 s word; frames 5..15 carry s ɪ ŋ k, with a little θ mass on the s frames.
        std::vector<std::map<int, double>> spec(22, kBlank);
        spec[5] = spec[6] = spec[7] = {{2, 0.88}, {1, 0.04}, {0, 0.02}};
        spec[9] = spec[10] = ph(3);
        spec[11] = spec[12] = ph(4);
        spec[13] = ph(5);
        LogPosteriors lp = frames(spec);
        auto r = a.assess("Think!", words({{" think", 0.10, 0.30}}), &lp);
        CHECK(r.phoneme_level);
        REQUIRE(r.words.size() == 1);
        const WordResult& w = r.words[0];
        CHECK(w.status == WordStatus::Matched);
        CHECK(w.scored_by == ScoredBy::Gop);
        REQUIRE(w.phonemes.size() == 4);
        const PhonemeResult& th = w.phonemes[0];
        CHECK(th.substituted);
        CHECK(th.actual_arpabet == "S");
        CHECK(th.score < 30);
        CHECK(th.gop < 0);
        REQUIRE(th.advice);
        CHECK(th.advice->id == "th_s");
        CHECK(th.start == doctest::Approx(0.10).epsilon(0.01));
        for (int k = 1; k < 4; ++k) {
            CAPTURE(k);
            CHECK(w.phonemes[k].score > 90);
            CHECK_FALSE(w.phonemes[k].substituted);
            CHECK(w.phonemes[k].start >= w.phonemes[k - 1].start);
        }
        CHECK(w.score == doctest::Approx(75.0));  // capped: a word with a substituted phone is not green
        CHECK(w.band == ScoreBand::Fair);
        CHECK(r.advice.size() == 1);
    }

    TEST_CASE("correct reading with posteriors scores green") {
        Assessor a;
        setup(a);
        a.set_vocab(PhonemeVocab(kLabels, 0));
        std::vector<std::map<int, double>> spec(16, kBlank);
        spec[3] = ph(5);   // k
        spec[4] = spec[5] = ph(8);  // æ
        spec[6] = ph(9);   // t
        LogPosteriors lp = frames(spec);
        auto r = a.assess("cat", words({{"cat", 0.06, 0.14}}), &lp);
        REQUIRE(r.words.size() == 1);
        CHECK(r.words[0].band == ScoreBand::Good);
        CHECK(r.words[0].score > 95);
        CHECK(r.advice.empty());
        CHECK(r.accuracy > 95);
        CHECK(r.completeness == 100.0);
    }

    TEST_CASE("final devoicing: bad read as bat") {
        Assessor a;
        setup(a);
        auto r = a.assess("Bad dog.", words({{"bat", 0.0, 0.3}, {"dog", 0.4, 0.7}}));
        const auto& ph = r.words[0].phonemes;
        REQUIRE(ph.size() == 3);
        CHECK(ph[2].substituted);
        REQUIRE(ph[2].advice);
        CHECK(ph[2].advice->id == "final_devoicing");
        CHECK(ph[2].advice->sound_id == "final-voiced");
    }

    TEST_CASE("inserted filler, numbers and pause mapping") {
        Assessor a;
        setup(a);
        auto r = a.assess("The cat. Forty two.",
                          words({{"The", 0.0, 0.2}, {"um", 0.3, 0.5}, {"cat.", 0.6, 0.9}, {"42", 2.5, 3.1}}));
        REQUIRE(r.words.size() == 4);
        for (const auto& w : r.words) CHECK(w.status == WordStatus::Matched);
        CHECK(r.words[2].start == doctest::Approx(2.5));
        CHECK(r.words[3].start == doctest::Approx(2.8));
        REQUIRE(r.inserted.size() == 1);
        CHECK(r.inserted[0].text == "um");
        CHECK(r.inserted[0].after_ref_word == 0);
        CHECK(r.fluency.hesitations.size() == 1);
        REQUIRE(r.fluency.long_pauses.size() == 1);  // 1.6 s after "cat." > 0.5 + 0.5 bonus
        std::string json = to_json(r);
        CHECK(json.find("\"long_pauses\":[{\"after_word\":1,") != std::string::npos);
    }

    TEST_CASE("strictness and options") {
        Assessor a;
        setup(a);
        a.options().set_strictness(Strictness::Strict);
        CHECK(a.options().gop.good_gop == doctest::Approx(1.5));
        a.options().good_threshold = 101;  // nothing can be green
        auto r = a.assess("cat", words({{"cat", 0.0, 0.3}}));
        CHECK(r.words[0].band == ScoreBand::Fair);
    }

    TEST_CASE("warnings and degenerate input") {
        Assessor a;
        setup(a);
        LogPosteriors lp(10, 3);
        auto r = a.assess("Zebra cat", words({{"zebra", 0, 0.3}, {"cat", 0.4, 0.6}}), &lp);
        CHECK_FALSE(r.phoneme_level);
        CHECK(r.warnings.size() == 2);  // no vocab + unknown word
        CHECK(r.words[0].scored_by == ScoredBy::None);
        CHECK(r.words[0].score == doctest::Approx(100));  // matched, ASR confidence 1

        a.set_vocab(PhonemeVocab(kLabels, 0));
        auto r2 = a.assess("cat", words({{"cat", 0, 0.3}}), &lp);  // wrong class count
        CHECK_FALSE(r2.phoneme_level);
        CHECK(r2.warnings.size() == 1);

        auto empty = a.assess("", {});
        CHECK(empty.words.empty());
        CHECK(empty.accuracy == 0.0);
        CHECK(empty.completeness == 0.0);
        auto silent = a.assess("the cat", {});
        CHECK(silent.completeness == 0.0);
        CHECK(silent.fluency_score == 0.0);
        CHECK(silent.words[1].status == WordStatus::Omitted);
    }

    TEST_CASE("JSON output") {
        Assessor a;
        setup(a);
        auto r = a.assess("Waiter: I think the cat sat.\n\xC3\x89mile: Yes", words({{"I", 0.0, 0.2}, {"sink", 0.3, 0.6}, {"the", 0.7, 0.8}, {"cat", 0.9, 1.2}}));
        std::string j = to_json(r);
        CHECK(j.front() == '{');
        CHECK(j.back() == '}');
        CHECK(j.find("\"version\":1") != std::string::npos);
        CHECK(j.find("\"scores\":{\"accuracy\":") != std::string::npos);
        CHECK(j.find("\"status\":\"omitted\"") != std::string::npos);
        CHECK(j.find("\"status\":\"substituted\"") != std::string::npos);
        CHECK(j.find("\"advice_id\":\"th_s\"") != std::string::npos);
        CHECK(j.find("\"color\":\"#C62828\"") == std::string::npos);
        CHECK(j.find("\"color\":\"#9E9E9E\"") != std::string::npos);
        CHECK(j.find("θ звучит как «с»") != std::string::npos);
        CHECK(j.find("\"u16_begin\":8") != std::string::npos);  // "I" after "Waiter: "
        CHECK(j.find("\"start\":null") != std::string::npos);   // omitted words
        // Brackets balance (strings contain none of them in this sample).
        int depth = 0;
        bool in_str = false;
        for (std::size_t i = 0; i < j.size(); ++i) {
            char c = j[i];
            if (in_str) {
                if (c == '\\') ++i;
                else if (c == '"') in_str = false;
                continue;
            }
            if (c == '"') in_str = true;
            if (c == '{' || c == '[') ++depth;
            if (c == '}' || c == ']') --depth;
            CHECK(depth >= 0);
        }
        CHECK(depth == 0);
        std::string pretty = to_json(r, true);
        CHECK(pretty.find("\n  \"scores\": {") != std::string::npos);
    }
}

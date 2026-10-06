#include "doctest.h"
#include "pron/gop.h"
#include "pron/phonemes.h"
#include "test_data.h"

using namespace pron;
using testdata::peaked;

namespace {
int col(const char* arpa) { return column_of_phoneme(phoneme_id(arpa)); }
}  // namespace

TEST_SUITE("gop") {
    TEST_CASE("GOP to score mapping and presets") {
        GopOptions o;  // normal: good 0.5, bad -4
        CHECK(gop_to_score(1.0, o) == doctest::Approx(100));
        CHECK(gop_to_score(-5.0, o) == doctest::Approx(0));
        CHECK(gop_to_score(-1.75, o) == doctest::Approx(50));
        GopOptions strict = GopOptions::preset(Strictness::Strict);
        GopOptions lenient = GopOptions::preset(Strictness::Lenient);
        for (double g : {-3.5, -2.0, 0.0, 1.0}) {
            CHECK(gop_to_score(g, strict) <= gop_to_score(g, o));
            CHECK(gop_to_score(g, lenient) >= gop_to_score(g, o));
        }
        GopOptions degenerate;
        degenerate.good_gop = degenerate.bad_gop = 0.0;
        CHECK(gop_to_score(0.1, degenerate) == 100.0);
        CHECK(gop_to_score(-0.1, degenerate) == 0.0);
    }

    TEST_CASE("correct phoneme scores high") {
        const int C = inventory_columns();
        auto lp = peaked({kBlankColumn, col("TH"), col("TH"), kBlankColumn}, C);
        PhonemeScore s = score_phoneme(lp, phoneme_id("TH"), 1, 3, GopOptions{});
        CHECK(s.gop > 5.0);
        CHECK(s.score == doctest::Approx(100));
        CHECK_FALSE(s.substituted);
        CHECK(s.likely_margin < 0);
        CHECK(s.mean_log_prob == doctest::Approx(std::log(0.9)).epsilon(1e-4));
    }

    TEST_CASE("substituted phoneme: θ pronounced as s") {
        const int C = inventory_columns();
        auto lp = peaked({col("S"), col("S"), col("S")}, C);
        PhonemeScore s = score_phoneme(lp, phoneme_id("TH"), 0, 3, GopOptions{});
        CHECK(s.gop < -5.0);
        CHECK(s.score == doctest::Approx(0));
        CHECK(s.substituted);
        CHECK(s.likely_id == phoneme_id("S"));
        CHECK(s.likely_margin > 0);
    }

    TEST_CASE("blank is not a competitor; ambiguous frames give a middle score") {
        const int C = inventory_columns();
        LogPosteriors lp(2, C);
        for (int t = 0; t < 2; ++t) {
            for (int c = 0; c < C; ++c) lp.at(t, c) = std::log(0.01f / (C - 3));
            lp.at(t, kBlankColumn) = std::log(0.6f);
            lp.at(t, col("IH")) = std::log(0.2f);
            lp.at(t, col("IY")) = std::log(0.19f);
        }
        PhonemeScore s = score_phoneme(lp, phoneme_id("IH"), 0, 2, GopOptions{});
        CHECK(s.gop == doctest::Approx(std::log(0.2 / 0.19)).epsilon(1e-3));
        CHECK(s.score > 80);
        CHECK(s.likely_id == phoneme_id("IY"));
        CHECK_FALSE(s.substituted);
        PhonemeScore s2 = score_phoneme(lp, phoneme_id("IY"), 0, 2, GopOptions{});
        CHECK(s2.gop < 0);
        CHECK(s2.likely_id == phoneme_id("IH"));
        CHECK_FALSE(s2.substituted);  // score still above the substitution threshold
        CHECK(score_phoneme(lp, phoneme_id("IY"), 1, 1, GopOptions{}).score == 0.0);  // empty span
    }

    TEST_CASE("score a full forced alignment") {
        const int C = inventory_columns();
        const int B = kBlankColumn;
        // "think" read as "sink": the θ frames look like s.
        auto lp = peaked({B, col("S"), col("S"), B, col("IH"), col("IH"), col("NG"), col("K"), B}, C);
        std::vector<int> targets = {col("TH"), col("IH"), col("NG"), col("K")};
        auto al = ctc_force_align(lp, targets, B);
        REQUIRE(al.ok);
        auto scores = score_alignment(lp, al, GopOptions{});
        REQUIRE(scores.size() == 4);
        CHECK(scores[0].phoneme_id == phoneme_id("TH"));
        CHECK(scores[0].substituted);
        CHECK(scores[0].likely_id == phoneme_id("S"));
        CHECK(scores[1].score == doctest::Approx(100));
        CHECK(scores[2].score == doctest::Approx(100));
        CHECK(scores[3].score == doctest::Approx(100));
        CHECK(score_alignment(lp, CtcAlignment{}, GopOptions{}).empty());
    }
}

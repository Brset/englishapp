#include "doctest.h"
#include "pron/ctc_align.h"
#include "test_data.h"

using namespace pron;
using testdata::peaked;

TEST_SUITE("ctc") {
    // Small synthetic vocabulary: 0 = blank, 1..4 = labels.
    const int B = 0;

    TEST_CASE("min frames") {
        CHECK(ctc_min_frames({1, 2, 3}) == 3);
        CHECK(ctc_min_frames({1, 1, 2}) == 4);
        CHECK(ctc_min_frames({}) == 0);
    }

    TEST_CASE("aligns a clean peaked sequence") {
        //            t: 0  1  2  3  4  5  6  7  8  9  10
        auto lp = peaked({B, 1, 1, B, 2, 2, 2, 3, B, 4, B}, 5);
        auto a = ctc_force_align(lp, {1, 2, 3, 4}, B);
        REQUIRE(a.ok);
        REQUIRE(a.spans.size() == 4);
        CHECK(a.spans[0].start_frame == 1);
        CHECK(a.spans[0].end_frame == 3);
        CHECK(a.spans[0].region_end == 4);
        CHECK(a.spans[1].start_frame == 4);
        CHECK(a.spans[1].end_frame == 7);
        CHECK(a.spans[2].start_frame == 7);
        CHECK(a.spans[2].end_frame == 8);
        CHECK(a.spans[3].start_frame == 9);
        CHECK(a.spans[3].end_frame == 10);
        CHECK(a.spans[3].region_end == 11);
        CHECK(a.spans[2].column == 3);
        CHECK(a.log_prob == doctest::Approx(11 * std::log(0.9)).epsilon(1e-4));
    }

    TEST_CASE("forced alignment follows the expected sequence even if the audio differs") {
        // The audio says label 4 where label 1 is expected; the aligner still places label 1.
        auto lp = peaked({B, 4, 4, B, 2, 2, B}, 5);
        auto a = ctc_force_align(lp, {1, 2}, B);
        REQUIRE(a.ok);
        CHECK(a.spans[0].end_frame <= a.spans[1].start_frame);
        CHECK(a.spans[1].start_frame >= 3);
        CHECK(a.spans[1].end_frame == 6);
    }

    TEST_CASE("repeated labels need a blank between them") {
        auto lp = peaked({1, B, 1}, 3);
        auto a = ctc_force_align(lp, {1, 1}, B);
        REQUIRE(a.ok);
        CHECK(a.spans[0].start_frame == 0);
        CHECK(a.spans[0].end_frame == 1);
        CHECK(a.spans[1].start_frame == 2);
        // Two frames are not enough for "1 1".
        CHECK_FALSE(ctc_force_align(peaked({1, 1}, 3), {1, 1}, B).ok);
    }

    TEST_CASE("exactly enough frames and no blanks") {
        auto lp = peaked({1, 2, 3}, 4);
        auto a = ctc_force_align(lp, {1, 2, 3}, B);
        REQUIRE(a.ok);
        for (int i = 0; i < 3; ++i) {
            CHECK(a.spans[i].start_frame == i);
            CHECK(a.spans[i].end_frame == i + 1);
        }
    }

    TEST_CASE("invalid input") {
        auto lp = peaked({1, 2}, 3);
        CHECK_FALSE(ctc_force_align(lp, {1, 2, 1}, B).ok);  // too short
        CHECK_FALSE(ctc_force_align(lp, {}, B).ok);
        CHECK_FALSE(ctc_force_align(lp, {0}, B).ok);  // blank in targets
        CHECK_FALSE(ctc_force_align(lp, {7}, B).ok);  // out of range
        CHECK_FALSE(ctc_force_align(LogPosteriors(), {1}, B).ok);
    }

    TEST_CASE("long uniform matrix still produces monotonic spans") {
        LogPosteriors lp(50, 6);
        for (auto& v : lp.data) v = static_cast<float>(std::log(1.0 / 6));
        auto a = ctc_force_align(lp, {1, 2, 3, 4, 5}, B);
        REQUIRE(a.ok);
        for (std::size_t i = 1; i < a.spans.size(); ++i)
            CHECK(a.spans[i].start_frame >= a.spans[i - 1].end_frame);
    }
}

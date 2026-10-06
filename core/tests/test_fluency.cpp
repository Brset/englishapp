#include "doctest.h"
#include "pron/fluency.h"

using namespace pron;

namespace {
// Words of 0.3 s separated by `gap` seconds.
std::vector<TimedWord> evenly(const std::vector<std::string>& w, double gap) {
    std::vector<TimedWord> out;
    double t = 0.0;
    for (const auto& s : w) {
        out.push_back({s, t, t + 0.3});
        t += 0.3 + gap;
    }
    return out;
}
}  // namespace

TEST_SUITE("fluency") {
    TEST_CASE("words per minute") {
        // 10 words, 0.3 s each + 0.1 s gaps: 10*0.3 + 9*0.1 = 3.9 s -> 153.8 wpm
        auto w = evenly({"a", "b", "c", "d", "e", "f", "g", "h", "i", "j"}, 0.1);
        auto r = compute_fluency(w);
        CHECK(r.word_count == 10);
        CHECK(r.speech_seconds == doctest::Approx(3.9));
        CHECK(r.words_per_minute == doctest::Approx(10 * 60.0 / 3.9));
        CHECK(r.long_pauses.empty());
        CHECK(r.repetitions.empty());
        CHECK(r.hesitations.empty());
        CHECK(r.score == doctest::Approx(100));
    }

    TEST_CASE("long pauses, configurable threshold and sentence bonus") {
        std::vector<TimedWord> w = {{"i", 0.0, 0.3}, {"like", 0.4, 0.7}, {"tea", 1.5, 1.8}, {"yes", 2.6, 2.9}};
        auto r = compute_fluency(w);
        REQUIRE(r.long_pauses.size() == 2);
        CHECK(r.long_pauses[0].after_word == 1);
        CHECK(r.long_pauses[0].duration() == doctest::Approx(0.8));
        CHECK(r.articulation_seconds == doctest::Approx(2.9 - 1.6));
        CHECK(r.articulation_wpm > r.words_per_minute);
        // Sentence ends after "tea": the 0.8 s pause there is allowed.
        auto r2 = compute_fluency(w, FluencyOptions{}, {false, false, true, false});
        CHECK(r2.long_pauses.size() == 1);
        FluencyOptions o;
        o.long_pause_seconds = 1.0;
        CHECK(compute_fluency(w, o).long_pauses.empty());
        CHECK(r.score < r2.score);
    }

    TEST_CASE("repetitions and hesitations") {
        auto w = evenly({"the", "the", "cat", "um", "sat", "on", "the", "on", "the", "mat"}, 0.1);
        auto r = compute_fluency(w);
        CHECK(r.hesitations == std::vector<int>{3});
        CHECK(r.repetitions == std::vector<int>{1, 7, 8});
        CHECK(r.word_count == 6);
        CHECK(r.score < 100);
        CHECK(is_filler("erm", FluencyOptions{}));
        CHECK_FALSE(is_filler("cat", FluencyOptions{}));
    }

    TEST_CASE("bigram repetition with a filler inside") {
        auto w = evenly({"in", "the", "in", "uh", "the", "box"}, 0.1);
        auto r = compute_fluency(w);
        CHECK(r.repetitions == std::vector<int>{2, 4});
        CHECK(r.hesitations == std::vector<int>{3});
        CHECK(r.word_count == 3);
    }

    TEST_CASE("slow reading is penalized") {
        auto fast = compute_fluency(evenly({"a", "b", "c", "d", "e", "f"}, 0.1));
        auto slow = compute_fluency(evenly({"a", "b", "c", "d", "e", "f"}, 0.45));
        CHECK(slow.words_per_minute < 90);
        CHECK(slow.long_pauses.empty());
        CHECK(slow.score < fast.score);
    }

    TEST_CASE("empty input") {
        auto r = compute_fluency({});
        CHECK(r.word_count == 0);
        CHECK(r.words_per_minute == 0.0);
        CHECK(r.score == 0.0);
    }
}

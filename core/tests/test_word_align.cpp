#include "doctest.h"
#include "pron/word_align.h"

using namespace pron;
using V = std::vector<std::string>;

TEST_SUITE("word_align") {
    TEST_CASE("similarity") {
        CHECK(word_similarity("think", "think") == doctest::Approx(1.0));
        CHECK(word_similarity("think", "sink") == doctest::Approx(0.6));
        CHECK(word_similarity("abc", "xyz") == doctest::Approx(0.0));
        CHECK(levenshtein("kitten", "sitting") == 3);
        CHECK(levenshtein("", "abc") == 3);
    }

    TEST_CASE("all matched") {
        auto a = align_words({"the", "cat", "sat"}, {"the", "cat", "sat"});
        for (int i = 0; i < 3; ++i) {
            CHECK(a.ref[i].status == WordStatus::Matched);
            CHECK(a.ref[i].hyp_index == i);
        }
        CHECK(a.inserted.empty());
        CHECK(a.cost == doctest::Approx(0.0));
    }

    TEST_CASE("substitution with fuzzy similarity") {
        auto a = align_words({"i", "think", "so"}, {"i", "sink", "so"});
        CHECK(a.ref[1].status == WordStatus::Substituted);
        CHECK(a.ref[1].hyp_index == 1);
        CHECK(a.ref[1].similarity == doctest::Approx(0.6));
        CHECK(std::string(to_string(a.ref[1].status)) == "substituted");
    }

    TEST_CASE("omitted word") {
        auto a = align_words({"the", "cat", "sat", "on", "the", "mat"}, {"the", "cat", "on", "the", "mat"});
        CHECK(a.ref[2].status == WordStatus::Omitted);
        CHECK(a.ref[2].hyp_index == -1);
        CHECK(a.ref[3].hyp_index == 2);
        CHECK(a.ref[5].status == WordStatus::Matched);
        CHECK(a.inserted.empty());
    }

    TEST_CASE("inserted words and repetitions") {
        auto a = align_words({"the", "cat", "sat"}, {"the", "um", "cat", "cat", "sat"});
        CHECK(a.ref[0].hyp_index == 0);
        CHECK(a.ref[2].hyp_index == 4);
        CHECK(a.ref[1].status == WordStatus::Matched);
        CHECK(a.inserted.size() == 2);
        CHECK(a.inserted[0] == 1);
    }

    TEST_CASE("dissimilar word in place of reference word is a substitution") {
        auto a = align_words({"a", "red", "car"}, {"a", "blue", "car"});
        CHECK(a.ref[1].status == WordStatus::Substituted);
        CHECK(a.inserted.empty());
    }

    TEST_CASE("fuzzy match prefers the similar neighbour") {
        // "wine" should pair with "vine", not with "the".
        auto a = align_words({"the", "wine", "is", "red"}, {"vine", "is", "red"});
        CHECK(a.ref[0].status == WordStatus::Omitted);
        CHECK(a.ref[1].status == WordStatus::Substituted);
        CHECK(a.ref[1].hyp_index == 0);
    }

    TEST_CASE("empty inputs") {
        auto a = align_words({"one", "two"}, {});
        CHECK(a.ref[0].status == WordStatus::Omitted);
        CHECK(a.ref[1].status == WordStatus::Omitted);
        auto b = align_words({}, {"x", "y"});
        CHECK(b.ref.empty());
        CHECK(b.inserted == std::vector<int>{0, 1});
    }
}

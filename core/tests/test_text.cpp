#include "doctest.h"
#include "pron/assessment.h"
#include "pron/text.h"

using namespace pron;

namespace {
std::vector<std::string> norms(const std::vector<Token>& t) {
    std::vector<std::string> out;
    for (const auto& x : t) out.push_back(x.norm);
    return out;
}
using V = std::vector<std::string>;
}  // namespace

TEST_SUITE("text") {
    TEST_CASE("basic tokenization keeps byte offsets") {
        std::string s = "Hello, world! It's   fine.";
        auto t = tokenize(s);
        REQUIRE(t.size() == 4);
        CHECK(norms(t) == V{"hello", "world", "it's", "fine"});
        for (const auto& tok : t) CHECK(s.substr(tok.begin, tok.end - tok.begin) == tok.text);
        CHECK(t[0].text == "Hello");
        CHECK(t[2].begin == 14);
        CHECK(t[2].end == 18);
    }

    TEST_CASE("typographic apostrophes and quotes") {
        auto t = tokenize("\xE2\x80\x9C" "Don\xE2\x80\x99t go,\xE2\x80\x9D she said. James' book; 'quoted'");
        CHECK(norms(t) == V{"don't", "go", "she", "said", "james", "book", "quoted"});
        CHECK(t[0].text == "Don\xE2\x80\x99t");
        CHECK(normalize_word("Don\xE2\x80\x99T") == "don't");
        CHECK(normalize_word(" Hello,") == "hello");
        CHECK(normalize_word("...") == "");
    }

    TEST_CASE("hyphens and dashes") {
        std::string s = "A well-known man \xE2\x80\x94 twenty-one - years";
        auto t = tokenize(s);
        CHECK(norms(t) == V{"a", "well", "known", "man", "twenty", "one", "years"});
        CHECK(t[1].text == "well");
        CHECK(t[2].text == "known");
        TokenizeOptions o;
        o.split_hyphens = false;
        CHECK(norms(tokenize("well-known", o)) == V{"well-known"});
    }

    TEST_CASE("numbers to words") {
        CHECK(number_to_words(0) == "zero");
        CHECK(number_to_words(13) == "thirteen");
        CHECK(number_to_words(42) == "forty two");
        CHECK(number_to_words(100) == "one hundred");
        CHECK(number_to_words(1001) == "one thousand one");
        CHECK(number_to_words(2500000) == "two million five hundred thousand");
        CHECK(number_to_words(-7) == "minus seven");
        CHECK(ordinal_to_words(1) == "first");
        CHECK(ordinal_to_words(3) == "third");
        CHECK(ordinal_to_words(12) == "twelfth");
        CHECK(ordinal_to_words(20) == "twentieth");
        CHECK(ordinal_to_words(21) == "twenty first");
        CHECK(year_to_words(1990) == "nineteen ninety");
        CHECK(year_to_words(1905) == "nineteen oh five");
        CHECK(year_to_words(1900) == "nineteen hundred");
        CHECK(year_to_words(2008) == "two thousand eight");
        CHECK(year_to_words(2024) == "twenty twenty four");
    }

    TEST_CASE("numbers inside text expand with shared offsets") {
        std::string s = "I have 42 cats, the 3rd in 1990, 1,000 mice and 3.5 kg in the 1980s.";
        auto t = tokenize(s);
        CHECK(norms(t) == V{"i", "have", "forty", "two", "cats", "the", "third", "in", "nineteen", "ninety",
                            "one", "thousand", "mice", "and", "three", "point", "five", "kg", "in", "the",
                            "nineteen", "eighties"});
        CHECK(t[2].from_number);
        CHECK(t[2].text == "42");
        CHECK(t[3].text == "42");
        CHECK(t[2].begin == t[3].begin);
        CHECK(!t[0].from_number);
        CHECK(norms(tokenize("mp3")) == V{"mp", "three"});
        TokenizeOptions o;
        o.expand_numbers = false;
        CHECK(norms(tokenize("42 cats", o)) == V{"42", "cats"});
    }

    TEST_CASE("dialogue speaker labels are skipped") {
        std::string s = "Waiter: Hello! What would you like?\nEmma: A cup of tea.\nMr. Smith: Yes.\nAt 10:30 we go: now";
        auto t = tokenize(s);
        CHECK(norms(t) == V{"hello", "what", "would", "you", "like", "a", "cup", "of", "tea", "yes", "at", "ten",
                            "thirty", "we", "go", "now"});
        TokenizeOptions o;
        o.skip_speaker_labels = false;
        CHECK(tokenize(s, o).front().norm == "waiter");
    }

    TEST_CASE("non-ASCII letters and UTF-16 offsets") {
        std::string s = "Caf\xC3\xA9 \xC3\x89t\xC3\xA9 \xF0\x9F\x98\x80 ok";  // "Café Été 😀 ok"
        auto t = tokenize(s);
        CHECK(norms(t) == V{"caf\xC3\xA9", "\xC3\xA9t\xC3\xA9", "ok"});
        CHECK(utf16_offset(s, t[1].begin) == 5);
        CHECK(utf16_offset(s, t[2].begin) == 12);  // emoji counts as 2 UTF-16 units
        CHECK(tokenize("").empty());
        CHECK(tokenize(" ,.!? \xE2\x80\x94 ").empty());
    }

    TEST_CASE("invalid UTF-8 does not crash") {
        std::string s = "ab\xFF\xFE cd \xC3";
        auto t = tokenize(s);
        REQUIRE(t.size() >= 2);
        CHECK(t[0].norm == "ab");
    }
}

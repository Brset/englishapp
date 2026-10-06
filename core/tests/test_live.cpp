#include "doctest.h"
#include "pron/live_tracker.h"

#include <string>
#include <vector>

using namespace pron;
using V = std::vector<std::string>;

namespace {
const char* kText = "The three brothers walked along a winding road, and they were very tired.";
// words: the three brothers walked along a winding road and they were very tired  (13)

int count_state(const LiveState& s, LiveWordState w) {
    int n = 0;
    for (auto x : s.words) if (x == w) ++n;
    return n;
}
V prefix(const V& v, size_t n) { return V(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(n)); }
}  // namespace

TEST_SUITE("live") {
    TEST_CASE("perfect reading") {
        LiveTracker t(kText);
        REQUIRE(t.word_count() == 13);
        V hyp = {"THE", "THREE", "BROTHERS", "WALKED", "ALONG", "A", "WINDING", "ROAD", "AND", "THEY", "WERE", "VERY", "TIRED"};
        int prev = 0;
        for (size_t n = 1; n <= hyp.size(); ++n) {
            const LiveState& s = t.update(prefix(hyp, n));
            CHECK(s.cursor == static_cast<int>(n));
            CHECK(s.cursor >= prev);
            prev = s.cursor;
        }
        const LiveState& s = t.state();
        CHECK(s.done);
        CHECK(s.cursor == 13);
        CHECK(count_state(s, LiveWordState::Read) == 13);
        CHECK(count_state(s, LiveWordState::Skipped) == 0);
    }

    TEST_CASE("growing hypothesis with unfinished last word") {
        LiveTracker t(kText);
        t.update({"THE", "THREE", "BROT"});
        CHECK(t.state().cursor == 3);
        t.update({"THE", "THREE", "BROTHERS", "WALK"});
        CHECK(t.state().cursor == 4);
        CHECK(t.state().words[4] == LiveWordState::Current);
        CHECK(t.state().words[5] == LiveWordState::Pending);
        CHECK(t.state().scroll_to == 4);
        CHECK_FALSE(t.state().done);
    }

    TEST_CASE("changed_from") {
        LiveTracker t(kText);
        CHECK(t.update({}).changed_from == 0);  // first report paints everything
        CHECK(t.update({"the", "three"}).changed_from == 0);
        CHECK(t.update({"the", "three", "brothers"}).changed_from == 2);
        CHECK(t.update({"the", "three", "brothers"}).changed_from == t.word_count());
    }

    TEST_CASE("misrecognised short words and missing function words are read") {
        LiveTracker t(kText);
        // "a" -> "uh", "the" missing at start
        t.update({"THREE", "BROTHERS", "WALKED", "ALONG", "UH", "WINDING", "ROAD"});
        const LiveState& s = t.state();
        CHECK(s.cursor == 8);
        CHECK(s.words[0] == LiveWordState::Read);  // "the" never heard but function word before heard word
        CHECK(s.words[5] == LiveWordState::Read);  // "a"
        CHECK(count_state(s, LiveWordState::Skipped) == 0);
    }

    TEST_CASE("skipped content word is marked skipped") {
        LiveTracker t(kText);
        t.update({"the", "three", "brothers", "along", "a", "winding"});
        const LiveState& s = t.state();
        CHECK(s.words[3] == LiveWordState::Skipped);  // "walked"
        CHECK(s.cursor == 7);
        CHECK(count_state(s, LiveWordState::Skipped) == 1);
    }

    TEST_CASE("a misrecognised content word is not reported as skipped") {
        LiveTracker t(kText);
        t.update({"the", "tree", "bothers", "walked", "along"});  // "three"/"brothers" garbled but fuzzy
        CHECK(t.state().cursor == 5);
        LiveTracker u("Think about the weather today");
        u.update({"OUT", "THE", "WEATHER", "TODAY"});  // "think" replaced by a wrong word
        CHECK(u.state().done);
        CHECK(count_state(u.state(), LiveWordState::Skipped) == 0);
    }

    TEST_CASE("repeating a phrase does not move the cursor back") {
        LiveTracker t(kText);
        t.update({"the", "three", "brothers", "walked", "along"});
        CHECK(t.state().cursor == 5);
        t.update({"the", "three", "brothers", "walked", "along", "the", "three", "brothers", "walked", "along"});
        CHECK(t.state().cursor == 5);
        t.update({"the", "three", "brothers", "walked", "along", "the", "three", "brothers", "walked", "along",
                  "a", "winding"});
        CHECK(t.state().cursor == 7);
        CHECK(count_state(t.state(), LiveWordState::Skipped) == 0);
    }

    TEST_CASE("recognizer garbage does not jump far") {
        LiveTracker t(kText);
        t.update({"the", "three"});
        CHECK(t.state().cursor == 2);
        t.update({"the", "three", "xyzzy", "banana", "quux", "tired"});  // lone far-away match: needs confirmation
        CHECK(t.state().cursor == 2);
        t.update({"the", "three", "xyzzy", "banana", "quux", "tired", "hello", "world"});
        CHECK(t.state().cursor == 2);
        CHECK(count_state(t.state(), LiveWordState::Skipped) == 0);
    }

    TEST_CASE("confirmed jump over several words is accepted") {
        LiveTracker t(kText);
        t.update({"the", "three", "winding", "road"});
        CHECK(t.state().cursor == 8);
        CHECK(t.state().words[2] == LiveWordState::Skipped);
        CHECK(t.state().words[3] == LiveWordState::Skipped);
    }

    TEST_CASE("numbers and punctuation") {
        LiveTracker t("I have 3 apples, don't I?");
        REQUIRE(t.word_count() == 6);
        t.update({"I", "HAVE", "THREE", "APPLES", "DON'T"});
        CHECK(t.state().cursor == 6);
        CHECK(t.state().done);
    }

    TEST_CASE("done when only function words remain") {
        LiveTracker t("She went to the market and so on");
        t.update({"she", "went", "to", "the", "market"});
        CHECK(t.state().done);
        CHECK(t.state().cursor == t.word_count());
    }

    TEST_CASE("set_cursor") {
        LiveTracker t(kText);
        t.update({"the", "three", "brothers"});
        t.set_cursor(8);
        CHECK(t.state().cursor == 8);
        CHECK(t.state().words[8] == LiveWordState::Current);
        CHECK(t.state().words[2] == LiveWordState::Read);
        // old hypothesis words are ignored; new words continue from 8
        t.update({"the", "three", "brothers", "and", "they"});
        CHECK(t.state().cursor == 10);
        t.set_cursor(0);
        CHECK(t.state().cursor == 0);
        CHECK(count_state(t.state(), LiveWordState::Read) == 0);
        t.update({"the", "three", "brothers", "and", "they", "the", "three"});
        CHECK(t.state().cursor == 2);
        t.set_cursor(99);
        CHECK(t.state().done);
    }

    TEST_CASE("json shape") {
        LiveTracker t("Hi, you.");
        t.update({"hi"});
        std::string j = live_state_json(t, "HI");
        CHECK(j.find("\"cursor\":1") != std::string::npos);
        CHECK(j.find("\"partial\":\"HI\"") != std::string::npos);
        CHECK(j.find("\"u16_begin\":4") != std::string::npos);
        CHECK(j.find("\"state\":\"current\"") != std::string::npos);
        CHECK(j.find("\"changed_from\":0") != std::string::npos);
    }
}

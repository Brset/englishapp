#include "doctest.h"
#include "pron/pron_c.h"
#include "test_data.h"

#include <cmath>
#include <cstring>
#include <string>
#include <vector>

extern "C" int pron_c_header_check(void);

namespace {
std::string take(char* s) {
    REQUIRE(s != nullptr);
    std::string out(s);
    pron_free_string(s);
    return out;
}
}  // namespace

TEST_SUITE("c_api") {
    TEST_CASE("header compiles as C and works from C") { CHECK(pron_c_header_check() == 1); }

    TEST_CASE("full flow") {
        CHECK(std::string(pron_version()) == "0.1.0");
        pron_assessor* a = pron_assessor_create();
        REQUIRE(a);
        CHECK(std::string(pron_last_error(a)).empty());
        const char* dict = testdata::kCmuSample;
        CHECK(pron_assessor_load_cmudict_text(a, dict, std::strlen(dict)) == 33);
        CHECK(pron_assessor_add_pronunciation(a, "zorb", "Z AO1 R B") == 0);
        CHECK(pron_assessor_add_pronunciation(a, "zorb", "Z QQ") == -1);
        CHECK(std::string(pron_last_error(a)).find("invalid ARPAbet") != std::string::npos);

        const char* labels[] = {"<pad>", "θ", "s", "ɪ", "ŋ", "k", "<unk>"};
        CHECK(pron_assessor_set_phoneme_vocab(a, labels, 7, 0) == 1);  // <unk> unmapped
        CHECK(pron_assessor_set_phoneme_vocab(a, labels, 7, 9) == -1);
        CHECK(pron_assessor_set_phoneme_vocab(a, labels, 7, 0) == 1);
        CHECK(pron_assessor_set_accent(a, "gb") == 0);
        CHECK(pron_assessor_set_accent(a, "any") == 0);
        CHECK(pron_assessor_set_accent(a, "mars") == -1);
        {
            std::string vj = take(pron_assessor_phoneme_vocab_json(a));
            CHECK(vj == "{\"size\":7,\"mapped\":5,\"unmapped\":[\"<unk>\"]}");
        }
        pron_assessor_set_strictness(a, PRON_STRICTNESS_STRICT);
        pron_assessor_set_long_pause(a, 0.7);

        // Posteriors: "think" pronounced as "sink".
        const int T = 12, C = 7;
        std::vector<float> lp(T * C, std::log(0.01f));
        int dom[T] = {0, 2, 2, 0, 3, 3, 4, 5, 0, 0, 0, 0};
        for (int t = 0; t < T; ++t) lp[t * C + dom[t]] = std::log(0.94f);
        pron_word w[] = {{"Think", 0.0, 0.2, 0.9f}};
        std::string json = take(pron_assess(a, "Think.", w, 1, lp.data(), T, C, 0.02));
        CHECK(json.find("\"phoneme_level\":true") != std::string::npos);
        CHECK(json.find("\"advice_id\":\"th_s\"") != std::string::npos);
        CHECK(json.find("\"actual_ipa\":\"s\"") != std::string::npos);
        CHECK(json.find("Кончик языка") != std::string::npos);

        // Without posteriors.
        json = take(pron_assess(a, "Zorb cat", w, 0, nullptr, 0, 0, 0));
        CHECK(json.find("\"completeness\":0") != std::string::npos);

        // Lookup.
        json = take(pron_assessor_lookup(a, "Think"));
        CHECK(json == "{\"word\":\"think\",\"source\":\"cmudict\",\"arpabet\":\"TH IH1 NG K\",\"ipa\":\"ˈθɪŋk\",\"stress_syllable\":0}");
        json = take(pron_assessor_lookup(a, "qwerty"));
        CHECK(json.find("\"source\":\"none\"") != std::string::npos);

        // Errors.
        CHECK(pron_assess(a, nullptr, w, 1, nullptr, 0, 0, 0) == nullptr);
        CHECK(std::string(pron_last_error(a)) == "invalid arguments");
        CHECK(pron_assess(a, "x", nullptr, 2, nullptr, 0, 0, 0) == nullptr);
        CHECK(pron_assessor_load_cmudict_file(a, "/nonexistent/cmudict") == -1);
        CHECK(std::string(pron_last_error(a)).find("cannot open") != std::string::npos);
        pron_assessor_destroy(a);
    }

    TEST_CASE("null handles are safe") {
        CHECK(pron_assess(nullptr, "x", nullptr, 0, nullptr, 0, 0, 0) == nullptr);
        CHECK(pron_assessor_load_cmudict_file(nullptr, "x") == -1);
        CHECK(pron_assessor_lookup(nullptr, "x") == nullptr);
        CHECK(std::string(pron_last_error(nullptr)) == "null handle");
        pron_assessor_set_strictness(nullptr, 1);
        pron_assessor_destroy(nullptr);
        pron_free_string(nullptr);
        CHECK(pron_tokenize(nullptr) == nullptr);
    }

    TEST_CASE("tokenize") {
        std::string json = take(pron_tokenize("Emma: Hi, 2 cats!"));
        CHECK(json.find("\"norm\":\"hi\"") != std::string::npos);
        CHECK(json.find("\"norm\":\"two\",\"byte_begin\":10,\"byte_end\":11") != std::string::npos);
        CHECK(json.find("emma") == std::string::npos);
    }
}

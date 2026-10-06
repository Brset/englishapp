#include "pron/pron_c.h"

#include <cstdlib>
#include <cstring>
#include <exception>
#include <string>

#include "pron/assessment.h"
#include "pron/json_writer.h"
#include "pron/result_json.h"
#include "pron/version.h"

struct pron_assessor {
    pron::Assessor assessor;
    std::string error;
};

namespace {

char* dup_string(const std::string& s) {
    char* p = static_cast<char*>(std::malloc(s.size() + 1));
    if (!p) return nullptr;
    std::memcpy(p, s.c_str(), s.size() + 1);
    return p;
}

}  // namespace

extern "C" {

const char* pron_version(void) { return PRON_VERSION_STRING; }

pron_assessor* pron_assessor_create(void) {
    try {
        return new pron_assessor();
    } catch (...) {
        return nullptr;
    }
}

void pron_assessor_destroy(pron_assessor* a) { delete a; }

const char* pron_last_error(const pron_assessor* a) { return a ? a->error.c_str() : "null handle"; }

int pron_assessor_load_cmudict_file(pron_assessor* a, const char* path_utf8) {
    if (!a || !path_utf8) return -1;
    try {
        pron::CmuDict::LoadStats st;
        if (!a->assessor.dict().load_file(path_utf8, &st)) {
            a->error = std::string("cannot open ") + path_utf8;
            return -1;
        }
        a->error.clear();
        return static_cast<int>(st.entries);
    } catch (const std::exception& e) {
        a->error = e.what();
        return -1;
    }
}

int pron_assessor_load_cmudict_text(pron_assessor* a, const char* text, size_t length) {
    if (!a || (!text && length)) return -1;
    try {
        auto st = a->assessor.dict().load_from_string(std::string(text ? text : "", length));
        a->error.clear();
        return static_cast<int>(st.entries);
    } catch (const std::exception& e) {
        a->error = e.what();
        return -1;
    }
}

int pron_assessor_add_pronunciation(pron_assessor* a, const char* word, const char* arpabet) {
    if (!a || !word || !arpabet) return -1;
    try {
        pron::Pronunciation p;
        if (!pron::parse_arpabet(arpabet, p) || p.empty()) {
            a->error = std::string("invalid ARPAbet: ") + arpabet;
            return -1;
        }
        a->assessor.dict().add(word, p);
        a->error.clear();
        return 0;
    } catch (const std::exception& e) {
        a->error = e.what();
        return -1;
    }
}

int pron_assessor_set_phoneme_vocab(pron_assessor* a, const char* const* labels, int count, int blank_index) {
    if (!a || !labels || count <= 0 || blank_index < 0 || blank_index >= count) {
        if (a) a->error = "invalid vocabulary";
        return -1;
    }
    try {
        std::vector<std::string> l;
        l.reserve(static_cast<std::size_t>(count));
        for (int i = 0; i < count; ++i) l.emplace_back(labels[i] ? labels[i] : "");
        pron::PhonemeVocab v(std::move(l), blank_index);
        int unmapped = static_cast<int>(v.unmapped_labels().size());
        a->assessor.set_vocab(std::move(v));
        a->error.clear();
        return unmapped;
    } catch (const std::exception& e) {
        a->error = e.what();
        return -1;
    }
}

void pron_assessor_set_strictness(pron_assessor* a, int strictness) {
    if (!a) return;
    pron::Strictness s = strictness <= PRON_STRICTNESS_LENIENT  ? pron::Strictness::Lenient
                         : strictness >= PRON_STRICTNESS_STRICT ? pron::Strictness::Strict
                                                                : pron::Strictness::Normal;
    a->assessor.options().set_strictness(s);
}

void pron_assessor_set_long_pause(pron_assessor* a, double seconds) {
    if (!a || !(seconds > 0)) return;
    a->assessor.options().fluency.long_pause_seconds = seconds;
}

char* pron_assess(pron_assessor* a, const char* reference, const pron_word* words, int n_words,
                  const float* log_posteriors, int n_frames, int n_classes, double frame_seconds) {
    if (!a) return nullptr;
    if (!reference || n_words < 0 || (n_words > 0 && !words)) {
        a->error = "invalid arguments";
        return nullptr;
    }
    try {
        std::vector<pron::AsrWord> asr;
        asr.reserve(static_cast<std::size_t>(n_words));
        for (int i = 0; i < n_words; ++i) {
            pron::AsrWord w;
            w.text = words[i].text ? words[i].text : "";
            w.start = words[i].start;
            w.end = words[i].end;
            w.probability = words[i].probability;
            asr.push_back(std::move(w));
        }
        pron::LogPosteriors lp;
        const pron::LogPosteriors* lpp = nullptr;
        if (log_posteriors && n_frames > 0 && n_classes > 0) {
            lp = pron::LogPosteriors(n_frames, n_classes, frame_seconds > 0 ? frame_seconds : 0.02);
            std::memcpy(lp.data.data(), log_posteriors, lp.data.size() * sizeof(float));
            lpp = &lp;
        }
        pron::AssessmentResult r = a->assessor.assess(reference, asr, lpp);
        a->error.clear();
        return dup_string(pron::to_json(r));
    } catch (const std::exception& e) {
        a->error = e.what();
        return nullptr;
    }
}

char* pron_tokenize(const char* text) {
    if (!text) return nullptr;
    try {
        std::string s(text);
        return dup_string(pron::tokens_to_json(s, pron::tokenize(s)));
    } catch (...) {
        return nullptr;
    }
}

char* pron_assessor_lookup(pron_assessor* a, const char* word) {
    if (!a || !word) return nullptr;
    try {
        std::string norm = pron::normalize_word(word);
        pron::Pronunciation p;
        pron::PronSource src = a->assessor.lexicon().lookup(norm, p);
        pron::JsonWriter j;
        j.begin_object();
        j.kv("word", norm);
        j.kv("source", pron::to_string(src));
        j.kv("arpabet", pron::to_arpabet_string(p, true));
        j.kv("ipa", pron::to_ipa_string(p, true));
        j.kv("stress_syllable", pron::primary_stress_syllable(p));
        j.end_object();
        a->error.clear();
        return dup_string(j.str());
    } catch (const std::exception& e) {
        a->error = e.what();
        return nullptr;
    }
}

void pron_free_string(char* s) { std::free(s); }

}  // extern "C"

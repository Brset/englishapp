// Full model-independent assessment: reference text + ASR words (+ optional phoneme posteriors)
// -> per-word / per-phoneme scores, totals and Russian advice.
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "pron/advice.h"
#include "pron/backends.h"
#include "pron/cmudict.h"
#include "pron/fluency.h"
#include "pron/gop.h"
#include "pron/lexicon.h"
#include "pron/phonemes.h"
#include "pron/posteriors.h"
#include "pron/text.h"
#include "pron/word_align.h"

namespace pron {

enum class ScoreBand { Good, Fair, Poor, Omitted };
const char* to_string(ScoreBand b);
const char* band_color(ScoreBand b);  // "#RRGGBB" suggestion for UI

// How phoneme scores of a word were obtained.
enum class ScoredBy {
    None,  // no phoneme detail (unknown word or omitted)
    Diff,  // derived from the dictionary pronunciation of the word the ASR heard instead
    Gop,   // forced alignment + GOP on phoneme posteriors
};
const char* to_string(ScoredBy s);

struct PhonemeResult {
    std::string arpabet;  // expected phoneme
    std::string ipa;
    int stress = -1;
    double score = -1.0;  // 0..100, -1 if not scored
    double gop = 0.0;     // only meaningful when ScoredBy::Gop
    double start = -1.0;  // seconds in the recording, -1 if unknown
    double end = -1.0;
    bool substituted = false;
    std::string actual_arpabet;  // likely produced phoneme ("" if unknown / omitted)
    std::string actual_ipa;
    std::optional<Advice> advice;
};

struct WordResult {
    int index = 0;           // reference word index
    std::string text;        // surface text from the reference
    std::string norm;
    std::size_t byte_begin = 0, byte_end = 0;  // UTF-8 byte offsets in the reference
    std::size_t u16_begin = 0, u16_end = 0;    // UTF-16 code unit offsets (C#, Java/Kotlin strings)
    WordStatus status = WordStatus::Omitted;
    std::string recognized;  // normalized recognized word ("" when omitted)
    double similarity = 0.0;
    double start = -1.0, end = -1.0;  // seconds, -1 when omitted
    double score = 0.0;
    ScoreBand band = ScoreBand::Omitted;
    PronSource pron_source = PronSource::None;
    std::string expected_ipa;
    int stress_syllable = -1;
    ScoredBy scored_by = ScoredBy::None;
    std::vector<PhonemeResult> phonemes;
};

struct InsertedWord {
    int hyp_index = 0;
    std::string text;
    double start = 0.0, end = 0.0;
    int after_ref_word = -1;  // reference word it follows (-1 = before the first word)
};

struct AdviceSummary {
    Advice advice;
    int count = 0;
    std::vector<int> words;  // reference word indices
};

struct AssessmentResult {
    std::string reference;
    std::vector<WordResult> words;
    std::vector<InsertedWord> inserted;
    std::vector<int> hyp_to_ref;  // for each recognized word: aligned reference index or -1
    FluencyResult fluency;
    double accuracy = 0.0;      // 0..100, mean score of read words
    double completeness = 0.0;  // 0..100, share of reference words that were read
    double fluency_score = 0.0; // 0..100
    double overall = 0.0;       // weighted combination
    bool phoneme_level = false; // posteriors were available and used
    // Diagnostics of the phoneme-posterior path.
    int post_frames = 0, post_classes = 0;
    bool post_used = false;
    std::string post_reason;    // "ok" or why posteriors were not used
    std::vector<AdviceSummary> advice;  // sorted by frequency
    std::vector<std::string> warnings;
};

struct AssessmentOptions {
    TokenizeOptions tokenize;
    WordAlignOptions align;
    GopOptions gop;
    FluencyOptions fluency;
    double good_threshold = 80.0;   // word score >= -> Good (green)
    double fair_threshold = 60.0;   // word score >= -> Fair (yellow), else Poor (red)
    double substituted_word_cap = 60.0;  // max score of a word the ASR heard as another word
    double substituted_phone_word_cap = 75.0;  // max score of a word with a substituted phoneme
    double word_padding_seconds = 0.08;  // widen ASR word spans before forced alignment
    double advice_score_threshold = 70.0;  // phonemes below this get advice
    Accent accent = Accent::Any;  // which pronunciation variants count as correct
    double weight_accuracy = 0.5, weight_completeness = 0.25, weight_fluency = 0.25;

    void set_strictness(Strictness s) { gop = GopOptions::preset(s); }
};

class Assessor {
public:
    Assessor();
    Assessor(const Assessor&) = delete;  // lexicon_ points into dict_
    Assessor& operator=(const Assessor&) = delete;

    CmuDict& dict() { return dict_; }
    const CmuDict& dict() const { return dict_; }
    void set_g2p(IG2P* g2p) { lexicon_.set_g2p(g2p); }
    const Lexicon& lexicon() const { return lexicon_; }
    void set_vocab(PhonemeVocab v) { vocab_ = std::move(v); has_vocab_ = vocab_.size() > 0; }
    const PhonemeVocab& vocab() const { return vocab_; }
    AssessmentOptions& options() { return opt_; }
    const AssessmentOptions& options() const { return opt_; }
    AdviceEngine& advice_engine() { return advice_; }

    // `posteriors` are raw model log-posteriors (frames x vocab labels) for the whole
    // recording, aligned in time with the ASR word timestamps; pass nullptr to score
    // from ASR output only.
    AssessmentResult assess(const std::string& reference, const std::vector<AsrWord>& recognized,
                            const LogPosteriors* posteriors = nullptr) const;

private:
    CmuDict dict_;
    Lexicon lexicon_;
    PhonemeVocab vocab_;
    bool has_vocab_ = false;
    AssessmentOptions opt_;
    AdviceEngine advice_;
};

// UTF-16 code unit offset corresponding to a UTF-8 byte offset.
std::size_t utf16_offset(const std::string& utf8, std::size_t byte_offset);

}  // namespace pron

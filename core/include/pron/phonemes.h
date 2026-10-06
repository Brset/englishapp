// Phoneme inventory: ARPAbet (CMUdict) <-> IPA, plus a few non-English "L1" phones that
// phoneme recognizers may output for Russian speakers (x, trilled r, close e ...).
#pragma once

#include <string>
#include <vector>

namespace pron {

struct PhonemeInfo {
    const char* arpabet;  // canonical symbol, upper case, no stress ("TH")
    const char* ipa;      // canonical IPA (UTF-8, "θ")
    bool vowel;
    bool voiced;
    bool english;         // false for extra L1 phones (X, RR, E, O, Y_RU)
    const char* example;  // example English word ("" for extras)
};

// Number of phonemes in the inventory (English ARPAbet + extras). Ids are 0..count-1.
int phoneme_count();
const PhonemeInfo& phoneme_info(int id);
// Id by ARPAbet symbol (case-insensitive, stress digits ignored: "ih1" -> IH). -1 if unknown.
int phoneme_id(const std::string& arpabet);
// Id by IPA symbol. Accepts canonical IPA plus common aliases produced by espeak-ng /
// wav2vec2 vocabularies ("iː" -> IY, "ə" -> AH, "ɚ" -> ER, "g" -> G, "t͡ʃ" -> CH, "r" -> RR ...).
// Length marks and tie bars are tolerated. -1 if unknown.
int phoneme_id_from_ipa(const std::string& ipa);

std::string arpabet_to_ipa(const std::string& arpabet);  // "" if unknown
std::string ipa_to_arpabet(const std::string& ipa);      // "" if unknown

// One phone of a pronunciation. Stress is from CMUdict vowel digits: 0 none, 1 primary,
// 2 secondary; -1 for consonants / unknown.
struct Phone {
    std::string symbol;  // canonical ARPAbet without stress ("IH")
    int stress = -1;
    bool operator==(const Phone& o) const { return symbol == o.symbol && stress == o.stress; }
};

using Pronunciation = std::vector<Phone>;

// Parse "TH IH1 NG K" into phones. Unknown symbols make the whole parse fail (returns false).
bool parse_arpabet(const std::string& s, Pronunciation& out);
// "TH IH1 NG K" (with stress) or "TH IH NG K" (without stress).
std::string to_arpabet_string(const Pronunciation& p, bool with_stress = true);
// "θɪŋk"; with_stress renders primary stress as ˈ at the start of the stressed syllable
// (simple maximal-onset syllabification: "abandon" -> "əˈbændən", "extra" -> "ˈɛkstɹə").
std::string to_ipa_string(const Pronunciation& p, bool with_stress = false);

// Index of the vowel carrying primary stress (counted among vowels, i.e. syllable index), -1 if none.
int primary_stress_syllable(const Pronunciation& p);
int syllable_count(const Pronunciation& p);

}  // namespace pron

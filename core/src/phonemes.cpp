#include "pron/phonemes.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <sstream>

namespace pron {
namespace {

// Order matters only for ids; keep English ARPAbet first.
const PhonemeInfo kInventory[] = {
    // vowels
    {"AA", "ɑ", true, true, true, "father"},
    {"AE", "æ", true, true, true, "cat"},
    {"AH", "ʌ", true, true, true, "cup"},
    {"AO", "ɔ", true, true, true, "thought"},
    {"AW", "aʊ", true, true, true, "now"},
    {"AY", "aɪ", true, true, true, "my"},
    {"EH", "ɛ", true, true, true, "bed"},
    {"ER", "ɝ", true, true, true, "bird"},
    {"EY", "eɪ", true, true, true, "day"},
    {"IH", "ɪ", true, true, true, "sit"},
    {"IY", "i", true, true, true, "see"},
    {"OW", "oʊ", true, true, true, "go"},
    {"OY", "ɔɪ", true, true, true, "boy"},
    {"UH", "ʊ", true, true, true, "book"},
    {"UW", "u", true, true, true, "food"},
    // consonants
    {"B", "b", false, true, true, "bad"},
    {"CH", "tʃ", false, false, true, "chair"},
    {"D", "d", false, true, true, "dog"},
    {"DH", "ð", false, true, true, "this"},
    {"F", "f", false, false, true, "fish"},
    {"G", "ɡ", false, true, true, "go"},
    {"HH", "h", false, false, true, "hat"},
    {"JH", "dʒ", false, true, true, "job"},
    {"K", "k", false, false, true, "cat"},
    {"L", "l", false, true, true, "leg"},
    {"M", "m", false, true, true, "man"},
    {"N", "n", false, true, true, "no"},
    {"NG", "ŋ", false, true, true, "sing"},
    {"P", "p", false, false, true, "pen"},
    {"R", "ɹ", false, true, true, "red"},
    {"S", "s", false, false, true, "sun"},
    {"SH", "ʃ", false, false, true, "she"},
    {"T", "t", false, false, true, "tea"},
    {"TH", "θ", false, false, true, "think"},
    {"V", "v", false, true, true, "van"},
    {"W", "w", false, true, true, "wet"},
    {"Y", "j", false, true, true, "yes"},
    {"Z", "z", false, true, true, "zoo"},
    {"ZH", "ʒ", false, true, true, "vision"},
    // extra phones typical of Russian-accented English (not in CMUdict)
    {"X", "x", false, false, false, ""},     // Russian "х"
    {"RR", "r", false, true, false, ""},     // trilled Russian "р"
    {"E", "e", true, true, false, ""},       // Russian close-mid "э/е"
    {"O", "o", true, true, false, ""},       // Russian "о" (monophthong)
    {"Y_RU", "ɨ", true, true, false, ""},    // Russian "ы"
};

constexpr int kCount = static_cast<int>(sizeof(kInventory) / sizeof(kInventory[0]));

struct Alias {
    const char* ipa;
    const char* arpabet;
};

// Aliases for IPA strings produced by espeak-ng / wav2vec2 phoneme vocabularies.
const Alias kIpaAliases[] = {
    {"ɑː", "AA"}, {"ɒ", "AA"},  {"a", "AA"},   {"ɐ", "AH"},  {"ə", "AH"},  {"ɔː", "AO"},
    {"ɜ", "ER"},  {"ɜː", "ER"}, {"ɚ", "ER"},   {"ɝː", "ER"}, {"iː", "IY"}, {"uː", "UW"},
    {"ᵻ", "IH"},  {"ɛ", "EH"},  {"eə", "EH"},  {"əʊ", "OW"}, {"g", "G"},   {"ɾ", "T"},
    {"ɫ", "L"},   {"t͡ʃ", "CH"}, {"ʧ", "CH"},   {"d͡ʒ", "JH"}, {"ʤ", "JH"},  {"ɹ", "R"},
    {"ʁ", "RR"},  {"χ", "X"},   {"ɪ", "IH"},   {"i", "IY"},  {"ɨ", "Y_RU"},
    {"ʔ", "T"},   {"ɪə", "IH"}, {"ʊə", "UH"},  {"ɛə", "EH"}, {"ɝ", "ER"},  {"ɚː", "ER"}, {"ɒː", "AA"},
    {"ɑ", "AA"},  {"ʌː", "AH"}, {"ɔ", "AO"},   {"ɡ", "G"},   {"ɹ̩", "R"},  {"ɻ", "R"},   {"ɑ̃", "AA"},
    {"ɪ̈", "IH"},  {"ɵ", "AH"},  {"ɘ", "AH"},
};

std::string upper_no_stress(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c >= '0' && c <= '9') continue;
        out += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return out;
}

// Remove length marks / tie bars / stress marks so "ˈiː" ~ "i".
std::string strip_ipa_marks(const std::string& s) {
    static const char* const kMarks[] = {"ː", "\xCD\xA1" /* U+0361 tie */, "ˈ", "ˌ", "\xCC\xA9" /* syllabic */};
    std::string out = s;
    for (const char* m : kMarks) {
        std::size_t len = std::strlen(m), pos;
        while ((pos = out.find(m)) != std::string::npos) out.erase(pos, len);
    }
    return out;
}

}  // namespace

int phoneme_count() { return kCount; }

const PhonemeInfo& phoneme_info(int id) { return kInventory[id]; }

int phoneme_id(const std::string& arpabet) {
    std::string u = upper_no_stress(arpabet);
    if (u == "AX") u = "AH";  // schwa
    if (u == "AXR") u = "ER";
    for (int i = 0; i < kCount; ++i)
        if (u == kInventory[i].arpabet) return i;
    return -1;
}

int phoneme_id_from_ipa(const std::string& ipa) {
    for (int i = 0; i < kCount; ++i)
        if (ipa == kInventory[i].ipa) return i;
    for (const auto& a : kIpaAliases)
        if (ipa == a.ipa) return phoneme_id(a.arpabet);
    std::string stripped = strip_ipa_marks(ipa);
    if (stripped.empty()) return -1;
    if (!stripped.empty() && stripped != ipa) return phoneme_id_from_ipa(stripped);
    return -1;
}

bool parse_accent(const std::string& s, Accent& out) {
    std::string l;
    for (char c : s) l += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (l == "any" || l == "auto" || l.empty()) { out = Accent::Any; return true; }
    if (l == "us" || l == "en-us" || l == "en_us" || l == "american" || l == "ga") { out = Accent::Us; return true; }
    if (l == "gb" || l == "uk" || l == "en-gb" || l == "en_gb" || l == "rp" || l == "british") { out = Accent::Gb; return true; }
    return false;
}

std::string arpabet_to_ipa(const std::string& arpabet) {
    int id = phoneme_id(arpabet);
    return id < 0 ? std::string() : std::string(kInventory[id].ipa);
}

std::string ipa_to_arpabet(const std::string& ipa) {
    int id = phoneme_id_from_ipa(ipa);
    return id < 0 ? std::string() : std::string(kInventory[id].arpabet);
}

bool parse_arpabet(const std::string& s, Pronunciation& out) {
    out.clear();
    std::istringstream is(s);
    std::string tok;
    while (is >> tok) {
        int id = phoneme_id(tok);
        if (id < 0) {
            out.clear();
            return false;
        }
        Phone p;
        p.symbol = kInventory[id].arpabet;
        char last = tok.back();
        if (last >= '0' && last <= '2') {
            p.stress = last - '0';
        } else if (kInventory[id].vowel) {
            p.stress = 0;
        }
        out.push_back(p);
    }
    return true;
}

PhonemeVariants phoneme_variants(const Pronunciation& p, std::size_t i, Accent accent) {
    PhonemeVariants v;
    if (i >= p.size()) return v;
    auto add = [&](const char* a) {
        int id = phoneme_id(a);
        if (id >= 0 && std::find(v.ids.begin(), v.ids.end(), id) == v.ids.end()) v.ids.push_back(id);
    };
    const std::string& s = p[i].symbol;
    const int stress = p[i].stress;
    const bool gb = accent != Accent::Us;
    auto vowel_at = [&](std::size_t k) {
        if (k >= p.size()) return false;
        int id = phoneme_id(p[k].symbol);
        return id >= 0 && kInventory[id].vowel;
    };
    const bool prev_vowel = i > 0 && vowel_at(i - 1);
    const bool next_vowel = vowel_at(i + 1);
    const std::string next = i + 1 < p.size() ? p[i + 1].symbol : std::string();
    const std::string next2 = i + 2 < p.size() ? p[i + 2].symbol : std::string();
    add(s.c_str());
    if (s == "ER") {
        if (stress <= 0) add("AH");  // ɚ ~ ə
    } else if (s == "AH") {
        if (stress <= 0) add("IH");  // weak vowel ə ~ ɪ ~ ᵻ
    } else if (s == "IH") {
        if (stress <= 0) add("AH");
    } else if (s == "D") {
        if (prev_vowel && next_vowel) add("T");  // flapped ɾ
    } else if (s == "T") {
        if (prev_vowel && next_vowel) add("D");
    } else if (s == "EY") {
        add("E");
    } else if (s == "OW") {
        add("O");
    } else if (s == "AO") {
        if (gb && next != "R") add("AA");  // ɒ in "dog", "long"
    } else if (s == "AE") {
        const bool bath = next == "S" || next == "F" || next == "TH" ||
                          (next == "N" && (next2 == "S" || next2 == "T" || next2 == "D" || next2 == "CH"));
        if (gb && bath) add("AA");  // ɑː in bath/dance/can't words
    } else if (s == "UH") {
        if (gb && next == "R") add("AO");  // poor, sure: ɔː
    } else if (s == "R") {
        if (gb && prev_vowel && !next_vowel) v.allow_blank = true;  // non-rhotic
    }
    return v;
}

std::string to_arpabet_string(const Pronunciation& p, bool with_stress) {
    std::string out;
    for (const auto& ph : p) {
        if (!out.empty()) out += ' ';
        out += ph.symbol;
        if (with_stress && ph.stress >= 0) out += static_cast<char>('0' + ph.stress);
    }
    return out;
}

namespace {

bool is_vowel_phone(const Phone& ph) {
    int id = phoneme_id(ph.symbol);
    return id >= 0 && kInventory[id].vowel;
}

// Two-consonant clusters that can start an English syllable (C + liquid/glide, s + C).
bool valid_onset(const std::string& a, const std::string& b) {
    static const char* const kSecond[] = {"L", "R", "W", "Y"};
    static const char* const kFirst[] = {"P", "B", "T", "D", "K", "G", "F", "TH", "SH"};
    if (a == "S") return b == "P" || b == "T" || b == "K" || b == "M" || b == "N" || b == "L" || b == "W";
    for (const char* f : kFirst)
        if (a == f)
            for (const char* sc : kSecond)
                if (b == sc) return true;
    return false;
}

// Index of the phone before which a syllable-initial stress mark is placed.
std::size_t stress_mark_position(const Pronunciation& p, std::size_t vowel) {
    std::size_t first_cons = vowel;
    while (first_cons > 0 && !is_vowel_phone(p[first_cons - 1])) --first_cons;
    if (first_cons == 0) return 0;  // first syllable: mark at word start
    std::size_t n = vowel - first_cons;
    if (n <= 1) return first_cons;
    const std::string& c2 = p[vowel - 2].symbol;
    if (valid_onset(c2, p[vowel - 1].symbol)) {
        // s + stop + liquid/glide ("spl", "str", "skw").
        if (n >= 3 && p[vowel - 3].symbol == "S" && (c2 == "P" || c2 == "T" || c2 == "K")) return vowel - 3;
        return vowel - 2;
    }
    return vowel - 1;
}

}  // namespace

std::string to_ipa_string(const Pronunciation& p, bool with_stress) {
    std::size_t mark = p.size();  // no mark
    if (with_stress) {
        for (std::size_t i = 0; i < p.size(); ++i) {
            if (p[i].stress == 1 && is_vowel_phone(p[i])) {
                mark = stress_mark_position(p, i);
                break;
            }
        }
    }
    std::string out;
    for (std::size_t i = 0; i < p.size(); ++i) {
        const Phone& ph = p[i];
        int id = phoneme_id(ph.symbol);
        if (id < 0) continue;
        if (i == mark) out += "ˈ";
        if (ph.symbol == "AH" && ph.stress == 0) {
            out += "ə";
        } else if (ph.symbol == "ER" && ph.stress == 0) {
            out += "ɚ";
        } else {
            out += kInventory[id].ipa;
        }
    }
    return out;
}

int primary_stress_syllable(const Pronunciation& p) {
    int syl = 0;
    for (const auto& ph : p) {
        int id = phoneme_id(ph.symbol);
        if (id < 0 || !kInventory[id].vowel) continue;
        if (ph.stress == 1) return syl;
        ++syl;
    }
    return -1;
}

int syllable_count(const Pronunciation& p) {
    int n = 0;
    for (const auto& ph : p) {
        int id = phoneme_id(ph.symbol);
        if (id >= 0 && kInventory[id].vowel) ++n;
    }
    return n;
}

}  // namespace pron

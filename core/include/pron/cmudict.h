// CMU Pronouncing Dictionary loader (cmudict-0.7b and cmudict.dict formats).
//
//   ;;; comment line
//   THINK  TH IH1 NG K
//   READ(1)  R EH1 D            (0.7b variant syntax)
//   read(2) r eh1 d # comment    (cmudict.dict variant syntax, inline comment)
#pragma once

#include <istream>
#include <string>
#include <unordered_map>
#include <vector>

#include "pron/phonemes.h"

namespace pron {

class CmuDict {
public:
    struct LoadStats {
        std::size_t entries = 0;   // pronunciations added
        std::size_t skipped = 0;   // malformed lines / unknown phones
    };

    // Parse a dictionary from a stream; may be called several times (entries are merged).
    LoadStats load(std::istream& in);
    LoadStats load_from_string(const std::string& text);
    // Opens a UTF-8 path (works on Windows too). Returns false if the file can't be opened.
    bool load_file(const std::string& path_utf8, LoadStats* stats = nullptr);

    // Add / override a pronunciation (e.g. user lexicon). Word is normalized to lowercase.
    void add(const std::string& word, const Pronunciation& pron);

    // All variants in dictionary order (main entry first). Lookup is case-insensitive and
    // accepts ’ as apostrophe. Returns nullptr if the word is absent.
    const std::vector<Pronunciation>* find(const std::string& word) const;
    bool contains(const std::string& word) const { return find(word) != nullptr; }

    std::size_t word_count() const { return map_.size(); }
    std::size_t pronunciation_count() const { return prons_; }

private:
    std::unordered_map<std::string, std::vector<Pronunciation>> map_;
    std::size_t prons_ = 0;
};

}  // namespace pron

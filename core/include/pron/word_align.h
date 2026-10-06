// Alignment of recognized (ASR) words against the reference text words.
#pragma once

#include <string>
#include <vector>

namespace pron {

enum class WordStatus {
    Matched,      // recognized word equals the reference word
    Substituted,  // a different word was recognized in this position
    Omitted,      // the reference word was not read
};

const char* to_string(WordStatus s);

struct WordAlignOptions {
    // Substitution cost = sub_weight * (1 - similarity). Insertion / deletion cost = 1.
    // With sub_weight < 2 a substitution is always preferred over deletion + insertion.
    double sub_weight = 1.6;
    // Pairs whose similarity is below this are still aligned (as substitutions) only if that
    // is cheaper than deletion + insertion; this threshold just makes very dissimilar pairs
    // cost a full 2.0 so that the aligner prefers to keep neighbours matched.
    double min_similarity = 0.34;
};

struct RefWordAlignment {
    WordStatus status = WordStatus::Omitted;
    int hyp_index = -1;        // index in the recognized word list, -1 when omitted
    double similarity = 0.0;   // 0..1 string similarity between reference and recognized word
};

struct WordAlignment {
    std::vector<RefWordAlignment> ref;  // one entry per reference word
    std::vector<int> inserted;          // indices of recognized words not aligned to any reference word
    double cost = 0.0;
};

// Character-level similarity 1 - levenshtein(a, b) / max(len) on UTF-8 bytes (words are
// expected to be normalized lowercase English). Equal strings -> 1, completely different -> 0.
double word_similarity(const std::string& a, const std::string& b);

// Levenshtein distance between two sequences of strings / bytes.
std::size_t levenshtein(const std::string& a, const std::string& b);

// Align normalized reference words with normalized recognized words.
WordAlignment align_words(const std::vector<std::string>& ref, const std::vector<std::string>& hyp,
                          const WordAlignOptions& opt = {});

}  // namespace pron

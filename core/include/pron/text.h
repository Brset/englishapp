// Text normalization and tokenization of the reference text and ASR output.
#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace pron {

// One normalized word of a text.
struct Token {
    std::string text;     // original surface form as it appears in the source (UTF-8)
    std::string norm;     // normalized form used for matching / lexicon lookup ("don't", "forty")
    std::size_t begin = 0;  // byte offset of the source span in the UTF-8 text (inclusive)
    std::size_t end = 0;    // byte offset of the source span (exclusive)
    bool from_number = false;  // produced by expanding a number ("42" -> "forty", "two")
};

struct TokenizeOptions {
    // Skip dialogue speaker labels at the start of a line: "Waiter: Hello!" -> "hello".
    bool skip_speaker_labels = true;
    // Expand digits to words ("42" -> "forty two", "3rd" -> "third", "1990" -> "nineteen ninety").
    bool expand_numbers = true;
    // Split hyphenated compounds ("well-known" -> "well", "known"). When false, hyphen is kept.
    bool split_hyphens = true;
};

// Tokenize a UTF-8 text into words. Offsets point into `text` so the UI can highlight words.
// Handles ASCII and typographic apostrophes (’ is normalized to '), hyphens, dashes, quotes.
std::vector<Token> tokenize(const std::string& text, const TokenizeOptions& opt = {});

// Normalize a single word (lowercase, strip surrounding punctuation, ’ -> ').
// Returns an empty string when nothing word-like remains. Does not expand numbers.
std::string normalize_word(const std::string& word);

// Number expansion helpers (English, US style, no "and").
// Supports 0 .. 999'999'999'999. Returns words separated by single spaces.
std::string number_to_words(long long n);
std::string ordinal_to_words(long long n);  // 1 -> "first", 23 -> "twenty third"
// "1990" -> "nineteen ninety", "1905" -> "nineteen oh five", "2008" -> "two thousand eight",
// "2024" -> "twenty twenty four". Only meaningful for 1000..2999.
std::string year_to_words(int year);

// Split a string by ASCII whitespace.
std::vector<std::string> split_ws(const std::string& s);

}  // namespace pron

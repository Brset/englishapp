#include "pron/text.h"

#include <cstdint>
#include <cstdlib>

namespace pron {
namespace {

struct Cp {
    char32_t cp;
    std::size_t begin;  // byte offset
    std::size_t end;
};

std::vector<Cp> decode_utf8(const std::string& s) {
    std::vector<Cp> out;
    out.reserve(s.size());
    std::size_t i = 0;
    while (i < s.size()) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        char32_t cp = 0xFFFD;
        std::size_t len = 1;
        if (c < 0x80) {
            cp = c;
        } else if ((c >> 5) == 0x6) {
            len = 2;
        } else if ((c >> 4) == 0xE) {
            len = 3;
        } else if ((c >> 3) == 0x1E) {
            len = 4;
        }
        if (len > 1) {
            if (i + len > s.size()) {
                len = 1;
            } else {
                char32_t v = c & (0xFF >> (len + 1));
                bool ok = true;
                for (std::size_t k = 1; k < len; ++k) {
                    unsigned char cc = static_cast<unsigned char>(s[i + k]);
                    if ((cc >> 6) != 0x2) {
                        ok = false;
                        break;
                    }
                    v = (v << 6) | (cc & 0x3F);
                }
                if (ok) {
                    cp = v;
                } else {
                    len = 1;
                }
            }
        }
        out.push_back({cp, i, i + len});
        i += len;
    }
    return out;
}

void encode_utf8(char32_t cp, std::string& out) {
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

bool is_digit(char32_t c) { return c >= '0' && c <= '9'; }

bool is_letter(char32_t c) {
    if (c < 0x80) return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
    if (c < 0xC0 || c == 0xD7 || c == 0xF7) return false;    // Latin-1 punctuation/symbols
    if (c >= 0x2000 && c <= 0x2BFF) return false;            // general punctuation, symbols
    if (c >= 0x3000 && c <= 0x303F) return false;            // CJK punctuation
    if (c == 0xFEFF || c == 0xFFFD) return false;            // BOM, invalid
    if (c >= 0x1F000) return false;                          // emoji etc.
    return true;
}

bool is_alnum(char32_t c) { return is_letter(c) || is_digit(c); }

bool is_apostrophe(char32_t c) { return c == '\'' || c == 0x2019 || c == 0x2018 || c == 0x02BC; }

bool is_hyphen(char32_t c) { return c == '-' || c == 0x2010 || c == 0x2011; }

bool is_space(char32_t c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f' || c == 0xA0 ||
           c == 0x2009 || c == 0x202F;
}

char32_t to_lower(char32_t c) {
    if (c >= 'A' && c <= 'Z') return c + 32;
    if (c >= 0xC0 && c <= 0xDE && c != 0xD7) return c + 32;
    return c;
}

// A maximal run of word characters in the source.
struct RawWord {
    std::size_t cp_begin, cp_end;  // indices into codepoint vector
};

struct Part {
    std::size_t cp_begin, cp_end;
};

std::string lower_norm(const std::vector<Cp>& cps, std::size_t b, std::size_t e) {
    std::string out;
    for (std::size_t i = b; i < e; ++i) {
        char32_t c = cps[i].cp;
        if (is_apostrophe(c)) c = '\'';
        if (is_hyphen(c)) c = '-';
        encode_utf8(to_lower(c), out);
    }
    return out;
}

void pluralize_last(std::string& words) {
    std::size_t sp = words.rfind(' ');
    std::size_t start = sp == std::string::npos ? 0 : sp + 1;
    if (words.size() > start && words.back() == 'y') {
        words.pop_back();
        words += "ies";
    } else if (words.size() > start && words.back() == 'x') {
        words += "es";
    } else {
        words += 's';
    }
}

// Expand a part containing digits into one or more words (space-separated). Empty = failed.
std::string expand_number_part(const std::string& s) {
    std::size_t i = 0;
    std::string intpart, frac;
    bool has_comma = false;
    while (i < s.size() && (is_digit(static_cast<unsigned char>(s[i])) || s[i] == ',')) {
        if (s[i] == ',') {
            has_comma = true;
        } else {
            intpart += s[i];
        }
        ++i;
    }
    if (i < s.size() && s[i] == '.' && i + 1 < s.size() && is_digit(static_cast<unsigned char>(s[i + 1]))) {
        ++i;
        while (i < s.size() && is_digit(static_cast<unsigned char>(s[i]))) frac += s[i++];
    }
    std::string suffix = s.substr(i);
    if (intpart.empty() || intpart.size() > 12) return std::string();
    long long value = std::atoll(intpart.c_str());

    if (!frac.empty()) {
        if (!suffix.empty()) return std::string();
        std::string out = number_to_words(value) + " point";
        for (char d : frac) out += std::string(" ") + number_to_words(d - '0');
        return out;
    }
    if (suffix.empty()) {
        if (!has_comma && intpart.size() == 4 && value >= 1100 && value <= 2099 && intpart[0] != '0')
            return year_to_words(static_cast<int>(value));
        // Leading zeros (e.g. "007"): read digit by digit.
        if (intpart.size() > 1 && intpart[0] == '0') {
            std::string out;
            for (char d : intpart) {
                if (!out.empty()) out += ' ';
                out += d == '0' ? "oh" : number_to_words(d - '0');
            }
            return out;
        }
        return number_to_words(value);
    }
    if (suffix == "st" || suffix == "nd" || suffix == "rd" || suffix == "th") return ordinal_to_words(value);
    if (suffix == "s" || suffix == "'s") {
        std::string w = (!has_comma && intpart.size() == 4 && value >= 1100 && value <= 2099)
                            ? year_to_words(static_cast<int>(value))
                            : number_to_words(value);
        pluralize_last(w);
        return w;
    }
    return std::string();
}

void add_token(std::vector<Token>& out, const std::string& src, const std::vector<Cp>& cps, std::size_t b,
               std::size_t e, const std::string& norm, bool from_number) {
    Token t;
    t.begin = cps[b].begin;
    t.end = cps[e - 1].end;
    t.text = src.substr(t.begin, t.end - t.begin);
    t.norm = norm;
    t.from_number = from_number;
    out.push_back(std::move(t));
}

void emit_part(std::vector<Token>& out, const std::string& src, const std::vector<Cp>& cps, Part p,
               const TokenizeOptions& opt) {
    // Strip apostrophes / hyphens at the edges.
    while (p.cp_begin < p.cp_end && !is_alnum(cps[p.cp_begin].cp)) ++p.cp_begin;
    while (p.cp_end > p.cp_begin && !is_alnum(cps[p.cp_end - 1].cp)) --p.cp_end;
    if (p.cp_begin >= p.cp_end) return;

    bool has_digit = false;
    for (std::size_t i = p.cp_begin; i < p.cp_end; ++i)
        if (is_digit(cps[i].cp)) has_digit = true;

    std::string norm = lower_norm(cps, p.cp_begin, p.cp_end);
    if (!has_digit || !opt.expand_numbers) {
        add_token(out, src, cps, p.cp_begin, p.cp_end, norm, false);
        return;
    }
    std::string words = is_digit(cps[p.cp_begin].cp) ? expand_number_part(norm) : std::string();
    if (!words.empty()) {
        for (const auto& w : split_ws(words)) add_token(out, src, cps, p.cp_begin, p.cp_end, w, true);
        return;
    }
    // Mixed token like "mp3" or "4x4": split into digit / non-digit runs.
    std::size_t i = p.cp_begin;
    while (i < p.cp_end) {
        bool dig = is_digit(cps[i].cp);
        std::size_t j = i;
        while (j < p.cp_end && is_digit(cps[j].cp) == dig) ++j;
        if (dig) {
            std::string num = lower_norm(cps, i, j);
            std::string w = expand_number_part(num);
            for (const auto& x : split_ws(w)) add_token(out, src, cps, i, j, x, true);
        } else {
            emit_part(out, src, cps, Part{i, j}, TokenizeOptions{false, false, opt.split_hyphens});
        }
        i = j;
    }
}

}  // namespace

std::vector<Token> tokenize(const std::string& text, const TokenizeOptions& opt) {
    std::vector<Cp> cps = decode_utf8(text);
    const std::size_t n = cps.size();

    // 1. Raw words.
    std::vector<RawWord> raws;
    std::size_t i = 0;
    while (i < n) {
        if (!is_alnum(cps[i].cp)) {
            ++i;
            continue;
        }
        std::size_t j = i + 1;
        while (j < n) {
            char32_t c = cps[j].cp;
            if (is_alnum(c)) {
                ++j;
            } else if ((is_apostrophe(c) || is_hyphen(c)) && j + 1 < n && is_alnum(cps[j + 1].cp)) {
                j += 2;
            } else if ((c == '.' || c == ',') && is_digit(cps[j - 1].cp) && j + 1 < n && is_digit(cps[j + 1].cp)) {
                j += 2;
            } else {
                break;
            }
        }
        raws.push_back({i, j});
        i = j;
    }

    // 2. Speaker labels ("Waiter: ...", "Mr Smith: ...") at line starts.
    std::vector<bool> skip(raws.size(), false);
    if (opt.skip_speaker_labels) {
        for (std::size_t r = 0; r < raws.size(); ++r) {
            // Line start: only whitespace between previous newline (or text start) and this word.
            bool line_start = true;
            for (std::size_t k = raws[r].cp_begin; k-- > 0;) {
                if (cps[k].cp == '\n') break;
                if (!is_space(cps[k].cp)) {
                    line_start = false;
                    break;
                }
            }
            if (!line_start) continue;
            char32_t first = cps[raws[r].cp_begin].cp;
            if (!(first >= 'A' && first <= 'Z')) continue;
            // Up to 3 words separated only by spaces, then ':' not followed by a digit.
            for (std::size_t k = r; k < raws.size() && k < r + 3; ++k) {
                bool has_digit = false;
                for (std::size_t q = raws[k].cp_begin; q < raws[k].cp_end; ++q)
                    if (is_digit(cps[q].cp)) has_digit = true;
                if (has_digit) break;
                if (k > r) {
                    bool only_spaces = true;
                    for (std::size_t q = raws[k - 1].cp_end; q < raws[k].cp_begin; ++q)
                        if (!(cps[q].cp == ' ' || cps[q].cp == '\t' || cps[q].cp == '.')) only_spaces = false;
                    if (!only_spaces) break;
                }
                std::size_t q = raws[k].cp_end;
                while (q < n && (cps[q].cp == ' ' || cps[q].cp == '\t')) ++q;
                if (q < n && cps[q].cp == ':' && !(q + 1 < n && is_digit(cps[q + 1].cp))) {
                    for (std::size_t s = r; s <= k; ++s) skip[s] = true;
                    break;
                }
            }
        }
    }

    // 3. Split hyphens, expand numbers, emit tokens.
    std::vector<Token> out;
    for (std::size_t r = 0; r < raws.size(); ++r) {
        if (skip[r]) continue;
        const RawWord& rw = raws[r];
        if (!opt.split_hyphens) {
            emit_part(out, text, cps, Part{rw.cp_begin, rw.cp_end}, opt);
            continue;
        }
        std::size_t b = rw.cp_begin;
        for (std::size_t k = rw.cp_begin; k <= rw.cp_end; ++k) {
            if (k == rw.cp_end || is_hyphen(cps[k].cp)) {
                emit_part(out, text, cps, Part{b, k}, opt);
                b = k + 1;
            }
        }
    }
    return out;
}

std::string normalize_word(const std::string& word) {
    TokenizeOptions opt;
    opt.skip_speaker_labels = false;
    opt.expand_numbers = false;
    opt.split_hyphens = false;
    std::string out;
    for (const auto& t : tokenize(word, opt)) {
        if (!out.empty()) out += ' ';
        out += t.norm;
    }
    return out;
}

}  // namespace pron

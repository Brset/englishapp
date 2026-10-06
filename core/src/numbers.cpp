#include "pron/text.h"

#include <sstream>

namespace pron {
namespace {

const char* const kOnes[] = {"zero",    "one",     "two",       "three",    "four",
                             "five",    "six",     "seven",     "eight",    "nine",
                             "ten",     "eleven",  "twelve",    "thirteen", "fourteen",
                             "fifteen", "sixteen", "seventeen", "eighteen", "nineteen"};
const char* const kTens[] = {"",      "",      "twenty",  "thirty", "forty",
                             "fifty", "sixty", "seventy", "eighty", "ninety"};

void append(std::string& out, const std::string& w) {
    if (w.empty()) return;
    if (!out.empty()) out += ' ';
    out += w;
}

std::string below_thousand(int n) {  // 1..999
    std::string out;
    if (n >= 100) {
        append(out, kOnes[n / 100]);
        append(out, "hundred");
        n %= 100;
    }
    if (n >= 20) {
        append(out, kTens[n / 10]);
        if (n % 10) append(out, kOnes[n % 10]);
    } else if (n > 0) {
        append(out, kOnes[n]);
    }
    return out;
}

}  // namespace

std::string number_to_words(long long n) {
    if (n < 0) return "minus " + number_to_words(-n);
    if (n == 0) return "zero";
    static const char* const kScales[] = {"", "thousand", "million", "billion"};
    int groups[4] = {0, 0, 0, 0};
    for (int i = 0; i < 4 && n > 0; ++i) {
        groups[i] = static_cast<int>(n % 1000);
        n /= 1000;
    }
    std::string out;
    if (n > 0) {
        // Beyond supported range: read digit by digit.
        return std::string();
    }
    for (int i = 3; i >= 0; --i) {
        if (groups[i] == 0) continue;
        append(out, below_thousand(groups[i]));
        append(out, kScales[i]);
    }
    return out;
}

std::string ordinal_to_words(long long n) {
    std::string card = number_to_words(n);
    if (card.empty()) return card;
    std::size_t sp = card.rfind(' ');
    std::string head = sp == std::string::npos ? std::string() : card.substr(0, sp + 1);
    std::string last = sp == std::string::npos ? card : card.substr(sp + 1);
    static const struct {
        const char* card;
        const char* ord;
    } kIrregular[] = {{"one", "first"},   {"two", "second"}, {"three", "third"},
                      {"five", "fifth"},  {"eight", "eighth"}, {"nine", "ninth"},
                      {"twelve", "twelfth"}};
    for (const auto& ir : kIrregular) {
        if (last == ir.card) return head + ir.ord;
    }
    if (!last.empty() && last.back() == 'y') {
        last.pop_back();
        return head + last + "ieth";
    }
    return head + last + "th";
}

std::string year_to_words(int year) {
    if (year < 1000 || year > 2999) return number_to_words(year);
    int hi = year / 100, lo = year % 100;
    if (year >= 2000 && year <= 2009) return number_to_words(year);
    if (lo == 0) return number_to_words(hi) + " hundred";
    std::string out = number_to_words(hi) + " ";
    if (lo < 10) return out + "oh " + kOnes[lo];
    return out + number_to_words(lo);
}

std::vector<std::string> split_ws(const std::string& s) {
    std::vector<std::string> out;
    std::istringstream is(s);
    std::string w;
    while (is >> w) out.push_back(w);
    return out;
}

}  // namespace pron

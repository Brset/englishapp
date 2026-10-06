#include "pron/json_writer.h"

#include <cmath>

namespace pron {

std::string JsonWriter::escape(const std::string& s) {
    static const char kHex[] = "0123456789abcdef";
    std::string out;
    out.reserve(s.size() + 2);
    out += '"';
    for (unsigned char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            default:
                if (c < 0x20) {
                    out += "\\u00";
                    out += kHex[c >> 4];
                    out += kHex[c & 0xF];
                } else {
                    out += static_cast<char>(c);  // UTF-8 passes through unchanged
                }
        }
    }
    out += '"';
    return out;
}

std::string JsonWriter::format_number(double v, int decimals) {
    if (!std::isfinite(v)) return "null";
    if (decimals < 0) decimals = 0;
    if (decimals > 9) decimals = 9;
    long long scale = 1;
    for (int i = 0; i < decimals; ++i) scale *= 10;
    bool neg = v < 0;
    double a = std::fabs(v);
    if (a * scale >= 9.0e18) {  // too large for fixed-point: integer part only
        decimals = 0;
        scale = 1;
        if (a >= 9.0e18) return neg ? "-9e18" : "9e18";
    }
    long long q = std::llround(a * static_cast<double>(scale));
    long long ip = q / scale, fp = q % scale;
    std::string out;
    if (neg && q != 0) out += '-';
    out += std::to_string(ip);
    if (decimals > 0 && fp != 0) {
        std::string frac = std::to_string(fp);
        frac.insert(0, static_cast<std::size_t>(decimals) - frac.size(), '0');
        while (!frac.empty() && frac.back() == '0') frac.pop_back();
        out += '.';
        out += frac;
    }
    return out;
}

void JsonWriter::newline() {
    if (!pretty_) return;
    out_ += '\n';
    out_.append(stack_.size() * 2, ' ');
}

void JsonWriter::before_value() {
    if (after_key_) {
        after_key_ = false;
        return;
    }
    if (!stack_.empty()) {
        if (stack_.back().second) out_ += ',';
        stack_.back().second = true;
        newline();
    }
}

JsonWriter& JsonWriter::begin_object() {
    before_value();
    out_ += '{';
    stack_.push_back({true, false});
    return *this;
}

JsonWriter& JsonWriter::end_object() {
    bool had = !stack_.empty() && stack_.back().second;
    if (!stack_.empty()) stack_.pop_back();
    if (had) newline();
    out_ += '}';
    return *this;
}

JsonWriter& JsonWriter::begin_array() {
    before_value();
    out_ += '[';
    stack_.push_back({false, false});
    return *this;
}

JsonWriter& JsonWriter::end_array() {
    bool had = !stack_.empty() && stack_.back().second;
    if (!stack_.empty()) stack_.pop_back();
    if (had) newline();
    out_ += ']';
    return *this;
}

JsonWriter& JsonWriter::key(const std::string& k) {
    before_value();
    out_ += escape(k);
    out_ += pretty_ ? ": " : ":";
    after_key_ = true;
    return *this;
}

JsonWriter& JsonWriter::value(const std::string& s) {
    before_value();
    out_ += escape(s);
    return *this;
}

JsonWriter& JsonWriter::value(const char* s) {
    if (!s) return null();
    return value(std::string(s));
}

JsonWriter& JsonWriter::value(bool b) {
    before_value();
    out_ += b ? "true" : "false";
    return *this;
}

JsonWriter& JsonWriter::value(long long v) {
    before_value();
    out_ += std::to_string(v);
    return *this;
}

JsonWriter& JsonWriter::value(double v, int decimals) {
    before_value();
    out_ += format_number(v, decimals);
    return *this;
}

JsonWriter& JsonWriter::null() {
    before_value();
    out_ += "null";
    return *this;
}

}  // namespace pron

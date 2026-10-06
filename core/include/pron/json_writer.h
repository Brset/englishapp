// Minimal streaming JSON writer (no dependencies, locale-independent number formatting).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace pron {

class JsonWriter {
public:
    explicit JsonWriter(bool pretty = false) : pretty_(pretty) {}

    JsonWriter& begin_object();
    JsonWriter& end_object();
    JsonWriter& begin_array();
    JsonWriter& end_array();
    JsonWriter& key(const std::string& k);

    JsonWriter& value(const std::string& s);
    JsonWriter& value(const char* s);
    JsonWriter& value(bool b);
    JsonWriter& value(int v) { return value(static_cast<long long>(v)); }
    JsonWriter& value(long long v);
    JsonWriter& value(std::size_t v) { return value(static_cast<long long>(v)); }
    // Fixed-point with up to `decimals` digits (trailing zeros trimmed). NaN/inf -> null.
    JsonWriter& value(double v, int decimals = 3);
    JsonWriter& null();

    // Convenience: key + value.
    template <typename T>
    JsonWriter& kv(const std::string& k, const T& v) {
        key(k);
        return value(v);
    }
    JsonWriter& kv(const std::string& k, double v, int decimals) {
        key(k);
        return value(v, decimals);
    }

    const std::string& str() const { return out_; }
    std::string take() { return std::move(out_); }

    static std::string escape(const std::string& s);
    static std::string format_number(double v, int decimals);

private:
    void before_value();
    void newline();

    std::string out_;
    // Stack of containers: true = object, false = array; paired with "has elements".
    std::vector<std::pair<bool, bool>> stack_;
    bool after_key_ = false;
    bool pretty_;
};

}  // namespace pron

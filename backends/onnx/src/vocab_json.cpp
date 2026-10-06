#include <cstdlib>
#include <map>
#include <stdexcept>

#include "pron/onnx_backends.h"

namespace pron {
namespace {
void put_utf8(std::string& s, unsigned cp) {
    if (cp < 0x80) s += char(cp);
    else if (cp < 0x800) { s += char(0xC0 | (cp >> 6)); s += char(0x80 | (cp & 0x3F)); }
    else if (cp < 0x10000) { s += char(0xE0 | (cp >> 12)); s += char(0x80 | ((cp >> 6) & 0x3F)); s += char(0x80 | (cp & 0x3F)); }
    else { s += char(0xF0 | (cp >> 18)); s += char(0x80 | ((cp >> 12) & 0x3F)); s += char(0x80 | ((cp >> 6) & 0x3F)); s += char(0x80 | (cp & 0x3F)); }
}
}  // namespace

std::vector<std::string> parse_vocab_json(const std::string& t) {
    size_t i = 0;
    auto fail = [&](const char* m) { throw std::runtime_error(std::string("vocab.json: ") + m); };
    auto ws = [&] { while (i < t.size() && (t[i] == ' ' || t[i] == '\t' || t[i] == '\n' || t[i] == '\r')) ++i; };
    auto hex4 = [&]() -> unsigned {
        if (i + 4 > t.size()) fail("bad \\u escape");
        unsigned v = 0;
        for (int k = 0; k < 4; ++k) {
            char c = t[i++]; v <<= 4;
            if (c >= '0' && c <= '9') v |= c - '0';
            else if (c >= 'a' && c <= 'f') v |= c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') v |= c - 'A' + 10;
            else fail("bad hex");
        }
        return v;
    };
    auto str = [&]() {
        if (i >= t.size() || t[i] != '"') fail("expected string");
        ++i;
        std::string s;
        while (true) {
            if (i >= t.size()) fail("unterminated string");
            char c = t[i++];
            if (c == '"') break;
            if (c != '\\') { s += c; continue; }
            if (i >= t.size()) fail("bad escape");
            char e = t[i++];
            switch (e) {
                case 'n': s += '\n'; break; case 't': s += '\t'; break; case 'r': s += '\r'; break;
                case 'b': s += '\b'; break; case 'f': s += '\f'; break;
                case 'u': {
                    unsigned cp = hex4();
                    if (cp >= 0xD800 && cp < 0xDC00 && i + 1 < t.size() && t[i] == '\\' && t[i + 1] == 'u') {
                        i += 2; unsigned lo = hex4();
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    }
                    put_utf8(s, cp); break;
                }
                default: s += e;  // \" \\ \/
            }
        }
        return s;
    };
    std::map<int, std::string> byid;
    ws();
    if (i >= t.size() || t[i] != '{') fail("expected '{'");
    ++i; ws();
    if (i < t.size() && t[i] == '}') fail("empty vocab");
    while (true) {
        ws(); std::string key = str(); ws();
        if (i >= t.size() || t[i] != ':') fail("expected ':'");
        ++i; ws();
        const char* b = t.c_str() + i; char* e = nullptr;
        long id = std::strtol(b, &e, 10);
        if (e == b || id < 0) fail("bad id");
        i += size_t(e - b);
        if (!byid.emplace(int(id), key).second) fail("duplicate id");
        ws();
        if (i < t.size() && t[i] == ',') { ++i; continue; }
        if (i < t.size() && t[i] == '}') break;
        fail("expected ',' or '}'");
    }
    std::vector<std::string> out(byid.rbegin()->first + 1);
    if (out.size() != byid.size()) fail("ids not contiguous");
    for (auto& kv : byid) out[kv.first] = kv.second;
    return out;
}
}  // namespace pron

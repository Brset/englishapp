#include "pron/cmudict.h"

#include <fstream>
#include <sstream>

#if defined(_WIN32)
#include <filesystem>
#endif

#include "pron/text.h"

namespace pron {
namespace {

std::string dict_key(const std::string& w) {
    // Normalize case and apostrophes exactly like the tokenizer does, so lookups by Token::norm
    // hit. Note: edge punctuation is stripped ("'bout" -> "bout").
    std::string n = normalize_word(w);
    if (n.empty()) {  // pure punctuation entries: keep raw lowercase key
        for (char c : w) n += static_cast<char>((c >= 'A' && c <= 'Z') ? c + 32 : c);
    }
    return n;
}

}  // namespace

CmuDict::LoadStats CmuDict::load(std::istream& in) {
    LoadStats st;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line.compare(0, 3, ";;;") == 0) continue;
        std::size_t hash = line.find(" #");
        if (hash != std::string::npos) line.erase(hash);
        std::size_t ws = line.find_first_of(" \t");
        if (ws == std::string::npos) {
            ++st.skipped;
            continue;
        }
        std::string word = line.substr(0, ws);
        // Strip variant marker "(2)".
        if (word.size() > 3 && word.back() == ')') {
            std::size_t open = word.rfind('(');
            if (open != std::string::npos && open > 0) word.erase(open);
        }
        Pronunciation p;
        if (!parse_arpabet(line.substr(ws + 1), p) || p.empty()) {
            ++st.skipped;
            continue;
        }
        add(word, p);
        ++st.entries;
    }
    return st;
}

CmuDict::LoadStats CmuDict::load_from_string(const std::string& text) {
    std::istringstream is(text);
    return load(is);
}

bool CmuDict::load_file(const std::string& path_utf8, LoadStats* stats) {
#if defined(_WIN32)
    std::ifstream in(std::filesystem::u8path(path_utf8), std::ios::binary);
#else
    std::ifstream in(path_utf8, std::ios::binary);
#endif
    if (!in) return false;
    LoadStats st = load(in);
    if (stats) *stats = st;
    return true;
}

void CmuDict::add(const std::string& word, const Pronunciation& pron) {
    auto& v = map_[dict_key(word)];
    for (const auto& existing : v)
        if (existing == pron) return;
    v.push_back(pron);
    ++prons_;
}

const std::vector<Pronunciation>* CmuDict::find(const std::string& word) const {
    auto it = map_.find(dict_key(word));
    return it == map_.end() ? nullptr : &it->second;
}

}  // namespace pron

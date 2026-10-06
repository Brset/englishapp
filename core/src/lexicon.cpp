#include "pron/lexicon.h"

namespace pron {

const char* to_string(PronSource s) {
    switch (s) {
        case PronSource::None: return "none";
        case PronSource::CmuDict: return "cmudict";
        case PronSource::G2P: return "g2p";
    }
    return "none";
}

PronSource Lexicon::lookup(const std::string& word, Pronunciation& out) const {
    out.clear();
    if (word.empty()) return PronSource::None;
    if (dict_) {
        if (const auto* v = dict_->find(word)) {
            out = v->front();
            return PronSource::CmuDict;
        }
        // Possessive / contraction "'s" on a known base word.
        if (word.size() > 2 && word.compare(word.size() - 2, 2, "'s") == 0) {
            if (const auto* v = dict_->find(word.substr(0, word.size() - 2))) {
                out = v->front();
                const std::string& last = out.back().symbol;
                if (last == "S" || last == "Z" || last == "SH" || last == "ZH" || last == "CH" || last == "JH") {
                    out.push_back({"IH", 0});
                    out.push_back({"Z", -1});
                } else if (last == "P" || last == "T" || last == "K" || last == "F" || last == "TH") {
                    out.push_back({"S", -1});
                } else {
                    out.push_back({"Z", -1});
                }
                return PronSource::CmuDict;
            }
        }
    }
    if (g2p_ && g2p_->pronounce(word, out) && !out.empty()) return PronSource::G2P;
    out.clear();
    return PronSource::None;
}

}  // namespace pron

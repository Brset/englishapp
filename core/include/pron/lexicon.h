// Expected pronunciation lookup: CMUdict first, then optional G2P fallback (espeak-ng).
#pragma once

#include <string>

#include "pron/backends.h"
#include "pron/cmudict.h"

namespace pron {

enum class PronSource { None, CmuDict, G2P };

const char* to_string(PronSource s);

class Lexicon {
public:
    explicit Lexicon(const CmuDict* dict = nullptr, IG2P* g2p = nullptr) : dict_(dict), g2p_(g2p) {}
    void set_dict(const CmuDict* d) { dict_ = d; }
    void set_g2p(IG2P* g) { g2p_ = g; }

    // Main pronunciation of a normalized word. Possessive "'s" is handled when only the base
    // word is in the dictionary ("teacher's" -> teacher + Z). Returns PronSource::None if unknown.
    PronSource lookup(const std::string& word, Pronunciation& out) const;

private:
    const CmuDict* dict_;
    IG2P* g2p_;
};

}  // namespace pron

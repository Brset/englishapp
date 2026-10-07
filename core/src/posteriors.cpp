#include "pron/posteriors.h"

#include <algorithm>
#include <cmath>

#include "pron/phonemes.h"

namespace pron {

int inventory_columns() { return phoneme_count() + 1; }

LogPosteriors LogPosteriors::slice(int begin, int end) const {
    begin = std::max(0, std::min(begin, frames));
    end = std::max(begin, std::min(end, frames));
    LogPosteriors out(end - begin, classes, frame_seconds);
    std::copy(data.begin() + static_cast<std::ptrdiff_t>(begin) * classes,
              data.begin() + static_cast<std::ptrdiff_t>(end) * classes, out.data.begin());
    return out;
}

namespace {
// ARPAbet-style label ("AH0", "TH", "Y_RU"): only then is the ARPAbet fallback applied, so that
// lower-case IPA letters such as "o", "e", "r", "y" are never mistaken for ARPAbet/extra phones.
bool looks_like_arpabet(const std::string& l) {
    for (char c : l)
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '2') || c == '_')) return false;
    return !l.empty();
}
}  // namespace

PhonemeVocab::PhonemeVocab(std::vector<std::string> labels, int blank_index)
    : labels_(std::move(labels)), map_(labels_.size(), -1), delim_(labels_.size(), false), ignored_(labels_.size(), false),
      present_(static_cast<std::size_t>(phoneme_count()) + 1, false), blank_(blank_index) {
    present_[kBlankColumn] = true;
    for (std::size_t i = 0; i < labels_.size(); ++i) {
        if (static_cast<int>(i) == blank_) continue;
        const std::string& l = labels_[i];
        if (l == "|" || l == " " || l == "\xE2\x96\x81") { delim_[i] = true; continue; }
        if (is_ipa_mark_only(l)) { ignored_[i] = true; continue; }
        if (l.empty() || l[0] == '<' || (l.front() == '[' && l.back() == ']')) continue;
        int id = phoneme_id_from_ipa(l);
        if (id < 0 && looks_like_arpabet(l)) id = phoneme_id(l);
        map_[i] = id;
        if (id >= 0) present_[static_cast<std::size_t>(column_of_phoneme(id))] = true;
    }
}

int PhonemeVocab::mapped_count() const {
    int n = 0;
    for (int m : map_)
        if (m >= 0) ++n;
    return n;
}

std::vector<std::string> PhonemeVocab::unmapped_labels() const {
    std::vector<std::string> out;
    for (std::size_t i = 0; i < labels_.size(); ++i)
        if (map_[i] < 0 && static_cast<int>(i) != blank_) out.push_back(labels_[i]);
    return out;
}

std::vector<std::string> PhonemeVocab::mapping_summary() const {
    std::vector<std::string> out;
    for (std::size_t i = 0; i < labels_.size(); ++i) {
        std::string t = labels_[i] + ">";
        if (static_cast<int>(i) == blank_ || delim_[i] || ignored_[i]) t += "blank";
        else if (map_[i] >= 0) t += phoneme_info(map_[i]).arpabet;
        else t += "-";
        out.push_back(t);
    }
    return out;
}

LogPosteriors PhonemeVocab::collapse(const LogPosteriors& model) const {
    const int C = inventory_columns();
    LogPosteriors out(model.frames, C, model.frame_seconds);
    const float kFloor = -1e4f;
    std::vector<float> mx(C), acc(C);
    for (int t = 0; t < model.frames; ++t) {
        std::fill(mx.begin(), mx.end(), -INFINITY);
        std::fill(acc.begin(), acc.end(), 0.0f);
        auto col_of = [&](int label) -> int {
            if (label == blank_ || delim_[label] || ignored_[label]) return kBlankColumn;
            int id = map_[label];
            return id < 0 ? -1 : column_of_phoneme(id);
        };
        const int L = std::min(model.classes, size());
        for (int l = 0; l < L; ++l) {
            int c = col_of(l);
            if (c >= 0) mx[c] = std::max(mx[c], model.at(t, l));
        }
        for (int l = 0; l < L; ++l) {
            int c = col_of(l);
            if (c >= 0) acc[c] += std::exp(model.at(t, l) - mx[c]);
        }
        for (int c = 0; c < C; ++c)
            out.at(t, c) = std::isfinite(mx[c]) ? std::max(kFloor, mx[c] + std::log(acc[c])) : kFloor;
    }
    return out;
}

}  // namespace pron

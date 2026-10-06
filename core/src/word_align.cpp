#include "pron/word_align.h"

#include <algorithm>
#include <limits>

namespace pron {

const char* to_string(WordStatus s) {
    switch (s) {
        case WordStatus::Matched: return "matched";
        case WordStatus::Substituted: return "substituted";
        case WordStatus::Omitted: return "omitted";
    }
    return "unknown";
}

std::size_t levenshtein(const std::string& a, const std::string& b) {
    std::vector<std::size_t> prev(b.size() + 1), cur(b.size() + 1);
    for (std::size_t j = 0; j <= b.size(); ++j) prev[j] = j;
    for (std::size_t i = 1; i <= a.size(); ++i) {
        cur[0] = i;
        for (std::size_t j = 1; j <= b.size(); ++j) {
            std::size_t sub = prev[j - 1] + (a[i - 1] == b[j - 1] ? 0 : 1);
            cur[j] = std::min({sub, prev[j] + 1, cur[j - 1] + 1});
        }
        std::swap(prev, cur);
    }
    return prev[b.size()];
}

double word_similarity(const std::string& a, const std::string& b) {
    if (a == b) return 1.0;
    std::size_t m = std::max(a.size(), b.size());
    if (m == 0) return 1.0;
    return 1.0 - static_cast<double>(levenshtein(a, b)) / static_cast<double>(m);
}

WordAlignment align_words(const std::vector<std::string>& ref, const std::vector<std::string>& hyp,
                          const WordAlignOptions& opt) {
    const std::size_t R = ref.size(), H = hyp.size();
    enum Move : unsigned char { kNone, kDiag, kDel, kIns };
    std::vector<double> cost((R + 1) * (H + 1), 0.0);
    std::vector<unsigned char> move((R + 1) * (H + 1), kNone);
    std::vector<double> sim(R * H + 1, 0.0);
    auto at = [H](std::size_t i, std::size_t j) { return i * (H + 1) + j; };

    for (std::size_t i = 0; i < R; ++i)
        for (std::size_t j = 0; j < H; ++j) sim[i * H + j] = word_similarity(ref[i], hyp[j]);

    for (std::size_t i = 1; i <= R; ++i) {
        cost[at(i, 0)] = static_cast<double>(i);
        move[at(i, 0)] = kDel;
    }
    for (std::size_t j = 1; j <= H; ++j) {
        cost[at(0, j)] = static_cast<double>(j);
        move[at(0, j)] = kIns;
    }
    const double eps = 1e-9;
    for (std::size_t i = 1; i <= R; ++i) {
        for (std::size_t j = 1; j <= H; ++j) {
            double s = sim[(i - 1) * H + (j - 1)];
            double sub = s >= 1.0 ? 0.0 : (s < opt.min_similarity ? 2.0 : opt.sub_weight * (1.0 - s));
            double cd = cost[at(i - 1, j - 1)] + sub;
            double cdel = cost[at(i - 1, j)] + 1.0;
            double cins = cost[at(i, j - 1)] + 1.0;
            // Tie-breaking: diagonal, then deletion (omission), then insertion.
            double best = cd;
            unsigned char m = kDiag;
            if (cdel < best - eps) {
                best = cdel;
                m = kDel;
            }
            if (cins < best - eps) {
                best = cins;
                m = kIns;
            }
            cost[at(i, j)] = best;
            move[at(i, j)] = m;
        }
    }

    WordAlignment out;
    out.ref.resize(R);
    out.cost = cost[at(R, H)];
    std::size_t i = R, j = H;
    while (i > 0 || j > 0) {
        unsigned char m = move[at(i, j)];
        if (m == kDiag) {
            double s = sim[(i - 1) * H + (j - 1)];
            RefWordAlignment& a = out.ref[i - 1];
            a.hyp_index = static_cast<int>(j - 1);
            a.similarity = s;
            a.status = ref[i - 1] == hyp[j - 1] ? WordStatus::Matched : WordStatus::Substituted;
            --i;
            --j;
        } else if (m == kDel) {
            out.ref[i - 1] = RefWordAlignment{};
            --i;
        } else {
            out.inserted.push_back(static_cast<int>(j - 1));
            --j;
        }
    }
    std::reverse(out.inserted.begin(), out.inserted.end());
    return out;
}

}  // namespace pron

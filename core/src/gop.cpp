#include "pron/gop.h"

#include <algorithm>
#include <limits>

namespace pron {

GopOptions GopOptions::preset(Strictness s) {
    GopOptions o;
    switch (s) {
        case Strictness::Lenient:
            o.good_gop = -0.5;
            o.bad_gop = -6.0;
            o.substitution_score = 45.0;
            break;
        case Strictness::Normal:
            break;
        case Strictness::Strict:
            o.good_gop = 1.5;
            o.bad_gop = -3.0;
            o.substitution_score = 70.0;
            break;
    }
    return o;
}

double gop_to_score(double gop, const GopOptions& opt) {
    if (opt.good_gop <= opt.bad_gop) return gop >= opt.good_gop ? 100.0 : 0.0;
    double x = (gop - opt.bad_gop) / (opt.good_gop - opt.bad_gop);
    return 100.0 * std::max(0.0, std::min(1.0, x));
}

PhonemeScore score_phoneme(const LogPosteriors& inv, int phoneme_id, int start, int end,
                           const GopOptions& opt) {
    PhonemeScore r;
    r.phoneme_id = phoneme_id;
    start = std::max(0, start);
    end = std::min(end, inv.frames);
    r.start_frame = start;
    r.end_frame = end;
    const int target = column_of_phoneme(phoneme_id);
    if (end <= start || target <= kBlankColumn || target >= inv.classes) return r;

    const int n = end - start;
    std::vector<double> mean(inv.classes, 0.0);
    double gop_sum = 0.0;
    for (int t = start; t < end; ++t) {
        double best_other = -std::numeric_limits<double>::infinity();
        for (int c = 0; c < inv.classes; ++c) {
            double v = inv.at(t, c);
            mean[c] += v;
            if (c != kBlankColumn && c != target) best_other = std::max(best_other, v);
        }
        gop_sum += inv.at(t, target) - best_other;
    }
    for (double& m : mean) m /= n;

    r.gop = gop_sum / n;
    r.score = gop_to_score(r.gop, opt);
    r.mean_log_prob = mean[target];
    int best_c = -1;
    for (int c = 0; c < inv.classes; ++c) {
        if (c == kBlankColumn || c == target) continue;
        if (best_c < 0 || mean[c] > mean[best_c]) best_c = c;
    }
    if (best_c >= 0) {
        r.likely_id = phoneme_of_column(best_c);
        r.likely_margin = mean[best_c] - mean[target];
        r.substituted = r.score < opt.substitution_score && r.likely_margin > 0.0;
    }
    return r;
}

PhonemeScore score_column(const LogPosteriors& m, int inventory_cols, int target_col, int expected_id,
                          const std::vector<int>& equivalent, int start, int end, const GopOptions& opt) {
    PhonemeScore r;
    r.phoneme_id = expected_id;
    start = std::max(0, start);
    end = std::min(end, m.frames);
    r.start_frame = start;
    r.end_frame = end;
    if (end <= start || target_col < 0 || target_col >= m.classes) return r;
    const int C = std::min(inventory_cols, m.classes);
    std::vector<bool> skip(static_cast<std::size_t>(C), false);
    skip[kBlankColumn] = true;
    for (int c : equivalent)
        if (c >= 0 && c < C) skip[static_cast<std::size_t>(c)] = true;

    const int n = end - start;
    std::vector<double> mean(static_cast<std::size_t>(C), 0.0);
    double gop_sum = 0.0, tgt_sum = 0.0;
    for (int t = start; t < end; ++t) {
        double best_other = -std::numeric_limits<double>::infinity();
        for (int c = 0; c < C; ++c) {
            double v = m.at(t, c);
            mean[static_cast<std::size_t>(c)] += v;
            if (!skip[static_cast<std::size_t>(c)]) best_other = std::max(best_other, v);
        }
        double tv = m.at(t, target_col);
        tgt_sum += tv;
        gop_sum += tv - best_other;
    }
    r.gop = gop_sum / n;
    r.score = gop_to_score(r.gop, opt);
    r.mean_log_prob = tgt_sum / n;
    int best_c = -1;
    for (int c = 0; c < C; ++c) {
        if (skip[static_cast<std::size_t>(c)]) continue;
        if (best_c < 0 || mean[static_cast<std::size_t>(c)] > mean[static_cast<std::size_t>(best_c)]) best_c = c;
    }
    if (best_c >= 0) {
        r.likely_id = phoneme_of_column(best_c);
        r.likely_margin = mean[static_cast<std::size_t>(best_c)] / n - r.mean_log_prob;
        r.substituted = r.score < opt.substitution_score && r.likely_margin > 0.0;
    }
    return r;
}

std::vector<PhonemeScore> score_alignment(const LogPosteriors& inv, const CtcAlignment& al,
                                          const GopOptions& opt) {
    std::vector<PhonemeScore> out;
    if (!al.ok) return out;
    out.reserve(al.spans.size());
    for (const auto& sp : al.spans)
        out.push_back(score_phoneme(inv, phoneme_of_column(sp.column), sp.start_frame, sp.end_frame, opt));
    return out;
}

}  // namespace pron

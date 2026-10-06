#include "pron/ctc_align.h"

#include <limits>

namespace pron {

int ctc_min_frames(const std::vector<int>& targets) {
    int n = static_cast<int>(targets.size());
    for (std::size_t i = 1; i < targets.size(); ++i)
        if (targets[i] == targets[i - 1]) ++n;
    return n;
}

CtcAlignment ctc_force_align(const LogPosteriors& lp, const std::vector<int>& targets, int blank) {
    CtcAlignment out;
    if (targets.empty() || !lp.valid() || lp.frames <= 0) return out;
    for (int c : targets)
        if (c < 0 || c >= lp.classes || c == blank) return out;
    if (blank < 0 || blank >= lp.classes) return out;
    const int T = lp.frames;
    if (T < ctc_min_frames(targets)) return out;

    const int L = static_cast<int>(targets.size());
    const int S = 2 * L + 1;
    auto label = [&](int s) { return (s % 2 == 0) ? blank : targets[s / 2]; };
    const double kNeg = -std::numeric_limits<double>::infinity();

    std::vector<double> prev(S, kNeg), cur(S, kNeg);
    std::vector<int> back(static_cast<std::size_t>(T) * S, -1);

    prev[0] = lp.at(0, blank);
    prev[1] = lp.at(0, label(1));
    for (int t = 1; t < T; ++t) {
        // States that can still reach the end in the remaining frames are not pruned
        // explicitly; impossible paths simply never reach the final states.
        for (int s = 0; s < S; ++s) {
            double best = prev[s];
            int arg = s;
            if (s >= 1 && prev[s - 1] > best) {
                best = prev[s - 1];
                arg = s - 1;
            }
            if (s >= 2 && s % 2 == 1 && label(s) != label(s - 2) && prev[s - 2] > best) {
                best = prev[s - 2];
                arg = s - 2;
            }
            if (best == kNeg) {
                cur[s] = kNeg;
                continue;
            }
            cur[s] = best + lp.at(t, label(s));
            back[static_cast<std::size_t>(t) * S + s] = arg;
        }
        std::swap(prev, cur);
    }

    int s = S - 1;
    if (S >= 2 && prev[S - 2] > prev[S - 1]) s = S - 2;
    if (prev[s] == kNeg) return out;
    out.log_prob = prev[s];

    std::vector<int> path(T);
    for (int t = T - 1; t >= 0; --t) {
        path[t] = s;
        if (t > 0) s = back[static_cast<std::size_t>(t) * S + s];
    }

    out.spans.resize(L);
    for (int i = 0; i < L; ++i) {
        out.spans[i].index = i;
        out.spans[i].column = targets[i];
        out.spans[i].start_frame = -1;
    }
    for (int t = 0; t < T; ++t) {
        int st = path[t];
        if (st % 2 == 0) continue;
        PhoneSpan& sp = out.spans[st / 2];
        if (sp.start_frame < 0) sp.start_frame = t;
        sp.end_frame = t + 1;
    }
    for (int i = 0; i < L; ++i) {
        if (out.spans[i].start_frame < 0) return CtcAlignment{};  // cannot happen for a valid path
        out.spans[i].region_end = (i + 1 < L) ? out.spans[i + 1].start_frame : T;
    }
    out.ok = true;
    return out;
}

}  // namespace pron

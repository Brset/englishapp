#include "pron/ctc_align.h"

#include <algorithm>
#include <limits>

namespace pron {

namespace {

CtcAlignment spans_from_path(const std::vector<int>& path, const std::vector<int>& targets) {
    CtcAlignment out;
    const int T = static_cast<int>(path.size());
    const int L = static_cast<int>(targets.size());
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

}  // namespace

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
    const double lpb = out.log_prob;
    out = spans_from_path(path, targets);
    out.log_prob = lpb;
    return out;
}

CtcAlignment ctc_force_align_banded(int T, const std::vector<int>& targets, const CtcEmitFn& emit,
                                    const std::vector<int>& lo, const std::vector<int>& hi) {
    CtcAlignment out;
    if (targets.empty() || T <= 0 || !emit) return out;
    if (lo.size() != targets.size() || hi.size() != targets.size()) return out;
    if (T < ctc_min_frames(targets)) return out;

    const int L = static_cast<int>(targets.size());
    const int S = 2 * L + 1;
    auto lab = [&](int s) { return (s % 2 == 0) ? -1 : s / 2; };  // emit() index, -1 = blank
    // Allowed frame range [a_s, b_s) per state; blanks span their two neighbours' windows.
    std::vector<int> a(S), b(S);
    for (int s = 0; s < S; ++s) {
        const int i = s / 2;
        if (s % 2 == 1) {
            a[s] = lo[i];
            b[s] = hi[i];
        } else {
            a[s] = i == 0 ? 0 : lo[i - 1];
            b[s] = i == L ? T : hi[i];
        }
        a[s] = std::max(0, std::min(a[s], T - 1));
        b[s] = std::max(a[s] + 1, std::min(b[s], T));
    }
    a[0] = 0;
    b[S - 1] = T;
    for (int s = 1; s < S; ++s) a[s] = std::max(a[s], a[s - 1]);   // monotone, so each frame's
    for (int s = S - 2; s >= 0; --s) b[s] = std::min(b[s], b[s + 1]);  // states form an interval
    for (int s = 0; s < S; ++s)
        if (b[s] <= a[s]) return out;  // windows contradict each other
    std::vector<int> smin(T), smax(T);
    {
        int lo_s = 0, hi_s = -1;
        for (int t = 0; t < T; ++t) {
            while (lo_s < S && b[lo_s] <= t) ++lo_s;
            while (hi_s + 1 < S && a[hi_s + 1] <= t) ++hi_s;
            smin[t] = lo_s;
            smax[t] = hi_s;
            if (smin[t] > smax[t]) return out;
        }
    }
    std::vector<std::size_t> off(static_cast<std::size_t>(T) + 1, 0);
    for (int t = 0; t < T; ++t) off[t + 1] = off[t] + static_cast<std::size_t>(smax[t] - smin[t] + 1);
    std::vector<unsigned char> back(off[T], 0);  // step taken into the state: 0, 1 or 2

    const double kNeg = -std::numeric_limits<double>::infinity();
    std::vector<double> prev(S, kNeg), cur(S, kNeg);
    if (smin[0] == 0) prev[0] = emit(0, -1);
    if (smax[0] >= 1) prev[1] = emit(0, 0);
    for (int t = 1; t < T; ++t) {
        for (int s = smin[t - 1]; s <= smax[t - 1]; ++s) cur[s] = kNeg;
        for (int s = smin[t]; s <= smax[t]; ++s) {
            double best = prev[s];
            int step = 0;
            if (s >= 1 && prev[s - 1] > best) {
                best = prev[s - 1];
                step = 1;
            }
            if (s >= 2 && s % 2 == 1 && targets[s / 2] != targets[s / 2 - 1] && prev[s - 2] > best) {
                best = prev[s - 2];
                step = 2;
            }
            cur[s] = best == kNeg ? kNeg : best + emit(t, lab(s));
            back[off[t] + static_cast<std::size_t>(s - smin[t])] = static_cast<unsigned char>(step);
        }
        for (int s = smin[t - 1]; s <= smax[t - 1]; ++s)
            if (s < smin[t] || s > smax[t]) prev[s] = kNeg;
        for (int s = smin[t]; s <= smax[t]; ++s) prev[s] = cur[s];
    }

    int s = S - 1;
    if (S >= 2 && smax[T - 1] >= S - 2 && smin[T - 1] <= S - 2 && prev[S - 2] > prev[S - 1]) s = S - 2;
    if (prev[s] == kNeg) return out;
    const double lpb = prev[s];

    std::vector<int> path(T);
    for (int t = T - 1; t >= 0; --t) {
        path[t] = s;
        if (t > 0) s -= back[off[t] + static_cast<std::size_t>(s - smin[t])];
    }
    out = spans_from_path(path, targets);
    out.log_prob = lpb;
    return out;
}

}  // namespace pron

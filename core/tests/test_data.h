#pragma once

#include <cmath>
#include <string>
#include <vector>

#include "pron/posteriors.h"

namespace testdata {

// Tiny CMUdict sample in the 0.7b format (with comments, variants and a malformed line).
inline const char* kCmuSample =
    ";;; # CMUdict  --  tiny test sample\n"
    ";;; comment lines start with three semicolons\n"
    "A  AH0\n"
    "A(1)  EY1\n"
    "ABANDON  AH0 B AE1 N D AH0 N\n"
    "BAD  B AE1 D\n"
    "BAT  B AE1 T\n"
    "BED  B EH1 D\n"
    "CAT  K AE1 T\n"
    "DOG  D AO1 G\n"
    "DON'T  D OW1 N T\n"
    "FORTY  F AO1 R T IY0\n"
    "HELLO  HH AH0 L OW1\n"
    "HOUSE  HH AW1 S\n"
    "I  AY1\n"
    "IS  IH1 Z\n"
    "MAT  M AE1 T\n"
    "ON  AA1 N\n"
    "RED  R EH1 D\n"
    "SAT  S AE1 T\n"
    "SEAT  S IY1 T\n"
    "SEE  S IY1\n"
    "SING  S IH1 NG\n"
    "SINK  S IH1 NG K\n"
    "SIT  S IH1 T\n"
    "TEACHER  T IY1 CH ER0\n"
    "THE  DH AH0\n"
    "THE(2)  DH AH1\n"
    "THE(3)  DH IY0\n"
    "THINK  TH IH1 NG K\n"
    "THIS  DH IH1 S\n"
    "TWO  T UW1\n"
    "VINE  V AY1 N\n"
    "WINE  W AY1 N\n"
    "WORLD  W ER1 L D\n"
    "BROKENLINE\n"
    "BADPHONE  B QQ1 D\n";

// Build a log-posterior matrix where frame t is dominated by column dom[t] with probability
// `p` and the remaining mass is spread uniformly over the other columns.
inline pron::LogPosteriors peaked(const std::vector<int>& dom, int classes, double p = 0.9,
                                  double frame_seconds = 0.02) {
    pron::LogPosteriors lp(static_cast<int>(dom.size()), classes, frame_seconds);
    const float hi = static_cast<float>(std::log(p));
    const float lo = static_cast<float>(std::log((1.0 - p) / (classes - 1)));
    for (int t = 0; t < lp.frames; ++t)
        for (int c = 0; c < classes; ++c) lp.at(t, c) = (c == dom[static_cast<std::size_t>(t)]) ? hi : lo;
    return lp;
}

}  // namespace testdata

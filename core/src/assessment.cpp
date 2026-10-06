#include "pron/assessment.h"

#include <algorithm>
#include <cmath>
#include <map>

#include "pron/ctc_align.h"

namespace pron {

const char* to_string(ScoreBand b) {
    switch (b) {
        case ScoreBand::Good: return "good";
        case ScoreBand::Fair: return "fair";
        case ScoreBand::Poor: return "poor";
        case ScoreBand::Omitted: return "omitted";
    }
    return "omitted";
}

const char* band_color(ScoreBand b) {
    switch (b) {
        case ScoreBand::Good: return "#2E7D32";
        case ScoreBand::Fair: return "#F9A825";
        case ScoreBand::Poor: return "#C62828";
        case ScoreBand::Omitted: return "#9E9E9E";
    }
    return "#9E9E9E";
}

const char* to_string(ScoredBy s) {
    switch (s) {
        case ScoredBy::None: return "none";
        case ScoredBy::Diff: return "diff";
        case ScoredBy::Gop: return "gop";
    }
    return "none";
}

std::size_t utf16_offset(const std::string& utf8, std::size_t byte_offset) {
    std::size_t units = 0;
    byte_offset = std::min(byte_offset, utf8.size());
    for (std::size_t i = 0; i < byte_offset; ++i) {
        unsigned char c = static_cast<unsigned char>(utf8[i]);
        if ((c & 0xC0) == 0x80) continue;  // continuation byte
        units += (c >= 0xF0) ? 2 : 1;      // 4-byte sequences need a surrogate pair
    }
    return units;
}

namespace {

struct HypWord {
    std::string norm;
    double start, end;
    float prob;
};

// Map each phone of `a` to a phone of `b` (or -1 if deleted) by minimum edit distance.
std::vector<int> align_phones(const Pronunciation& a, const Pronunciation& b) {
    const std::size_t n = a.size(), m = b.size();
    std::vector<std::vector<int>> d(n + 1, std::vector<int>(m + 1, 0));
    for (std::size_t i = 0; i <= n; ++i) d[i][0] = static_cast<int>(i);
    for (std::size_t j = 0; j <= m; ++j) d[0][j] = static_cast<int>(j);
    for (std::size_t i = 1; i <= n; ++i)
        for (std::size_t j = 1; j <= m; ++j)
            d[i][j] = std::min({d[i - 1][j - 1] + (a[i - 1].symbol == b[j - 1].symbol ? 0 : 1), d[i - 1][j] + 1,
                                d[i][j - 1] + 1});
    std::vector<int> map(n, -1);
    std::size_t i = n, j = m;
    while (i > 0 && j > 0) {
        int sub = a[i - 1].symbol == b[j - 1].symbol ? 0 : 1;
        if (d[i][j] == d[i - 1][j - 1] + sub) {
            map[i - 1] = static_cast<int>(j - 1);
            --i;
            --j;
        } else if (d[i][j] == d[i - 1][j] + 1) {
            --i;
        } else {
            --j;
        }
    }
    return map;
}

ScoreBand band_for(double score, const AssessmentOptions& opt) {
    if (score >= opt.good_threshold) return ScoreBand::Good;
    if (score >= opt.fair_threshold) return ScoreBand::Fair;
    return ScoreBand::Poor;
}

bool sentence_boundary_after(const std::string& text, const std::vector<Token>& toks, std::size_t i) {
    std::size_t b = toks[i].end;
    std::size_t e = i + 1 < toks.size() ? toks[i + 1].begin : text.size();
    for (std::size_t k = b; k < e && k < text.size(); ++k) {
        char c = text[k];
        if (c == '.' || c == '!' || c == '?' || c == ';' || c == ':' || c == '\n') return true;
    }
    return false;
}

}  // namespace

Assessor::Assessor() : lexicon_(&dict_, nullptr) {}

AssessmentResult Assessor::assess(const std::string& reference, const std::vector<AsrWord>& recognized,
                                  const LogPosteriors* posteriors) const {
    AssessmentResult res;
    res.reference = reference;
    const AssessmentOptions& opt = opt_;

    // 1. Reference tokens.
    std::vector<Token> toks = tokenize(reference, opt.tokenize);
    std::vector<std::string> ref_norm;
    for (const auto& t : toks) ref_norm.push_back(t.norm);

    // 2. Recognized words -> normalized hypothesis words (numbers expanded, time split evenly).
    std::vector<HypWord> hyp;
    TokenizeOptions hyp_opt = opt.tokenize;
    hyp_opt.skip_speaker_labels = false;
    for (const auto& w : recognized) {
        std::vector<Token> parts = tokenize(w.text, hyp_opt);
        if (parts.empty()) continue;
        double dur = std::max(0.0, w.end - w.start) / static_cast<double>(parts.size());
        for (std::size_t k = 0; k < parts.size(); ++k)
            hyp.push_back({parts[k].norm, w.start + dur * k, w.start + dur * (k + 1), w.probability});
    }
    std::vector<std::string> hyp_norm;
    for (const auto& h : hyp) hyp_norm.push_back(h.norm);

    // 3. Word alignment.
    WordAlignment al = align_words(ref_norm, hyp_norm, opt.align);
    res.hyp_to_ref.assign(hyp.size(), -1);
    for (std::size_t i = 0; i < al.ref.size(); ++i)
        if (al.ref[i].hyp_index >= 0) res.hyp_to_ref[al.ref[i].hyp_index] = static_cast<int>(i);
    {
        int last_ref = -1;
        std::size_t ins = 0;
        for (std::size_t j = 0; j < hyp.size(); ++j) {
            if (res.hyp_to_ref[j] >= 0) {
                last_ref = res.hyp_to_ref[j];
            } else if (ins < al.inserted.size() && al.inserted[ins] == static_cast<int>(j)) {
                res.inserted.push_back({static_cast<int>(j), hyp[j].norm, hyp[j].start, hyp[j].end, last_ref});
                ++ins;
            }
        }
    }

    // 4. Fluency.
    std::vector<TimedWord> timed;
    std::vector<bool> sent_end;
    for (std::size_t j = 0; j < hyp.size(); ++j) {
        timed.push_back({hyp[j].norm, hyp[j].start, hyp[j].end});
        int r = res.hyp_to_ref[j];
        sent_end.push_back(r >= 0 && sentence_boundary_after(reference, toks, static_cast<std::size_t>(r)));
    }
    res.fluency = compute_fluency(timed, opt.fluency, sent_end);

    // 5. Phoneme-level posteriors in inventory space.
    LogPosteriors inv;
    bool use_gop = false;
    if (posteriors) {
        if (!has_vocab_) {
            res.warnings.push_back("posteriors given but no phoneme vocabulary set; phoneme scoring skipped");
        } else if (!posteriors->valid() || posteriors->classes != vocab_.size()) {
            res.warnings.push_back("posterior matrix does not match the phoneme vocabulary; phoneme scoring skipped");
        } else {
            inv = vocab_.collapse(*posteriors);
            use_gop = true;
        }
    }
    res.phoneme_level = use_gop;

    // 6. Per-word results.
    std::map<std::string, std::size_t> advice_index;
    for (std::size_t i = 0; i < toks.size(); ++i) {
        WordResult w;
        w.index = static_cast<int>(i);
        w.text = toks[i].text;
        w.norm = toks[i].norm;
        w.byte_begin = toks[i].begin;
        w.byte_end = toks[i].end;
        w.u16_begin = utf16_offset(reference, toks[i].begin);
        w.u16_end = utf16_offset(reference, toks[i].end);
        const RefWordAlignment& a = al.ref[i];
        w.status = a.status;
        w.similarity = a.similarity;

        Pronunciation expected;
        w.pron_source = lexicon_.lookup(w.norm, expected);
        if (w.pron_source == PronSource::None)
            res.warnings.push_back("no pronunciation for word '" + w.norm + "'");
        w.expected_ipa = to_ipa_string(expected, true);
        w.stress_syllable = primary_stress_syllable(expected);
        for (const auto& ph : expected) {
            PhonemeResult pr;
            pr.arpabet = ph.symbol;
            pr.ipa = arpabet_to_ipa(ph.symbol);
            if (ph.symbol == "AH" && ph.stress == 0) pr.ipa = "ə";
            if (ph.symbol == "ER" && ph.stress == 0) pr.ipa = "ɚ";
            pr.stress = ph.stress;
            w.phonemes.push_back(pr);
        }

        if (a.status == WordStatus::Omitted) {
            w.score = 0.0;
            w.band = ScoreBand::Omitted;
            res.words.push_back(std::move(w));
            continue;
        }
        const HypWord& h = hyp[static_cast<std::size_t>(a.hyp_index)];
        w.recognized = h.norm;
        w.start = h.start;
        w.end = h.end;

        // 6a. Dictionary diff against the word the ASR heard instead.
        std::vector<int> diff_actual(expected.size(), -2);  // -2 = no info, -1 = deleted, else phoneme id
        if (a.status == WordStatus::Substituted && !expected.empty()) {
            Pronunciation heard;
            if (lexicon_.lookup(h.norm, heard) != PronSource::None) {
                std::vector<int> map = align_phones(expected, heard);
                for (std::size_t k = 0; k < expected.size(); ++k)
                    diff_actual[k] = map[k] < 0 ? -1 : phoneme_id(heard[static_cast<std::size_t>(map[k])].symbol);
            }
        }

        // 6b. Forced alignment + GOP.
        bool gop_done = false;
        if (use_gop && !expected.empty()) {
            const double fs = inv.frame_seconds;
            int f0 = static_cast<int>(std::floor((h.start - opt.word_padding_seconds) / fs));
            int f1 = static_cast<int>(std::ceil((h.end + opt.word_padding_seconds) / fs));
            f0 = std::max(0, f0);
            f1 = std::min(inv.frames, f1);
            std::vector<int> targets;
            for (const auto& ph : expected) targets.push_back(column_of_phoneme(phoneme_id(ph.symbol)));
            if (f1 - f0 < ctc_min_frames(targets)) {  // word span too short: widen symmetrically
                int need = ctc_min_frames(targets) - (f1 - f0);
                f0 = std::max(0, f0 - (need + 1) / 2);
                f1 = std::min(inv.frames, f0 + ctc_min_frames(targets) + 1);
            }
            LogPosteriors seg = inv.slice(f0, f1);
            CtcAlignment ca = ctc_force_align(seg, targets, kBlankColumn);
            if (ca.ok) {
                std::vector<PhonemeScore> ps = score_alignment(seg, ca, opt.gop);
                for (std::size_t k = 0; k < ps.size(); ++k) {
                    PhonemeResult& pr = w.phonemes[k];
                    pr.score = ps[k].score;
                    pr.gop = ps[k].gop;
                    pr.start = (f0 + ca.spans[k].start_frame) * fs;
                    pr.end = (f0 + ca.spans[k].region_end) * fs;
                    if (ps[k].substituted && ps[k].likely_id >= 0) {
                        pr.substituted = true;
                        pr.actual_arpabet = phoneme_info(ps[k].likely_id).arpabet;
                        pr.actual_ipa = phoneme_info(ps[k].likely_id).ipa;
                    }
                }
                gop_done = true;
            } else {
                res.warnings.push_back("forced alignment failed for word '" + w.norm + "'");
            }
        }

        // Fill substitutions from the dictionary diff where GOP gave none.
        bool diff_used = false;
        for (std::size_t k = 0; k < w.phonemes.size(); ++k) {
            PhonemeResult& pr = w.phonemes[k];
            int d = diff_actual[k];
            if (d == -2) continue;
            bool same = d >= 0 && phoneme_info(d).arpabet == pr.arpabet;
            if (!gop_done) {
                pr.score = same ? 100.0 : 0.0;
                diff_used = true;
            }
            if (!same && !pr.substituted && (!gop_done || pr.score < opt.advice_score_threshold)) {
                pr.substituted = true;
                if (d >= 0) {
                    pr.actual_arpabet = phoneme_info(d).arpabet;
                    pr.actual_ipa = phoneme_info(d).ipa;
                }
            }
        }
        w.scored_by = gop_done ? ScoredBy::Gop : (diff_used ? ScoredBy::Diff : ScoredBy::None);

        // 6c. Word score.
        if (gop_done || diff_used) {
            double sum = 0.0;
            for (const auto& pr : w.phonemes) sum += pr.score;
            w.score = w.phonemes.empty() ? 0.0 : sum / static_cast<double>(w.phonemes.size());
            if (a.status == WordStatus::Substituted) w.score = std::min(w.score, opt.substituted_word_cap);
            for (const auto& pr : w.phonemes)
                if (pr.substituted) w.score = std::min(w.score, opt.substituted_phone_word_cap);
        } else if (a.status == WordStatus::Matched) {
            double p = std::max(0.0f, std::min(1.0f, h.prob));
            w.score = 60.0 + 40.0 * p;
        } else {
            w.score = opt.substituted_word_cap * a.similarity;
        }
        w.band = band_for(w.score, opt);

        // 6d. Advice.
        for (std::size_t k = 0; k < w.phonemes.size(); ++k) {
            PhonemeResult& pr = w.phonemes[k];
            if (pr.score < 0 || pr.score >= opt.advice_score_threshold) continue;
            int exp_id = phoneme_id(pr.arpabet);
            int act_id = pr.actual_arpabet.empty() ? -1 : phoneme_id(pr.actual_arpabet);
            pr.advice = advice_.advise(exp_id, act_id, k + 1 == w.phonemes.size());
            if (!pr.advice) continue;
            auto it = advice_index.find(pr.advice->id);
            if (it == advice_index.end()) {
                advice_index[pr.advice->id] = res.advice.size();
                res.advice.push_back(AdviceSummary{*pr.advice, 0, {}});
                it = advice_index.find(pr.advice->id);
            }
            AdviceSummary& s = res.advice[it->second];
            ++s.count;
            if (s.words.empty() || s.words.back() != w.index) s.words.push_back(w.index);
        }
        res.words.push_back(std::move(w));
    }
    std::stable_sort(res.advice.begin(), res.advice.end(),
                     [](const AdviceSummary& x, const AdviceSummary& y) { return x.count > y.count; });

    // 7. Totals.
    int read = 0;
    double sum = 0.0;
    for (const auto& w : res.words) {
        if (w.status == WordStatus::Omitted) continue;
        ++read;
        sum += w.score;
    }
    res.accuracy = read ? sum / read : 0.0;
    res.completeness = toks.empty() ? 0.0 : 100.0 * read / static_cast<double>(toks.size());
    res.fluency_score = read ? res.fluency.score : 0.0;
    double wsum = opt.weight_accuracy + opt.weight_completeness + opt.weight_fluency;
    if (wsum > 0)
        res.overall = (opt.weight_accuracy * res.accuracy + opt.weight_completeness * res.completeness +
                       opt.weight_fluency * res.fluency_score) /
                      wsum;
    return res;
}

}  // namespace pron

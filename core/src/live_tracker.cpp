#include "pron/live_tracker.h"

#include <algorithm>
#include <cstddef>
#include <set>

#include "pron/assessment.h"
#include "pron/json_writer.h"

namespace pron {

namespace {

constexpr int kBack = 2;      // window: cursor - kBack ...
constexpr int kAhead = 8;     // ... cursor + kAhead
constexpr int kTailAhead = 3; // max forward jump for the (still unstable) last hypothesis word

std::size_t letters(const std::string& s) {
    std::size_t n = 0;
    for (unsigned char c : s) if (c != '\'') ++n;
    return n;
}

int edit_distance(const std::string& a, const std::string& b) {
    const std::size_t n = a.size(), m = b.size();
    std::vector<int> prev(m + 1), cur(m + 1);
    for (std::size_t j = 0; j <= m; ++j) prev[j] = static_cast<int>(j);
    for (std::size_t i = 1; i <= n; ++i) {
        cur[0] = static_cast<int>(i);
        for (std::size_t j = 1; j <= m; ++j) {
            const int sub = prev[j - 1] + (a[i - 1] == b[j - 1] ? 0 : 1);
            cur[j] = std::min({prev[j] + 1, cur[j - 1] + 1, sub});
        }
        std::swap(prev, cur);
    }
    return prev[m];
}

double similarity(const std::string& a, const std::string& b) {
    const std::size_t mx = std::max(a.size(), b.size());
    if (mx == 0) return 1.0;
    return 1.0 - static_cast<double>(edit_distance(a, b)) / static_cast<double>(mx);
}

}  // namespace

const char* to_string(LiveWordState s) {
    switch (s) {
        case LiveWordState::Pending: return "pending";
        case LiveWordState::Current: return "current";
        case LiveWordState::Read: return "read";
        case LiveWordState::Skipped: return "skipped";
    }
    return "pending";
}

bool is_function_word(const std::string& w) {
    static const std::set<std::string> kList = {
        "a", "an", "the", "of", "in", "on", "at", "to", "for", "and", "or", "is", "are", "was", "were", "it", "i",
        "you", "he", "she", "we", "they", "my", "your", "his", "her", "our", "their", "this", "that", "with", "by",
        "from", "as", "be", "but", "so", "not", "do", "does", "did", "have", "has", "had", "its", "than", "then",
        "there", "these", "those", "them", "me", "us", "him", "been", "will", "would", "can", "could", "if", "into",
        "about", "very", "just", "when", "what", "who", "which", "am", "no", "up", "out", "all"};
    return letters(w) <= 3 || kList.count(w) > 0;
}

bool live_words_match(const std::string& ref, const std::string& hyp, bool partial, std::size_t min_prefix) {
    if (ref.empty() || hyp.empty()) return false;
    if (ref == hyp) return true;
    if (similarity(ref, hyp) >= 0.7) return true;
    const std::size_t lr = letters(ref), lh = letters(hyp);
    if (lr >= 5 && lh >= 5 && ref.compare(0, 4, hyp, 0, 4) == 0) return true;
    // unfinished last word of a streaming hypothesis ("brot" for "brothers")
    if (partial && lh >= min_prefix && lh < lr && ref.compare(0, hyp.size(), hyp) == 0) return true;
    return false;
}

LiveTracker::LiveTracker(const std::string& reference_utf8) : reference_(reference_utf8) {
    tokens_ = tokenize(reference_);
    const std::size_t n = tokens_.size();
    ref_norm_.reserve(n);
    ref_func_.assign(n, 0);
    for (std::size_t i = 0; i < n; ++i) {
        ref_norm_.push_back(tokens_[i].norm);
        ref_func_[i] = is_function_word(tokens_[i].norm) ? 1 : 0;
        if (!ref_func_[i]) last_content_ = static_cast<int>(i);
    }
    marks_.assign(n, LiveWordState::Pending);
    done_ = n == 0;
    publish();
    published_ = false;  // first update reports changed_from = 0
}

void LiveTracker::publish() {
    const int n = word_count();
    LiveState s;
    s.cursor = cursor_;
    s.scroll_to = std::min(cursor_, n);
    s.done = done_;
    s.words = marks_;
    if (!done_ && cursor_ >= 0 && cursor_ < n) s.words[cursor_] = LiveWordState::Current;
    s.changed_from = n;
    if (!published_) {
        s.changed_from = 0;
    } else {
        for (int i = 0; i < n; ++i)
            if (s.words[i] != state_.words[i]) { s.changed_from = i; break; }
    }
    state_ = std::move(s);
    published_ = true;
}

int LiveTracker::process_word(const std::vector<std::string>& hyp, size_t i, bool final) {
    const int n = word_count();
    if (cursor_ >= n) return 1;
    const std::string& h = hyp[i];
    const bool is_tail = (i + 1 == hyp.size());
    const bool hfunc = is_function_word(h);
    const int lo = std::max(0, cursor_ - kBack);
    const int hi = std::min(n - 1, cursor_ + (is_tail && !final ? kTailAhead : kAhead));

    int best = -1;
    double best_cost = 1e9;
    for (int j = lo; j <= hi; ++j) {
        // After the flush the recognizer may still leave the very last word cut ("fa" for "fast"):
        // accept a 2+ letter prefix, but only for the word the reader was expected to say next.
        const bool cut_last = is_tail && final && j == cursor_;
        if (!live_words_match(ref_norm_[j], h, (is_tail && !final) || cut_last, cut_last ? 2 : 3)) continue;
        double cost = (1.0 - similarity(ref_norm_[j], h)) * 4.0;
        if (j >= cursor_) {
            if (hfunc) {  // a function word may only be reached over other function words
                bool ok = true;
                for (int g = cursor_; g < j; ++g) if (!ref_func_[g]) { ok = false; break; }
                if (!ok) continue;
            }
            cost += 0.3 * (j - cursor_);
        } else {
            // re-reading something already passed: only interesting if it repairs a "skipped" word
            // or is the nearest repetition; slightly dispreferred against moving forward.
            cost += 0.5 + 0.4 * (cursor_ - j);
        }
        if (cost < best_cost) { best_cost = cost; best = j; }
    }
    if (best < 0) return 1;  // recognizer garbage / unrelated word: ignore (counts as "heard something")
    const long abs_i = static_cast<long>(base_ + i);
    const long unmatched = std::max(0L, abs_i - last_anchor_abs_ - 1);

    if (best < cursor_) {
        if (marks_[best] == LiveWordState::Skipped) marks_[best] = LiveWordState::Read;
        last_anchor_abs_ = abs_i;
        return 1;
    }

    // Skipping two or more content words needs confirmation by the next hypothesis word.
    int skipped_content = 0;
    for (int g = cursor_; g < best; ++g) if (!ref_func_[g]) ++skipped_content;
    if (skipped_content >= 2 && !final) {
        if (is_tail) return 0;  // wait for the next word
        bool confirmed = false;
        const std::string& nx = hyp[i + 1];
        for (int j = best + 1; j <= std::min(n - 1, best + 3); ++j)
            if (live_words_match(ref_norm_[j], nx, i + 2 == hyp.size())) { confirmed = true; break; }
        if (!confirmed) return 1;
    }

    // Unmatched hypothesis words between two anchors are misrecognised words: they stand in for
    // (at most as many) content words of the gap, which therefore count as read, not skipped.
    long forgive = unmatched;
    for (int g = cursor_; g < best; ++g) {
        if (ref_func_[g]) marks_[g] = LiveWordState::Read;
        else if (forgive > 0) { marks_[g] = LiveWordState::Read; --forgive; }
        else marks_[g] = LiveWordState::Skipped;
    }
    last_anchor_abs_ = abs_i;
    marks_[best] = LiveWordState::Read;
    cursor_ = best + 1;
    return 1;
}

const LiveState& LiveTracker::update(const std::vector<std::string>& hypothesis, bool final) {
    // Only the last kTail raw words of the (ever growing) recognizer hypothesis are re-tokenized per call; the
    // older prefix is stable and only its normalized word count is kept. Per-call cost is O(kTail), not O(9 min).
    constexpr std::size_t kTail = 48, kSlack = 48;
    if (hypothesis.size() < pre_raw_) { pre_raw_ = 0; pre_norm_ = 0; }
    TokenizeOptions opt;
    opt.skip_speaker_labels = false;
    const auto norm_words = [&](std::size_t from, std::size_t to) {
        std::string joined;
        for (std::size_t k = from; k < to; ++k) { joined += hypothesis[k]; joined += ' '; }
        std::vector<std::string> v;
        for (const Token& t : tokenize(joined, opt)) if (!t.norm.empty()) v.push_back(t.norm);
        return v;
    };
    if (hypothesis.size() > pre_raw_ + kTail + kSlack) {
        const std::size_t to = hypothesis.size() - kTail;
        pre_norm_ += norm_words(pre_raw_, to).size();
        pre_raw_ = to;
    }
    const std::vector<std::string> tail = norm_words(pre_raw_, hypothesis.size());
    const std::size_t off = pre_norm_;
    const std::size_t total = off + tail.size();

    const std::size_t prev_n = last_off_ + last_hyp_.size();
    if (total < base_) base_ = total;
    if (base_ < off) {  // cannot be that far behind: the stable prefix was consumed long ago
        consumed_ = consumed_ > off - base_ ? consumed_ - (off - base_) : 0;
        base_ = off;
    }
    std::vector<std::string> hyp(tail.begin() + static_cast<std::ptrdiff_t>(base_ - off), tail.end());

    // where to resume: the unstable last word may have been revised since the previous call
    std::size_t resume = std::min(consumed_, hyp.size());
    const std::size_t prev_eff = prev_n > base_ ? prev_n - base_ : 0;
    if (prev_eff > 0) {
        const std::size_t li = prev_eff - 1;
        const std::size_t abs_li = base_ + li;
        if (li < hyp.size() && abs_li >= last_off_ && abs_li - last_off_ < last_hyp_.size() &&
            hyp[li] != last_hyp_[abs_li - last_off_])
            resume = std::min(resume, li);
        if (hyp.size() < prev_eff) resume = hyp.empty() ? 0 : hyp.size() - 1;
    }
    last_hyp_ = tail;
    last_off_ = off;

    std::size_t i = resume;
    for (; i < hyp.size(); ++i) {
        if (done_) { i = hyp.size(); break; }
        if (process_word(hyp, i, final) == 0) break;
        // finished? (last content word read and only function words remain)
        if (last_content_ >= 0 ? cursor_ > last_content_ : cursor_ >= word_count()) {
            for (int g = cursor_; g < word_count(); ++g) marks_[g] = LiveWordState::Read;
            cursor_ = word_count();
            done_ = true;
        }
    }
    consumed_ = i;
    publish();
    return state_;
}

const LiveState& LiveTracker::set_cursor(int word_index) {
    const int n = word_count();
    const int c = std::max(0, std::min(word_index, n));
    for (int g = c; g < n; ++g) marks_[g] = LiveWordState::Pending;
    cursor_ = c;
    done_ = (n == 0) || (c >= n);
    // everything the recognizer has heard so far belongs to the old position
    base_ = last_off_ + last_hyp_.size();
    consumed_ = 0;
    last_anchor_abs_ = static_cast<long>(base_) - 1;
    publish();
    return state_;
}

std::string live_state_json(const LiveTracker& t, const std::string& partial) {
    const LiveState& s = t.state();
    JsonWriter j;
    j.begin_object();
    j.kv("cursor", s.cursor);
    j.kv("scroll_to", s.scroll_to);
    j.kv("done", s.done);
    j.kv("partial", partial);
    // Token offsets are non-decreasing: advance one running UTF-16 counter instead of rescanning the text per
    // word (O(N) instead of O(N^2) for a 5000-word reference, once per audio chunk).
    std::size_t cur_byte = 0, cur_units = 0;
    const auto u16_at = [&](std::size_t byte) {
        if (byte < cur_byte) { cur_byte = 0; cur_units = 0; }
        cur_units += utf16_offset(t.reference().substr(cur_byte, byte - cur_byte), byte - cur_byte);
        cur_byte = std::min(byte, t.reference().size());
        return cur_units;
    };
    j.key("words").begin_array();
    for (std::size_t i = 0; i < s.words.size(); ++i) {
        j.begin_object();
        j.kv("i", static_cast<int>(i));
        j.kv("state", to_string(s.words[i]));
        j.kv("u16_begin", u16_at(t.tokens()[i].begin));
        j.kv("u16_end", u16_at(t.tokens()[i].end));
        j.end_object();
    }
    j.end_array();
    j.kv("changed_from", s.changed_from);
    j.end_object();
    return j.take();
}

}  // namespace pron

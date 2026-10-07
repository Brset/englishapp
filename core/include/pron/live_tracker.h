// Live reading tracker: aligns a growing recognizer hypothesis to the reference text and keeps a
// per-word state (read / skipped / current / pending). Pure C++, no recognizer dependency.
#pragma once

#include <string>
#include <vector>

#include "pron/text.h"

namespace pron {

enum class LiveWordState { Pending = 0, Current, Read, Skipped };
const char* to_string(LiveWordState s);

struct LiveState {
    int cursor = 0;      // index of the next expected word (== word count when done)
    int scroll_to = 0;   // word index the UI should keep visible
    bool done = false;
    int changed_from = 0;  // smallest word index whose state changed since the previous update (== word count: none)
    std::vector<LiveWordState> words;  // one per reference token (Current at the cursor)
};

// True for short function words (<= 3 letters or in a fixed list): they may be misrecognized or
// dropped without counting as skipped.
bool is_function_word(const std::string& norm);
// Fuzzy word match: edit-distance similarity >= 0.7, or equal first 4 letters for long words.
bool live_words_match(const std::string& ref_norm, const std::string& hyp_norm, bool hyp_is_partial = false);

class LiveTracker {
public:
    explicit LiveTracker(const std::string& reference_utf8);

    const std::string& reference() const { return reference_; }
    const std::vector<Token>& tokens() const { return tokens_; }
    int word_count() const { return static_cast<int>(tokens_.size()); }

    // `hypothesis` = the FULL current recognizer word list (any case, punctuation, digits ok).
    // `final` = the recognizer is flushed: the last hypothesis word is committed unconditionally.
    const LiveState& update(const std::vector<std::string>& hypothesis, bool final = false);
    // Manual jump (user tapped a word). Words before it are reset to pending/read: those before the
    // new cursor keep their state, the cursor word and everything after become pending.
    const LiveState& set_cursor(int word_index);
    const LiveState& state() const { return state_; }

private:
    void publish();
    int process_word(const std::vector<std::string>& hyp, size_t i, bool final);  // 1 consumed, 0 deferred

    std::string reference_;
    std::vector<Token> tokens_;
    std::vector<std::string> ref_norm_;
    std::vector<char> ref_func_;
    int last_content_ = -1;
    std::vector<LiveWordState> marks_;  // Pending/Read/Skipped only
    int cursor_ = 0;
    bool done_ = false;
    LiveState state_;
    bool published_ = false;
    // hypothesis bookkeeping
    size_t base_ = 0;       // hypothesis words before this index belong to the time before set_cursor()
    long last_anchor_abs_ = -1;  // absolute index (in the full hypothesis) of the last matched word
    size_t consumed_ = 0;   // normalized hypothesis words (>= base_) already processed
    std::vector<std::string> last_hyp_;  // normalized TAIL of the previous hypothesis (absolute index = last_off_ + i)
    std::size_t last_off_ = 0;           // normalized words before last_hyp_[0]
    std::size_t pre_raw_ = 0;            // raw hypothesis words already folded into pre_norm_ (stable prefix)
    std::size_t pre_norm_ = 0;           // normalized word count of that prefix
};

// State JSON as documented in pron_live.h ("partial" = raw recognizer text).
std::string live_state_json(const LiveTracker& t, const std::string& partial);

}  // namespace pron

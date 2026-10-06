// Frame-level log-posterior matrices and mapping of model vocabularies to the phoneme inventory.
#pragma once

#include <string>
#include <vector>

namespace pron {

// Row-major T x C matrix of natural-log probabilities (log-softmax output).
struct LogPosteriors {
    int frames = 0;
    int classes = 0;
    double frame_seconds = 0.02;  // wav2vec2: 20 ms hop
    std::vector<float> data;

    LogPosteriors() = default;
    LogPosteriors(int t, int c, double frame_sec = 0.02)
        : frames(t), classes(c), frame_seconds(frame_sec),
          data(static_cast<std::size_t>(t) * static_cast<std::size_t>(c), -1e9f) {}

    float at(int t, int c) const { return data[static_cast<std::size_t>(t) * classes + c]; }
    float& at(int t, int c) { return data[static_cast<std::size_t>(t) * classes + c]; }
    bool valid() const {
        return frames >= 0 && classes > 0 && data.size() == static_cast<std::size_t>(frames) * classes;
    }
    // Frames [begin, end) as a new matrix (clamped to the valid range).
    LogPosteriors slice(int begin, int end) const;
};

// Column layout of an inventory-space ("collapsed") posterior matrix:
//   column 0          = CTC blank
//   column 1 + id     = phoneme id from phonemes.h
constexpr int kBlankColumn = 0;
inline int column_of_phoneme(int phoneme_id) { return phoneme_id + 1; }
inline int phoneme_of_column(int column) { return column - 1; }
int inventory_columns();  // phoneme_count() + 1

// Mapping of a model's output labels (IPA, ARPAbet, or special tokens) to inventory ids.
class PhonemeVocab {
public:
    PhonemeVocab() = default;
    // Labels are matched as IPA first, then as ARPAbet. Special tokens ("<pad>", "|", "<unk>")
    // and unknown labels are left unmapped.
    PhonemeVocab(std::vector<std::string> labels, int blank_index);

    const std::vector<std::string>& labels() const { return labels_; }
    int blank_index() const { return blank_; }
    int size() const { return static_cast<int>(labels_.size()); }
    // Phoneme id for a label index, -1 if unmapped (or blank).
    int phoneme_of_label(int label) const { return map_[label]; }
    std::vector<std::string> unmapped_labels() const;
    // Number of labels mapped to an inventory phoneme.
    int mapped_count() const;
    // True if label is a word delimiter ("|" or " "); it is folded into the blank column.
    bool is_delimiter(int label) const { return delim_[label]; }
    // True if at least one model label feeds this inventory column (blank is always present).
    bool has_column(int column) const { return column >= 0 && column < static_cast<int>(present_.size()) && present_[column]; }

    // Collapse model posteriors into inventory space: for each phoneme, log-sum-exp of all
    // labels mapped to it; phonemes without a label get a very low log-probability.
    LogPosteriors collapse(const LogPosteriors& model) const;

private:
    std::vector<std::string> labels_;
    std::vector<int> map_;
    std::vector<bool> delim_;
    std::vector<bool> present_;
    int blank_ = 0;
};

}  // namespace pron

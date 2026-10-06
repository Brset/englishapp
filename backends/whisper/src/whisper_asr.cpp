#include "pron/whisper_asr.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "whisper.h"

namespace pron {

struct WhisperAsr::Impl {
    whisper_context* ctx = nullptr;
    int n_threads = 4;
    bool dtw = false;
    ~Impl() { if (ctx) whisper_free(ctx); }
};

WhisperAsr::WhisperAsr(const std::string& model_path, int n_threads, int use_dtw) : impl_(new Impl) {
    impl_->n_threads = std::max(1, n_threads);
    bool dtw = use_dtw < 0 ? model_path.find("base.en") != std::string::npos : use_dtw != 0;
    whisper_context_params cp = whisper_context_default_params();
    cp.use_gpu = false;
    if (dtw) {
        cp.dtw_token_timestamps = true;
        cp.dtw_aheads_preset = WHISPER_AHEADS_BASE_EN;
    }
    impl_->dtw = dtw;
    impl_->ctx = whisper_init_from_file_with_params(model_path.c_str(), cp);
    if (!impl_->ctx) throw std::runtime_error("WhisperAsr: failed to load model: " + model_path);
}

WhisperAsr::~WhisperAsr() = default;

std::vector<AsrWord> WhisperAsr::transcribe(const AudioView& audio, const AsrOptions& opt) {
    std::vector<AsrWord> words;
    if (!audio.samples || audio.count == 0) return words;
    if (audio.sample_rate != WHISPER_SAMPLE_RATE)
        throw std::runtime_error("WhisperAsr: audio must be 16 kHz mono");

    whisper_full_params p = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
    p.n_threads = impl_->n_threads;
    p.language = opt.language.empty() ? "en" : opt.language.c_str();
    p.translate = false;
    p.single_segment = false;
    p.token_timestamps = true;
    p.print_progress = p.print_realtime = p.print_timestamps = p.print_special = false;
    if (!opt.initial_prompt.empty()) p.initial_prompt = opt.initial_prompt.c_str();

    if (whisper_full(impl_->ctx, p, audio.samples, static_cast<int>(audio.count)) != 0)
        throw std::runtime_error("WhisperAsr: whisper_full failed");

    const whisper_token eot = whisper_token_eot(impl_->ctx);
    const int nseg = whisper_full_n_segments(impl_->ctx);
    double psum = 0;
    int pcount = 0;
    auto flush = [&]() {
        if (!words.empty() && pcount > 0) words.back().probability = float(psum / pcount);
        psum = 0;
        pcount = 0;
    };
    for (int s = 0; s < nseg; ++s) {
        const int nt = whisper_full_n_tokens(impl_->ctx, s);
        for (int t = 0; t < nt; ++t) {
            const whisper_token_data d = whisper_full_get_token_data(impl_->ctx, s, t);
            if (d.id >= eot) continue;  // special tokens ([_BEG_], timestamps, ...)
            const char* txt = whisper_full_get_token_text(impl_->ctx, s, t);
            if (!txt || !*txt) continue;
            double t0 = d.t0 / 100.0, t1 = d.t1 / 100.0;
            if (impl_->dtw && d.t_dtw >= 0) t0 = d.t_dtw / 100.0;
            const bool starts_word = txt[0] == ' ' || words.empty();
            if (starts_word) {
                flush();
                AsrWord w;
                w.text = txt;
                w.start = t0;
                w.end = std::max(t0, t1);
                words.push_back(std::move(w));
            } else {
                AsrWord& w = words.back();
                w.text += txt;
                w.end = std::max(w.end, t1);
            }
            psum += d.p;
            ++pcount;
        }
    }
    flush();
    // With DTW, t_dtw marks token starts: close each word at the next word's start.
    if (impl_->dtw)
        for (size_t i = 0; i + 1 < words.size(); ++i)
            words[i].end = std::max(words[i].start, words[i + 1].start);
    return words;
}

}  // namespace pron

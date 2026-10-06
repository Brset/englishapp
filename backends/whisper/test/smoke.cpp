#include <cstdio>
#include <fstream>
#include <vector>

#include "pron/whisper_asr.h"

int main(int argc, char** argv) {
    const std::string path = argc > 1 ? argv[1] : "models/for-tests-ggml-tiny.en.bin";
    if (!std::ifstream(path, std::ios::binary).good()) {
        std::printf("SKIP: model not found: %s\n", path.c_str());
        return 77;
    }
    try {
        pron::WhisperAsr asr(path, 2);
        std::vector<float> silence(16000, 0.0f);
        pron::AudioView a;
        a.samples = silence.data();
        a.count = silence.size();
        a.sample_rate = 16000;
        auto words = asr.transcribe(a, pron::AsrOptions{});
        std::printf("OK: %zu words\n", words.size());
        for (auto& w : words) {
            if (w.end < w.start) { std::printf("FAIL: bad times\n"); return 1; }
        }
    } catch (const std::exception& e) {
        std::printf("FAIL: %s\n", e.what());
        return 1;
    }
    return 0;
}

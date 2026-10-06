#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>

#include "pron/piper_tts.h"

static void write_wav(const std::string& path, const std::vector<float>& s, int sr) {
    std::ofstream f(path, std::ios::binary);
    auto w32 = [&](uint32_t v) { f.write(reinterpret_cast<char*>(&v), 4); };
    auto w16 = [&](uint16_t v) { f.write(reinterpret_cast<char*>(&v), 2); };
    uint32_t bytes = uint32_t(s.size() * 2);
    f.write("RIFF", 4); w32(36 + bytes); f.write("WAVEfmt ", 8); w32(16); w16(1); w16(1);
    w32(sr); w32(sr * 2); w16(2); w16(16); f.write("data", 4); w32(bytes);
    for (float x : s) { float c = x > 1 ? 1 : (x < -1 ? -1 : x); w16(uint16_t(int16_t(std::lrint(c * 32767)))); }
}

static double rms(const std::vector<float>& s) {
    double a = 0;
    for (float x : s) a += double(x) * x;
    return s.empty() ? 0 : std::sqrt(a / s.size());
}

int main(int argc, char** argv) {
    if (argc < 3) return 2;
    std::string dir = argv[1], out = argv[2];
    std::ifstream probe(dir + "/tokens.txt");
    if (!probe) { std::puts("SKIP: voice not available"); return 77; }
    pron::PiperTts tts(dir + "/en_US-lessac-medium.onnx", dir + "/tokens.txt", dir + "/espeak-ng-data");
    int sr = tts.sample_rate();
    auto a = tts.synthesize("Think about the weather.", 1.0f);
    auto b = tts.synthesize("Think about the weather.", 0.5f);
    std::printf("sr=%d n1=%zu (%.2fs) n05=%zu (%.2fs) rms=%.4f\n", sr, a.size(), double(a.size()) / sr, b.size(),
                double(b.size()) / sr, rms(a));
    write_wav(out + "/think_about_the_weather_1.0.wav", a, sr);
    write_wav(out + "/think_about_the_weather_0.5.wav", b, sr);
    if (sr <= 0 || a.empty() || b.empty()) return 1;
    if (rms(a) < 1e-3 || rms(b) < 1e-3) return 1;
    if (b.size() <= a.size()) return 1;
    return 0;
}

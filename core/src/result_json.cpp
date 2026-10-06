#include "pron/result_json.h"

#include "pron/json_writer.h"

namespace pron {
namespace {

void write_advice_fields(JsonWriter& j, const Advice& a) {
    j.kv("id", a.id);
    j.kv("sound_id", a.sound_id);
    j.kv("expected_ipa", a.expected_ipa);
    j.kv("actual_ipa", a.actual_ipa);
    j.kv("title_ru", a.title_ru);
    j.kv("tip_ru", a.tip_ru);
}

void write_time(JsonWriter& j, const char* k, double t) {
    j.key(k);
    if (t < 0) {
        j.null();
    } else {
        j.value(t, 3);
    }
}

}  // namespace

std::string advice_to_json(const Advice& a, bool pretty) {
    JsonWriter j(pretty);
    j.begin_object();
    write_advice_fields(j, a);
    j.end_object();
    return j.take();
}

std::string tokens_to_json(const std::string& source, const std::vector<Token>& tokens, bool pretty) {
    JsonWriter j(pretty);
    j.begin_array();
    for (const auto& t : tokens) {
        j.begin_object();
        j.kv("text", t.text);
        j.kv("norm", t.norm);
        j.kv("byte_begin", t.begin);
        j.kv("byte_end", t.end);
        j.kv("u16_begin", utf16_offset(source, t.begin));
        j.kv("u16_end", utf16_offset(source, t.end));
        j.kv("from_number", t.from_number);
        j.end_object();
    }
    j.end_array();
    return j.take();
}

std::string to_json(const AssessmentResult& r, bool pretty) {
    JsonWriter j(pretty);
    j.begin_object();
    j.kv("version", kResultJsonVersion);

    j.key("scores").begin_object();
    j.kv("accuracy", r.accuracy, 1);
    j.kv("completeness", r.completeness, 1);
    j.kv("fluency", r.fluency_score, 1);
    j.kv("overall", r.overall, 1);
    j.end_object();
    j.kv("phoneme_level", r.phoneme_level);
    j.key("phoneme_debug").begin_object();
    j.kv("frames", r.post_frames);
    j.kv("classes", r.post_classes);
    j.kv("used", r.post_used);
    j.kv("reason", r.post_reason);
    j.end_object();

    j.key("words").begin_array();
    for (const auto& w : r.words) {
        j.begin_object();
        j.kv("index", w.index);
        j.kv("text", w.text);
        j.kv("norm", w.norm);
        j.kv("byte_begin", w.byte_begin);
        j.kv("byte_end", w.byte_end);
        j.kv("u16_begin", w.u16_begin);
        j.kv("u16_end", w.u16_end);
        j.kv("status", to_string(w.status));
        j.kv("recognized", w.recognized);
        j.kv("similarity", w.similarity, 3);
        write_time(j, "start", w.start);
        write_time(j, "end", w.end);
        j.kv("score", w.score, 1);
        j.kv("band", to_string(w.band));
        j.kv("color", band_color(w.band));
        j.kv("pron_source", to_string(w.pron_source));
        j.kv("expected_ipa", w.expected_ipa);
        j.kv("stress_syllable", w.stress_syllable);
        j.kv("scored_by", to_string(w.scored_by));
        j.key("phonemes").begin_array();
        for (const auto& p : w.phonemes) {
            j.begin_object();
            j.kv("arpabet", p.arpabet);
            j.kv("ipa", p.ipa);
            j.kv("stress", p.stress);
            j.key("score");
            if (p.score < 0) {
                j.null();
            } else {
                j.value(p.score, 1);
            }
            j.kv("gop", p.gop, 3);
            write_time(j, "start", p.start);
            write_time(j, "end", p.end);
            j.kv("substituted", p.substituted);
            j.kv("actual_arpabet", p.actual_arpabet);
            j.kv("actual_ipa", p.actual_ipa);
            j.key("advice_id");
            if (p.advice) {
                j.value(p.advice->id);
            } else {
                j.null();
            }
            j.end_object();
        }
        j.end_array();
        j.end_object();
    }
    j.end_array();

    j.key("inserted").begin_array();
    for (const auto& w : r.inserted) {
        j.begin_object();
        j.kv("text", w.text);
        j.kv("start", w.start, 3);
        j.kv("end", w.end, 3);
        j.kv("after_word", w.after_ref_word);
        j.end_object();
    }
    j.end_array();

    const FluencyResult& f = r.fluency;
    // Reference word a pause follows: nearest aligned recognized word at or before it.
    auto ref_of = [&](int hyp) {
        if (hyp >= static_cast<int>(r.hyp_to_ref.size())) hyp = static_cast<int>(r.hyp_to_ref.size()) - 1;
        for (; hyp >= 0; --hyp)
            if (r.hyp_to_ref[hyp] >= 0) return r.hyp_to_ref[hyp];
        return -1;
    };
    j.key("fluency").begin_object();
    j.kv("word_count", f.word_count);
    j.kv("speech_seconds", f.speech_seconds, 3);
    j.kv("articulation_seconds", f.articulation_seconds, 3);
    j.kv("words_per_minute", f.words_per_minute, 1);
    j.kv("articulation_wpm", f.articulation_wpm, 1);
    j.key("long_pauses").begin_array();
    for (const auto& p : f.long_pauses) {
        j.begin_object();
        j.kv("after_word", ref_of(p.after_word));
        j.kv("start", p.start, 3);
        j.kv("end", p.end, 3);
        j.kv("duration", p.duration(), 3);
        j.end_object();
    }
    j.end_array();
    j.kv("repetitions", static_cast<int>(f.repetitions.size()));
    j.kv("hesitations", static_cast<int>(f.hesitations.size()));
    j.end_object();

    j.key("advice").begin_array();
    for (const auto& a : r.advice) {
        j.begin_object();
        write_advice_fields(j, a.advice);
        j.kv("count", a.count);
        j.key("words").begin_array();
        for (int w : a.words) j.value(w);
        j.end_array();
        j.end_object();
    }
    j.end_array();

    j.key("warnings").begin_array();
    for (const auto& w : r.warnings) j.value(w);
    j.end_array();

    j.end_object();
    return j.take();
}

}  // namespace pron

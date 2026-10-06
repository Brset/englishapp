#include "pron/advice.h"

#include "pron/phonemes.h"

namespace pron {
namespace {

const char* const kTipTh =
    "Звук θ (think) — глухой межзубный. Кончик языка слегка высуньте между зубами "
    "(или прижмите к краю верхних зубов) и выдыхайте воздух через щель между языком "
    "и зубами. Губы не участвуют, язык не упирается в альвеолы.";
const char* const kTipDh =
    "Звук ð (this) — звонкий межзубный: положение языка как для θ (кончик между зубами), "
    "но с голосом. Звук тянется, язык не ударяет по зубам.";
const char* const kTipW =
    "Звук w — не «в»: нижняя губа не касается верхних зубов. Округлите губы трубочкой, "
    "как для «у», и быстро разведите их, переходя к гласному: we, what, window.";
const char* const kTipAe =
    "Звук æ (cat) — широкий, между «а» и «э». Опустите нижнюю челюсть ниже, чем для "
    "русского «э», язык плоско лежит внизу, уголки губ слегка растянуты.";
const char* const kTipR =
    "Английский r не раскатывается: кончик языка загнут чуть назад и не касается нёба, "
    "вибрации нет. Губы слегка округлены. В британском варианте r после гласного не "
    "произносится (car, more).";
const char* const kTipH =
    "Английский h — лёгкий выдох без шума, как будто дышите на стекло. Не поднимайте "
    "заднюю часть языка к нёбу, как в русском «х».";
const char* const kTipNg =
    "Звук ŋ (sing) произносится задней частью языка, прижатой к мягкому нёбу (как для «к»), "
    "но воздух идёт через нос. Не заменяйте его на «н» и не добавляйте «к» в конце: "
    "sing [sɪŋ], а не [sɪnk].";
const char* const kTipFinal =
    "Звонкий согласный в конце слова оглушён, как в русском «дуб» [дуп]. В английском это "
    "меняет смысл (bad — bat, dog — dock, eyes — ice). Сохраняйте голос до конца слова "
    "и немного удлиняйте гласный перед звонким согласным.";

std::vector<AdviceRule> builtin_rules() {
    std::vector<AdviceRule> r;
    auto add = [&](const char* exp, const char* act, Position pos, const char* id, const char* sound,
                   const char* title, std::string tip) {
        r.push_back(AdviceRule{exp, act, pos, id, sound, title, std::move(tip)});
    };
    const Position A = Position::Any;

    // θ
    add("TH", "S", A, "th_s", "θ", "θ звучит как «с»",
        std::string(kTipTh) + " Сравните: sink — think, sick — thick.");
    add("TH", "F", A, "th_f", "θ", "θ звучит как «ф»",
        std::string(kTipTh) + " Нижняя губа расслаблена. Сравните: fin — thin, free — three.");
    add("TH", "T", A, "th_t", "θ", "θ звучит как «т»",
        std::string(kTipTh) + " Воздух проходит непрерывно, без взрыва. Сравните: tree — three, tin — thin.");
    add("TH", "*", A, "th", "θ", "Неточный звук θ", kTipTh);
    // ð
    add("DH", "Z", A, "dh_z", "ð", "ð звучит как «з»",
        std::string(kTipDh) + " Сравните: zen — then, breeze — breathe.");
    add("DH", "D", A, "dh_d", "ð", "ð звучит как «д»",
        std::string(kTipDh) + " Сравните: day — they, dare — there.");
    add("DH", "V", A, "dh_v", "ð", "ð звучит как «в»",
        std::string(kTipDh) + " Губы не участвуют. Сравните: van — than.");
    add("DH", "*", A, "dh", "ð", "Неточный звук ð", kTipDh);
    // w / v
    add("W", "V", A, "w_v", "w", "w звучит как «в»",
        std::string(kTipW) + " Сравните: vest — west, vine — wine.");
    add("W", "*", A, "w", "w", "Неточный звук w", kTipW);
    add("V", "W", A, "v_w", "v", "v звучит как w",
        "Звук v — как русское «в»: верхние зубы касаются нижней губы. Сравните: wine — vine, west — vest.");
    // æ
    add("AE", "EH", A, "ae_e", "æ", "æ звучит как «э»",
        std::string(kTipAe) + " Сравните: bed — bad, men — man, pen — pan.");
    add("AE", "E", A, "ae_e", "æ", "æ звучит как «э»",
        std::string(kTipAe) + " Сравните: bed — bad, men — man, pen — pan.");
    add("AE", "AA", A, "ae_a", "æ", "æ звучит как «а»",
        std::string(kTipAe) + " Сравните: cart — cat, heart — hat.");
    add("AE", "AH", A, "ae_a", "æ", "æ звучит как «а»",
        std::string(kTipAe) + " Сравните: cut — cat, bud — bad.");
    add("AE", "*", A, "ae", "æ", "Неточный звук æ", kTipAe);
    // ɪ / iː
    add("IH", "IY", A, "ih_iy", "ɪ", "Краткий ɪ звучит как долгий iː",
        "Краткий ɪ (sit) — короткий и расслабленный, ближе к «ы/и» в безударном слоге. "
        "Не растягивайте его и не улыбайтесь. Сравните: seat — sit, sheep — ship, leave — live.");
    add("IY", "IH", A, "iy_ih", "iː", "Долгий iː звучит как краткий ɪ",
        "Долгий iː (see) — напряжённый и длинный, уголки губ растянуты как в улыбке. "
        "Сравните: sit — seat, ship — sheep, live — leave.");
    // ʊ / uː
    add("UH", "UW", A, "uh_uw", "ʊ", "Краткий ʊ звучит как долгий uː",
        "Краткий ʊ (book) — короткий и расслабленный, губы округлены слабо. "
        "Сравните: pool — pull, fool — full, Luke — look.");
    add("UW", "UH", A, "uw_uh", "uː", "Долгий uː звучит как краткий ʊ",
        "Долгий uː (food) — губы сильно округлены и вытянуты вперёд, звук тянется. "
        "Сравните: full — fool, pull — pool.");
    // Final devoicing.
    const char* const kPairs[][2] = {{"B", "P"}, {"D", "T"},  {"G", "K"},  {"V", "F"},
                                     {"Z", "S"}, {"DH", "TH"}, {"ZH", "SH"}, {"JH", "CH"}};
    for (const auto& p : kPairs)
        add(p[0], p[1], Position::WordFinal, "final_devoicing", "final-voiced",
            "Оглушение звонкого согласного в конце слова", kTipFinal);
    // r
    add("R", "RR", A, "r_trill", "r", "Раскатистый русский «р»", kTipR);
    add("R", "*", A, "r", "r", "Неточный звук r", kTipR);
    // h
    add("HH", "X", A, "h_x", "h", "h звучит как «х»", std::string(kTipH) + " Сравните: hat, house, who.");
    add("HH", "*", A, "h", "h", "Неточный звук h", kTipH);
    // ŋ
    add("NG", "N", A, "ng_n", "ŋ", "ŋ звучит как «н»",
        std::string(kTipNg) + " Сравните: thin — thing, win — wing.");
    add("NG", "K", A, "ng_nk", "ŋ", "ŋ звучит как «нк»", std::string(kTipNg) + " Сравните: sink — sing.");
    add("NG", "*", A, "ng", "ŋ", "Неточный звук ŋ", kTipNg);
    return r;
}

}  // namespace

AdviceEngine::AdviceEngine() : rules_(builtin_rules()) {}

AdviceEngine AdviceEngine::empty() { return AdviceEngine(EmptyTag{}); }

void AdviceEngine::add_rule(AdviceRule r) { rules_.push_back(std::move(r)); }

const AdviceRule* AdviceEngine::find(int expected_id, int actual_id, bool word_final) const {
    if (expected_id < 0 || expected_id >= phoneme_count()) return nullptr;
    const std::string exp = phoneme_info(expected_id).arpabet;
    const std::string act = (actual_id >= 0 && actual_id < phoneme_count()) ? phoneme_info(actual_id).arpabet : "";
    const AdviceRule* best = nullptr;
    int best_rank = 0;
    for (const auto& r : rules_) {
        if (r.expected != exp) continue;
        int rank = 0;
        bool exact = !act.empty() && r.actual == act;
        if (r.position == Position::WordFinal) {
            if (!word_final || !exact) continue;
            rank = 3;
        } else if (exact) {
            rank = 2;
        } else if (r.actual == "*") {
            rank = 1;
        } else {
            continue;
        }
        if (rank > best_rank) {
            best = &r;
            best_rank = rank;
        }
    }
    return best;
}

std::optional<Advice> AdviceEngine::advise(int expected_id, int actual_id, bool word_final) const {
    const AdviceRule* r = find(expected_id, actual_id, word_final);
    if (!r) return std::nullopt;
    Advice a;
    a.id = r->id;
    a.sound_id = r->sound_id;
    a.expected_ipa = phoneme_info(expected_id).ipa;
    if (actual_id >= 0 && actual_id < phoneme_count()) a.actual_ipa = phoneme_info(actual_id).ipa;
    a.title_ru = r->title_ru;
    a.tip_ru = r->tip_ru;
    return a;
}

}  // namespace pron

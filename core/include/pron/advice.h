// Advice engine: typical substitutions of Russian speakers -> Russian-language tips.
#pragma once

#include <optional>
#include <string>
#include <vector>

namespace pron {

enum class Position { Any, WordFinal };

struct AdviceRule {
    std::string expected;  // ARPAbet of the expected phoneme ("TH")
    std::string actual;    // ARPAbet of the produced phoneme, or "*" for any / unknown
    Position position = Position::Any;
    std::string id;        // stable advice id ("th_s")
    std::string sound_id;  // training sound id, same vocabulary as content focus_sounds ("θ")
    std::string title_ru;  // short title, UTF-8
    std::string tip_ru;    // articulation tip, UTF-8
};

struct Advice {
    std::string id;
    std::string sound_id;
    std::string expected_ipa;
    std::string actual_ipa;  // "" when unknown
    std::string title_ru;
    std::string tip_ru;
};

class AdviceEngine {
public:
    // Loads the built-in table of typical Russian-speaker errors.
    AdviceEngine();
    // Empty engine (for custom tables).
    static AdviceEngine empty();

    void add_rule(AdviceRule r);
    const std::vector<AdviceRule>& rules() const { return rules_; }

    // Best rule for (expected, actual) phoneme ids; actual_id = -1 if unknown.
    // Priority: word-final rule > exact actual match > wildcard "*".
    const AdviceRule* find(int expected_id, int actual_id, bool word_final) const;
    std::optional<Advice> advise(int expected_id, int actual_id, bool word_final) const;

private:
    struct EmptyTag {};
    explicit AdviceEngine(EmptyTag) {}
    std::vector<AdviceRule> rules_;
};

}  // namespace pron

// JSON serialization of results consumed by the apps (through the C API).
#pragma once

#include <string>
#include <vector>

#include "pron/assessment.h"
#include "pron/text.h"

namespace pron {

// Version of the JSON layout; bump on incompatible changes.
constexpr int kResultJsonVersion = 1;

std::string to_json(const AssessmentResult& r, bool pretty = false);
// [{"text":..,"norm":..,"byte_begin":..,"byte_end":..,"u16_begin":..,"u16_end":..,"from_number":..}]
std::string tokens_to_json(const std::string& source, const std::vector<Token>& tokens, bool pretty = false);
std::string advice_to_json(const Advice& a, bool pretty = false);

}  // namespace pron

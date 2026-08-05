// Tiny shared helpers for the event-log string lists (codex unlocks, quest
// bookkeeping). Existed as byte-identical static twins in effect_applicator.cpp
// and quests/quest_engine.cpp — one home now.
#pragma once

#include <algorithm>
#include <string>
#include <vector>

namespace sm {

inline void push_unique_string(std::vector<std::string>& values,
                               const std::string& value) {
    if (value.empty()) return;
    if (std::find(values.begin(), values.end(), value) == values.end()) {
        values.push_back(value);
    }
}

inline void push_string(std::vector<std::string>& values,
                        const std::string& value) {
    if (!value.empty()) values.push_back(value);
}

} // namespace sm

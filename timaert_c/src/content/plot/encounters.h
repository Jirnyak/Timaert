// Random encounter table — pure data, mirrors plot/encounters.ts.
//
// Each encounter has a title, body text, and 1..3 player choices.
// Each choice carries a list of GameEvent effects to apply when chosen.
// The table is built once on first access (depends on heap strings), then
// returned by const reference. The encounter trigger lives in main.cpp's
// per-tick code; the modal renderer lives in ui/overlays.cpp.
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "events/event_types.h"

namespace sm::content {

struct EncounterChoice {
    std::string label;
    std::vector<GameEvent> effects; // empty = no-op (e.g. "Leave")
};

struct EncounterDef {
    std::string title;
    std::string body;
    std::vector<EncounterChoice> choices;
};

// Build (or return cached) encounter table.
const std::vector<EncounterDef>& encounters();

} // namespace sm::content

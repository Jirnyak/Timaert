#include "macro/landmark_registry.h"
#include "macro/landmark_iter.h"
#include "macro/state.h"
#include "macro/markers.h"
#include <cstdio>

namespace sm {

namespace {

const char* mood_label(SettlementMood m) {
    switch (m) {
        case SettlementMood::Prosperous: return "prosperous";
        case SettlementMood::Stable:     return "stable";
        case SettlementMood::Tense:      return "tense";
        case SettlementMood::Unrest:     return "unrest";
        case SettlementMood::Revolt:     return "revolt";
    }
    return "?";
}

const char* marker_kind(MarkerStyle s) {
    switch (s) {
        case MarkerStyle::Quest:    return "quest";
        case MarkerStyle::POI:      return "poi";
        case MarkerStyle::Danger:   return "danger";
        case MarkerStyle::Waypoint: return "waypoint";
    }
    return "?";
}

// The per-kind DETAIL line is the one thing the registry row cannot express —
// it reads kind-specific state. Everything else (kind label, colour, the id
// prefix) comes from the row.
void detail_line(const LandmarkView& lm, char* buf, std::size_t n) {
    switch (lm.type) {
        case LandmarkType::City:
            std::snprintf(buf, n, "pop %d \xC2\xB7 %s",
                          lm.population, mood_label(lm.mood));
            return;
        case LandmarkType::Village:
            std::snprintf(buf, n, "pop %d", lm.population);
            return;
        case LandmarkType::Spire:
            std::snprintf(buf, n, "%s", lm.depleted ? "depleted" : "active");
            return;
        default:
            buf[0] = '\0';
            return;
    }
}

} // namespace

std::vector<LandmarkEntry> collect_landmarks(const GameState& gs) {
    std::vector<LandmarkEntry> out;
    out.reserve(gs.settlements.size() + gs.villages.size()
              + gs.spires.size() + gs.markers.size());

    for_each_landmark(gs, [&](const LandmarkView& lm) {
        const LandmarkDef& def = landmark_def(lm.type);
        char buf[64];
        detail_line(lm, buf, sizeof(buf));
        out.push_back({std::string(def.label),
                       std::string(def.id) + "_" + std::to_string(lm.id),
                       lm.name, buf, lm.x, lm.y, def.color});
    });

    // Markers are the mobile pin layer (markers.h), not a placed landmark
    // kind — they keep their own colour and enumeration.
    constexpr std::uint32_t kColorMarker = 0xFF67E8F9u; // cyan-300
    for (const auto& m : gs.markers) {
        out.push_back({"Marker",
                       "marker_" + m.id,
                       m.label.empty() ? m.id : m.label,
                       marker_kind(m.style),
                       int(m.x), int(m.y), kColorMarker});
    }

    return out;
}

} // namespace sm

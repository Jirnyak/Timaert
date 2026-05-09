#include "macro/landmark_registry.h"
#include "macro/state.h"
#include "macro/markers.h"
#include <cstdio>

namespace sm {

namespace {

// Match TS Tailwind class colours from landmark-registry.ts as ARGB.
// text-amber-300, text-lime-300, text-purple-300, text-cyan-300.
constexpr std::uint32_t kColorCity    = 0xFFFCD34Du; // amber-300
constexpr std::uint32_t kColorVillage = 0xFFBEF264u; // lime-300
constexpr std::uint32_t kColorSpire   = 0xFFD8B4FEu; // purple-300
constexpr std::uint32_t kColorMarker  = 0xFF67E8F9u; // cyan-300

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

} // namespace

std::vector<LandmarkEntry> collect_landmarks(const GameState& gs) {
    std::vector<LandmarkEntry> out;
    out.reserve(gs.settlements.size() + gs.villages.size()
              + gs.spires.size() + gs.markers.size());

    char buf[64];

    for (const auto& s : gs.settlements) {
        std::snprintf(buf, sizeof(buf), "pop %d \xC2\xB7 %s",
                      s.population, mood_label(s.mood));
        out.push_back({"City",
                       "city_" + std::to_string(s.id),
                       s.name, buf, s.x, s.y, kColorCity});
    }
    for (const auto& v : gs.villages) {
        std::snprintf(buf, sizeof(buf), "pop %d", v.population);
        out.push_back({"Village",
                       "village_" + std::to_string(v.id),
                       v.name, buf, v.x, v.y, kColorVillage});
    }
    for (const auto& sp : gs.spires) {
        std::snprintf(buf, sizeof(buf), "spell 0x%08X",
                      static_cast<unsigned>(sp.spellId));
        out.push_back({"Spire",
                       "spire_" + std::to_string(sp.id),
                       sp.depleted ? "Depleted Spire" : "Spire",
                       sp.depleted ? "depleted" : "active",
                       sp.x, sp.y, kColorSpire});
        (void)buf;
    }
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

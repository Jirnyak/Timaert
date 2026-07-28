// Universal markers (quests, POI, danger, waypoints). Mirrors markers.ts.
#pragma once
#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace sm {

enum class MarkerStyle : std::uint8_t { Quest = 0, POI, Danger, Waypoint };

struct Marker {
    std::string id;
    MarkerStyle style;
    float x, y;
    std::string label;
};

// Glyph + ARGB colour per style, indexed by enum value. Colours match TS
// MARKER_COLORS (CSS hex -> 0xAARRGGBB). Glyphs mostly match TS
// (markers.ts MARKER_GLYPHS); the quest glyph deliberately diverges from
// TS "?" to "!" — an owner call: a quest pin on the world map reads as an
// exclamation, not a question. NB colours here are 0xAARRGGBB, NOT
// ImGui-packed; ImGui renderers must repack via IM_COL32 (see
// macro_overlay.cpp `marker_imcol`).
inline constexpr const char* kMarkerGlyph[4] = {
    "!",                  // quest    (owner: "!" not TS "?")
    "\xE2\x98\x85",       // poi      ★ U+2605
    "!",                  // danger
    "\xE2\x97\x86",       // waypoint ◆ U+25C6
};
inline constexpr std::uint32_t kMarkerColor[4] = {
    0xFFFFD700u,  // quest    #ffd700 gold
    0xFF87CEEBu,  // poi      #87ceeb sky-blue
    0xFFFF4444u,  // danger   #ff4444 red
    0xFF90EE90u,  // waypoint #90ee90 light-green
};

inline bool has_marker(const std::vector<Marker>& m, const std::string& id) {
    for (const auto& mk : m) if (mk.id == id) return true;
    return false;
}

inline void add_marker(std::vector<Marker>& m, std::string id, MarkerStyle s,
                       float x, float y, std::string label = {}) {
    if (has_marker(m, id)) return;  // TS dedups by id
    m.push_back({std::move(id), s, x, y, std::move(label)});
}

inline void remove_marker(std::vector<Marker>& m, const std::string& id) {
    auto it = std::find_if(m.begin(), m.end(),
        [&](const Marker& mk) { return mk.id == id; });
    if (it != m.end()) m.erase(it);
}

inline void remove_markers_by_prefix(std::vector<Marker>& m, const std::string& prefix) {
    m.erase(std::remove_if(m.begin(), m.end(),
        [&](const Marker& mk) { return mk.id.rfind(prefix, 0) == 0; }),
        m.end());
}

} // namespace sm

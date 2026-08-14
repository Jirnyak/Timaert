// Landmark PRESENTATION rows — the UI half of the landmark registry, bound to
// macro/landmark_registry.h by enum index (the Rule 13 shape: data row below,
// presentation bound by id above; the static_assert below refuses a drifted
// parallel table — the kInteractRows lesson). Colour is NOT here: the macro
// row's `color` is the ONE colour authority for map draw, minimap and lists
// alike (owner ruling 2026-08-14).
//
// Adding a landmark kind = its registry row + one row here (+ a yield in
// macro/landmark_iter.h when it gains storage). Kinds without drawn art use
// SpriteId::Count_ — sprite_get() returns null and the draw falls back to a
// glyph circle in the row colour.
#pragma once
#include <cstddef>
#include <cstdint>

#include "assets/sprite_atlas.h"
#include "macro/landmark_registry.h"

namespace sm::ui {

// Minimap mark shape. Ring = filled dot + dark outline (cities), Dot = bare
// filled dot (villages), Diamond = 4-point poly + dark outline (spires).
enum class MiniShape : std::uint8_t { Ring, Dot, Diamond };

struct LandmarkDrawRow {
    LandmarkType type;        // MUST equal its index — asserted below
    SpriteId sprite;          // Count_ = no art, glyph-circle fallback
    SpriteId spriteDepleted;  // variant for a consumed landmark (== sprite
                              // when the kind cannot deplete)
    float basePx;             // landmark_size() floor at low zoom
    float maxPx;              // landmark_size() cap at high zoom
    float minZoom;            // hide below this zoom (0 = always visible)
    float labelZoom;          // draw the name at/above this zoom (0 = never)
    MiniShape mini;
    float miniR;              // minimap mark radius, px
};

inline constexpr LandmarkDrawRow
    kLandmarkDraw[std::size_t(LandmarkType::Count)] = {
    {LandmarkType::None,    SpriteId::Count_, SpriteId::Count_,
     0.0f, 0.0f, 0.0f, 0.0f, MiniShape::Dot, 0.0f},
    {LandmarkType::City,    SpriteId::City, SpriteId::City,
     28.0f, 192.0f, 0.0f, 6.0f, MiniShape::Ring, 3.0f},
    {LandmarkType::Village, SpriteId::Village, SpriteId::Village,
     22.0f, 144.0f, 3.0f, 0.0f, MiniShape::Dot, 1.6f},
    {LandmarkType::Spire,   SpriteId::Spire, SpriteId::SpireDark,
     26.0f, 160.0f, 0.0f, 0.0f, MiniShape::Diamond, 4.0f},
    {LandmarkType::Ruin,    SpriteId::Count_, SpriteId::Count_,
     22.0f, 144.0f, 0.0f, 0.0f, MiniShape::Dot, 1.6f},
    {LandmarkType::Lair,    SpriteId::Count_, SpriteId::Count_,
     22.0f, 144.0f, 0.0f, 0.0f, MiniShape::Dot, 1.6f},
    {LandmarkType::Shrine,  SpriteId::Count_, SpriteId::Count_,
     22.0f, 144.0f, 0.0f, 0.0f, MiniShape::Dot, 1.6f},
    {LandmarkType::Mine,    SpriteId::Count_, SpriteId::Count_,
     22.0f, 144.0f, 0.0f, 0.0f, MiniShape::Dot, 1.6f},
    {LandmarkType::Tower,   SpriteId::Count_, SpriteId::Count_,
     22.0f, 144.0f, 0.0f, 0.0f, MiniShape::Dot, 1.6f},
};

constexpr bool landmark_draw_rows_match() {
    for (std::size_t i = 0; i < std::size_t(LandmarkType::Count); ++i)
        if (kLandmarkDraw[i].type != LandmarkType(i)) return false;
    return true;
}
static_assert(landmark_draw_rows_match(),
              "kLandmarkDraw row order must mirror LandmarkType — the enum "
              "index IS the binding");

inline constexpr const LandmarkDrawRow& landmark_draw(LandmarkType t) {
    return kLandmarkDraw[std::size_t(t)];
}

} // namespace sm::ui

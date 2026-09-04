// THE pre-world palette and the two geometry helpers every shell screen
// shares. One table, so the title menu, the studio splash, the slideshow and
// character creation cannot drift apart colour by colour.
//
// The palette is keyed to the painted backdrop (assets/backgrounds/0.png):
// wheat and parchment for text, grass for the living accent, dulled bronze
// for frames and counters, warm near-black for panels. Fabled
// sword-and-sorcery, not grimdark — the owner's verdict (2026-09-04).
#pragma once

#include "imgui.h"

namespace sm::ui {

inline constexpr ImU32 kPalParchment = IM_COL32(230, 217, 176, 255);
inline constexpr ImU32 kPalParchDim  = IM_COL32(168, 154, 120, 255);
inline constexpr ImU32 kPalGrass     = IM_COL32(122, 166,  60, 255);
inline constexpr ImU32 kPalBronze    = IM_COL32(138, 113,  63, 255);
inline constexpr ImU32 kPalNight     = IM_COL32( 16,  13,   8, 255);

// Blood, three shades — the splash's swarm and the runnels down the blade.
inline constexpr ImU32 kPalBloodDeep = IM_COL32( 96,  14,  10, 255);
inline constexpr ImU32 kPalBlood     = IM_COL32(140,  24,  16, 255);
inline constexpr ImU32 kPalBloodHot  = IM_COL32(170,  36,  24, 255);

// Steel, fuller and leather — the sword's materials once its pixels settle.
inline constexpr ImU32 kPalSteel     = IM_COL32(205, 199, 186, 255);
inline constexpr ImU32 kPalFuller    = IM_COL32(158, 154, 144, 255);
inline constexpr ImU32 kPalLeather   = IM_COL32( 74,  52,  30, 255);

// All ImGui geometry is in *logical points* (DisplaySize), never in drawable
// pixels. On a Retina display the SDL drawable size is 2x the window size, so
// passing drawable pixels here would push a menu off screen. Always read the
// size from ImGui itself.
inline ImVec2 viewport_size() {
    return ImGui::GetIO().DisplaySize;
}

// The painted title backdrop, covering the whole viewport: crop, never
// letterbox — a backdrop must reach every edge. Everything derives from
// DisplaySize on THIS frame; nothing is cached against the window size, which
// is exactly the resize bug both reference intros suffered (a resize event
// rebuilt or restarted their scene) and these screens refuse to inherit.
// Falls back to a plain dark wash when the art is missing.
void draw_title_backdrop();

} // namespace sm::ui

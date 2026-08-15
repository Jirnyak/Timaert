// Full-screen macro map page (the M key) — the map IS the world seen through
// a second camera. The SAME macro fragment synth draws the basemap (main.cpp
// hands MacroRendererVk::record this camera instead of the live one while the
// page is open) and the SAME draw_macro_overlay draws landmarks, pins,
// walkers and the player under it — ONE world renderer, ONE overlay, two
// cameras. The knowledge law (macro/knowledge.h) therefore applies to the
// page for free: black terra incognita, graphite memory, living sight.
//
// This module owns only what is genuinely the page's: the camera with its
// DERIVED zoom floor (the whole world fitted to the viewport; the ceiling is
// the live view's own maximum), the open-edge anchoring on the player, and
// the chrome — a header readout and a legend built from the landmark/marker
// registries, listing only what the player has actually discovered.
//
// It replaced the 256×256 CPU-baked minimap window (ui/overlays.cpp
// MiniMapCache, deleted): the map is now the shipping shader at any zoom.
#pragma once

namespace sm {
struct GameState;
}

namespace sm::ui {

struct MapScreenState {
    // Map camera, world cells / drawable-px-per-cell — same units as the
    // live camera. zoom == 0 marks "never opened": the first open lands at
    // the world-fit floor.
    float camX = 0.0f, camY = 0.0f;
    float zoom = 0.0f;
    // Rising-edge detector for the open anchoring (any toggle path: key,
    // toolbar, smoke) — the draw path owns it so there is ONE open door.
    bool wasOpen = false;
    // Explored-cell census for the header, recounted only when the
    // knowledge revision moves (the page pauses the world, so that is a
    // quest reveal or the opening sweep, never per frame).
    unsigned long long exploredCells = 0;
    unsigned int censusRev = ~0u;
};

// The page's zoom floor: the whole world's height fitted to the viewport —
// derived, not tuned. (Width fits too on any non-portrait window; the torus
// wraps the sides regardless.)
float map_fit_zoom(int viewHPx, int mapH);

// The page chrome: header readout (size, seed, position, explored share,
// discovered landmark counts), the registry-driven legend, and the player's
// own pins (rename / centre-on / remove — placement is the page's
// double-click, main.cpp). Draws on top of the shader basemap + world
// overlay; writes *open = false when the user closes the page. `gs` is
// mutable for exactly one reason: the pin list edits gs.markers.
// `viewW/viewH` in logical points, like every ImGui panel.
void draw_map_screen(MapScreenState& st, GameState& gs, bool* open,
                     int viewW, int viewH, float scale);

} // namespace sm::ui

// Full-screen macro map page (the M key) — a MENU over the game, drawing an
// IMAGE of the world, never the world itself (owner ruling 2026-08-15). The
// basemap is the CHART composition of the macro shader (pc.mapStyle: flat
// atlas colours, cell-sharp inked water, ink roads, fixed relief light — no
// clock, no motion); everything on top is this module's OWN drawing, in
// primitives only: landmark MiniShapes at the ONE registry colour, marker
// ink, a plain position mark for the player. The world overlay — paperdolls,
// walkers, travel routes, click-to-travel — is NOT drawn while the page is
// open; a map click ANNOTATES (pin toggle) and commands nothing.
//
// The module owns the page camera with its DERIVED zoom floor (the whole
// world fitted to the viewport; ceiling = the live view's maximum; first
// open = their geometric mean), the chrome readout, the registry-driven
// legend (only what the player has discovered) and the pin list.
//
// It replaced the 256×256 CPU-baked minimap window (ui/overlays.cpp
// MiniMapCache, deleted): the map is now the shipping shader at any zoom.
#pragma once

namespace sm {
struct GameState;
struct TerrainData;
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

// The WHOLE page above the chart basemap: hover + tooltip + click-pin,
// landmark primitives, marker ink, the player mark, and the chrome (readout,
// legend, pin list). Writes *open = false when the user closes the page.
// `gs` is mutable for exactly one reason: pins edit gs.markers. `viewW/viewH`
// and `zoomLogical` in logical points (the page camera's zoom / dpr), like
// every ImGui surface.
void draw_map_screen(MapScreenState& st, GameState& gs,
                     const TerrainData& terrain, bool* open,
                     int viewW, int viewH, float zoomLogical, float scale);

} // namespace sm::ui

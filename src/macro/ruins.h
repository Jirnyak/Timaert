// Ruin placement — worldgen pass for the Ruin landmark (landmark_registry.h
// row "ruin"), §42 Инк 5. Half the registry could never be born: the row,
// the subworld route (SubworldMode::Ruin) and the surface generator
// (gen_ruin) all stood ready while no pass pushed a Ruin record — which is
// why the demo's scene 4 («разведать руины») had no target in the world.
// The site law mirrors generate_spires: the danger-zone field already
// encodes "far from civilization", so the row's own zone band IS the
// distance law, and a Mitchell best-candidate pick spreads the ruins over
// it without a tuned separation constant. Souls at birth come from the
// registry row's born columns × the site's danger byte
// (landmark_born_population) — a ruin in redder land haunts harder.
#pragma once
#include <cstdint>

namespace sm {

struct GameState;
struct TerrainData;
struct ZoneLayer;

// Push Ruin landmarks into gs.landmarks — one per city the world raised
// plus a base handful, so ruin density follows civilization's own scale.
// Deterministic from gs.worldSeed. Requires zones — call AFTER
// generate_zones (and after the settlement passes, whose cells it avoids).
// A world with no admissible land simply gets fewer ruins (logged).
void generate_ruins(GameState& gs, const ZoneLayer& zones,
                    const TerrainData& terrain, std::uint8_t seaLevel8);

} // namespace sm

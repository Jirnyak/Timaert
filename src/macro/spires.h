// Spire placement — worldgen pass for the Spire landmark (landmark_registry.h
// row "spire"). One learnable spell = one spire the world must offer; the
// spire's site is decided by the danger-zone field, not by dice against
// civilization: zones already encode "far from cities" (civ BFS pulls danger
// down), so the zone gate IS the distance law. The spell's tier picks how deep
// into the wild band the spire must stand — read straight from the spell
// registry (macro/spells.h), which lives in the world layers precisely so
// worldgen can ask it (ARCHITECTURE.md Rule 13).
#pragma once
#include <cstdint>

namespace sm {

struct GameState;
struct TerrainData;
struct ZoneLayer;

// Fill gs.spires (assumed cleared by populate_landmarks_from_politik) — one
// spire per kSpellDefs row, Spire.spellId = the row's append-only ordinal.
// Deterministic from gs.worldSeed. Requires zones — call AFTER
// generate_zones. A spell whose zone band does not exist on this world gets
// its gate relaxed down to the table minimum; a world with no admissible land
// at all simply lacks that spire (logged).
void generate_spires(GameState& gs, const ZoneLayer& zones,
                     const TerrainData& terrain, std::uint8_t seaLevel8);

} // namespace sm

// Spire placement — worldgen pass for the Spire landmark (landmark_registry.h
// row "spire"). One learnable spell = one spire the world must offer; the
// spire's site is decided by the danger-zone field, not by dice against
// civilization: zones already encode "far from cities" (civ BFS pulls danger
// down), so the zone gate IS the distance law. The spell's tier picks how deep
// into the wild band the spire must stand.
//
// Callers pass the spell list as plain data (ordinal + tier) because the spell
// registry lives in content/ (L4) and macro/ (L1) must not reach up. The app
// layer closes the loop (src/app/main.cpp, after generate_zones).
#pragma once
#include <cstdint>
#include <span>

namespace sm {

struct GameState;
struct TerrainData;
struct ZoneLayer;

// One row per learnable spell. `spellOrdinal` is the spell's registration
// index in spell_registry() — append-only, like the creature catalog
// (macro/fauna.cpp kCreatureCatalog): reordering registrations would silently
// re-key every spire in every save.
struct SpireSpellSpec {
    std::uint32_t spellOrdinal;
    std::uint8_t  tier;   // SpellDef.tier, 1..5 (armageddon 5, magic_bolt 1)
};

// Fill gs.spires (assumed cleared by populate_landmarks_from_politik).
// Deterministic from gs.worldSeed. Requires zones — call AFTER
// generate_zones. A spell whose zone band does not exist on this world gets
// its gate relaxed down to the table minimum; a world with no admissible land
// at all simply lacks that spire (logged).
void generate_spires(GameState& gs, const ZoneLayer& zones,
                     const TerrainData& terrain, std::uint8_t seaLevel8,
                     std::span<const SpireSpellSpec> spells);

} // namespace sm

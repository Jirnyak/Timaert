// Subworld spawn — populates the ECS with creatures from the per-cell
// fauna table. Mirrors `subworld/spawn.ts` populator: each visible cell
// rolls its own table once per cell entry, scaled by the world tile area.
#pragma once
#include <cstdint>
#include "ecs/world.h"
#include "sub/seamless_manager.h"
#include "sub/fauna.h"
#include "macro/biomes.h"
#include "macro/features.h"

namespace sm::sub {

// Despawn the previous subworld scene's creatures, then sample the
// resolved table for the centre cell and emplace one entity per pick.
// Faction / AI / colour / radius all come from the FaunaEntry — engine
// stays creature-agnostic.
//
// `landmarkPop` is settlement population (0 if none) and feeds the
// √(pop/100) level bonus from `subworld/spawn.ts::deriveContextScale`.
// `zoneLevel` is the macro zones difficulty (0..9); zones >2 add (z-2)
// levels and 1+(z-2)*0.18 hp/damage multipliers.
void respawn_subworld_npcs(ecs::World& w,
                           Biome biome,
                           FeatureType feature,
                           LandmarkKind landmark,
                           const SeamlessSubworldManager& mgr,
                           std::uint32_t seed,
                           int landmarkPop = 0,
                           int zoneLevel   = 0);

} // namespace sm::sub

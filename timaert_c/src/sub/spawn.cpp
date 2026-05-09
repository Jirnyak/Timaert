#include "sub/spawn.h"
#include "ecs/components.h"
#include "core/rng.h"
#include <cmath>

namespace sm::sub {

void respawn_subworld_npcs(ecs::World& w,
                           Biome biome,
                           FeatureType feature,
                           LandmarkKind landmark,
                           const SeamlessSubworldManager& mgr,
                           std::uint32_t seed,
                           int landmarkPop,
                           int zoneLevel) {
    auto& reg = w.reg;
    // Despawn ONLY entities tagged as living in the subworld. Macro NPCs
    // (peasants/caravans/etc) carry NPCKind without SubworldTag and must
    // survive the trip so they reappear on the macro map after `leave()`.
    {
        auto view = reg.view<ecs::SubworldTag>();
        for (auto e : view) reg.destroy(e);
    }

    const FaunaTable& table = get_fauna_table(biome, feature, landmark);
    std::uint32_t rngState = seed ^ 0xFAEAu;
    auto picks = roll_fauna(table, rngState);
    if (picks.empty()) return;

    // ── Macroworld context scale (TS spawn.ts::deriveContextScale) ──
    // Universal modifiers — extend by adding a line in this block, every
    // spawned NPC inherits automatically.
    int   levelBonus = 0;
    float hpMult     = 1.0f;
    float damageMult = 1.0f;
    if (landmark == LandmarkKind::City || landmark == LandmarkKind::Village) {
        if (landmarkPop > 0)
            levelBonus += int(std::floor(std::sqrt(float(landmarkPop) / 100.0f)));
    }
    if (zoneLevel > 2) {
        const int   zb = zoneLevel - 2;
        levelBonus += zb;
        const float boost = 1.0f + float(zb) * 0.18f;
        hpMult     = boost;
        damageMult = boost;
    }

    Rng pos(seed ^ 0xB17EBu);
    for (const auto& p : picks) {
        const FaunaEntry& f = *p.entry;
        // Up to 20 retries to land on a non-water tile.
        float fx = 0.0f, fy = 0.0f;
        bool placed = false;
        for (int attempt = 0; attempt < 20; ++attempt) {
            fx = pos.next_f01() * float(kFullSize);
            fy = pos.next_f01() * float(kFullSize);
            int ix = int(fx), iy = int(fy);
            if (ix < 0 || ix >= kFullSize || iy < 0 || iy >= kFullSize) continue;
            if (!mgr.tiles().empty() &&
                mgr.tiles()[std::size_t(iy) * kFullSize + ix] == TILE_WATER) continue;
            placed = true;
            break;
        }
        if (!placed) continue;

        // Synthetic NPCKind id: 256 + entry's address-stable creature index
        // would be ideal, but pointer identity isn't a stable id across
        // builds, so we hash the label. Faction goes through verbatim.
        std::uint32_t typeHash = 0;
        for (const char* c = f.label; *c; ++c)
            typeHash = typeHash * 131u + std::uint32_t(*c);
        const std::uint16_t typeId = std::uint16_t(0x100 | (typeHash & 0xFF));

        auto e = reg.create();
        reg.emplace<ecs::Position>(e, fx, fy);
        reg.emplace<ecs::VisualPos>(e, fx, fy, 32.0f);
        reg.emplace<ecs::NPCKind>(e, typeId, std::uint16_t(p.faction));
        const float hp = f.combat.hp * hpMult;
        reg.emplace<ecs::Health>(e, hp, hp);
        reg.emplace<ecs::Combat>(e,
            f.combat.damage * damageMult, f.combat.speed, f.combat.attackRange,
            f.combat.cooldown, 0.0f,
            f.combat.attackKind == CombatTemplate::Missile ? ecs::Combat::Missile
                                                           : ecs::Combat::Melee);
        reg.emplace<ecs::NpcLevel>(e, std::int16_t(f.baseLevel + levelBonus));
        reg.emplace<ecs::Active>(e);
        reg.emplace<ecs::SubworldTag>(e);
        // AI mode from FaunaEntry.ai → component dispatched in tick_npc_ai.
        // Wander pace ≈ 40 % of combat speed (TS uses ~0.4× too).
        ecs::SubworldAi::Kind aiKind =
            f.ai == FaunaAi::Combat ? ecs::SubworldAi::Combat
          : f.ai == FaunaAi::Flee   ? ecs::SubworldAi::Flee
                                    : ecs::SubworldAi::Wander;
        reg.emplace<ecs::SubworldAi>(e, aiKind, /*aiTimer*/0.0f,
            /*vx*/0.0f, /*vy*/0.0f,
            /*wanderSpeed*/f.combat.speed * 0.40f, f.radius);
        const std::uint8_t cr = std::uint8_t((f.color >> 16) & 0xFFu);
        const std::uint8_t cg = std::uint8_t((f.color >>  8) & 0xFFu);
        const std::uint8_t cb = std::uint8_t( f.color        & 0xFFu);
        reg.emplace<ecs::Sprite>(e, typeId, cr, cg, cb, std::uint8_t(255), f.radius);
    }
}

} // namespace sm::sub

#include "sub/spawn.h"
#include "ecs/components.h"
#include "core/rng.h"
#include "macro/npc.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace sm::sub {

namespace {

constexpr int kMaxSubworldSpawnReaps = 2048;

void clear_existing_subworld_entities(ecs::World& w) {
    auto& reg = w.reg;
    std::array<entt::entity, kMaxSubworldSpawnReaps> doomed{};
    for (;;) {
        int doomedCount = 0;
        auto view = reg.view<ecs::SubworldTag>();
        for (auto e : view) {
            if (doomedCount >= kMaxSubworldSpawnReaps) break;
            doomed[std::size_t(doomedCount++)] = e;
        }
        if (doomedCount == 0) break;
        for (int i = 0; i < doomedCount; ++i) {
            const entt::entity e = doomed[std::size_t(i)];
            if (reg.valid(e)) reg.destroy(e);
        }
    }
}

} // namespace

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
    clear_existing_subworld_entities(w);

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

    Rng pos(rngState);
    const auto& tiles = mgr.tiles();
    const bool tilesUsable =
        tiles.size() >= std::size_t(kFullSize) * std::size_t(kFullSize);
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
            if (tilesUsable && tiles[std::size_t(iy) * kFullSize + ix] == TILE_WATER) {
                continue;
            }
            placed = true;
            break;
        }
        if (!placed) continue;

        const int npcLevel = normalize_soldier_level(
            int(f.baseLevel) + int(std::floor(pos.next_f01() * 2.0f)) + levelBonus);
        const float levelScale =
            1.0f + float(std::max(0, npcLevel - 1)) * 0.15f;

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
        const float hp = std::floor(f.combat.hp * hpMult * levelScale);
        const float damage = std::floor(f.combat.damage * damageMult * levelScale);
        reg.emplace<ecs::Health>(e, hp, hp);
        reg.emplace<ecs::Combat>(e,
            damage, f.combat.speed, f.combat.attackRange,
            f.combat.cooldown, 0.0f,
            f.combat.attackKind == CombatTemplate::Missile ? ecs::Combat::Missile
                                                           : ecs::Combat::Melee);
        reg.emplace<ecs::NpcLevel>(e, std::int16_t(npcLevel));
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

void spawn_player_squad(ecs::World& w,
                        const SoldierSquad& squad,
                        const SeamlessSubworldManager& mgr,
                        float playerX,
                        float playerY,
                        std::uint32_t seed) {
    spawn_player_squad(w, squad, mgr.tiles(), playerX, playerY, seed);
}

void spawn_player_squad(ecs::World& w,
                        const SoldierSquad& squad,
                        const std::vector<std::uint8_t>& tiles,
                        float playerX,
                        float playerY,
                        std::uint32_t seed) {
    if (squad.members.empty()) return;

    auto& reg = w.reg;
    Rng rng(seed ^ 0x51AD5A11u);
    constexpr float kPi = 3.1415926535f;
    constexpr float kTau = kPi * 2.0f;
    const int count = std::max(1, int(squad.members.size()));
    const bool tilesUsable =
        tiles.size() >= std::size_t(kFullSize) * std::size_t(kFullSize);

    for (int i = 0; i < count; ++i) {
        const SoldierRecord& soldier = squad.members[std::size_t(i)];
        if (!valid_npc_kind(soldier.kind)) continue;

        const NPCType type = static_cast<NPCType>(soldier.kind);
        const NpcTypeDef& def = npc_def(type);
        const int level = normalize_soldier_level(soldier.level);
        const float levelMul = 1.0f + float(std::max(0, level - 1)) * 0.08f;

        float fx = playerX;
        float fy = playerY;
        bool placed = false;
        for (int attempt = 0; attempt < 24; ++attempt) {
            const float baseAngle = (float(i) / float(count)) * kTau;
            const float jitter = (rng.next_f01() - 0.5f) * 0.7f;
            const float radius = 5.0f + float((i % 5) * 3) + rng.next_f01() * 2.0f;
            fx = std::clamp(playerX + std::cos(baseAngle + jitter) * radius,
                            1.0f, float(kFullSize - 2));
            fy = std::clamp(playerY + std::sin(baseAngle + jitter) * radius,
                            1.0f, float(kFullSize - 2));
            const int ix = int(fx);
            const int iy = int(fy);
            if (tilesUsable &&
                tiles[std::size_t(iy) * kFullSize + ix] == TILE_WATER) {
                continue;
            }
            placed = true;
            break;
        }
        if (!placed) continue;

        auto e = reg.create();
        reg.emplace<ecs::Position>(e, fx, fy);
        reg.emplace<ecs::VisualPos>(e, fx, fy, 48.0f);
        reg.emplace<ecs::NPCKind>(
            e, ecs::NPCKind{std::uint16_t(type), std::uint16_t(0)});
        const float hp = float(def.combat.hp) * levelMul;
        reg.emplace<ecs::Health>(e, hp, hp);
        reg.emplace<ecs::Combat>(e,
            float(def.combat.damage) * levelMul,
            def.combat.speed,
            def.combat.attackRange,
            def.combat.cooldown,
            0.0f,
            def.combat.attackKind == CombatTemplate::Missile ? ecs::Combat::Missile
                                                             : ecs::Combat::Melee);
        reg.emplace<ecs::NpcLevel>(e, std::int16_t(level));
        reg.emplace<ecs::Active>(e);
        reg.emplace<ecs::SubworldTag>(e);
        reg.emplace<ecs::PlayerSoldierTag>(e);
        reg.emplace<ecs::SubworldAi>(e, ecs::SubworldAi::Combat,
            /*aiTimer*/0.0f, /*vx*/0.0f, /*vy*/0.0f,
            /*wanderSpeed*/def.combat.speed * 0.40f,
            /*radius*/0.8f);
        reg.emplace<ecs::SoldierLink>(e, soldier.entityId, soldier.kind,
                                      std::int16_t(level));
        reg.emplace<ecs::Sprite>(e, std::uint16_t(type),
            std::uint8_t(120), std::uint8_t(190), std::uint8_t(255),
            std::uint8_t(255), 1.0f);
    }
}

} // namespace sm::sub

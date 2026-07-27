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
constexpr int kMaxCityCitizenProjection = 128;
constexpr int kMaxVillageCitizenProjection = 48;

// Despawn the current subworld cell's world creatures (fauna + citizens), but
// PRESERVE the player's projected squad (PlayerSoldierTag): the squad follows
// the player across seamless re-centres and is not part of a cell's population.
void clear_existing_subworld_entities(ecs::World& w) {
    auto& reg = w.reg;
    std::array<entt::entity, kMaxSubworldSpawnReaps> doomed{};
    for (;;) {
        int doomedCount = 0;
        auto view = reg.view<ecs::SubworldTag>();
        for (auto e : view) {
            if (reg.any_of<ecs::PlayerSoldierTag>(e)) continue;
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

ecs::NpcCharacter make_spawn_character(std::uint32_t seed,
                                        NPCType type,
                                        std::uint32_t salt) {
    Rng rng(seed ^ (std::uint32_t(type) * std::uint32_t{2654435761}) ^ salt);
    ecs::NpcCharacter ch{};
    ch.visualSeed = rng.next_u32();
    ch.bodyShape = std::uint8_t(rng.next_u32() & std::uint32_t{0x3});
    ch.nameIdx = std::uint8_t(rng.next_u32() & std::uint32_t{0xF});
    ch.tintR = std::uint8_t(150 + int(rng.next_u32() % std::uint32_t{96}));
    ch.tintG = std::uint8_t(150 + int(rng.next_u32() % std::uint32_t{96}));
    ch.tintB = std::uint8_t(150 + int(rng.next_u32() % std::uint32_t{96}));
    return ch;
}

void maybe_emplace_missile_attack(entt::registry& reg,
                                  entt::entity e,
                                  const CombatTemplate& combat) {
    if (combat.attackKind != CombatTemplate::Missile) return;
    reg.emplace<ecs::MissileAttack>(
        e,
        combat.missileSpeed > 0.0f ? combat.missileSpeed : 200.0f,
        combat.missileBlast,
        combat.missileColorRGBA);
}

bool city_spawn_tile(std::uint8_t tile, int pass) {
    if (pass == 0) {
        return tile == TILE_ROAD || tile == TILE_SQUARE;
    }
    return tile != TILE_WATER && tile != TILE_HOUSE && tile != TILE_WALL;
}

bool find_city_spawn_spot(const std::vector<std::uint8_t>& tiles,
                          Rng& rng,
                          float& fx,
                          float& fy) {
    if (tiles.size() < std::size_t(kFullSize) * std::size_t(kFullSize)) {
        return false;
    }
    constexpr int kCenterOrigin = kCellSize;
    for (int pass = 0; pass < 2; ++pass) {
        for (int attempt = 0; attempt < 64; ++attempt) {
            const int x = kCenterOrigin
                + int(rng.next_u32() % std::uint32_t(kCellSize));
            const int y = kCenterOrigin
                + int(rng.next_u32() % std::uint32_t(kCellSize));
            const std::uint8_t t = tiles[std::size_t(y) * kFullSize + x];
            if (!city_spawn_tile(t, pass)) continue;
            fx = float(x) + 0.5f;
            fy = float(y) + 0.5f;
            return true;
        }
    }
    return false;
}

NPCType pick_civilian_type(Rng& rng) {
    const int roll = int(rng.next_u32() % 100u);
    if (roll < 55) return NPCType::Peasant;
    if (roll < 76) return NPCType::Merchant;
    if (roll < 97) return NPCType::Woodcutter;
    return NPCType::Witch;
}

void spawn_settlement_population(ecs::World& w,
                                 LandmarkKind landmark,
                                 const SeamlessSubworldManager& mgr,
                                 std::uint32_t seed,
                                 int landmarkPop,
                                 int levelBonus) {
    if (landmark != LandmarkKind::City && landmark != LandmarkKind::Village) {
        return;
    }
    const int pop = std::max(0, landmarkPop);
    if (pop == 0) return;

    const bool city = landmark == LandmarkKind::City;
    const int cap = city ? kMaxCityCitizenProjection
                         : kMaxVillageCitizenProjection;
    const int divisor = city ? 80 : 6;
    const int minimum = city ? 24 : 6;
    const int target = std::min(cap, std::max(minimum, pop / divisor));
    const int guards = std::max(city ? 2 : 1, target / 10);
    Rng rng(seed ^ (city ? 0xC1712E55u : 0xA117A6E5u));
    const auto& tiles = mgr.tiles();
    auto& reg = w.reg;

    for (int i = 0; i < target; ++i) {
        float fx = 0.0f;
        float fy = 0.0f;
        if (!find_city_spawn_spot(tiles, rng, fx, fy)) {
            break;
        }
        NPCType type = NPCType::Peasant;
        if (i < guards) {
            type = NPCType::Guard;
        } else if (i == guards) {
            type = NPCType::Merchant;
        } else if (i == guards + 1) {
            type = NPCType::Woodcutter;
        } else {
            type = pick_civilian_type(rng);
        }
        const NpcTypeDef& def = npc_def(type);
        const int npcLevel = normalize_soldier_level(
            def.baseLevel + int(rng.next_u32() % 3u) + levelBonus);
        const float levelScale =
            1.0f + float(std::max(0, npcLevel - 1)) * 0.15f;

        auto e = reg.create();
        reg.emplace<ecs::Position>(e, fx, fy);
        reg.emplace<ecs::VisualPos>(e, fx, fy, 32.0f);
        reg.emplace<ecs::NPCKind>(e, std::uint16_t(type), std::uint16_t(0));
        const float hp = std::floor(float(def.combat.hp) * levelScale);
        const float damage = std::floor(float(def.combat.damage) * levelScale);
        reg.emplace<ecs::Health>(e, hp, hp);
        reg.emplace<ecs::Combat>(e,
            damage, def.combat.speed, def.combat.attackRange,
            def.combat.cooldown, 0.0f,
            def.combat.attackKind == CombatTemplate::Missile ? ecs::Combat::Missile
                                                             : ecs::Combat::Melee);
        maybe_emplace_missile_attack(reg, e, def.combat);
        reg.emplace<ecs::NpcLevel>(e, std::int16_t(npcLevel));
        reg.emplace<ecs::Active>(e);
        reg.emplace<ecs::SubworldTag>(e);
        reg.emplace<ecs::SubworldAi>(e,
            type == NPCType::Guard ? ecs::SubworldAi::Combat
                                   : ecs::SubworldAi::Flee,
            0.0f, 0.0f, 0.0f, def.combat.speed * 0.35f, 0.55f);
        reg.emplace<ecs::NpcCharacter>(
            e, make_spawn_character(seed, type, std::uint32_t(i) * 7919u));
        reg.emplace<ecs::Sprite>(e, std::uint16_t(type),
            std::uint8_t(type == NPCType::Guard ? 170 : 190),
            std::uint8_t(type == NPCType::Merchant ? 190 : 150),
            std::uint8_t(type == NPCType::Witch ? 210 : 120),
            std::uint8_t(255), 0.55f);
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

    spawn_settlement_population(w, landmark, mgr, seed, landmarkPop, levelBonus);
    if (picks.empty()) return;

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

        // Synthetic NPCKind id: (0x100 | stable monster-catalog index). The
        // high 0x100 bit marks a monster (vs a humanoid NPCType < Count); the
        // low byte is the creature's catalog index, recoverable on the death /
        // loot path via creature_def_from_kind(). Faction goes through verbatim.
        const int catIdx = creature_index(&f);
        const std::uint16_t typeId =
            std::uint16_t(0x100 | (catIdx < 0 ? 0 : catIdx));

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
        maybe_emplace_missile_attack(reg, e, f.combat);
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
        reg.emplace<ecs::Sprite>(e, typeId, cr, cg, cb, std::uint8_t(255), f.radius,
                                 std::uint8_t(f.archetype));
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
        maybe_emplace_missile_attack(reg, e, def.combat);
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

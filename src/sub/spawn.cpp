#include "sub/spawn.h"
#include "macro/entry_context.h"
#include "macro/faction.h"
#include "ecs/components.h"
#include "ecs/npc_character.h"
#include "core/rng.h"
#include "macro/npc.h"
#include "macro/character_sheet.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace sm::sub {

namespace {

constexpr int kMaxSubworldSpawnReaps = 2048;
// Safety valve for the macro→subworld projection (Inc 5d). Only macro NPCs whose
// integer cell falls in the 3×3 window are projected, so in normal play this is
// a handful; the cap merely bounds a pathological single-cell cluster and is not
// expected to bind. The projection's return count reflects what was projected.
constexpr int kMaxProjectedMacroNpcs = 128;

// Signed toroidal offset of macro cell `a` from window centre `c` on a torus of
// circumference `n`, folded to (-n/2, n/2]. A result in {-1,0,1} means `a` is in
// the 3×3 window at that cell offset; anything else is outside it. Matches the
// wrap semantics the macro AI uses (core/torus.h), so "in the window" here means
// exactly the same cells the seamless manager loads.
int toroidal_cell_offset(int a, int c, int n) {
    if (n <= 0) return a - c;
    int d = ((a - c) % n + n) % n;   // [0, n)
    if (d * 2 > n) d -= n;           // fold to (-n/2, n/2]
    return d;
}

ecs::NpcCharacter make_spawn_character(std::uint32_t seed,
                                        NPCType type,
                                        std::uint32_t salt) {
    Rng rng(seed ^ (std::uint32_t(type) * std::uint32_t{2654435761}) ^ salt);
    return ecs::roll_npc_character(rng, 150);
}

bool find_city_spawn_spot(const std::vector<std::uint8_t>& tiles,
                          Rng& rng,
                          int originX,
                          int originY,
                          float& fx,
                          float& fy) {
    if (tiles.size() < std::size_t(kFullSize) * std::size_t(kFullSize)) {
        return false;
    }
    for (int attempt = 0; attempt < 64; ++attempt) {
        const int x = originX
            + int(rng.next_u32() % std::uint32_t(kCellSize));
        const int y = originY
            + int(rng.next_u32() % std::uint32_t(kCellSize));
        const std::uint8_t t = tiles[std::size_t(y) * kFullSize + x];
        // Water drowns; house/wall footprints are SOLID now (sub/collide.h) —
        // a body born inside masonry would have to walk out through the
        // escape rule, so don't put it there in the first place.
        if (t == TILE_WATER || t == TILE_HOUSE || t == TILE_WALL) continue;
        fx = float(x) + 0.5f;
        fy = float(y) + 0.5f;
        return true;
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
                                 std::uint16_t settlementFaction,
                                 int landmarkPop,
                                 int levelBonus,
                                 int originX,
                                 int originY) {
    if (landmark != LandmarkKind::City && landmark != LandmarkKind::Village) {
        return;
    }
    const int pop = std::max(0, landmarkPop);
    if (pop == 0) return;

    const bool city = landmark == LandmarkKind::City;
    const int target = pop;
    const int guards = std::max(city ? 2 : 1, target / 10);
    Rng rng(seed ^ (city ? 0xC1712E55u : 0xA117A6E5u));
    const auto& tiles = mgr.tiles();
    auto& reg = w.reg;

    for (int i = 0; i < target; ++i) {
        float fx = 0.0f;
        float fy = 0.0f;
        if (!find_city_spawn_spot(tiles, rng, originX, originY, fx, fy)) {
            continue;
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
        // Universal character sheet — same struct the player carries; combat is
        // DERIVED from it (project_combat), so level scaling lives in the sheet's
        // spent points, not a separate multiplier. Seeded from the town seed so
        // a settlement regenerates identically.
        const CharacterSheet sheet = make_character_sheet(
            type, npcLevel, seed ^ (std::uint32_t(i) * 7919u));
        const CombatTemplate pc = project_combat(sheet, def.combat);

        auto e = reg.create();
        reg.emplace<ecs::Position>(e, fx, fy, 0.0f);
        reg.emplace<ecs::VisualPos>(e, fx, fy, 32.0f);
        // Every citizen — guard, merchant, peasant alike — wears the faction of
        // the kingdom that owns this town (resolved by the caller). A city is
        // its realm's city on both layers, or the subworld contradicts the map.
        reg.emplace<ecs::NPCKind>(e, std::uint16_t(type), settlementFaction);
        const float hp = std::floor(pc.hp);
        const float damage = std::floor(pc.damage);
        reg.emplace<ecs::Health>(e, hp, hp);
        reg.emplace<ecs::Combat>(e,
            damage, pc.speed, pc.attackRange,
            pc.cooldown, 0.0f,
            pc.attackKind == CombatTemplate::Missile ? ecs::Combat::Missile
                                                     : ecs::Combat::Melee);
        maybe_emplace_missile_attack(reg, e, pc);
        reg.emplace<ecs::NpcLevel>(e, std::int16_t(npcLevel));
        reg.emplace<ecs::SubworldTag>(e);
        reg.emplace<ecs::SubworldAi>(e, subworld_ai_for(def.ai),
            0.0f, 0.0f, 0.0f, pc.speed * 0.35f, 0.55f);
        reg.emplace<CharacterSheet>(e, sheet);
        reg.emplace<ecs::NpcCharacter>(
            e, make_spawn_character(seed, type, std::uint32_t(i) * 7919u));
        reg.emplace<ecs::Sprite>(e, std::uint16_t(type),
            std::uint8_t(type == NPCType::Guard ? 170 : 190),
            std::uint8_t(type == NPCType::Merchant ? 190 : 150),
            std::uint8_t(type == NPCType::Witch ? 210 : 120),
            std::uint8_t(255), 0.55f);
        maybe_emplace_carried_light(reg, e, def);
    }
}

// Emplace one fauna creature at (fx,fy). Shared by the per-cell and the
// whole-window spawn paths so the entity layout lives in exactly one place. The
// caller decides npcLevel and the context multipliers (they consume the caller's
// RNG stream in order); the per-level HP/damage scale folds in here.
void emplace_fauna_entity(entt::registry& reg, const FaunaEntry& f,
                          std::uint16_t faction, float fx, float fy,
                          int npcLevel, float hpMult, float damageMult) {
    const float levelScale = 1.0f + float(std::max(0, npcLevel - 1)) * 0.15f;
    // Synthetic NPCKind id: (0x100 | stable monster-catalog index). The high
    // 0x100 bit marks a monster (vs a humanoid NPCType < Count); the low byte is
    // the creature's catalog index, recoverable on the death / loot path via
    // creature_def_from_kind(). Faction goes through verbatim.
    const int catIdx = creature_index(&f);
    const std::uint16_t typeId =
        std::uint16_t(0x100 | (catIdx < 0 ? 0 : catIdx));

    auto e = reg.create();
    reg.emplace<ecs::Position>(e, fx, fy, 0.0f);
    reg.emplace<ecs::VisualPos>(e, fx, fy, 32.0f);
    reg.emplace<ecs::NPCKind>(e, typeId, faction);
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
    reg.emplace<ecs::SubworldTag>(e);
    // AI mode from FaunaEntry.ai → component dispatched in tick_npc_ai.
    // Wander pace ≈ 40 % of combat speed (TS uses ~0.4× too).
    const ecs::SubworldAi::Kind aiKind =
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

} // namespace

// ── Universal per-humanoid component attachers (declared in spawn.h) ─────
// ONE home for the rules every spawn site shares — settlement populator,
// squads, macro projection (this TU) and the console/encounter spawner
// (engine.cpp). These used to exist as byte-identical file-local twins in
// both TUs, each with a comment admitting the duplication.

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

// Attach the NPC type's carried light (torch / lantern / arcane glow), if it has
// one, as an ecs::LightEmitter — the SAME universal component the player lantern
// and spell bolts use, so the renderer's one gather_point_lights pass lights it
// with zero per-emitter code. Data-driven and strictly opt-in: a type with
// lightRadius <= 0 (every row that doesn't set the fields) gets nothing, so
// lighting a new type is one data row in kNpcTypeDefs and no code change here.
// Called from every humanoid spawn site after its Sprite emplace, so a guard is
// lit whether it is a settlement citizen, a projected macro body or a squad
// soldier — one rule, one place. Budget-safe: guards are bounded per settlement
// and the nearest-N cull (gather_point_lights) protects the SSBO regardless.
void maybe_emplace_carried_light(entt::registry& reg,
                                 entt::entity e,
                                 const NpcTypeDef& def) {
    if (def.lightRadius <= 0.0f) return;
    reg.emplace<ecs::LightEmitter>(
        e, ecs::LightEmitter{0.0f, def.lightHeight, 0.0f,
                             def.lightR, def.lightG, def.lightB,
                             def.lightRadius, def.lightIntensity});
}

// ── Per-cell population + seamless persistence helpers ───────────────────

void clear_subworld_world_entities(ecs::World& w) {
    auto& reg = w.reg;
    std::array<entt::entity, kMaxSubworldSpawnReaps> doomed{};
    for (;;) {
        int doomedCount = 0;
        auto view = reg.view<ecs::SubworldTag>();
        for (auto e : view) {
            if (reg.any_of<ecs::PlayerSoldierTag, ecs::PlayerTag>(e)) continue;
            // Projected macro NPCs (Inc 5d) mirror persistent overworld bodies,
            // not a cell's procedural fill — a whole-window rebuild (respawn_fauna)
            // must leave them be, exactly like the player-side projections above.
            // On enter this is a no-op (projection runs after the clear).
            if (reg.all_of<ecs::MacroOrigin>(e)) continue;
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

void spawn_cell_npcs(ecs::World& w,
                     Biome biome,
                     int treeCount,
                     LandmarkKind landmark,
                     const SeamlessSubworldManager& mgr,
                     int ox,
                     int oy,
                     std::uint32_t cellSeed,
                     std::uint16_t settlementFaction,
                     int landmarkPop,
                     int zoneLevel) {
    auto& reg = w.reg;
    const int originX = (ox + 1) * kCellSize;
    const int originY = (oy + 1) * kCellSize;

    // Context scale — identical modifiers to the whole-window path, but keyed to
    // THIS cell's macro context so each of the 3×3 cells is populated on its own
    // terms (a city cell fills with citizens even when it is not the centre —
    // which is what stops a city from vanishing when you step one cell out).
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

    spawn_settlement_population(w, landmark, mgr, cellSeed, settlementFaction,
                                landmarkPop, levelBonus, originX, originY);

    const FaunaTable& table = get_fauna_table(biome, treeCount, landmark);
    std::uint32_t rngState = cellSeed ^ 0xFAEAu;
    auto picks = roll_fauna(table, rngState);
    if (picks.empty()) return;

    Rng pos(rngState);
    const auto& tiles = mgr.tiles();
    const bool tilesUsable =
        tiles.size() >= std::size_t(kFullSize) * std::size_t(kFullSize);
    for (const auto& p : picks) {
        const FaunaEntry& f = *p.entry;
        // Scatter within this cell's sub-region only. Up to 20 retries to dodge
        // water; positions are composite-window tiles like everything else.
        float fx = 0.0f, fy = 0.0f;
        bool placed = false;
        for (int attempt = 0; attempt < 20; ++attempt) {
            fx = float(originX) + pos.next_f01() * float(kCellSize);
            fy = float(originY) + pos.next_f01() * float(kCellSize);
            const int ix = int(fx), iy = int(fy);
            if (ix < 0 || ix >= kFullSize || iy < 0 || iy >= kFullSize) continue;
            if (tilesUsable &&
                tiles[std::size_t(iy) * kFullSize + ix] == TILE_WATER) {
                continue;
            }
            placed = true;
            break;
        }
        if (!placed) continue;

        const int npcLevel = normalize_soldier_level(
            int(f.baseLevel) + int(std::floor(pos.next_f01() * 2.0f)) + levelBonus);
        emplace_fauna_entity(reg, f,
                             std::uint16_t(faction_index(p.factionId)),
                             fx, fy,
                             npcLevel, hpMult, damageMult);
    }
}

void rebase_subworld_entities(ecs::World& w, float dxTiles, float dyTiles) {
    auto& reg = w.reg;
    // Shift the authoritative sim position AND the smoothed render position so a
    // recentre neither drifts entities nor produces a one-frame interpolation
    // streak. Both views are SubworldTag-gated, so the player squad shifts too.
    auto posView = reg.view<ecs::SubworldTag, ecs::Position>();
    for (auto e : posView) {
        auto& p = posView.get<ecs::Position>(e);
        p.x += dxTiles;
        p.y += dyTiles;
    }
    auto visView = reg.view<ecs::SubworldTag, ecs::VisualPos>();
    for (auto e : visView) {
        auto& v = visView.get<ecs::VisualPos>(e);
        v.vx += dxTiles;
        v.vy += dyTiles;
    }
}

void despawn_subworld_entities_outside_window(ecs::World& w) {
    auto& reg = w.reg;
    std::array<entt::entity, kMaxSubworldSpawnReaps> doomed{};
    for (;;) {
        int doomedCount = 0;
        auto view = reg.view<ecs::SubworldTag, ecs::Position>();
        for (auto e : view) {
            if (reg.any_of<ecs::PlayerSoldierTag, ecs::PlayerTag>(e)) continue;
            const auto& p = view.get<ecs::Position>(e);
            const bool inside = p.x >= 0.0f && p.x < float(kFullSize)
                             && p.y >= 0.0f && p.y < float(kFullSize);
            if (inside) continue;
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

void spawn_player_squad(ecs::World& w,
                        const SoldierSquad& squad,
                        const SeamlessSubworldManager& mgr,
                        float playerX,
                        float playerY,
                        std::uint32_t seed,
                        std::uint16_t faction) {
    spawn_player_squad(w, squad, mgr.tiles(), playerX, playerY, seed, faction);
}

void spawn_player_squad(ecs::World& w,
                        const SoldierSquad& squad,
                        const std::vector<std::uint8_t>& tiles,
                        float playerX,
                        float playerY,
                        std::uint32_t seed,
                        std::uint16_t faction) {
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
        // Humanoid soldier → same sheet the player carries; combat is DERIVED
        // from it (project_combat). Seeded per squad slot (kind+level+slot) so a
        // squad reprojects identically. Level scaling lives in the sheet's spent
        // points, not a separate multiplier.
        const CharacterSheet sheet = make_character_sheet(
            type, level,
            (std::uint32_t(i) * 2654435761u)
                ^ (std::uint32_t(soldier.kind) << 8)
                ^ std::uint32_t(level));
        const CombatTemplate pc = project_combat(sheet, def.combat);

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
        reg.emplace<ecs::Position>(e, fx, fy, 0.0f);
        reg.emplace<ecs::VisualPos>(e, fx, fy, 48.0f);
        // The squad's faction, not the member's own kind: a soldier fights for
        // whoever raised the squad. This used to be a hardcoded "empire", which
        // made the player's own household troops imperial subjects in the data
        // even while the battle pass forced them onto his side by a tag.
        reg.emplace<ecs::NPCKind>(
            e, ecs::NPCKind{std::uint16_t(type), faction});
        const float hp = pc.hp;
        reg.emplace<ecs::Health>(e, hp, hp);
        reg.emplace<ecs::Combat>(e,
            pc.damage,
            pc.speed,
            pc.attackRange,
            pc.cooldown,
            0.0f,
            pc.attackKind == CombatTemplate::Missile ? ecs::Combat::Missile
                                                     : ecs::Combat::Melee);
        maybe_emplace_missile_attack(reg, e, pc);
        reg.emplace<ecs::NpcLevel>(e, std::int16_t(level));
        reg.emplace<ecs::SubworldTag>(e);
        reg.emplace<ecs::PlayerSoldierTag>(e);
        reg.emplace<ecs::SubworldAi>(e, ecs::SubworldAi::Combat,
            /*aiTimer*/0.0f, /*vx*/0.0f, /*vy*/0.0f,
            /*wanderSpeed*/pc.speed * 0.40f,
            /*radius*/0.8f);
        reg.emplace<CharacterSheet>(e, sheet);
        reg.emplace<ecs::SoldierLink>(e, soldier.entityId, soldier.kind,
                                      std::int16_t(level));
        reg.emplace<ecs::Sprite>(e, std::uint16_t(type),
            std::uint8_t(120), std::uint8_t(190), std::uint8_t(255),
            std::uint8_t(255), 1.0f);
        maybe_emplace_carried_light(reg, e, def);
    }
}

// ── Macro→subworld projection (Inc 5d) ───────────────────────────────────

int project_macro_npcs_into_subworld(ecs::World& w,
                                     const SeamlessSubworldManager& mgr,
                                     int centerCx, int centerCy,
                                     int mapW, int mapH,
                                     std::uint32_t seed) {
    auto& reg = w.reg;
    const auto& tiles = mgr.tiles();
    const bool tilesUsable =
        tiles.size() >= std::size_t(kFullSize) * std::size_t(kFullSize);

    // Snapshot the source set FIRST. Projecting a body emplaces into the very
    // component pools this view iterates (Position / NPCKind / Health / …),
    // which can reallocate and invalidate a live view iterator mid-loop. So we
    // collect the persistent macro NPCs, then create their projections.
    // MacroNpcRuntime is the macro discriminator (subworld bodies never have it);
    // excluding SubworldTag/Dead keeps the source set to live overworld NPCs.
    // PlayerTag skips a macro NPC the player is currently possessing (Inc 5e-2) —
    // you don't meet a foreign projection of your own former body on enter.
    std::vector<entt::entity> sources;
    {
        auto view = reg.view<ecs::MacroNpcRuntime, ecs::Position, ecs::NPCKind,
                             ecs::Health, ecs::NpcLevel, ecs::NpcCharacter>(
            entt::exclude<ecs::SubworldTag, ecs::Dead, ecs::PlayerTag>);
        for (auto macro : view) sources.push_back(macro);
    }

    int projected = 0;
    for (const entt::entity macro : sources) {
        if (projected >= kMaxProjectedMacroNpcs) break;

        const auto& mpos = reg.get<ecs::Position>(macro);
        // Which of the 3×3 window cells does this macro NPC occupy (if any)?
        const int ox = toroidal_cell_offset(int(mpos.x), centerCx, mapW);
        const int oy = toroidal_cell_offset(int(mpos.y), centerCy, mapH);
        if (ox < -1 || ox > 1 || oy < -1 || oy > 1) continue;

        const auto& kind = reg.get<ecs::NPCKind>(macro);
        // Humanoid NPCType rows only (0..Count). Guard on the wide type BEFORE
        // any narrowing so a stray monster id (0x100|idx) can never alias a
        // humanoid row. Macro NPCs are always humanoid, so this never trips in
        // practice — it keeps the projection honest if that ever changes.
        if (kind.type >= std::uint16_t(NPCType::Count)) continue;
        const NPCType type = static_cast<NPCType>(kind.type);
        const NpcTypeDef& def = npc_def(type);
        const int level = normalize_soldier_level(reg.get<ecs::NpcLevel>(macro).value);

        // Deterministic per-(cell, type, index) stream: the same overworld state
        // reprojects identically, yet two same-type NPCs in one cell still differ
        // (their integer coords or the running index diverge the salt).
        const std::uint32_t salt =
            (std::uint32_t(int(mpos.x)) * 73856093u) ^
            (std::uint32_t(int(mpos.y)) * 19349663u) ^
            (std::uint32_t(kind.type) << 11) ^
            (std::uint32_t(projected) * 2654435761u);
        Rng rng(seed ^ salt);

        // Entry-side scatter within this window cell's sub-region
        // (macro/entry_context.h): the band starts at the edge this NPC walked
        // in from and deepens with its time in the macro cell, so a party that
        // just chased somebody across the border materialises AT that border,
        // behind them — while a local that has been here forever gets the full
        // uniform cell (the band formula degrades to exactly the old scatter).
        // Water is dodged per attempt like the fauna path (spawn_cell_npcs);
        // falls back to the cell centre if 20 tries all hit water (never lose
        // the NPC).
        const auto& mrt = reg.get<ecs::MacroNpcRuntime>(macro);
        int sdx = 0, sdy = 0;
        (void)unpack_entry_dir(mrt.entryDir, sdx, sdy);
        const int originX = (ox + 1) * kCellSize;
        const int originY = (oy + 1) * kCellSize;
        float fx = float(originX) + float(kCellSize) * 0.5f;
        float fy = float(originY) + float(kCellSize) * 0.5f;
        for (int attempt = 0; attempt < 20; ++attempt) {
            const float tx = float(originX) + entry_axis_pos(
                sdx, mrt.entryTicks, float(kCellSize), rng.next_f01());
            const float ty = float(originY) + entry_axis_pos(
                sdy, mrt.entryTicks, float(kCellSize), rng.next_f01());
            const int ix = int(tx), iy = int(ty);
            if (ix < 0 || ix >= kFullSize || iy < 0 || iy >= kFullSize) continue;
            if (tilesUsable &&
                tiles[std::size_t(iy) * kFullSize + ix] == TILE_WATER) {
                continue;
            }
            fx = tx; fy = ty;
            break;
        }

        // Universal character sheet — the SAME struct the player and every
        // citizen carry; Combat is DERIVED from it (project_combat), so level
        // scaling lives in the sheet's spent points, not a separate multiplier.
        const CharacterSheet sheet =
            make_character_sheet(type, level, seed ^ salt ^ 0x5D0F11u);
        const CombatTemplate pc = project_combat(sheet, def.combat);
        // HP is body-native PERSISTENT state, so it is COPIED from the macro
        // entity (a wounded overworld lord arrives wounded); the derived maxHp
        // comes from the fresh sheet. Capability (Combat) is synthesised from the
        // sheet, exactly like the settlement-citizen path — HP = state, Combat =
        // capability.
        const float maxHp = std::max(1.0f, std::floor(pc.hp));
        const float hp =
            std::clamp(std::floor(reg.get<ecs::Health>(macro).hp), 1.0f, maxHp);

        auto e = reg.create();
        reg.emplace<ecs::Position>(e, fx, fy, 0.0f);
        reg.emplace<ecs::VisualPos>(e, fx, fy, 32.0f);
        // Copy the macro NPCKind verbatim — the faction index goes through so the
        // universal player_stance()/threat paths read the same reputation as the
        // overworld body.
        reg.emplace<ecs::NPCKind>(e, kind.type, kind.factionIdx);
        reg.emplace<ecs::Health>(e, hp, maxHp);
        reg.emplace<ecs::Combat>(e,
            std::floor(pc.damage), pc.speed, pc.attackRange,
            pc.cooldown, 0.0f,
            pc.attackKind == CombatTemplate::Missile ? ecs::Combat::Missile
                                                     : ecs::Combat::Melee);
        maybe_emplace_missile_attack(reg, e, pc);
        reg.emplace<ecs::NpcLevel>(e, std::int16_t(level));
        reg.emplace<ecs::SubworldTag>(e);
        // Data-driven hostility via the shared subworld_ai_for() mapping —
        // keyed off the SAME NpcTypeDef.ai the macro AI uses, so a new hostile
        // type needs no edit here — one data row, no code change (the
        // project's core law).
        reg.emplace<ecs::SubworldAi>(e, subworld_ai_for(def.ai),
            0.0f, 0.0f, 0.0f, pc.speed * 0.35f, 0.55f);
        reg.emplace<CharacterSheet>(e, sheet);
        // Preserve the macro NPC's visual identity verbatim, so the same lord
        // wears the same face and name in both worlds.
        reg.emplace<ecs::NpcCharacter>(e, reg.get<ecs::NpcCharacter>(macro));
        reg.emplace<ecs::Sprite>(e, kind.type,
            std::uint8_t(type == NPCType::Guard ? 170 : 190),
            std::uint8_t(type == NPCType::Merchant ? 190 : 150),
            std::uint8_t(type == NPCType::Witch ? 210 : 120),
            std::uint8_t(255), 0.55f);
        maybe_emplace_carried_light(reg, e, def);
        // The 5d backlink: this projection mirrors `macro`. The reaper skips it,
        // and leave() (5e) reads it to land the player back on the macro cell.
        reg.emplace<ecs::MacroOrigin>(e, macro);
        ++projected;
    }
    return projected;
}

// ── Exit remap query (Inc 5e-1) ──────────────────────────────────────────

MacroExitCell macro_exit_cell_for_body(ecs::World& w, entt::entity body,
                                       int mapW, int mapH) {
    MacroExitCell out{false, 0, 0, entt::null};
    if (mapW <= 0 || mapH <= 0) return out;
    auto& reg = w.reg;
    if (body == entt::null || !reg.valid(body)) return out;
    // Only a possessed macro-projected body carries the backlink.
    if (!reg.all_of<ecs::MacroOrigin>(body)) return out;
    const entt::entity macro = reg.get<ecs::MacroOrigin>(body).macro;
    // The macro entity may have been reaped (e.g. it died in the meantime); a
    // stale handle just means "no remap" → fall back to the window centre.
    if (!reg.valid(macro) || !reg.all_of<ecs::Position>(macro)) return out;
    // Macro Position is an integer cell on the torus — the SAME space as
    // gs.player.x/y; wrap exactly as sync_macro_player_to_center does.
    const auto& mp = reg.get<ecs::Position>(macro);
    int nx = int(mp.x) % mapW;
    int ny = int(mp.y) % mapH;
    if (nx < 0) nx += mapW;
    if (ny < 0) ny += mapH;
    out.has = true;
    out.cx = nx;
    out.cy = ny;
    out.macro = macro;
    return out;
}

// ── Identity adoption (Inc 5e-2) ──────────────────────────────────────────

int adopt_possessed_macro_as_player(ecs::World& w, entt::entity macro) {
    auto& reg = w.reg;
    // Null / stale / not a real macro NPC → nothing to adopt; leave the flag
    // wherever the caller's teardown put it (this is the un-possessed exit path).
    if (macro == entt::null || !reg.valid(macro)) return -1;
    if (!reg.all_of<ecs::MacroNpcRuntime>(macro)) return -1;
    // Move the single player flag onto the lord you inhabited. leave() has
    // already reaped the SubworldTag body that wore it, so this becomes the sole
    // PlayerTag afterwards — the exactly-one invariant holds.
    if (!reg.all_of<ecs::PlayerTag>(macro)) reg.emplace<ecs::PlayerTag>(macro);
    // The save-stable identity is the deterministic spawn ordinal, not the
    // (never-serialised) entity id. make_npc always stamps one; a missing id
    // means a synthetic setup, in which case in-memory possession still works
    // but cannot persist (return -1 ⇒ boot won't try to reattach).
    if (const auto* sid = reg.try_get<ecs::MacroSpawnId>(macro)) {
        return int(sid->index);
    }
    return -1;
}

// ── Possession (Inc 5c) ──────────────────────────────────────────────────

entt::entity current_player_body(ecs::World& w) {
    // Exactly one PlayerTag flag is live while a subworld is active; return the
    // first (and only) holder. entt::null before enter / after leave.
    for (auto e : w.reg.view<ecs::PlayerTag>()) return e;
    return entt::null;
}

bool possess_entity(ecs::World& w, entt::entity target) {
    auto& reg = w.reg;
    if (target == entt::null || !reg.valid(target)) return false;
    if (!reg.all_of<ecs::Position>(target)) return false; // must be a real body
    const entt::entity cur = current_player_body(w);
    if (cur == target) return false;                      // already inhabiting it
    if (reg.valid(cur)) {
        reg.remove<ecs::PlayerTag>(cur);
        // Hero husk (no NPCKind) has no independent existence — its canonical
        // state lives in gs.player. Destroy it rather than strand an inert,
        // un-rendered, un-AI'd body in the scene. A vacated FOREIGN body keeps
        // every component; with the flag gone its AI / draw / targetability all
        // resume by construction (each is PlayerTag-gated).
        if (!reg.all_of<ecs::NPCKind>(cur)) reg.destroy(cur);
    }
    if (!reg.all_of<ecs::PlayerTag>(target)) reg.emplace<ecs::PlayerTag>(target);
    return true;
}

} // namespace sm::sub

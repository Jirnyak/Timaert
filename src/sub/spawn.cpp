#include "sub/spawn.h"
#include "sub/city_layout.h"
#include "macro/entry_context.h"
#include "macro/faction.h"
#include "ecs/components.h"
#include "ecs/npc_character.h"
#include "core/rng.h"
#include "macro/npc.h"
#include "macro/character_sheet.h"
#include "macro/macro_stock.h"
#include "sub/body.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace sm::sub {

namespace {

constexpr int kMaxSubworldSpawnReaps = 2048;
// How fast a body's DRAWN position catches up to its logical one, and how much
// of its walking speed it uses when it has nowhere in particular to be. One
// value each, because two bodies of the same kind moved at different smoothing
// speeds depending on which spawner made them (32 vs 48).
constexpr float kBodyVisualCatchUp = 32.0f;
constexpr float kBodyWanderSpeedFraction = 0.35f;
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

// The face of a DERIVED body: appearance and name rolled from its seed, never
// stored anywhere above. `tintBase` used to be 150 here and 160 in the other two
// spawners — a difference nobody could see (no pass reads NpcCharacter's tint)
// and nobody could justify, which is exactly how the seven other divergences
// started. One value, named once.
constexpr int kBodyFaceTintBase = 160;

// hash3, not a XOR chain: the settlement populator passes seed = cellSeed ^
// (i*7919) and salt = i*7919, so `seed ^ type ^ salt` cancelled the i term and
// every citizen of one type in one town wore the SAME face (the clone crowds of
// 2026-08-10). A multiplicative avalanche hash cannot cancel equal
// contributions arriving through different arguments.
ecs::NpcCharacter derive_face(std::uint32_t seed, NPCType type,
                              std::uint32_t salt) {
    Rng rng(hash3(seed, salt, std::uint32_t(type)));
    return ecs::roll_npc_character(rng, kBodyFaceTintBase);
}

// Find a spot for one inhabitant of the settlement centred at (cx, cy) with
// built-up radius `radius` (sub/city_layout.h — the SAME number the generator
// stamped its walls from).
//
// This used to draw a uniform tile out of the whole 1024×1024 macro cell, which
// had nothing to do with where the town stood: a city walls 4–8 % of its cell
// and a village ~1 %, so nearly every "citizen" was born in the fields and
// forests outside the gates and the streets inside were deserted. Sampling is
// now confined to the built-up disk — strictly inside the walls, nobody outside.
//
// r = radius·√u, not radius·u: uniform in AREA. Sampling the radius linearly
// would pile the whole population onto the market square.
bool find_city_spawn_spot(const std::vector<std::uint8_t>& tiles,
                          Rng& rng,
                          float cx,
                          float cy,
                          float radius,
                          float& fx,
                          float& fy) {
    if (tiles.size() < std::size_t(kFullSize) * std::size_t(kFullSize)) {
        return false;
    }
    if (!(radius > 0.0f)) return false;
    constexpr float kTau = 6.2831853f;
    for (int attempt = 0; attempt < 64; ++attempt) {
        const float a = rng.next_f01() * kTau;
        const float r = radius * std::sqrt(rng.next_f01());
        const int x = int(cx + std::cos(a) * r);
        const int y = int(cy + std::sin(a) * r);
        if (x < 0 || x >= kFullSize || y < 0 || y >= kFullSize) continue;
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

// ── THE birth of a subworld humanoid ───────────────────────────────────────
//
// One function, because a body is one idea. Four places used to write this
// sequence by hand — citizens, the player's squad, the macro projection and the
// hostile spawner — and by 2026-08-06 they had drifted apart in seven ways, one
// of which the player could see: the SQUAD was invisible. Its bodies were built
// without `NpcCharacter`, so the paper-doll pass (which draws `Position +
// NpcCharacter`) never saw them, and their sprite kept the default archetype
// 0xFF, which the creature pass skips — you walked into the subworld with ten
// mercenaries and saw an empty field with something invisible swinging in it.
//
// That was not a missing component. It was a fifth dialect of "what a body is".
// So the component set lives HERE, derived from the ONE table row (macro/npc.h
// kNpcTypeDefs) plus the caller's CONTEXT — who it fights for, what rank it
// holds, why it is standing there. A new component on bodies is one line in
// this function and every kind of body has it; a caller cannot forget what it
// never spells out.
//
// What is context (a parameter) and what is data (the row) is the whole design:
// faction, level, position and role come from above — the world decided them.
// Reach, speed, damage, sight, the sprite and the light it carries come from
// the row, because those are what the THING is, and the table is the only place
// that says so.
//
// The component set itself. Only two things about a body are not settled by its
// row and its context — the face it wears and the wounds it already carries —
// and those are exactly what the axis in spawn.h decides (derived vs tracked).
// Everything below is the same for a peasant, a mercenary and a lord.
entt::entity emplace_body(entt::registry& reg, const HumanoidBody& body,
                          const ecs::NpcCharacter& face,
                          float healthFraction,
                          const AuraMods* aura = nullptr) {
    const NpcTypeDef& def = npc_def(body.type);
    CharacterSheet sheet =
        make_character_sheet(body.type, body.level, body.seed);
    // The leader's buff lands IN the sheet, before anything is projected from
    // it (macro/aura.h, ruling №2): from here down a buffed soldier is simply
    // a soldier whose sheet says more, and no formula ever meets a second
    // source of strength standing beside it.
    if (aura) apply_aura(sheet, *aura);
    const CombatTemplate pc = project_combat(sheet, def.combat);

    // Integers, not fractions: a body that hits for 17.85 accumulates a
    // different wound than one that hits for 17, and the two spawners used to
    // disagree about which it was.
    const float maxHp  = std::max(1.0f, std::floor(pc.hp));
    const float damage = std::floor(pc.damage);
    // Wounds travel as a FRACTION, not as a number of points, so the two layers
    // never have to agree on how big a lord's bar is. A tracked entity at two
    // thirds arrives at two thirds whatever the sheet says down here, and the
    // return trip needs no conversion table either.
    const float hp = std::clamp(std::floor(maxHp * healthFraction), 1.0f, maxHp);

    const auto e = reg.create();
    reg.emplace<ecs::Position>(e, body.x, body.y, 0.0f);
    reg.emplace<ecs::VisualPos>(e, body.x, body.y, kBodyVisualCatchUp);
    reg.emplace<ecs::NPCKind>(e, std::uint16_t(body.type), body.faction);
    reg.emplace<ecs::Health>(e, hp, maxHp);
    reg.emplace<ecs::Combat>(e,
        damage, pc.speed, pc.attackRange, pc.cooldown, 0.0f,
        pc.attackKind == CombatTemplate::Missile ? ecs::Combat::Missile
                                                 : ecs::Combat::Melee);
    maybe_emplace_missile_attack(reg, e, pc);
    reg.emplace<ecs::NpcLevel>(e, std::int16_t(body.level));
    reg.emplace<ecs::SubworldTag>(e);
    reg.emplace<ecs::SubworldAi>(e,
        body.combatant ? ecs::SubworldAi::Combat : subworld_ai_for(def.ai),
        /*aiTimer*/0.0f, /*vx*/0.0f, /*vy*/0.0f,
        /*wanderSpeed*/pc.speed * kBodyWanderSpeedFraction,
        /*radius*/pc.bodyRadius);
    reg.emplace<CharacterSheet>(e, sheet);
    // A face for every body. This is the line the squad never had.
    reg.emplace<ecs::NpcCharacter>(e, face);
    // The sprite record. Its tint is NOT set from the call site any more: for a
    // humanoid (archetype 0xFF) the paper-doll pass draws the body from the FACE
    // and never reads these bytes, so the three spawners were each inventing a
    // colour nobody could see — a guard tinted 170, a hostile always red. When
    // the pass learns to tint, it will read a column of the table like a
    // creature's does, not four guesses in four call sites.
    // Width AND height, both from the row (sub/body.h): what a thing is, how
    // much room it takes, and how big it looks are one decision. The height is
    // varied by the body's own shape byte — the one the face has always rolled
    // and nobody has ever read — so a crowd has tall and short people in it
    // without a second field or a second roll.
    reg.emplace<ecs::Sprite>(e, std::uint16_t(body.type),
        std::uint8_t(255), std::uint8_t(255), std::uint8_t(255),
        std::uint8_t(255), pc.bodyRadius, std::uint8_t(def.sprite),
        body_height_m(def) * body_shape_height_scale(face.bodyShape));
    maybe_emplace_carried_light(reg, e, def);
    return e;
}

void spawn_settlement_population(ecs::World& w,
                                 LandmarkKind landmark,
                                 const SeamlessSubworldManager& mgr,
                                 std::uint32_t seed,
                                 std::uint16_t settlementFaction,
                                 int landmarkPop,
                                 int levelBonus,
                                 int originX,
                                 int originY,
                                 MacroStockKey populationKey) {
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

    // Both generators build their settlement on the CELL CENTRE, and the disk
    // they build it in is defined once in sub/city_layout.h. A town's people
    // belong in that disk — see find_city_spawn_spot.
    const float centerX = float(originX) + float(kCellSize) * 0.5f;
    const float centerY = float(originY) + float(kCellSize) * 0.5f;
    const float populationRadius = settlement_population_radius(city, pop);

    for (int i = 0; i < target; ++i) {
        float fx = 0.0f;
        float fy = 0.0f;
        if (!find_city_spawn_spot(tiles, rng, centerX, centerY,
                                  populationRadius, fx, fy)) {
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
        // A citizen is DERIVED — he is one unit of this place's population made
        // visible, and nothing about him is remembered above. What is CONTEXT
        // here: which town's faction he wears, what the place's level does to
        // him, and that he lives his errands rather than fighting. Everything
        // else comes from his row and his seed.
        //
        // The loan says which stock he was drawn from, so his death pays the
        // settlement back without anyone asking what kind of body it was: a town
        // cannot be emptied in the subworld while the map still counts everyone
        // as alive. Borrowing and returning are the same row of one table.
        spawn_derived_body(reg,
            HumanoidBody{
                type, fx, fy, settlementFaction,
                normalize_soldier_level(npc_def(type).baseLevel
                                        + int(rng.next_u32() % 3u) + levelBonus),
                seed ^ (std::uint32_t(i) * 7919u),
                /*combatant*/false},
            /*faceSalt*/std::uint32_t(i) * 7919u,
            BodyLoan::from(MacroStock::Population, populationKey));
    }
}

// Emplace one fauna creature at (fx,fy). Shared by the per-cell and the
// whole-window spawn paths so the entity layout lives in exactly one place. The
// caller decides npcLevel and the context multipliers (they consume the caller's
// RNG stream in order); the per-level HP/damage scale folds in here.
void emplace_fauna_entity(entt::registry& reg, const FaunaEntry& f,
                          std::uint16_t faction, float fx, float fy,
                          int npcLevel, float hpMult, float damageMult,
                          const BodyLoan& loan = BodyLoan::none()) {
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
    // Look comes from the row's sprite — ONE table for every visible kind.
    const SpriteDef& look = sprite_row(f.sprite);
    const std::uint8_t cr = std::uint8_t((look.tint >> 16) & 0xFFu);
    const std::uint8_t cg = std::uint8_t((look.tint >>  8) & 0xFFu);
    const std::uint8_t cb = std::uint8_t( look.tint        & 0xFFu);
    reg.emplace<ecs::Sprite>(e, typeId, cr, cg, cb, std::uint8_t(255), f.radius,
                             std::uint8_t(f.sprite), body_height_m(f));
    // The receipt, stamped by the birth (the spawn_derived_body doctrine): a
    // wild head is one unit of its cell's fauna_count made visible, and its
    // death thins the cell for good. No loan = a creature from thin air
    // (console spawns, harnesses) — honest about owing the map nothing.
    if (loan.stock != MacroStock::Count) {
        stamp_macro_debt(reg, e, loan.stock, loan.key, 1);
    }
}

} // namespace

// ── The two forms of birth (declared in spawn.h) ─────────────────────────

entt::entity spawn_derived_body(entt::registry& reg, const HumanoidBody& body,
                                std::uint32_t faceSalt, const BodyLoan& loan,
                                const AuraMods* aura) {
    const entt::entity e =
        emplace_body(reg, body, derive_face(body.seed, body.type, faceSalt),
                     /*healthFraction*/1.0f, aura);
    // The receipt, stamped by the birth rather than by the caller: borrowing is
    // part of coming into being, not a line a spawner might remember to add.
    // Nothing lent means nothing to stamp — a body drawn from thin air is honest
    // about owing the map nothing.
    if (loan.stock != MacroStock::Count) {
        stamp_macro_debt(reg, e, loan.stock, loan.key, 1);
    }
    return e;
}

entt::entity spawn_tracked_body(entt::registry& reg, entt::entity macro,
                                float x, float y, std::uint32_t seed,
                                bool combatant) {
    if (macro == entt::null || !reg.valid(macro)) return entt::null;
    if (!reg.all_of<ecs::NPCKind, ecs::Health, ecs::NpcLevel,
                    ecs::NpcCharacter>(macro)) {
        return entt::null;
    }
    const auto& kind = reg.get<ecs::NPCKind>(macro);
    // Guard on the WIDE type before narrowing, so a monster id (0x100 | idx)
    // can never alias a humanoid row.
    if (kind.type >= std::uint16_t(NPCType::Count)) return entt::null;

    const auto& health = reg.get<ecs::Health>(macro);
    const float fraction = health.maxHp > 0.0f
        ? std::clamp(health.hp / health.maxHp, 0.0f, 1.0f)
        : 1.0f;

    HumanoidBody body{};
    body.type      = static_cast<NPCType>(kind.type);
    body.x         = x;
    body.y         = y;
    // What it is, whose it is and how senior it is are read from the entity
    // itself — a tracked body has no second opinion about its own identity.
    body.faction   = kind.factionIdx;
    body.level     = normalize_soldier_level(reg.get<ecs::NpcLevel>(macro).value);
    body.seed      = seed;
    body.combatant = combatant;

    const entt::entity e =
        emplace_body(reg, body, reg.get<ecs::NpcCharacter>(macro), fraction);

    // The belongings and the personality are STATE up there, so they are copied,
    // not rolled. A derived body has neither on purpose: its loot is rolled from
    // its seed at the moment it dies, which costs a city of five thousand people
    // exactly nothing to carry.
    if (const auto* bag = reg.try_get<ecs::NpcInventory>(macro)) {
        reg.emplace<ecs::NpcInventory>(e, *bag);
    }
    if (const auto* traits = reg.try_get<ecs::NpcTraits>(macro)) {
        reg.emplace<ecs::NpcTraits>(e, *traits);
    }
    // The backlink is part of being tracked, not an extra the caller attaches:
    // it is the address the return trip writes to.
    reg.emplace<ecs::MacroOrigin>(e, macro);
    return e;
}

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

// ── Dungeon residents (sub/dgn interiors) ────────────────────────────────

int spawn_dungeon_residents(ecs::World& w,
                            const SeamlessSubworldManager& mgr,
                            std::uint32_t seed,
                            std::uint16_t settlementFaction,
                            int count,
                            int levelBonus,
                            float x0, float y0, float x1, float y1,
                            std::uint8_t floorTile,
                            MacroStockKey populationKey) {
    if (count <= 0) return 0;
    const auto& tiles = mgr.tiles();
    if (tiles.size() < std::size_t(kFullSize) * std::size_t(kFullSize)) {
        return 0;
    }
    Rng rng(seed ^ 0xD0E51DE7u);
    int placed = 0;
    for (int i = 0; i < count; ++i) {
        // Plain interior floor only: partitions paint TILE_WALL, furniture
        // paints TILE_HOUSE, the threshold TILE_ROAD — a body stands on none
        // of them, and WHICH tile is floor is the interior's own answer.
        float fx = 0.0f, fy = 0.0f;
        bool found = false;
        for (int attempt = 0; attempt < 24 && !found; ++attempt) {
            fx = x0 + rng.next_f01() * std::max(0.0f, x1 - x0);
            fy = y0 + rng.next_f01() * std::max(0.0f, y1 - y0);
            const int ix = int(fx), iy = int(fy);
            if (ix < 1 || iy < 1 || ix >= kFullSize - 1 || iy >= kFullSize - 1) {
                continue;
            }
            if (tiles[std::size_t(iy) * kFullSize + std::size_t(ix)]
                != floorTile) {
                continue;
            }
            found = true;
        }
        if (!found) continue;
        const NPCType type = pick_civilian_type(rng);
        // The same derived-citizen birth as the street (one row of one law):
        // level from the settlement's context bonus, loan from the SAME
        // population stock — a death in here pays the town back exactly like
        // a death on the square.
        spawn_derived_body(w.reg,
            HumanoidBody{
                type, fx, fy, settlementFaction,
                normalize_soldier_level(npc_def(type).baseLevel
                                        + int(rng.next_u32() % 3u) + levelBonus),
                seed ^ (std::uint32_t(i) * 7919u),
                /*combatant*/false},
            /*faceSalt*/std::uint32_t(i) * 7919u,
            BodyLoan::from(MacroStock::Population, populationKey));
        ++placed;
    }
    return placed;
}

int spawn_dungeon_vermin(ecs::World& w,
                         const SeamlessSubworldManager& mgr,
                         std::uint32_t seed,
                         LandmarkKind tableKind,
                         Biome biome,
                         int treeCount,
                         int levelBonus,
                         float hpMult,
                         float damageMult,
                         int budget,
                         float x0, float y0, float x1, float y1,
                         std::uint8_t floorTile,
                         MacroStockKey faunaKey) {
    if (budget <= 0) return 0;
    const auto& tiles = mgr.tiles();
    if (tiles.size() < std::size_t(kFullSize) * std::size_t(kFullSize)) {
        return 0;
    }
    // One table resolve, the same one the open cell runs.
    const FaunaTable& table = get_fauna_table(biome, treeCount, tableKind);
    std::uint32_t rngState = seed ^ 0xCE11A5u;
    auto picks = roll_fauna(table, rngState);
    if (picks.empty()) return 0;

    Rng pos(rngState);
    auto& reg = w.reg;
    int placed = 0;
    for (const auto& p : picks) {
        if (placed >= budget) break;
        const FaunaEntry& f = *p.entry;
        float fx = 0.0f, fy = 0.0f;
        bool found = false;
        for (int attempt = 0; attempt < 24 && !found; ++attempt) {
            fx = x0 + pos.next_f01() * std::max(0.0f, x1 - x0);
            fy = y0 + pos.next_f01() * std::max(0.0f, y1 - y0);
            const int ix = int(fx), iy = int(fy);
            if (ix < 1 || iy < 1 || ix >= kFullSize - 1 || iy >= kFullSize - 1) {
                continue;
            }
            if (tiles[std::size_t(iy) * kFullSize + std::size_t(ix)]
                != floorTile) {
                continue;
            }
            found = true;
        }
        if (!found) continue;
        const int npcLevel = normalize_soldier_level(
            int(f.baseLevel) + int(std::floor(pos.next_f01() * 2.0f))
            + levelBonus);
        emplace_fauna_entity(reg, f, faction_index(p.factionId), fx, fy,
                             npcLevel, hpMult, damageMult,
                             BodyLoan::from(MacroStock::FaunaCount, faunaKey));
        ++placed;
    }
    return placed;
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
                     int zoneLevel,
                     int landmarkSubjectId,
                     int macroCellX,
                     int macroCellY,
                     int faunaCount) {
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
                                landmarkPop, levelBonus, originX, originY,
                                MacroStockKey{landmarkSubjectId,
                                              std::int16_t(macroCellX),
                                              std::int16_t(macroCellY)});

    const FaunaTable& table = get_fauna_table(biome, treeCount, landmark);
    std::uint32_t rngState = cellSeed ^ 0xFAEAu;
    auto picks = roll_fauna(table, rngState);
    if (picks.empty()) return;

    // The honest headcount: the roll proposes, the macro stock DISPOSES. A
    // hunted cell embodies only what still stands on it — return after a
    // cull and the survivors are all there is (the repopulate-on-recenter
    // farm dies here). -1 = no macro context wired = the old unbounded roll.
    int budget = faunaCount >= 0 ? faunaCount : int(picks.size());
    const BodyLoan faunaLoan = faunaCount >= 0
        ? BodyLoan::from(MacroStock::FaunaCount,
                         MacroStockKey{-1, std::int16_t(macroCellX),
                                       std::int16_t(macroCellY)})
        : BodyLoan::none();

    Rng pos(rngState);
    const auto& tiles = mgr.tiles();
    const bool tilesUsable =
        tiles.size() >= std::size_t(kFullSize) * std::size_t(kFullSize);
    for (const auto& p : picks) {
        if (budget <= 0) break;
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
                             npcLevel, hpMult, damageMult, faunaLoan);
        --budget;
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
                        std::uint16_t faction,
                        const AuraMods* aura) {
    spawn_player_squad(w, squad, mgr.tiles(), playerX, playerY, seed, faction,
                       aura);
}

void spawn_player_squad(ecs::World& w,
                        const SoldierSquad& squad,
                        const std::vector<std::uint8_t>& tiles,
                        float playerX,
                        float playerY,
                        std::uint32_t seed,
                        std::uint16_t faction,
                        const AuraMods* aura) {
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
        const int level = normalize_soldier_level(soldier.level);
        // The sheet, the combat template and the whole body are derived inside
        // the one birth below; the slot only decides WHO stands here and where.
        // Seeded per squad slot (kind + level + slot) so a squad reprojects
        // identically, and level scaling stays in the sheet's spent points.

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

        // The squad is born through the ONE birth every subworld humanoid gets.
        // A squad is not a kind of creature — it is CONTEXT from the map above:
        // whoever the leader raised, embodied here under the leader's faction.
        // Put a goblin or a dragon in the roster on the macro layer and that is
        // what walks beside you, drawn from the same table as everything else.
        // Before this, the squad had its own hand-written birth that forgot
        // `NpcCharacter` — which is precisely why an army of ten was invisible.
        //
        // A soldier is DERIVED: the roster line says WHO stands here, the seed
        // says everything else. Nothing is lent yet — the roster becomes a macro
        // stock of its own when squads become the macro entity (macrosim.md,
        // "Squad as THE macro entity"), and on that day this call gains a loan
        // and nothing else changes.
        const auto e = spawn_derived_body(reg,
            HumanoidBody{
                type, fx, fy, faction, level,
                (std::uint32_t(i) * 2654435761u)
                    ^ (std::uint32_t(soldier.kind) << 8)
                    ^ std::uint32_t(level),
                /*combatant*/true},
            /*faceSalt*/std::uint32_t(i) * 2654435761u,
            BodyLoan::none(), aura);
        reg.emplace<ecs::PlayerSoldierTag>(e);
        reg.emplace<ecs::SoldierLink>(e, soldier.entityId, soldier.kind,
                                      std::int16_t(level));
    }
}

// ── Macro→subworld projection (Inc 5d) ───────────────────────────────────

int project_macro_npcs_into_subworld(ecs::World& w,
                                     const SeamlessSubworldManager& mgr,
                                     int centerCx, int centerCy,
                                     int mapW, int mapH,
                                     std::uint32_t seed) {
    return project_macro_npcs_into_subworld(w, mgr.tiles(), centerCx, centerCy,
                                            mapW, mapH, seed);
}

int project_macro_npcs_into_subworld(ecs::World& w,
                                     const std::vector<std::uint8_t>& tiles,
                                     int centerCx, int centerCy,
                                     int mapW, int mapH,
                                     std::uint32_t seed) {
    auto& reg = w.reg;
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

        // THE tracked form, and the whole reason the axis exists: this body is
        // not a body LIKE the lord up there — it IS him, wearing his face, his
        // wounds and his belongings, with the backlink that lets what happens
        // here be written back. Everything the projection used to spell out by
        // hand — the sheet, the combat template, the hostility from the row, the
        // copied identity — is the one birth now, so a lord and a townsman can
        // no longer differ in anything but what the axis says they differ in.
        const entt::entity leaderBody =
            spawn_tracked_body(reg, macro, fx, fy, seed ^ salt ^ 0x5D0F11u,
                               /*combatant*/false);
        if (leaderBody == entt::null) {
            continue;   // not a body-shaped macro entity; nothing was created
        }
        ++projected;

        // The leader's buff, read from the leader's OWN sheet (macro/aura.h)
        // — collected once per squad and applied into every member's sheet at
        // birth. A generic leader's derived sheet carries no aura sources
        // today (no perks), so this collects empty and changes nothing; a
        // hand-authored or persistent leader with a Leader-class perk buffs
        // its troops through this same line with no further change anywhere.
        AuraMods leaderAura{};
        if (const auto* leaderSheet = reg.try_get<CharacterSheet>(leaderBody)) {
            leaderAura = collect_leader_aura(*leaderSheet);
        }

        // The leader's troops. Each roster row is one unit of the squad's
        // roster STOCK made visible: a DERIVED body — the row says WHO stands
        // here, the seed says everything else — wearing the OWNER's faction
        // (the banner rule, spawn.h) and carrying the receipt that pays its
        // death back into the roster (macro/macro_stock.h "roster": subject =
        // the squad's MacroSpawnId ordinal, detail = this member's entityId).
        // Placed on a tight ring around the leader, dodging water like every
        // other placement here; a member that finds no land stands ON the
        // leader's spot rather than being lost. Counted against the same
        // projection cap as everyone else.
        if (const auto* roster = reg.try_get<ecs::SquadRoster>(macro)) {
            const auto* sid = reg.try_get<ecs::MacroSpawnId>(macro);
            constexpr float kTau = 6.2831853f;
            const int memberCount = int(roster->members.size());
            for (int m = 0; m < memberCount; ++m) {
                if (projected >= kMaxProjectedMacroNpcs) break;
                const SoldierRecord& rec = roster->members[std::size_t(m)];
                if (!valid_npc_kind(rec.kind)) continue;

                float mfx = fx, mfy = fy;
                for (int attempt = 0; attempt < 20; ++attempt) {
                    const float ang = (float(m) / float(memberCount)) * kTau
                                      + (rng.next_f01() - 0.5f) * 0.9f;
                    const float rad = 2.0f + rng.next_f01() * 3.0f;
                    const float tx = std::clamp(fx + std::cos(ang) * rad,
                                                1.0f, float(kFullSize - 2));
                    const float ty = std::clamp(fy + std::sin(ang) * rad,
                                                1.0f, float(kFullSize - 2));
                    const int ix = int(tx), iy = int(ty);
                    if (tilesUsable &&
                        tiles[std::size_t(iy) * kFullSize + ix] == TILE_WATER) {
                        continue;
                    }
                    mfx = tx; mfy = ty;
                    break;
                }

                // No MacroSpawnId (synthetic setups only — make_npc always
                // stamps one) means no addressable roster: an honest fiat body
                // rather than a receipt against nobody.
                const BodyLoan loan = sid
                    ? BodyLoan::from(
                          MacroStock::Roster,
                          MacroStockKey{std::int32_t(sid->index),
                                        std::int16_t(int(mpos.x)),
                                        std::int16_t(int(mpos.y)),
                                        std::int32_t(rec.entityId)})
                    : BodyLoan::none();
                spawn_derived_body(reg,
                    HumanoidBody{
                        static_cast<NPCType>(rec.kind), mfx, mfy,
                        kind.factionIdx,
                        normalize_soldier_level(rec.level),
                        ((seed ^ salt) + std::uint32_t(m) * 2654435761u)
                            ^ (rec.entityId << 7),
                        /*combatant*/true},
                    /*faceSalt*/std::uint32_t(m) * 2654435761u ^ 0x9E3779B9u,
                    loan, &leaderAura);
                ++projected;
            }
        }
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

#include "sub/spawn.h"
#include "sub/city_layout.h"
#include "sub/dgn/dispatch.h"   // dungeon_scene_seed, dungeon_has_upper —
                                // the household/partition law (CANON S28)
#include "macro/entry_context.h"
#include "macro/faction.h"
#include "ecs/components.h"
#include "ecs/npc_character.h"
#include "core/rng.h"
#include "macro/npc.h"
#include "macro/character_sheet.h"
#include "macro/macro_stock.h"
#include "macro/tree_layer.h"
#include "macro/spell_book_state.h"   // SpellBook — part of the record a body inherits
#include "macro/squad.h"              // sheet_of — THE door to "who is this"
#include "macro/player_entity.h"      // player_squad_entity — «чья это запись»
#include "sub/record.h"              // record_of / StandingMirror — дверь шва
#include "sub/body.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
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
// (kMaxProjectedMacroNpcs — the projection's 128-body ceiling — died with
// §42 Инк 6, owner: «потолков нет». The window filter alone decides who
// stands; the crowd grid is the one physical bound and it shouts.)

// Does something CARRY a body above the water plane here? The universal half
// of the wet-tile question (sub/height.h): the tile says what the ground is,
// this says what would hold a body up regardless of it — a bridge deck, a
// jetty, a wall walk. Without an index the answer is honestly "no", which is
// the old tile-only behaviour, unchanged.
bool carried_above_water(const StructureIndex* solids, float x, float y) {
    if (!solids || solids->empty()) return false;
    // Probe from above with no step allowance: the highest solid top under
    // the probe ceiling, whatever it belongs to.
    const float top = solids->support_at(x, y, kNpcBodyRadiusDefault,
                                         kSeaLevelM + kDryFootingProbeM,
                                         /*stepUp*/0.0f);
    return is_dry_footing(top);
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

// ── THE placement door (CANON S28) ────────────────────────────────────────
// One law for every rejection-sampled stand: draw candidates from the
// caller's OWN point law — the owner of the truth about "where" — until one
// stands. The door only answers yes or no; the CALLER counts the refusals
// and says them out loud (no silent drops, no ceilings). Catalog-backed
// scenes (interiors) never come here at all: their floor points are
// standable by construction (dgn/dispatch.cpp folds trav while it is alive).
template <typename NextPoint, typename Accept>
bool resolve_stand(int attempts, NextPoint next, Accept ok,
                   float& outX, float& outY) {
    for (int a = 0; a < attempts; ++a) {
        float fx = 0.0f, fy = 0.0f;
        next(fx, fy);
        if (!ok(fx, fy)) continue;
        outX = fx;
        outY = fy;
        return true;
    }
    return false;
}

// ── The town's shape, READ BACK OFF THE GROUND ────────────────────────────
// A settlement is no longer a disk: its outline is grown over the terrain, so
// it runs long down its tract and stops at a bluff (sub/gens/kit/growth.h).
// A scalar radius can therefore only describe the part of it that is
// guaranteed — and filling that disk with people while the streets and houses
// spread past it puts a round crowd inside a shape that is not round.
//
// This is NOT a second definition of the shape. The generator decides where
// the wall goes; this MEASURES where it went, by walking out along each
// bearing and remembering the furthest masonry. One authority, and no
// plumbing between two systems that would then have to be kept in step — the
// same reasoning the wall-integrity test uses to audit the ring without
// re-implementing it.
struct TownShape {
    static constexpr int kBearings = 256;   // ~1.4°, finer than any gateway
    float cx = 0.0f, cy = 0.0f;
    std::array<float, kBearings> r{};       // outermost masonry per bearing

    bool holds(float x, float y, float inset) const {
        const float dx = x - cx, dy = y - cy;
        constexpr float kTau = 6.2831853f;
        float a = std::atan2(dy, dx);
        if (a < 0.0f) a += kTau;
        int b = int(a / kTau * float(kBearings));
        if (b >= kBearings) b = kBearings - 1;
        const float bound = r[std::size_t(b)] - inset;
        if (bound <= 0.0f) return false;
        return dx * dx + dy * dy <= bound * bound;
    }
};

// The furthest the town's masonry reaches — the disk the sampler must cover to
// be able to land anywhere inside it.
float shape_reach(const TownShape& s) {
    float m = 0.0f;
    for (float v : s.r) m = std::max(m, v);
    return m;
}

TownShape measure_town(const std::vector<std::uint8_t>& tiles,
                       float cx, float cy, float maxRadius, float fallback) {
    TownShape s{};
    s.cx = cx;
    s.cy = cy;
    s.r.fill(0.0f);
    if (tiles.size() < std::size_t(kFullSize) * std::size_t(kFullSize)) {
        s.r.fill(fallback);
        return s;
    }
    constexpr float kTau = 6.2831853f;
    for (int b = 0; b < TownShape::kBearings; ++b) {
        const float ang = float(b) * kTau / float(TownShape::kBearings);
        const float ca = std::cos(ang), sa = std::sin(ang);
        for (float d = 1.0f; d <= maxRadius; d += 1.0f) {
            const int x = int(cx + ca * d);
            const int y = int(cy + sa * d);
            if (x < 0 || y < 0 || x >= kFullSize || y >= kFullSize) break;
            if (tiles[std::size_t(y) * kFullSize + x] == TILE_WALL) {
                s.r[std::size_t(b)] = d;      // keep the FURTHEST: the outer ring
            }
        }
    }
    // A bearing that met no masonry is a gateway (or an unwalled place): take
    // what its neighbours know, so a gate does not read as a hole in the crowd.
    for (int b = 0; b < TownShape::kBearings; ++b) {
        if (s.r[std::size_t(b)] > 0.0f) continue;
        float lo = 0.0f, hi = 0.0f;
        for (int k = 1; k < TownShape::kBearings; ++k) {
            const std::size_t j = std::size_t((b - k + TownShape::kBearings)
                                              % TownShape::kBearings);
            if (s.r[j] > 0.0f) { lo = s.r[j]; break; }
        }
        for (int k = 1; k < TownShape::kBearings; ++k) {
            const std::size_t j = std::size_t((b + k) % TownShape::kBearings);
            if (s.r[j] > 0.0f) { hi = s.r[j]; break; }
        }
        const float best = std::max(lo, hi);
        s.r[std::size_t(b)] = best > 0.0f ? best : fallback;
    }
    return s;
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
                          const TownShape& shape,
                          float& fx,
                          float& fy) {
    if (tiles.size() < std::size_t(kFullSize) * std::size_t(kFullSize)) {
        return false;
    }
    if (!(radius > 0.0f)) return false;
    constexpr float kTau = 6.2831853f;
    // Sample the town's REACH and keep what its outline holds. Sampling the
    // guaranteed disk instead would put a round crowd in a town that is not
    // round — visible at a glance on the minimap, and wrong on the ground:
    // the lobes a city grows down its tract would hold houses and no people.
    const float reach = std::max(radius, shape_reach(shape));
    const bool found = resolve_stand(
        64,
        [&](float& sx, float& sy) {
            const float a = rng.next_f01() * kTau;
            const float r = reach * std::sqrt(rng.next_f01());
            sx = cx + std::cos(a) * r;
            sy = cy + std::sin(a) * r;
        },
        [&](float sx, float sy) {
            const int x = int(sx), y = int(sy);
            if (x < 0 || x >= kFullSize || y < 0 || y >= kFullSize) {
                return false;
            }
            // Inside the walls the generator actually built — per bearing, so
            // an organic footprint is respected exactly. The inset keeps a
            // body off the masonry's own thickness.
            if (!shape.holds(sx, sy, kSettlementWallRing.halfThickness + 1.0f)) {
                return false;
            }
            const std::uint8_t t = tiles[std::size_t(y) * kFullSize + x];
            // Water drowns; house/wall footprints are SOLID (sub/collide.h)
            // — a body born inside masonry would have to walk out through
            // the escape rule, so don't put it there in the first place.
            return t != TILE_WATER && t != TILE_HOUSE && t != TILE_WALL;
        },
        fx, fy);
    if (!found) return false;
    fx = float(int(fx)) + 0.5f;
    fy = float(int(fy)) + 0.5f;
    return true;
}

// (pick_civilian_type lived here until 2026-08-24 — an RNG-only crowd that
// could not tell an iron town from a swamp one, canon-audit F4. The crowd
// rolls by THE spawn law now: fauna.h pick_crowd_row.)

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
// HOW A SHEET BECOMES A SWING — one place, read by birth and by the mirror's
// re-derive (refresh_body_strike below).
//
// It used to be spelled out inline at the single site that needed it, which was
// fine while nothing could change a body's numbers after it was born. Under the
// mirror law a record CAN change under a standing body — he levels from a kill,
// something is put on him — and a second copy of this assembly is exactly the
// «two answers to one question» the project keeps paying for.
//
// `recoverySteps` is NOT a number this derives: it is the body's own clock, the
// one «occupied» gate every action charges. Re-deriving it would cancel a swing
// mid-recovery, so the refresh preserves it and birth starts it at zero.
ecs::Combat combat_from_sheet(const CharacterSheet& sheet,
                              const NpcTypeDef& def) {
    const CombatTemplate pc = project_combat(sheet, def.combat);
    return ecs::Combat{
        pc.dice, pc.flatAdd, std::int16_t(100), pc.luck,
        std::uint8_t(pc.dmgType), march_speed(pc.speedMarchMult),
        pc.attackRange, pc.cooldown, /*recoverySteps*/0u,
        pc.attackKind == CombatTemplate::Missile ? ecs::Combat::Missile
                                                 : ecs::Combat::Melee};
}

// Everything below is the same for a peasant, a mercenary and a lord.
entt::entity emplace_body(entt::registry& reg, const BodySpec& body,
                          const ecs::NpcCharacter& face,
                          float healthFraction,
                          const BonusTotals* squadBonuses = nullptr) {
    const NpcTypeDef& def = npc_def(body.type);
    // WHO HE IS. A record the world keeps wins over a fresh roll of his row:
    // the projection used to call make_character_sheet with the CELL's seed,
    // so a lord walking down into the subworld arrived as a DIFFERENT PERSON
    // of the same type and rank, and the sheet the save has been keeping for
    // him since v90 took no part in it.
    CharacterSheet sheet = body.sheet
        ? *body.sheet
        : make_character_sheet(body.type, body.level, body.seed);
    // The leader's buff lands IN the sheet, before anything is projected from
    // it (character_sheet.h, ruling №2): from here down a buffed soldier is simply
    // a soldier whose sheet says more, and no formula ever meets a second
    // source of strength standing beside it.
    if (squadBonuses) sheet = effective_sheet(sheet, *squadBonuses);
    const CombatTemplate pc = project_combat(sheet, def.combat);

    // Integers, not fractions: a body that hits for 17.85 accumulates a
    // different wound than one that hits for 17, and the two spawners used to
    // disagree about which it was. (The wound itself is dice + an already-
    // floored flatAdd now — the strike assembly is integer end to end.)
    const float maxHp  = float(body_max_hp(sheet, def.combat));
    // Wounds travel as a FRACTION, not as a number of points, so the two layers
    // never have to agree on how big a lord's bar is. A tracked entity at two
    // thirds arrives at two thirds whatever the sheet says down here, and the
    // return trip needs no conversion table either.
    const int hp = std::clamp(int(maxHp * healthFraction), 1, int(maxHp));

    const auto e = reg.create();
    reg.emplace<ecs::Position>(e, body.x, body.y, 0.0f);
    reg.emplace<ecs::VisualPos>(e, body.x, body.y, kBodyVisualCatchUp);
    reg.emplace<ecs::NPCKind>(e, std::uint16_t(body.type), body.faction);
    // ALL THREE pools, through the sheet's own doors (CANON S14 «три
    // ресурса» — a scene body is not a kind of body that gets fewer bars;
    // sp/maxSp stood at ZERO here until landing 4в). Mana does NOT cross as
    // a fraction the way a wound does: nothing in the world spends an NPC's
    // mana yet, so a body arrives with a full well rather than importing a
    // number no macro writer maintains. The day an NPC pays for a cast, this
    // is the one line that starts carrying `mpFraction` beside
    // `healthFraction`. Stamina likewise arrives full: combat does not burn
    // SP (v1 verdict) and the macro squad's fatigue is squad state, not this
    // one soldier's.
    {
        ecs::Pools pools{};
        pools.hp = hp;
        pools.maxHp = int(maxHp);
        pools.mp = pools.maxMp = body_max_mp(sheet, def.combat);
        pools.sp = pools.maxSp = body_max_sp(sheet, def.combat);
        reg.emplace<ecs::Pools>(e, pools);
    }
    // Its pace: the world's march (macro/movement_cost.h) times what this row
    // is against a walking man. ONE scale for every body, the player's
    // included — a peasant walks at exactly the speed the map says a man
    // walks, and a bandit's 2.25 means he runs.
    // The guard that keeps the two scales honest about a cell's LENGTH: the
    // macro side derives the walking speed from kSubworldTilesPerMacroCell,
    // and this file is where both constants stand in one scope.
    static_assert(float(kCellSize) == kSubworldTilesPerMacroCell,
                  "sub/map_data.h kCellSize must equal the macro side's "
                  "kSubworldTilesPerMacroCell — the parity anchor rides on it");
    const float bodySpeed = march_speed(pc.speedMarchMult);
    reg.emplace<ecs::Combat>(e, combat_from_sheet(sheet, def));
    maybe_emplace_missile_attack(reg, e, pc);
    maybe_emplace_flying(reg, e, pc);
    reg.emplace<ecs::NpcLevel>(e, std::int16_t(body.level));
    reg.emplace<ecs::SubworldTag>(e);
    // How much room this body takes: the row's ONE width column, man-shaped
    // default resolved (npc.h npc_body_radius). This is the ONE line where
    // the creature birth and the humanoid birth used to differ about a
    // number — and where a template shadow copy of the width used to answer.
    const float bodyRadius = npc_body_radius(def);
    reg.emplace<ecs::SubworldAi>(e,
        body.combatant ? ecs::SubworldAi::Combat : subworld_ai_for(def.ai),
        /*aiTimer*/0.0f, /*vx*/0.0f, /*vy*/0.0f,
        /*wanderSpeed*/bodySpeed * kBodyWanderSpeedFraction,
        /*radius*/bodyRadius);
    reg.emplace<CharacterSheet>(e, sheet);
    // A face for every body. This is the line the squad never had.
    reg.emplace<ecs::NpcCharacter>(e, face);
    // The sprite record. Colour comes from THE sprite table's row — the same
    // place a wolf's grey and a peasant's cloth come from — and never from the
    // call site: three spawners each used to invent a tint (a guard 170, a
    // hostile always red) that only the procedural pass would have read anyway.
    // A row with drawn art ignores it, because art speaks for itself.
    // Width AND height both from the row (sub/body.h): what a thing is, how
    // much room it takes and how big it looks are one decision. The height is
    // varied by the body's own shape byte — the one the face rolls — so a crowd
    // has tall and short people in it without a second field or a second roll.
    const SpriteDef& look = sprite_row(def.sprite);
    reg.emplace<ecs::Sprite>(e, std::uint16_t(body.type),
        std::uint8_t((look.tint >> 16) & 0xFFu),
        std::uint8_t((look.tint >>  8) & 0xFFu),
        std::uint8_t( look.tint        & 0xFFu),
        std::uint8_t(255), bodyRadius, std::uint8_t(def.sprite),
        body_height_m(def) * body_shape_height_scale(face.bodyShape));
    maybe_emplace_carried_light(reg, e, def);
    return e;
}

void spawn_landmark_population(ecs::World& w,
                               const SpawnContext& townCtx,
                               LandmarkType landmark,
                               const SeamlessSubworldManager& mgr,
                               std::uint32_t seed,
                               std::uint32_t worldSeed,
                               std::uint16_t settlementFaction,
                               int landmarkPop,
                               int originX,
                               int originY,
                               MacroStockKey populationKey,
                               const WorldTime& now) {
    // THE gate of the population door (§42): a place with souls and a crowd
    // family embodies — City, Village, Spire, Ruin, Lair alike. The literal
    // `!= City && != Village` that stood here outlived the refactor that
    // universalised the record underneath it, and no test reddened because
    // zero bodies is a legal count.
    const LandmarkDef& def = landmark_def(landmark);
    const int pop = std::max(0, landmarkPop);
    if (pop == 0 || def.crowdHabitat == 0) return;

    // Partition (CANON S28: one soul embodies once): the interiors' own
    // households come OFF the street — a door's residents are a SHARE of
    // this number, never a second helping on top of it.
    const int reserve = interior_reserve_for_cell(
        mgr.structures(), landmark, worldSeed,
        int(populationKey.cellX), int(populationKey.cellY),
        float(originX), float(originY), pop, now);
    const int target = std::max(0, pop - reserve);
    if (target == 0) return;

    // One street salt for every kind — the old city/village pair encoded
    // the kind into the stream, which is context's job, not the seed's.
    Rng rng(seed ^ 0xC1712E55u);
    const auto& tiles = mgr.tiles();
    auto& reg = w.reg;

    // Both settlement generators build on the CELL CENTRE, and the disk they
    // build in is defined once in sub/city_layout.h. The City/Village split
    // here is the LAYOUT's own vocabulary (walled rings vs a house core) —
    // geometry dispatch, exactly like resolve_mode; every other kind's
    // exterior stands in the village-core disk until its own layout law
    // earns a name (§42 Инк 5, the genesis increment).
    const float centerX = float(originX) + float(kCellSize) * 0.5f;
    const float centerY = float(originY) + float(kCellSize) * 0.5f;
    // The disk is the settlement's MASONRY footprint — a function of its
    // population (the walls were built for everyone), never of the street
    // remainder: souls at their hearths do not shrink the town.
    const float populationRadius =
        settlement_population_radius(landmark == LandmarkType::City, pop);
    // …and the shape the generator actually built, measured once off the
    // ground. The scalar above stays the FLOOR (an unwalled hamlet has no
    // masonry to measure, and a place whose walls did not reach still holds
    // its guaranteed core).
    const TownShape townShape = measure_town(
        tiles, centerX, centerY,
        std::max(populationRadius * 2.0f, float(kCellSize) * 0.45f),
        populationRadius);

    // Fixed posts as DATA (registry crowd role rows): the first bodies take
    // the rows' types in order — max(min, div ? target/div : 0) each — and
    // the rest roll the place's crowd stripe. No branch by kind.
    int roleQuota[4] = {};
    for (int r = 0; r < int(def.crowdRoleCount); ++r) {
        const LandmarkCrowdRole& role = def.crowdRoles[r];
        roleQuota[r] = std::max(int(role.min),
                                role.div ? target / int(role.div) : 0);
    }
    int roleRow = 0;

    int refused = 0;
    for (int i = 0; i < target; ++i) {
        float fx = 0.0f;
        float fy = 0.0f;
        if (!find_city_spawn_spot(tiles, rng, centerX, centerY,
                                  populationRadius, townShape, fx, fy)) {
            // A soul that found no ground is COUNTED and said below — never
            // dropped silently (CANON S28: no silent truncation anywhere).
            ++refused;
            continue;
        }
        while (roleRow < int(def.crowdRoleCount) && roleQuota[roleRow] <= 0) {
            ++roleRow;
        }
        NPCType type = NPCType::Peasant;
        if (roleRow < int(def.crowdRoleCount)) {
            type = def.crowdRoles[roleRow].npc;
            --roleQuota[roleRow];
        } else {
            std::uint32_t ts = rng.state;
            type = pick_crowd_row(townCtx, ts);
            rng.state = ts;
        }
        // A citizen is DERIVED — he is one unit of this place's population made
        // visible, and nothing about him is remembered above. What is CONTEXT
        // here: which town's faction he wears, and that he lives his errands
        // rather than fighting. His STRENGTH is not context — it is his row.
        // A capital's guard and a hamlet's guard are the same guard; the capital
        // simply fields more of them (CANON.md S12).
        //
        // The loan says which stock he was drawn from, so his death pays the
        // settlement back without anyone asking what kind of body it was: a town
        // cannot be emptied in the subworld while the map still counts everyone
        // as alive. Borrowing and returning are the same row of one table.
        spawn_derived_body(reg,
            BodySpec{
                type, fx, fy, settlementFaction,
                normalize_soldier_level(npc_def(type).baseLevel
                                        + int(rng.next_u32() % 3u)),
                seed ^ (std::uint32_t(i) * 7919u),
                /*combatant*/false},
            /*faceSalt*/std::uint32_t(i) * 7919u,
            BodyLoan::from(MacroStock::Population, populationKey));
    }
    if (refused > 0) {
        const std::string_view id = landmark_def(landmark).id;
        std::fprintf(stderr,
                     "[spawn] WARN %.*s crowd at cell (%d,%d): %d of %d "
                     "bodies found no ground\n",
                     int(id.size()), id.data(),
                     int(populationKey.cellX), int(populationKey.cellY),
                     refused, target);
    }
}

} // namespace

// Folded to (-n/2, n/2]. Matches the wrap semantics the macro AI uses
// (core/torus.h), so "in the window" here means exactly the same cells the
// seamless manager loads.
int toroidal_cell_offset(int a, int c, int n) {
    if (n <= 0) return a - c;
    int d = ((a - c) % n + n) % n;   // [0, n)
    if (d * 2 > n) d -= n;           // fold to (-n/2, n/2]
    return d;
}

// ── The two forms of birth (declared in spawn.h) ─────────────────────────

entt::entity spawn_derived_body(entt::registry& reg, const BodySpec& body,
                                std::uint32_t faceSalt, const BodyLoan& loan,
                                const BonusTotals* squadBonuses) {
    const entt::entity e =
        emplace_body(reg, body, derive_face(body.seed, body.type, faceSalt),
                     /*healthFraction*/1.0f, squadBonuses);
    // The receipt, stamped by the birth rather than by the caller: borrowing is
    // part of coming into being, not a line a spawner might remember to add.
    // Nothing lent means nothing to stamp — a body drawn from thin air is honest
    // about owing the map nothing.
    if (loan.stock != MacroStock::Count) {
        stamp_macro_debt(reg, e, loan.stock, loan.key, 1);
    }
    return e;
}

// ── WHAT A TRACKED BODY DOES NOT OWN ────────────────────────────────────
//
// The same list, now answering the opposite question — and that inversion IS
// the landing (owner's form 2026-09-12, «ЗЕРКАЛО ДЛЯ ВСЕХ»).
//
// It used to name what got COPIED across the seam, and the copy was the bug:
// a lord you stripped and looted underground climbed out dressed, because his
// belongings down here were a duplicate nobody read back. A duplicate cannot be
// fixed by remembering to fold it up — the fold-up is what drops fields
// (BodyEquipment was missing from the hand-written run that preceded this very
// list). So the copy is gone: the body carries none of these, and every reader
// asks sub/record.h whose they are.
//
// The list survives because the WITNESS needs it: one place naming the kinds of
// owned state, read by the guard below, so adding a kind adds its guard in the
// same edit.
//
// CharacterSheet is deliberately absent, for the same reason as before: the
// sheet decides a body's bars and its blow, so it is needed BEFORE the entity
// exists and rides in through BodySpec (emplace_body above) — where it is read
// from the record, not rolled.
template <class... Cs>
struct OwnedState {
    static bool none_on(const entt::registry& reg, entt::entity body) {
        return (... && !reg.all_of<Cs>(body));
    }
    static bool any_on(const entt::registry& reg, entt::entity e) {
        return (... || reg.all_of<Cs>(e));
    }
};
// The belongings, the personality, WHAT HE IS WEARING and what he knows.
using TrackedInheritance =
    OwnedState<ecs::NpcInventory, ecs::NpcTraits,
               ecs::BodyEquipment, SpellBook>;

bool tracked_body_owns_nothing(const entt::registry& reg,
                               entt::entity macro, entt::entity body) {
    if (!reg.valid(macro) || !reg.valid(body)) return false;
    // Two halves, and both must hold or the claim is empty: the body carries
    // none of it, AND the record it points at is where it actually lives. A
    // body beside a record that holds nothing either would pass the first half
    // for the wrong reason.
    return TrackedInheritance::none_on(reg, body)
        && TrackedInheritance::any_on(reg, macro);
}

entt::entity spawn_tracked_body(entt::registry& reg, entt::entity macro,
                                float x, float y, std::uint32_t seed,
                                bool combatant) {
    if (macro == entt::null || !reg.valid(macro)) return entt::null;
    if (!reg.all_of<ecs::NPCKind, ecs::Pools, ecs::NpcLevel,
                    ecs::NpcCharacter>(macro)) {
        return entt::null;
    }
    const auto& kind = reg.get<ecs::NPCKind>(macro);
    // A kind that names no row at all is refused; a kind that names one is
    // trackable, whatever it is. The extra refusal that stood here — "not a
    // creature" — died with the second birth: a pack leader is a macro entity
    // like a lord, and his body copies his face and his wounds down the same
    // way (CANON.md S4).
    if (!valid_npc_kind(kind.type)) return entt::null;

    const auto& health = reg.get<ecs::Pools>(macro);
    const float fraction = health.maxHp > 0
        ? std::clamp(float(health.hp) / float(health.maxHp), 0.0f, 1.0f)
        : 1.0f;

    BodySpec body{};
    body.type      = static_cast<NPCType>(kind.type);
    body.x         = x;
    body.y         = y;
    // What it is, whose it is and how senior it is are read from the entity
    // itself — a tracked body has no second opinion about its own identity.
    body.faction   = kind.factionIdx;
    body.level     = normalize_soldier_level(reg.get<ecs::NpcLevel>(macro).value);
    body.seed      = seed;
    body.combatant = combatant;
    // HIMSELF, not a namesake: the record the macro layer keeps (owned by a
    // named lord, derived identically for a transient one) through the one
    // door that answers that question for anybody.
    // ...and he arrives WEARING it. The base sheet alone stood here, so a lord's
    // enchanted ring and his burning haste took no part in his subworld swing —
    // his gear protected him (the damage door read it) but never struck with
    // him. Through the one effective door, like every other read of a body.
    const BonusTotals standing = standing_bonuses_of(reg, macro);
    const CharacterSheet macroSheet =
        effective_sheet(sheet_of(reg, macro), standing);
    body.sheet = &macroSheet;

    const entt::entity e =
        emplace_body(reg, body, reg.get<ecs::NpcCharacter>(macro), fraction);

    // The belongings, the personality, the gear and the book are STATE up
    // there, and they STAY up there: nothing is copied down. The line that
    // stood here copied all four, and the copy is what made a stripped lord
    // climb out dressed — what he owns is read through sub/record.h by whoever
    // asks, and what happens to it down here happens to HIM.
    //
    // A derived body has none of it on purpose either: its loot is rolled from
    // its seed at the moment it dies, which costs a city of five thousand
    // people exactly nothing to carry.
    //
    // The backlink is part of being tracked, not an extra the caller attaches:
    // it is the ADDRESS — of the bars he spends, the bag he carries and the
    // plate he wears. It used to be described as «where the return trip writes»;
    // there is no return trip any more, because there is no copy to return.
    reg.emplace<ecs::MacroOrigin>(e, macro);
    // What stood on him when the numbers above were derived — the comparison
    // the per-tick re-derive is gated on (sub/record.h StandingMirror).
    reg.emplace<StandingMirror>(e, standing);
    return e;
}

bool refresh_body_strike(entt::registry& reg, entt::entity body) {
    if (!reg.valid(body)) return false;
    auto* cache = reg.try_get<StandingMirror>(body);
    if (!cache) return false;                  // not a mirror; nothing to track
    const entt::entity rec = record_of(reg, body);
    if (rec == entt::null) return false;
    // A body with no row has no creature template to project a swing from —
    // the hero husk is exactly that, and his hands are assembled elsewhere
    // (hand_strike_fields), from the same effective sheet.
    const auto* kind = reg.try_get<ecs::NPCKind>(body);
    if (!kind || !valid_npc_kind(kind->type)) return false;
    auto* combat = reg.try_get<ecs::Combat>(body);
    if (!combat) return false;

    // THE GATE. Everything below it is the expensive half (CANON's 0.00196 ms);
    // everything above is the 0.00041 ms the owner accepted paying every tick.
    const BonusTotals now = standing_bonuses_of(reg, rec);
    if (now == cache->totals) return false;
    cache->totals = now;

    const NpcTypeDef& def = npc_def(NPCType(std::uint8_t(kind->type)));
    const CharacterSheet eff = effective_sheet(sheet_of(reg, rec), now);
    // His own clock survives the re-derive: it says how busy the hand is, not
    // how strong it is (ecs::Combat::recoverySteps).
    const std::uint32_t recovering = combat->recoverySteps;
    *combat = combat_from_sheet(eff, def);
    combat->recoverySteps = recovering;
    return true;
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

void maybe_emplace_flying(entt::registry& reg, entt::entity e,
                          const CombatTemplate& combat) {
    if (combat.cruiseM <= 0.0f) return;
    reg.emplace<ecs::Flying>(e);
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

// THE household law (CANON S28): 1–3 souls per hearth, one more in a
// crowded town (≥128 — a full city, not a hamlet). One place, two readers:
// the engine's interior spawn clamps it by the live stock, the street
// spawner subtracts the same shares as its reserve — the partition cannot
// drift because there is nothing to drift between.
int doors_in_cell(const std::vector<Structure>& structures,
                  float originX, float originY) {
    int doors = 0;
    const float x1 = originX + float(kCellSize);
    const float y1 = originY + float(kCellSize);
    for (const Structure& s : structures) {
        if (structure_opens(s.kind) == DungeonRef::None) continue;
        if (structure_opens_top(s.kind)) continue;
        if (!dungeon_kind_row(structure_opens(s.kind)).householdAbove) continue;
        if (s.x < originX || s.x >= x1 || s.y < originY || s.y >= y1) continue;
        ++doors;
    }
    return doors;
}

int interior_household_share(std::uint32_t worldSeed, int cellX, int cellY,
                             std::uint16_t ordinal, int level,
                             int landmarkPop, int doorsInCell,
                             const WorldTime& now) {
    const std::uint32_t dSeed = dungeon_scene_seed(
        worldSeed, cellX, cellY, ordinal, std::int8_t(level));
    // WHO LIVES HERE — a uniform roll over [1, 2·mean − 1]: symmetric about
    // the mean, so the doors of a town hold its people EXACTLY in expectation.
    //
    // The mean is the town's people over the town's DOORS, counted on the
    // ground (doors_in_cell) rather than taken from the count the layout law
    // wished for — a settlement seats about three quarters of the houses it
    // asks for, and sizing hearths from the wish left the remainder homeless.
    // A hamlet of few doors therefore packs them; a town that got all its
    // houses runs at the layout's own figure.
    const int mean = doorsInCell > 0
        ? std::max(1, landmarkPop / doorsInCell)
        : hearth_souls_mean(landmarkPop);
    const int souls = 1 + int((dSeed >> 8) % std::uint32_t(2 * mean - 1));
    // …and HOW MANY OF THEM ARE IN at this hour. The same population, moved
    // by the sun between the street and the hearth (city_layout.h
    // crowd_outdoor_share01) — never created and never destroyed, which is
    // what keeps the partition exact while the town breathes.
    return hearth_indoors_now(souls, dSeed, now);
}

// THE garrison partition (CANON S28; owner's eye 2026-09-11: «снаружи
// больше сотни, внутри десятки на ярус»): the storeys keep pop >>
// crowdInsideShift, split evenly with the remainder to the lower floors;
// the THRONG is outside — Σ over storeys + street == pop, and every term
// re-derives from the LIVE number, so a cleared floor thins the whole
// place the way one organism thins.
int interior_garrison_share(LandmarkType landmark, int landmarkPop,
                            int storeys, int level) {
    if (landmarkPop <= 0 || storeys <= 0 || level < 0 || level >= storeys) {
        return 0;
    }
    const LandmarkDef& def = landmark_def(landmark);
    const int inside = landmarkPop >> def.crowdInsideShift;
    return inside / storeys + (level < inside % storeys ? 1 : 0);
}

// The reserve walk (CANON S28 partition): every soul this cell's doors
// keep behind them — households behind House doors, the garrison behind a
// tower's gate — asked of the same pure laws the engine asks when a door
// is actually opened, over the same inputs (ordinal = the door prop's tag,
// footprint = the prop's own half-extents). WHICH law a door speaks is the
// dungeon kind row's own columns, never a branch here on the kind's name.
int interior_reserve_for_cell(const std::vector<Structure>& structures,
                              LandmarkType landmark,
                              std::uint32_t worldSeed,
                              int cellX, int cellY,
                              float originX, float originY,
                              int landmarkPop, const WorldTime& now) {
    const int doors = doors_in_cell(structures, originX, originY);
    int reserve = 0;
    const float x1 = originX + float(kCellSize);
    const float y1 = originY + float(kCellSize);
    for (const Structure& s : structures) {
        const std::uint8_t opens = structure_opens(s.kind);
        if (opens == DungeonRef::None) continue;
        if (structure_opens_top(s.kind)) continue;
        if (s.x < originX || s.x >= x1 || s.y < originY || s.y >= y1) {
            continue;
        }
        const DungeonKindRow& row = dungeon_kind_row(opens);
        DungeonRef ref{};
        ref.kind = opens;
        ref.ordinal = s.tag;
        ref.footHx = structure_half_x(s);
        ref.footHy = structure_half_y(s);
        if (row.householdAbove) {
            reserve += interior_household_share(worldSeed, cellX, cellY,
                                                s.tag, 0, landmarkPop,
                                                doors, now);
            if (dungeon_has_upper(ref)) {
                reserve += interior_household_share(worldSeed, cellX, cellY,
                                                    s.tag, 1, landmarkPop,
                                                    doors, now);
            }
        } else if (row.placeGarrison
                   && landmark_def(landmark).crowdHabitat != 0) {
            const int storeys = dungeon_storey_count(ref);
            for (int level = 0; level < storeys; ++level) {
                reserve += interior_garrison_share(landmark, landmarkPop,
                                                   storeys, level);
            }
        }
    }
    // A TOWN CANNOT KEEP MORE PEOPLE THAN IT HAS. Household sizes are a roll
    // centred on the mean the house count was derived from (city_layout.h), so
    // their sum tracks the population but scatters about it by a soul or two —
    // and on a night when the scatter lands high, an unclamped reserve would
    // claim more souls than the place owns and leave the street a negative
    // number of people. The street is the REMAINDER, so the remainder is what
    // the clamp protects: street + kept == population, exactly, at every hour.
    //
    // The door-open path pays the same respect from its own side, clamping its
    // household by the LIVE stock — so an emptied town opens on empty houses.
    return std::min(reserve, std::max(0, landmarkPop));
}

namespace {
// Uniform draw WITHOUT replacement over the scene's floor catalog (partial
// Fisher-Yates): body k stands on order[k % n]. While free floor remains no
// two draws share a tile; past the catalog's size the draw wraps — bodies
// share tiles rather than vanish (CANON S28: no silent ceilings; the caller
// says the shortage out loud). The catalog's points are standable by
// construction (dgn/dispatch.cpp folds trav while it is alive), so there is
// no rejection here at all — the guessing that dropped bodies against a
// tile byte 24 tries at a time is gone.
struct FloorDraw {
    std::vector<std::uint32_t> order;
    std::size_t drawn = 0;
    explicit FloorDraw(std::size_t n) : order(n) {
        for (std::uint32_t i = 0; i < std::uint32_t(n); ++i) order[i] = i;
    }
    const StandPoint& next(const std::vector<StandPoint>& catalog, Rng& rng) {
        const std::size_t n = order.size();
        const std::size_t slot = drawn % n;
        if (drawn < n && slot + 1 < n) {
            const std::size_t j =
                slot + std::size_t(rng.next_u32() % std::uint32_t(n - slot));
            std::swap(order[slot], order[j]);
        }
        ++drawn;
        return catalog[order[slot]];
    }
};
} // namespace

int spawn_dungeon_residents(ecs::World& w,
                            std::uint32_t seed,
                            std::uint16_t settlementFaction,
                            LandmarkType landmark,
                            std::uint8_t danger,
                            std::uint8_t depositsNear,
                            int count,
                            const std::vector<StandPoint>& floorCatalog,
                            float originX, float originY,
                            MacroStockKey populationKey,
                            bool combatant) {
    if (count <= 0) return 0;
    if (floorCatalog.empty()) {
        std::fprintf(stderr,
                     "[spawn] WARN interior of landmark %d: empty floor "
                     "catalog — %d residents refused\n",
                     int(populationKey.subject), count);
        return 0;
    }
    Rng rng(seed ^ 0xD0E51DE7u);
    FloorDraw draw(floorCatalog.size());
    int placed = 0;
    for (int i = 0; i < count; ++i) {
        const StandPoint& pt = draw.next(floorCatalog, rng);
        const float fx = originX + float(pt.x) + 0.5f;
        const float fy = originY + float(pt.y) + 0.5f;
        SpawnContext townCtx{};
        // The household rolls its OWN place's crowd stripe — the landmark
        // kind travels in from the door (§42: the hardcoded City that stood
        // here dressed every interior in the world as a town house).
        townCtx.landmark = landmark;
        townCtx.danger = danger;
        townCtx.depositsNear = depositsNear;
        std::uint32_t ts = rng.state;
        const NPCType type = pick_crowd_row(townCtx, ts);
        rng.state = ts;
        // The same derived-citizen birth as the street (one row of one law):
        // level from his own row, loan from the SAME population stock — a death
        // in here pays the town back exactly like a death on the square.
        // Whether it fights is CONTEXT: a hearth's family flees, a garrisoned
        // storey stands its ground.
        spawn_derived_body(w.reg,
            BodySpec{
                type, fx, fy, settlementFaction,
                normalize_soldier_level(npc_def(type).baseLevel
                                        + int(rng.next_u32() % 3u)),
                seed ^ (std::uint32_t(i) * 7919u),
                combatant},
            /*faceSalt*/std::uint32_t(i) * 7919u,
            BodyLoan::from(MacroStock::Population, populationKey));
        ++placed;
    }
    return placed;
}

int spawn_dungeon_vermin(ecs::World& w,
                         std::uint32_t seed,
                         LandmarkType tableKind,
                         std::uint8_t danger,
                         Biome biome,
                         int treeCount,
                         int budget,
                         const std::vector<StandPoint>& floorCatalog,
                         float originX, float originY,
                         MacroStockKey faunaKey) {
    if (budget <= 0) return 0;
    if (floorCatalog.empty()) {
        std::fprintf(stderr,
                     "[spawn] WARN den at cell (%d,%d): empty floor catalog "
                     "— %d heads refused\n",
                     int(faunaKey.cellX), int(faunaKey.cellY), budget);
        return 0;
    }
    // The same ONE law the open cell runs (fauna.h roll_spawns): habitat ×
    // danger-match over the body table; the den kind is the habitat bit.
    SpawnContext sctx{};
    sctx.biome = biome;
    sctx.forest = is_forest_cell(treeCount);
    sctx.landmark = tableKind;
    sctx.danger = danger;
    std::uint32_t rngState = seed ^ 0xCE11A5u;
    auto picks = roll_spawns(sctx, rngState);
    if (picks.empty()) return 0;

    Rng pos(rngState);
    auto& reg = w.reg;
    FloorDraw draw(floorCatalog.size());
    int placed = 0;
    for (const auto& p : picks) {
        if (placed >= budget) break;
        const FaunaEntry& f = *p.entry;
        const StandPoint& pt = draw.next(floorCatalog, pos);
        const float fx = originX + float(pt.x) + 0.5f;
        const float fy = originY + float(pt.y) + 0.5f;
        const int npcLevel = normalize_soldier_level(
            int(f.baseLevel) + int(std::floor(pos.next_f01() * 2.0f)));
        spawn_derived_body(reg,
            BodySpec{f.type, fx, fy,
                     std::uint16_t(faction_index(p.factionId)), npcLevel,
                     seed ^ (std::uint32_t(placed) * 2654435761u),
                     /*combatant*/false},
            /*faceSalt*/std::uint32_t(placed) * 7919u,
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
            if (reg.any_of<ecs::PlayerSoldierTag, ecs::AvatarTag>(e)) continue;
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
                     LandmarkType landmark,
                     std::uint8_t danger,
                     std::uint8_t depositsNear,
                     const SeamlessSubworldManager& mgr,
                     int ox,
                     int oy,
                     std::uint32_t cellSeed,
                     std::uint32_t worldSeed,
                     std::uint16_t settlementFaction,
                     int landmarkPop,
                     int landmarkSubjectId,
                     int macroCellX,
                     int macroCellY,
                     int faunaCount,
                     const SoldierSquad* garrison,
                     const WorldTime& now) {
    auto& reg = w.reg;
    const int originX = (ox + 1) * kCellSize;
    const int originY = (oy + 1) * kCellSize;

    // Context decides WHO and HOW MANY stand on this cell — never what they are
    // worth. Each of the 3×3 cells populates on its own macro terms (a city cell
    // fills with citizens even when it is not the centre — which is what stops a
    // city from vanishing when you step one cell out), and every body that stands
    // up is exactly its table row. The two markups that used to live here — the
    // settlement's √(pop/100) level bonus and the danger zone's +1 level with a
    // 1+0.18·(z−2) hp/damage multiplier — were a hidden auto-level: they made the
    // same guard stronger for standing in a bigger town and the same wolf tougher
    // for standing in a redder province. Deleted 2026-08-20 (CANON.md S12); when
    // the zone is meant to matter it must weight the TABLE, not the body.
    SpawnContext townCtx{};
    townCtx.biome = biome;
    townCtx.forest = is_forest_cell(treeCount);
    townCtx.landmark = landmark;
    townCtx.danger = danger;
    townCtx.depositsNear = depositsNear;
    spawn_landmark_population(w, townCtx, landmark, mgr, cellSeed, worldSeed,
                              settlementFaction,
                              landmarkPop, originX, originY,
                              MacroStockKey{landmarkSubjectId,
                                            std::int16_t(macroCellX),
                                            std::int16_t(macroCellY)},
                              now);

    // THE PLACE'S STANDING ARMY on its streets (§42 Инк 7): every garrison
    // record at home embodies as a FIGHTING body of its own row and level,
    // under the place's banner, with the Garrison loan — killed on the wall
    // = struck from the roll through THE one settle door; out on patrol or
    // hired away = not in this roster = not on this street. Garrison souls
    // were paid out of the population at recruitment, so they stand BESIDE
    // the crowd's partition, never inside it.
    if (garrison && garrison->size() > 0 && landmarkSubjectId >= 0) {
        Rng grng(cellSeed ^ 0x6A121501u);
        const float centerX = float(originX) + float(kCellSize) * 0.5f;
        const float centerY = float(originY) + float(kCellSize) * 0.5f;
        const float radius = settlement_population_radius(
            landmark == LandmarkType::City, landmarkPop);
        const auto& tiles = mgr.tiles();
        // The garrison stands in the same town its citizens do — same shape,
        // measured the same way, so the wall's defenders are not confined to
        // a disk inside a town that is not one.
        const TownShape townShape = measure_town(
            tiles, centerX, centerY,
            std::max(radius * 2.0f, float(kCellSize) * 0.45f), radius);
        // HALF THE WATCH STANDS IN THE UPPER QUARTER (owner, 2026-09-13:
        // «стража везде, просто в верхнем квартале её больше, например
        // половина»). The quarter is about a tenth of the town's ground, so
        // half the garrison on it is a watch several times as thick as the
        // streets below — which is what a lord's own quarter looks like, and
        // it falls out of one number rather than a density dial.
        //
        // The quarter is FOUND, not handed over: its keep is the tallest roof
        // the generator raised, and its size is a pure function of the
        // population the generator used (city_layout.h city_upper_radius). So
        // the spawner needs no channel to the generator to know where the
        // lord's district is — the same way it measures the town's shape off
        // the ground rather than trusting a scalar.
        float keepX = centerX, keepY = centerY;
        bool haveKeep = false;
        if (landmark == LandmarkType::City) {
            const Structure* keep = nullptr;
            for (const Structure& st : mgr.structures()) {
                if (st.kind != Structure::House) continue;
                if (st.x < float(originX) || st.x >= float(originX + kCellSize)) continue;
                if (st.y < float(originY) || st.y >= float(originY + kCellSize)) continue;
                if (keep == nullptr || st.height > keep->height) keep = &st;
            }
            if (keep != nullptr) {
                keepX = keep->x;
                keepY = keep->y;
                haveKeep = true;
            }
        }
        const float quarterR = city_upper_radius(landmarkPop);
        int refused = 0;
        for (int i = 0; i < garrison->size(); ++i) {
            const SoldierRecord& rec = (*garrison)[i];
            if (!valid_npc_kind(rec.kind)) continue;
            // Every other man of the roll takes the quarter, so the split is
            // exact for any roster size and needs no second roll to decide it.
            const bool inQuarter = haveKeep && (i % 2) == 0;
            float fx = 0.0f, fy = 0.0f;
            // A man who cannot stand in the quarter still stands somewhere:
            // the district is small and dense, and when it is full the rest of
            // the watch takes the streets below. Measured on a city of 5 488:
            // 104 of a wanted 392 fit, and eight further tries each moved the
            // number by nothing — the quarter is FULL, not unlucky.
            const bool stood = inQuarter
                && find_city_spawn_spot(tiles, grng, keepX, keepY,
                                        quarterR, townShape, fx, fy);
            if (stood) {
                // stood in the quarter
            } else if (!find_city_spawn_spot(tiles, grng, centerX, centerY,
                                             radius, townShape, fx, fy)) {
                ++refused;
                continue;
            }
            spawn_derived_body(reg,
                BodySpec{
                    static_cast<NPCType>(rec.kind), fx, fy,
                    settlementFaction,
                    normalize_soldier_level(rec.level),
                    cellSeed ^ (rec.entityId * 2654435761u),
                    /*combatant*/true},
                /*faceSalt*/rec.entityId * 7919u,
                BodyLoan::from(MacroStock::Garrison,
                               MacroStockKey{landmarkSubjectId,
                                             std::int16_t(macroCellX),
                                             std::int16_t(macroCellY),
                                             std::int32_t(rec.entityId)}));
        }
        if (refused > 0) {
            std::fprintf(stderr,
                         "[spawn] WARN garrison of landmark %d: %d of %d "
                         "soldiers found no ground\n",
                         landmarkSubjectId, refused, garrison->size());
        }
    }

    // THE spawn law (fauna.h): the danger byte weights the TABLE — who is
    // rolled — never the body after the pick (S12; the negative control in
    // subworld_spawn_parity_test keeps the autolevel dead).
    SpawnContext sctx{};
    sctx.biome = biome;
    sctx.forest = is_forest_cell(treeCount);
    sctx.landmark = landmark;
    sctx.danger = danger;
    std::uint32_t rngState = cellSeed ^ 0xFAEAu;
    auto picks = roll_spawns(sctx, rngState);
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
    int refused = 0;
    for (const auto& p : picks) {
        if (budget <= 0) break;
        const FaunaEntry& f = *p.entry;
        // Through THE placement door: the cell's own uniform scatter is the
        // ground's point law, dodging water. Same RNG stream and attempt
        // count as before the door — bit for bit.
        float fx = 0.0f, fy = 0.0f;
        const bool placed = resolve_stand(
            20,
            [&](float& sx, float& sy) {
                sx = float(originX) + pos.next_f01() * float(kCellSize);
                sy = float(originY) + pos.next_f01() * float(kCellSize);
            },
            [&](float sx, float sy) {
                const int ix = int(sx), iy = int(sy);
                if (ix < 0 || ix >= kFullSize || iy < 0 || iy >= kFullSize) {
                    return false;
                }
                return !(tilesUsable &&
                         tiles[std::size_t(iy) * kFullSize + ix]
                             == TILE_WATER);
            },
            fx, fy);
        if (!placed) { ++refused; continue; }

        const int npcLevel = normalize_soldier_level(
            int(f.baseLevel) + int(std::floor(pos.next_f01() * 2.0f)));
        spawn_derived_body(reg,
            BodySpec{f.type, fx, fy,
                     std::uint16_t(faction_index(p.factionId)), npcLevel,
                     cellSeed ^ (std::uint32_t(budget) * 2654435761u),
                     /*combatant*/false},
            /*faceSalt*/std::uint32_t(budget) * 7919u, faunaLoan);
        --budget;
    }
    if (refused > 0) {
        std::fprintf(stderr,
                     "[spawn] WARN fauna at cell (%d,%d): %d heads found no "
                     "dry ground\n",
                     macroCellX, macroCellY, refused);
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
            if (reg.any_of<ecs::PlayerSoldierTag, ecs::AvatarTag>(e)) continue;
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
                        const BonusTotals* squadBonuses,
                        std::int32_t rosterSubject,
                        std::int16_t rosterCx,
                        std::int16_t rosterCy) {
    spawn_player_squad(w, squad, mgr.tiles(), playerX, playerY, seed, faction,
                       squadBonuses, rosterSubject, rosterCx, rosterCy);
}

void spawn_player_squad(ecs::World& w,
                        const SoldierSquad& squad,
                        const std::vector<std::uint8_t>& tiles,
                        float playerX,
                        float playerY,
                        std::uint32_t seed,
                        std::uint16_t faction,
                        const BonusTotals* squadBonuses,
                        std::int32_t rosterSubject,
                        std::int16_t rosterCx,
                        std::int16_t rosterCy) {
    if (squad.empty()) return;

    auto& reg = w.reg;
    Rng rng(seed ^ 0x51AD5A11u);
    constexpr float kPi = 3.1415926535f;
    constexpr float kTau = kPi * 2.0f;
    const int count = std::max(1, squad.size());
    const bool tilesUsable =
        tiles.size() >= std::size_t(kFullSize) * std::size_t(kFullSize);

    for (int i = 0; i < count; ++i) {
        const SoldierRecord& soldier = squad[i];
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
        // A soldier is DERIVED: the roster line says WHO stands here, the
        // seed says everything else — and he is LENT like any lord's man
        // (§42 Инк 6, «игрок не особен»): the receipt names the OWNING
        // squad's MacroSpawnId (the flag record's — a worn lord's men are
        // HIS stock, A2 2026-09-17) and this member's entityId, so his death
        // strikes the roster through THE one settle door
        // (macro_stock.cpp "roster"), exactly as every other army pays.
        const auto e = spawn_derived_body(reg,
            BodySpec{
                type, fx, fy, faction, level,
                (std::uint32_t(i) * 2654435761u)
                    ^ (std::uint32_t(soldier.kind) << 8)
                    ^ std::uint32_t(level),
                /*combatant*/true},
            /*faceSalt*/std::uint32_t(i) * 2654435761u,
            BodyLoan::from(MacroStock::Roster,
                           MacroStockKey{
                               rosterSubject, rosterCx, rosterCy,
                               std::int32_t(soldier.entityId)}),
            squadBonuses);
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
                                     std::uint32_t seed,
                                     const StructureIndex* solids) {
    return project_macro_npcs_into_subworld(w, mgr.tiles(), centerCx, centerCy,
                                            mapW, mapH, seed, solids);
}

int project_macro_npcs_into_subworld(ecs::World& w,
                                     const std::vector<std::uint8_t>& tiles,
                                     int centerCx, int centerCy,
                                     int mapW, int mapH,
                                     std::uint32_t seed,
                                     const StructureIndex* solids) {
    auto& reg = w.reg;
    const bool tilesUsable =
        tiles.size() >= std::size_t(kFullSize) * std::size_t(kFullSize);

    // Snapshot the source set FIRST. Projecting a body emplaces into the very
    // component pools this view iterates (Position / NPCKind / Health / …),
    // which can reallocate and invalidate a live view iterator mid-loop. So we
    // collect the persistent macro NPCs, then create their projections.
    // MacroNpcRuntime is the macro discriminator (subworld bodies never have it);
    // excluding SubworldTag/Dead keeps the source set to live overworld NPCs.
    // PlayerTag skips the macro record the player is currently BEING — his
    // body in the scene is the tracked avatar, not a foreign projection.
    // The player's OWN squad is NOT excluded any more (вердикт владельца №6,
    // 2026-09-17): while he wears somebody else, his abandoned party stands
    // in the world — so standing on its cell it projects like any other
    // party, visibly and senselessly (the ai door reads «флажка на мне нет»
    // through the mirror). The old writeback fear died with the mirror law:
    // a projected body owns nothing, it reads and writes THE record. While
    // he is HIMSELF the squad carries PlayerTag, so nothing double-projects.
    std::vector<entt::entity> sources;
    {
        auto view = reg.view<ecs::MacroNpcRuntime, ecs::MacroCell, ecs::NPCKind,
                             ecs::Pools, ecs::NpcLevel, ecs::NpcCharacter>(
            entt::exclude<ecs::Dead, ecs::PlayerTag>);
        for (auto macro : view) sources.push_back(macro);
    }

    // Idempotence (SUB-2): a macro NPC whose projection ALREADY stands in the
    // scene is not projected twice. This is what lets a seam crossing call
    // this door again for the freshly-entered cells — before, projection ran
    // ONCE at enter() while the window reaper honestly despawned any lord
    // whose cell slid out, so stepping one cell away lost him until a full
    // leave/enter. His wounds are safe across that despawn: the per-tick
    // write-back (reconcile_tracked_bodies_to_macro) has already paid the
    // fraction up before any reap can run.
    std::vector<entt::entity> alreadyProjected;
    for (auto [body, origin] :
         reg.view<ecs::MacroOrigin, ecs::SubworldTag>().each()) {
        (void)body;
        alreadyProjected.push_back(origin.macro);
    }

    int projected = 0;
    for (const entt::entity macro : sources) {
        const auto& mcell = reg.get<ecs::MacroCell>(macro);
        const int mcx = ecs::cell_x(mcell, mapW);
        const int mcy = ecs::cell_y(mcell, mapW);
        // Which of the 3×3 window cells does this macro NPC occupy (if any)?
        const int ox = toroidal_cell_offset(mcx, centerCx, mapW);
        const int oy = toroidal_cell_offset(mcy, centerCy, mapH);
        if (ox < -1 || ox > 1 || oy < -1 || oy > 1) continue;
        if (std::find(alreadyProjected.begin(), alreadyProjected.end(), macro)
            != alreadyProjected.end()) continue;

        // (The projection cap that stood here — kMaxProjectedMacroNpcs, 128
        // for the whole scene — died with §42 Инк 6, owner: «потолков нет».
        // Every macro body standing in the window walks in; the one physical
        // bound is the crowd grid, and it already shouts when it binds.)
        const auto& kind = reg.get<ecs::NPCKind>(macro);

        // Deterministic per-(cell, type, index) stream: the same overworld state
        // reprojects identically, yet two same-type NPCs in one cell still differ
        // (their integer coords or the running index diverge the salt).
        const std::uint32_t salt =
            cell_seed(0u, mcx, mcy) ^
            (std::uint32_t(kind.type) << 11) ^
            (std::uint32_t(projected) * 2654435761u);
        Rng rng(seed ^ salt);

        // Entry-side scatter within this window cell's sub-region
        // (macro/entry_context.h): the band starts at the edge this NPC walked
        // in from and deepens with its time in the macro cell, so a party that
        // just chased somebody across the border materialises AT that border,
        // behind them — while a local that has been here forever gets the full
        // uniform cell (the band formula degrades to exactly the old scatter).
        // Water is dodged per attempt like the fauna path (spawn_cell_npcs) —
        // but what is dodged is WET FOOTING, not a wet tile: a body may
        // stand on whatever would carry it above the water plane, so the
        // deck of a bridge is a perfectly good place to meet a caravan
        // (sub/height.h is_dry_footing). Falls back to the cell centre if 20
        // tries all land in water (never lose the NPC).
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
                tiles[std::size_t(iy) * kFullSize + ix] == TILE_WATER
                && !carried_above_water(solids, tx, ty)) {
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

        // The leader's buff, read from the leader's OWN sheet (character_sheet.h squad_bonuses)
        // — collected once per squad and applied into every member's sheet at
        // birth. A generic leader's derived sheet carries no bonus sources
        // today (no perks), so this collects empty and changes nothing; a
        // hand-authored or persistent leader with a Leader-class perk buffs
        // its troops through this same line with no further change anywhere.
        BonusTotals leaderBonuses{};
        if (const auto* leaderSheet = reg.try_get<CharacterSheet>(leaderBody)) {
            leaderBonuses = squad_bonuses(*leaderSheet);
        }

        // The leader's troops. Each roster row is one unit of the squad's
        // roster STOCK made visible: a DERIVED body — the row says WHO stands
        // here, the seed says everything else — wearing the OWNER's faction
        // (the banner rule, spawn.h) and carrying the receipt that pays its
        // death back into the roster (macro/macro_stock.h "roster": subject =
        // the squad's MacroSpawnId ordinal, detail = this member's entityId).
        // Placed on a tight ring around the leader, dodging water like every
        // other placement here; a member that finds no land stands ON the
        // leader's spot rather than being lost. The whole roster walks in —
        // no ceiling (§42 Инк 6): an army of hundreds meets you as hundreds.
        if (const auto* roster = reg.try_get<ecs::SquadRoster>(macro)) {
            const auto* sid = reg.try_get<ecs::MacroSpawnId>(macro);
            constexpr float kTau = 6.2831853f;
            const int memberCount = int(roster->squad.size());
            for (int m = 0; m < memberCount; ++m) {
                const SoldierRecord& rec = roster->squad[std::size_t(m)];
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
                                        std::int16_t(mcx),
                                        std::int16_t(mcy),
                                        std::int32_t(rec.entityId)})
                    : BodyLoan::none();
                // ONE birth for every member — the sheet-less second birth is
                // dead: man or beast, the row and level project a sheet through
                // spawn_derived_body (leader's bonuses applied in it), and every
                // body carries the same roster receipt, so a wolf's death pays
                // the pack back exactly like a spearman's.
                spawn_derived_body(reg,
                    BodySpec{
                        static_cast<NPCType>(rec.kind), mfx, mfy,
                        kind.factionIdx,
                        normalize_soldier_level(rec.level),
                        ((seed ^ salt) + std::uint32_t(m) * 2654435761u)
                            ^ (rec.entityId << 7),
                        /*combatant*/true},
                    /*faceSalt*/std::uint32_t(m) * 2654435761u ^ 0x9E3779B9u,
                    loan, &leaderBonuses);
                ++projected;
            }
        }
    }
    return projected;
}

// (Вселение = перенос флажка живёт в sub/possess.h — header-only, чтобы
// эффект спелла possession не линковал слой спавна.)

} // namespace sm::sub

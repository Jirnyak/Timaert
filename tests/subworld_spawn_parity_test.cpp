#include "check.h"
#include "macro/faction.h"
#include "sub/spawn.h"
#include "core/rng.h"
#include "ecs/components.h"
#include "macro/npc.h"
#include "macro/player_entity.h"
#include "sub/base_generator.h"
#include "sub/map_factory.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

struct SpawnRecord {
    std::uint16_t type = 0;
    std::uint16_t faction = 0;
    float x = 0.0f;
    float y = 0.0f;
    float hp = 0.0f;
    float maxHp = 0.0f;
    float damage = 0.0f;
    float speed = 0.0f;
    float range = 0.0f;
    float cooldown = 0.0f;
    float radius = 0.0f;
    std::int16_t level = 0;
    sm::ecs::SubworldAi::Kind ai = sm::ecs::SubworldAi::Wander;
    sm::ecs::Combat::Kind kind = sm::ecs::Combat::Melee;
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
};

int fail(const char* msg) {
    // Testing law #1: the verdict lives in the ONE check.h counter — the
    // returned int is vestigial and IGNORED; main ends with report().
    sm::test::check(false, msg, "tests/subworld_spawn_parity_test.cpp", 0);
    return 1;
}

// Canonical monster id: (0x100 | stable catalog index), mirroring the shipping
// spawn path (src/sub/spawn.cpp + src/sub/engine.cpp) and every design doc. The
// TS authority assigns fauna NO numeric id — identity is label + table row — so
// the uint16 `NPCKind.type` is a C++/ECS-only encoding of the creature's stable
// catalog row, never a label hash.
std::uint16_t catalog_type_id(const sm::FaunaEntry* entry) {
    const int idx = sm::creature_index(entry);
    return std::uint16_t(idx < 0 ? 0 : idx);
}

// The stance a row folds to is not restated here — it is asked of the SAME
// door the spawner uses (sub/spawn.h), so this cannot drift from it and cannot
// pass by copying it. What the case still proves is that the body ACTUALLY
// carries the stance its row implies.
sm::ecs::SubworldAi::Kind ai_kind(sm::AIBehaviour ai) {
    return sm::sub::subworld_ai_for(ai);
}

sm::ecs::Combat::Kind combat_kind(const sm::CombatTemplate& combat) {
    return combat.attackKind == sm::CombatTemplate::Missile
        ? sm::ecs::Combat::Missile
        : sm::ecs::Combat::Melee;
}

bool less_spawn_record(const SpawnRecord& a, const SpawnRecord& b) {
    if (a.x != b.x) return a.x < b.x;
    if (a.y != b.y) return a.y < b.y;
    return a.type < b.type;
}

bool near(float a, float b, float eps = 0.001f) {
    return std::fabs(a - b) <= eps;
}

sm::sub::CellContext meadow_cell(int cx, int cy) {
    sm::sub::CellContext c{};
    c.cx = cx;
    c.cy = cy;
    c.macroHeight = 0.62f;
    c.biome = sm::Biome::Meadow;
    c.feature = sm::FT_None;
    c.landmark.id = -1;
    c.landmark.size = 0;
    c.landmark.kind = sm::LandmarkType::None;
    c.seed = 0x24680000u
        ^ (std::uint32_t(cx) * 73856093u)
        ^ (std::uint32_t(cy) * 19349663u);
    return c;
}

// TS-derived expectation for ONE window cell at offset (ox,oy). Mirrors
// spawn_cell_npcs exactly: same table roll from `seed`, same 20-try water dodge,
// but scattered only within that cell's sub-region [(ox+1)*kCellSize .. +
// kCellSize)² — the change that keeps every cell's fauna in its own cell.
std::vector<SpawnRecord> expected_cell_fauna(
    const sm::sub::SeamlessSubworldManager& mgr,
    sm::Biome biome,
    sm::LandmarkType landmark,
    std::uint8_t danger,
    int ox,
    int oy,
    std::uint32_t seed,
    int landmarkPop) {

    const int originX = (ox + 1) * sm::sub::kCellSize;
    const int originY = (oy + 1) * sm::sub::kCellSize;

    // The roll comes from THE shipping law (fauna.h roll_spawns), not a
    // replica of it — what this mirror owns is the placement half: WHERE the
    // picks stand and in what order the stream hands them out.
    sm::SpawnContext sctx{};
    sctx.biome = biome;
    sctx.forest = false;   // this fixture rolls open meadow
    sctx.landmark = landmark;
    sctx.danger = danger;
    std::uint32_t rngState = seed ^ 0xFAEAu;
    const std::vector<sm::FaunaPick> picks = sm::roll_spawns(sctx, rngState);

    // No context markup: a creature is its ROW and its own level (CANON.md S12).
    // The settlement's √(pop/100) bonus and the zone's +level / ×1.18 hp+damage
    // died 2026-08-20 — and `landmarkPop` stays a parameter precisely so this
    // mirror keeps proving it changes NOTHING about a body.
    (void)landmarkPop;

    const auto& tiles = mgr.tiles();
    const bool tilesUsable =
        tiles.size() >= std::size_t(sm::sub::kFullSize) * sm::sub::kFullSize;

    sm::Rng rng(rngState);
    std::vector<SpawnRecord> out;
    out.reserve(picks.size());

    for (const sm::FaunaPick& pick : picks) {
        const sm::FaunaEntry& f = *pick.entry;
        float fx = 0.0f;
        float fy = 0.0f;
        bool placed = false;
        for (int attempt = 0; attempt < 20; ++attempt) {
            fx = float(originX) + rng.next_f01() * float(sm::sub::kCellSize);
            fy = float(originY) + rng.next_f01() * float(sm::sub::kCellSize);
            const int ix = int(fx);
            const int iy = int(fy);
            if (ix < 0 || ix >= sm::sub::kFullSize
                || iy < 0 || iy >= sm::sub::kFullSize) {
                continue;
            }
            if (tilesUsable
                && tiles[std::size_t(iy) * sm::sub::kFullSize + ix]
                    == sm::sub::TILE_WATER) {
                continue;
            }
            placed = true;
            break;
        }
        if (!placed) continue;

        const int level = sm::normalize_soldier_level(
            int(f.baseLevel) + int(std::floor(rng.next_f01() * 2.0f)));
        // NOT restated here any more. A body's hp and damage are its row
        // PROJECTED THROUGH ITS SHEET since the two births merged (2026-08-20,
        // CANON.md S14: one sheet for everything alive), and a test that
        // recomputed that projection would be testing that it can copy the
        // spawner. What this mirror still owns is the part it was written for:
        // WHICH creatures the roll produces, WHERE they stand, and in what
        // order the RNG stream hands them out. The strength of a body is
        // asserted as a PROPERTY below instead (`sheet_lifts_every_body`).
        const float hp = 0.0f;
        const float damage = 0.0f;

        SpawnRecord r{};
        r.type = catalog_type_id(pick.entry);
        r.faction = std::uint16_t(sm::faction_index(pick.factionId));
        r.x = fx;
        r.y = fy;
        r.hp = hp;
        r.maxHp = hp;
        r.damage = damage;
        r.speed = f.combat.speed;
        r.range = f.combat.attackRange;
        r.cooldown = f.combat.cooldown;
        r.radius = f.radius;
        r.level = std::int16_t(level);
        r.ai = ai_kind(f.ai);
        r.kind = combat_kind(f.combat);
        // The tint is the row's PICTURE, not a number restated here: a creature
        // and its sprite row are bound through THE sprite table, so this asserts
        // the binding holds rather than that someone can copy a literal.
        const std::uint32_t tint = sm::sprite_row(f.sprite).tint;
        r.r = std::uint8_t((tint >> 16) & 0xFFu);
        r.g = std::uint8_t((tint >> 8) & 0xFFu);
        r.b = std::uint8_t(tint & 0xFFu);
        out.push_back(r);
    }

    std::sort(out.begin(), out.end(), less_spawn_record);
    return out;
}

std::vector<SpawnRecord> actual_fauna(sm::ecs::World& world) {
    std::vector<SpawnRecord> out;
    auto view = world.reg.view<sm::ecs::SubworldTag, sm::ecs::NPCKind,
                               sm::ecs::Position, sm::ecs::Health,
                               sm::ecs::Combat, sm::ecs::NpcLevel,
                               sm::ecs::SubworldAi, sm::ecs::Sprite>();
    for (auto e : view) {
        const auto& kind = view.get<sm::ecs::NPCKind>(e);
        const auto& pos = view.get<sm::ecs::Position>(e);
        const auto& hp = view.get<sm::ecs::Health>(e);
        const auto& combat = view.get<sm::ecs::Combat>(e);
        const auto& level = view.get<sm::ecs::NpcLevel>(e);
        const auto& ai = view.get<sm::ecs::SubworldAi>(e);
        const auto& sprite = view.get<sm::ecs::Sprite>(e);

        SpawnRecord r{};
        r.type = kind.type;
        r.faction = kind.factionIdx;
        r.x = pos.x;
        r.y = pos.y;
        r.hp = hp.hp;
        r.maxHp = hp.maxHp;
        r.damage = combat.damage;
        r.speed = combat.speed;
        r.range = combat.attackRange;
        r.cooldown = combat.cooldown;
        r.radius = ai.radius;
        r.level = level.value;
        r.ai = ai.kind;
        r.kind = combat.kind;
        r.r = sprite.r;
        r.g = sprite.g;
        r.b = sprite.b;
        out.push_back(r);
    }
    std::sort(out.begin(), out.end(), less_spawn_record);
    return out;
}

bool records_match(const SpawnRecord& e, const SpawnRecord& a) {
    return e.type == a.type && e.faction == a.faction && e.level == a.level
        && e.ai == a.ai && e.kind == a.kind
        && e.r == a.r && e.g == a.g && e.b == a.b
        && near(e.x, a.x) && near(e.y, a.y)
        && near(e.speed, a.speed)
        && near(e.range, a.range) && near(e.cooldown, a.cooldown)
        && near(e.radius, a.radius);
}

int compare_records(const std::vector<SpawnRecord>& expected,
                    const std::vector<SpawnRecord>& actual) {
    if (expected.size() != actual.size()) {
        std::fprintf(stderr, "expected=%zu actual=%zu\n",
                     expected.size(), actual.size());
        return fail("fauna count differs from TS-derived roll");
    }

    for (std::size_t i = 0; i < expected.size(); ++i) {
        const SpawnRecord& e = expected[i];
        const SpawnRecord& a = actual[i];
        if (!records_match(e, a)) {
            std::fprintf(
                stderr,
                "idx=%zu expected type=%u faction=%u level=%d pos=%.3f,%.3f "
                "hp=%.3f dmg=%.3f ai=%d rgb=%u,%u,%u actual type=%u "
                "faction=%u level=%d pos=%.3f,%.3f hp=%.3f dmg=%.3f "
                "ai=%d rgb=%u,%u,%u\n",
                i, e.type, e.faction, int(e.level), e.x, e.y, e.hp,
                e.damage, int(e.ai), e.r, e.g, e.b,
                a.type, a.faction, int(a.level), a.x, a.y, a.hp,
                a.damage, int(a.ai), a.r, a.g, a.b);
            return fail("fauna ECS record differs from TS-derived expected");
        }
    }
    return 0;
}

// Spawn one window cell (offset ox,oy) whose ABSOLUTE macro coordinate is
// (absCx,absCy) — the seed is taken from that absolute coord so a crossing that
// re-maps the same window offset to a different macro cell reproduces the right
// creatures, exactly like SubworldEngine::spawn_cell resolving center+offset.
void spawn_cell_at(sm::ecs::World& world,
                   const sm::sub::SeamlessSubworldManager& mgr,
                   int ox, int oy, int absCx, int absCy) {
    const sm::sub::CellContext c = meadow_cell(absCx, absCy);
    sm::sub::spawn_cell_npcs(world, c.biome, c.feature,
                             sm::LandmarkType::None, /*danger*/0, /*depositsNear*/0, mgr,
                             ox, oy, c.seed,
                             std::uint16_t(sm::faction_index("empire")),
                             0);
}

// Fill all nine window cells for a manager centred on macro (0,0): window offset
// (ox,oy) IS absolute cell (ox,oy) here.
void spawn_all_cells(sm::ecs::World& world,
                     const sm::sub::SeamlessSubworldManager& mgr) {
    for (int oy = -1; oy <= 1; ++oy)
        for (int ox = -1; ox <= 1; ++ox)
            spawn_cell_at(world, mgr, ox, oy, ox, oy);
}

bool run_water_blocked_squad_case() {
    sm::SoldierSquad squad{};
    squad.push(sm::make_soldier(
        std::uint8_t(sm::NPCType::Guard), 4, 77u));

    sm::ecs::World world{};
    std::vector<std::uint8_t> water(
        std::size_t(sm::sub::kFullSize) * sm::sub::kFullSize,
        sm::sub::TILE_WATER);
    sm::sub::spawn_player_squad(world, squad, water, 512.0f, 512.0f, 123u,
                                std::uint16_t(sm::faction_index(sm::kPlayerFactionId)));

    int projected = 0;
    auto view = world.reg.view<sm::ecs::PlayerSoldierTag>();
    for (auto e : view) {
        (void)e;
        ++projected;
    }
    return projected == 0;
}

bool run_city_population_projection_case(
    const sm::sub::SeamlessSubworldManager& mgr) {
    sm::ecs::World world{};
    // City in the CENTRE window cell (ox=oy=0) — off-centre cities are covered
    // by the carry-across case; here we lock the citizen role mix.
    sm::sub::spawn_cell_npcs(world,
                             sm::Biome::Meadow,
                             sm::FT_None,
                             sm::LandmarkType::City,
                             /*danger*/0,
                             /*depositsNear*/0,
                             mgr,
                             /*ox*/0, /*oy*/0,
                             0xFACEB00Cu,
                             std::uint16_t(sm::faction_index("empire")),
                             4000,
                             0);

    int count = 0;
    int guards = 0;
    int merchants = 0;
    int woodcutters = 0;
    auto view = world.reg.view<sm::ecs::SubworldTag,
                               sm::ecs::NPCKind,
                               sm::ecs::NpcCharacter,
                               sm::ecs::SubworldAi>();
    for (auto e : view) {
        const auto& kind = view.get<sm::ecs::NPCKind>(e);
        const auto& ai = view.get<sm::ecs::SubworldAi>(e);
        ++count;
        if (kind.type == std::uint16_t(sm::NPCType::Guard)
            && ai.kind == sm::ecs::SubworldAi::Combat) {
            ++guards;
        }
        if (kind.type == std::uint16_t(sm::NPCType::Merchant)) {
            ++merchants;
        }
        if (kind.type == std::uint16_t(sm::NPCType::Woodcutter)) {
            ++woodcutters;
        }
    }

    // Citizens must land inside the centre cell's sub-region, never the whole
    // 3×3 — proof the per-cell origin gate replaced the old centre-only window.
    auto posView = world.reg.view<sm::ecs::SubworldTag, sm::ecs::Position,
                                  sm::ecs::NpcCharacter>();
    for (auto e : posView) {
        const auto& p = posView.get<sm::ecs::Position>(e);
        if (p.x < float(sm::sub::kCellSize) || p.x >= float(2 * sm::sub::kCellSize)
            || p.y < float(sm::sub::kCellSize)
            || p.y >= float(2 * sm::sub::kCellSize)) {
            return false;
        }
    }

    return count >= 24 && guards >= 2 && merchants >= 1 && woodcutters >= 1;
}

// A place decides HOW MANY people stand in it — never how strong each of them is
// (CANON.md S12; the owner's words 2026-08-20: «в большом городе просто больше
// стражников, а так они такие же»). Both halves are asserted here, and the first
// is a NEGATIVE CONTROL for the deleted √(pop/100) auto-level: with the old code
// a 4000-soul city handed every citizen +6 levels, so the band check below failed
// outright. The band itself is not a restated literal — it is what the spawner is
// allowed to add over a row: `baseLevel + rng%3`.
bool run_population_does_not_scale_bodies_case(
    const sm::sub::SeamlessSubworldManager& mgr) {
    struct Town { int pop; int count; };
    Town towns[2] = {{40, 0}, {4000, 0}};

    for (Town& t : towns) {
        sm::ecs::World world{};
        sm::sub::spawn_cell_npcs(world,
                                 sm::Biome::Meadow, sm::FT_None,
                                 sm::LandmarkType::City, /*danger*/0, /*depositsNear*/0, mgr,
                                 /*ox*/0, /*oy*/0,
                                 0xFACEB00Cu,
                                 std::uint16_t(sm::faction_index("empire")),
                                 t.pop);
        auto view = world.reg.view<sm::ecs::SubworldTag, sm::ecs::NPCKind,
                                   sm::ecs::NpcLevel, sm::ecs::NpcCharacter>();
        for (auto e : view) {
            const std::uint16_t typeId = view.get<sm::ecs::NPCKind>(e).type;
            if (typeId >= std::uint16_t(sm::NPCType::Count)) continue;  // fauna
            const int lvl = int(view.get<sm::ecs::NpcLevel>(e).value);
            const int base = int(sm::npc_def(sm::NPCType(typeId)).baseLevel);
            const int ceiling = sm::normalize_soldier_level(base + 2);
            const int floorLvl = sm::normalize_soldier_level(base);
            if (lvl < floorLvl || lvl > ceiling) return false;
            ++t.count;
        }
    }
    // ...and the town's size still shows, in the only place it may: the crowd.
    return towns[0].count > 0 && towns[1].count > towns[0].count;
}

// Symptom #3 — the core seamless-persistence invariant. Fill the 3×3, then cross
// +x and back. Content shared between the old and new windows must be carried
// verbatim (shifted, not re-rolled), only departed cells evicted, and a return
// trip must reproduce the original scene bit-for-bit (fresh-respawn determinism).
bool run_carry_across_case(const sm::sub::SeamlessSubworldManager& mgr) {
    const float kCell = float(sm::sub::kCellSize);
    const float kFull = float(sm::sub::kFullSize);

    sm::ecs::World world{};
    spawn_all_cells(world, mgr);
    const std::vector<SpawnRecord> before = actual_fauna(world);
    if (before.empty()) return false;

    // ── Cross +x (dx=1): shift left one cell, evict what left, spawn column that
    // entered. Window offset ox=1 now maps to absolute cell 2. ──
    sm::sub::rebase_subworld_entities(world, -kCell, 0.0f);
    sm::sub::despawn_subworld_entities_outside_window(world);
    for (int oy = -1; oy <= 1; ++oy)
        spawn_cell_at(world, mgr, /*ox*/1, oy, /*absCx*/2, oy);
    const std::vector<SpawnRecord> after = actual_fauna(world);

    // Invariant 1: nothing drifted outside the composite window.
    for (const SpawnRecord& a : after) {
        if (a.x < 0.0f || a.x >= kFull || a.y < 0.0f || a.y >= kFull) {
            return false;
        }
    }

    // Invariant 2: the overlap (old cells with x >= kCellSize) is carried across
    // untouched — same creatures, shifted by exactly -kCellSize. After the shift
    // they occupy [0, 2*kCellSize); the freshly entered column sits beyond that.
    std::vector<SpawnRecord> expectedSurvivors;
    for (SpawnRecord s : before) {
        if (s.x >= kCell) {
            s.x -= kCell;
            expectedSurvivors.push_back(s);
        }
    }
    std::sort(expectedSurvivors.begin(), expectedSurvivors.end(),
              less_spawn_record);
    std::vector<SpawnRecord> afterSurvivors;
    for (const SpawnRecord& a : after) {
        if (a.x < 2.0f * kCell) afterSurvivors.push_back(a);
    }
    std::sort(afterSurvivors.begin(), afterSurvivors.end(), less_spawn_record);
    if (compare_records(expectedSurvivors, afterSurvivors) != 0) return false;

    // Invariant 3: cross back -x (dx=-1). Carried cells return to their exact
    // original positions (net-zero shift) and the re-entered column respawns
    // deterministically from its seed, so the whole scene equals `before`.
    sm::sub::rebase_subworld_entities(world, kCell, 0.0f);
    sm::sub::despawn_subworld_entities_outside_window(world);
    for (int oy = -1; oy <= 1; ++oy)
        spawn_cell_at(world, mgr, /*ox*/-1, oy, /*absCx*/-1, oy);
    const std::vector<SpawnRecord> roundTrip = actual_fauna(world);
    return compare_records(before, roundTrip) == 0;
}

// Hybrid model: procedural fauna is deleted on eviction and respawns FRESH — but
// fresh must be deterministic per cell (same seed → same set), the property the
// future per-macro-cell visitation counter will perturb on purpose.
bool run_reentry_determinism_case(
    const sm::sub::SeamlessSubworldManager& mgr) {
    sm::ecs::World a{};
    spawn_cell_at(a, mgr, /*ox*/0, /*oy*/0, /*absCx*/7, /*absCy*/3);
    const std::vector<SpawnRecord> first = actual_fauna(a);

    sm::ecs::World b{};
    spawn_cell_at(b, mgr, /*ox*/0, /*oy*/0, /*absCx*/7, /*absCy*/3);
    const std::vector<SpawnRecord> second = actual_fauna(b);

    return !first.empty() && compare_records(first, second) == 0;
}

// Minimal macro-NPC set mirroring make_npc's projection-relevant components
// (MacroNpcRuntime discriminator + Position + NPCKind + Health + NpcLevel +
// NpcCharacter). Created in a FIXED order so a second identical registry
// reproduces the same view iteration — the property the re-entry determinism
// check relies on. Macro Position is integer macro-cell coords on the torus.
struct MacroSeeds {
    entt::entity bandit = entt::null;   // centre cell (0,0)      → in-window
    entt::entity peasant = entt::null;  // +1,0                   → in-window
    entt::entity wrap = entt::null;     // (mapW-1,0) = offset -1 → in-window (torus)
    entt::entity far = entt::null;      // (50,50)                → OUTSIDE window
};

MacroSeeds seed_macro_npcs(entt::registry& reg, int mapW) {
    // Deterministic spawn ordinal, one per NPC, reset per call — exactly like the
    // shipping `spawn_macro_npcs`/`make_npc` counter. This is the save-stable
    // identity a possessed lord is re-found by after a load regenerates the ECS
    // (Inc 5e-2), so stamping it here lets the parity test exercise reattach.
    std::uint32_t spawnIndex = 0;
    auto mk = [&](sm::NPCType type, std::uint16_t faction, int cx, int cy,
                  float hp, float maxHp, std::int16_t level,
                  std::uint32_t vseed) {
        auto e = reg.create();
        reg.emplace<sm::ecs::MacroNpcRuntime>(e);
        reg.emplace<sm::ecs::MacroSpawnId>(e, spawnIndex++);
        reg.emplace<sm::ecs::Position>(e, float(cx), float(cy), 0.0f);
        reg.emplace<sm::ecs::NPCKind>(e, std::uint16_t(type), faction);
        reg.emplace<sm::ecs::Health>(e, hp, maxHp);
        reg.emplace<sm::ecs::NpcLevel>(e, level);
        sm::ecs::NpcCharacter ch{};
        ch.visualSeed = vseed;
        reg.emplace<sm::ecs::NpcCharacter>(e, ch);
        return e;
    };
    MacroSeeds s;
    // The bandit is HALF DEAD on the map (3.5 of 7) — the wound the projection
    // has to carry down. Everyone else is whole, so "arrives whole" has a
    // control standing right next to "arrives wounded".
    s.bandit  = mk(sm::NPCType::Bandit,   3, 0,        0,   3.5f,  7.0f, 4, 0xB0B0u);
    s.peasant = mk(sm::NPCType::Peasant,  1, 1,        0,  12.0f, 12.0f, 2, 0xCAFEu);
    s.wrap    = mk(sm::NPCType::Guard,    2, mapW - 1, 0,  30.0f, 30.0f, 5, 0x1234u);
    s.far     = mk(sm::NPCType::Merchant, 1, 50,       50, 20.0f, 20.0f, 3, 0x9999u);
    return s;
}

// A squad is a squad whatever it is made of (CANON.md S4/S16). A pack leader's
// roster may name BEASTS, and each of them must come down as its own catalog
// row — a sheet-less creature body — while a man in the same roster still comes
// down a man. Before the kind field was widened this was not expressible: the
// monster half of the id space (0x100 | row) did not fit in a byte, so a wolf
// in a roster either vanished at the validity gate or arrived as whatever
// humanoid its low byte happened to name.
bool run_beast_member_projection_case(
    const sm::sub::SeamlessSubworldManager& mgr) {
    constexpr int kMapW = 1024, kMapH = 1024;
    constexpr std::uint16_t kBeast = std::uint16_t(sm::NPCType::Wolf);

    sm::ecs::World world{};
    auto& reg = world.reg;

    auto leader = reg.create();
    reg.emplace<sm::ecs::MacroNpcRuntime>(leader);
    reg.emplace<sm::ecs::MacroSpawnId>(leader, std::uint32_t(0));
    reg.emplace<sm::ecs::Position>(leader, 0.0f, 0.0f, 0.0f);
    reg.emplace<sm::ecs::NPCKind>(leader, std::uint16_t(sm::NPCType::Bandit),
                                  std::uint16_t(3));
    reg.emplace<sm::ecs::Health>(leader, 10.0f, 10.0f);
    reg.emplace<sm::ecs::NpcLevel>(leader, std::int16_t(3));
    reg.emplace<sm::ecs::NpcCharacter>(leader, sm::ecs::NpcCharacter{});

    sm::ecs::SquadRoster roster{};
    roster.squad.push(sm::make_soldier(kBeast, 2, 5001u));
    roster.squad.push(sm::make_soldier(
        std::uint16_t(sm::NPCType::Guard), 2, 5002u));
    reg.emplace<sm::ecs::SquadRoster>(leader, roster);

    const int projected = sm::sub::project_macro_npcs_into_subworld(
        world, mgr, /*cx*/0, /*cy*/0, kMapW, kMapH, 0xBEA57u);
    if (projected != 3) return false;   // the leader and both of his members

    int beasts = 0, men = 0;
    for (auto e : reg.view<sm::ecs::SubworldTag, sm::ecs::NPCKind>()) {
        const std::uint16_t t = reg.get<sm::ecs::NPCKind>(e).type;
        if (t == kBeast) {
            // Built from the WOLF's line: its picture is the wolf's sprite row
            // and its bulk is the wolf's authored radius, not a man's. (The old
            // proof — "a beast has no face" — died with the second birth: there
            // is one birth now and it gives every body the same components.)
            const sm::NpcTypeDef& wolf = sm::npc_def(sm::NPCType::Wolf);
            const auto* spr = reg.try_get<sm::ecs::Sprite>(e);
            if (!spr) return false;
            if (spr->spriteRow != std::uint8_t(wolf.sprite)) return false;
            if (!near(spr->scale, wolf.radius)) return false;
            ++beasts;
        } else {
            ++men;
        }
    }
    // One wolf, two men (the leader and his guard) — the roster was honoured
    // member by member, not flattened to one kind.
    return beasts == 1 && men == 2;
}

// The strength of a body is its ROW seen through its SHEET — the law that made
// the creature birth disappear (CANON.md S14). Asserted as a property, because
// restating the projection here would only prove that a test can copy a
// spawner: every wild body must carry a sheet, must be worth at least the raw
// line its row authors, and must scale with its own level rather than with
// where it was met.
bool sheet_lifts_every_body(sm::ecs::World& world) {
    int checked = 0;
    auto v = world.reg.view<sm::ecs::SubworldTag, sm::ecs::NPCKind,
                            sm::ecs::Health, sm::ecs::NpcLevel>();
    for (auto e : v) {
        const std::uint16_t t = v.get<sm::ecs::NPCKind>(e).type;
        if (!sm::valid_npc_kind(t)) return false;
        const sm::NpcTypeDef& row = sm::npc_def(sm::NPCType(t));
        const auto& h = v.get<sm::ecs::Health>(e);
        if (!world.reg.all_of<sm::CharacterSheet>(e)) return false;
        if (!(h.maxHp >= float(row.combat.hp))) return false;
        ++checked;
    }
    return checked > 0;   // a loop that measured nothing has proven nothing
}

// Sorted (faction,x,y) fingerprint of the projected bodies — handle-independent,
// so it compares cleanly across two worlds for the determinism check.
std::vector<std::array<float, 3>> projection_fingerprint(sm::ecs::World& world) {
    std::vector<std::array<float, 3>> out;
    auto v = world.reg.view<sm::ecs::SubworldTag, sm::ecs::MacroOrigin,
                            sm::ecs::Position, sm::ecs::NPCKind>();
    for (auto e : v) {
        const auto& p = v.get<sm::ecs::Position>(e);
        const auto& k = v.get<sm::ecs::NPCKind>(e);
        out.push_back({float(k.factionIdx), p.x, p.y});
    }
    std::sort(out.begin(), out.end());
    return out;
}

// Inc 5d — macro→subworld projection. A macro NPC standing in the 3×3 window is
// materialised as a full combat body carrying a MacroOrigin backlink; the macro
// entity is left untouched; the reaper spares projections; hostility is data-
// driven from NpcTypeDef.ai via subworld_ai_for (Aggressive|Patrol→Combat,
// else Flee); HP / faction /
// identity are copied body-native; toroidal wrap lands a map-edge NPC in the
// correct window cell; and the same inputs reproduce the same scene.
bool run_macro_projection_case(const sm::sub::SeamlessSubworldManager& mgr) {
    using sm::ecs::SubworldAi;
    constexpr int kMapW = 1024, kMapH = 1024;
    constexpr int kCenterCx = 0, kCenterCy = 0;
    constexpr std::uint32_t kSeed = 0x5D5D5D5Du;
    const float kC = float(sm::sub::kCellSize);

    sm::ecs::World world{};
    auto& reg = world.reg;
    const MacroSeeds s = seed_macro_npcs(reg, kMapW);

    // Ordinary fauna in the centre cell, so the reaper-skip check has non-
    // projection bodies to actually reap. Fauna carries no MacroNpcRuntime, so
    // the projection's source view never sees it.
    spawn_cell_at(world, mgr, /*ox*/0, /*oy*/0, /*absCx*/0, /*absCy*/0);
    int faunaBefore = 0;
    for (auto e : reg.view<sm::ecs::SubworldTag>(
             entt::exclude<sm::ecs::MacroOrigin>)) {
        (void)e;
        ++faunaBefore;
    }
    if (faunaBefore <= 0) return false;   // sanity: fauna actually present

    const int projected = sm::sub::project_macro_npcs_into_subworld(
        world, mgr, kCenterCx, kCenterCy, kMapW, kMapH, kSeed);
    if (projected != 3) return false;     // three in-window, the far one skipped

    // Macro entities are UNTOUCHED: still MacroNpcRuntime, never tagged/linked.
    for (entt::entity m : {s.bandit, s.peasant, s.wrap, s.far}) {
        if (!reg.all_of<sm::ecs::MacroNpcRuntime>(m)) return false;
        if (reg.any_of<sm::ecs::SubworldTag, sm::ecs::MacroOrigin>(m)) return false;
    }

    // Index projections by their origin; every backlink must point at a live
    // macro NPC, and the far one must never appear.
    entt::entity pBandit = entt::null, pPeasant = entt::null, pWrap = entt::null;
    int projCount = 0;
    for (auto e : reg.view<sm::ecs::SubworldTag, sm::ecs::MacroOrigin>()) {
        ++projCount;
        const entt::entity origin = reg.get<sm::ecs::MacroOrigin>(e).macro;
        if (!reg.valid(origin) || !reg.all_of<sm::ecs::MacroNpcRuntime>(origin)) {
            return false;
        }
        if (origin == s.bandit) pBandit = e;
        else if (origin == s.peasant) pPeasant = e;
        else if (origin == s.wrap) pWrap = e;
        else return false;   // far NPC or a stranger — must not be projected
    }
    if (projCount != 3 || pBandit == entt::null || pPeasant == entt::null
        || pWrap == entt::null) {
        return false;
    }

    // Data-driven hostility via the shared subworld_ai_for(): the FIGHTING
    // rows are Aggressive (bandits) and Patrol (guards); every other type
    // flees. So the projected bandit AND the projected guard hold ground while
    // the peasant runs — the same one-column rule as settlement citizens.
    if (reg.get<SubworldAi>(pBandit).kind != SubworldAi::Combat) return false;
    if (reg.get<SubworldAi>(pPeasant).kind != SubworldAi::Flee) return false;
    if (reg.get<SubworldAi>(pWrap).kind != SubworldAi::Combat) return false;

    // A wound travels as a FRACTION, not as a number of points: the two layers
    // never have to agree on how big a bar is, and the return trip needs no
    // conversion either. The bandit is at half on the map, so he arrives at half
    // of whatever his sheet gives him down here — that is the invariant, and it
    // survives any rebalance of either side.
    {
        const auto& h = reg.get<sm::ecs::Health>(pBandit);
        if (!(h.maxHp > 0.0f && h.hp >= 1.0f && h.hp <= h.maxHp)) return false;
        const float frac = h.hp / h.maxHp;
        if (!(frac > 0.4f && frac < 0.6f)) return false;
    }
    // The control: an untouched macro entity arrives untouched. Without this,
    // "wounded arrives wounded" would also pass if every body arrived at half.
    {
        const auto& h = reg.get<sm::ecs::Health>(pWrap);
        if (!(h.maxHp > 0.0f && h.hp == h.maxHp)) return false;
    }
    // Combat SYNTHESISED from the fresh sheet (capability), damage must be sane.
    if (!(reg.get<sm::ecs::Combat>(pBandit).damage > 0.0f)) return false;

    // Identity + faction copied verbatim from the macro NPC.
    if (reg.get<sm::ecs::NpcCharacter>(pBandit).visualSeed != 0xB0B0u) return false;
    if (reg.get<sm::ecs::NPCKind>(pBandit).factionIdx != 3) return false;
    if (reg.get<sm::ecs::NPCKind>(pWrap).factionIdx != 2) return false;

    // Placement: each projection lands in ITS window cell's sub-region (never
    // outside the composite window). Centre → [kC,2kC); +1,0 → [2kC,3kC); the
    // torus-wrapped -1,0 → [0,kC).
    auto in = [](float v, float lo, float hi) { return v >= lo && v < hi; };
    {
        const auto& pb = reg.get<sm::ecs::Position>(pBandit);
        if (!in(pb.x, kC, 2 * kC) || !in(pb.y, kC, 2 * kC)) return false;
        const auto& pp = reg.get<sm::ecs::Position>(pPeasant);
        if (!in(pp.x, 2 * kC, 3 * kC) || !in(pp.y, kC, 2 * kC)) return false;
        const auto& pw = reg.get<sm::ecs::Position>(pWrap);
        if (!in(pw.x, 0.0f, kC) || !in(pw.y, kC, 2 * kC)) return false;
    }

    const std::vector<std::array<float, 3>> fp1 = projection_fingerprint(world);

    // Reaper skip: clear_subworld_world_entities wipes ordinary fauna but SPARES
    // the projections (they mirror persistent macro state, like the player squad).
    sm::sub::clear_subworld_world_entities(world);
    int faunaAfter = 0, projAfter = 0;
    for (auto e : reg.view<sm::ecs::SubworldTag>()) {
        if (reg.all_of<sm::ecs::MacroOrigin>(e)) ++projAfter;
        else ++faunaAfter;
    }
    if (faunaAfter != 0 || projAfter != 3) return false;

    // Determinism: an identical macro set + same centre + same seed reproduces
    // the same projected scene bit-for-bit (the re-entry guarantee).
    sm::ecs::World world2{};
    seed_macro_npcs(world2.reg, kMapW);
    const int projected2 = sm::sub::project_macro_npcs_into_subworld(
        world2, mgr, kCenterCx, kCenterCy, kMapW, kMapH, kSeed);
    if (projected2 != 3) return false;
    const std::vector<std::array<float, 3>> fp2 = projection_fingerprint(world2);
    if (fp1 != fp2) return false;

    return true;
}

// Inc 5e-1 — exit-position remap query. macro_exit_cell_for_body maps a possessed
// macro-projected body back onto its ORIGIN's macro cell ("exit AS the lord"),
// wrapped to the torus. A body with no backlink (the hero husk / ambient fauna)
// yields no remap, so leave() falls back to the window centre; a stale backlink
// (the origin was reaped mid-session) also yields no remap and never crashes.
bool run_exit_remap_case(const sm::sub::SeamlessSubworldManager& mgr) {
    constexpr int kMapW = 1024, kMapH = 1024;
    constexpr int kCenterCx = 0, kCenterCy = 0;
    constexpr std::uint32_t kSeed = 0x5E1E5E1Eu;

    sm::ecs::World world{};
    auto& reg = world.reg;
    const MacroSeeds s = seed_macro_npcs(reg, kMapW);
    const int projected = sm::sub::project_macro_npcs_into_subworld(
        world, mgr, kCenterCx, kCenterCy, kMapW, kMapH, kSeed);
    if (projected != 3) return false;

    // Map two projections back to their origins (centre bandit + map-edge guard).
    entt::entity pBandit = entt::null, pWrap = entt::null;
    for (auto e : reg.view<sm::ecs::SubworldTag, sm::ecs::MacroOrigin>()) {
        const entt::entity origin = reg.get<sm::ecs::MacroOrigin>(e).macro;
        if (origin == s.bandit) pBandit = e;
        else if (origin == s.wrap) pWrap = e;
    }
    if (pBandit == entt::null || pWrap == entt::null) return false;

    // Possess the centre-cell bandit → exit lands on ITS macro cell (0,0).
    {
        const sm::sub::MacroExitCell c =
            sm::sub::macro_exit_cell_for_body(world, pBandit, kMapW, kMapH);
        if (!c.has || c.cx != 0 || c.cy != 0) return false;
    }
    // Possess the map-edge guard → exit lands on the guard's OWN macro cell
    // (mapW-1,0), NOT the window centre (0,0). This is the whole point of 5e-1:
    // you resurface where the body you possessed actually stood on the overworld.
    {
        const sm::sub::MacroExitCell c =
            sm::sub::macro_exit_cell_for_body(world, pWrap, kMapW, kMapH);
        if (!c.has || c.cx != kMapW - 1 || c.cy != 0) return false;
    }
    // No backlink (hero husk / plain body) → no remap; a bare positioned entity
    // stands in for the husk. The function reads the MACRO entity's cell, so the
    // body's own Position here is deliberately irrelevant.
    {
        auto bare = reg.create();
        reg.emplace<sm::ecs::Position>(bare, 5.0f, 5.0f, 0.0f);
        if (sm::sub::macro_exit_cell_for_body(world, bare, kMapW, kMapH).has) {
            return false;
        }
    }
    // Null body → no remap.
    if (sm::sub::macro_exit_cell_for_body(world, entt::null, kMapW, kMapH).has) {
        return false;
    }
    // Degenerate torus dims → no remap (guards the caller's terrain check).
    if (sm::sub::macro_exit_cell_for_body(world, pWrap, 0, kMapH).has) return false;

    // Stale backlink: the possessed lord's macro entity was reaped mid-session →
    // the remap must vanish (default exit), never dereference the dead handle.
    reg.destroy(s.bandit);
    if (sm::sub::macro_exit_cell_for_body(world, pBandit, kMapW, kMapH).has) {
        return false;
    }

    return true;
}

// Inc 5e-2 — identity remap ("exit AS the lord", surviving save/load). Two pure
// ECS halves compose the round-trip, with the serialised int between them:
//   1. adopt_possessed_macro_as_player: leave() hands the single PlayerTag to the
//      possessed macro origin and reports its save-stable ordinal.
//   2. reattach_player_to_macro_spawn: a later load regenerates the macro NPCs
//      from `worldSeed`; the stored ordinal re-finds the SAME lord and moves the
//      flag off the freshly-built hero husk onto it.
// save_roundtrip_test proves the int itself survives; this proves the ECS halves.
bool run_identity_remap_case(const sm::sub::SeamlessSubworldManager& mgr) {
    constexpr int kMapW = 1024, kMapH = 1024;
    constexpr std::uint32_t kSeed = 0x1DEA1DEAu;

    sm::ecs::World world{};
    auto& reg = world.reg;
    const MacroSeeds s = seed_macro_npcs(reg, kMapW);
    if (sm::sub::project_macro_npcs_into_subworld(
            world, mgr, 0, 0, kMapW, kMapH, kSeed) != 3) {
        return false;
    }
    // The ordinal stamped on the bandit (creation order 0) is what adopt must
    // report and what reattach must re-find in a regenerated registry.
    const std::uint32_t banditOrdinal =
        reg.get<sm::ecs::MacroSpawnId>(s.bandit).index;

    // ── Step 1: ADOPT. Mirror leave(): the flag-wearing subworld body is already
    // gone (reaped) by the time adopt runs, so the macro origin has NO PlayerTag
    // yet — adopt must create it AND return the ordinal. ──
    if (reg.any_of<sm::ecs::PlayerTag>(s.bandit)) return false;  // precondition
    const int adopted = sm::sub::adopt_possessed_macro_as_player(world, s.bandit);
    if (adopted < 0 || std::uint32_t(adopted) != banditOrdinal) return false;
    if (!reg.all_of<sm::ecs::PlayerTag>(s.bandit)) return false;
    {
        int tags = 0;
        for (auto e : reg.view<sm::ecs::PlayerTag>()) { (void)e; ++tags; }
        if (tags != 1) return false;   // exactly one flag, on the lord
    }
    // adopt only adopts real macro NPCs: a bare husk (no MacroNpcRuntime) and a
    // null/invalid handle both yield -1 and touch nothing.
    {
        auto husk = reg.create();
        reg.emplace<sm::ecs::Position>(husk, 3.0f, 3.0f, 0.0f);
        if (sm::sub::adopt_possessed_macro_as_player(world, husk) != -1) return false;
        if (reg.any_of<sm::ecs::PlayerTag>(husk)) return false;
        reg.destroy(husk);
    }
    if (sm::sub::adopt_possessed_macro_as_player(world, entt::null) != -1) {
        return false;
    }

    // ── Step 2: REATTACH after a simulated load. A load rebuilds the ECS from
    // scratch, so use a FRESH registry seeded by the same helper (same ordinals →
    // same NPCs) and stand up the ordinary hero husk that boot creates before
    // reattach runs. ──
    sm::ecs::World loaded{};
    auto& lreg = loaded.reg;
    const MacroSeeds ls = seed_macro_npcs(lreg, kMapW);
    const auto husk = lreg.create();
    lreg.emplace<sm::ecs::PlayerTag>(husk);
    lreg.emplace<sm::ecs::Position>(husk, 999.0f, 999.0f, 0.0f);   // stale husk cell

    const float px = 12.0f, py = 34.0f;   // the loaded player scalar (authoritative)
    if (!sm::reattach_player_to_macro_spawn(loaded, int(banditOrdinal), px, py)) {
        return false;
    }
    if (lreg.valid(husk)) return false;                          // husk destroyed
    if (!lreg.all_of<sm::ecs::PlayerTag>(ls.bandit)) return false;
    {
        int tags = 0;
        entt::entity flag = entt::null;
        for (auto e : lreg.view<sm::ecs::PlayerTag>()) { ++tags; flag = e; }
        if (tags != 1 || flag != ls.bandit) return false;        // one flag, on the lord
    }
    {
        const auto& p = lreg.get<sm::ecs::Position>(ls.bandit);
        if (!near(p.x, px) || !near(p.y, py)) return false;      // snapped to scalar
    }

    // Negative paths: an absent ordinal (died before save / seed changed) and a
    // sentinel id<0 both change nothing — the ordinary hero husk is preserved.
    {
        sm::ecs::World miss{};
        seed_macro_npcs(miss.reg, kMapW);
        const auto missHusk = miss.reg.create();
        miss.reg.emplace<sm::ecs::PlayerTag>(missHusk);
        miss.reg.emplace<sm::ecs::Position>(missHusk, 1.0f, 2.0f, 0.0f);
        if (sm::reattach_player_to_macro_spawn(miss, 9999, 5.0f, 6.0f)) return false;
        if (!miss.reg.valid(missHusk)) return false;             // husk preserved
        if (sm::reattach_player_to_macro_spawn(miss, -1, 5.0f, 6.0f)) return false;
        if (!miss.reg.valid(missHusk)) return false;
    }

    return true;
}

} // namespace

int main() {
    sm::sub::clear_saved_subworlds();
    sm::sub::SeamlessSubworldManager mgr;
    mgr.init(0, 0, meadow_cell);
    mgr.consume_composite_dirty();

    // ── Per-cell fauna parity: the centre cell's ECS entities must match the
    // TS-derived roll, now scattered within the centre sub-region only. ──
    const sm::sub::CellContext centre = meadow_cell(0, 0);
    sm::ecs::World world{};
    spawn_cell_at(world, mgr, /*ox*/0, /*oy*/0, /*absCx*/0, /*absCy*/0);

    const std::vector<SpawnRecord> expected =
        expected_cell_fauna(mgr, sm::Biome::Meadow,
                            sm::LandmarkType::None, /*danger*/0, 0, 0,
                            centre.seed, 0);
    const std::vector<SpawnRecord> actual = actual_fauna(world);
    const int cmp = compare_records(expected, actual);
    if (cmp != 0) {
        sm::sub::clear_saved_subworlds();
        return cmp;
    }

    if (!run_water_blocked_squad_case()) {
        sm::sub::clear_saved_subworlds();
        return fail("player squad spawned on an all-water traversability grid");
    }

    if (!sheet_lifts_every_body(world)) {
        sm::sub::clear_saved_subworlds();
        return fail("a wild body came up without a sheet, or weaker than the "
                    "row it is made of (CANON.md S14)");
    }

    if (!run_population_does_not_scale_bodies_case(mgr)) {
        sm::sub::clear_saved_subworlds();
        return fail("a settlement's population moved a body's LEVEL "
                    "(auto-level by town size is deleted — CANON.md S12)");
    }

    if (!run_city_population_projection_case(mgr)) {
        sm::sub::clear_saved_subworlds();
        return fail("city population projection missing roles or off-cell");
    }

    if (!run_carry_across_case(mgr)) {
        sm::sub::clear_saved_subworlds();
        return fail("seam crossing did not carry/evict/respawn cells correctly");
    }

    if (!run_reentry_determinism_case(mgr)) {
        sm::sub::clear_saved_subworlds();
        return fail("per-cell fauna respawn is not deterministic from its seed");
    }

    if (!run_beast_member_projection_case(mgr)) {
        sm::sub::clear_saved_subworlds();
        return fail("a BEAST in a roster did not come down as its own row "
                    "(squads carry men and monsters alike — CANON.md S4/S16)");
    }

    if (!run_macro_projection_case(mgr)) {
        sm::sub::clear_saved_subworlds();
        return fail("macro->subworld projection wrong "
                    "(window/wrap/backlink/hostility/hp/placement/reaper/determinism)");
    }

    if (!run_exit_remap_case(mgr)) {
        sm::sub::clear_saved_subworlds();
        return fail("exit remap wrong "
                    "(origin cell / wrap / no-backlink fallback / stale handle)");
    }

    if (!run_identity_remap_case(mgr)) {
        sm::sub::clear_saved_subworlds();
        return fail("identity remap wrong "
                    "(adopt ordinal/flag/one-tag/guards or reattach "
                    "find/husk-swap/snap/miss)");
    }

    std::printf("OK subworld_spawn_parity_test fauna=%zu seed=%u no_autolevel=%d "
                "water_squad_blocked=1 city_projection=1 carry_across=1 "
                "reentry_determinism=1 macro_projection=1 exit_remap=1 "
                "identity_remap=1\n",
                actual.size(), centre.seed, 1);
    sm::sub::clear_saved_subworlds();
    // ── THE spawn law's own invariants (fauna.h, owner 2026-08-24) ──
    {
        // The danger byte re-weights WHO IS ROLLED — and nothing after the
        // pick (S12: the autolevel negative controls above stand untouched).
        auto mean_strength = [](std::uint8_t danger) {
            sm::SpawnContext c{};
            c.biome = sm::Biome::Meadow;
            c.danger = danger;
            double sum = 0.0;
            int n = 0;
            for (std::uint32_t seed = 1; seed <= 64; ++seed) {
                std::uint32_t rs = seed * 2654435761u;
                for (const auto& pk : sm::roll_spawns(c, rs)) {
                    sum += double(sm::spawn_strength(pk.entry->type));
                    ++n;
                }
            }
            return n > 0 ? sum / double(n) : 0.0;
        };
        const double safe = mean_strength(16);
        const double wild = mean_strength(240);
        CHECK(wild > safe + 8.0,
              "the danger byte shifts composition toward the strong rows");
        CHECK(sm::danger_match_weight(255, 0) == 1u,
              "a full-span mismatch is vanishingly rare — never zero");
        CHECK(sm::danger_match_weight(200, 200) == 1024u,
              "a perfect strength-danger match pays the full peak");
        // Same seed, same danger => the identical stream (determinism).
        sm::SpawnContext c{};
        c.biome = sm::Biome::Meadow;
        c.danger = 128;
        std::uint32_t r1 = 0xC0FFEEu, r2 = 0xC0FFEEu;
        const auto a = sm::roll_spawns(c, r1);
        const auto b = sm::roll_spawns(c, r2);
        bool same = a.size() == b.size();
        for (std::size_t i = 0; same && i < a.size(); ++i) {
            same = a[i].entry == b[i].entry;
        }
        CHECK(same, "the roll is a fact of (seed, context) alone");
        // The derivation orders the bestiary the way the fiction does.
        CHECK(sm::spawn_strength(sm::NPCType::Rabbit)
                  < sm::spawn_strength(sm::NPCType::Wolf)
              && sm::spawn_strength(sm::NPCType::Wolf)
                  < sm::spawn_strength(sm::NPCType::Troll),
              "derived strength orders rabbit < wolf < troll");
    }

    CHECK(true, "every gate above held");
    return sm::test::report("subworld_spawn_parity_test");
}

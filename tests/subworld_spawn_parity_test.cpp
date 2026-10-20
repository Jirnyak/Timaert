#include "check.h"
#include "scene_objects_fixture.h"  // арена сцены фикстурам (кусок 1)
#include "tables/faction.h"
#include "macro/world_row.h"
#include "macro/fauna.h"   // SpawnContext/roll_spawns — паритет зовёт закон логова сам
#include "sub/spawn.h"
#include "sub/record.h"   // THE door: whose record is this body (mirror law)
#include "sub/body.h"     // body_height_m — продукт лица головы (кусок 1)
#include "core/rng.h"
#include "ecs/components.h"
#include "tables/npc.h"
#include "macro/player_entity.h"
#include "sub/base_generator.h"
#include "macro/store.h"

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
    c.landmark.kind = sm::SquadType::None;
    c.seed = 0x24680000u
        ^ sm::sub::cell_seed(0u, cx, cy);
    return c;
}

// TS-derived expectation for ONE window cell at offset (ox,oy). Mirrors
// spawn_cell_npcs exactly: same table roll from `seed`, same 20-try water dodge,
// but scattered only within that cell's sub-region [(ox+1)*kCellSize .. +
// kCellSize)² — the change that keeps every cell's fauna in its own cell.
std::vector<SpawnRecord> expected_cell_fauna(
    const sm::sub::SeamlessSubworldManager& mgr,
    sm::Biome biome,
    sm::SquadType landmark,
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
        r.faction = pick.faction;   // ОРДИНАЛ колонкой (M-187)
        r.x = fx;
        r.y = fy;
        r.hp = hp;
        r.maxHp = hp;
        r.damage = damage;
        r.speed = sm::march_speed(f.combat.speedMarchMult);
        r.range = f.combat.attackRange;
        // Cooldown is sheet-derived since the recovery door (S14, 2026-09-07)
        // — per-body like hp/damage, so it leaves the exact mirror and is
        // asserted as a PROPERTY below (`sheet_lifts_every_body`: the sheet
        // may only QUICKEN the row's base, never slow or zero it).
        r.cooldown = 0.0f;
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

std::vector<SpawnRecord> actual_fauna(sm::sub::SubObjects& objs) {
    std::vector<SpawnRecord> out;
    // СВИП ПЛОСКОЙ АРЕНЫ (ломоть D): сущности у тела больше нет, живой слот
    // называет сам себя битом kObjAlive. Снарядного гейта здесь не нужно —
    // отбор идёт колонками тела (вид/род/бары/лист/думка), а у снаряда нет
    // ни вида, ни рода.
    for (int s = 0; s < int(sm::kMaxSubObjects); ++s) {
        if ((objs.flags[std::size_t(s)] & sm::sub::kObjAlive) == 0u) continue;
        if (sm::sub::slot_sprite(objs, s) == nullptr) continue;
        const auto* kindCol = sm::sub::slot_kind(objs, s);
        if (kindCol == nullptr) continue;
        const auto* hpCol = sm::sub::slot_pools(objs, s);
        const auto* combatCol = sm::sub::slot_combat(objs, s);
        const auto* aiColF = sm::sub::slot_ai(objs, s);
        if (hpCol == nullptr || combatCol == nullptr
            || aiColF == nullptr) continue;
        const auto& kind = *kindCol;
        const auto& pos = *sm::sub::slot_pos(objs, s);
        const auto& hp = *hpCol;
        const auto& combat = *combatCol;
        const std::int16_t level = sm::sub::slot_level(objs, s);
        const auto& ai = *aiColF;
        const auto& sprite = *sm::sub::slot_sprite(objs, s);

        SpawnRecord r{};
        r.type = kind.type;
        r.faction = kind.factionIdx;
        r.x = pos.x;
        r.y = pos.y;
        r.hp = hp.hp;
        r.maxHp = hp.maxHp;
        // The wound is dice now; the record keeps the strike's expectation
        // (print-only — strength is asserted as a property, not mirrored).
        r.damage = float(sm::dice_mean_x2(combat.dice)) * 0.5f
                 + float(combat.flatAdd);
        r.speed = combat.speed;
        r.range = combat.attackRange;
        r.cooldown = 0.0f;   // property, not mirror — see expected_fauna
        r.radius = ai.radius;
        r.level = level;
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
    const bool ok1 = !(expected.size() != actual.size());
    if (!ok1) {
        std::fprintf(stderr, "expected=%zu actual=%zu\n",
                     expected.size(), actual.size());
    }
    CHECK(ok1,
          "the cell spawns exactly as many creatures as its seeded roll names");
    if (!ok1) return 1;

    // ОДИН ВЕРДИКТ НА ОБХОД, А НЕ ОДИН НА ЗАПИСЬ (AGENTS §8 п.3). Раньше гейт
    // бил по разу на существо и выходил на первом несовпавшем, из-за чего счёт
    // проверок прибора был СОДЕРЖАНИЕМ (сколько тварей накатал генератор), а
    // не покрытием, и пин на него был бы пином на выход одного сида. Теперь
    // обход полный: печатается ПЕРВАЯ несовпавшая запись целиком (адрес
    // провала настоящий) и общий счёт «N из M». Что обход что-то намерил,
    // утверждает ok1 выше — он требует непустого и равного размера.
    int mismatches = 0;
    for (std::size_t i = 0; i < expected.size(); ++i) {
        const SpawnRecord& e = expected[i];
        const SpawnRecord& a = actual[i];
        if (!records_match(e, a) && mismatches++ == 0) {
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
        }
    }
    const bool ok2 = !(mismatches != 0);
    if (!ok2) {
        std::fprintf(stderr, "records that do not match: %d of %zu\n",
                     mismatches, expected.size());
    }
    CHECK(ok2,
          "every spawned creature matches its catalogue row and stands inside its OWN cell");
    if (!ok2) return 1;
    return 0;
}

// Spawn one window cell (offset ox,oy) whose ABSOLUTE macro coordinate is
// (absCx,absCy) — the seed is taken from that absolute coord so a crossing that
// re-maps the same window offset to a different macro cell reproduces the right
// creatures, exactly like SubworldEngine::spawn_cell resolving center+offset.
void spawn_cell_at(sm::sub::SubObjects& objs,
                   const sm::sub::SeamlessSubworldManager& mgr,
                   int ox, int oy, int absCx, int absCy) {
    const sm::sub::CellContext c = meadow_cell(absCx, absCy);
    sm::sub::spawn_cell_npcs(objs, c.biome, c.feature,
                             sm::SquadType::None, /*danger*/0, /*depositsNear*/0, mgr,
                             ox, oy, c.seed, /*worldSeed*/c.seed,
                             std::uint16_t(sm::faction_index("empire")),
                             0,
/*landmarkSubjectId*/-1,
                             /*macroCellX*/0, /*macroCellY*/0,
                             /*faunaCount*/-1, /*garrison*/nullptr,
                             sm::world_time_at(1, 12, 0));
}

// Fill all nine window cells for a manager centred on macro (0,0): window offset
// (ox,oy) IS absolute cell (ox,oy) here.
void spawn_all_cells(sm::sub::SubObjects& objs,
                     const sm::sub::SeamlessSubworldManager& mgr) {
    for (int oy = -1; oy <= 1; ++oy)
        for (int ox = -1; ox <= 1; ++ox)
            spawn_cell_at(objs, mgr, ox, oy, ox, oy);
}

bool run_water_blocked_squad_case() {
    sm::Inventory squad{};
    sm::creatures_push(squad, sm::make_soldier(
        std::uint8_t(sm::NPCType::Guard), 4, 77u));

    auto worldArena_ = sm::test::make_scene_arena();
    sm::sub::SubObjects& objs = *worldArena_;   // арена сцены — предусловие рождения тел (кусок 1, §8 п.11)
    std::vector<std::uint8_t> water(
        std::size_t(sm::sub::kFullSize) * sm::sub::kFullSize,
        sm::sub::TILE_WATER);
    sm::sub::spawn_player_squad(objs, squad, water, 512.0f, 512.0f, 123u,
                                std::uint16_t(sm::faction_index(sm::kPlayerFactionId)));

    int projected = 0;
    // Свип плоской арены (ломоть D): гейт — колонка займа, не сущность.
    for (int s = 0; s < int(sm::kMaxSubObjects); ++s) {
        if ((objs.flags[std::size_t(s)] & sm::sub::kObjAlive) == 0u) continue;
        const auto* d = sm::sub::slot_debt(objs, s);
        if (d != nullptr
            && d->stock == std::uint8_t(sm::MacroStock::Creatures))
            ++projected;
    }
    return projected == 0;
}

bool run_city_population_projection_case(
    const sm::sub::SeamlessSubworldManager& mgr) {
    auto worldArena_ = sm::test::make_scene_arena();
    sm::sub::SubObjects& objs = *worldArena_;   // арена сцены — предусловие рождения тел (кусок 1, §8 п.11)
    // City in the CENTRE window cell (ox=oy=0) — off-centre cities are covered
    // by the carry-across case; here we lock the citizen role mix.
    //
    // СОСТАВ УЛИЦЫ ЕСТЬ СОСТАВ КОНТЕЙНЕРА (v122, вердикт владельца: «что
    // есть в сквадe города в инвентаре, то чел и увидит в городе»). Прежде
    // фикстура давала пять стражей «гарнизоном», а крестьян доролливала
    // сцена по числу паствы — ролл УНИЧТОЖЕН, поэтому толпу кладёт сюда
    // фикстура, целиком и сама (§8 п.11). Пять стражей ИМЕННЫЕ: их лица
    // обязаны переживать пере-вход, и свидетель ниже ждёт ровно пятерых.
    sm::Inventory homeSouls{};
    sm::raise_flock_into_container(homeSouls, 4000);
    for (int i = 0; i < 5; ++i) {
        sm::creatures_push(homeSouls, sm::make_soldier(
            std::uint8_t(sm::NPCType::Guard),
            sm::npc_def(sm::NPCType::Guard).baseLevel,
            4000u + std::uint32_t(i)));
    }
    sm::sub::spawn_cell_npcs(objs,
                             sm::Biome::Meadow,
                             sm::FT_None,
                             sm::SquadType::City,
                             /*danger*/0,
                             /*depositsNear*/0,
                             mgr,
                             /*ox*/0, /*oy*/0,
                             0xFACEB00Cu,
                             /*worldSeed*/0xFACEB00Cu,
                             std::uint16_t(sm::faction_index("empire")),
                             4000,
                             0,
                             /*macroCellX*/0, /*macroCellY*/0,
                             /*faunaCount*/-1, &homeSouls,
                             sm::world_time_at(1, 12, 0));

    // The street is Peasant + Guard and NOBODY else since 2026-09-18 (verdict
    // №2 taken literally: «пусть пока в городе только стражники и пизанты»).
    // The professions, the merchant and the witch keep their enum rows as
    // dead save ordinals — a spawner raising one is exactly the regression
    // this fixture now guards against.
    int count = 0;
    int guards = 0;
    int peasants = 0;
    int others = 0;
    // Свип плоской арены (ломоть D): гейт — колонки рода и думки.
    for (int s = 0; s < int(sm::kMaxSubObjects); ++s) {
        if ((objs.flags[std::size_t(s)] & sm::sub::kObjAlive) == 0u) continue;
        const auto* kindCol = sm::sub::slot_kind(objs, s);
        if (kindCol == nullptr) continue;
        const auto* aiCol = sm::sub::slot_ai(objs, s);
        if (aiCol == nullptr) continue;
        const auto& kind = *kindCol;
        const auto& ai = *aiCol;
        ++count;
        if (kind.type == std::uint16_t(sm::NPCType::Guard)
            && ai.kind == sm::ecs::SubworldAi::Combat) {
            ++guards;
        } else if (kind.type == std::uint16_t(sm::NPCType::Peasant)) {
            ++peasants;
        } else {
            ++others;
        }
    }

    // Citizens must land inside the centre cell's sub-region, never the whole
    // 3×3 — proof the per-cell origin gate replaced the old centre-only window.
    for (int s = 0; s < int(sm::kMaxSubObjects); ++s) {
        if ((objs.flags[std::size_t(s)] & sm::sub::kObjAlive) == 0u) continue;
        if (sm::sub::slot_kind(objs, s) == nullptr) continue;
        const auto& p = *sm::sub::slot_pos(objs, s);
        if (p.x < float(sm::sub::kCellSize) || p.x >= float(2 * sm::sub::kCellSize)
            || p.y < float(sm::sub::kCellSize)
            || p.y >= float(2 * sm::sub::kCellSize)) {
            return false;
        }
    }

    return count >= 24 && guards == 5 && peasants >= 1 && others == 0;
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
        auto townArena_ = sm::test::make_scene_arena();
        sm::sub::SubObjects& objs = *townArena_;   // арена сцены — предусловие рождения тел (кусок 1, §8 п.11)
        // ВОПЛОЩАЮТСЯ ГОЛОВЫ (v122), и предмет этого контроля от того лишь
        // ЖЁСТЧЕ: уровень тела теперь ФАКТ его головы, а головы обоих
        // городов рождены одной дверью — значит размер города не может
        // сдвинуть уровень даже случайно. Предусловие свидетель рождает
        // сам (§8 п.11).
        sm::Inventory homeSouls{};
        sm::raise_flock_into_container(homeSouls, t.pop);
        sm::sub::spawn_cell_npcs(objs,
                                 sm::Biome::Meadow, sm::FT_None,
                                 sm::SquadType::City, /*danger*/0, /*depositsNear*/0, mgr,
                                 /*ox*/0, /*oy*/0,
                                 0xFACEB00Cu,
                                 /*worldSeed*/0xFACEB00Cu,
                                 std::uint16_t(sm::faction_index("empire")),
                                 t.pop,
                                 /*landmarkSubjectId*/-1,
                                 /*macroCellX*/0, /*macroCellY*/0,
                                 /*faunaCount*/-1, &homeSouls,
                                 sm::world_time_at(1, 12, 0));
        // Свип плоской арены (ломоть D): гейт — колонка рода.
        for (int s = 0; s < int(sm::kMaxSubObjects); ++s) {
            if ((objs.flags[std::size_t(s)] & sm::sub::kObjAlive) == 0u)
                continue;
            const auto* kindCol = sm::sub::slot_kind(objs, s);
            if (kindCol == nullptr) continue;
            const std::uint16_t typeId = kindCol->type;
            if (typeId >= std::uint16_t(sm::NPCType::Count)) continue;  // fauna
            const int lvl = int(sm::sub::slot_level(objs, s));
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

    auto worldArena_ = sm::test::make_scene_arena();
    sm::sub::SubObjects& objs = *worldArena_;   // арена сцены — предусловие рождения тел (кусок 1, §8 п.11)
    spawn_all_cells(objs, mgr);
    const std::vector<SpawnRecord> before = actual_fauna(objs);
    if (before.empty()) return false;

    // ── Cross +x (dx=1): shift left one cell, evict what left, spawn column that
    // entered. Window offset ox=1 now maps to absolute cell 2. ──
    sm::sub::rebase_subworld_entities(objs, -kCell, 0.0f);
    sm::sub::despawn_subworld_entities_outside_window(objs);
    for (int oy = -1; oy <= 1; ++oy)
        spawn_cell_at(objs, mgr, /*ox*/1, oy, /*absCx*/2, oy);
    const std::vector<SpawnRecord> after = actual_fauna(objs);

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
    sm::sub::rebase_subworld_entities(objs, kCell, 0.0f);
    sm::sub::despawn_subworld_entities_outside_window(objs);
    for (int oy = -1; oy <= 1; ++oy)
        spawn_cell_at(objs, mgr, /*ox*/-1, oy, /*absCx*/-1, oy);
    const std::vector<SpawnRecord> roundTrip = actual_fauna(objs);
    return compare_records(before, roundTrip) == 0;
}

// Hybrid model: procedural fauna is deleted on eviction and respawns FRESH — but
// fresh must be deterministic per cell (same seed → same set), the property the
// future per-macro-cell visitation counter will perturb on purpose.
bool run_reentry_determinism_case(
    const sm::sub::SeamlessSubworldManager& mgr) {
    auto aArena_ = sm::test::make_scene_arena();
    sm::sub::SubObjects& a = *aArena_;   // арена сцены — предусловие рождения тел (кусок 1, §8 п.11)
    spawn_cell_at(a, mgr, /*ox*/0, /*oy*/0, /*absCx*/7, /*absCy*/3);
    const std::vector<SpawnRecord> first = actual_fauna(a);

    auto bArena_ = sm::test::make_scene_arena();
    sm::sub::SubObjects& b = *bArena_;   // арена сцены — предусловие рождения тел (кусок 1, §8 п.11)
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
    sm::MacroHandle bandit{};   // centre cell (0,0)      → in-window
    sm::MacroHandle peasant{};  // +1,0                   → in-window
    sm::MacroHandle wrap{};     // (mapW-1,0) = offset -1 → in-window (torus)
    sm::MacroHandle far{};      // (50,50)                → OUTSIDE window
};

MacroSeeds seed_macro_npcs(sm::MacroStore& st, int mapW) {
    // Deterministic spawn ordinal, one per NPC, reset per call — exactly like
    // the shipping `spawn_macro_npcs`/`make_npc` counter: the save-stable
    // identity every persistent macro NPC carries (Inc 5e-2).
    std::uint32_t spawnIndex = 1;   // выдача с 1: ординал 0 — «никто»
    auto mk = [&](sm::NPCType type, std::uint16_t faction, int cx, int cy,
                  int hp, int maxHp, std::int16_t level,
                  std::uint32_t vseed) {
        sm::MacroHandle h = sm::store_birth(st);
        sm::store_rename_ordinal(st, h, spawnIndex++);
        h = sm::handle_at(st, h.slot);
        st.cell[h.slot] = sm::ecs::MacroCell{
            sm::ecs::cell_index(cx, cy, 1024)};
        st.kind[h.slot] = sm::ecs::NPCKind{std::uint16_t(type), faction};
        st.pools[h.slot] = sm::ecs::Pools{hp, maxHp};
        // Уровень — АНКЕТА (M-246а), той же дверью, какой рождает make_npc.
        st.sheet[h.slot] = sm::make_character_sheet(
            type, level, sm::leader_sheet_seed(st.spawnId[h.slot].index));
        sm::ecs::NpcCharacter ch{};
        ch.visualSeed = vseed;
        // Отличимое от дефолта телосложение: лицо на теле не хранится
        // (кусок 1), закон «лицо — факт головы» свидетельствуется ростом,
        // и рост нулевой формы был бы неотличим от «лицо потеряно».
        ch.bodyShape = 3;
        st.character[h.slot] = ch;
        return h;
    };
    MacroSeeds s;
    // The bandit is WOUNDED on the map (3 of 7 — the bar is integer since
    // v80; the old float 3.5f truncated to exactly this) — the wound the
    // projection has to carry down. Everyone else is whole, so "arrives
    // whole" has a control standing right next to "arrives wounded".
    s.bandit  = mk(sm::NPCType::Bandit,   3, 0,        0,   3,  7, 4, 0xB0B0u);
    s.peasant = mk(sm::NPCType::Peasant,  1, 1,        0,  12, 12, 2, 0xCAFEu);
    s.wrap    = mk(sm::NPCType::Guard,    2, mapW - 1, 0,  30, 30, 5, 0x1234u);
    s.far     = mk(sm::NPCType::Merchant, 1, 50,       50, 20, 20, 3, 0x9999u);
    return s;
}

// A squad is a squad whatever it is made of (CANON.md S4/S16). A pack leader's
// creatures may name BEASTS, and each of them must come down as its own catalog
// row — a sheet-less creature body — while a man in the same creatures still comes
// down a man. Before the kind field was widened this was not expressible: the
// monster half of the id space (0x100 | row) did not fit in a byte, so a wolf
// in a creatures either vanished at the validity gate or arrived as whatever
// humanoid its low byte happened to name.
bool run_beast_member_projection_case(
    const sm::sub::SeamlessSubworldManager& mgr) {
    constexpr int kMapW = 1024, kMapH = 1024;
    constexpr std::uint16_t kBeast = std::uint16_t(sm::NPCType::Wolf);

    auto worldStore_ = sm::make_macro_store();
    auto worldArena_ = sm::test::make_scene_arena();
    sm::sub::SubObjects& objs = *worldArena_;   // арена сцены — предусловие рождения тел (кусок 1, §8 п.11)

    sm::MacroStore& stl = *worldStore_;
    const sm::MacroHandle hl = sm::store_birth(stl);
    // (Пин ординала 0 снят с M-220: 0 — «никто» по закону нуля-ординала,
    // рождение уже выдало законный ординал само.)
    stl.cell[hl.slot] = sm::ecs::MacroCell{sm::ecs::cell_index(0, 0, 1024)};
    stl.kind[hl.slot] = sm::ecs::NPCKind{std::uint16_t(sm::NPCType::Bandit),
                                         std::uint16_t(3)};
    stl.pools[hl.slot] = sm::ecs::Pools{10, 10};
    stl.sheet[hl.slot].levelData.level = 3;   // уровень — анкета (M-246а)
    {
        auto& bag = stl.inventory[hl.slot];
        sm::creatures_push(bag.inv, sm::make_soldier(kBeast, 2, 5001u));
        sm::creatures_push(bag.inv, sm::make_soldier(
            std::uint16_t(sm::NPCType::Guard), 2, 5002u));
    }

    const int projected = sm::sub::project_macro_npcs_into_subworld(
        objs, stl, mgr, /*cx*/0, /*cy*/0, kMapW, kMapH, 0xBEA57u,
        sm::sub::WATER_LEVEL);
    if (projected != 3) return false;   // the leader and both of his members

    int beasts = 0, men = 0;
    // Свип плоской арены (ломоть D): гейт — колонка рода, и он же держит
    // счёт честным (у снаряда рода нет, за «мужика» он не сойдёт).
    for (int s = 0; s < int(sm::kMaxSubObjects); ++s) {
        if ((objs.flags[std::size_t(s)] & sm::sub::kObjAlive) == 0u) continue;
        const auto* kindCol = sm::sub::slot_kind(objs, s);
        if (kindCol == nullptr) continue;
        const std::uint16_t t = kindCol->type;
        if (t == kBeast) {
            // Built from the WOLF's line: its picture is the wolf's sprite row
            // and its bulk is the wolf's authored radius, not a man's. (The old
            // proof — "a beast has no face" — died with the second birth: there
            // is one birth now and it gives every body the same components.)
            const sm::NpcTypeDef& wolf = sm::npc_def(sm::NPCType::Wolf);
            const auto* spr = sm::sub::slot_sprite(objs, s);
            if (!spr) return false;
            if (spr->spriteRow != std::uint8_t(wolf.sprite)) return false;
            if (!near(spr->scale, wolf.radius)) return false;
            ++beasts;
        } else {
            ++men;
        }
    }
    // One wolf, two men (the leader and his guard) — the creatures was honoured
    // member by member, not flattened to one kind.
    return beasts == 1 && men == 2;
}

// The strength of a body is its ROW seen through its SHEET — the law that made
// the creature birth disappear (CANON.md S14). Asserted as a property, because
// restating the projection here would only prove that a test can copy a
// spawner: every wild body must carry a sheet, must be worth at least the raw
// line its row authors, and must scale with its own level rather than with
// where it was met.
bool sheet_lifts_every_body(sm::sub::SubObjects& objs) {
    int checked = 0;
    // Свип плоской арены (ломоть D): гейт — колонка рода.
    for (int s = 0; s < int(sm::kMaxSubObjects); ++s) {
        if ((objs.flags[std::size_t(s)] & sm::sub::kObjAlive) == 0u) continue;
        const auto* kindCol = sm::sub::slot_kind(objs, s);
        if (kindCol == nullptr) continue;
        const std::uint16_t t = kindCol->type;
        if (!sm::valid_npc_kind(t)) return false;
        const sm::NpcTypeDef& row = sm::npc_def(sm::NPCType(t));
        const auto* hCol = sm::sub::slot_pools(objs, s);
        if (hCol == nullptr) continue;
        const auto& h = *hCol;
        if (sm::sub::slot_sheet(objs, s) == nullptr) return false;
        if (!(h.maxHp >= float(row.combat.hp))) return false;
        // The recovery door's half of the same property (S14, 2026-09-07):
        // the sheet may only QUICKEN the row's authored tempo — Spd and the
        // generic skill divide, nothing multiplies — and never to zero.
        if (const auto* c = sm::sub::slot_combat(objs, s)) {
            if (!(c->cooldown > 0.0f
                  && c->cooldown <= row.combat.cooldown + 1.0e-4f)) {
                return false;
            }
        }
        ++checked;
    }
    return checked > 0;   // a loop that measured nothing has proven nothing
}

// Sorted (faction,x,y) fingerprint of the projected bodies — handle-independent,
// so it compares cleanly across two worlds for the determinism check.
std::vector<std::array<float, 3>> projection_fingerprint(
    sm::sub::SubObjects& objs) {
    std::vector<std::array<float, 3>> out;
    // Свип плоской арены (ломоть D): гейт — ГОДНЫЙ бэклинк шва.
    for (int s = 0; s < int(sm::kMaxSubObjects); ++s) {
        if ((objs.flags[std::size_t(s)] & sm::sub::kObjAlive) == 0u) continue;
        if (sm::sub::slot_macro_origin(objs, s).slot
            == sm::kMacroNoSlot) continue;
        const auto* k = sm::sub::slot_kind(objs, s);
        if (k == nullptr) continue;
        const auto& p = *sm::sub::slot_pos(objs, s);
        out.push_back({float(k->factionIdx), p.x, p.y});
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

    auto worldStore_ = sm::make_macro_store();
    auto worldArena_ = sm::test::make_scene_arena();
    sm::sub::SubObjects& objs = *worldArena_;   // арена сцены — предусловие рождения тел (кусок 1, §8 п.11)
    const MacroSeeds s = seed_macro_npcs(*worldStore_, kMapW);

    // Ordinary fauna in the centre cell, so the reaper-skip check has non-
    // projection bodies to actually reap. Fauna carries no MacroNpcRuntime, so
    // the projection's source view never sees it.
    spawn_cell_at(objs, mgr, /*ox*/0, /*oy*/0, /*absCx*/0, /*absCy*/0);
    int faunaBefore = 0;
    {
        // Свип плоской арены (ломоть D). ЗДЕСЬ ГЕЙТ ОБЯЗАТЕЛЕН И ОН НЕ
        // УКРАШЕНИЕ: счёт идёт по ОТСУТСТВИЮ бэклинка, то есть считает
        // «всех, кто не проекция», а entity-вьюха снарядных слотов не
        // видела вовсе (у снаряда сущности нет). В этой фикстуре никто не
        // стреляет — число не меняется, но форма обязана быть честной.
        for (int slot = 0; slot < int(sm::kMaxSubObjects); ++slot) {
            if ((objs.flags[std::size_t(slot)] & sm::sub::kObjAlive) == 0u)
                continue;
            if ((objs.flags[std::size_t(slot)] & sm::sub::kObjProjectile) != 0u)
                continue;
            if (sm::sub::slot_macro_origin(objs, slot).id != 0)
                continue;
            ++faunaBefore;
        }
    }
    if (faunaBefore <= 0) return false;   // sanity: fauna actually present

    const int projected = sm::sub::project_macro_npcs_into_subworld(
        objs, *worldStore_, mgr, kCenterCx, kCenterCy, kMapW, kMapH, kSeed,
        sm::sub::WATER_LEVEL);
    if (projected != 3) return false;     // three in-window, the far one skipped

    // Macro records are UNTOUCHED: слоты живы, проекция их не трогает
    // (кластер 7: макро-сквад в реестре не живёт, судить теги не у кого).
    for (const sm::MacroHandle m : {s.bandit, s.peasant, s.wrap, s.far}) {
        if (!worldStore_->valid(m)) return false;
    }

    // Index projections by their origin; every backlink must point at a live
    // macro NPC, and the far one must never appear.
    //
    // СВИП ПЛОСКОЙ АРЕНЫ (ломоть D): адрес тела ЕСТЬ слот, поэтому три
    // переменные ниже — сразу слоты, которыми читаются колонки, и перевода
    // между адресациями больше нет вовсе; «не нашли» — отрицательный слот.
    // Снарядного гейта здесь не нужно: отбор идёт по бэклинку шва, а слот
    // снаряда рождается без него (`MacroHandle{}.id == 0`).
    int sBandit = -1, sPeasant = -1, sWrap = -1;
    int projCount = 0;
    for (int slot = 0; slot < int(sm::kMaxSubObjects); ++slot) {
        if ((objs.flags[std::size_t(slot)] & sm::sub::kObjAlive) == 0u) continue;
        const sm::MacroHandle origin = sm::sub::slot_macro_origin(objs, slot);
        if (origin.id == 0) continue;
        ++projCount;
        if (!worldStore_->valid(origin)) {
            return false;
        }
        if (origin == s.bandit) sBandit = slot;
        else if (origin == s.peasant) sPeasant = slot;
        else if (origin == s.wrap) sWrap = slot;
        else return false;   // far NPC or a stranger — must not be projected
    }
    if (projCount != 3 || sBandit < 0 || sPeasant < 0 || sWrap < 0) {
        return false;
    }

    // Data-driven hostility via the shared subworld_ai_for(): the FIGHTING
    // rows are Aggressive (bandits) and Patrol (guards); every other type
    // flees. So the projected bandit AND the projected guard hold ground while
    // the peasant runs — the same one-column rule as settlement citizens.
    if (sm::sub::slot_ai(objs, sBandit)->kind != SubworldAi::Combat) return false;
    if (sm::sub::slot_ai(objs, sPeasant)->kind != SubworldAi::Flee) return false;
    if (sm::sub::slot_ai(objs, sWrap)->kind != SubworldAi::Combat) return false;

    // A wound travels as a FRACTION, not as a number of points: the two layers
    // never have to agree on how big a bar is, and the return trip needs no
    // conversion either. The bandit is at half on the map, so he arrives at half
    // of whatever his sheet gives him down here — that is the invariant, and it
    // survives any rebalance of either side.
    {
        const auto& h = (*sm::sub::slot_pools(objs, sBandit));
        if (!(h.maxHp > 0 && h.hp >= 1 && h.hp <= h.maxHp)) return false;
        const float frac = float(h.hp) / float(h.maxHp);
        if (!(frac > 0.4f && frac < 0.6f)) return false;
    }
    // The control: an untouched macro entity arrives untouched. Without this,
    // "wounded arrives wounded" would also pass if every body arrived at half.
    {
        const auto& h = (*sm::sub::slot_pools(objs, sWrap));
        if (!(h.maxHp > 0.0f && h.hp == h.maxHp)) return false;
    }
    // Combat SYNTHESISED from the fresh sheet (capability): the row's dice
    // must be real and the sheet's add positive — a spent sheet always adds.
    if (!(sm::sub::slot_combat(objs, sBandit)->dice.n > 0)) return false;
    if (!(sm::sub::slot_combat(objs, sBandit)->flatAdd > 0)) return false;

    // Identity + faction copied verbatim from the macro NPC. Лицо на теле не
    // хранится (кусок 1) — «лицо — факт головы» свидетельствуется ПРОДУКТОМ:
    // рост спрайта обязан быть ростом строки, умноженным на телосложение
    // ЗАПИСИ (store — истина; ослабление против дословной копии названо
    // при вердикте).
    {
        const sm::MacroStore& st = *worldStore_;
        const float want =
            sm::sub::body_height_m(sm::npc_def(sm::NPCType::Bandit))
            * sm::sub::body_shape_height_scale(
                  st.character[s.bandit.slot].bodyShape);
        const auto* spr = sm::sub::slot_sprite(objs, sBandit);
        if (!spr || spr->height != want) return false;
    }
    {
        const auto* k = sm::sub::slot_kind(objs, sBandit);
        if (!k || k->factionIdx != 3) return false;
    }
    {
        const auto* k = sm::sub::slot_kind(objs, sWrap);
        if (!k || k->factionIdx != 2) return false;
    }

    // Placement: each projection lands in ITS window cell's sub-region (never
    // outside the composite window). Centre → [kC,2kC); +1,0 → [2kC,3kC); the
    // torus-wrapped -1,0 → [0,kC).
    auto in = [](float v, float lo, float hi) { return v >= lo && v < hi; };
    {
        const auto& pb = *sm::sub::slot_pos(objs, sBandit);
        if (!in(pb.x, kC, 2 * kC) || !in(pb.y, kC, 2 * kC)) return false;
        const auto& pp = *sm::sub::slot_pos(objs, sPeasant);
        if (!in(pp.x, 2 * kC, 3 * kC) || !in(pp.y, kC, 2 * kC)) return false;
        const auto& pw = *sm::sub::slot_pos(objs, sWrap);
        if (!in(pw.x, 0.0f, kC) || !in(pw.y, kC, 2 * kC)) return false;
    }

    // ИДЕМПОТЕНТНОСТЬ ДВЕРИ (SUB-2) — НОСИТЕЛЬ ЗАКОНА, КОТОРОГО НЕ БЫЛО.
    // Шов зовёт эту дверь СНОВА на каждом ре-центре (ровно за этим она и
    // идемпотентна), и второй зов по тому же миру обязан вернуть НОЛЬ: сквад,
    // чья проекция уже стоит, не воплощается вторым телом. Закон был записан
    // в коде абзацем и не охранялся ничем — мутация «гейт идемпотентности
    // выключен» проходила 127/127 ctest и смоуки ЗЕЛЁНОЙ, потому что ни один
    // прогон не звал дверь по одному миру дважды (замер M-260, 2026-10-13).
    {
        const int again = sm::sub::project_macro_npcs_into_subworld(
            objs, *worldStore_, mgr, kCenterCx, kCenterCy, kMapW, kMapH, kSeed,
            sm::sub::WATER_LEVEL);
        if (again != 0) return false;   // второй зов не рождает ни одного тела
        int standing = 0;
        // Свип плоской арены (ломоть D): гейт — ГОДНЫЙ бэклинк шва.
        for (int slot = 0; slot < int(sm::kMaxSubObjects); ++slot) {
            if ((objs.flags[std::size_t(slot)] & sm::sub::kObjAlive) == 0u)
                continue;
            if (sm::sub::slot_macro_origin(objs, slot).id != 0)
                ++standing;
        }
        if (standing != 3) return false;   // и не удваивает уже стоящих
    }

    const std::vector<std::array<float, 3>> fp1 = projection_fingerprint(objs);

    // Reaper skip: clear_subworld_world_entities wipes ordinary fauna but SPARES
    // the projections (they mirror persistent macro state, like the player squad).
    sm::sub::clear_subworld_world_entities(objs);
    int faunaAfter = 0, projAfter = 0;
    {
        // Свип плоской арены (ломоть D). Снарядный гейт обязателен: ветка
        // `else` считает ВСЕХ, кто не проекция, а снарядного слота
        // entity-вьюха не видела (сущности у снаряда нет).
        for (int slot = 0; slot < int(sm::kMaxSubObjects); ++slot) {
            if ((objs.flags[std::size_t(slot)] & sm::sub::kObjAlive) == 0u)
                continue;
            if ((objs.flags[std::size_t(slot)] & sm::sub::kObjProjectile) != 0u)
                continue;
            if (sm::sub::slot_macro_origin(objs, slot).id != 0)
                ++projAfter;
            else ++faunaAfter;
        }
    }
    if (faunaAfter != 0 || projAfter != 3) return false;

    // Determinism: an identical macro set + same centre + same seed reproduces
    // the same projected scene bit-for-bit (the re-entry guarantee).
    auto world2Store_ = sm::make_macro_store();
    auto world2Arena_ = sm::test::make_scene_arena();
    sm::sub::SubObjects& objs2 = *world2Arena_;   // арена сцены — предусловие рождения тел (кусок 1, §8 п.11)
    seed_macro_npcs(*world2Store_, kMapW);
    const int projected2 = sm::sub::project_macro_npcs_into_subworld(
        objs2, *world2Store_, mgr, kCenterCx, kCenterCy, kMapW, kMapH, kSeed,
        sm::sub::WATER_LEVEL);
    if (projected2 != 3) return false;
    const std::vector<std::array<float, 3>> fp2 = projection_fingerprint(objs2);
    if (fp1 != fp2) return false;

    return true;
}

// («ВСЕЛЕНИЕ = ПЕРЕНОС ФЛАЖКА», owner 2026-09-12.) Two cases stood here — one
// for the exit-remap query, one for the identity-adoption door — and both doors
// were cut with the ceremony they belonged to. The claim they guarded is alive
// and is guarded END TO END by the `subworld_exit_remap` smoke, which possesses
// a projected lord, leaves through the real teardown, and asserts the law as it
// stands since 2026-09-14: BOTH flags move at the moment of taking (the scene
// one onto the body, the macro one onto its record), and he then climbs out
// where he STOOD — the window centre, not the lord's own cell — still being the
// lord. That is a better witness than these were — it exercises the path the
// game runs instead of two functions in isolation.

} // namespace

// ПРИЗРАК ХЭНДЛА НЕ СМЕЕТ ПРИКРЫВАТЬ ЖИВУЮ ЗАПИСЬ, ЗАНЯВШУЮ ЕГО СЛОТ.
// Плоскость бита идемпотентности (M-260) ставит бит ТОЛЬКО по ГОДНОМУ хэндлу
// (`st.valid` — слот жив И поколение совпало), и это не украшение: слоты
// стора переиспользуются фрилистом стеком (`store_death` кладёт слот назад и
// бампает поколение, `store_birth` снимает его первым). Значит тело, чья
// запись умерла, продолжает СТОЯТЬ в сцене с протухшим хэндлом на тот же
// слот — и бит «по слоту» закрыл бы им НОВУЮ запись, то есть живой сквад не
// воплотился бы никогда, молча.
//
// Свидетеля у этого закона не было: мутация «бит по слоту без `valid`»
// проходила 127/127 ЗЕЛЁНОЙ (замер M-260, 2026-10-13), потому что ни один
// прогон не рождал призрака. Предусловие — призрак и перерождение слота —
// рождается ЗДЕСЬ, а не ожидается от генератора (§8 п.11).
bool run_ghost_origin_case(const sm::sub::SeamlessSubworldManager& mgr) {
    constexpr int kMapW = 1024, kMapH = 1024;
    constexpr int kCenterCx = 0, kCenterCy = 0;
    constexpr std::uint32_t kSeed = 0x9A17C0DEu;

    auto storeMem = sm::make_macro_store();
    auto arena_ = sm::test::make_scene_arena();
    sm::sub::SubObjects& objs = *arena_;   // арена — предусловие рождения тел
    const MacroSeeds s = seed_macro_npcs(*storeMem, kMapW);
    sm::MacroStore& st = *storeMem;

    // Трое в окне встают телами (четвёртый за окном) — как в кейсе проекции.
    if (sm::sub::project_macro_npcs_into_subworld(
            objs, st, mgr, kCenterCx, kCenterCy, kMapW, kMapH, kSeed,
            sm::sub::WATER_LEVEL) != 3) return false;

    auto count_origins = [&](bool wantValid) {
        int n = 0;
        // Свип плоской арены (ломоть D): гейт — наличие бэклинка шва.
        for (int slot = 0; slot < int(sm::kMaxSubObjects); ++slot) {
            if ((objs.flags[std::size_t(slot)] & sm::sub::kObjAlive) == 0u)
                continue;
            const sm::MacroHandle h = sm::sub::slot_macro_origin(objs, slot);
            if (h.id == 0) continue;
            if (st.valid(h) == wantValid) ++n;
        }
        return n;
    };
    if (count_origins(/*wantValid*/true) != 3) return false;

    // ПРИЗРАК: запись бандита умирает, его тело со сцены никто не убирал.
    sm::store_death(st, s.bandit);
    if (st.valid(s.bandit)) return false;            // хэндл протух
    if (count_origins(/*wantValid*/true) != 2) return false;
    if (count_origins(/*wantValid*/false) != 1) return false;   // он и есть призрак

    // ПЕРЕРОЖДЕНИЕ ТОГО ЖЕ СЛОТА: фрилист — стек, значит слот возвращается
    // первым. Если это перестанет быть правдой, сценарий обязан сказать об
    // этом вслух, а не проверять пустоту.
    sm::MacroHandle reborn = sm::store_birth(st);
    if (reborn.slot != s.bandit.slot) return false;  // не тот слот — нет сценария
    if (reborn.id == s.bandit.id) return false;      // ординал обязан отличаться
    // Фикстура пинит ординал нового жильца (ожидаемая запись 77 ниже);
    // хэндл `reborn` после этого валидность не несёт — колонки пишутся слотом.
    sm::store_rename_ordinal(st, reborn, 77u);
    reborn = sm::handle_at(st, reborn.slot);
    st.cell[reborn.slot] = sm::ecs::MacroCell{
        sm::ecs::cell_index(kCenterCx, kCenterCy, kMapW)};
    st.kind[reborn.slot] = sm::ecs::NPCKind{
        std::uint16_t(sm::NPCType::Guard), 2};
    st.pools[reborn.slot] = sm::ecs::Pools{9, 9};
    st.sheet[reborn.slot].levelData.level = 2;   // уровень — анкета (M-246а)
    {
        sm::ecs::NpcCharacter ch{};
        ch.visualSeed = 0xFEEDu;
        ch.bodyShape = 3;
        st.character[reborn.slot] = ch;
    }

    // ЗАКОН: новая запись воплощается, призрак её не прикрыл. Двое уже
    // стоящих при этом не удваиваются — тот же гейт идемпотентности.
    if (sm::sub::project_macro_npcs_into_subworld(
            objs, st, mgr, kCenterCx, kCenterCy, kMapW, kMapH, kSeed,
            sm::sub::WATER_LEVEL) != 1) return false;
    if (count_origins(/*wantValid*/true) != 3) return false;
    if (count_origins(/*wantValid*/false) != 1) return false;   // призрак так и стоит
    return true;
}

int main() {
    sm::sub::SeamlessSubworldManager mgr;
    mgr.init(0, 0, meadow_cell);
    mgr.consume_composite_dirty();

    // ── Per-cell fauna parity: the centre cell's ECS entities must match the
    // TS-derived roll, now scattered within the centre sub-region only. ──
    const sm::sub::CellContext centre = meadow_cell(0, 0);
    auto centreArena_ = sm::test::make_scene_arena();
    sm::sub::SubObjects& centreObjs = *centreArena_;   // арена сцены — предусловие рождения тел (кусок 1, §8 п.11)
    spawn_cell_at(centreObjs, mgr, /*ox*/0, /*oy*/0, /*absCx*/0, /*absCy*/0);

    const std::vector<SpawnRecord> expected =
        expected_cell_fauna(mgr, sm::Biome::Meadow,
                            sm::SquadType::None, /*danger*/0, 0, 0,
                            centre.seed, 0);
    const std::vector<SpawnRecord> actual = actual_fauna(centreObjs);
    const int cmp = compare_records(expected, actual);
    if (cmp != 0) {
        return cmp;
    }

    const bool ok1 = !(!run_water_blocked_squad_case());
    if (!ok1) {
    }
    CHECK(ok1, "player squad spawned on an all-water traversability grid");
    if (!ok1) return 1;

    const bool ok2 = !(!sheet_lifts_every_body(centreObjs));
    if (!ok2) {
    }
    CHECK(ok2, "a wild body came up without a sheet, or weaker than the "
               "row it is made of (CANON.md S14)");
    if (!ok2) return 1;

    const bool ok3 = !(!run_population_does_not_scale_bodies_case(mgr));
    if (!ok3) {
    }
    CHECK(ok3, "a settlement's population moved a body's LEVEL "
               "(auto-level by town size is deleted — CANON.md S12)");
    if (!ok3) return 1;

    const bool ok4 = !(!run_city_population_projection_case(mgr));
    if (!ok4) {
    }
    CHECK(ok4, "city population projection missing roles or off-cell");
    if (!ok4) return 1;

    const bool ok5 = !(!run_carry_across_case(mgr));
    if (!ok5) {
    }
    CHECK(ok5, "seam crossing did not carry/evict/respawn cells correctly");
    if (!ok5) return 1;

    const bool ok6 = !(!run_reentry_determinism_case(mgr));
    if (!ok6) {
    }
    CHECK(ok6, "per-cell fauna respawn is not deterministic from its seed");
    if (!ok6) return 1;

    const bool ok7 = !(!run_beast_member_projection_case(mgr));
    if (!ok7) {
    }
    CHECK(ok7, "a BEAST in a creatures did not come down as its own row "
               "(squads carry men and monsters alike — CANON.md S4/S16)");
    if (!ok7) return 1;

    const bool ok8 = !(!run_macro_projection_case(mgr));
    if (!ok8) {
    }
    CHECK(ok8, "macro->subworld projection wrong "
               "(window/wrap/backlink/hostility/hp/placement/reaper/determinism)");
    if (!ok8) return 1;

    const bool ok9 = !(!run_ghost_origin_case(mgr));
    if (!ok9) {
    }
    CHECK(ok9, "призрак мёртвой записи прикрыл живую, занявшую его слот "
               "(бит идемпотентности обязан стоять на ГОДНОМ хэндле)");
    if (!ok9) return 1;

    std::printf("OK subworld_spawn_parity_test fauna=%zu seed=%u no_autolevel=%d "
                "water_squad_blocked=1 city_projection=1 carry_across=1 "
                "reentry_determinism=1 macro_projection=1 ghost_origin=1 "
                "identity_remap=1\n",
                actual.size(), centre.seed, 1);
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

    // ── The garrison partition (§42 Инк 4/5, CANON S28) ──
    // Pure arithmetic law: street throng + Σ storey shares == population,
    // soul for soul, for any population and any storey count — the same
    // shares the street reserve subtracts and each storey embodies. The
    // storeys keep pop >> crowdInsideShift (the owner's eye: «снаружи
    // больше сотни, внутри десятки на ярус»); a remainder must land on
    // the LOWER floors, never vanish.
    {
        const int pops[] = {0, 1, 3, 5, 100, 383, 8192};
        bool holds = true;
        for (int pop : pops) {
            for (int storeys = 1; storeys <= 5 && holds; ++storeys) {
                const int insideTotal =
                    pop >> sm::landmark_def(sm::SquadType::Spire)
                               .crowdInsideShift;
                const int outside = pop - insideTotal;
                int inside = 0;
                for (int level = 0; level < storeys; ++level) {
                    inside += sm::sub::interior_garrison_share(
                        sm::SquadType::Spire, pop, storeys, level);
                }
                holds = (outside + inside == pop);
            }
            if (!holds) break;
        }
        CHECK(holds, "throng + storeys == the spire's population, always");
        CHECK(sm::sub::interior_garrison_share(sm::SquadType::Spire,
                                               100, 3, 0)
                  >= sm::sub::interior_garrison_share(sm::SquadType::Spire,
                                                      100, 3, 2),
              "a share remainder lands on the lower floors, never vanishes");
    }

    // ── THE SEAM CARRIES THE RECORD, NOT A NAMESAKE ─────────────────────
    //
    // Owner's verdict, 2026-09-12 (CANON, «ШВА АНКЕТЫ» + «ЗЕРКАЛО ДЛЯ ВСЕХ»):
    // what crosses into the subworld is the CHARACTER, and it crosses as an
    // ADDRESS, not as a copy. Two things are asserted, and each has a real way
    // to fail: the man who arrives is the SAME MAN (his own stored sheet, not a
    // fresh roll of his row from the cell's seed, which is what the projection
    // used to do), and he OWNS NOTHING down here — с 0a ломтя 7 это держит
    // сама ДВЕРЬ state_of: арм самозаписи снесён, дверь отвечает только
    // записью (бывший предикат tracked_body_owns_nothing умер с армом).
    //
    // The second claim used to be its mirror image — «the copy is complete» —
    // and inverting it is the landing: with no copy there is no fold-up to
    // forget, which is the bug class SAVE-1 and the frozen-AI fold both came
    // from.
    {
        auto worldStore_ = sm::make_macro_store();
        auto worldArena_ = sm::test::make_scene_arena();
        sm::sub::SubObjects& objs = *worldArena_;   // арена сцены — предусловие рождения тел (кусок 1, §8 п.11)

        sm::MacroStore& stq = *worldStore_;
        sm::MacroHandle hq = sm::store_birth(stq);
        sm::store_rename_ordinal(stq, hq, 11u);
        hq = sm::handle_at(stq, hq.slot);
        stq.kind[hq.slot] = sm::ecs::NPCKind{
            std::uint16_t(sm::NPCType::Bandit), std::uint16_t(3)};
        stq.pools[hq.slot] = sm::ecs::Pools{10, 10};
        stq.sheet[hq.slot].levelData.level = 7;   // уровень — анкета (M-246а)

        // HIS sheet — deliberately NOT the one his row and level would roll
        // from the cell seed, so "he arrived as himself" is a claim that can
        // come out false.
        sm::CharacterSheet own =
            sm::make_character_sheet(sm::NPCType::Bandit, 7, 0xA11CEu);
        own.attributes[sm::AttributeId::Str] =
            std::uint8_t(own.attributes.of(sm::AttributeId::Str) + 7);
        stq.sheet[hq.slot] = own;

        const int sBody = sm::sub::spawn_tracked_body(
            objs, stq, hq, 100.0f, 100.0f, /*seed*/0xD1FFu,
            /*combatant*/true);
        CHECK(sBody >= 0,
              "the fixture must actually project a body");

        CHECK(sBody >= 0
                  && sm::sub::slot_state<sm::ecs::NpcInventory>(objs, sBody, stq)
                         == &stq.inventory[hq.slot],
              "a projected body owns none of it: the door answers with the "
              "RECORD's bag and nothing else");

        // (Негативный контроль «сумка контрабандой на теле» умер ломтём D
        // ВМЕСТЕ С ВЫРАЗИМОСТЬЮ нарушения: у тела больше нет сущности, а у
        // арены нет колонки сумки — подложить двери «своё владение» стало
        // несобираемым. Запрет арма самозаписи держится ПО ПОСТРОЕНИЮ, и
        // лучшая форма свидетеля — ровно эта несобираемость, §8 п.6.)
        const auto* carried = sBody >= 0
            ? sm::sub::slot_sheet(objs, sBody) : nullptr;
        CHECK(carried != nullptr, "a body always has a sheet");
        CHECK(carried && carried->attributes.of(sm::AttributeId::Str)
                  == own.attributes.of(sm::AttributeId::Str),
              "the man who walks into the subworld is the man the world "
              "remembers, not a fresh roll of his row");

        // The negative control, asserted so the detector is known to work: a
        // body the world keeps NO record of is still drawn from its seed, so
        // the check above is reading a real difference and not a tautology.
        sm::sub::BodySpec anon{};
        anon.type = sm::NPCType::Bandit;
        anon.level = 7;
        anon.seed = 0xD1FFu;
        const int sStranger =
            sm::sub::spawn_derived_body(objs, anon, /*faceSalt*/0u);
        const auto* strangerSheet = sStranger >= 0
            ? sm::sub::slot_sheet(objs, sStranger) : nullptr;
        CHECK(strangerSheet != nullptr, "a derived body has a sheet");
        CHECK(strangerSheet && strangerSheet->attributes.of(sm::AttributeId::Str)
                  != own.attributes.of(sm::AttributeId::Str),
              "a body with no record is NOT the lord — otherwise the identity "
              "check above could not fail");
    }

    // ── THE DOOR OF THE SEAM: WHOSE RECORD IS THIS BODY? ────────────────
    //
    // Mirror law, owner 2026-09-12 («ЗЕРКАЛО ДЛЯ ВСЕХ»): a body owns nothing
    // and every reader asks sub/record.h whose state it is holding. The claim
    // that can fail is ADDRESS EQUALITY — not «the numbers look alike», which
    // two copies also satisfy, but «it is literally the same block», which is
    // the only thing that makes a fold-up unnecessary. The same form
    // map_subject_test uses for store_of/creatures_of.
    //
    // Both births are asserted, because the door has exactly two honest
    // answers and a test that only exercised one would not notice the day it
    // grew a third.
    {
        auto worldStore_ = sm::make_macro_store();
        auto worldArena_ = sm::test::make_scene_arena();
        sm::sub::SubObjects& objs = *worldArena_;   // арена сцены — предусловие рождения тел (кусок 1, §8 п.11)

        sm::MacroStore& stq = *worldStore_;
        sm::MacroHandle hq = sm::store_birth(stq);
        sm::store_rename_ordinal(stq, hq, 12u);
        hq = sm::handle_at(stq, hq.slot);
        stq.kind[hq.slot] = sm::ecs::NPCKind{
            std::uint16_t(sm::NPCType::Bandit), std::uint16_t(3)};
        stq.pools[hq.slot] = sm::ecs::Pools{40, 40};
        stq.sheet[hq.slot].levelData.level = 5;   // уровень — анкета (M-246а)

        // Both bodies are created BEFORE any pointer is taken: a spawn
        // reallocates component storage, and a reference held across one is
        // the project's standing grabla (ecs-ref-not-across-tick).
        const int sBody = sm::sub::spawn_tracked_body(
            objs, stq, hq, 64.0f, 64.0f, /*seed*/0x5EEDu,
            /*combatant*/false);
        sm::sub::BodySpec anon{};
        anon.type = sm::NPCType::Bandit;
        anon.level = 5;
        anon.seed = 0x5EEDu;
        const int sCitizen =
            sm::sub::spawn_derived_body(objs, anon, /*faceSalt*/1u);

        CHECK(sBody >= 0 && sCitizen >= 0,
              "the fixture must project both kinds of body");

        CHECK(sBody >= 0 && sm::sub::slot_macro_record(objs, sBody, stq) == hq,
              "a projected body's state belongs to the record it projects");
        CHECK(sCitizen >= 0
                  && sm::sub::slot_macro_record(objs, sCitizen, stq)
                         == sm::MacroHandle{},
              "a body nothing above remembers answers for itself — the other "
              "honest birth, not a fallback");

        CHECK(sBody >= 0
                  && sm::sub::slot_pools_of(objs, sBody, stq)
                         == &stq.pools[hq.slot],
              "the bars a projected body spends are LITERALLY the record's "
              "block — one memory, so there is nothing to fold back up");
        CHECK(sCitizen >= 0
                  && sm::sub::slot_pools_of(objs, sCitizen, stq)
                         == &(*sm::sub::slot_pools(objs, sCitizen)),
              "a derived body spends its own bars");

        // NEGATIVE CONTROL, asserted: strip the backlink and the very same
        // body answers with its own block instead. Without this the address
        // equality above could be passing because both handles happen to name
        // the same storage, and nobody would know.
        sm::sub::slot_set_origin(objs, sBody, sm::MacroHandle{});
        CHECK(sBody >= 0
                  && sm::sub::slot_macro_record(objs, sBody, stq)
                         == sm::MacroHandle{}
                  && sm::sub::slot_pools_of(objs, sBody, stq)
                         == &(*sm::sub::slot_pools(objs, sBody)),
              "without the backlink the door answers SELF — the detector "
              "above is reading a real difference");

        // A record reaped out from under a standing body degrades to self,
        // never to nothing: a body with no bars at all would be an
        // invulnerable ghost, which is worse than losing the write-back.
        // Протухание — механикой store (шаг 2 1е): store_death бампает
        // поколение слота, и всякий старый хэндл мертвеет.
        sm::sub::slot_set_origin(objs, sBody, hq);
        sm::store_death(stq, hq);
        CHECK(sBody >= 0
                  && sm::sub::slot_macro_record(objs, sBody, stq)
                         == sm::MacroHandle{}
                  && sm::sub::slot_pools_of(objs, sBody, stq)
                         == &(*sm::sub::slot_pools(objs, sBody)),
              "a stale backlink degrades to the body itself, not to null");
    }

    // ── THE RE-DERIVE IS GATED ON «DID IT CHANGE» ───────────────────────
    //
    // The other half of the mirror: a body's bars are a block copy, but its
    // SWING is a sheet re-roll, and CANON's measurement says that costs five
    // times as much (0.00196 ms/body — 12.5 % of a frame at a thousand bodies).
    // So it runs only when what stands on the record actually changed, and
    // `BonusTotals::operator==` is the question asked.
    //
    // What is asserted here is the GATE, because the gate is the whole cost
    // argument: it must say NO on a body nobody touched, and YES the tick after
    // the record changed. Both arms can fail — an always-yes gate is the
    // expensive bug, an always-no gate is the stale one.
    {
        auto worldStore_ = sm::make_macro_store();
        auto worldArena_ = sm::test::make_scene_arena();
        sm::sub::SubObjects& objs = *worldArena_;   // арена сцены — предусловие рождения тел (кусок 1, §8 п.11)

        sm::MacroStore& stq = *worldStore_;
        sm::MacroHandle hq = sm::store_birth(stq);
        sm::store_rename_ordinal(stq, hq, 13u);
        hq = sm::handle_at(stq, hq.slot);
        stq.kind[hq.slot] = sm::ecs::NPCKind{
            std::uint16_t(sm::NPCType::Bandit), std::uint16_t(3)};
        stq.pools[hq.slot] = sm::ecs::Pools{50, 50};
        stq.sheet[hq.slot].levelData.level = 6;   // уровень — анкета (M-246а)

        const int sBody = sm::sub::spawn_tracked_body(
            objs, stq, hq, 32.0f, 32.0f,
            /*seed*/0xBEEFu, /*combatant*/true);
        CHECK(sBody >= 0, "the fixture projects a body");
        if (sBody < 0) return sm::test::report(
            "subworld_spawn_parity_test");

        CHECK(!sm::sub::refresh_body_strike(objs, &stq, sBody),
              "nothing stood on him and nothing changed: the gate refuses, so "
              "a standing crowd costs one comparison and no sheet rolls");

        // His clock, set to something recognisable BEFORE the rebuild: a
        // re-derive must not hand a busy arm a free swing.
        sm::sub::slot_combat(objs, sBody)->recoverySteps = 7u;

        // Now something stands on him. An AFFIX carries the bonus rows
        // directly, so the claim does not rest on which catalog row happens to
        // grant what today — the strength is authored right here.
        {
            auto& gear = stq.gear[hq.slot].gear;
            auto& bag = stq.inventory[hq.slot].inv;
            sm::ItemRef ring{};
            ring.def = 0;
            ring.count = 1;
            ring.set_affix(0, sm::Bonus{std::uint8_t(sm::BonusId::Str),
                                        std::int16_t(6)});
            // Прямое авторство фикстуры (M-183): вещь В ИНВЕНТАРЕ, ячейка —
            // индекс; дверь equip тут не годится — строка 0 не носима, а
            // сила утверждения именно в аффиксе, не в строке каталога.
            int free = -1;
            for (int i = 0; i < sm::kMaxInventorySlots; ++i) {
                if (bag.slots[std::size_t(i)].empty()) { free = i; break; }
            }
            CHECK(free >= 0, "the lord's bag has an empty slot for the ring");
            CHECK(bag.put_at(free, ring),
                  "...и дверь адреса её туда кладёт (M-296: слот снаружи "
                  "не пишется)");
            gear.worn[0] = std::uint16_t(free);
        }
        CHECK(sm::sub::refresh_body_strike(objs, &stq, sBody),
              "what stands on the record changed: the gate opens");
        CHECK(!sm::sub::refresh_body_strike(objs, &stq, sBody),
              "...and closes again immediately — it re-cached what it saw, so "
              "one change costs one rebuild, not one per tick");
        CHECK(sm::sub::slot_combat(objs, sBody)->recoverySteps == 7u,
              "the rebuild kept his clock: how busy a hand is, is not one of "
              "the numbers a sheet decides");
    }

    // ── D-ХВОСТ (2026-10-04): ЖИЛЕЦ ИНТЕРЬЕРА — ГОЛОВА СВОЕГО ОТРЕЗКА ──
    //
    // Вердикт владельца («что есть в инвентаре, то чел и увидит»): род,
    // уровень и займ жильца — ФАКТ ГОЛОВЫ [segmentStart, segmentStart+count)
    // той же последовательности, которую улица пропускает резервом; ролл
    // рода мёртв. Фикстура кладёт четыре РАЗЛИЧИМЫХ ПО ПОЗИЦИИ рода —
    // экзотику, которой полоса толпы города не роллила бы никогда, — и
    // спрашивает тела отрезка [1, 3). Ожидание читается ИЗ ВХОДА
    // (creatures_range — это определение последовательности, не копия
    // логики под судом: судятся прогулка отрезка, род и займ).
    {
        auto segArena_ = sm::test::make_scene_arena();
        sm::sub::SubObjects& objs = *segArena_;   // арена сцены — предусловие рождения тел (кусок 1, §8 п.11)
        sm::Inventory inv{};
        CHECK(sm::creatures_push_stack(inv, sm::NPCType::Wolf,  2, 1)
                  && sm::creatures_push_stack(inv, sm::NPCType::Witch, 5, 1)
                  && sm::creatures_push_stack(inv, sm::NPCType::Bear,  3, 1)
                  && sm::creatures_push_stack(inv, sm::NPCType::Deer,  1, 1),
              "фикстура: четыре головы легли");
        std::vector<sm::CreatureRef> heads;
        for (const sm::CreatureRef h : sm::creatures_range(inv)) {
            heads.push_back(h);
        }
        CHECK(heads.size() == 4, "последовательность видит все четыре головы");
        CHECK(heads.size() == 4
                  && heads[0].kind != heads[1].kind
                  && heads[1].kind != heads[2].kind
                  && heads[2].kind != heads[3].kind
                  && heads[0].kind != heads[2].kind
                  && heads[1].kind != heads[3].kind,
              "роды различимы по позиции — иначе съехавший отрезок невидим");
        std::vector<sm::sub::StandPoint> floorCat;
        for (int i = 0; i < 4; ++i) {
            floorCat.push_back({std::uint16_t(4 + i), std::uint16_t(4), 0u});
        }
        sm::MacroStockKey segKey{};
        segKey.subject = 77;
        const int placed = sm::sub::spawn_dungeon_residents(
            objs, 0xD00Au, /*faction*/0, /*count*/2, /*segmentStart*/1,
            &inv, floorCat, 0.0f, 0.0f, segKey, /*combatant*/false);
        CHECK(placed == 2, "отрезок [1, 3) дал ровно два тела");
        int bodies = 0;
        bool sawSecond = false, sawThird = false;
        bool sawStreetHead = false, sawForeignHead = false;
        bool loansNameHeads = true;
        bool levelsAreHeadFacts = true;
        // Свип плоской арены (ломоть D): гейт — колонки займа и рода.
        for (int s = 0; s < int(sm::kMaxSubObjects); ++s) {
            if ((objs.flags[std::size_t(s)] & sm::sub::kObjAlive) == 0u)
                continue;
            const auto* debtP = sm::sub::slot_debt(objs, s);
            if (debtP == nullptr) continue;
            const auto* kindCol = sm::sub::slot_kind(objs, s);
            if (kindCol == nullptr) continue;
            const auto& kind = *kindCol;
            ++bodies;
            const auto& debt = *debtP;
            sawSecond |= kind.type == std::uint16_t(heads[1].kind);
            sawThird |= kind.type == std::uint16_t(heads[2].kind);
            sawStreetHead |= kind.type == std::uint16_t(heads[0].kind);
            sawForeignHead |= kind.type == std::uint16_t(heads[3].kind);
            loansNameHeads &= debt.detailKind == std::uint8_t(kind.type);
            // Уровень — колонка арены (кусок 1): 0 значил бы «не назначен»
            // и валит проверку, как валило отсутствие компоненты.
            const int lvl = int(sm::sub::slot_level(objs, s));
            const int wantLevel = kind.type == std::uint16_t(heads[1].kind)
                ? int(heads[1].level) : int(heads[2].level);
            levelsAreHeadFacts &= lvl == wantLevel;
        }
        CHECK(bodies == 2, "в сцене ровно тела отрезка");
        CHECK(sawSecond && sawThird,
              "род жильца — факт головы своего отрезка, не ролл полосы");
        CHECK(!sawStreetHead && !sawForeignHead,
              "уличная голова и голова чужой двери за этой дверью не стоят");
        CHECK(loansNameHeads, "займ называет род ИМЕННО этой головы — "
              "смерть спишет её, а не первую подвернувшуюся");
        CHECK(levelsAreHeadFacts, "уровень жильца — факт головы");
    }

    return sm::test::report("subworld_spawn_parity_test");
}

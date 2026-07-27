#include "sub/spawn.h"
#include "core/rng.h"
#include "ecs/components.h"
#include "macro/npc.h"
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
    std::fprintf(stderr, "FAIL subworld_spawn_parity_test: %s\n", msg);
    return 1;
}

// Canonical monster id: (0x100 | stable catalog index), mirroring the shipping
// spawn path (src/sub/spawn.cpp + src/sub/engine.cpp) and every design doc. The
// TS authority assigns fauna NO numeric id — identity is label + table row — so
// the uint16 `NPCKind.type` is a C++/ECS-only encoding of the creature's stable
// catalog row, never a label hash.
std::uint16_t catalog_type_id(const sm::sub::FaunaEntry* entry) {
    const int idx = sm::sub::creature_index(entry);
    return std::uint16_t(0x100 | (idx < 0 ? 0 : idx));
}

sm::ecs::SubworldAi::Kind ai_kind(sm::sub::FaunaAi ai) {
    return ai == sm::sub::FaunaAi::Combat ? sm::ecs::SubworldAi::Combat
         : ai == sm::sub::FaunaAi::Flee   ? sm::ecs::SubworldAi::Flee
                                          : sm::ecs::SubworldAi::Wander;
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
    c.landmarkSettlementId = -1;
    c.landmarkSize = 0;
    c.landmarkKind = sm::sub::CellLandmarkKind::None;
    c.seed = 0x24680000u
        ^ (std::uint32_t(cx) * 73856093u)
        ^ (std::uint32_t(cy) * 19349663u);
    return c;
}

std::vector<SpawnRecord> expected_fauna(
    const sm::sub::SeamlessSubworldManager& mgr,
    sm::Biome biome,
    sm::FeatureType feature,
    sm::sub::LandmarkKind landmark,
    std::uint32_t seed,
    int landmarkPop,
    int zoneLevel) {

    const sm::sub::FaunaTable& table =
        sm::sub::get_fauna_table(biome, feature, landmark);
    std::uint32_t rngState = seed ^ 0xFAEAu;
    const std::vector<sm::sub::FaunaPick> picks =
        sm::sub::roll_fauna(table, rngState);

    int levelBonus = 0;
    float hpMult = 1.0f;
    float damageMult = 1.0f;
    if (landmark == sm::sub::LandmarkKind::City
        || landmark == sm::sub::LandmarkKind::Village) {
        if (landmarkPop > 0) {
            levelBonus += int(std::floor(std::sqrt(float(landmarkPop) / 100.0f)));
        }
    }
    if (zoneLevel > 2) {
        const int zb = zoneLevel - 2;
        levelBonus += zb;
        const float boost = 1.0f + float(zb) * 0.18f;
        hpMult = boost;
        damageMult = boost;
    }

    const auto& tiles = mgr.tiles();
    const bool tilesUsable =
        tiles.size() >= std::size_t(sm::sub::kFullSize) * sm::sub::kFullSize;

    sm::Rng rng(rngState);
    std::vector<SpawnRecord> out;
    out.reserve(picks.size());

    for (const sm::sub::FaunaPick& pick : picks) {
        const sm::sub::FaunaEntry& f = *pick.entry;
        float fx = 0.0f;
        float fy = 0.0f;
        bool placed = false;
        for (int attempt = 0; attempt < 20; ++attempt) {
            fx = rng.next_f01() * float(sm::sub::kFullSize);
            fy = rng.next_f01() * float(sm::sub::kFullSize);
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
            int(f.baseLevel) + int(std::floor(rng.next_f01() * 2.0f)) + levelBonus);
        const float levelScale =
            1.0f + float(std::max(0, level - 1)) * 0.15f;
        const float hp = std::floor(f.combat.hp * hpMult * levelScale);
        const float damage =
            std::floor(f.combat.damage * damageMult * levelScale);

        SpawnRecord r{};
        r.type = catalog_type_id(pick.entry);
        r.faction = std::uint16_t(pick.faction);
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
        r.r = std::uint8_t((f.color >> 16) & 0xFFu);
        r.g = std::uint8_t((f.color >> 8) & 0xFFu);
        r.b = std::uint8_t(f.color & 0xFFu);
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
        if (e.type != a.type || e.faction != a.faction || e.level != a.level
            || e.ai != a.ai || e.kind != a.kind
            || e.r != a.r || e.g != a.g || e.b != a.b
            || !near(e.x, a.x) || !near(e.y, a.y)
            || !near(e.hp, a.hp) || !near(e.maxHp, a.maxHp)
            || !near(e.damage, a.damage) || !near(e.speed, a.speed)
            || !near(e.range, a.range) || !near(e.cooldown, a.cooldown)
            || !near(e.radius, a.radius)) {
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

bool run_water_blocked_squad_case() {
    sm::SoldierSquad squad{};
    squad.members.push_back(sm::make_soldier(
        std::uint8_t(sm::NPCType::Guard), 4, 77u));

    sm::ecs::World world{};
    std::vector<std::uint8_t> water(
        std::size_t(sm::sub::kFullSize) * sm::sub::kFullSize,
        sm::sub::TILE_WATER);
    sm::sub::spawn_player_squad(world, squad, water, 512.0f, 512.0f, 123u);

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
    sm::sub::respawn_subworld_npcs(world,
                                   sm::Biome::Meadow,
                                   sm::FT_None,
                                   sm::sub::LandmarkKind::City,
                                   mgr,
                                   0xFACEB00Cu,
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

    return count >= 24 && guards >= 2 && merchants >= 1 && woodcutters >= 1;
}

} // namespace

int main() {
    sm::sub::clear_saved_subworlds();
    sm::sub::SeamlessSubworldManager mgr;
    mgr.init(0, 0, meadow_cell);
    mgr.consume_composite_dirty();

    constexpr std::uint32_t kSeed = 0x13572468u;
    constexpr int kZoneLevel = 5;
    sm::ecs::World world{};
    sm::sub::respawn_subworld_npcs(world,
                                   sm::Biome::Meadow,
                                   sm::FT_None,
                                   sm::sub::LandmarkKind::None,
                                   mgr,
                                   kSeed,
                                   0,
                                   kZoneLevel);

    const std::vector<SpawnRecord> expected =
        expected_fauna(mgr, sm::Biome::Meadow, sm::FT_None,
                       sm::sub::LandmarkKind::None, kSeed, 0, kZoneLevel);
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

    if (!run_city_population_projection_case(mgr)) {
        sm::sub::clear_saved_subworlds();
        return fail("city population projection missing visible roles");
    }

    std::printf("OK subworld_spawn_parity_test fauna=%zu seed=%u zone=%d water_squad_blocked=1 city_projection=1\n",
                actual.size(), kSeed, kZoneLevel);
    sm::sub::clear_saved_subworlds();
    return 0;
}

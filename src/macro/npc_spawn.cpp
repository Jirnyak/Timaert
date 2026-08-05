#include "macro/npc_spawn.h"
#include "macro/faction.h"
#include "macro/npc.h"
#include "macro/npc_ai.h"
#include "macro/items.h"
#include "ecs/components.h"
#include "ecs/npc_character.h"
#include "core/torus.h"
#include "core/rng.h"
#include <cmath>
#include <cstdint>

namespace sm {

namespace {

// Thread-local Rng adapter so we can pass the existing
// `RngFn = float(*)()` API into items.cpp without rewriting it.
thread_local Rng* tl_rng = nullptr;
float tl_rng_f01() { return tl_rng ? tl_rng->next_f01() : 0.0f; }

inline bool is_land(const TerrainData& t, int mapW, int mapH, int x, int y) {
    if (t.width != mapW || t.height != mapH || !t.has_rgba_storage())
        return true;
    const int xx = wrapi(x, t.width);
    const int yy = wrapi(y, t.height);
    std::size_t idx = (std::size_t(yy) * std::size_t(t.width) + std::size_t(xx)) * 4u + 3u;
    return idx < t.rgba.size() && t.rgba[idx] >= 128;
}

struct XY { int x, y; };
XY find_valid_spawn(int cx, int cy, int radius, Rng& rng,
                    int mapW, int mapH, const TerrainData& terr,
                    int maxAttempts = 20) {
    for (int i = 0; i < maxAttempts; ++i) {
        int x = wrapi(cx + int(rng.next_u32() % std::uint32_t(radius * 2)) - radius, mapW);
        int y = wrapi(cy + int(rng.next_u32() % std::uint32_t(radius * 2)) - radius, mapH);
        if (is_land(terr, mapW, mapH, x, y)) return {x, y};
    }
    return {cx, cy};
}

void make_npc(ecs::World& w, NPCType type, std::uint16_t factionIdx,
              int x, int y, int homeId, Rng& rng, std::uint32_t& spawnIndex) {
    auto e = w.reg.create();
    w.reg.emplace<ecs::Position>(e, float(x), float(y), 0.0f);
    w.reg.emplace<ecs::VisualPos>(e, float(x), float(y), 0.0f);
    w.reg.emplace<ecs::NPCKind>(e, std::uint16_t(type), factionIdx);

    const auto& def = npc_def(type);
    int hp  = def.baseHp + int(rng.next_u32() % 15u);
    int lvl = def.baseLevel + int(rng.next_u32() % 4u);

    ecs::MacroNpcRuntime rt{};
    rt.homeSettlementId   = homeId;
    rt.targetSettlementId = -1;
    rt.targetX            = float(x);
    rt.targetY            = float(y);
    rt.state              = std::uint8_t(NPCState::Idle);
    rt.stateTimer         = 0;
    rt.teleportCooldown   = 0;
    rt.sp                 = std::int16_t(hp * 2);
    rt.visualSpeed        = 0.0f;
    rt.tickAccum          = std::uint32_t(rng.next_int(0, int(kAiTicks)));  // de-sync
    w.reg.emplace<ecs::MacroNpcRuntime>(e, rt);

    // Stable identity for possession persistence (Inc 5e-2): the Nth macro NPC
    // created gets ordinal N. Deterministic because spawn_macro_npcs walks a
    // fixed spawn sequence seeded off `worldSeed`.
    w.reg.emplace<ecs::MacroSpawnId>(e, spawnIndex++);

    // Health derived from baseHp + level jitter (matches TS `makeNpc`).
    w.reg.emplace<ecs::Health>(e, float(hp), float(hp));

    // Level + per-NPC inventory (TS `generateNpcInventory(type, lvl, rng)`).
    w.reg.emplace<ecs::NpcLevel>(e, std::int16_t(lvl));

    // TS `makeNpc`: 1-2 trait rolls, duplicates skipped.
    ecs::NpcTraits traits{};
    const std::uint8_t traitRolls =
        std::uint8_t(1u + (rng.next_u32() % 2u));
    for (std::uint8_t i = 0; i < traitRolls; ++i) {
        const std::uint8_t raw = std::uint8_t(
            rng.next_u32() % std::uint32_t(NPCTrait::Count));
        bool duplicate = false;
        for (std::uint8_t j = 0; j < traits.count; ++j) {
            duplicate = duplicate || traits.traits[j] == raw;
        }
        constexpr std::uint8_t kMaxTraits =
            std::uint8_t(sizeof(traits.traits) / sizeof(traits.traits[0]));
        if (!duplicate && traits.count < kMaxTraits) {
            traits.traits[traits.count++] = raw;
        }
    }
    w.reg.emplace<ecs::NpcTraits>(e, traits);

    ecs::NpcInventory bag{};
    tl_rng = &rng;
    auto stacks = generate_npc_inventory(int(type), lvl, &tl_rng_f01);
    tl_rng = nullptr;
    for (auto& s : stacks) bag.inv.add(s.id, s.count);
    w.reg.emplace<ecs::NpcInventory>(e, std::move(bag));

    // Per-NPC visual identity (TS `generateNpcCharacter(type)` -
    // redesigned as a compact POD seed per relaxed translation policy).
    w.reg.emplace<ecs::NpcCharacter>(e, ecs::roll_npc_character(rng, 160));
}

// A settlement's faction is its KINGDOM's faction. The resolver itself lives in
// macro/politik.h (faction_index_for_kingdom) because the subworld citizen
// spawn and the procedural quest generator need the very same answer — this
// file used to own a private copy, which is exactly how the two layers drifted
// apart. It replaced two legacy hacks at once: a latitude-band position
// heuristic (settlement_faction) that could return "barbarians" (an id no
// registry ever contained), and a first-letter id matcher that then collapsed it
// onto "bandits" — so north-eastern towns spawned bandit-faction peasants.
std::uint16_t settlement_faction_index(const GameState& gs, int kingdomIdx) {
    return faction_index_for_kingdom(gs.politik, kingdomIdx);
}

} // namespace


void spawn_macro_npcs(GameState& gs, ecs::World& w,
                      const TerrainData& terrain, std::uint32_t seed) {
    Rng rng(seed + 7777u);
    std::uint32_t spawnIndex = 0;  // deterministic ordinal, one per make_npc call
    const int mw = gs.mapW;
    const int mh = gs.mapH;
    if (mw <= 0 || mh <= 0)
        return;

    // Per-settlement spawns.
    for (auto& s : gs.settlements) {
        const std::uint16_t fIdx = settlement_faction_index(gs, s.kingdomIdx);

        int peasantCount = 2 + int(rng.next_u32() % 3u);
        for (int i = 0; i < peasantCount; ++i) {
            auto p = find_valid_spawn(s.x, s.y, 10, rng, mw, mh, terrain);
            make_npc(w, NPCType::Peasant, fIdx, p.x, p.y, s.id, rng, spawnIndex);
        }
        int woodcutterCount = 1 + int(rng.next_u32() % 2u);
        for (int i = 0; i < woodcutterCount; ++i) {
            auto p = find_valid_spawn(s.x, s.y, 12, rng, mw, mh, terrain);
            make_npc(w, NPCType::Woodcutter, fIdx, p.x, p.y, s.id, rng, spawnIndex);
        }
        if (rng.next_f01() > 0.4f) {
            auto p = find_valid_spawn(s.x, s.y, 4, rng, mw, mh, terrain);
            // A merchant is a RESIDENT, so he wears his town's colours like
            // every other citizen — there is no trade guild standing above the
            // realms. He used to be hardcoded to "timaert", which made the
            // shopkeeper of a Magica city a foreign republican.
            make_npc(w, NPCType::Merchant, fIdx, p.x, p.y, s.id, rng, spawnIndex);
        }
        int guardCount = 1 + int(rng.next_u32() % 2u);
        for (int i = 0; i < guardCount; ++i) {
            auto p = find_valid_spawn(s.x, s.y, 6, rng, mw, mh, terrain);
            make_npc(w, NPCType::Guard, fIdx, p.x, p.y, s.id, rng, spawnIndex);
        }
    }

    if (gs.settlements.empty()) return;
    const std::size_t nSet = gs.settlements.size();

    // Caravans: max(1, 0.3 * settlements)
    int caravanCount = int(nSet * 3 / 10);
    if (caravanCount < 1) caravanCount = 1;
    for (int i = 0; i < caravanCount; ++i) {
        auto& home = gs.settlements[rng.next_u32() % nSet];
        auto p = find_valid_spawn(home.x, home.y, 8, rng, mw, mh, terrain);
        // A caravan flies the flag of the town it sets out FROM (the same home
        // id it already carries), not of a guild — same rule as its merchant.
        make_npc(w, NPCType::Caravan,
                 settlement_faction_index(gs, home.kingdomIdx),
                 p.x, p.y, home.id, rng, spawnIndex);
    }

    // Bandits: 0.3 * settlements + 2
    int banditCount = int(nSet * 3 / 10) + 2;
    for (int i = 0; i < banditCount; ++i) {
        auto& ref = gs.settlements[rng.next_u32() % nSet];
        float angle = rng.next_f01() * 6.2831853f;
        int dist  = 20 + int(rng.next_u32() % 30u);
        int cx = wrapi(ref.x + int(std::lround(std::cos(angle) * dist)), mw);
        int cy = wrapi(ref.y + int(std::lround(std::sin(angle) * dist)), mh);
        auto p = find_valid_spawn(cx, cy, 15, rng, mw, mh, terrain);
        make_npc(w, NPCType::Bandit, std::uint16_t(faction_index("bandits")), p.x, p.y, -1, rng, spawnIndex);
    }

    // Witches: max(1, 0.1 * settlements)
    int witchCount = int(nSet / 10); if (witchCount < 1) witchCount = 1;
    for (int i = 0; i < witchCount; ++i) {
        auto& ref = gs.settlements[rng.next_u32() % nSet];
        float angle = rng.next_f01() * 6.2831853f;
        int dist  = 25 + int(rng.next_u32() % 35u);
        int cx = wrapi(ref.x + int(std::lround(std::cos(angle) * dist)), mw);
        int cy = wrapi(ref.y + int(std::lround(std::sin(angle) * dist)), mh);
        auto p = find_valid_spawn(cx, cy, 15, rng, mw, mh, terrain);
        std::uint16_t f = rng.next_f01() > 0.3f
                        ? std::uint16_t(faction_index("magika")) : std::uint16_t(faction_index("cults"));
        make_npc(w, NPCType::Witch, f, p.x, p.y, -1, rng, spawnIndex);
    }

    // Sorceresses: max(1, 0.05 * settlements)
    int sorcCount = int(nSet / 20); if (sorcCount < 1) sorcCount = 1;
    for (int i = 0; i < sorcCount; ++i) {
        auto& ref = gs.settlements[rng.next_u32() % nSet];
        float angle = rng.next_f01() * 6.2831853f;
        int dist  = 30 + int(rng.next_u32() % 40u);
        int cx = wrapi(ref.x + int(std::lround(std::cos(angle) * dist)), mw);
        int cy = wrapi(ref.y + int(std::lround(std::sin(angle) * dist)), mh);
        auto p = find_valid_spawn(cx, cy, 15, rng, mw, mh, terrain);
        std::uint16_t f = rng.next_f01() > 0.5f
                        ? std::uint16_t(faction_index("magika")) : std::uint16_t(faction_index("cults"));
        make_npc(w, NPCType::Sorceress, f, p.x, p.y, -1, rng, spawnIndex);
    }

    // Per-village gatherers.
    for (auto& v : gs.villages) {
        const std::uint16_t fIdx = settlement_faction_index(gs, v.kingdomIdx);
        int vPeas = 1 + int(rng.next_u32() % 3u);
        for (int i = 0; i < vPeas; ++i) {
            auto p = find_valid_spawn(v.x, v.y, 8, rng, mw, mh, terrain);
            make_npc(w, NPCType::Peasant, fIdx, p.x, p.y, v.nearestCityId, rng, spawnIndex);
        }
        if (rng.next_f01() > 0.4f) {
            auto p = find_valid_spawn(v.x, v.y, 10, rng, mw, mh, terrain);
            make_npc(w, NPCType::Woodcutter, fIdx, p.x, p.y, v.nearestCityId, rng, spawnIndex);
        }
    }
}

} // namespace sm

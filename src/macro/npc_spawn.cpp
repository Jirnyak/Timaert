#include "macro/npc_spawn.h"
#include "macro/agent_memory.h"
#include "macro/currency.h"
#include "macro/faction.h"
#include "macro/npc.h"
#include "macro/deposit_layer.h"
#include "macro/npc_ai.h"
#include "macro/squad.h"
#include "macro/items.h"
#include "ecs/components.h"
#include "ecs/npc_character.h"
#include "core/torus.h"
#include "core/rng.h"
#include <algorithm>
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

// `levelOverride > 0` pins the level (quest spawns name their difficulty);
// the level draw is consumed either way, so the boot RNG stream is untouched.
// Returns the created entity (spawn_squad decorates it with roster/orders).
entt::entity make_npc(ecs::World& w, NPCType type, std::uint16_t factionIdx,
                      int x, int y, int homeId, Rng& rng,
                      std::uint32_t& spawnIndex, int levelOverride = -1) {
    auto e = w.reg.create();
    w.reg.emplace<ecs::Position>(e, float(x), float(y), 0.0f);
    w.reg.emplace<ecs::VisualPos>(e, float(x), float(y), 0.0f);
    w.reg.emplace<ecs::NPCKind>(e, std::uint16_t(type), factionIdx);

    const auto& def = npc_def(type);
    // The same draw the jittered hp used to consume, now used as the seed of the
    // sheet that decides it — so the boot RNG stream is untouched and worlds
    // generate as before, but there is no longer a THIRD way to compute what a
    // body is worth. `baseHp + rng % 15` was that third way (the subworld
    // derives hp from the character sheet at both of its births), and the two
    // scales disagreed enough that a macro lord's wound had to be converted to
    // reach his body. Reading the same row on both layers is what makes a wound
    // crossable at all (sub/spawn.h, the tracked form).
    const std::uint32_t sheetSeed = rng.next_u32();
    int lvl = def.baseLevel + int(rng.next_u32() % 4u);
    if (levelOverride > 0) lvl = levelOverride;
    const CharacterSheet sheet = make_character_sheet(type, lvl, sheetSeed);
    const int hp = body_max_hp(sheet, def.combat);

    // Stable identity for possession persistence (Inc 5e-2): the Nth macro NPC
    // created gets ordinal N. Deterministic because spawn_macro_npcs walks a
    // fixed spawn sequence seeded off `worldSeed`. Taken BEFORE the runtime
    // block below because the march caches derive from it.
    const std::uint32_t ordinal = spawnIndex++;

    ecs::MacroNpcRuntime rt{};
    rt.homeSettlementId   = homeId;   // ONE landmark id space (v54)
    rt.targetSettlementId = -1;
    rt.targetX            = float(x);
    rt.targetY            = float(y);
    rt.state              = std::uint8_t(NPCState::Idle);
    rt.stateTimer         = 0;
    rt.teleportCooldown   = 0;
    rt.visualSpeed        = 0.0f;
    rt.tickAccum          = std::uint32_t(rng.next_int(0, int(kAiTicks)));  // de-sync
    // The march caches (maxSp/travel/marathon/pace) come from the ORDINAL
    // sheet — the one every other consumer of "the leader as a sheet" derives
    // (leader_sheet_seed: auto-resolve, level-up recompute) — NOT from the
    // birth sheet above, whose seed is an unstored RNG draw. The two sheets
    // differ by seed only; hp stays with the birth sheet so the boot RNG
    // stream and every world stays byte-identical, and cross-layer state
    // travels as FRACTIONS (wound law, fatigue) so the seams never notice.
    refresh_leader_travel_stats(
        rt, make_character_sheet(type, lvl, leader_sheet_seed(ordinal)),
        type);
    rt.sp = rt.maxSp;   // born rested
    w.reg.emplace<ecs::MacroNpcRuntime>(e, rt);

    w.reg.emplace<ecs::MacroSpawnId>(e, ordinal);

    // Every macro entity IS a squad; born alone, it is a squad of one and its
    // own leader (ecs::SquadRoster doctrine). Draws no RNG — streams untouched.
    w.reg.emplace<ecs::SquadRoster>(e);

    // Every agent can REMEMBER (macro/agent_memory.h, W2b): a bounded head,
    // born empty. 136 B × the 16384-squad cap ≈ 2.2 MiB — the whole world's
    // memory budget, by the owner's brief.
    w.reg.emplace<AgentMemory>(e);

    // Health derived from the character sheet — the same law the subworld uses.
    w.reg.emplace<ecs::Health>(e, float(hp), float(hp));

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

    // Through the ONE loot registry (macro/items.h), the same door a felled tree
    // and a dead body already pay out through. A macro entity is the one kind of
    // body that carries its bag in advance, because its belongings are STATE:
    // they are what it will still be carrying when it is embodied below.
    ecs::NpcInventory bag{};
    tl_rng = &rng;
    auto stacks = roll_loot_profile(npc_loot_id(int(type)), lvl, &tl_rng_f01);
    tl_rng = nullptr;
    for (const ItemRef& s : stacks) bag.inv.add_ref(s);
    // The PURSE (owner, W2d): money is the agent's FACTION coin, carried in
    // the same bag as everything else — a trader can pay, and killing him
    // drops his purse like any other loot. Amounts are the data row below;
    // the extra RNG draw re-rolls worlds (v31 — old saves are void anyway).
    {
        // THE purse table now lives beside the row it describes
        // (macro/npc.h kNpcPurse), because the subworld's derived bodies pay
        // out of it too (damage-door Inc 5) — a macro merchant and the corpse
        // of a merchant below are one creature and answer with one number.
        const NpcPurseRow& purse = npc_purse(type);
        const int coins = purse.min
            + int(rng.next_u32() % std::uint32_t(purse.max - purse.min + 1));
        bag.inv.add(currency_for_faction_id(faction_id_for_index(factionIdx)),
                    coins);
    }
    w.reg.emplace<ecs::NpcInventory>(e, std::move(bag));

    // Per-NPC visual identity (TS `generateNpcCharacter(type)` -
    // redesigned as a compact POD seed per relaxed translation policy).
    w.reg.emplace<ecs::NpcCharacter>(e, ecs::roll_npc_character(rng, 160));
    return e;
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
                      const TerrainData& terrain, std::uint32_t seed,
                      const DepositLayer* deposits) {
    Rng rng(seed + 7777u);
    // Genesis of the ordinal stream (v23): the boot spawn starts the ONE
    // persistent counter at zero; every later runtime spawn continues it and
    // it rides the save, so an identity is never issued twice in a world's
    // whole life.
    gs.nextMacroSpawnOrdinal = 0;
    std::uint32_t& spawnIndex = gs.nextMacroSpawnOrdinal;
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
            make_npc(w, NPCType::Peasant, fIdx, p.x, p.y, v.id, rng,
                     spawnIndex);
        }
        if (rng.next_f01() > 0.4f) {
            auto p = find_valid_spawn(v.x, v.y, 10, rng, mw, mh, terrain);
            // The home-link fix (W2b): a village woodcutter is the VILLAGE's
            // man — his haul lands in the village store, not a city's.
            make_npc(w, NPCType::Woodcutter, fIdx, p.x, p.y, v.id, rng,
                     spawnIndex);
        }
        // Village-context professions (resources.md): a live vein inside the
        // gatherer reach raises ITS profession — the same table row the AI
        // works by, so presence of ore IS the presence of miners. One man
        // per kind present; specialisation stays context, never a type.
        if (deposits) {
            constexpr struct { DepositKind kind; NPCType type; } kMineRoles[] = {
                {DepositKind::Iron,  NPCType::Miner},
                {DepositKind::Stone, NPCType::Quarryman},
                {DepositKind::Clay,  NPCType::ClayDigger},
            };
            for (const auto& role : kMineRoles) {
                bool near = false;
                for (const auto& [idx, remaining]
                     : deposits->cells[std::size_t(role.kind)]) {
                    if (remaining <= 0) continue;
                    const float dsq = torus_dist_sq(
                        float(int(idx % std::uint32_t(mw))),
                        float(int(idx / std::uint32_t(mw))),
                        float(v.x), float(v.y), float(mw), float(mh));
                    if (dsq <= float(kGathererReach) * float(kGathererReach)) {
                        near = true;
                        break;
                    }
                }
                if (!near) continue;
                auto p = find_valid_spawn(v.x, v.y, 10, rng, mw, mh, terrain);
                make_npc(w, role.type, fIdx, p.x, p.y, v.id, rng,
                         spawnIndex);
            }
        }
    }
}

bool spawn_npc_at(GameState& gs, ecs::World& w, const TerrainData& terrain,
                  const char* typeToken, int x, int y, int level) {
    NPCType type{};
    if (!npc_type_from_label(typeToken, type)) return false;
    if (gs.mapW <= 0 || gs.mapH <= 0) return false;

    // Deterministic from the world seed and the named cell — independent of
    // when in the session the event arrives.
    Rng rng(hash3(std::uint32_t(x), std::uint32_t(y),
                  gs.worldSeed ^ 0x51AE57u));
    const XY p = find_valid_spawn(wrapi(x, gs.mapW), wrapi(y, gs.mapH),
                                  6, rng, gs.mapW, gs.mapH, terrain);

    // The possession-identity ordinal (MacroSpawnId) comes from the ONE
    // persistent counter (v23). The old max-over-living scan reissued a dead
    // NPC's ordinal — the 19.24 hole; runtime spawns now persist in the macro
    // snapshot, so the identity has to be for life.

    // Faction: an overworld-aggressive type is an outlaw ("bandits", exactly
    // like the boot spawner's bandit pool); every civil type belongs to the
    // realm whose LAND it stands on — the same "земля решает" rule the
    // subworld spawner uses.
    const std::uint16_t f = npc_def(type).ai == AIBehaviour::Aggressive
        ? std::uint16_t(faction_index("bandits"))
        : faction_index_for_cell(gs.politik, p.x, p.y);

    make_npc(w, type, f, p.x, p.y, /*homeId*/ -1, rng,
             gs.nextMacroSpawnOrdinal, level);
    return true;
}

entt::entity spawn_squad(GameState& gs, ecs::World& w,
                         const TerrainData& terrain, const SquadSpec& spec) {
    if (gs.mapW <= 0 || gs.mapH <= 0) return entt::null;

    // Deterministic from the world seed and the named cell, like spawn_npc_at.
    Rng rng(hash3(std::uint32_t(spec.x), std::uint32_t(spec.y),
                  gs.worldSeed ^ 0x50AD5EEDu));
    const XY p = find_valid_spawn(wrapi(spec.x, gs.mapW),
                                  wrapi(spec.y, gs.mapH),
                                  4, rng, gs.mapW, gs.mapH, terrain);

    // Ordinals from the ONE persistent counter (v23) — the max-over-living
    // scan and its 19.24 reuse hole are gone.
    const std::uint16_t f = spec.factionIndex >= 0
        ? std::uint16_t(spec.factionIndex)
        : faction_index_for_cell(gs.politik, p.x, p.y);

    const entt::entity leader =
        make_npc(w, spec.leaderType, f, p.x, p.y, spec.homeSettlementId,
                 rng, gs.nextMacroSpawnOrdinal, spec.leaderLevel);

    // The roster rows — through the same append every other producer uses.
    auto& roster = w.reg.get<ecs::SquadRoster>(leader);
    for (const SoldierRecord& r : spec.members) {
        if (!valid_npc_kind(r.kind)) continue;
        if (!roster.squad.push(make_soldier(r.kind, r.level, r.entityId))) {
            break;   // the ceiling refuses out loud (macro/army.h)
        }
    }

    // A route, only if the spec actually gives one (opt-in like the
    // component; its presence is the order).
    if (spec.waypointCount > 0) {
        ecs::SquadOrders orders{};
        orders.waypointCount = std::uint8_t(
            std::min<int>(spec.waypointCount, 8));
        orders.waypoints = spec.waypoints;
        w.reg.emplace<ecs::SquadOrders>(leader, orders);
    }
    return leader;
}

int raise_deserter_bands(GameState& gs, ecs::World& w,
                         const TerrainData& terrain, int day) {
    SoldierSquad& pool = gs.deserterPool;
    if (pool.empty() || gs.mapW <= 0 || gs.mapH <= 0) return 0;

    // √(pool) men walk off today — the garrison's own law, applied to the pile
    // (see the header for why this needs no rate constant). At least one, never
    // more than the pool holds: the pool is the only bound.
    const int poolSize = pool.size();
    const int take = std::min(poolSize,
                              std::max(1, int(std::sqrt(float(poolSize)))));

    // The freshest arrivals leave first — the men of the last rout, still
    // together, walk off before the old hands who have been drifting for weeks.
    SoldierSquad band{};
    for (int i = poolSize - take; i < poolSize; ++i) band.push(pool[i]);
    pool.count = std::int32_t(poolSize - take);

    // Slot 0 is the leader, always (CANON.md S4): the strongest man of the
    // group is the one the rest follow. Ties break on the earlier record so the
    // choice is deterministic.
    int best = 0;
    for (int i = 1; i < band.size(); ++i) {
        if (band[i].level > band[best].level) best = i;
    }
    const SoldierRecord captain = band[best];
    band.remove_at(best);

    // WHERE is not the pool's question (header): uniform land today, the blood
    // field tomorrow — this is the single line that changes then.
    Rng rng(hash3(std::uint32_t(day), gs.worldSeed, 0xDE5E27u));
    const XY site = find_valid_spawn(int(rng.next_u32() % std::uint32_t(gs.mapW)),
                                     int(rng.next_u32() % std::uint32_t(gs.mapH)),
                                     /*radius*/8, rng, gs.mapW, gs.mapH, terrain);

    SquadSpec spec{};
    spec.leaderType = valid_npc_kind(captain.kind)
        ? NPCType(captain.kind) : NPCType::Bandit;
    spec.leaderLevel = captain.level;
    spec.x = site.x;
    spec.y = site.y;
    // A leaderless armed man is an outlaw — deserters fly no realm's colours.
    spec.factionIndex = faction_index("bandits");
    spec.members = std::move(band);

    if (spawn_squad(gs, w, terrain, spec) == entt::null) {
        // The map refused the spawn: the men go back, because the pool is a
        // conservation law and a failed roll may not eat anybody.
        pool.push(captain);
        add_squad(pool, spec.members);
        return 0;
    }
    return take;
}

} // namespace sm

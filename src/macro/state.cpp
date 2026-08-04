// Faithful port of state.ts factories: defaultPlayer / createGameState /
// createFactions.  Faction relations sampled deterministically from `seed`
// via the band system in state.ts (ALLY / WAR / HOSTILE_LIGHT / NEUTRAL).
#include "macro/state.h"
#include "macro/faction.h"
#include "macro/map_generator.h"
#include "macro/language.h"
#include "core/rng.h"
#include "core/torus.h"
#include <array>

namespace sm {

// ── Factions ───────────────────────────────────────────────────
// The registry (macro/faction.h) is the single source of truth: one row per
// faction — kingdoms included — with temperament, colour, description and the
// player-reputation seed. Relations come from ONE place: an authored pair
// override or the temperament matrix, sampled per world seed. The legacy
// split (universal list + kingdom list + resolve_band id-chain) is gone.

static int sample_band(Rng& rng, RelationBand b) {
    return b.lo + int(rng.next_f01() * float(b.hi - b.lo + 1));
}

// ── createFactions ─────────────────────────────────────────────
void create_factions(GameState& gs, std::uint32_t seed) {
    gs.factions.clear();

    for (const FactionDef& d : kFactionDefs) {
        Faction f;
        f.id = d.id; f.name = d.name; f.description = d.description;
        f.color = d.color;
        gs.factions.emplace(f.id, std::move(f));
    }

    // Symmetric relation matrix sampled from `seed`, in REGISTRY order (stable
    // and explicit — the old code iterated a std::map, so inserting a faction
    // reshuffled every sampled relation after it alphabetically).
    Rng rng{seed ^ 0x9e3779b9u};
    for (int i = 0; i < kFactionCount; ++i) {
        for (int j = i + 1; j < kFactionCount; ++j) {
            const int rel = sample_band(rng, faction_band(i, j));
            gs.factions[kFactionDefs[i].id].relations[kFactionDefs[j].id] = rel;
            gs.factions[kFactionDefs[j].id].relations[kFactionDefs[i].id] = rel;
        }
        gs.factions[kFactionDefs[i].id].relations[kFactionDefs[i].id] = 100;
    }

    // The player's row is the one pair set NOT sampled from a temperament band:
    // a new game must open with the standing the registry declares (bandits and
    // demons already want him dead, cults are wary, the realms are indifferent),
    // and play moves it from there. Written after the sampling loop and in both
    // directions, so the matrix answers the same number whichever way it is
    // asked. This is also the extension seam: a new faction states its opening
    // stance toward the player in its own playerReputation column — one column,
    // no code anywhere.
    for (int i = 0; i < kFactionCount; ++i) {
        const FactionDef& d = kFactionDefs[i];
        if (std::strcmp(d.id, kPlayerFactionId) == 0) continue;
        gs.factions[kPlayerFactionId].relations[d.id] = d.playerReputation;
        gs.factions[d.id].relations[kPlayerFactionId] = d.playerReputation;
    }
}

// ── defaultPlayer (state.ts) ───────────────────────────────────
PlayerState default_player() {
    PlayerState p;
    p.name      = "Traveller";
    p.ageDays   = 1000;             // ~3 years; matches TS
    p.gold      = 1000;
    p.sheet.attributes = default_attributes();
    p.sheet.skills     = default_skills();
    p.sheet.perks      = default_perks();
    p.sheet.levelData  = default_level_data();
    p.combatStats = calculate_combat_stats(p.sheet.attributes, p.sheet.skills);
    p.army        = default_squad();

    // Starter inventory: 2 healing potions + 5 bread.
    p.inventory.add("potion_hp",  2);
    p.inventory.add("food_bread", 5);

    // Starter spellbook: magic_bolt.
    spellbook_learn(p.spellBook, "magic_bolt");

    p.codexUnlocked = {"cosmology", "attributes", "perks_skills", "market", "settlements"};

    // No reputation seeding here any more: the player's standing IS his row in
    // the relation matrix, so it is seeded where that matrix is built
    // (create_factions) rather than on a map of his own.
    return p;
}

// ── createGameState (state.ts) ─────────────────────────────────
GameState default_game_state(std::uint32_t seed, int mapW, int mapH,
                             const LayerParameters& mapParams,
                             int cityCountTarget) {
    GameState gs;
    gs.version      = kSaveVersion;
    gs.worldSeed    = seed;
    gs.mapW         = mapW;
    gs.mapH         = mapH;
    gs.mapParams    = mapParams;
    gs.mapParams.seed = float(seed % 100000u);
    gs.cityCountTarget = cityCountTarget;
    gs.worldTime    = WorldTime{1, 8, 0};   // day 1, 08:00
    gs.subState     = GameSubState{};       // Exploring
    gs.deserterPool = default_squad();
    gs.player       = default_player();
    create_factions(gs, seed);
    return gs;
}

// ── Politik → landmark bridge ────────────────────────────────
//
// `generate_politik()` knows where capitals + cities sit but not how
// they relate to the player-facing `Settlement` / `Village` records the
// rest of the macro tick + UI consume. This helper closes that loop:
//
//   1. Each politik city becomes a `Settlement` (id = index, naming
//      from the kingdom language, default mood Stable, garrison empty,
//      economy state with one local resource roll based on biome).
//   2. Each settlement spawns 1–3 satellite villages on land cells in
//      a small ring (4–14 cells away) — same kingdom, smaller pop.
//   3. Markers refreshed so the codex / overlay tooltip / quest engine
//      see the new POIs.
//
// All deterministic via `gs.worldSeed`. Idempotent: clears prior lists.
void populate_landmarks_from_politik(GameState& gs,
                                     const TerrainData& terrain,
                                     std::uint8_t seaLevel8) {
    gs.settlements.clear();
    gs.villages.clear();
    gs.spires.clear();
    if (!terrain.has_rgba_storage()) {
        return;
    }

    Rng rng(gs.worldSeed ^ 0xC1A05E1Du);

    auto land_at = [&](int x, int y) -> bool {
        int wx = wrapi(x, terrain.width);
        int wy = wrapi(y, terrain.height);
        return !terrain.is_water(wx, wy, seaLevel8);
    };

    const auto& cities = gs.politik.cities;
    gs.settlements.reserve(cities.size());

    for (std::size_t i = 0; i < cities.size(); ++i) {
        const City& c = cities[i];
        Settlement s{};
        s.id          = int(i);
        s.x           = c.x;
        s.y           = c.y;
        s.kingdomIdx  = c.kingdomIdx;
        s.population  = c.population > 0 ? c.population
                                         : 200 + int(rng.next_u32() % 800u);
        s.mood        = SettlementMood::Stable;
        s.garrison    = default_squad();
        // Pick economy archetype from biome temp + moisture readings.
        int wx = wrapi(c.x, terrain.width);
        int wy = wrapi(c.y, terrain.height);
        std::uint8_t m = terrain.moisture_at(wx, wy);
        std::uint8_t t = terrain.temperature_at(wx, wy);
        std::vector<ResourceId> local;
        if (m > 160) local.push_back(ResourceId::Grain);
        if (t < 96)  local.push_back(ResourceId::Iron);
        if (t > 180) local.push_back(ResourceId::Clay);
        if (local.empty()) local.push_back(ResourceId::Grain);
        s.eco = create_economy_state(local);
        s.economy = (m > 160) ? "farming"
                  : (t < 96)  ? "mining"
                  : (t > 180) ? "crafting"
                  : "trade";
        // Naming via the owning kingdom's procedural language.
        if (c.kingdomIdx >= 0
            && c.kingdomIdx < int(gs.politik.kingdoms.size())) {
            s.name = !c.name.empty()
                ? c.name
                : generate_name(gs.politik.kingdoms[c.kingdomIdx].language, rng);
        } else {
            s.name = !c.name.empty() ? c.name : "Outpost";
        }
        gs.settlements.push_back(std::move(s));
    }

    // ── Villages: 1–3 per settlement, scattered on land within a ring.
    int villageId = 0;
    for (const auto& s : gs.settlements) {
        const int n = 1 + int(rng.next_u32() % 3u);   // 1..3
        for (int v = 0; v < n; ++v) {
            int vx = s.x;
            int vy = s.y;
            bool placed = false;
            for (int attempt = 0; attempt < 18; ++attempt) {
                float ang = rng.next_f01() * 6.2831853f;
                int   dist = 4 + int(rng.next_u32() % 11u);  // 4..14 cells
                int   tx = wrapi(s.x + int(std::cos(ang) * float(dist)),
                                 gs.mapW);
                int   ty = wrapi(s.y + int(std::sin(ang) * float(dist)),
                                 gs.mapH);
                if (land_at(tx, ty)) { vx = tx; vy = ty; placed = true; break; }
            }
            if (!placed) continue;
            Village vil{};
            vil.id            = villageId++;
            vil.x             = vx;
            vil.y             = vy;
            vil.kingdomIdx    = s.kingdomIdx;
            vil.population    = 30 + int(rng.next_u32() % 90u);   // 30..119
            vil.mood          = SettlementMood::Stable;
            vil.nearestCityId = s.id;
            vil.lastTradeDay  = 0;
            vil.eco           = create_economy_state({ResourceId::Grain});
            if (s.kingdomIdx >= 0
                && s.kingdomIdx < int(gs.politik.kingdoms.size())) {
                vil.name = generate_name(
                    gs.politik.kingdoms[s.kingdomIdx].language, rng);
            } else {
                vil.name = "Hamlet";
            }
            gs.villages.push_back(std::move(vil));
        }
    }
}

} // namespace sm

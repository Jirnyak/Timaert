// Faithful port of state.ts factories: defaultPlayer / createGameState /
// createFactions.  Faction relations sampled deterministically from `seed`
// via the band system in state.ts (ALLY / WAR / HOSTILE_LIGHT / NEUTRAL).
#include "macro/state.h"
#include "macro/econ_day.h"
#include "macro/currency.h"
#include "macro/faction.h"
#include "macro/map_generator.h"
#include "macro/language.h"
#include "macro/settlement_score.h"
#include "core/rng.h"
#include "core/torus.h"
#include <algorithm>
#include <array>
#include <vector>

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
    // Start money: imperial coin until the chargen names a homeland — then
    // apply_intro_story_result re-mints it into the realm's own currency.
    p.inventory.add("coin_empire", 1000);
    p.sheet.attributes = default_attributes();
    p.sheet.skills     = default_skills();
    p.sheet.perks      = default_perks();
    p.sheet.levelData  = default_level_data();
    p.combatStats = calculate_combat_stats(p.sheet.attributes, p.sheet.skills);
    p.army        = default_squad();

    // Starter inventory: 2 healing potions + 5 bread.
    p.inventory.add("potion_hp",  2);
    p.inventory.add("bread", 5);

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
    gs.worldTime    = world_time_at(1, 8, 0);   // day 1, 08:00
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
                                     std::uint8_t seaLevel8,
                                     TreeLayer& trees,
                                     const DepositLayer& deposits) {
    gs.settlements.clear();
    gs.villages.clear();
    gs.spires.clear();
    if (!terrain.has_rgba_storage()) {
        return;
    }

    Rng rng(gs.worldSeed ^ 0xC1A05E1Du);

    const auto& cities = gs.politik.cities;
    gs.settlements.reserve(cities.size());

    for (std::size_t i = 0; i < cities.size(); ++i) {
        const City& c = cities[i];
        Settlement s{};
        s.id          = int(i);
        s.x           = c.x;
        s.y           = c.y;
        s.kingdomIdx  = c.kingdomIdx;
        // Politik prices every city's souls from its ground (R2); the old
        // 200+rng%800 fallback was the last population dice standing.
        s.population  = std::max(1, c.population);
        s.mood        = SettlementMood::Stable;
        s.garrison    = default_squad();
        // Born mid-life (owner): the market has wares on day one, and the
        // town has stocks to live on while the first caravans find their legs.
        // (The old EconomyState "archetype" strings died with it, W2b-4 —
        // what a town actually HAS now lives in this one inventory.)
        seed_landmark_inventory(
            s.inventory, s.population, EconSite::City,
            currency_for_faction_id(faction_id_for_index(std::uint16_t(
                faction_index_for_kingdom(gs.politik, s.kingdomIdx)))));
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

    // ── Villages: resources decide (R2). Politics placed the cities;
    // each city's hinterland — half the city spacing, the land nearest
    // to it by construction — is scanned WHOLE, every candidate priced
    // by THE settlement score, and villages take the best sites in
    // order. The COUNT derives from the hinterland's summed capacity (a
    // lush delta feeds more mouths than a dry steppe, a barren one
    // feeds none), the POPULATION from the chosen site's own score. The
    // old dice — 1+rng%3 villages, 18 blind darts whose only criterion
    // was "land", 30+rng%90 souls — are dead; rng still names things.
    SettlementSiteContext site{};
    site.w.gs      = &gs;
    site.w.trees   = &trees;
    site.w.terrain = &terrain;
    site.deposits  = &deposits;
    site.seaLevel8 = seaLevel8;
    const int spacing = derive_city_spacing(&terrain, seaLevel8,
                                            gs.mapW, gs.mapH,
                                            int(cities.size()));
    const int reach = std::max(4, spacing / 2);

    struct Candidate { int score, x, y; };
    std::vector<Candidate> cands;
    cands.reserve(std::size_t(2 * reach + 1) * std::size_t(2 * reach + 1));

    int villageId = 0;
    for (const auto& s : gs.settlements) {
        cands.clear();
        long long hinterlandCapacity = 0;
        for (int dy = -reach; dy <= reach; ++dy) {
            for (int dx = -reach; dx <= reach; ++dx) {
                // The town works its own kSettlementReach ring; a village
                // starts beyond it.
                if (std::max(std::abs(dx), std::abs(dy)) <= kSettlementReach)
                    continue;
                const int x = wrapi(s.x + dx, gs.mapW);
                const int y = wrapi(s.y + dy, gs.mapH);
                const int score = settlement_site_score(
                    site, SettlementScoreRow::Village, x, y);
                if (score < 0) continue;   // vetoed ground
                hinterlandCapacity += score;
                cands.push_back(Candidate{score, x, y});
            }
        }
        // Capacity says how many the land feeds; a city with ANY admissible
        // ground keeps at least one village (owner: a town with no hamlet
        // at all reads as a bug, not as honesty).
        const int n = std::max<long long>(1, std::min<long long>(
            kMaxVillagesPerCity,
            hinterlandCapacity / kVillageCapacityQuota));
        if (cands.empty()) continue;
        // Best sites first; ties resolved by scan order (y, then x) so
        // the pick is a fact of the world data, not of the sort.
        std::stable_sort(cands.begin(), cands.end(),
                         [](const Candidate& a, const Candidate& b) {
                             return a.score > b.score;
                         });
        // Villages DIVIDE the city's land rather than share one pocket of
        // it: the separation is half the hinterland, so the k best sites
        // land in different quarters of the ring and scatter AROUND the
        // town instead of stacking a block apart in the single fattest
        // corner. A crowded-out candidate is skipped, not downgraded —
        // the next-best free site wins, which is what spreads them.
        const int separation = village_separation(reach);
        int placed = 0;
        for (const Candidate& c : cands) {
            if (placed >= n) break;
            // Torus distance against every village so far — neighbouring
            // hinterlands may touch at the rim.
            bool crowded = false;
            for (const auto& other : gs.villages) {
                const int ddx = std::min(std::abs(c.x - other.x),
                                         gs.mapW - std::abs(c.x - other.x));
                const int ddy = std::min(std::abs(c.y - other.y),
                                         gs.mapH - std::abs(c.y - other.y));
                if (std::max(ddx, ddy) < separation) {
                    crowded = true;
                    break;
                }
            }
            if (crowded) continue;
            Village vil{};
            vil.id            = villageId++;
            vil.x             = c.x;
            vil.y             = c.y;
            vil.kingdomIdx    = s.kingdomIdx;
            // The land feeds as many as it yields: souls ARE the score.
            vil.population    = std::max(kVillagePopFloor, c.score);
            vil.mood          = SettlementMood::Stable;
            vil.nearestCityId = s.id;
            seed_landmark_inventory(
                vil.inventory, vil.population, EconSite::Village,
                currency_for_faction_id(faction_id_for_index(std::uint16_t(
                    faction_index_for_kingdom(gs.politik, vil.kingdomIdx)))));
            if (s.kingdomIdx >= 0
                && s.kingdomIdx < int(gs.politik.kingdoms.size())) {
                vil.name = generate_name(
                    gs.politik.kingdoms[s.kingdomIdx].language, rng);
            } else {
                vil.name = "Hamlet";
            }
            gs.villages.push_back(std::move(vil));
            ++placed;
        }
    }
}

} // namespace sm

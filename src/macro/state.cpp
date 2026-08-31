// Faithful port of state.ts factories: defaultPlayer / createGameState /
// createFactions.  Faction relations sampled deterministically from `seed`
// via the band system in state.ts (ALLY / WAR / HOSTILE_LIGHT / NEUTRAL).
#include "macro/state.h"
#include "macro/codex.h"
#include "macro/econ_day.h"
#include "macro/currency.h"
#include "macro/faction.h"
#include "macro/map_generator.h"
#include "macro/language.h"
#include "macro/npc_ai.h"          // kGathererReach — the field's press radius
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
    gs.relations = RelationMatrix{};
    claim_registry_slots(gs.relations);

    // Symmetric relation matrix sampled from `seed`, in REGISTRY order (stable
    // and explicit — the old code iterated a std::map, so inserting a faction
    // reshuffled every sampled relation after it alphabetically). Each pair is
    // written once now: set_relation owns the symmetry, so no caller can set
    // one direction and forget the other.
    Rng rng{seed ^ 0x9e3779b9u};
    for (int i = 0; i < kFactionCount; ++i) {
        for (int j = i + 1; j < kFactionCount; ++j) {
            set_relation(gs.relations, i, j, sample_band(rng, faction_band(i, j)));
        }
    }

    // The player's row is the one pair set NOT sampled from a temperament band:
    // a new game must open with the standing the registry declares (bandits and
    // demons already want him dead, cults are wary, the realms are indifferent),
    // and play moves it from there. This is also the extension seam: a new
    // faction states its opening stance toward the player in its own
    // playerReputation column — one column, no code anywhere.
    const FactionSlot player = faction_slot(gs.relations, kPlayerFactionId);
    for (int i = 0; i < kFactionCount; ++i) {
        if (i == player) continue;
        set_relation(gs.relations, player, i, kFactionDefs[i].playerReputation);
    }
}

// ── defaultPlayer (state.ts) ───────────────────────────────────
PlayerState default_player() {
    PlayerState p;
    p.name      = "Traveller";
    p.ageDays   = 1000;             // ~3 years; matches TS
    // Start money: imperial coin until the chargen names a homeland — then
    // apply_intro_story_result re-mints it into the realm's own currency.
    // (The starter kit is dealt into his BAG — an ordinary NpcInventory on
    // his squad entity — by the world boot, once that entity exists. A
    // PlayerState cannot carry goods any more; it is not a container.)
    p.sheet.attributes = default_attributes();
    p.sheet.skills     = default_skills();
    p.sheet.perks      = default_perks();
    p.sheet.levelData  = default_level_data();
    p.combatStats = calculate_combat_stats(p.sheet.attributes, p.sheet.skills);



    // Starter spellbook: magic_bolt.
    spellbook_learn(p.spellBook, spell_ordinal("magic_bolt"));

    p.codexUnlockedBits = kCodexInitialUnlockBits;

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
    // Same % 100000 decimation as the boot path (main.cpp): UI heritage —
    // five on-screen digits keep naming the same worlds they always did.
    gs.mapParams.seed = seed % 100000u;
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
                                     DepositLayer& deposits) {
    gs.landmarks.clear();
    if (!terrain.has_rgba_storage()) {
        return;
    }

    Rng rng(gs.worldSeed ^ 0xC1A05E1Du);

    const auto& cities = gs.politik.cities;
    gs.landmarks.reserve(cities.size());

    for (std::size_t i = 0; i < cities.size(); ++i) {
        const City& c = cities[i];
        Landmark s{};
        s.type        = LandmarkType::City;
        // v54: ONE landmark id space — the ordinal issuer, never the loop index.
        s.id          = int(gs.nextLandmarkOrdinal++);
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
            s.inventory, s.population, EconSite(landmark_def(s.type).econSite),
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
        // The capital's landmark ID (owner 2026-08-31): this loop is the
        // one place that knows which landmark politik city `i` became —
        // stamp the kingdom's suzerain edge here, never resolve it by
        // coordinates again.
        if (s.kingdomIdx >= 0
            && s.kingdomIdx < int(gs.politik.kingdoms.size())
            && gs.politik.kingdoms[std::size_t(s.kingdomIdx)]
                       .capitalCityIdx == int(i)) {
            gs.politik.kingdoms[std::size_t(s.kingdomIdx)]
                .capitalLandmarkId = s.id;
        }
        gs.landmarks.push_back(std::move(s));
    }

    // ── Villages: the settlement FIELD decides (owner 2026-08-31,
    // «полевой подход»). Politics placed the cities; each city's
    // hinterland is scanned WHOLE and every candidate priced by THE
    // settlement score — then villages OCCUPY best-first, and every
    // placed village PRESSES the field around itself
    // (village_pressure), so the next best honestly moves away. The
    // old separation rule, the capacity quota and the per-city cap
    // died into the field; placement stops when the best pressed
    // candidate falls under half this hinterland's own first-best —
    // the land's own quality bar, no absolute constant. Souls are the
    // owner's SCALE (kVillageBornBase + a seed roll), never the score.
    SettlementSiteContext site{};
    site.w.gs      = &gs;
    site.w.trees   = &trees;
    site.w.terrain = &terrain;
    site.w.deposits = &deposits;
    site.seaLevel8 = seaLevel8;
    // The deposit-reach field: the score sees what the crews mine (v71).
    const std::vector<std::uint16_t> depositReach =
        build_deposit_reach_field(deposits, gs.mapW, gs.mapH);
    site.depositReach = depositReach.empty() ? nullptr
                                             : depositReach.data();
    const int spacing = derive_city_spacing(&terrain, seaLevel8,
                                            gs.mapW, gs.mapH,
                                            int(cities.size()));
    const int reach = std::max(4, spacing / 2);

    struct Candidate { int pressed, raw, x, y; bool feeds; };
    std::vector<Candidate> cands;
    cands.reserve(std::size_t(2 * reach + 1) * std::size_t(2 * reach + 1));
    // Every village placed so far, with the RAW score of its ground —
    // what it claims and consumes is what presses the field, across
    // hinterland rims and city borders alike (one world, one field).
    struct PlacedVillage { int x, y, score; };
    std::vector<PlacedVillage> pressed;

    // Snapshot the cities before appending villages: the loop below pushes
    // into the SAME gs.landmarks vector, and a live iterator would not
    // survive the growth.
    struct CityRef { int id, x, y, kingdomIdx; };
    std::vector<CityRef> cityRefs;
    cityRefs.reserve(cities.size());
    for (const auto& lm : gs.landmarks) {
        if (lm.type == LandmarkType::City) {
            cityRefs.push_back(CityRef{lm.id, lm.x, lm.y, lm.kingdomIdx});
        }
    }

    const auto torus_cheb = [&](int ax, int ay, int bx, int by) {
        const int ddx = std::min(std::abs(ax - bx),
                                 gs.mapW - std::abs(ax - bx));
        const int ddy = std::min(std::abs(ay - by),
                                 gs.mapH - std::abs(ay - by));
        return std::max(ddx, ddy);
    };

    for (const auto& s : cityRefs) {
        cands.clear();
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
                // The SELF-FEEDING GATE (settlement_score.h): beyond the
                // forced first hamlet, a village stands only where its box
                // ploughs its born hundred or a prize vein pays the bread.
                const SettlementSiteTerms t =
                    settlement_site_terms(site, x, y);
                const bool feeds = t.arable >= kVillageArableGate
                                || t.deposit >= kVillageDepositGate;
                // Pressed once against everything already standing —
                // neighbouring hinterlands may touch at the rim.
                int v = score;
                for (const PlacedVillage& pv : pressed) {
                    const int d = torus_cheb(x, y, pv.x, pv.y);
                    if (d <= kGathererReach)
                        v -= village_pressure(pv.score, d);
                }
                cands.push_back(Candidate{v, score, x, y, feeds});
            }
        }
        if (cands.empty()) continue;
        int placedHere = 0;
        for (;;) {
            // The best PRESSED candidate that can FEED itself; ties
            // resolved by scan order so the pick is a fact of the world
            // data, not of a sort. The forced first hamlet ignores the
            // gate (owner: a town with no hamlet reads as a bug).
            int bestIdx = -1;
            for (std::size_t i = 0; i < cands.size(); ++i) {
                if (placedHere > 0 && !cands[i].feeds) continue;
                if (bestIdx < 0
                    || cands[i].pressed > cands[std::size_t(bestIdx)].pressed)
                    bestIdx = int(i);
            }
            if (bestIdx < 0) break;
            const Candidate c = cands[std::size_t(bestIdx)];
            if (placedHere > 0 && c.pressed <= 0) {
                break;   // the field is spent — the land carries no more
            }
            cands[std::size_t(bestIdx)] = cands.back();
            cands.pop_back();
            // The new village presses the remaining field around itself.
            for (Candidate& r : cands) {
                const int d = torus_cheb(r.x, r.y, c.x, c.y);
                if (d <= kGathererReach)
                    r.pressed -= village_pressure(c.raw, d);
            }
            pressed.push_back(PlacedVillage{c.x, c.y, c.raw});
            ++placedHere;
            Landmark vil{};
            vil.type          = LandmarkType::Village;
            vil.id            = int(gs.nextLandmarkOrdinal++);   // v54: same issuer
            vil.x             = c.x;
            vil.y             = c.y;
            vil.kingdomIdx    = s.kingdomIdx;
            // Souls = the owner's scale, never the score (CANON S25):
            // a hundred-odd, the ~200 tail included.
            vil.population    = kVillageBornBase
                              + int(rng.next_u32()
                                    % std::uint32_t(kVillageBornSpread));
            vil.mood          = SettlementMood::Stable;
            vil.nearestCityId = s.id;
            seed_landmark_inventory(
                vil.inventory, vil.population,
                EconSite(landmark_def(vil.type).econSite),
                currency_for_faction_id(faction_id_for_index(std::uint16_t(
                    faction_index_for_kingdom(gs.politik, vil.kingdomIdx)))));
            if (s.kingdomIdx >= 0
                && s.kingdomIdx < int(gs.politik.kingdoms.size())) {
                vil.name = generate_name(
                    gs.politik.kingdoms[s.kingdomIdx].language, rng);
            } else {
                vil.name = "Hamlet";
            }
            gs.landmarks.push_back(std::move(vil));
        }
    }
}

} // namespace sm

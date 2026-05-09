// Faithful port of state.ts factories: defaultPlayer / createGameState /
// createFactions.  Faction relations sampled deterministically from `seed`
// via the band system in state.ts (ALLY / WAR / HOSTILE_LIGHT / NEUTRAL).
#include "macro/state.h"
#include "macro/map_generator.h"
#include "macro/language.h"
#include "core/rng.h"
#include "core/torus.h"
#include <array>

namespace sm {

// ── Universal factions ─────────────────────────────────────────
struct UniversalFaction {
    const char*   id;
    const char*   name;
    const char*   description;
    std::uint32_t color;
};

static constexpr UniversalFaction kUniversalFactions[] = {
    {"cults",    "Demonic Cults",
                 "Worshippers of the Old Ones. Hunted everywhere.",          0x581c87},
    {"wildlife", "Wildlife",
                 "Beasts and roaming creatures. Indifferent to mortal politics.",
                                                                              0x6b8e23},
    {"bandits",  "Bandit Clans",
                 "Outlaws and raiders. Hostile to all civilised folk.",       0x7a3a1a},
    {"demons",   "Demonic Hordes",
                 "Forces of the abyss. War against everything.",              0x8b0000},
};

// ── Relation bands (see state.ts) ──────────────────────────────
struct Band { int lo, hi; };
static constexpr Band ALLY            = {  55,  90};
static constexpr Band WAR             = {-100, -75};
static constexpr Band HOSTILE_LIGHT   = { -50,   0};
static constexpr Band NEUTRAL_BAND    = { -50,  50};
static constexpr Band ANY_BAND        = {-100, 100};
static constexpr Band CULT_PAIR_BAND  = { -60, -20};
static constexpr Band WILD_PAIR_BAND  = { -30,  30};

static int sample_band(Rng& rng, Band b) {
    return b.lo + int(rng.next_f01() * float(b.hi - b.lo + 1));
}

// ── Pair overrides (state.ts PAIR_OVERRIDES) ───────────────────
struct PairOverride { const char* a; const char* b; Band band; };
static constexpr PairOverride kPairOverrides[] = {
    {"timaert", "northern_magica", ALLY},
    {"empire",  "lower_magica",    ALLY},
    {"timaert", "cults",           WAR },
};

static bool match_pair(const char* a, const char* b,
                       const std::string& x, const std::string& y) {
    return (x == a && y == b) || (x == b && y == a);
}

// Lineage of a kingdom by id (matches kingdom_defs()). Empty for universals.
static const Lineage* lineage_for_kingdom(const std::string& id) {
    for (const auto& k : kingdom_defs())
        if (id == k.id) return &k.lineage;
    return nullptr;
}

// resolveBand: return relation band for (a,b). a/b are faction ids.
static Band resolve_band(const std::string& a, const std::string& b) {
    // 1. PAIR_OVERRIDES
    for (const auto& p : kPairOverrides)
        if (match_pair(p.a, p.b, a, b)) return p.band;

    // 2. Universals first.
    const bool aBandit  = a == "bandits", bBandit = b == "bandits";
    const bool aDemon   = a == "demons",  bDemon  = b == "demons";
    const bool aCult    = a == "cults",   bCult   = b == "cults";
    const bool aWild    = a == "wildlife",bWild   = b == "wildlife";

    if (aBandit || bBandit || aDemon || bDemon) return WAR;
    if (aCult && bCult)   return CULT_PAIR_BAND;
    if (aWild && bWild)   return WILD_PAIR_BAND;

    const Lineage* lineageA = lineage_for_kingdom(a);
    const Lineage* lineageB = lineage_for_kingdom(b);

    // Universals (cults / wildlife) vs kingdoms
    if (aCult || bCult) {
        const Lineage* L = aCult ? lineageB : lineageA;
        if (!L) return HOSTILE_LIGHT;
        if (*L == Lineage::Magika) return WAR;            // magic vs cults
        return HOSTILE_LIGHT;
    }
    if (aWild || bWild) return WILD_PAIR_BAND;

    // 3. Kingdom-vs-kingdom by lineage.
    if (!lineageA || !lineageB) return NEUTRAL_BAND;
    const Lineage la = *lineageA, lb = *lineageB;

    // Same magica family — anything from ally to rival.
    if (la == Lineage::Magika    && lb == Lineage::Magika)    return ANY_BAND;
    // Magica vs barbarians — war.
    if ((la == Lineage::Magika    && lb == Lineage::Barbarians) ||
        (lb == Lineage::Magika    && la == Lineage::Barbarians)) return WAR;
    // Empire vs magica — uneasy / hostile.
    if ((la == Lineage::Empire    && lb == Lineage::Magika)    ||
        (lb == Lineage::Empire    && la == Lineage::Magika))    return HOSTILE_LIGHT;
    // Barbarians among themselves and vs others — anything.
    if (la == Lineage::Barbarians || lb == Lineage::Barbarians) return ANY_BAND;
    // Timaert / Empire — neutral default (mercantile / lawful).
    return NEUTRAL_BAND;
}

// ── lineageDescription (state.ts) ──────────────────────────────
static const char* lineage_description(Lineage l) {
    switch (l) {
        case Lineage::Empire:     return "Theocratic empire. Magic is forbidden.";
        case Lineage::Magika:     return "Ruled by powerful mages. High magic economy.";
        case Lineage::Barbarians: return "Feudal lords ruling by might and steel.";
        case Lineage::Timaert:    return "Maritime trade republic. Neutral and wealthy.";
    }
    return "";
}

// ── createFactions ─────────────────────────────────────────────
void create_factions(GameState& gs, std::uint32_t seed) {
    gs.factions.clear();

    // 1. Universals.
    for (const auto& u : kUniversalFactions) {
        Faction f;
        f.id = u.id; f.name = u.name; f.description = u.description;
        f.color = u.color;
        gs.factions.emplace(f.id, std::move(f));
    }

    // 2. One faction per kingdom.
    for (const auto& kd : kingdom_defs()) {
        Faction f;
        f.id = kd.id; f.name = kd.name;
        f.description = lineage_description(kd.lineage);
        f.color = kd.color_rgb;
        gs.factions.emplace(f.id, std::move(f));
    }

    // 3. Symmetric relation matrix sampled from `seed`.
    Rng rng{seed ^ 0x9e3779b9u};
    std::vector<std::string> ids;
    ids.reserve(gs.factions.size());
    for (const auto& kv : gs.factions) ids.push_back(kv.first);

    for (std::size_t i = 0; i < ids.size(); ++i) {
        for (std::size_t j = i + 1; j < ids.size(); ++j) {
            const Band band = resolve_band(ids[i], ids[j]);
            const int  rel  = sample_band(rng, band);
            gs.factions[ids[i]].relations[ids[j]] = rel;
            gs.factions[ids[j]].relations[ids[i]] = rel;
        }
        gs.factions[ids[i]].relations[ids[i]] = 100;
    }
}

// ── defaultPlayer (state.ts) ───────────────────────────────────
PlayerState default_player() {
    PlayerState p;
    p.name      = "Traveller";
    p.ageDays   = 1000;             // ~3 years; matches TS
    p.gold      = 1000;
    p.attributes = default_attributes();
    p.skills     = default_skills();
    p.perks      = default_perks();
    p.levelData  = default_level_data();
    p.combatStats = calculate_combat_stats(p.attributes, p.skills);
    p.army        = default_army();

    // Starter inventory: 2 healing potions + 5 bread.
    p.inventory.add("potion_hp",  2);
    p.inventory.add("food_bread", 5);

    // Starter spellbook: magic_bolt.
    p.spellBookSpellIds.emplace_back("magic_bolt");

    // Reputation seed (createInitialReputation in TS).
    p.reputation["bandits"] = -100;
    p.reputation["demons"]  = -100;
    p.reputation["cults"]   =  -10;
    p.reputation["wildlife"] =   0;
    for (const auto& kd : kingdom_defs())
        p.reputation[kd.id] = 0;
    return p;
}

// ── createGameState (state.ts) ─────────────────────────────────
GameState default_game_state(std::uint32_t seed, int mapW, int mapH) {
    GameState gs;
    gs.version      = kSaveVersion;
    gs.worldSeed    = seed;
    gs.mapW         = mapW;
    gs.mapH         = mapH;
    gs.worldTime    = WorldTime{1, 8, 0};   // day 1, 08:00
    gs.subState     = GameSubState{};       // Exploring
    gs.deserterPool = default_army();
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
        s.garrison    = default_army();
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

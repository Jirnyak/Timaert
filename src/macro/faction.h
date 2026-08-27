// THE faction registry — the single source of truth for every faction in the
// game: identity (id / name / description / colour), temperament, starting
// player reputation, and the default relation between any two factions.
//
// WHY THIS EXISTS (owner decision, 2026-07-30: "единая система истины, никаких
// разделений на королевства"). Before this header the faction system was split
// across FIVE parallel vocabularies, each hand-maintained and each subtly wrong:
//   • npc_faction_id_for   (sub/engine.cpp,  idx→id, 5 entries)
//   • faction_id_for_idx   (macro/npc_spawn.cpp — the SAME table duplicated,
//     with a DIFFERENT fallback: "" in one copy, "empire" in the other)
//   • fauna_faction_id_for (sub/engine.cpp,  idx→id, 4 entries, different ids
//     for the same indices — so ecs::NPCKind.factionIdx meant different
//     factions depending on a type bit, and spell friendly-fire compared the
//     raw indices across that boundary: a bandit NPC (3) and a bandit creature
//     (2) counted as enemies, a bandit NPC (3) and a DEMON (3) as brothers)
//   • FaunaFaction         (sub/fauna.h enum, a fourth spelling)
//   • faction_idx          (macro/npc_spawn.cpp, id→idx by FIRST LETTER —
//     "barbarians" and "bandits" both mapped to 3, so barbarian settlements
//     spawned bandit-faction guards)
// plus a registry split in two (kUniversalFactions + kingdom_defs — with
// "magika" emitted by the vocabularies but never registered, so all its
// relations silently read neutral) and relations decided by an if-chain over
// id strings and kingdom lineages.
//
// All of that collapses to this one table and one matrix:
//   • ONE row per faction — kingdoms are ordinary rows, not a separate class.
//   • ONE index space: ecs::NPCKind.factionIdx is an index into kFactionDefs,
//     for humanoids and monsters alike. 0xFFFF (kNoFaction) = factionless.
//   • ONE relation source: temperament × temperament → band, plus an authored
//     pair-override table. Adding a faction is ONE ROW — its relations to
//     everyone follow from its temperament, its reputation column seeds the
//     player standing, and no code anywhere changes.
//
// Header-only POD tables in .rodata; no allocation, no exceptions, no init
// order. The player side is deliberately NOT a row: the player's standing with
// every faction is the reputation map (dynamic per save), and combat resolves
// it through one callback — see SubworldEngine::battle_relation_callback.
#pragma once
#include <cstdint>
#include <cstring>

namespace sm {

// THE hostility line: a relation below this is an enemy. One number for every
// consumer — the subworld battle masks (SubworldEngine::battle_relation_
// callback), the subworld aggro checks (sub/ai.h re-exports it), and the macro
// squad threat step (npc_ai.cpp) — so the map and the ground can never
// disagree about who is at war. It lives HERE because relations live here.
// The predicate that applies it is factions_hostile (macro/state.h) — apply
// the threshold through it, not by hand.
inline constexpr int kHostileThreshold = -50;

// THE friendship line, the other end of the same scale: at or above this a
// faction is an ALLY — it cannot be provoked into a private grudge by a stray
// hit (maybe_flip_temp_hostile), and the stance colours saturate to full
// friend here. Lived as a private constant in engine.cpp while the hostile
// line lived here; the two ends of one scale belong on one shelf.
inline constexpr int kAllyRepThreshold = 50;

// ── Temperament — how a faction behaves toward strangers ───────────────────
// The relation between two factions with no authored override is a pure
// function of their temperaments (kTemperamentBands below).
enum class Temperament : std::uint8_t {
    Lawful = 0,     // theocratic order — magic is suspect
    Magical,        // mage-ruled realms and orders
    Mercantile,     // trade republics — neutral and wealthy
    Savage,         // feudal warlords — anything can happen
    Outlaw,         // raiders — at war with all civilised folk
    Abyssal,        // forces of the abyss — war against everything
    Cultist,        // hidden worshippers — hunted, and hunting mages
    Feral,          // beasts — indifferent to mortal politics
    Count
};

inline const char* temperament_label(Temperament t) {
    switch (t) {
        case Temperament::Lawful:     return "Lawful";
        case Temperament::Magical:    return "Magical";
        case Temperament::Mercantile: return "Mercantile";
        case Temperament::Savage:     return "Savage";
        case Temperament::Outlaw:     return "Outlaw";
        case Temperament::Abyssal:    return "Abyssal";
        case Temperament::Cultist:    return "Cultist";
        case Temperament::Feral:      return "Feral";
        default:                      return "?";
    }
}

// ── The registry ───────────────────────────────────────────────────────────
struct FactionDef {
    const char*   id;               // stable machine id — THE universal key
    const char*   name;
    const char*   description;
    std::uint32_t color;            // 0xRRGGBB
    Temperament   temperament;
    int           playerReputation; // new-game seed for gs.player.reputation
};

// Sentinel for "no faction" in ecs::NPCKind.factionIdx: resolves to the empty
// id, which every relation path already treats as neutral / fights-nobody.
inline constexpr std::uint16_t kNoFaction = 0xFFFFu;

// THE player's faction id — one spelling for the whole project. It names an
// ordinary registry row (see the "player" entry below), so it interns, resolves
// and compares exactly like every other faction; nothing about the player is a
// special case in the relation algorithm.
inline constexpr const char* kPlayerFactionId = "player";

inline constexpr FactionDef kFactionDefs[] = {
    // ── Universal factions ────────────────────────────────────────────────
    {"wildlife", "Wildlife",
     "Beasts and roaming creatures. Indifferent to mortal politics.",
     0x6b8e23, Temperament::Feral,   0},
    {"bandits",  "Bandit Clans",
     "Outlaws and raiders. Hostile to all civilised folk.",
     0x7a3a1a, Temperament::Outlaw,  -100},
    {"demons",   "Demonic Hordes",
     "Forces of the abyss. War against everything.",
     0x8b0000, Temperament::Abyssal, -100},
    {"cults",    "Demonic Cults",
     "Worshippers of the Old Ones. Hunted everywhere.",
     0x581c87, Temperament::Cultist, -10},
    // The wandering mage orders — previously emitted by the spawn vocabulary
    // but never registered, so every relation involving them silently read
    // neutral. A real faction now, closing that gap by construction.
    {"magika",   "Magika Orders",
     "Itinerant mages sworn to no single realm.",
     0x8b5cf6, Temperament::Magical, 0},
    // ── Kingdoms — ordinary rows; politik references them by id ───────────
    {"old_magica",      "Old Magica",
     "Ruled by powerful mages. High magic economy.",
     0xa78bfa, Temperament::Magical,    0},
    {"northern_magica", "Northern Magica",
     "Ruled by powerful mages. High magic economy.",
     0x7c3aed, Temperament::Magical,    0},
    {"lower_magica",    "Lower Magica",
     "Ruled by powerful mages. High magic economy.",
     0xc4b5fd, Temperament::Magical,    0},
    {"lake_duchy",      "Lake Duchy",
     "Ruled by powerful mages. High magic economy.",
     0x60a5fa, Temperament::Magical,    0},
    {"empire",          "Empire of Light",
     "Theocratic empire. Magic is forbidden.",
     0xf59e0b, Temperament::Lawful,     0},
    {"timaert",         "Republic of Timaert",
     "Maritime trade republic. Neutral and wealthy.",
     0x10b981, Temperament::Mercantile, 0},
    {"barbarian_north", "North Barbarians",
     "Feudal lords ruling by might and steel.",
     0x991b1b, Temperament::Savage,     0},
    {"barbarian_south", "South Barbarians",
     "Feudal lords ruling by might and steel.",
     0xb91c1c, Temperament::Savage,     0},
    {"barbarian_west",  "West Barbarians",
     "Feudal lords ruling by might and steel.",
     0xdc2626, Temperament::Savage,     0},
    {"barbarian_east",  "East Barbarians",
     "Feudal lords ruling by might and steel.",
     0xef4444, Temperament::Savage,     0},
    // ── The unruled ───────────────────────────────────────────────────────
    // Everyone who answers to no crown: a settlement no kingdom owns, a town
    // that has thrown its lord out, a landmark held by whoever lives in it.
    // This is what a place WITHOUT an owner is, and it exists so that "unowned"
    // never has to be spelled as "imperial" — the fallback that used to hand
    // every ownerless town to the Empire of Light. Mercantile: at war with
    // raiders and the abyss, wary of cults, neutral toward the realms — free
    // folk trade with everyone and bow to no one. A city whose kingdomIdx goes
    // to -1 (conquest lost, rebellion, a scripted secession) becomes theirs by
    // construction, with no code anywhere to change.
    {"freefolk",        "Free Folk",
     "Towns and holdings that answer to no crown.",
     0x94a3b8, Temperament::Mercantile, 0},
    // ── The player's own realm ────────────────────────────────────────────
    // The player is an ORDINARY ROW (owner's ruling, 2026-08-04: «пусть просто
    // будет фракция игрока в общей матрице фракций… и она и станет королевством
    // игрока»). His soldiers wear this index like any other body wears its
    // faction, and his standing with everyone is his ROW in the relation
    // matrix — the same storage every other pair uses, not a private map on the
    // side (see macro/state.h player_reputation / add_player_reputation).
    //
    // The temperament below is never consulted for HIS relations: create_factions
    // seeds the player's row from each faction's playerReputation column and play
    // moves it from there, so adding a faction sets its stance toward the player
    // in that faction's own row — one column, no code. Mercantile is the label
    // a UI shows, nothing more.
    {"player",          "Your Realm",
     "You, your household, and everyone who marches under your banner.",
     0xfacc15, Temperament::Mercantile, 100},
};
inline constexpr int kFactionCount =
    int(sizeof(kFactionDefs) / sizeof(kFactionDefs[0]));
static_assert(kFactionCount < int(kNoFaction), "sentinel must stay out of range");

// Index of a faction id, -1 for null/empty/unknown. Pointer-first: ids are
// string literals, so the common path never reaches strcmp.
inline int faction_index(const char* id) {
    if (!id || id[0] == '\0') return -1;
    for (int i = 0; i < kFactionCount; ++i)
        if (kFactionDefs[i].id == id) return i;
    for (int i = 0; i < kFactionCount; ++i)
        if (std::strcmp(kFactionDefs[i].id, id) == 0) return i;
    return -1;
}

// THE idx→id map (replaces npc_faction_id_for / fauna_faction_id_for /
// faction_id_for_idx). Out of range — including kNoFaction — degrades to the
// empty id, which every consumer treats as neutral.
inline const char* faction_id_for_index(std::uint16_t idx) {
    return idx < std::uint16_t(kFactionCount) ? kFactionDefs[idx].id : "";
}

inline const FactionDef* faction_def_by_index(std::uint16_t idx) {
    return idx < std::uint16_t(kFactionCount) ? &kFactionDefs[idx] : nullptr;
}

// ── Relations ──────────────────────────────────────────────────────────────
// A relation is sampled (per world seed) from a band [lo, hi]. The band for a
// pair is: authored pair override if one exists, else the temperament matrix.
struct RelationBand { int lo, hi; };

inline constexpr RelationBand kAllyBand      = {  55,  90};
inline constexpr RelationBand kWarBand       = {-100, -75};
inline constexpr RelationBand kHostileBand   = { -50,   0};
inline constexpr RelationBand kNeutralBand   = { -50,  50};
inline constexpr RelationBand kAnyBand       = {-100, 100};
inline constexpr RelationBand kCultPairBand  = { -60, -20};
inline constexpr RelationBand kWildPairBand  = { -30,  30};

// The matrix — symmetric by construction (asserted by faction_relations_test).
// Row/column order = Temperament order. This replaces the resolve_band()
// if-chain verbatim, including its precedence quirks:
//   • Outlaw and Abyssal are at war with everything (checked first there);
//   • Cultist fights Magical (witch hunts cut both ways), is lightly hostile
//     to everyone else INCLUDING Feral (the chain hit its cult branch before
//     its wildlife branch), and two cult factions circle each other;
//   • Feral drifts in the wildlife band against all civilised temperaments;
//   • Magical realms are anything to each other, at war with Savage, uneasy
//     with Lawful; Savage is volatile with everyone; the rest are neutral.
inline constexpr RelationBand
kTemperamentBands[int(Temperament::Count)][int(Temperament::Count)] = {
    //                Lawful        Magical       Mercantile    Savage        Outlaw    Abyssal   Cultist        Feral
    /*Lawful*/     {kNeutralBand, kHostileBand, kNeutralBand, kAnyBand,     kWarBand, kWarBand, kHostileBand,  kWildPairBand},
    /*Magical*/    {kHostileBand, kAnyBand,     kNeutralBand, kWarBand,     kWarBand, kWarBand, kWarBand,      kWildPairBand},
    /*Mercantile*/ {kNeutralBand, kNeutralBand, kNeutralBand, kAnyBand,     kWarBand, kWarBand, kHostileBand,  kWildPairBand},
    /*Savage*/     {kAnyBand,     kWarBand,     kAnyBand,     kAnyBand,     kWarBand, kWarBand, kHostileBand,  kWildPairBand},
    /*Outlaw*/     {kWarBand,     kWarBand,     kWarBand,     kWarBand,     kWarBand, kWarBand, kWarBand,      kWarBand},
    /*Abyssal*/    {kWarBand,     kWarBand,     kWarBand,     kWarBand,     kWarBand, kWarBand, kWarBand,      kWarBand},
    /*Cultist*/    {kHostileBand, kWarBand,     kHostileBand, kHostileBand, kWarBand, kWarBand, kCultPairBand, kHostileBand},
    /*Feral*/      {kWildPairBand,kWildPairBand,kWildPairBand,kWildPairBand,kWarBand, kWarBand, kHostileBand,  kWildPairBand},
};

// Authored exceptions — data, checked before the temperament matrix.
struct FactionPairOverride { const char* a; const char* b; RelationBand band; };
inline constexpr FactionPairOverride kFactionPairOverrides[] = {
    {"timaert", "northern_magica", kAllyBand},
    {"empire",  "lower_magica",    kAllyBand},
    {"timaert", "cults",           kWarBand},
};

// Band for a pair of registry indices (order-independent).
inline RelationBand faction_band(int ia, int ib) {
    if (ia < 0 || ib < 0 || ia >= kFactionCount || ib >= kFactionCount)
        return kNeutralBand;
    const char* a = kFactionDefs[ia].id;
    const char* b = kFactionDefs[ib].id;
    for (const auto& o : kFactionPairOverrides) {
        if ((std::strcmp(o.a, a) == 0 && std::strcmp(o.b, b) == 0) ||
            (std::strcmp(o.a, b) == 0 && std::strcmp(o.b, a) == 0))
            return o.band;
    }
    return kTemperamentBands[int(kFactionDefs[ia].temperament)]
                            [int(kFactionDefs[ib].temperament)];
}

} // namespace sm

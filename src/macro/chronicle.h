// THE WORLD'S CHRONICLE — what happened, as flat records the world can be
// ASKED about.
//
// Owner's rulings, 2026-08-27 (CANON.md S20.1). The event bus is the nervous
// system of the WORLD, not a channel of notifications for the interface: the
// world changes and grows, quests run, the story moves, owners change, towns
// revolt — one maximally universal and CONTEXTUAL system, built once.
//
// ── THE TEST THIS FILE EXISTS TO PASS ────────────────────────────────────
// The owner's own example. A witcher-like squad walks the villages and takes
// contracts on monsters; the monsters are actors on the same bus, and a
// monster squad that kept killing peasants nearby LEFT TRACES. The witcher
// goes and finds it himself. For that, something must be able to ask the
// world:
//
//     what happened near this village in the last thirty days?
//
// Every decision below follows from that one question:
//   · it is asked BY PLACE and BY TIME    ⇒ facts must be indexable, and a
//     string is not an index;
//   · the witcher may arrive a game week later ⇒ facts must survive the SAVE;
//   · every squad on the map will ask it  ⇒ the scan must be an ARRAY walk,
//     not a chase through heap pointers.
// A `std::string` inside the record breaks all three at once. That is why a
// fact is a POD — not to be fast, but because the living world is impossible
// otherwise. Words are DERIVED at the moment of display, from the same tables
// that already hold them (the idiom the skill tooltip already uses).
//
// ── WHO ──────────────────────────────────────────────────────────────────
// Owner: «сквады это акторные элементы макромира, основную историю творят они
// (но другие энтити тоже — ландмарки и сами клетки, ну и фракции)». So a
// subject is a KIND plus an ordinal in that kind's own id space — squads
// first, and the rest able to speak by the same record.
#pragma once

#include "core/table_guard.h"
#include "core/time.h"

#include <cstdint>
#include <vector>

namespace sm {

// ── Who a fact is about ──────────────────────────────────────────────────
enum class FactSubject : std::uint8_t {
    None = 0,
    Squad,      // ecs::MacroSpawnId — the macro world's actors, and the main
                // authors of its history
    Landmark,   // settlement / village / site id
    Cell,       // the place itself did or suffered it
    Faction,    // faction_index()
    Count
};

// ── What happened ────────────────────────────────────────────────────────
// A row, so a new kind of happening is one line and no code. The columns are
// what a READER needs to decide whether this fact interests it without
// understanding the kind: whether it is worth remembering long, and what the
// `amount` column means.
enum class FactKind : std::uint16_t {
    None = 0,
    Killed,          // subject killed object            (amount = bodies)
    Died,            // subject died                     (amount = bodies)
    Battle,          // subject fought object            (amount = casualties)
    Robbed,          // subject took from object         (amount = value)
    Traded,          // subject traded with object       (amount = value)
    Gathered,        // subject took from the land       (amount = units)
    Built,           // subject raised something         (amount = units)
    Starved,         // subject went hungry              (amount = heads)
    Revolted,        // subject rose against object      (amount = heads)
    OwnerChanged,    // object now belongs to subject
    Explored,        // subject learned a place          (amount = depth)
    Interacted,      // subject used something below     (amount = kind-specific)
    Spawned,         // subject appeared in the world
    QuestTaken,      // subject took object's contract
    QuestDone,       // subject finished it
    Count
};

struct FactKindDef {
    // MUST equal the row's index in kFactKinds (guard below the table).
    FactKind    id;
    const char* key;      // authoring id; runtime addresses by ordinal
    const char* label;    // what a human reads
    // How long this kind stays INTERESTING, in days. Not a lifetime — the ring
    // evicts by age regardless — but what a reader means by "recent" when it
    // asks about this kind. A monster's kill is news for a season; a trade is
    // stale in a week.
    std::uint16_t interestDays;
    // WHAT DOING THIS MAKES OF YOU. Owner, 2026-08-27: a nameless squad can
    // BECOME named — like a lord — once it has done enough; then its deeds
    // start being kept for good, and before that they do not.
    //
    // So renown is a column of the same table that already says how long a
    // deed is news, because both questions are "how much did this matter".
    // Killing a peasant is 1; taking a city is what a figure does.
    std::uint16_t renown;
};

inline constexpr FactKindDef kFactKinds[] = {
    //                                                    interest  renown
    {FactKind::None,         "none",          "—",              0,      0},
    {FactKind::Killed,       "killed",        "Killed",        32,      1},
    {FactKind::Died,         "died",          "Died",          32,      0},
    {FactKind::Battle,       "battle",        "Battle",        32,     10},
    {FactKind::Robbed,       "robbed",        "Robbed",        16,      2},
    {FactKind::Traded,       "traded",        "Traded",         8,      0},
    {FactKind::Gathered,     "gathered",      "Gathered",       8,      0},
    {FactKind::Built,        "built",         "Built",         64,     50},
    {FactKind::Starved,      "starved",       "Starved",       32,      0},
    {FactKind::Revolted,     "revolted",      "Revolted",      64,    100},
    // The most a single deed can make of anybody: take a place from its
    // owner, and the world knows your name from that day.
    {FactKind::OwnerChanged, "owner_changed", "Changed hands", 64,    100},
    {FactKind::Explored,     "explored",      "Explored",      64,      5},
    {FactKind::Interacted,   "interacted",    "Used",           4,      0},
    {FactKind::Spawned,      "spawned",       "Appeared",      16,      0},
    {FactKind::QuestTaken,   "quest_taken",   "Took contract", 32,      1},
    {FactKind::QuestDone,    "quest_done",    "Kept contract", 32,      5},
};
static_assert(sizeof(kFactKinds) / sizeof(kFactKinds[0])
                  == std::size_t(FactKind::Count),
              "kFactKinds must carry one row per FactKind");
static_assert(rows_in_enum_order(kFactKinds, &FactKindDef::id),
              "kFactKinds rows must stand in FactKind order");

inline constexpr const FactKindDef& fact_kind_def(FactKind k) {
    return kFactKinds[std::size_t(k)];
}

// WHEN A NOBODY BECOMES SOMEBODY — and the number is DERIVED, not chosen: it
// is the most any single deed is worth in the table above. So the rule reads
// as a sentence about the world rather than as a threshold —
//
//     you become a figure by doing ONCE what a figure does,
//     or by doing enough lesser things to add up to it.
//
// Retune a row and the bar moves with it, because the bar IS the table.
inline constexpr int renown_to_be_named() {
    int most = 0;
    for (const FactKindDef& d : kFactKinds) {
        if (int(d.renown) > most) most = int(d.renown);
    }
    return most;
}
inline constexpr int kRenownToBeNamed = renown_to_be_named();

// Is this band a figure yet? ONE number decides, so nothing can disagree with
// it — there is no flag beside the counter to fall out of step.
inline constexpr bool renown_is_named(std::uint32_t renown) {
    return renown >= std::uint32_t(kRenownToBeNamed);
}
static_assert(kRenownToBeNamed > 0,
              "if no deed is worth renown, nobody could ever become a figure");

// ── The record ───────────────────────────────────────────────────────────
//
// 32 bytes exactly, and every field a number. `day` rather than a tick: a
// chronicle is asked in DAYS ("the last thirty"), and `seq` already orders
// two facts inside one day, so a second time column would be a second answer
// to one question.
struct WorldFact {
    std::int32_t  day = 0;        // world day this happened on
    std::uint32_t seq = 0;        // global fact number; also the ring's proof
    std::uint32_t nextInCell = 0; // intrusive chain, newest→oldest, per index cell
    std::uint16_t kind = 0;       // FactKind
    std::uint8_t  subjectKind = 0;// FactSubject
    std::uint8_t  objectKind = 0; // FactSubject
    std::uint32_t subject = 0;    // ordinal in the subject kind's own space
    std::uint32_t object = 0;
    std::int16_t  x = 0, y = 0;   // the macro cell it happened on
    std::int32_t  amount = 0;     // what the kind's row says it means
};
static_assert(sizeof(WorldFact) == 32,
              "a fact is 32 bytes: it is scanned by the thousand and saved whole");

// ── TWO TIERS, ONE RECORD (owner, 2026-08-27) ────────────────────────────
//
// Dwarf Fortress keeps every historical event of every year in memory, and
// that is its known illness: a long world generates into gigabytes and then
// crawls on its own past — late versions had to add historical pruning
// because unbounded did not work. The half DF gets right is the DISTINCTION
// it draws between an event and a legend, and that is the half taken here.
//
// So: ONE record type, two places it lives.
//   · the RING — what the world is ASKED. Indexed by cell, sized to the
//     longest `interestDays` above, and it forgets. This is the witcher's
//     tier.
//   · the ANNALS — what the world REMEMBERS. Append-only, no spatial index,
//     rides the save. This is "the village was razed in year three".
//
// WHAT REACHES THE ANNALS is decided by WHO took part (owner's ruling: «вечно
// помнятся дела ИМЕНОВАННЫХ — лордов, игрока, городов; безымянная массовка
// растворяется; как в DF, история — это история ФИГУР»). In this world that
// rule is not a heuristic but a consequence: a squad IS a lord and carries an
// ordinal identity (MacroSpawnId); a landmark and a faction likewise. The
// nameless crowd — roster rows, bodies below, the peasants of a city — has no
// ordinal at all and can only appear as `amount`. So the test is simply
// whether a fact has a named participant, and the volume follows: deeds
// between figures are hundreds a day, not thousands.
inline constexpr std::uint32_t kChronicleAnnals = 1u << 20;   // 1M x 32 B = 32 MB

// The ring's size is a FORMULA with one unmeasured term, and it is written
// down rather than hidden: it must hold `longest interestDays × the world's
// facts per day`. The longest span is 64 days (the Built/Revolted/Explored
// rows); the world's daily rate has never been measured, which is exactly why
// `Chronicle::factsToday` ships as part of the contract. 2^16 records is 2 MB
// — by house rule not an argument (CANON S26) — and covers 64 days at a
// thousand facts a day. Retune it from the counter, not from feel.
inline constexpr std::uint32_t kChronicleFacts = 1u << 16;

// The index is COARSE, like the squad index it borrows its shape from: "what
// happened near here" is a neighbourhood question, so a fact belongs to an
// index cell rather than to a map cell, and a query touches 3×3 of them.
inline constexpr int kChronicleCellSize = 8;

// A participant reference is a KIND plus one bit: is this one NAMED?
//
// The bit rides in the kind byte because the record is 32 bytes and bit 7 was
// free (the kinds number five). It is a bit rather than a field for the same
// reason everything here is packed: a fact is scanned by the thousand.
inline constexpr std::uint8_t kFactSubjectNamed = 0x80u;

inline constexpr std::uint8_t fact_subject_kind(std::uint8_t packed) {
    return std::uint8_t(packed & 0x7Fu);
}
inline constexpr bool fact_subject_marked_named(std::uint8_t packed) {
    return (packed & kFactSubjectNamed) != 0u;
}
inline constexpr std::uint8_t fact_subject(FactSubject kind, bool named) {
    return std::uint8_t(std::uint8_t(kind)
                        | (named ? kFactSubjectNamed : std::uint8_t(0)));
}

// Does this fact have a NAMED participant? Then it is history. Otherwise it is
// weather: it happened, the ring will remember it for a while, and then the
// world will forget, which is what forgetting is for.
//
// A LANDMARK and a FACTION are named by construction — a city has a name the
// day it is founded. A SQUAD is not: it starts as one more band on the map and
// becomes a figure by its deeds (owner, 2026-08-27), which is why the caller
// marks the bit — only the world knows how much this band has done.
inline constexpr bool subject_is_named(std::uint8_t packed) {
    const std::uint8_t kind = fact_subject_kind(packed);
    if (kind == std::uint8_t(FactSubject::Landmark)
        || kind == std::uint8_t(FactSubject::Faction)) {
        return true;
    }
    return kind != std::uint8_t(FactSubject::None)
        && fact_subject_marked_named(packed);
}

struct Chronicle {
    std::vector<WorldFact>     ring;      // kChronicleFacts, written round
    std::vector<std::uint32_t> cellHead;  // newest fact of each index cell
    // What the world remembers for good. Append-only; capped only so a
    // runaway cannot eat the machine, and the cap is loud when it is reached
    // (annalsFull) rather than silently dropping the past.
    std::vector<WorldFact>     annals;
    bool annalsFull = false;
    std::uint32_t nextSeq = 1;            // 0 is "no fact", so counting starts at 1
    int cols = 0, rows = 0;
    // Facts written on the current day, and the day it is counting — the
    // instrument that turns kChronicleFacts from a feeling into a measurement.
    std::int32_t countingDay = -1;
    std::uint32_t factsToday = 0;

    bool ready() const { return cols > 0 && rows > 0 && !ring.empty(); }
    // The oldest sequence still in the ring. Everything below it has been
    // overwritten, which is exactly how a chain knows where to stop.
    std::uint32_t oldest_seq() const {
        return nextSeq > kChronicleFacts ? nextSeq - kChronicleFacts : 1u;
    }
};

// Size it for a map. Safe to call again; a resize forgets, which is correct —
// a chronicle of another world is not this world's past.
void chronicle_init(Chronicle& c, int mapW, int mapH);

// Write one fact. Returns its sequence number (0 if the chronicle has no
// world yet). Every field is a number, so this is a store and two links.
std::uint32_t chronicle_record(Chronicle& c, const WorldFact& fact);

// ── The question the witcher asks ────────────────────────────────────────
//
// Walks the index cells around (x, y), newest first, and hands every fact no
// older than `sinceDay` to `visit`. Returns how many it offered.
//
// The chains SELF-TRUNCATE: a link is followed only while it points at a slot
// that still holds the fact it linked to, which the sequence number proves.
// Eviction is strictly oldest-first, so the first dead link is the end of the
// live part of the chain — no unlinking, no bookkeeping, and a ring that
// wrapped away half a village's past simply stops earlier.
using FactVisitor = void (*)(void* user, const WorldFact& fact);
int chronicle_near(const Chronicle& c, int x, int y, int radiusCells,
                   std::int32_t sinceDay, FactVisitor visit, void* user);

// The same walk over the RING, newest first — for the player's journal, which
// is a view on the past rather than a place with a past of its own.
int chronicle_recent(const Chronicle& c, std::int32_t sinceDay, int limit,
                     FactVisitor visit, void* user);

// Rebuild the per-cell chains from the facts already in the ring. Used by the
// LOAD: the file carries the facts, not the links, because a link is derived
// and a stored derivative is a second truth waiting to disagree.
void chronicle_rebuild_links(Chronicle& c);

// ── ASKING IN THE TABLE'S OWN WORDS ──────────────────────────────────────
//
// "Recent" is not one number for the whole game: a killing is news for a
// season and a sale is stale in a week, and the kind's row already says so
// (`interestDays`). This asks the question the way the world means it —
// "what of THIS sort happened near here while it was still news" — so a
// reader never restates a window the table already owns.
int chronicle_near_kind(const Chronicle& c, int x, int y, int radiusCells,
                        FactKind kind, std::int32_t today,
                        FactVisitor visit, void* user);

// ── SAYING IT IN WORDS ───────────────────────────────────────────────────
//
// THE law (CANON S20.1): a fact is numbers, and the words are DERIVED at the
// moment of display. That is what makes the player's journal a VIEW on the
// world's memory rather than eight thousand sentences in the save file, and
// it is what makes localisation possible for free — the same fact says itself
// in another language by changing the tables, not the past.
//
// The chronicle does not know how the world NAMES things (a squad's name
// lives on an ECS component, a settlement's in GameState), and it must not
// learn: it is a macro-layer record of ordinals. So the caller lends it three
// resolvers. This is the whole of the "readers of facts need to see the
// world" problem, in its smallest honest form — and it is the same lending
// the witcher will do when he goes looking for the village.
struct FactNaming {
    // Each returns a display name for an ordinal, or nullptr if it cannot say
    // — in which case the sentence falls back to what it does know.
    const char* (*squad)(void* user, std::uint32_t ordinal) = nullptr;
    const char* (*landmark)(void* user, std::uint32_t id) = nullptr;
    const char* (*faction)(void* user, std::uint32_t index) = nullptr;
    void* user = nullptr;
};

// Write the fact as one line into `out` (always NUL-terminated). Returns the
// length written. A naming that resolves nothing still produces a true
// sentence — "a band killed someone here" — because a chronicle that could
// only speak about things it has names for would be silent about most of the
// world.
int fact_sentence(const WorldFact& f, const FactNaming& naming,
                  char* out, int cap);

// The ANNALS, newest first: what the world remembers about a figure, however
// long ago. `subjectKind`/`subject` of 0 means "anyone" — the whole legend.
int chronicle_annals_of(const Chronicle& c, std::uint8_t subjectKind,
                        std::uint32_t subject, int limit,
                        FactVisitor visit, void* user);

} // namespace sm

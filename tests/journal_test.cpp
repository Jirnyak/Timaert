// The player's journal — his KNOWLEDGE of the chronicle (macro/journal.h).
// Owner rulings pinned here (2026-08-28):
//   · he learns what he TOOK PART in (subject or object) wherever it happened;
//   · he learns what happened ON HIS CELL while he stood there — locality;
//   · everything else the world remembers WITHOUT him: not in the journal;
//   · the journal never forgets (his whole game) — append-only, LOUD cap;
//   · the capture is idempotent: re-asking the chronicle re-learns nothing;
//   · while he wears a possessed lord, the lord's deeds are HIS participation;
//   · a writer pays renown WITH the fact — the one deed door (macro/squad.h
//     record_deed) makes the two halves one action, and the raw chronicle
//     door pays nothing (the negative control).
#include "check.h"

#include "macro/journal.h"
#include "macro/squad.h"

#include <cstdint>

namespace {

using namespace sm;

WorldFact fact(int day, FactKind kind, std::uint8_t subjKind,
               std::uint32_t subj, int x, int y) {
    WorldFact f{};
    f.day = day;
    f.kind = std::uint16_t(kind);
    f.subjectKind = subjKind;
    f.subject = subj;
    f.x = std::int16_t(x);
    f.y = std::int16_t(y);
    f.amount = 1;
    return f;
}

void test_participation_locality_and_silence() {
    GameState gs{};
    gs.mapW = 64;
    gs.mapH = 64;
    chronicle_init(gs.chronicle, gs.mapW, gs.mapH);
    gs.player.x = 10.0f;
    gs.player.y = 10.0f;

    // (a) The player's own deed, far away — learned by PARTICIPATION.
    chronicle_record(gs.chronicle,
                     fact(1, FactKind::Killed,
                          fact_subject(FactSubject::Squad, true),
                          ecs::kPlayerSquadOrdinal, 50, 50));
    // (b) Somebody else's deed on the player's cell — learned by LOCALITY.
    chronicle_record(gs.chronicle,
                     fact(1, FactKind::Battle,
                          fact_subject(FactSubject::Squad, false),
                          777u, 10, 10));
    // (c) Somebody else's deed far away — the world knows, the player DOES
    // NOT: this is S11 applied to history, and the whole point of a journal.
    chronicle_record(gs.chronicle,
                     fact(1, FactKind::Revolted,
                          std::uint8_t(FactSubject::Landmark), 3u, 40, 40));

    player_journal_capture(gs);
    CHECK(gs.player.journal.size() == 2,
          "participation and locality are learned; the far world is not");
    CHECK(gs.player.journal[0].subject == ecs::kPlayerSquadOrdinal
              && gs.player.journal[1].subject == 777u,
          "the journal keeps the very records, in the order they happened");

    // Idempotence: the reader's cursor moved, so nothing is learned twice.
    player_journal_capture(gs);
    CHECK(gs.player.journal.size() == 2,
          "re-asking the chronicle re-learns nothing");

    // The player as the OBJECT of another's deed — his side of a done-to.
    WorldFact hit = fact(2, FactKind::Killed,
                         fact_subject(FactSubject::Squad, false), 778u,
                         30, 30);
    hit.objectKind = fact_subject(FactSubject::Squad, true);
    hit.object = ecs::kPlayerSquadOrdinal;
    chronicle_record(gs.chronicle, hit);
    player_journal_capture(gs);
    CHECK(gs.player.journal.size() == 3
              && gs.player.journal[2].object == ecs::kPlayerSquadOrdinal,
          "being done-to is participation too");

    // Walking away changes what "here" means: the same foreign cell that was
    // silent above becomes his the tick he stands on it.
    gs.player.x = 40.0f;
    gs.player.y = 40.0f;
    chronicle_record(gs.chronicle,
                     fact(3, FactKind::Battle,
                          fact_subject(FactSubject::Squad, false),
                          779u, 40, 40));
    player_journal_capture(gs);
    CHECK(gs.player.journal.size() == 4,
          "locality follows the player, not a fixed home cell");
}

void test_the_journal_never_forgets_and_the_cap_is_loud() {
    GameState gs{};
    gs.mapW = 64;
    gs.mapH = 64;
    chronicle_init(gs.chronicle, gs.mapW, gs.mapH);
    gs.player.x = 5.0f;
    gs.player.y = 5.0f;

    // Fill to the cap in slices small enough that the ring never evicts
    // between captures (the live game captures every tick, so eviction
    // between looks is not a state it can reach).
    const std::uint32_t cap = PlayerState::kJournalFactsCap;
    std::uint32_t filed = 0;
    while (filed < cap) {
        const std::uint32_t slice = std::min<std::uint32_t>(16384u,
                                                            cap - filed);
        for (std::uint32_t i = 0; i < slice; ++i) {
            chronicle_record(gs.chronicle,
                             fact(int(filed + i) / 256 + 1, FactKind::Battle,
                                  fact_subject(FactSubject::Squad, false),
                                  1000u + filed + i, 5, 5));
        }
        filed += slice;
        player_journal_capture(gs);
    }
    CHECK(gs.player.journal.size() == std::size_t(cap)
              && gs.player.journalFull == 0,
          "a whole-game log fills to the cap without forgetting anything");

    // One past the cap: the flag goes up LOUDLY, nothing already learned is
    // dropped (append-only is the owner's law: this is his whole game).
    chronicle_record(gs.chronicle,
                     fact(300, FactKind::Battle,
                          fact_subject(FactSubject::Squad, false),
                          999999u, 5, 5));
    player_journal_capture(gs);
    CHECK(gs.player.journal.size() == std::size_t(cap)
              && gs.player.journalFull == 1,
          "the cap is loud, never a silent drop of his past");
}

// While the player WEARS a possessed lord (possession.md), his deeds file
// under the LORD's ordinal — and participation is learned wherever it
// happened, exactly as with his own squad.
void test_a_possessed_lords_deeds_are_his_participation() {
    GameState gs{};
    gs.mapW = 64;
    gs.mapH = 64;
    chronicle_init(gs.chronicle, gs.mapW, gs.mapH);
    gs.player.x = 5.0f;
    gs.player.y = 5.0f;

    // Not possessing: the lord's far-away deed is somebody else's.
    chronicle_record(gs.chronicle,
                     fact(1, FactKind::Killed,
                          fact_subject(FactSubject::Squad, false),
                          42u, 50, 50));
    player_journal_capture(gs);
    CHECK(gs.player.journal.empty(),
          "an unpossessed lord's deed far away is not the player's");

    // Possessing him: the same deed, wherever it happened, is HIS.
    gs.player.possessedMacroSpawnId = 42;
    chronicle_record(gs.chronicle,
                     fact(2, FactKind::Killed,
                          fact_subject(FactSubject::Squad, false),
                          42u, 50, 50));
    player_journal_capture(gs);
    CHECK(gs.player.journal.size() == 1
              && gs.player.journal[0].subject == 42u,
          "while possessed, the lord's ordinal is the player's participation");
}

// The journal holds COPIES of immutable records — and the ring's intrusive
// chain link is the INDEX's wiring, not the fact's: a captured copy must not
// carry a dead link into the save (write_fact refuses to store the derived
// link for the same reason).
void test_a_captured_copy_carries_no_ring_link() {
    GameState gs{};
    gs.mapW = 64;
    gs.mapH = 64;
    chronicle_init(gs.chronicle, gs.mapW, gs.mapH);
    gs.player.x = 5.0f;
    gs.player.y = 5.0f;

    // Two facts on one cell: the second's ring slot LINKS to the first.
    chronicle_record(gs.chronicle,
                     fact(1, FactKind::Battle,
                          fact_subject(FactSubject::Squad, false), 7u, 5, 5));
    chronicle_record(gs.chronicle,
                     fact(1, FactKind::Battle,
                          fact_subject(FactSubject::Squad, false), 8u, 5, 5));
    player_journal_capture(gs);
    CHECK(gs.player.journal.size() == 2
              && gs.player.journal[0].nextInCell == 0u
              && gs.player.journal[1].nextInCell == 0u,
          "a journal copy is the record, never the ring's chain link");
}

// ── THE deed door: a writer pays renown WITH the fact (CANON S20.1) ───────
// «Дверь ОДНА: подать факт и заплатить славу — одно действие. Разъединить их
// нельзя.» The door is macro/squad.h record_deed; the raw chronicle door is
// the negative control that proves paying is the DOOR's work, not the
// chronicle's.
void test_the_deed_door_files_and_pays_as_one_action() {
    GameState gs{};
    gs.mapW = 64;
    gs.mapH = 64;
    chronicle_init(gs.chronicle, gs.mapW, gs.mapH);
    ecs::World w;

    // A band with a save-stable identity and a renown store…
    const entt::entity band = w.reg.create();
    w.reg.emplace<ecs::MacroSpawnId>(band, ecs::MacroSpawnId{7u});
    auto& rt = w.reg.emplace<ecs::MacroNpcRuntime>(band);
    rt.renown = 0u;
    // …robs a town of some standing: the deed is worth its row's base plus a
    // tenth of what the VICTIM was worth (fame is made of fame).
    Landmark town{};
    town.type = LandmarkType::City;
    town.id = 3;
    town.renown = 50u;
    gs.landmarks.push_back(town);

    WorldFact f{};
    f.day = 1;
    f.kind = std::uint16_t(FactKind::Killed);
    f.objectKind = std::uint8_t(FactSubject::Landmark);
    f.object = 3u;
    f.x = 10;
    f.y = 10;
    f.amount = 5;
    const std::uint32_t seq = record_deed(w, gs, f, band);
    const std::uint32_t worth = renown_for_deed(FactKind::Killed, 50u);
    CHECK(seq != 0u, "the door filed the fact");
    CHECK(rt.renown == worth,
          "…and paid the doer exactly renown_for_deed — one action");
    const WorldFact& filed =
        gs.chronicle.ring[std::size_t((seq - 1u) % kChronicleFacts)];
    CHECK(filed.seq == seq && filed.subject == 7u
              && fact_subject_kind(filed.subjectKind)
                     == std::uint8_t(FactSubject::Squad),
          "the record carries the doer's save-stable ordinal");
    CHECK(!fact_subject_marked_named(filed.subjectKind),
          "figure-ness is marked from PRE-deed renown: a nobody's first deed "
          "is a nobody's");

    // A figure's next deed wears the bit — still from PRE-deed renown.
    rt.renown = std::uint32_t(kRenownToBeNamed);
    const std::uint32_t seq2 = record_deed(w, gs, f, band);
    const WorldFact& filed2 =
        gs.chronicle.ring[std::size_t((seq2 - 1u) % kChronicleFacts)];
    CHECK(fact_subject_marked_named(filed2.subjectKind),
          "a figure's deed is marked as a figure's");

    // NEGATIVE CONTROL: the raw chronicle door records but pays NOTHING — a
    // writer that bypasses record_deed is exactly the free filing this law
    // forbids.
    const std::uint32_t before = rt.renown;
    WorldFact raw = f;
    raw.subjectKind = std::uint8_t(FactSubject::Squad);
    raw.subject = 7u;
    CHECK(chronicle_record(gs.chronicle, raw) != 0u,
          "the raw door still records");
    CHECK(rt.renown == before, "…but grants no renown: paying is the deed "
                               "door's half of the one action");
}

} // namespace

int main() {
    test_participation_locality_and_silence();
    test_the_journal_never_forgets_and_the_cap_is_loud();
    test_a_possessed_lords_deeds_are_his_participation();
    test_a_captured_copy_carries_no_ring_link();
    test_the_deed_door_files_and_pays_as_one_action();
    return sm::test::report("journal_test");
}

// The player's journal — his KNOWLEDGE of the chronicle (macro/journal.h).
// Owner rulings pinned here (2026-08-28):
//   · he learns what he TOOK PART in (subject or object) wherever it happened;
//   · he learns what happened ON HIS CELL while he stood there — locality;
//   · everything else the world remembers WITHOUT him: not in the journal;
//   · the journal never forgets (his whole game) — append-only, LOUD cap;
//   · the capture is idempotent: re-asking the chronicle re-learns nothing.
#include "check.h"

#include "macro/journal.h"

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
    WorldFact hit = fact(2, FactKind::Robbed,
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

} // namespace

int main() {
    test_participation_locality_and_silence();
    test_the_journal_never_forgets_and_the_cap_is_loud();
    return sm::test::report("journal_test");
}

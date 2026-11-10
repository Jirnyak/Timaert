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
#include <memory>

#include "macro/journal.h"
#include "macro/birth_squad.h"  // место рождается СО СВОИМ ТЕЛОМ (M-90 шаг 5)
#include "macro/squad.h"
#include "bus/store.h"

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

// Locality is the flag holder's CELL now (подпосадка 4): "standing at" is
// a flagged entity with a MacroCell, moved by writing that cell — exactly
// what the walker and the jump door do in the game.
// The player standing somewhere — and standing there AS HIMSELF, which since
// 2026-09-14 means the fixture must raise an honest ORIGINAL: «кто оригинал» is
// the HOME BIT of the playerFlag column (M-255 — it used to be a reserved
// ordinal-literal), and «ношу ли я чужое тело» is the flag and the original
// being different entities (player_entity.h). A stand-in with only PlayerTag
// used to be enough because the question was then asked by comparing the FLAG's
// own ordinal to the constant — one entity, one read. It is not enough now, and
// that is the fixture's debt, not the law's: a world without an original is a
// world that cannot exist.
//
// СВИДЕТЕЛЬ РОЖДАЕТ СВОЁ ПРЕДУСЛОВИЕ САМ (§8 п.11) и ходит ТЕМИ ЖЕ ДВЕРЬМИ,
// что игра: ординал — от эмитента, активный — дверь переноса флажка
// переноса. Прибей он ординал литералом, как раньше, — и свидетель проверял бы
// форму, которой в игре больше нет.
void stand_at(sm::GameState& gs, sm::MacroStore& st, int x, int y) {
    if (!st.valid(sm::player_flag_handle(gs))) {
        const sm::MacroHandle h = sm::store_birth(st);
        // Ординал выдала дверь рождения (M-220). Оба носителя — СВОЕЙ
        // дверью (5б + M-255): колонка playerFlag — истина, хэндл GameState
        // — кэш, и каждая дверь пишет их вместе.
        sm::transfer_player_flag(st, gs.playerFlagRef, h);
        sm::transfer_player_flag(st, gs.playerFlagRef, h);
    }
    // Клетка пишется НОСИТЕЛЮ ФЛАЖКА — локальность журнал мерит по нему.
    const sm::MacroHandle f = sm::player_flag_handle(gs);
    st.cell[f.slot] = ecs::MacroCell{ecs::cell_index(x, y, 64)};
}

void test_participation_locality_and_silence() {
    std::unique_ptr<GameState> gsOwn = make_game_state();
    GameState& gs = *gsOwn;
    gs.mapW = 64;
    gs.mapH = 64;
    chronicle_init(gs.chronicle, gs.mapW, gs.mapH);
    auto wStore_ = sm::make_macro_store();   // the flag holder is nobody's LORD → he wears nobody
    stand_at(gs, *wStore_, 10, 10);

    // (a) The player's own deed, far away — learned by PARTICIPATION.
    chronicle_record(gs.chronicle,
                     fact(1, FactKind::Killed,
                          fact_subject(FactSubject::Squad),
                          sm::player_flag_ordinal(gs, *wStore_),
                          50, 50));
    // (b) Somebody else's deed on the player's cell — learned by LOCALITY.
    chronicle_record(gs.chronicle,
                     fact(1, FactKind::Killed,
                          fact_subject(FactSubject::Squad),
                          777u, 10, 10));
    // (c) Somebody else's deed far away — the world knows, the player DOES
    // NOT: this is S11 applied to history, and the whole point of a journal.
    chronicle_record(gs.chronicle,
                     fact(1, FactKind::Starved,
                          std::uint8_t(FactSubject::Rooted), 3u, 40, 40));

    player_journal_capture(gs, *wStore_);
    CHECK(gs.player.journalCount == 2u,
          "participation and locality are learned; the far world is not");
    CHECK(gs.player.journal[0].subject
                  == sm::player_flag_ordinal(gs, *wStore_)
              && gs.player.journal[1].subject == 777u,
          "the journal keeps the very records, in the order they happened");

    // Idempotence: the reader's cursor moved, so nothing is learned twice.
    player_journal_capture(gs, *wStore_);
    CHECK(gs.player.journalCount == 2u,
          "re-asking the chronicle re-learns nothing");

    // The player as the OBJECT of another's deed — his side of a done-to.
    WorldFact hit = fact(2, FactKind::Killed,
                         fact_subject(FactSubject::Squad), 778u,
                         30, 30);
    hit.objectKind = fact_subject(FactSubject::Squad);
    hit.object = sm::player_flag_ordinal(gs, *wStore_);
    chronicle_record(gs.chronicle, hit);
    player_journal_capture(gs, *wStore_);
    CHECK(gs.player.journalCount == 3u
              && gs.player.journal[2].object
                     == sm::player_flag_ordinal(gs, *wStore_),
          "being done-to is participation too");

    // Walking away changes what "here" means: the same foreign cell that was
    // silent above becomes his the tick he stands on it.
    stand_at(gs, *wStore_, 40, 40);
    chronicle_record(gs.chronicle,
                     fact(3, FactKind::Killed,
                          fact_subject(FactSubject::Squad),
                          779u, 40, 40));
    player_journal_capture(gs, *wStore_);
    CHECK(gs.player.journalCount == 4u,
          "locality follows the player, not a fixed home cell");
}

void test_the_journal_never_forgets_and_the_cap_is_loud() {
    std::unique_ptr<GameState> gsOwn = make_game_state();
    GameState& gs = *gsOwn;
    gs.mapW = 64;
    gs.mapH = 64;
    chronicle_init(gs.chronicle, gs.mapW, gs.mapH);
    auto wStore_ = sm::make_macro_store();
    stand_at(gs, *wStore_, 5, 5);

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
                             fact(int(filed + i) / 256 + 1, FactKind::Killed,
                                  fact_subject(FactSubject::Squad),
                                  1000u + filed + i, 5, 5));
        }
        filed += slice;
        player_journal_capture(gs, *wStore_);
    }
    CHECK(gs.player.journalCount == cap
              && gs.player.journalFull == 0,
          "a whole-game log fills to the cap without forgetting anything");

    // One past the cap: the flag goes up LOUDLY, nothing already learned is
    // dropped (append-only is the owner's law: this is his whole game).
    chronicle_record(gs.chronicle,
                     fact(300, FactKind::Killed,
                          fact_subject(FactSubject::Squad),
                          999999u, 5, 5));
    player_journal_capture(gs, *wStore_);
    CHECK(gs.player.journalCount == cap
              && gs.player.journalFull == 1,
          "the cap is loud, never a silent drop of his past");
}

// While the player WEARS a possessed lord, his deeds file under the LORD's
// ordinal — and participation is learned wherever it happened, exactly as with
// his own squad.
void test_a_possessed_lords_deeds_are_his_participation() {
    std::unique_ptr<GameState> gsOwn = make_game_state();
    GameState& gs = *gsOwn;
    gs.mapW = 64;
    gs.mapH = 64;
    chronicle_init(gs.chronicle, gs.mapW, gs.mapH);
    auto wStore_ = sm::make_macro_store();
    stand_at(gs, *wStore_, 5, 5);

    // Not possessing: the lord's far-away deed is somebody else's.
    chronicle_record(gs.chronicle,
                     fact(1, FactKind::Killed,
                          fact_subject(FactSubject::Squad),
                          42u, 50, 50));
    player_journal_capture(gs, *wStore_);
    CHECK(gs.player.journalCount == 0u,
          "an unpossessed lord's deed far away is not the player's");

    // Possessing him: the same deed, wherever it happened, is HIS. «Whom do
    // I wear» is the flag on the lord himself (v87) — the world is asked, no
    // out-of-snapshot field.
    sm::MacroStore& stL = *wStore_;
    sm::MacroHandle hL = sm::store_birth(stL);
    sm::store_rename_ordinal(stL, hL, 42u);   // пин ординала фикстуры
    hL = sm::handle_at(stL, hL.slot);                // хэндл — за колонкой
    // Possession MOVES the one flag — дверью переноса (5б): колонка
    // анкеты + кэш-хэндл, запись нового = срыв старого.
    sm::transfer_player_flag(stL, gs.playerFlagRef, hL);
    chronicle_record(gs.chronicle,
                     fact(2, FactKind::Killed,
                          fact_subject(FactSubject::Squad),
                          42u, 50, 50));
    player_journal_capture(gs, *wStore_);
    CHECK(gs.player.journalCount == 1u
              && gs.player.journal[0].subject == 42u,
          "while possessed, the lord's ordinal is the player's participation");
}

// The journal holds COPIES of immutable records — and the ring's intrusive
// chain link is the INDEX's wiring, not the fact's: a captured copy must not
// carry a dead link into the save (write_fact refuses to store the derived
// link for the same reason).
void test_a_captured_copy_carries_no_ring_link() {
    std::unique_ptr<GameState> gsOwn = make_game_state();
    GameState& gs = *gsOwn;
    gs.mapW = 64;
    gs.mapH = 64;
    chronicle_init(gs.chronicle, gs.mapW, gs.mapH);
    auto wStore_ = sm::make_macro_store();
    stand_at(gs, *wStore_, 5, 5);

    // Two facts on one cell: the second's ring slot LINKS to the first.
    chronicle_record(gs.chronicle,
                     fact(1, FactKind::Killed,
                          fact_subject(FactSubject::Squad), 7u, 5, 5));
    chronicle_record(gs.chronicle,
                     fact(1, FactKind::Killed,
                          fact_subject(FactSubject::Squad), 8u, 5, 5));
    player_journal_capture(gs, *wStore_);
    CHECK(gs.player.journalCount == 2u
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
    std::unique_ptr<GameState> gsOwn = make_game_state();
    GameState& gs = *gsOwn;
    gs.mapW = 64;
    gs.mapH = 64;
    chronicle_init(gs.chronicle, gs.mapW, gs.mapH);
    auto wStore_ = sm::make_macro_store();

    // A band with a save-stable identity and a renown store…
    sm::MacroStore& stB = *wStore_;
    sm::MacroHandle hB = sm::store_birth(stB);
    sm::store_rename_ordinal(stB, hB, 7u);
    hB = sm::handle_at(stB, hB.slot);
    auto& rt = stB.runtime[hB.slot];
    rt.renown = 0u;
    // …robs a town of some standing: the deed is worth its row's base plus a
    // tenth of what the VICTIM was worth (fame is made of fame).
    // Слава места — колонка runtime.renown ЕГО ТЕЛА (M-90 шаг 5), поэтому
    // жертва приходит в мир дверью рождения со слотом (ломтик F: строки
    // места больше нет). Эмитент поднимается ВЫШЕ рукописного ординала
    // банды (7): выдача с него столкнула бы два субъекта в одном ординале.
    stB.nextSpawnId = 8u;   // эмитент — у стора (M-220)
    const MacroHandle hTown =
        birth_squad(gs, stB, SquadType::City, 10, 10);
    stB.runtime[hTown.slot].renown = 50u;

    WorldFact f{};
    f.day = 1;
    f.kind = std::uint16_t(FactKind::Killed);
    f.objectKind = std::uint8_t(FactSubject::Rooted);
    f.object = stB.spawnId[hTown.slot].index;
    f.x = 10;
    f.y = 10;
    f.amount = 5;
    const std::uint32_t seq = record_deed(*wStore_, gs, f, hB);
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
    // (ЗДЕСЬ ПРОВЕРЯЛСЯ БИТ «ИМЕНОВАН» В ЗАПИСИ ФАКТА — он снят вместе с
    //  анналами, своим единственным читателем (M-184, DOD п.9). Закон
    //  «фигурность из ПРЕД-дельной славы» остался величиной `renown`, и
    //  плата славой проверена двумя строками ВЫШЕ.)

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

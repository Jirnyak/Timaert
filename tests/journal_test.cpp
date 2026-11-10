// THE PLAYER'S JOURNAL IS HIS SQUAD'S CHAIN IN THE WORLD'S CHRONICLE
// (macro/chronicle.h; CANON S20.1 «ХРАНИЛИЩЕ ОДНО, А ПАМЯТЬ СКВАДА ЕСТЬ ИНДЕКС
// В НЁМ»; наряд M-423).
//
// Здесь стоял свидетель ЖУРНАЛА КОПИЙ: двери захвата, которая раз в тик
// переписывала в состояние игрока факты, где он участвовал или на чьей клетке
// стоял, под своим курсором и своим громким капом. Копии были платой за
// забывающее кольцо летописи; лог не забывает, и хранить стало нечего. Законы,
// которые судятся теперь:
//   · история сквада = «что он сделал» (цепочка по СУБЪЕКТУ) + «что сделали с
//     ним» (цепочка по ОБЪЕКТУ), слитые новейшими вперёд;
//   · всё остальное мир помнит БЕЗ него: вопрос задаётся по участнику, не по
//     месту;
//   · у игрока нет своей истории — он читает цепочку того сквада, которым
//     играет СЕЙЧАС; сменил тело — читает историю нового тела;
//   · умер — и доступ стёрся, а история цела: новый жилец слота не наследует
//     чужую цепочку, записи остаются в логе мира;
//   · цепочки ВЫВОДНЫЕ: индекс, впервые увидевший готовый лог (загрузка),
//     отвечает ровно как тот, что жил с логом;
//   · a writer pays renown WITH the fact — the one deed door (macro/squad.h
//     record_deed) makes the two halves one action, and the raw chronicle
//     door pays nothing (the negative control).
#include "check.h"
#include <memory>

#include "macro/chronicle.h"
#include "macro/birth_squad.h"  // место рождается СО СВОИМ ТЕЛОМ (M-90 шаг 5)
#include "macro/player_entity.h"
#include "macro/squad.h"
#include "macro/state.h"
#include "bus/session.h"
#include "bus/store.h"

#include <cstdint>
#include <limits>
#include <vector>

namespace {

using namespace sm;

constexpr std::int32_t kAllDays = std::numeric_limits<std::int32_t>::min();
constexpr int kNoLimit = std::numeric_limits<int>::max();

struct Collected {
    std::vector<WorldFact> facts;
};
void collect(void* user, const WorldFact& f) {
    static_cast<Collected*>(user)->facts.push_back(f);
}

// A deed: `subj` did `kind` (to `obj`, if any) on a cell.
WorldFact deed(int day, FactKind kind, std::uint32_t subj, std::uint32_t obj,
               int x, int y) {
    WorldFact f{};
    f.day = day;
    f.kind = std::uint16_t(kind);
    f.subjectKind = fact_subject(FactSubject::Squad);
    f.subject = subj;
    if (obj != 0u) {
        f.objectKind = fact_subject(FactSubject::Squad);
        f.object = obj;
    }
    f.x = std::int16_t(x);
    f.y = std::int16_t(y);
    f.amount = 1;
    return f;
}

// Свидетель рождает сквады ТОЙ ЖЕ дверью, что игра: ординал выдаёт эмитент
// стора, а не литерал фикстуры (§8 п.11).
struct Band {
    MacroHandle h{};
    std::uint32_t ordinal = 0;
};
Band birth(MacroStore& st, int x, int y) {
    Band b;
    b.h = store_birth(st);
    b.ordinal = st.spawnId[b.h.slot].index;
    st.cell[b.h.slot] = ecs::MacroCell{ecs::cell_index(x, y, 64)};
    return b;
}

struct World {
    std::unique_ptr<GameState> gsOwn = make_game_state();
    std::unique_ptr<MacroStore> stOwn = make_macro_store();
    std::unique_ptr<ChronicleLinks> linksOwn = make_chronicle_links();
    GameState& gs = *gsOwn;
    MacroStore& st = *stOwn;
    ChronicleLinks& links = *linksOwn;
    Session ses;
    World() {
        gs.mapW = 64;
        gs.mapH = 64;
        chronicle_init(gs.chronicle, gs.mapW, gs.mapH);
    }
};

// ── What I did, what was done to me, and the world that went on without me ─
void test_a_squads_history_is_its_two_chains() {
    World w;
    const Band me = birth(w.st, 10, 10);
    const Band foe = birth(w.st, 30, 30);
    const Band other = birth(w.st, 10, 10);   // stands on MY cell
    CHECK_OR_RETURN(me.ordinal != 0u && foe.ordinal != 0u
                        && other.ordinal != 0u,
                    "the door of birth gave every band an identity");

    Chronicle& c = w.gs.chronicle;
    // day 1: I kill somebody far from home.
    chronicle_record(c, deed(1, FactKind::Killed, me.ordinal, other.ordinal, 50, 50));
    // day 2: somebody else's deed ON MY OWN CELL — I am not named in it.
    chronicle_record(c, deed(2, FactKind::Killed, other.ordinal, foe.ordinal, 10, 10));
    // day 3: the foe kills ME — my side of a done-to.
    chronicle_record(c, deed(3, FactKind::Killed, foe.ordinal, me.ordinal, 30, 30));
    // day 4: the world goes on far away, without me.
    chronicle_record(c, deed(4, FactKind::Starved, foe.ordinal, 0u, 40, 40));
    // day 5: I explore.
    chronicle_record(c, deed(5, FactKind::Explored, me.ordinal, 0u, 12, 12));

    Collected did, doneTo, mine;
    const int nDid = chronicle_by_subject(c, w.links, w.st, me.h, kAllDays,
                                          kNoLimit, collect, &did);
    chronicle_by_object(c, w.links, w.st, me.h, kAllDays, kNoLimit, collect,
                        &doneTo);
    chronicle_about(c, w.links, w.st, me.h, kAllDays, kNoLimit, collect, &mine);

    CHECK(nDid == 2 && did.facts.size() == 2u
              && did.facts[0].day == 5 && did.facts[1].day == 1,
          "'what I did' is every fact that names me as SUBJECT, newest first");
    CHECK(doneTo.facts.size() == 1u && doneTo.facts[0].day == 3
              && doneTo.facts[0].subject == foe.ordinal,
          "'what was done to me' is every fact that names me as OBJECT");
    CHECK(mine.facts.size() == 3u && mine.facts[0].day == 5
              && mine.facts[1].day == 3 && mine.facts[2].day == 1,
          "my history is both chains merged by time, newest first");
    bool foreign = false;
    for (const WorldFact& f : mine.facts) {
        if (f.day == 2 || f.day == 4) foreign = true;
    }
    CHECK(!foreign,
          "negative control: deeds that do not name me are not my history — "
          "neither the far one nor the one on my own cell; the question is "
          "asked BY PARTICIPANT, not by place");

    // The window and the limit are read, not ignored.
    Collected recent, capped;
    chronicle_about(c, w.links, w.st, me.h, /*sinceDay*/3, kNoLimit, collect,
                    &recent);
    chronicle_about(c, w.links, w.st, me.h, kAllDays, /*limit*/1, collect,
                    &capped);
    CHECK(recent.facts.size() == 2u && recent.facts.back().day == 3,
          "the walk stops at the first fact older than the window");
    CHECK(capped.facts.size() == 1u && capped.facts[0].day == 5,
          "a limit keeps the NEWEST, not the first met");

    // The foe has a history of its own in the same one log.
    Collected foes;
    chronicle_about(c, w.links, w.st, foe.h, kAllDays, kNoLimit, collect, &foes);
    CHECK(foes.facts.size() == 3u,
          "one storage, many memories: the foe's chain runs through the same "
          "records (killed me, starved, was killed by another)");

    // A deed done to oneself is ONE fact, told once.
    chronicle_record(c, deed(6, FactKind::Starved, me.ordinal, me.ordinal, 10, 10));
    Collected withSelf;
    chronicle_about(c, w.links, w.st, me.h, kAllDays, kNoLimit, collect,
                    &withSelf);
    CHECK(withSelf.facts.size() == 4u && withSelf.facts[0].day == 6
              && withSelf.facts[1].day == 5,
          "a fact that names me as both subject and object is in my history "
          "once, not twice");

    // Nobody has no history, and a fact about nobody belongs to no chain.
    Collected nobody;
    CHECK(chronicle_about(c, w.links, w.st, MacroHandle{}, kAllDays, kNoLimit,
                          collect, &nobody) == 0,
          "a handle that names nobody has no history");
}

// ── The player is nothing special: he reads the chain of whom he plays ────
void test_the_players_history_follows_the_flag() {
    World w;
    const Band home = birth(w.st, 5, 5);
    const Band lord = birth(w.st, 50, 50);
    transfer_player_flag(w.st, w.ses.playerFlagRef, home.h);
    Chronicle& c = w.gs.chronicle;

    // The lord has a past of his own from before anyone wore him.
    chronicle_record(c, deed(1, FactKind::Killed, lord.ordinal, 0u, 50, 50));
    // The player, as himself, does a deed.
    chronicle_record(c, deed(2, FactKind::Explored, home.ordinal, 0u, 5, 5));

    Collected asHimself;
    chronicle_about(c, w.links, w.st, player_flag_handle(w.ses), kAllDays,
                    kNoLimit, collect, &asHimself);
    CHECK(asHimself.facts.size() == 1u
              && asHimself.facts[0].subject == home.ordinal,
          "as himself he reads his own squad's chain; an unworn lord's deed "
          "far away is somebody else's");

    // Possession MOVES the one flag — and with it, whose history is read.
    transfer_player_flag(w.st, w.ses.playerFlagRef, lord.h);
    chronicle_record(c, deed(3, FactKind::Killed, lord.ordinal, 0u, 51, 50));
    Collected asLord;
    chronicle_about(c, w.links, w.st, player_flag_handle(w.ses), kAllDays,
                    kNoLimit, collect, &asLord);
    CHECK(asLord.facts.size() == 2u && asLord.facts[0].day == 3
              && asLord.facts[1].day == 1,
          "wearing the lord he reads the LORD's history — the whole of it, "
          "the deed done before he came included: the memory is the squad's, "
          "not the player's");
    bool leaked = false;
    for (const WorldFact& f : asLord.facts) {
        if (f.subject == home.ordinal) leaked = true;
    }
    CHECK(!leaked,
          "negative control: the body he left keeps its own history; it does "
          "not follow him into the new one");
    Collected stillThere;
    chronicle_about(c, w.links, w.st, home.h, kAllDays, kNoLimit, collect,
                    &stillThere);
    CHECK(stillThere.facts.size() == 1u,
          "and nothing was lost: the home squad's chain is asked by its own "
          "handle, the same door any squad uses");
}

// ── «Умер — и всё, что у него было, стирается»: ДОСТУП, не ИСТОРИЯ ───────
void test_death_erases_the_access_and_keeps_the_history() {
    World w;
    const Band witness = birth(w.st, 1, 1);
    const Band doomed = birth(w.st, 20, 20);
    Chronicle& c = w.gs.chronicle;
    chronicle_record(c, deed(1, FactKind::Killed, doomed.ordinal, 0u, 20, 20));
    chronicle_record(c, deed(2, FactKind::Killed, witness.ordinal,
                             doomed.ordinal, 20, 20));

    Collected alive;
    chronicle_about(c, w.links, w.st, doomed.h, kAllDays, kNoLimit, collect,
                    &alive);
    CHECK_OR_RETURN(alive.facts.size() == 2u,
                    "while it lives the squad has its history");

    // It dies, and the very same slot is born again as somebody else.
    store_death(w.st, doomed.h);
    const Band heir = birth(w.st, 20, 20);
    CHECK_OR_RETURN(heir.h.slot == doomed.h.slot
                        && heir.ordinal != doomed.ordinal,
                    "precondition: the slot is REUSED by a new identity — "
                    "otherwise nothing below is being tested");

    Collected stale, inherited;
    CHECK(chronicle_about(c, w.links, w.st, doomed.h, kAllDays, kNoLimit,
                          collect, &stale) == 0,
          "the dead squad's handle reaches nothing: access died with it");
    CHECK(chronicle_about(c, w.links, w.st, heir.h, kAllDays, kNoLimit,
                          collect, &inherited) == 0,
          "and the new tenant of the slot does NOT inherit the dead one's "
          "chain — a born squad has no past");

    // The new tenant starts a chain of its own, and it does not run on into
    // the dead one's records.
    chronicle_record(c, deed(3, FactKind::Explored, heir.ordinal, 0u, 20, 20));
    chronicle_record(c, deed(4, FactKind::Killed, witness.ordinal,
                             heir.ordinal, 20, 20));
    Collected own;
    chronicle_about(c, w.links, w.st, heir.h, kAllDays, kNoLimit, collect, &own);
    CHECK(own.facts.size() == 2u && own.facts[0].day == 4
              && own.facts[1].day == 3,
          "its history is exactly its own two facts — the chain ends where "
          "its identity began");

    // HISTORY is not erased: the chronicle is the WORLD's, not the squad's.
    struct Count { std::uint32_t who; int n; };
    Count dead{doomed.ordinal, 0};
    chronicle_recent(c, 0, kNoLimit,
                     [](void* u, const WorldFact& f) {
                         Count& k = *static_cast<Count*>(u);
                         if (f.subject == k.who || f.object == k.who) ++k.n;
                     }, &dead);
    CHECK(dead.n == 2,
          "negative control: both facts about the dead squad are still in "
          "the world's log — what died is the door to them, not the past");
    Collected witnessed;
    chronicle_by_subject(c, w.links, w.st, witness.h, kAllDays, kNoLimit,
                         collect, &witnessed);
    CHECK(witnessed.facts.size() == 2u
              && witnessed.facts[1].object == doomed.ordinal,
          "and the one who killed it still remembers doing so");
}

// ── The chains are DERIVED: a loaded world rebuilds them from the log ─────
void test_a_fresh_index_answers_like_the_one_that_lived() {
    World w;
    const Band a = birth(w.st, 3, 3);
    const Band b = birth(w.st, 9, 9);
    Chronicle& c = w.gs.chronicle;
    Collected sink;
    for (int d = 1; d <= 20; ++d) {
        chronicle_record(c, deed(d, FactKind::Killed, d % 3 ? a.ordinal : b.ordinal,
                                 d % 2 ? b.ordinal : 0u, d, d));
        // Asked between writes: the lived index is built a fact at a time.
        chronicle_about(c, w.links, w.st, a.h, kAllDays, kNoLimit, collect,
                        &sink);
    }
    const std::unique_ptr<ChronicleLinks> fresh = make_chronicle_links();
    for (const Band& who : {a, b}) {
        Collected lived, rebuilt;
        chronicle_about(c, w.links, w.st, who.h, kAllDays, kNoLimit, collect,
                        &lived);
        chronicle_about(c, *fresh, w.st, who.h, kAllDays, kNoLimit, collect,
                        &rebuilt);
        bool same = lived.facts.size() == rebuilt.facts.size();
        for (std::size_t i = 0; same && i < lived.facts.size(); ++i) {
            same = lived.facts[i].seq == rebuilt.facts[i].seq;
        }
        CHECK(!lived.facts.empty() && same,
              "an index that first meets the finished log — a save being "
              "loaded — answers exactly as the one that lived with it");
    }
    // And the door of forgetting brings the lived one back to the same answer.
    Collected before, after;
    chronicle_about(c, w.links, w.st, b.h, kAllDays, kNoLimit, collect, &before);
    chronicle_rebuild_links(w.links);
    chronicle_about(c, w.links, w.st, b.h, kAllDays, kNoLimit, collect, &after);
    CHECK(before.facts.size() == after.facts.size() && !after.facts.empty()
              && before.facts.front().seq == after.facts.front().seq
              && before.facts.back().seq == after.facts.back().seq,
          "forgetting what was derived loses nothing: it is derived again");
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
        birth_squad(gs, nullptr, stB, SquadType::City, 10, 10);
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
    const WorldFact* filedAt = chronicle_fact(gs.chronicle, seq);
    CHECK_OR_RETURN(filedAt != nullptr, "the filed fact is in the log");
    const WorldFact& filed = *filedAt;
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
    test_a_squads_history_is_its_two_chains();
    test_the_players_history_follows_the_flag();
    test_death_erases_the_access_and_keeps_the_history();
    test_a_fresh_index_answers_like_the_one_that_lived();
    test_the_deed_door_files_and_pays_as_one_action();
    return sm::test::report("journal_test");
}

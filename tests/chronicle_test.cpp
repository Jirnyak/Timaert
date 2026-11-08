// THE WORLD REMEMBERS, AND CAN BE ASKED.
//
// This file's whole reason is the owner's own test case (CANON.md S20.1): a
// witcher-like squad walks the villages, and a monster squad that kept killing
// peasants nearby left TRACES. The witcher finds it himself. For that,
// something must be able to ask:
//
//     what happened near this village in the last thirty days?
//
// So the tests below are that question, asked in the ways it will actually be
// asked, each with the negative control that proves the answer is READ and not
// assumed: a fact too far away, a fact too old, a fact of the wrong kind.
#include "check.h"

#include "macro/chronicle.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

using namespace sm;

struct Collected {
    std::vector<WorldFact> facts;
};

void collect(void* user, const WorldFact& f) {
    static_cast<Collected*>(user)->facts.push_back(f);
}

WorldFact killing(int day, int x, int y, std::uint32_t who, int bodies) {
    WorldFact f{};
    f.day = day;
    f.kind = std::uint16_t(FactKind::Killed);
    f.subjectKind = std::uint8_t(FactSubject::Squad);
    f.subject = who;
    f.objectKind = std::uint8_t(FactSubject::Rooted);
    f.object = 3u;
    f.x = std::int16_t(x);
    f.y = std::int16_t(y);
    f.amount = bodies;
    return f;
}

// ── The record is what it claims to be ───────────────────────────────────
void test_the_record_is_flat_and_the_rows_are_rows() {
    CHECK(sizeof(WorldFact) == 32,
          "a fact is 32 bytes of numbers: it is scanned by the thousand");
    for (int i = 0; i < int(FactKind::Count); ++i) {
        const FactKindDef& d = fact_kind_def(FactKind(i));
        CHECK(int(d.id) == i, "every kind row stands at its own ordinal");
        CHECK(d.key != nullptr && d.key[0] != '\0', "and names itself");
        CHECK(d.label != nullptr && d.label[0] != '\0', "and labels itself");
    }
    CHECK(fact_kind_def(FactKind::Killed).interestDays
              > fact_kind_def(FactKind::Traded).interestDays,
          "a killing is news longer than a sale — the row says how long "
          "'recent' means for its own kind");
}

// ── THE question ─────────────────────────────────────────────────────────
void test_a_witcher_finds_the_monster_by_its_traces() {
    Chronicle c;
    chronicle_init(c, 256, 256);

    // A monster squad has been killing peasants outside one village for a
    // week. Elsewhere, other things happened.
    constexpr std::uint32_t kMonster = 77u;
    for (int d = 20; d < 27; ++d) {
        chronicle_record(c, killing(d, 100, 100, kMonster, 2));
    }
    chronicle_record(c, killing(25, 200, 30, 99u, 5));    // far away
    chronicle_record(c, killing(2,  101, 101, 55u, 1));   // long ago, near

    // The witcher stands at the village and asks about the last thirty days.
    Collected got;
    const int offered =
        chronicle_near(c, 100, 100, /*radiusCells*/1, /*sinceDay*/25 - 30 + 1,
                       collect, &got);
    CHECK(offered == int(got.facts.size()), "the count is what it handed over");

    int monsterBodies = 0, monsterFacts = 0;
    bool sawFarAway = false, sawAncient = false;
    for (const WorldFact& f : got.facts) {
        if (f.subject == kMonster) { ++monsterFacts; monsterBodies += f.amount; }
        if (f.subject == 99u) sawFarAway = true;
        if (f.subject == 55u) sawAncient = true;
    }
    CHECK(monsterFacts == 7,
          "every trace the monster left near the village comes back");
    CHECK(monsterBodies == 14, "with the tally intact — it killed fourteen");
    CHECK(!sawFarAway,
          "negative control: what happened across the map did NOT — the "
          "question is asked BY PLACE");
    CHECK(sawAncient,
          "and a nearby fact from day 2 is inside a thirty-day window ending "
          "on day 25, so it belongs");

    // ...and the same question with a tighter window drops it, which is what
    // proves the window is read rather than ignored.
    Collected tight;
    chronicle_near(c, 100, 100, 1, /*sinceDay*/20, collect, &tight);
    bool ancientInTight = false;
    for (const WorldFact& f : tight.facts) {
        if (f.subject == 55u) ancientInTight = true;
    }
    CHECK(!ancientInTight,
          "negative control: the same fact falls out of a shorter window — "
          "the question is asked BY TIME too");
}

// ── The ring forgets, and the chains end where it forgot ─────────────────
void test_the_chains_truncate_themselves() {
    Chronicle c;
    chronicle_init(c, 128, 128);

    // Fill the ring past its capacity from ONE place, so every fact lands on
    // one chain and the oldest are certainly evicted.
    const std::uint32_t over = kChronicleFacts + 100u;
    for (std::uint32_t i = 0; i < over; ++i) {
        chronicle_record(c, killing(50, 64, 64, i, 1));
    }

    Collected got;
    chronicle_near(c, 64, 64, 0, /*sinceDay*/0, collect, &got);
    CHECK(int(got.facts.size()) <= int(kChronicleFacts),
          "a walk cannot return more than the ring holds");
    CHECK(!got.facts.empty(), "and it returns what the ring still has");

    // Newest first, and nothing evicted comes back: the chain stopped where
    // the ring forgot, with no unlinking anywhere.
    CHECK(got.facts.front().subject == over - 1u,
          "the newest fact is first");
    for (const WorldFact& f : got.facts) {
        CHECK(f.subject >= over - kChronicleFacts,
              "no evicted fact is ever handed back");
    }
    // ...and the walk TERMINATES, which is the thing a self-truncating chain
    // could most easily get wrong: a link pointing at a slot reused by a
    // NEWER fact would be a loop.
    CHECK(int(got.facts.size()) < int(over),
          "negative control: the ring really did forget some — otherwise the "
          "termination above proves nothing");
}

// ── The journal's view: the whole past, newest first ─────────────────────
void test_recent_walks_time_without_an_index() {
    Chronicle c;
    chronicle_init(c, 64, 64);
    for (int d = 1; d <= 10; ++d) {
        chronicle_record(c, killing(d, d, d, std::uint32_t(d), d));
    }

    Collected got;
    const int n = chronicle_recent(c, /*sinceDay*/6, /*limit*/100, collect, &got);
    CHECK(n == 5, "days 6..10 are five days");
    CHECK(got.facts.front().day == 10 && got.facts.back().day == 6,
          "newest first, and it stops at the window's edge");

    Collected capped;
    chronicle_recent(c, 0, /*limit*/3, collect, &capped);
    CHECK(int(capped.facts.size()) == 3, "a limit is a limit");
    CHECK(capped.facts.front().day == 10,
          "and it keeps the NEWEST three, not the first three it met");
}

// ── Refusals ─────────────────────────────────────────────────────────────
void test_the_chronicle_refuses_what_it_cannot_record() {
    Chronicle c;
    WorldFact f = killing(1, 0, 0, 1u, 1);
    CHECK(chronicle_record(c, f) == 0u,
          "a chronicle with no world records nothing");

    chronicle_init(c, 64, 64);
    WorldFact none = f;
    none.kind = std::uint16_t(FactKind::None);
    CHECK(chronicle_record(c, none) == 0u,
          "a fact of no kind is not a fact");
    none.kind = std::uint16_t(FactKind::Count);
    CHECK(chronicle_record(c, none) == 0u,
          "and neither is one whose kind names no row");
    CHECK(chronicle_record(c, f) == 1u,
          "negative control: a real fact IS recorded, and counting starts at 1 "
          "so that zero can mean 'no fact'");
}

// ── The instrument ───────────────────────────────────────────────────────
// kChronicleFacts is honestly provisional, so the thing that makes it
// measurable is part of the contract rather than a debug afterthought.
void test_the_world_counts_its_own_facts() {
    Chronicle c;
    chronicle_init(c, 64, 64);
    for (int i = 0; i < 5; ++i) chronicle_record(c, killing(7, 1, 1, 1u, 1));
    CHECK(c.factsToday == 5u && c.countingDay == 7,
          "the world counts what it produced today");
    chronicle_record(c, killing(8, 1, 1, 1u, 1));
    CHECK(c.factsToday == 1u && c.countingDay == 8,
          "and starts again on the next day — so the ring's size can be tuned "
          "from evidence instead of from feel");

    // ПИК — ТО, ЧЕМ ФОРМУЛА СЧИТАЕТСЯ, И ОН НЕ ТОТ ЖЕ, ЧТО СЧЁТ ДНЯ (M-254).
    // `factsToday` обнулился на смене суток; величина формулы — максимум по
    // ВСЕМ дням, иначе кап кольца калибровать нечем.
    CHECK(c.factsPeak == 5u,
          "the peak remembers the fattest day (5), not the current one (1)");
    for (int i = 0; i < 9; ++i) chronicle_record(c, killing(8, 1, 1, 1u, 1));
    CHECK(c.factsToday == 10u && c.factsPeak == 10u,
          "a fatter day raises the peak");
    chronicle_record(c, killing(9, 1, 1, 1u, 1));
    CHECK(c.factsToday == 1u && c.factsPeak == 10u,
          "негативный контроль: ТОЩИЙ день пик НЕ опускает — иначе прибор "
          "мерил бы последний день, а формуле нужен самый плодовитый");

    // И ГОРИЗОНТ ФОРМУЛЫ ВЫВЕДЕН ИЗ ТАБЛИЦЫ, А НЕ НАЗВАН ЧИСЛОМ: правка
    // строки рода обязана двигать его сама (ЗАКОН СЛОВАРЯ п.3).
    std::uint16_t longest = 0;
    for (std::size_t k = 0; k < std::size_t(FactKind::Count); ++k) {
        const std::uint16_t d = fact_kind_def(FactKind(k)).interestDays;
        if (d > longest) longest = d;
    }
    CHECK(kChronicleLongestInterestDays == longest && longest > 0,
          "the formula's horizon IS the table's longest interestDays");
}

// ── ЗДЕСЬ СТОЯЛИ ТРИ ЗАКОНА АННАЛОВ, И ОНИ СНЕСЕНЫ ВМЕСТЕ С ЯРУСОМ (M-184).
//
// Поимённо, чтобы снос был виден, а не угадывался:
//   · `test_the_annals_are_loud_when_they_fill` — громкий кап вместо тихой
//     потери прошлого (флаг `annalsFull` на 2^20 записях);
//   · `test_the_annals_keep_figures_and_forget_weather` — отбор «помнится
//     дело ИМЕНОВАННОГО, безымянная массовка растворяется»;
//   · `test_the_annals_are_asked_about_a_figure` — дверь запроса
//     `chronicle_annals_of`, «что мир знает об этой фигуре».
//
// ЭТО НЕ ЧИСТКА ПОКРЫТИЯ, А СНОС ОХРАНЯЕМОГО: законов, которые они
// утверждали, в мире больше нет (вердикт владельца 2026-10-30 «да сноси
// оба»). Свидетель, переживший свой закон, охранял бы ПУСТОТУ — и это та
// самая форма, которую §8 п.7 называет «тест охраняет дефект».
//
// ЧТО ОСТАЛОСЬ ЖИВЫМ И ПОЧЕМУ: закон СЛАВЫ (`renown_is_named`,
// `renown_for_deed`, `kRenownToBeNamed`) — он отвечает на другой вопрос
// («стал ли этот фигурой») и читается тремя дверями помимо летописи.
// Его свидетель — ниже, и из него убраны только те проверки, что спрашивали
// снесённый ярус.

// ── A NOBODY BECOMES SOMEBODY ────────────────────────────────────────────
// Owner, 2026-08-27: a nameless squad can become named, like a lord, once it
// has accumulated deeds — and from that day its deeds are kept for good.
// This is the rule that keeps sixteen thousand bands from drowning the
// world's memory while still letting any of them earn a place in it.
void test_a_band_becomes_a_figure_by_its_deeds() {
    // The bar is DERIVED from the table, not chosen: it is the most any single
    // deed is worth AGAINST A NOBODY, so "become a figure by doing once what a
    // figure does, or by adding up enough lesser things, or by beating
    // somebody who already was one".
    int most = 0;
    for (const FactKindDef& d : kFactKinds)
        most = std::max(most, int(d.baseRenown));
    CHECK(kRenownToBeNamed == most,
          "the bar IS the table: retune a row and it moves with it");
    CHECK(fact_kind_def(FactKind::Traded).baseRenown == 0,
          "negative control: selling food makes nobody anybody");
    // …and the partner's fame does not rub off either (owner, 2026-08-29:
    // «торговля — маленькое дело»): the victim's share is for deeds AGAINST
    // somebody, a mutual deal pays none — the kind row says so by column.
    CHECK(renown_for_deed(FactKind::Traded, 1000u) == 0u,
          "a deal with a famous city tithes none of its renown");
    CHECK(fact_kind_def(FactKind::Killed).victimShare,
          "the control is real: raiding the same city still pays the share");

    // ── THE VICTIM ANSWERS WHAT A DEED IS WORTH ──────────────────────────
    // Owner's ruling: renown is contextual, and the context is WHOM it was
    // done to. Every macro entity already carries what it is worth — its own
    // renown — so nothing had to be invented to say "killing a legend is
    // worth more than killing a peasant".
    const std::uint32_t vsNobody = renown_for_deed(FactKind::Killed, 0u);
    const std::uint32_t vsLegend = renown_for_deed(FactKind::Killed, 1000u);
    CHECK(vsNobody == fact_kind_def(FactKind::Killed).baseRenown,
          "killing a nobody is worth the deed's base and nothing more");
    CHECK(vsLegend > vsNobody,
          "and killing a legend is worth more — fame is made of fame, by "
          "construction rather than by a second rule");
    CHECK(vsLegend - vsNobody == 1000u / std::uint32_t(kRenownShareDivisor),
          "the share is a TENTH: ten victories over a man's equals make you "
          "his equal");
    CHECK(renown_for_deed(FactKind::Count, 500u) == 0u,
          "a deed of no kind is worth nothing, however famous the victim");

    CHECK(!renown_is_named(0), "a fresh band is nobody");
    CHECK(!renown_is_named(kRenownToBeNamed - 1), "and almost is not enough");
    CHECK(renown_is_named(kRenownToBeNamed), "one great deed is enough");

    // ── ЗДЕСЬ ЛЕТОПИСЬ ХРАНИЛА ОДИН И ТОТ ЖЕ ФАКТ ПО-РАЗНОМУ В ЗАВИСИМОСТИ
    // ОТ ЭТОГО БИТА, И ЭТА ПОЛОВИНА СВИДЕТЕЛЯ СНЕСЕНА (M-184). Она
    // утверждала: безымянное убийство — погода (кольцо держит, мир не
    // помнит), то же убийство фигурой — история; голод безвестного хутора —
    // погода, тот же голод славного города — легенда; и никакая слава не
    // делает недельную новость вечной (`kChronicleWeatherDays`). Все три
    // утверждения спрашивали АННАЛЫ, а яруса больше нет.
    //
    // ВМЕСТЕ С НИМИ УШЛИ ДВЕ ПРОВЕРКИ БИТА («род читается без бита», «бит —
    // единственное, что различает»): бит `kFactSubjectNamed` снят как
    // колонка-сирота, и утверждать о нём стало НЕЧЕГО.
    //
    // ЧТО ЭТО ОСТАВИЛО ПОД ОХРАНОЙ: закон славы целиком — планка ИЗ ТАБЛИЦЫ,
    // доля жертвы, «слава делается из славы», и предикат фигурности. Он
    // читается ОДНОЙ дверью — `record_battle_facts@src/macro/squad.h`
    // (пересчёт 2026-11-08), то есть охраняется живой закон, а не пустота.
    // Здесь стояло «`squad_is_named@src/app/main.cpp` и двумя дверями
    // `src/macro/squad.h`»: та дверь приложения имела ноль вызовов во всём
    // дереве и снесена той же ревизией, а второй двери `squad.h` не было
    // никогда — число читателей было завышено втрое в трёх файлах сразу.
}

// ── THE WORDS ARE DERIVED, NOT STORED ────────────────────────────────────
// CANON S20.1: a fact is numbers and the sentence is made at the moment of
// display. This is what turns the player's journal into a VIEW on the world's
// memory instead of eight thousand sentences in the save — and what makes
// localisation free, because the same past says itself differently by
// changing tables rather than history.
const char* name_squad(void*, std::uint32_t o) {
    return o == 42u ? "Bloody Karl" : nullptr;
}
const char* name_rooted(void*, std::uint32_t id) {
    return id == 3u ? "Ryazan" : nullptr;
}

void test_a_fact_says_itself() {
    WorldFact f{};
    f.day = 77;
    // РОД — ЖИВОЙ И С ДВУМЯ УЧАСТНИКАМИ: предмет проверки есть ФРАЗА
    // (субъект + ярлык рода + объект), поэтому роду достаточно иметь обе
    // ссылки. Стоял `OwnerChanged` — снесён как сирота 2026-11-03, и
    // свидетель переведён на живой род, а не похоронен вместе с ним.
    constexpr FactKind kVerb = FactKind::Taxed;
    f.kind = std::uint16_t(kVerb);
    f.subjectKind = fact_subject(FactSubject::Squad);
    f.subject = 42u;
    f.objectKind = std::uint8_t(FactSubject::Rooted);
    f.object = 3u;

    FactNaming naming{};
    naming.squad = name_squad;
    naming.rooted = name_rooted;

    char line[128];
    const int n = fact_sentence(f, naming, line, int(sizeof(line)));
    CHECK(n > 0 && line[n] == '\0', "a sentence is written and terminated");
    CHECK(std::strstr(line, "Bloody Karl") != nullptr, "it names the doer");
    CHECK(std::strstr(line, "Ryazan") != nullptr, "and what he did it to");
    // ОЖИДАНИЕ — ИЗ ТОЙ ЖЕ ТАБЛИЦЫ, ЧТО ЧИТАЕТ КОД (§8 п.4). Здесь стоял
    // ЛИТЕРАЛ «Changed hands», и он ломался дважды: при перекалибровке ярлыка
    // и при сносе самого рода. Ярлык спрашивается у строки — тогда
    // утверждение говорит «фраза берёт ГЛАГОЛ у таблицы», а не «ярлык равен
    // вот этим двум словам».
    CHECK(std::strstr(line, fact_kind_def(kVerb).label) != nullptr,
          "and the VERB is the kind's own label — a new kind speaks by being "
          "added to the table, with no code here");
    CHECK(std::strstr(line, "77") != nullptr, "and when");

    // A chronicle that could only speak about what it has names for would be
    // silent about most of the world. An unresolvable ordinal still gets an
    // honest noun.
    FactNaming mute{};
    char bare[128];
    fact_sentence(f, mute, bare, int(sizeof(bare)));
    CHECK(std::strstr(bare, "a band") != nullptr
          && std::strstr(bare, "a place") != nullptr,
          "with no names at all the fact is still a true sentence");
    CHECK(std::strstr(bare, "Bloody Karl") == nullptr,
          "negative control: the name came from the NAMING, not from the fact "
          "— which is the whole point of not storing it");

    // A tiny buffer truncates instead of running past its end.
    char tiny[8];
    const int t = fact_sentence(f, naming, tiny, int(sizeof(tiny)));
    CHECK(t < int(sizeof(tiny)) && tiny[t] == '\0',
          "a short buffer is filled and terminated, never overrun");

    // A fact of no kind says nothing rather than something wrong.
    WorldFact none{};
    char empty[16];
    CHECK(fact_sentence(none, naming, empty, int(sizeof(empty))) == 0
          && empty[0] == '\0',
          "a fact of no kind is not a sentence");
}

// ── "RECENT" IS THE ROW'S WORD, NOT THE READER'S ─────────────────────────
void test_the_kind_owns_its_own_window() {
    Chronicle c;
    chronicle_init(c, 64, 64);

    // A sale and a killing on the same day, in the same place.
    WorldFact sale{};
    sale.day = 1;
    sale.kind = std::uint16_t(FactKind::Traded);
    sale.subjectKind = std::uint8_t(FactSubject::Cell);
    sale.x = 8; sale.y = 8;
    chronicle_record(c, sale);

    WorldFact kill = sale;
    kill.kind = std::uint16_t(FactKind::Killed);
    chronicle_record(c, kill);

    // Ten days later the sale is stale (its row says 8 days) and the killing
    // is still news (its row says 32).
    Collected trades, kills;
    const std::int32_t today = 11;
    chronicle_near_kind(c, 8, 8, 1, FactKind::Traded, today, collect, &trades);
    chronicle_near_kind(c, 8, 8, 1, FactKind::Killed, today, collect, &kills);

    CHECK(trades.facts.empty(),
          "a sale is stale in a week, because ITS ROW says so");
    CHECK(kills.facts.size() == 1u,
          "and a killing on the same day is still news, because its row says "
          "something else — one reader, two windows, no restated numbers");
}

} // namespace

int main() {
    test_the_record_is_flat_and_the_rows_are_rows();
    test_a_witcher_finds_the_monster_by_its_traces();
    test_the_chains_truncate_themselves();
    test_recent_walks_time_without_an_index();
    test_the_chronicle_refuses_what_it_cannot_record();
    test_the_world_counts_its_own_facts();
    test_a_band_becomes_a_figure_by_its_deeds();
    test_a_fact_says_itself();
    test_the_kind_owns_its_own_window();
    return sm::test::report("chronicle_test");
}

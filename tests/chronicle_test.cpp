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
    f.objectKind = std::uint8_t(FactSubject::Landmark);
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
}

// ── TWO TIERS: the world forgets weather and remembers figures ───────────
// Owner's ruling: «вечно помнятся дела ИМЕНОВАННЫХ — лордов, игрока, городов;
// безымянная массовка растворяется». In this world that is not a heuristic
// but a consequence — a squad IS a lord and carries an ordinal, a landmark and
// a faction likewise, while the crowd below has no ordinal at all and can only
// appear as `amount`.
void test_the_annals_keep_figures_and_forget_weather() {
    Chronicle c;
    chronicle_init(c, 128, 128);

    // Weather: the land itself yielded wood. Nobody named took part.
    WorldFact harvest{};
    harvest.day = 5;
    harvest.kind = std::uint16_t(FactKind::Gathered);
    harvest.subjectKind = std::uint8_t(FactSubject::Cell);
    harvest.x = 10; harvest.y = 10;
    harvest.amount = 8;
    for (int i = 0; i < 50; ++i) chronicle_record(c, harvest);

    CHECK(c.annals.empty(),
          "fifty harvests by nobody are weather: the world does not remember "
          "them");

    // History: a lord took a city.
    WorldFact conquest{};
    conquest.day = 6;
    conquest.kind = std::uint16_t(FactKind::OwnerChanged);
    conquest.subjectKind = std::uint8_t(FactSubject::Squad);
    conquest.subject = 42u;
    conquest.objectKind = std::uint8_t(FactSubject::Landmark);
    conquest.object = 3u;
    conquest.x = 10; conquest.y = 10;
    chronicle_record(c, conquest);

    CHECK(c.annals.size() == 1u,
          "a deed between named figures is history, and one is enough");
    CHECK(c.annals[0].subject == 42u && c.annals[0].object == 3u,
          "and it remembers WHO, which is the whole of the rule");

    // A nameless death still reaches the RING — the witcher must find it —
    // and still does not reach the annals. That is the two tiers in one line.
    WorldFact anonymous{};
    anonymous.day = 6;
    anonymous.kind = std::uint16_t(FactKind::Killed);
    anonymous.subjectKind = std::uint8_t(FactSubject::Cell);
    anonymous.x = 10; anonymous.y = 10;
    anonymous.amount = 3;
    chronicle_record(c, anonymous);

    Collected near;
    chronicle_near(c, 10, 10, 0, /*sinceDay*/0, collect, &near);
    bool sawAnonymous = false;
    for (const WorldFact& f : near.facts) {
        if (f.kind == std::uint16_t(FactKind::Killed)
            && f.subjectKind == std::uint8_t(FactSubject::Cell)) {
            sawAnonymous = true;
        }
    }
    CHECK(sawAnonymous,
          "the nameless killing IS in the ring — otherwise the witcher could "
          "never pick up the trail");
    CHECK(c.annals.size() == 1u,
          "negative control: and it is STILL not in the annals — the two tiers "
          "answer two different questions about the same fact");
}

// ── The annals answer "what is known about this figure" ──────────────────
void test_the_annals_are_asked_about_a_figure() {
    Chronicle c;
    chronicle_init(c, 128, 128);

    auto deed = [&](int day, std::uint32_t lord, std::uint32_t city) {
        WorldFact f{};
        f.day = day;
        f.kind = std::uint16_t(FactKind::OwnerChanged);
        f.subjectKind = std::uint8_t(FactSubject::Squad);
        f.subject = lord;
        f.objectKind = std::uint8_t(FactSubject::Landmark);
        f.object = city;
        f.x = 20; f.y = 20;
        chronicle_record(c, f);
    };
    deed(1, 7u, 1u);
    deed(2, 8u, 2u);
    deed(3, 7u, 3u);

    Collected his;
    chronicle_annals_of(c, std::uint8_t(FactSubject::Squad), 7u, 100,
                        collect, &his);
    CHECK(his.facts.size() == 2u, "two of the three deeds are his");
    CHECK(his.facts.front().day == 3, "newest first, like every view here");

    // He is also findable as the OBJECT of someone else's deed — a grudge is
    // a fact about both parties, and history does not care which side of the
    // verb you were on.
    Collected city;
    chronicle_annals_of(c, std::uint8_t(FactSubject::Landmark), 3u, 100,
                        collect, &city);
    CHECK(city.facts.size() == 1u,
          "the city remembers being taken, though it did nothing");

    Collected all;
    chronicle_annals_of(c, 0u, 0u, 100, collect, &all);
    CHECK(all.facts.size() == 3u,
          "negative control: asking about nobody returns the whole legend, so "
          "the filter above really filtered");
}

// ── A NOBODY BECOMES SOMEBODY ────────────────────────────────────────────
// Owner, 2026-08-27: a nameless squad can become named, like a lord, once it
// has accumulated deeds — and from that day its deeds are kept for good.
// This is the rule that keeps sixteen thousand bands from drowning the
// world's memory while still letting any of them earn a place in it.
void test_a_band_becomes_a_figure_by_its_deeds() {
    // The bar is DERIVED from the table, not chosen: it is the most any single
    // deed is worth, so "become a figure by doing once what a figure does, or
    // by adding up enough lesser things".
    int most = 0;
    for (const FactKindDef& d : kFactKinds) most = std::max(most, int(d.renown));
    CHECK(kRenownToBeNamed == most,
          "the bar IS the table: retune a row and it moves with it");
    CHECK(fact_kind_def(FactKind::OwnerChanged).renown == std::uint16_t(most),
          "and taking a place from its owner is what a figure does");
    CHECK(fact_kind_def(FactKind::Traded).renown == 0,
          "negative control: selling bread makes nobody anybody");

    CHECK(!renown_is_named(0), "a fresh band is nobody");
    CHECK(!renown_is_named(kRenownToBeNamed - 1), "and almost is not enough");
    CHECK(renown_is_named(kRenownToBeNamed), "one great deed is enough");

    // ...and the chronicle keeps the SAME fact differently depending on it.
    Chronicle c;
    chronicle_init(c, 64, 64);

    WorldFact nobody{};
    nobody.day = 1;
    nobody.kind = std::uint16_t(FactKind::Killed);
    nobody.subjectKind = fact_subject(FactSubject::Squad, /*named*/false);
    nobody.subject = 5u;
    nobody.x = 8; nobody.y = 8;
    nobody.amount = 1;
    chronicle_record(c, nobody);
    CHECK(c.annals.empty(),
          "a nameless band's killing is weather: the ring holds it, the world "
          "does not remember it");

    WorldFact somebody = nobody;
    somebody.subjectKind = fact_subject(FactSubject::Squad, /*named*/true);
    chronicle_record(c, somebody);
    CHECK(c.annals.size() == 1u,
          "the SAME deed by a figure is history — the difference is one bit "
          "about the doer, not the deed");

    // The bit rides in the kind byte, and reading the kind must not see it.
    CHECK(fact_subject_kind(somebody.subjectKind)
              == std::uint8_t(FactSubject::Squad),
          "the packed reference still reads as a squad");
    CHECK(fact_subject_marked_named(somebody.subjectKind)
          && !fact_subject_marked_named(nobody.subjectKind),
          "and the bit is the only thing that differs");

    // A city is named by construction: it had a name the day it was founded,
    // so nobody has to mark it.
    WorldFact city{};
    city.day = 1;
    city.kind = std::uint16_t(FactKind::Starved);
    city.subjectKind = std::uint8_t(FactSubject::Landmark);
    city.subject = 2u;
    city.x = 8; city.y = 8;
    chronicle_record(c, city);
    CHECK(c.annals.size() == 2u,
          "a landmark needs no bit: a city has a name by construction");
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
const char* name_landmark(void*, std::uint32_t id) {
    return id == 3u ? "Ryazan" : nullptr;
}

void test_a_fact_says_itself() {
    WorldFact f{};
    f.day = 77;
    f.kind = std::uint16_t(FactKind::OwnerChanged);
    f.subjectKind = fact_subject(FactSubject::Squad, true);
    f.subject = 42u;
    f.objectKind = std::uint8_t(FactSubject::Landmark);
    f.object = 3u;

    FactNaming naming{};
    naming.squad = name_squad;
    naming.landmark = name_landmark;

    char line[128];
    const int n = fact_sentence(f, naming, line, int(sizeof(line)));
    CHECK(n > 0 && line[n] == '\0', "a sentence is written and terminated");
    CHECK(std::strstr(line, "Bloody Karl") != nullptr, "it names the doer");
    CHECK(std::strstr(line, "Ryazan") != nullptr, "and what he did it to");
    CHECK(std::strstr(line, "Changed hands") != nullptr,
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
    test_the_annals_keep_figures_and_forget_weather();
    test_the_annals_are_asked_about_a_figure();
    test_a_band_becomes_a_figure_by_its_deeds();
    test_a_fact_says_itself();
    test_the_kind_owns_its_own_window();
    return sm::test::report("chronicle_test");
}

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

#include <cstdio>
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

} // namespace

int main() {
    test_the_record_is_flat_and_the_rows_are_rows();
    test_a_witcher_finds_the_monster_by_its_traces();
    test_the_chains_truncate_themselves();
    test_recent_walks_time_without_an_index();
    test_the_chronicle_refuses_what_it_cannot_record();
    test_the_world_counts_its_own_facts();
    return sm::test::report("chronicle_test");
}

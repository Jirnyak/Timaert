# The World's Chronicle — CANON S20.1, built

Track write-up, 2026-08-27/28. This is THE doc for what the world remembers:
what a fact is, why it is shaped that way, where each piece lives, and the
laws that keep one memory from becoming two. The intent it implements is
[CANON.md](CANON.md) §S20.1 and [work_vector.md](work_vector.md) §3.

- **Code:** [macro/chronicle.h](src/macro/chronicle.h) /
  [.cpp](src/macro/chronicle.cpp) (the record, the tiers, the queries, the
  words), [events/event_bus.h](src/events/event_bus.h) (the door),
  [sub/engine.h](src/sub/engine.h) (the micro→macro door and zones),
  [macro/state.h](src/macro/state.h) (`GameState::chronicle`, landmark renown),
  [ecs/components.h](src/ecs/components.h) (`MacroNpcRuntime::renown`).
- **Tests:** `chronicle_test` (the record, both tiers, renown, the words, the
  windows), `world_tick_parity_test` (a famine is one fact), `save_roundtrip`
  (the ring comes back ASKABLE), smokes `force_encounter` and `spire_climb`.

## The question this system exists to answer

The owner's own test case, and every decision below follows from it. A
witcher-like squad walks the villages and takes contracts on monsters; a
monster squad that kept killing peasants nearby LEFT TRACES; the witcher goes
and finds it himself. For that, something must be able to ask the world:

> what happened near this village in the last thirty days?

- it is asked BY PLACE and BY TIME ⇒ facts must be indexable, and a string is
  not an index;
- the witcher may arrive a game week later ⇒ facts must survive the SAVE;
- every squad on the map will ask it ⇒ the scan must be an ARRAY walk, not a
  chase through heap pointers.

A `std::string` inside the record breaks all three at once. **A fact is a POD
not to be fast, but because the living world is impossible otherwise.**

## The laws

**A fact is a CHANGE TO THE WORLD, not an action.** The chronicle is not an
input log. Ringing a bell is nothing; a bell that raised the guard is the fact
"alarm raised", named after the change and not after the button. Opening a
door is scene state. This is what keeps the table small and the rate low.

**The table of fact kinds is a table of DEEDS, not of mechanisms.** The
subworld hangs an INTERACTION on any place or object and it can mean anything,
so "interacted" can carry no honest weight — whatever number it held would be
wrong for a door or wrong for a spire. The CONTEXT chooses the deed; the deed
carries the weight. Most interactions choose no deed at all.

**Two different deeds must wear different kinds.** Found the hard way: standing
in a spire's circle and draining its orb both filed `Explored`, so the "have I
been here" check conflated them and one visit was remembered twice.

**Transitions, not states.** A town hungry for a season is ONE famine. A
chronicle that filed the state daily would bury the day it began under the days
it continued.

**Words are DERIVED at the moment of display.** `fact_sentence` builds the line
from the kind's own label plus three borrowed name resolvers. The player's
journal becomes a VIEW on the world's memory instead of sentences in the save,
and localisation costs nothing: the same past speaks another language by
changing tables, not history.

**One memory across both layers.** What happened DOWN THERE happened, as far as
the world is concerned, in the MACRO CELL that contains that subworld. The
layers do not keep two memories to reconcile — they keep one, and the place is
the seam.

## The record

32 bytes, every field a number
(`WorldFact`, [macro/chronicle.h](src/macro/chronicle.h)):

| field | meaning |
|---|---|
| `day` | the world day. Not a tick: a chronicle is asked in DAYS, and `seq` already orders two facts inside one — a second time column would be a second answer to one question. |
| `seq` | global fact number, and the ring's proof of life |
| `nextInCell` | intrusive chain, newest→oldest, per index cell |
| `kind` | a row of the deed table |
| `subjectKind` / `objectKind` | `FactSubject` (Squad / Landmark / Cell / Faction) **plus one bit**: is this participant NAMED. The bit rides in the kind byte because the record is 32 bytes and the kinds number five. |
| `subject` / `object` | ordinal in that kind's own id space |
| `x`, `y` | the macro cell it happened on |
| `amount` | what the kind's row says it means. SIGNED — reputation and coin move both ways. |

## Two tiers, one record

Dwarf Fortress keeps every historical event of every year in memory, and that
is its known illness: a long world generates into gigabytes and then crawls on
its own past, so late versions added historical pruning. The half DF gets right
is the distinction between an EVENT and a LEGEND, and that is the half taken.

- **the RING** — what the world is ASKED. Indexed by cell, forgets by age. Its
  size is a FORMULA with one unmeasured term: `longest interestDays × the
  world's facts per day`. The rate has never been measured, which is why
  `Chronicle::factsToday` ships as part of the contract — retune from the
  counter, not from feel.
- **the ANNALS** — what the world REMEMBERS. Append-only, no spatial index,
  rides the save WHOLE. They are not a cache: a legends mode (owner's plan)
  will read exactly them. The cap is LOUD (`annalsFull`) rather than silently
  dropping the past.

**The chains self-truncate.** Eviction is strictly oldest-first, so the first
dead link in a cell's chain is the end of its live part. No unlinking, no
bookkeeping: a link is followed only while its sequence number proves the slot
still holds what it linked to.

## Renown — what the world thinks of you

**Every MACRO entity with an identity carries one**: a band
(`MacroNpcRuntime::renown`), a city and a village (`Settlement`/`Village`). The
microworld has none — a mob, a projectile, a house have no standing to win or
lose, and that is a different layer and a different question.

**A band is not named by birth.** It starts as one more band whose deeds are
weather the ring forgets in a season; do enough and it is a FIGURE, whose deeds
enter the annals for good. ONE number is stored — "is it named" is DERIVED
(`renown_is_named`), so a counter and a flag can never disagree.

**What a deed is worth: the VICTIM answers.** Nothing had to be invented,
because every macro entity already carries what it is worth — its own renown:

```
gain = the deed row's base + victim's renown / kRenownShareDivisor
```

Recursive by construction — fame is made of fame — with no second rule for
famous victims. The share is a tenth, and the number is the sentence: *ten
victories over a man's equals make you his equal.*

So the deed row carries a BASE, not an answer, and the table splits by itself:
deeds against SOMEBODY have small bases (what they are really worth, the victim
says), and deeds against the LAND — which has no standing — are their base
(fell a forest 1, raise a field 10, find a vein 15).

**The bar to become a figure is DERIVED**: the most any single deed is worth
against a nobody. So the rule is a sentence — *become a figure by doing once
what a figure does, by adding up enough lesser things, or by beating somebody
who already was one* — and retuning a row moves the bar with it, because the
bar IS the table.

## The doors

| door | who uses it |
|---|---|
| `chronicle_record(c, fact)` | THE door. Everything else is a wrapper. |
| `EventBus::record(fact)` | the bus IS the chronicle's door (owner's ruling); the frame's facts are a RANGE of sequence numbers, not a second buffer |
| `record_deed(app, fact, subjectEntity)` | app-side wrapper: resolves the entity to its save-stable ordinal, marks the named bit, files the fact AND pays the renown. The two halves cannot come apart — a writer that filed and forgot would be a world where nobody becomes somebody. |
| `SubworldEngine::record_world_fact` | micro→macro: stamps the containing macro cell |
| `record_landmark_fact` (world_tick) | a place has no entity and earns no renown — it is named the day it is founded — so it files directly |

## Zones — places that mean something

A subworld scene carries a flat, capped array of zones: `{place, radius, deed}`.
Crossing INTO one is a fact (crossing, not standing — the same law the famine
follows). A generator that wants a meaning writes `add_sub_zone`, not a
mechanism.

**The dedup is the world's own memory.** "Have I been here" is asked of the
chronicle, because a place already remembers that somebody stood in it. A
separate `visited` flag would be a second truth about one past — and it would
have to be saved, loaded and kept in step.

## Asking

| query | shape |
|---|---|
| `chronicle_near(x, y, radius, sinceDay)` | the witcher's question: 3×3 index cells, newest first |
| `chronicle_near_kind(..., kind, today)` | the same, with the window the KIND's own row gives — a sale is stale in a week, a killing is news for a season, and no reader restates either number |
| `chronicle_recent(sinceDay, limit)` | the journal's view: backwards through the ring, which is backwards through time, with no index |
| `chronicle_annals_of(kind, ordinal, limit)` | what is known of a figure — as subject OR object, because a grudge is a fact about both parties |

## What we deliberately do NOT want

1. **A fact per player action.** The chronicle is not an input log.
2. **A row per object.** "Orb drained", "bell rung", "door opened" as separate
   kinds = a table that grows with content instead of with meaning.
3. **Facts nobody would ever ask about.** If no reader would ask "what happened
   here", it is not a fact.
4. **Weight that depends on which object caused it.** That was the `Interacted`
   mistake: no honest number exists for a row that means anything.
5. **Two truths** — a fact and a flag saying the same thing.

## Open

- **Writers.** Wired: auto-resolved death, famine and revolt transitions, a
  drained spire, a zone crossing. Missing because the MECHANIC does not exist
  yet: capturing a settlement (`LandmarkChangeOwner` has a tag and no emitter).
- **The fact rate** is still unmeasured, so `kChronicleFacts` is provisional by
  construction. `factsToday` is the instrument.
- **A faction has no renown store** — one field, when the politics track lands;
  it reads through the same door.
- **`GameEvent` is still a struct with strings**, not yet a slice of the
  chronicle. Most of its strings duplicate an ordinal already in the same
  event; the tails are persistent string lists and a logic layer that cannot
  see the world.
- **?27 — the text system** (authored and procedural dialogue) is a separate
  track. "Words from facts" answers the journal, not the conversation.

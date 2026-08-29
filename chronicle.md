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
  windows, the `victimShare` column with its `Robbed` control),
  `journal_test` (the deed door files AND pays as one action — with the
  negative control that a raw `chronicle_record` grants nothing; a possessed
  lord's deeds are the player's participation; a captured copy carries no ring
  link), `world_tick_parity_test` (a famine is one fact), `save_roundtrip`
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
  size is a FORMULA: `longest interestDays × the world's facts per day`. The
  rate is MEASURED now (`chronicle_rate` smoke, 2026-08-28): ~80–100 facts a
  day of background (caravan deals, kills), 240–340 at the famine wave — worst
  day × 64 interest days ≈ 21.6k, so `kChronicleFacts = 2^16` stands confirmed.
  `factsToday` remains the instrument; retune from it, never from feel.
- **the ANNALS** — what the world REMEMBERS. Append-only, no spatial index,
  rides the save WHOLE. They are not a cache: a legends mode (owner's plan)
  will read exactly them. The cap is LOUD (`annalsFull`) rather than silently
  dropping the past — and HEARD since 2026-08-29: the Journal panel shows a
  red `[world annals full]` badge (the flag used to have no listener). The
  load path VALIDATES what it reads (`read_chronicle`, save.cpp): an annals
  fact with an impossible `seq` or an out-of-table `kind` fails the load
  instead of poisoning the eternal memory.

**What reaches the annals** (owner, 2026-08-28): *a fact a FIGURE took part
in, if its kind is more than a week's news.* Both halves come from columns
that already existed. Figure-ness is derived from renown for EVERYONE — a
landmark has a NAME the day it is founded, but a name is a word, not
historical weight: a fresh hamlet's hunger is weather, a renowned city's is
legend. And no fame makes a weather kind eternal
(`kChronicleWeatherDays = 8`, the table's own shortest interest tier: Traded,
Gathered) — «приём хлеба в город — не событие», however famous the city. The
effect on the measured world: a young world's annals hold ~0 instead of ~150
a day — eternity is earned.

**The chains self-truncate.** Eviction is strictly oldest-first, so the first
dead link in a cell's chain is the end of its live part. No unlinking, no
bookkeeping: a link is followed only while its sequence number proves the slot
still holds what it linked to.

## Renown — what the world thinks of you

**Every MACRO entity with an identity carries one**: a band
(`MacroNpcRuntime::renown`) and every landmark (`Landmark::renown` — one
column of the ONE roster since v62, [landmarks.md](landmarks.md)). The
standing doors — `renown_slot` / `renown_of` / `grant_renown` — live beside
the deed door in `macro/squad.h`. The microworld has none — a mob, a projectile, a house have no standing to win or
lose, and that is a different layer and a different question.

**Nobody is named by birth — bands and PLACES alike** (owner, 2026-08-28).
A band starts as one more band whose deeds are weather the ring forgets in a
season; do enough and it is a FIGURE, whose deeds enter the annals for good.
The same law now holds for landmarks: a city earns its historical weight by
what it does and suffers (`record_landmark_fact` pays the subject its deed's
worth — a town that lived through a revolt is KNOWN). ONE number is stored —
"is it named" is DERIVED (`renown_is_named`), so a counter and a flag can
never disagree. The named bit in a fact is the participant's standing AT THE
MOMENT of the deed, marked from PRE-deed renown («с этого дня её дела идут в
анналы») — queries compare kinds, never the packed byte.

**What a deed is worth: the VICTIM answers.** Nothing had to be invented,
because every macro entity already carries what it is worth — its own renown:

```
gain = the deed row's base + victim's renown / kRenownShareDivisor
```

Recursive by construction — fame is made of fame — with no second rule for
famous victims. The share is a tenth, and the number is the sentence: *ten
victories over a man's equals make you his equal.*

The share is paid for deeds AGAINST somebody; a mutual deal pays none — the
kind row's `victimShare` column says so, and `Traded` is the row that says
false (owner, 2026-08-29: «торговля — маленькое дело», a caravan run must not
tithe the partner city's fame).

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
| `EventBus::record(fact)` | the bus IS the chronicle's door (owner's ruling); the frame's facts are a RANGE of sequence numbers, not a second buffer. The bus's own `WorldHistoryEntry` ring — a FOURTH store of the past beside ring/annals/journal — died 2026-08-29 (`history_at` / `query_history` gone, `flush()` lost its date arguments); the deed door below files straight through `chronicle_record` |
| `record_deed(world, gs, fact, subjectEntity)` (macro/squad.h) | THE deed wrapper, macro-side beside its landmark twin: resolves the entity to its save-stable ordinal, marks the named bits from each participant's PRE-deed renown, files the fact AND pays the renown. The two halves cannot come apart — a writer that filed and forgot would be a world where nobody becomes somebody. It lived on the app layer at first; the UI's and the subworld's writers could not reach it there and filed for free, which is why the door moved down to macro. |
| `SubworldEngine::record_world_fact` | micro→macro: stamps the containing macro cell, then files through `record_deed` — a drained orb or a crossed circle pays the doer like a kill above ground |
| `record_landmark_fact` (state.h) | the landmark twin of `record_deed`: marks figure-ness from the place's own renown, files the fact AND pays the deed (base + a tenth of the object's renown). The old "a place earns no renown" was a hole in the one-door law, closed 2026-08-28 |
| `player_journal_capture` (macro/journal.h) | a READER, not a writer: once a tick the player asks "what happened since seq N" and copies what is HIS — participation (subject/object = his squad, or the lord he currently possesses) and locality (his cell, while he stood there). The copy drops `nextInCell` — the chain link is the ring index's wiring, not the fact's. No fact writer knows the journal exists |

## Zones — places that mean something

A subworld scene carries a flat, capped array of zones: `{place, radius, deed}`.
Crossing INTO one is a fact (crossing, not standing — the same law the famine
follows). A generator that wants a meaning writes `add_sub_zone`, not a
mechanism.

**The dedup is the world's own memory.** "Have I been here" is asked of the
chronicle, because a place already remembers that somebody stood in it. A
separate `visited` flag would be a second truth about one past — and it would
have to be saved, loaded and kept in step.

## The player's journal — knowledge, not a second memory

S11 applied to history (owner, 2026-08-28): the world does not TELL what the
player never learned. The chronicle stays one and whole — legends mode reads
it raw — and what changes is only the VIEW.

- **What he learns**: what he TOOK PART in (his squad ordinal as subject or
  object, wherever it happened) and what happened ON HIS CELL while he stood
  there. Rumours will enter through this same door: a rumour is a chronicle
  record read to him, and hearing appends like witnessing does.
- **The container** (`PlayerState::journal`): COPIES of the 32-byte records —
  the ring forgets and the journal must not («это лог игрока всей его игры»),
  and a copy of an immutable record is not a second truth, because there is
  nothing for the two to disagree about. Append-only, LOUD cap
  (`kJournalFactsCap = 2^20` — the annals' own size: his whole-game log gets
  no less room than the world's eternal memory), the whole cap reserved in
  one block at first capture so the game loop never allocates for it.
- **Words at display time**, as everywhere: the journal panel renders
  `fact_sentence` through the app's naming resolvers (`app_fact_naming`); no
  sentence is stored anywhere.
- **The session feed is neither** («не хранится даже в сессии»): "Game
  saved.", "You have learned X!" go to `SessionFeed` — a POD ring the HUD
  draws and fades, never serialized. Three questions, three answers: the
  world remembers by the chronicle, the player knows by his journal, the
  moment speaks through the feed — and there is no fourth store.

## Annihilation — a drained thing no longer exists

Owner, 2026-08-28: «истощённая жила — это не существующая жила; истощённый
шпиль — это смена ландмарка».

- **A worked-out vein LEAVES the deposit map** the moment it runs dry; the
  chronicle's `Drained` fact is the only record of what stood there (amount =
  the resource registry row + 1 — the land has no ordinal to ride as the
  object). Scarcity needs no memorial: `DepositLayer::virginUnits` — what the
  world was BORN with — is derived from terrain + seed at layer build (the
  load path rebuilds the layer anyway, so it recomputes for free), and the
  «железо родится от скудности» law prospects against it. Discovery can push
  the live stock above the born level; a richer-than-born world simply misses
  nothing.
- **A drained spire's fact points at the PLACE**: object = the spire's
  landmark ordinal (possible only because of the one landmark id space, v54);
  the spell it held resolves from the spire itself. The full form — a spent
  spire BECOMES another landmark kind — waits for the S9 transition
  machinery.

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
  drained spire, a zone crossing, a worked-out vein, every caravan deal, the
  player's own deals (settlement and NPC), a struck vein (`Discovered`,
  subject = the CELL — the land found it). Missing because the MECHANIC does
  not exist yet: capturing a settlement (`LandmarkChangeOwner` has a tag and
  no emitter).
- **A faction has no renown store** — one field, when the politics track
  lands; it reads through the same door.
- **`GameEvent` is still a struct with strings**, not yet a slice of the
  chronicle. The spellbook's own strings died first (flat ordinal rows, v59);
  the quest identities followed (v63, 2026-08-29): `Quest.ordinal` from the
  one issuer `nextQuestOrdinal` (issued at ACCEPT — an offer is a seed-
  regenerated projection until taken; its pre-accept identity is the POD
  provenance triple {giver, generator slot, bornDay}, which is also the
  same-day re-offer dedup that the eternal `completedQuestIds`/
  `failedQuestIds` string lists actually were), the codex became a registry
  (`macro/codex.h`, unlock = a bit per ordinal), and the Quest*/CodexUnlock
  events carry ordinals in `a` with no `s1`. Next is the slice itself.
  `NodeContext` still cannot see the world — it needs a reading door in the
  `FactNaming` shape.
- **Rumours** — the journal door is built; the ACQUISITION mechanic (tavern?
  an encounter's word of mouth?) is the owner's gameplay call.
- **?27 — the text system**: authored text will run through encapsulated
  LOCALIZATION from the start (want Russian — you know exactly which strings
  to fill); procedural lines are DEFERRED to a Markov-context core with
  designed skeletons («банда бандитов числом ~50 нападает на деревню <имя>»
  — an M&B/DF/Qud hybrid). "Words from facts" answers the journal, not the
  conversation.

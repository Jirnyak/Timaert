# Progression — Прогрессия игры

How the game advances over a playthrough: **levels, spell unlocks, and the
plot/event arc** — driven by the L3 event system.

- **Code:** [events/](src/events) —
  [event_bus.h](src/events/event_bus.h),
  [logic_nodes.h](src/events/logic_nodes.h),
  [node_registry.h](src/events/node_registry.h),
  [effect_applicator.h](src/events/effect_applicator.h);
  plot in [content/plot/](src/content/plot)
- **Architecture:** [ARCHITECTURE.md](ARCHITECTURE.md) §L3 — Event System, §L4

## Model

- **EventBus + LogicNodeEngine:** condition→effect graph. Nodes react to events
  and emit new ones — the core control-flow of the game.
- **Levels:** XP thresholds ([rpg.md](rpg.md)) raise the level inside `award_exp`,
  the ONE path that grants and consumes experience — so a level a quest paid for
  is a level the player has.
- **Levelling has NO feedback yet.** A `sys_level_up` node used to sit in the
  registry waiting on `EventTag::PlayerLevelUp` to pop a "Level Up!" dialog, but
  **nothing ever emitted that event** — all four `award_exp` call sites take a
  `LevelData` and no bus — so the dialog never appeared in play. The node and its
  test were deleted 2026-08-05 rather than left as furniture; the event tag
  survives in `event_types.h` for whoever wires this up for real. Note how it hid
  for so long: its unit test emitted `PlayerLevelUp` itself, proving the handler
  worked while saying nothing about whether the event is ever raised.
- **Attribute & skill points:** 3 attribute + 1 skill point per level, spent
  through `spend_attribute_point` / `spend_skill_point`. Skills follow one law —
  a rank is a percent, capped at 100 — see [rpg.md](rpg.md).
- **Spells:** learned / unlocked through the spell book ([spells.md](spells.md)).
- **Plot:** L4 pure-data `LogicNode` factories (intro slides, chapters) applied
  via `effect_applicator`; the quest arc mixes main / procedural / side
  ([quests.md](quests.md)).

## Data-driven extension

Add plot content → one `content/plot/*.cpp` exporting `LogicNode[]`, registered
from the owning module. Add a system reaction → one node in `node_registry`.

## Connections

The arc this spine is meant to carry is the **ten-year clock and its endings**
([lore.md](lore.md) §4, §8): ten in-game years = 10 × 2²⁰ ticks ≈ 45.5 real
hours at the shipped ladder ([time.md](time.md)), after which the default
prophecy fires unless the player took the witch or the rebellion route. None of
that is built yet — see the parity ledger in [lore.md](lore.md) §11.

The spine that ties RPG levels, spell unlocks, quests, and events into a single
advancing arc. Effect application is centralised in
[effect_applicator](src/events/effect_applicator.h).

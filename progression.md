# Progression — Прогрессия игры

How the game advances over a playthrough: **levels, spell unlocks, and the
plot/event arc** — driven by the L3 event system.

- **Code:** [events/](src/events) —
  [event_bus.h](src/events/event_bus.h),
  [logic_nodes.h](src/events/logic_nodes.h),
  [node_registry.h](src/events/node_registry.h),
  [effect_applicator.h](src/events/effect_applicator.h);
  plot in [content/plot/](src/content/plot)
- **TS origin:** `game/event-bus.ts`, `game/logic-nodes.ts`, `game/plot/*`
- **Architecture:** [ARCHITECTURE.md](ARCHITECTURE.md) §L3 — Event System, §L4

## Model

- **EventBus + LogicNodeEngine:** condition→effect graph. Nodes react to events
  and emit new ones — the core control-flow of the game.
- **Levels:** XP thresholds ([rpg.md](rpg.md)) fire a level-up node. Experience
  is granted and consumed by ONE path (`award_exp`), so a level a quest paid for
  is a level the player has.
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

The spine that ties RPG levels, spell unlocks, quests, and events into a single
advancing arc. Effect application is centralised in
[effect_applicator](src/events/effect_applicator.h).

# Quests — Квесты

Data-driven quest framework: objective and reward **registries**, plus
procedural generation from world context. No hardcoded if-chains.

- **Code:** [events/quests/](src/events/quests) —
  [quest_types.h](src/events/quests/quest_types.h),
  [quest_engine.h](src/events/quests/quest_engine.h);
  generators in [content/quests/procedural.h](src/content/quests/procedural.h)
- **TS origin:** `game/quests/*`
- **Architecture:** [ARCHITECTURE.md](ARCHITECTURE.md) §Quest System

## Model

- **Six objective verbs:** visit_cell, find_location, deliver_items,
  destroy_npc, wait_at, interact_cell (checked against player state + events).
- **Five reward types:** gold, xp, item, reputation, event.
- **`QuestEngine::tick`:** expiry → objective checks (`kObjectiveCheckers`) →
  rewards (`kRewardAppliers`) → emit complete/fail. Markers track spatial
  objectives.
- **Procedural:** cities get 2–4 quests, villages 1–2, from economy/distance/mood.

## Data-driven extension

New verb → one `kObjectiveCheckers` entry + one `Objective` discriminant. New
reward → one `kRewardAppliers` entry. New procedural quest → one generator in
`kQuestGenerators`.

## Connections

Quests span the progression arc ([progression.md](progression.md)); place
markers ([landmarks.md](landmarks.md)); reward the RPG sheet ([rpg.md](rpg.md));
read economy context ([economy.md](economy.md)).

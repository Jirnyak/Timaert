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
- **`QuestEngine::tick`:** expiry → per-objective checks (`eval_objective`) →
  rewards (`emit_reward`) → emit complete/fail. The engine is **pure**: it
  mutates quest/player state and the event bus only — never the map overlay.
- **Quest markers** are a *derived* projection of the active quests onto the
  universal marker layer, not engine state (see below).
- **Procedural:** cities get 2–4 quests, villages 1–2, from economy/distance/mood.

## Quest markers — a derived overlay

Active quests show on the world map as gold **"!"** pins, with **no** bespoke
rendering path — they reuse the universal marker layer ([markers.h](src/macro/markers.h),
[landmarks.md](landmarks.md)):

- **Producer:** `rebuild_quest_markers(gs, activeQuests)`
  ([quest_engine.h](src/events/quests/quest_engine.h)) rebuilds the whole
  `quest_*` slice of `GameState::markers`. One `MarkerStyle::Quest` pin per
  **incomplete** objective whose completion is anchored to a world cell — the
  cell resolver mirrors `eval_objective` field-for-field (visit_cell / wait_at /
  interact_cell → `ix,iy`; find_location → `cellX,cellY`; deliver_items → the
  target settlement's cell). A `destroy_npc` kill-count has no fixed cell, so it
  gets no pin. Marker id is `quest_<questId>_<objIdx>`, so every active target of
  a multi-objective quest is marked independently.
- **Trigger:** the producer is pure and idempotent, so it runs off a cheap
  per-frame **signature guard** in `process_world_events` — a quest-set
  fingerprint (`quest_marker_signature`, integer-only, no allocation) gates the
  allocating rebuild, so a steady state never re-allocates markers. The same
  guard reconciles stale `quest_*` pins carried in from a loaded save (the cache
  signature is reset to 0 on new-game and on load).
- **Renderer + toggle:** the universal marker pass in `draw_macro_overlay`
  ([ui/macro_overlay.cpp](src/ui/macro_overlay.cpp)) draws every
  `GameState::markers` entry by style; the **QuestMarkers** UI-settings element
  gates and scales it ([ui-settings.md](ui-settings.md)).

## Planned generators from the fiction

Four **witch quest generators**, one per witch, each keyed to her domain —
exploration/fetch, contracts on bosses and named NPCs, a standing black-artifact
turn-in, and pure absurdity — plus the eunuch/cult content that follows the world
state. They are ordinary procedural generators in the sense above, not authored
lines. Spec: [lore.md](lore.md) §3.6.

## Data-driven extension

New verb → one `eval_objective` case + one `Objective` discriminant (+ one
`objective_target_cell` case if it is spatial, so it earns a marker). New reward
→ one `emit_reward` case. New procedural quest → one generator in the procedural
content layer.

## Connections

Quests span the progression arc ([progression.md](progression.md)); the authored
lines they will eventually carry — the red witch's route, the rebellion, the
witch-champion ending, the "place that does not exist" — are specified in
[lore.md](lore.md) §4–5; place markers ([landmarks.md](landmarks.md)); reward the RPG sheet ([rpg.md](rpg.md));
read economy context ([economy.md](economy.md)).

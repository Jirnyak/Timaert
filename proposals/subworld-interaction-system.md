# Universal subworld interaction system — nearest_lootable primitive

> Salvaged 2026-08-03 from the generalist-agent session log (`jopus/timaert_fixes.txt`,
> session killed by an API 401 before implementation). Design was fully worked out,
> zero code landed: `src/sub/interaction.{h,cpp}` do not exist, only `bool interact();`
> at `src/sub/engine.h` survives. Status: READY TO IMPLEMENT.

## Idea

Decouple looting from the corpse *kind*: key on the **payload** component
(`ecs::CorpseLoot`), not on `Structure::Corpse`. Then a corpse, chest, bag or
barrel are all lootable through one path with zero engine change — the seed of
the unified-container system (`proposals/unified-container-system.md`).

## Change

New pure primitive — `src/sub/interaction.h` + `src/sub/interaction.cpp`
(mirrors `sub/targeting.{h,cpp}`: free function, no SubworldEngine state,
unit-testable on a bare `entt::registry`):

```cpp
// interaction.h
namespace sm::sub {
inline constexpr float kSubworldLootReach = 12.0f;   // metres; was inline in interact()
// Nearest entity carrying a lootable payload (ecs::CorpseLoot) within `maxRange`
// of (px,py). Keys on the PAYLOAD component, not Structure::Corpse — so a corpse,
// chest, bag or barrel are all lootable through one path.
entt::entity nearest_lootable(entt::registry& reg, float px, float py,
                              float maxRange = kSubworldLootReach);
}
```

Implementation = the current `interact()` scan (engine.cpp ~1251-1264 at the
time of writing) generalised: iterate `view<Position, CorpseLoot, SubworldTag>`
(drop the Structure requirement and the `Structure::Corpse` filter),
nearest-within-range wins, same `d2 <= bestD2` tie-break.

Refactor `SubworldEngine::interact()` to call `nearest_lootable(reg, playerX_,
playerY_)` instead of the inline loop; keep the "No corpse loot nearby." status,
gold/inventory transfer, and `reg.destroy(best)` exactly as-is.

## Files

- `src/sub/interaction.h` (new) — primitive + `kSubworldLootReach`
- `src/sub/interaction.cpp` (new) — `nearest_lootable`
- `src/sub/engine.cpp` — `interact()` calls the primitive
- `tests/subworld_loot_interaction_test.cpp` (new)
- `CMakeLists.txt` — add_executable + foreach entry, mirroring `targeting_test`

## Test plan — `tests/subworld_loot_interaction_test.cpp` (bare registry)

1. a corpse (`Structure::Corpse` + `CorpseLoot`) in range is found;
2. a non-corpse lootable (`Position` + `SubworldTag` + `CorpseLoot`, no
   Structure) is also found — the key new universality property;
3. a `Structure::Corpse` with no `CorpseLoot` is ignored (keys on payload, not kind);
4. nearest-of-two lootables wins;
5. a lootable just outside `kSubworldLootReach` returns `entt::null`.

## Verification

- full build green; new test + full ctest suite;
- validated Vulkan smoke → PASS, exit 0, sole VUID = benign 05137;
- existing `subworld_loot_xp` smoke (death→corpse→interact() end-to-end) must
  still pass — the refactor is behavior-preserving.

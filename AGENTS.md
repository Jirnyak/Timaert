# AGENTS

## Performance-first code

All new code should prioritize high performance. Favor better algorithms, data layouts, and containers to maximize FPS and avoid UI freezes.

## No exceptions / no RTTI

Do not rely on C++ exceptions or RTTI in this codebase. They are disabled for both performance and binary size reasons.

## EnTT ECS Architecture

This project uses EnTT for entity-component-system. Key conventions:

- **ECS World**: Located at `ctx.ecs_world` (std::unique_ptr<ecs::World>)
- **Components**: Defined in `ecs/components/` - use small POD structs (< 64 bytes)
- **Systems**: Defined in `ecs/systems/` - operate on component views
- **Spawning**: Use `ecs::spawn_*` functions in `spawn_system.h`

### Key Components
- `ecs::Position` - tile position with visual interpolation
- `ecs::NPCTag` - NPC type identification
- `ecs::Health` - current/max health
- `ecs::CombatStats` - will, lust, combat data
- `ecs::Active` / `ecs::Dead` - entity state tags
- `ecs::ObjectSprite` - for static objects (trees)

### Migration Patterns

When migrating legacy systems to ECS:
1. **Cache data for UI**: Store copies of ECS component data (e.g., `enemy_name_`, `enemy_life_`) when starting interactions that need persistent access
2. **Check both paths**: Use `has_enemy()` pattern to check `enemy_ != nullptr || enemy_entity_ != entt::null`
3. **Lambda captures**: Never capture raw pointers to ECS data in lambdas - cache strings/values by value
4. **Remove incrementally**: Delete legacy managers only after all usages are migrated

### GameState Interface

GameState methods take only `(GameContext& ctx, TextureManager& textures)`. EntityManager was removed - all entity management is now ECS-based.

## Build Commands

Desktop: `mkdir -p build && cd build && cmake .. -GNinja && ninja`
Web: `mkdir -p build-web && cd build-web && emcmake cmake .. -DCMAKE_BUILD_TYPE=Release && emmake make -j$(nproc)`

## Save Game Compatibility

No need to maintain save compatibility as the game is in early development stage. For any breaking changes to save format, simply increment `kSaveVersion`. All existing saves will be invalidated automatically.

## Legacy Code Removal

All legacy code should be removed as soon as possible since the game is in early development. There's no need to maintain backward compatibility or keep deprecated code paths. Remove legacy systems immediately after migration to ECS is complete.

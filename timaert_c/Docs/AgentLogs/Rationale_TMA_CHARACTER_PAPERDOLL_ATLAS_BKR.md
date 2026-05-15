# Rationale: TMA_CHARACTER_PAPERDOLL_ATLAS_BKR

Domain: CHARACTER_RENDERING_PORTER

## Native Paper-Doll Data Path

Problem: TS paper-doll data drove real character visuals, while native render paths could fall back to flat placeholders.

Solution: Ported atlas manifest parsing, deterministic descriptor generation, category/sprite lookup, render-plan layering, palette configuration, and animation timing into `src/assets/character_paperdoll.*`.

Rejected Alternatives: Runtime TS bridge or JSON-side reflection would add fragile startup dependencies and per-frame conversion pressure. Hard-coded placeholder sprite variants would not satisfy the paper-doll transfer.

Scalability potential: Low devices use cached 48x48 composed sprites and fixed lookup tables. High devices can spend saved CPU on more simultaneous characters and denser overlay/subworld views.

Hardware Impact: i3/MX350 class hardware avoids repeated file-path formatting and dynamic descriptor allocations during visible character draws.

## GL Composition And Cache Identity

Problem: Composed paper-doll frames need GL textures without allocating on the frame path or returning a wrong texture under hash collision.

Solution: `CharacterTextureCache` uses fixed open-addressed descriptor and texture tables; texture entries store exact descriptor plus animation/direction/frame identity. Failed composition stores a null cache entry to stop retry churn.

Rejected Alternatives: `std::unordered_map` texture storage and hash-only identity were simpler but allowed node allocations and collision-driven wrong-texture reuse. Recomposition every draw was rejected as direct frame-time waste.

Scalability potential: Low uses bounded fixed caches. Mid/High/Ultra keep the same deterministic path while supporting larger visible NPC counts before composition becomes visible.

Hardware Impact: Cache-hit path stays bounded and branch-light; expected gain is reduced frame spikes during map/proximity overlays on low-end silicon.

## Atlas Hot Path

Problem: Every render-plan miss still formatted relative PNG paths and performed manifest hash lookup for visible layers.

Solution: Precomputed `Category x Sprite` sheet ordinals after atlas load and switched render-plan and required-asset checks to ordinal lookup. Directional render orders are constexpr tables.

Rejected Alternatives: Keeping transparent `string_view` lookup only reduced temporary allocation, but still preserved per-layer path assembly. Lazy statics were rejected for the directional order because static-guard work is avoidable.

Scalability potential: Low devices use the cheapest ordinal lookup; high-end devices keep budget for more active characters and visual density.

Hardware Impact: The miss path saves repeated path formatting and hashing; estimated practical win is microseconds per new descriptor/frame composition, with the largest benefit during first exposure to dense NPC groups.

## Palette Parity

Problem: Native palette replacement used a looser RGB threshold than the TS WebGL shader and kept redundant grayscale prefilters.

Solution: Palette matching now uses squared RGB distance `<= 162`, matching TS normalized distance `< 0.05` without square root, and a single grayscale gate equivalent to TS channel-delta behavior.

Rejected Alternatives: Per-pixel floating point `sqrt` distance would match the shader text directly but is slower and unnecessary. The older loose integer threshold was rejected because it recolored too broad a set of near-gray pixels.

Scalability potential: Low devices get integer-only color matching on composition misses. High devices get parity while preserving cycles for higher paper-doll population.

Hardware Impact: Integer-only squared distance avoids expensive floating point per atlas pixel while tightening visual parity.

## Renderer Integration

Problem: Macro and subworld renderers needed paper-doll visuals without tight dependency on other in-flight agent systems.

Solution: Integrated through existing `NpcCharacter`, `SubworldAi`, and renderer hooks. Macro player/NPC sprites and proximity portraits use paper-doll textures with PNG fallback. Subworld NPCs use paper-doll billboards with movement-aware animation and camera-relative direction.

Rejected Alternatives: Adding a new render ownership layer or direct dependency on future systems would create integration risk with parallel agents. Flat placeholders were rejected because they do not complete this domain transfer.

Scalability potential: Low devices reuse cached textures and stable portrait frames. High-end devices can show movement-aware paper-doll silhouettes across denser crowds.

Hardware Impact: Stable idle portraits avoid cycling through four portrait textures per NPC row, reducing cache pressure in UI-heavy scenes.

## Animation Guard

Problem: Native animation helpers mostly failed closed for invalid enum values, but `delay_count()` directly indexed the delay-count table.

Solution: Added the same bounds check used by the frame-count and start-index helpers, and covered it with a focused invalid-animation regression test.

Rejected Alternatives: Trusting all callers to pass valid enum values was rejected because native integration points can receive stale or corrupted state during future save/render work.

Scalability potential: This is not a visual feature; it keeps the cheap animation path predictable on all hardware tiers.

Hardware Impact: No measurable runtime cost in normal calls; prevents undefined table access in bad-data cases.

## Compile-Only Spawner Repair

Problem: The app target was blocked by a dirty non-domain road/spawner edit: local lambdas referenced `torus_delta`, and the call site still passed a budget argument to the current `find_road_path` signature.

Solution: Added the missing torus delta helper and removed only the stale call argument, restoring the function/signature contract that was already present in the dirty file.

Rejected Alternatives: Reworking road generation or changing pruning policy was rejected because this prompt owns character rendering, not road gameplay.

Scalability potential: No intentional gameplay/performance change; this only restored buildability for verification.

Hardware Impact: No measurable runtime impact expected from the compile-only signature/helper repair.

## Macro Night-Darken Parity

Problem: TS forwards `GameScreen.svelte` night-darken into `character/renderer.ts`, where paper-doll pixels are mixed toward a dark blue tint. Native macro paper-dolls used full-white ImGui texture draws, so characters stayed visually daylight-bright at night.

Solution: Added a native copy of the TS piecewise night schedule and applied the TS character tint formula through the ImGui `AddImage` tint parameter. The composed 48x48 paper-doll textures remain time-independent and reusable.

Rejected Alternatives: Precomposing separate day/night texture variants was rejected because it would multiply cache keys and texture churn for a uniform color transform. Tinting PNG fallback sprites was rejected because the fallback path is not the transferred character renderer and should remain behaviorally stable.

Scalability potential: Low devices keep one cached texture per descriptor/frame and pay only one tint color calculation per macro overlay frame. High-end devices can render denser paper-doll crowds without exploding cache residency by time-of-day.

Hardware Impact: Estimated sub-microsecond CPU cost per frame for the tint calculation; avoids texture duplication and upload churn on i3/MX350-class hardware.

## Integer Generation Chance Gate

Problem: TS `Math.random()` chance checks are conceptually `[0, 100)`, so a `100%` generated character layer cannot fail the chance gate. Native generation used `next_f01() * 100.0f`; because `next_f01()` returns `float`, a maximum raw RNG value can round to `1.0f`, creating a theoretical false failure for `100%` layers.

Solution: Replaced the character generator chance gate with one 64-bit integer threshold compare. `chance >= 100` now always passes, while lower chances still consume one RNG value and avoid modulo bias.

Rejected Alternatives: Changing the shared `Rng::next_f01()` implementation was rejected because other systems may depend on its current behavior. Keeping the float gate was rejected because the edge case violates the TS contract for required generated layers.

Scalability potential: Low/Middle/High/Ultra all benefit from deterministic descriptor generation with no extra allocations or runtime state. The stable hash test makes future generator changes explicit instead of accidental.

Hardware Impact: Equivalent to the float path in descriptor generation and still allocation-free; a 64-bit multiply/compare replaces a float conversion/multiply/compare.

## NPC Appearance Presets And Event Hostile Visual Identity

Problem: TS `npc.ts` applies type-specific paper-doll appearance tweaks after random character generation: Merchant/Caravan get backpacks, Guard gets shoulder armor, and Witch/Sorceress get horns. Native paper-doll descriptors were keyed only by raw visual seed, and event-spawned `BattleStart` hostiles without a macro NPC override had no `NpcCharacter`, so subworld rendering could skip the paper-doll path.

Solution: Added a compact `AppearancePreset` in the character asset module, applied the TS preset mutations to generated descriptors, and extended the descriptor cache key to seed+preset. Macro and subworld renderers map existing `NPCType` values to presets without making the asset layer depend on macro registries. Event-spawned subworld hostiles now receive a deterministic `NpcCharacter` when no macro override is supplied, and the battle smoke asserts that component exists.

Rejected Alternatives: Storing full TS `CharacterData` in ECS was rejected because it would bloat every NPC with canvas/editor data and palette maps. Re-generating modified descriptors in renderers every frame was rejected because it would defeat the fixed cache path. Encoding appearance only by changing seeds was rejected because it could not guarantee required backpacks, shoulder armor, or horns.

Scalability potential: Low devices still use one cached 48x48 texture per descriptor/frame/preset; high-end devices get denser character variety without multiplying time-of-day textures or adding per-frame JSON/state conversion.

Hardware Impact: Cache lookup adds one preset byte to descriptor identity. Composition cost is unchanged after cache miss, and event-spawned hostiles avoid generic fallback sprites without adding frame-path allocations.

// Macroworld dynamic night lighting — the universal, data-driven light-source
// system for the 2D world map. Every cell that emits light (settlements,
// villages, active spires, and any future landmark type) is enumerated by
// `collect_macro_lights`, then `bake_light_field` rasterises the summed glow
// into a per-cell RGB field texture the macro fragment shader samples once and
// adds at night (scaled by the day/night `nightDarken` curve).
//
// The field is baked on world-change only (never per frame): population and
// terrain are static between edits, and the shader scales the same field by
// nightDarken as the clock advances. Increment A bakes a radial falloff (TS
// renderer.ts parity); increment B replaces the bake with terrain-occluded
// propagation (open land carries light far, forest/mountain cells absorb it).
//
// L1 macro layer: depends only on GameState + the landmark data table. The
// Vulkan renderer consumes the baked bytes, not this header, so the GPU target
// keeps its minimal link set.
#pragma once
#include <cstdint>
#include <vector>

namespace sm {

struct GameState;
struct FeatureLayer;

// One night-emitting light on the macro map.
//   nx, ny     normalized map UV of the emitter cell centre, [0,1)
//   radius     falloff distance in cells
//   intensity  peak glow scalar, 0..1
//   r, g, b    emitted tint, linear 0..1
struct MacroLight {
    float nx, ny;
    float radius;
    float intensity;
    float r, g, b;
};

// TS totalGlow clamp ceiling (renderer.ts): summed per-channel glow is stored
// in [0, kMacroGlowCeil] and RGBA8-encoded as value/kMacroGlowCeil; the shader
// multiplies the sample back by kMacroGlowCeil to decode.
inline constexpr float kMacroGlowCeil = 1.5f;

// Master night-glow brightness — the single tunable that dims (or brightens)
// the whole field. The bake multiplies every emitter's summed contribution by
// this before the clamp/encode, so it scales settlements, spires and any future
// emitter uniformly, without touching the population curves, the falloff, or the
// shader. Tuned below 1 because macro.frag adds the decoded glow on top of the
// night-darkened scene, where a full-strength city core saturates to white;
// this pulls the peak down to a warm pool of light. Raise toward 1 for brighter
// towns, lower for a darker night. Director-tunable — the one knob to turn.
inline constexpr float kMacroGlowGain = 0.45f;

// Data-driven emitter census. Inhabited landmarks (settlements, villages) scale
// radius + intensity by population via the TS curve; fixed POIs (active spires,
// and any landmark type whose LandmarkDef carries a non-zero lightColor) use
// their table tint + synthetic strength (LandmarkDef.lightPop). Adding a new
// emitting landmark type is one table row plus, once it has world instances,
// one loop here — never an engine branch in the renderer or shader.
std::vector<MacroLight> collect_macro_lights(const GameState& gs);

// Bake the per-cell RGB night-light field. Writes width*height*4 RGBA8 bytes
// into `out` (RGB = glow encoded in [0, kMacroGlowCeil], A = 255).
//
// Falloff is f = max(0, 1 - D/radius), contribution color * f*f * intensity
// (TS renderer.ts parity, summed then clamped). D is the emitter distance
// measured across the terrain:
//   features == nullptr  D is Euclidean — isotropic radial glow (increment A).
//   features != nullptr  D is optical distance from a bounded Dijkstra over the
//                        feature grid: each traversed cell costs a per-feature
//                        amount (open land ~1, roads < 1 so light runs along
//                        them, tree canopy several× so it dies sooner). Light
//                        thus carries far over open ground and is smothered by
//                        forest, flowing around dense stands rather than through
//                        them (increment B). Torus-wrapped.
//
// `cellHeights` (optional, width*height normalized 0..1 macro heights) adds the
// ELEVATION term to the same Dijkstra (increment C — the deferred pass from the
// mountains→biome refactor): every UPHILL step pays `kGlowClimbCost` per unit
// of rise, so a ridge between a town and a valley smothers the glow exactly
// like a forest does — bare massifs occlude again, from the heightmap itself
// (mountains are a biome, not a feature). Downhill is free: glow spilling into
// a valley below a hilltop town stays visible. Flat heights ⇒ byte-identical to
// the heights-free bake. Ignored on the radial (no-features) path.
//
// An empty (0-sized) feature layer takes the nullptr path. The nullptr path is
// exact isotropic falloff — not an octile approximation — so callers with no
// feature layer keep identical increment-A output.
// `treeDensity` (optional, width*height floats 0..1 = tree count / 16384):
// continuous canopy occlusion — the tree-count field's replacement for the
// old binary FT_Tree optical row. Size-mismatched input is ignored.
void bake_light_field(int width, int height,
                      const std::vector<MacroLight>& lights,
                      std::vector<std::uint8_t>& out,
                      const FeatureLayer* features = nullptr,
                      const std::vector<float>* cellHeights = nullptr,
                      const std::vector<float>* treeDensity = nullptr);

// Optical cost of climbing one full unit of normalized height against the
// glow (per Dijkstra step, scaled by the step's rise). Tuned so a typical
// massif rise (~0.2 above the surrounding plain) exceeds any settlement
// radius — a range wall reads opaque — while gentle hills only shorten the
// glow. One knob, like kMacroGlowGain.
inline constexpr float kGlowClimbCost = 60.0f;

} // namespace sm

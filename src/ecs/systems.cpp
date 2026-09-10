#include "ecs/systems.h"
#include "ecs/components.h"
#include <cmath>

namespace sm::ecs::sys {

// A second `tick_projectiles` lived here: it moved X and Y only (no z at all),
// checked no bounds, no ground, no structures and no hits, and destroyed a
// projectile purely when its lifeTimer ran out. It had ZERO call sites — the
// real implementation is sub/spell_effects.cpp tick_spell_projectiles, which is
// honestly 3D — but it sat under the obvious name, in the obvious file, next to
// two systems that ARE wired in. Anyone reaching for "the projectile system"
// would have found this one and quietly reintroduced a flat world. Deleted
// 2026-08-05 (owner spotted it). One system per job.

void tick_visual_interp(World& w, float dt) {
    // SubworldTag: this is the SCENE's interpolator (its one caller is the
    // subworld tick), and the registry carries both scales — without the tag
    // it walked all ~16k macro squads every sub-tick, moving their VisualPos
    // in the wrong units (cells read as tiles; macro smoothing has its own
    // tick_macro_npc_visuals). Same hole class as SUB-1, one word.
    auto view = w.reg.view<Position, VisualPos, SubworldTag>();
    for (auto e : view) {
        auto& p = view.get<Position>(e);
        auto& v = view.get<VisualPos>(e);
        float dx = p.x - v.vx, dy = p.y - v.vy;
        float d  = std::sqrt(dx * dx + dy * dy);
        if (d < 0.001f) continue;
        float step = v.speed * dt;
        if (step >= d) { v.vx = p.x; v.vy = p.y; }
        else { v.vx += dx / d * step; v.vy += dy / d * step; }
    }
}

void tick_combat_recovery(World& w, std::uint32_t steps) {
    if (steps == 0u) return;
    auto view = w.reg.view<Combat>();
    for (auto e : view) {
        auto& c = view.get<Combat>(e);
        c.recoverySteps = c.recoverySteps > steps ? c.recoverySteps - steps : 0u;
    }
}

} // namespace sm::ecs::sys

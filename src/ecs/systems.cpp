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
    auto view = w.reg.view<Position, VisualPos>();
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

void tick_combat_cooldowns(World& w, std::uint32_t steps) {
    if (steps == 0u) return;
    auto view = w.reg.view<Combat>();
    for (auto e : view) {
        auto& c = view.get<Combat>(e);
        c.cooldownSteps = c.cooldownSteps > steps ? c.cooldownSteps - steps : 0u;
    }
}

} // namespace sm::ecs::sys

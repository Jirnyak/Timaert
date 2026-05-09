#include "ecs/systems.h"
#include "ecs/components.h"
#include <cmath>

namespace sm::ecs::sys {

void tick_projectiles(World& w, float dt) {
    auto& reg = w.reg;
    auto view = reg.view<Position, Projectile>();
    std::vector<entt::entity> dead;
    for (auto e : view) {
        auto& p  = view.get<Position>(e);
        auto& pj = view.get<Projectile>(e);
        p.x += pj.vx * dt;
        p.y += pj.vy * dt;
        pj.lifeTimer -= dt;
        if (pj.lifeTimer <= 0.0f) dead.push_back(e);
    }
    for (auto e : dead) reg.destroy(e);
}

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

void tick_combat_cooldowns(World& w, float dt) {
    auto view = w.reg.view<Combat>();
    for (auto e : view) {
        auto& c = view.get<Combat>(e);
        if (c.cooldownTimer > 0.0f) c.cooldownTimer -= dt;
    }
}

} // namespace sm::ecs::sys

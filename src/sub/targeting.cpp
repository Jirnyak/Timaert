#include "sub/targeting.h"

#include <cmath>

namespace sm::sub {

entt::entity aim_target(entt::registry& reg,
                        float px, float py, float yaw,
                        float maxRange, float cosHalfAngle,
                        entt::entity shooter) {
    if (maxRange <= 0.0f) return entt::null;

    const float fx = std::cos(yaw);
    const float fy = std::sin(yaw);
    const float maxR2 = maxRange * maxRange;

    entt::entity best = entt::null;
    float bestD2 = maxR2; // start at the range boundary; nearer replaces it

    // Same candidate set as the shipped melee path (engine.cpp:828-829): live,
    // current-scene NPCs/monsters. Requiring NPCKind also excludes the player
    // entity, which carries no NPCKind; the explicit player-side skip below
    // additionally covers projected player soldiers.
    auto view = reg.view<ecs::Position, ecs::Health, ecs::NPCKind,
                         ecs::SubworldTag>(entt::exclude<ecs::Dead>);
    for (auto e : view) {
        if (e == shooter) continue;
        if (reg.any_of<ecs::PlayerTag, ecs::PlayerSoldierTag>(e)) continue;

        const auto& hp = view.get<ecs::Health>(e);
        if (hp.hp <= 0.0f) continue;

        const auto& pos = view.get<ecs::Position>(e);
        const float dx = pos.x - px;
        const float dy = pos.y - py;
        const float d2 = dx * dx + dy * dy;
        if (d2 > maxR2) continue;

        // Cone test. A co-located target (d2 ~ 0) has no defined bearing and is
        // treated as "in front" so it is never unreachable.
        if (d2 > 1e-8f) {
            const float cosang = (fx * dx + fy * dy) / std::sqrt(d2);
            if (cosang < cosHalfAngle) continue;
        }

        if (d2 <= bestD2) {
            bestD2 = d2;
            best = e;
        }
    }
    return best;
}

} // namespace sm::sub

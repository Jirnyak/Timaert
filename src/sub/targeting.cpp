#include "sub/targeting.h"
#include "sub/body.h"

#include <cmath>

namespace sm::sub {

namespace {

// The shared melee-candidate filter: live, current-scene, not the player's
// own side. One spelling for both the grid arm and the full-scan arm.
inline bool melee_candidate(entt::registry& reg, entt::entity e) {
    if (!reg.all_of<ecs::Position, ecs::Health, ecs::NPCKind,
                    ecs::SubworldTag>(e)
        || reg.any_of<ecs::Dead>(e)) {
        return false;
    }
    if (reg.any_of<ecs::PlayerTag, ecs::PlayerSoldierTag>(e)) return false;
    return reg.get<ecs::Health>(e).hp > 0.0f;
}

} // namespace

entt::entity melee_pick_target(entt::registry& reg,
                               float px, float py, float pz,
                               float range,
                               HostileFn isHostile, void* user,
                               MeleeNeighborsFn neighborsFn,
                               void* neighborsUser) {
    if (range <= 0.0f) return entt::null;

    // A blow reaches a body's SURFACE, not its centre: the target's own
    // radius extends the reach (the same `reach + radius[target]` law the NPC
    // strike walks in sub/movement.cpp). Candidates are ranked by the surface
    // GAP, so "nearest" means nearest to being hit, not nearest centre — a
    // troll's flank two metres out loses to a frog an arm's length away.
    entt::entity hostileBest = entt::null;
    float hostileGap = range;
    entt::entity anyBest = entt::null;
    float anyGap = range;

    const auto consider = [&](entt::entity e) {
        if (!melee_candidate(reg, e)) return;
        const auto& pos = reg.get<ecs::Position>(e);
        const float dx = pos.x - px;
        const float dy = pos.y - py;
        const float dz = pos.z - pz;
        const float gap = std::sqrt(dx * dx + dy * dy + dz * dz)
                        - body_radius(reg, e);
        if (gap > range) return;
        if (gap <= anyGap) {
            anyGap = gap;
            anyBest = e;
        }
        if (gap <= hostileGap && isHostile && isHostile(user, e)) {
            hostileGap = gap;
            hostileBest = e;
        }
    };

    // Broad phase: the battle pick grid, exactly like the spell contact asks
    // it (a SUPERSET promise — extras are filtered above, a miss would be a
    // bug, so a broad phase that cannot promise completeness answers -1 and
    // the swing falls back to the full scan; null = no grid, headless tests).
    if (neighborsFn) {
        static std::uint32_t buf[kMaxMeleeNeighbors];
        const int n = neighborsFn(neighborsUser, px, py, range,
                                  buf, kMaxMeleeNeighbors);
        if (n >= 0) {
            for (int i = 0; i < n; ++i) {
                const entt::entity e = entt::entity(buf[std::size_t(i)]);
                if (reg.valid(e)) consider(e);
            }
            return hostileBest != entt::null ? hostileBest : anyBest;
        }
    }
    auto view = reg.view<ecs::Position, ecs::Health, ecs::NPCKind,
                         ecs::SubworldTag>(entt::exclude<ecs::Dead>);
    for (auto e : view) consider(e);
    return hostileBest != entt::null ? hostileBest : anyBest;
}

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

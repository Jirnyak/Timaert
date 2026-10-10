#include "sub/targeting.h"
#include "sub/body.h"
#include "sub/caps.h"       // kMaxSubObjects — the subworld scene ceiling
#include "sub/doors.h"      // slot_* — слот-двери арены (ступень (iv))
#include "sub/record.h"     // entity-двери aim_target (до ломтя (ii-б))

#include <cmath>

namespace sm::sub {

namespace {

// The shared melee-candidate filter: live, current-scene, not the player's
// own side. One spelling for both the grid arm and the full-scan arm.
inline bool melee_candidate(const SubObjects& objs, int s) {
    if (!slot_flag(objs, s, kObjAlive)
        || slot_kind(objs, s) == nullptr
        || slot_flag(objs, s, kObjDead)) {
        return false;
    }
    if (objs.is_avatar_slot(s)
        || slot_flag(objs, s, kObjPlayerSoldier)) return false;
    const auto* hp = slot_pools(objs, s);
    return hp != nullptr && hp->hp > 0;
}

} // namespace

int melee_pick_target(const SubObjects& objs,
                      float px, float py, float pz,
                      float range,
                      HostileFn isHostile, void* user,
                      MeleeNeighborsFn neighborsFn,
                      void* neighborsUser) {
    if (range <= 0.0f) return -1;

    // A blow reaches a body's SURFACE, not its centre: the target's own
    // radius extends the reach (the same `reach + radius[target]` law the NPC
    // strike walks in sub/movement.cpp). Candidates are ranked by the surface
    // GAP, so "nearest" means nearest to being hit, not nearest centre — a
    // troll's flank two metres out loses to a frog an arm's length away.
    int hostileBest = -1;
    float hostileGap = range;
    int anyBest = -1;
    float anyGap = range;

    const auto consider = [&](int s) {
        if (!melee_candidate(objs, s)) return;
        const ecs::Position& pos = objs.pos[std::size_t(s)];
        const float dx = pos.x - px;
        const float dy = pos.y - py;
        const float dz = pos.z - pz;
        const float gap = std::sqrt(dx * dx + dy * dy + dz * dz)
                        - slot_body_radius(objs, s);
        if (gap > range) return;
        if (gap <= anyGap) {
            anyGap = gap;
            anyBest = s;
        }
        if (gap <= hostileGap && isHostile && isHostile(user, s)) {
            hostileGap = gap;
            hostileBest = s;
        }
    };

    // Broad phase: the battle pick grid, exactly like the spell contact asks
    // it (a SUPERSET promise — extras are filtered above, a miss would be a
    // bug, so a broad phase that cannot promise completeness answers -1 and
    // the swing falls back to the full scan; null = no grid, headless tests).
    if (neighborsFn) {
        static std::uint16_t buf[kMaxSubObjects];
        const int n = neighborsFn(neighborsUser, px, py, range,
                                  buf, kMaxSubObjects);
        if (n >= 0) {
            for (int i = 0; i < n; ++i) consider(int(buf[std::size_t(i)]));
            return hostileBest >= 0 ? hostileBest : anyBest;
        }
    }
    // Полный проход — по слотам арены (гейт kObjAlive внутри кандидата);
    // вьюха реестра в выборе цели больше не участвует.
    for (int s = 0; s < int(kMaxSubObjects); ++s) consider(s);
    return hostileBest >= 0 ? hostileBest : anyBest;
}

entt::entity aim_target(entt::registry& reg,
                        float px, float py, float pz,
                        float fx, float fy, float fz,
                        float maxRange, float cosHalfAngle,
                        entt::entity shooter) {
    if (maxRange <= 0.0f) return entt::null;

    const float maxR2 = maxRange * maxRange;

    entt::entity best = entt::null;
    float bestD2 = maxR2; // start at the range boundary; nearer replaces it

    // Same candidate set as the shipped melee path (engine.cpp:828-829): live,
    // current-scene NPCs/monsters. Requiring a body kind (колонка арены,
    // кусок 1) also excludes the player entity, whose slot carries no kind;
    // the explicit player-side skip below additionally covers projected
    // player soldiers.
    auto view = reg.view<ecs::ObjectSlot>();
    for (auto e : view) {
        if (e == shooter) continue;
        if (body_kind(reg, e) == nullptr) continue;
        if (object_flag(reg, e, kObjDead)) continue;
        if (is_avatar(reg, e)
            || object_flag(reg, e, kObjPlayerSoldier)) continue;

        const auto* hpCol = body_pools(reg, e);
        if (hpCol == nullptr || hpCol->hp <= 0) continue;

        const ecs::Position& pos = *body_pos(reg, e);
        const float dx = pos.x - px;
        const float dy = pos.y - py;
        // Eye to EYE, through the one door (body_eye_m — the same height an
        // NPC missile aims at: «оба конца на высоте глаз»). A body's
        // Position.z is its FEET, and a bearing to feet drops a close target
        // out of a level cone — the possess smokes caught exactly that when
        // the cone first went 3D (2026-09-19).
        const float dz = (pos.z + body_eye_m(reg, e)) - pz;
        const float d2 = dx * dx + dy * dy + dz * dz;
        if (d2 > maxR2) continue;

        // Cone test. An XY-co-located target has no defined bearing and is
        // treated as "in front" so it is never unreachable.
        if (dx * dx + dy * dy > 1e-8f) {
            const float cosang =
                (fx * dx + fy * dy + fz * dz) / std::sqrt(d2);
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

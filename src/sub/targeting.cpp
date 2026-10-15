#include "sub/targeting.h"
#include "sub/body.h"
#include "sub/caps.h"       // kMaxSubObjects — the subworld scene ceiling
#include "sub/doors.h"      // slot_* — слот-двери арены (ступень (iv))
#include "sub/record.h"     // objects_find — мост арены (уедет фреймом, (vi))

#include <cmath>

namespace sm::sub {

namespace {

// The shared melee-candidate filter: live, current-scene, not the player's
// own side. One spelling for both the grid arm and the full-scan arm —
// кандидатский гейт ОДИН (slot_npc_candidate@src/sub/doors.h), игроцкая
// сторона — отдельной строкой (боевой выбор своих не бьёт, миникарта видит).
inline bool melee_candidate(const SubObjects& objs, int s) {
    return slot_npc_candidate(objs, s)
        && !slot_flag(objs, s, kObjPlayerSoldier);
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
    // bug, and полнота обещана ВСЕГДА: длина буфера есть ссылка на массив в
    // кап, а у счёта нет сентинела, так что доложить об усечке нечем — ровно
    // как у спелловой двери, и там же замерено, где кончается гарантия ТИПА;
    // null = сетки нет вовсе, безголовые свидетели).
    static std::uint16_t buf[kMaxSubObjects];
    const std::uint32_t n = neighborsFn(neighborsUser, px, py, range, buf);
    for (std::uint32_t i = 0; i < n; ++i) {
        consider(int(buf[std::size_t(i)]));
    }
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

    // Same candidate set as the shipped melee path: ОДИН кандидатский гейт
    // slot_npc_candidate (он же отсекает игрока — у его слота нет рода);
    // проекция игроцкой стороны — отдельной строкой, как у melee_candidate.
    const SubObjects* objs = objects_find(reg);
    if (objs == nullptr) return entt::null;   // сцены нет — целей нет
    auto view = reg.view<ecs::ObjectSlot>();
    for (auto e : view) {
        if (e == shooter) continue;
        const int s = int(view.get<ecs::ObjectSlot>(e).slot);
        if (!slot_npc_candidate(*objs, s)) continue;
        if (slot_flag(*objs, s, kObjPlayerSoldier)) continue;

        const ecs::Position& pos = objs->pos[std::size_t(s)];
        const float dx = pos.x - px;
        const float dy = pos.y - py;
        // Eye to EYE, through the one door (slot_body_eye_m — the same height
        // an NPC missile aims at: «оба конца на высоте глаз»). A body's
        // Position.z is its FEET, and a bearing to feet drops a close target
        // out of a level cone — the possess smokes caught exactly that when
        // the cone first went 3D (2026-09-19).
        const float eye = slot_body_eye_m(*objs, s);
        const float dz = (pos.z + eye) - pz;
        const float d2 = dx * dx + dy * dy + dz * dz;
        if (d2 > maxR2) continue;

        // Cone test. СТРАЖ СТОИТ НА ТОМ, ЧТО ОН ОХРАНЯЕТ — на `d2`, то есть
        // на самом ЗНАМЕНАТЕЛЕ деления ниже, — И МЕРА ЕГО ТЕЛЕСНАЯ, А НЕ
        // ПЛОСКАЯ (ЗАКОН ИЗОТРОПИИ). Вырожден тот, кто стоит В МОЁМ ОБЪЁМЕ:
        // пеленга у него нет, и схватка в упор не спрашивает направления.
        // Масштаб берётся у САМОГО ТЕЛА (`slot_body_eye_m`, уже в руках) —
        // своего числа тут не заводится.
        //
        // Стояло `dx*dx + dy*dy > 1e-8f` — ПЛОСКИЙ страж у трёхмерного
        // пеленга, и он пропускал цель РОВНО НАД ГОЛОВОЙ (dx=dy=0, dz=50)
        // мимо конуса ВОВСЕ: она принималась «перед тобой» на любой
        // дистанции внутри maxRange. Такая цель теперь честно судится
        // конусом — смотришь вверх, попадает; смотришь прямо, нет.
        //
        // А вот `d2 > 1e-8f` было бы ДРУГОЙ крайностью, и `targeting_test`
        // поймал её сразу: враг, стоящий НА СТРЕЛКЕ, имеет пеленг «строго
        // вверх» (его глаз выше моего на свой рост), и ровный взгляд
        // отверг бы того, с кем борешься в упор. Закон, который охраняет тот
        // свидетель, — «co-located enemy is reachable», и он верен.
        const float eye2 = eye * eye;
        if (d2 > eye2) {
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

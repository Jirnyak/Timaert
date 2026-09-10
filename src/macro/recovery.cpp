#include "macro/recovery.h"
#include "ecs/pools.h"
#include "macro/movement_cost.h"

#include <algorithm>

namespace sm {

void recover_bar(float amount,
                 float& accumulator,
                 int& current,
                 int maximum) {
    if (maximum <= 0) {
        current = 0;
        accumulator = 0.0f;
        return;
    }
    if (current >= maximum) {
        current = maximum;
        accumulator = 0.0f;
        return;
    }
    if (amount <= 0.0f) {
        return;
    }

    accumulator += amount;
    const int whole = int(accumulator);
    if (whole <= 0) {
        return;
    }

    current = std::min(maximum, current + whole);
    accumulator -= float(whole);
    if (current >= maximum) {
        current = maximum;
        accumulator = 0.0f;
    }
}

void rest_pools(ecs::Pools& pools, float hours, int marathonRank) {
    if (hours <= 0.0f) {
        return;
    }
    const int maxSp = std::max(1, pools.maxSp);
    if (pools.sp < maxSp) {
        pools.spCarry += float(maxSp) * kRestRegenPctPerHour
                         * skill_mult_of(SkillId::Marathon, marathonRank)
                         * hours;
        settle_sp_carry(pools.sp, maxSp, pools.spCarry);
    }
    // A full bar cannot BANK rest — but only the POSITIVE remainder dies with
    // the fill: a negative one is a fraction of a march already owed, and
    // forgiving it would leak cost every time a journey began from full.
    if (pools.sp >= maxSp) {
        pools.sp = maxSp;
        if (pools.spCarry > 0.0f) pools.spCarry = 0.0f;
    }
    recover_bar(float(pools.maxHp) * kRestRegenPctPerHour * hours,
                pools.hpCarry, pools.hp, pools.maxHp);
    recover_bar(float(pools.maxMp) * kRestRegenPctPerHour * hours,
                pools.mpCarry, pools.mp, pools.maxMp);
}

} // namespace sm

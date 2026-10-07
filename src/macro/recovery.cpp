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

int settle_pools_over_time(ecs::Pools& pools, float hours, int marathonRank,
                           float burnPerHour, bool regenerates) {
    if (hours <= 0.0f) {
        return 0;
    }
    const int maxSp = std::max(1, pools.maxSp);
    const int spBefore = pools.sp;
    // ДВА ПРОЦЕССА, И ТОЛЬКО ОДИН ИЗ НИХ ВЫКЛЮЧАЕТСЯ ДВИЖЕНИЕМ.
    // ЖЖЕНИЕ идёт ВСЕГДА, по ставке своей клетки: идёшь по горе — горит как
    // гора, стоишь на ней же — горит столько же. Отдельной «цены шага» не
    // существует; марш дорог потому, что на дорогой клетке он МЕДЛЕННЕЕ и
    // проводит на ней больше часов. РЕГЕН плоский, от веса не зависит.
    // Складываются ЗНАКОМ, а не веткой: ветка «если планка неполна — реген»
    // дала бы полной планке на дороге жжение без возмещения, и она текла бы
    // вниз на ровном месте.
    {
        const float burn =
            (burnPerHour > 0.0f ? burnPerHour : 0.0f) * hours;
        const float regen = regenerates
            ? float(maxSp) * kRestRegenPctPerHour
                  * skill_mult_of(SkillId::Marathon, marathonRank) * hours
            : 0.0f;
        pools.spCarry += regen - burn;
        settle_sp_carry(pools.sp, maxSp, pools.spCarry);
    }
    // A full bar cannot BANK rest — but only the POSITIVE remainder dies with
    // the fill: a negative one is a fraction of a march already owed, and
    // forgiving it would leak cost every time a journey began from full.
    if (pools.sp >= maxSp) {
        pools.sp = maxSp;
        if (pools.spCarry > 0.0f) pools.spCarry = 0.0f;
    }
    // «Марш не лечит НИЧЕГО» (CANON S14) — ОДНА СТРОКА, и она стоит на всех
    // трёх планках. Прежде этот закон держался ФОРМОЙ ЗВОНЯЩИХ (идущее тело
    // просто не звало отдых), и ровно поэтому его нельзя было оставить так:
    // дверь теперь зовут ВСЕГДА, ради жжения, — и без этого гейта марширующий
    // лорд лечился бы в дороге, чего не делал ни один день до сегодня.
    if (regenerates) {
        recover_bar(float(pools.maxHp) * kRestRegenPctPerHour * hours,
                    pools.hpCarry, pools.hp, pools.maxHp);
        recover_bar(float(pools.maxMp) * kRestRegenPctPerHour * hours,
                    pools.mpCarry, pools.mp, pools.maxMp);
    }
    // ...и укус долга — ЗДЕСЬ, в той же двери, что потратила (ЗАКОН
    // СПОСОБНОСТИ п.4), почково по очку (bite_spent_debt).
    return bite_spent_debt(pools, spBefore);
}

} // namespace sm

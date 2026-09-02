// Дневной шаг поля угрозы (контракт в threat_field.h).
#include "macro/threat_field.h"

#include <algorithm>
#include <vector>

#include "macro/chronicle.h"
#include "macro/state.h"

namespace sm {

namespace {

inline std::uint32_t saturate_add_(std::uint32_t a, std::uint64_t b) {
    const std::uint64_t sum = std::uint64_t(a) + b;
    return sum > 0xFFFFFFFFull ? 0xFFFFFFFFu : std::uint32_t(sum);
}

} // namespace

void threat_field_daily(const MacroWorld& mw, int day) {
    NavWorld* nv = mw.nav;
    if (!nv || !mw.gs || !nv->baked()) return;
    const Chronicle& c = mw.gs->chronicle;
    const std::size_t R = nv->regionLandmarkId.size();
    if (R == 0) return;

    // РЕПЛЕЙ: загрузка и перепёк графа приходят сюда одинаково — поле не
    // того размера (или летопись моложе курсора: другой мир). Сбросить и
    // прочитать кольцо с самого старого живого факта; поправка на возраст
    // ниже доигрывает распад, который поле пережило бы вживую.
    if (nv->threat.size() != R || nv->threatSeenSeq >= c.nextSeq) {
        nv->threat.assign(R, 0u);
        nv->threatSeenSeq = c.oldest_seq() - 1u;
    }

    // ИСТОЧНИК: новые Died-факты кольца. Один писатель поля — этот читатель
    // летописи; сами факты пишут прежние двери (record_battle_facts,
    // record_landmark_fact), так что игрок-мясник поднимает поле наравне с
    // любым лордом, а бездомные банды не поднимают его по построению.
    if (c.ready()) {
        const std::uint32_t price = std::uint32_t(threat_soul_price());
        const std::uint32_t from =
            std::max(nv->threatSeenSeq + 1u, c.oldest_seq());
        for (std::uint32_t seq = from; seq < c.nextSeq; ++seq) {
            const WorldFact& f = c.ring[(seq - 1u) % kChronicleFacts];
            if (f.seq != seq) continue;   // слот уже перезаписан кольцом
            if (f.kind != std::uint16_t(FactKind::Died)) continue;
            if (f.amount <= 0) continue;
            const std::uint16_t r = nav_region_at(*nv, int(f.x), int(f.y));
            if (std::size_t(r) >= R) continue;   // вода/вне графа
            const int age = day - f.day;
            const int shift = age > 0 ? age / kThreatDecayDays : 0;
            if (shift >= 32) continue;   // старше горизонта распада
            nv->threat[r] = saturate_add_(
                nv->threat[r],
                (std::uint64_t(f.amount) * price) >> shift);
        }
        nv->threatSeenSeq = c.nextSeq - 1u;
    }

    // ДИФФУЗИЯ: 1/8 округи в день, поровну на ребро. Дельты копятся в
    // стороне и применяются разом — порядок обхода округ не влияет на
    // итог (поле без гонок с самим собой).
    std::vector<std::int64_t> delta(R, 0);
    for (std::size_t r = 0; r < R; ++r) {
        const std::uint32_t t = nv->threat[r];
        if (t == 0u) continue;
        const int deg = r < nv->portalCount.size()
                            ? int(nv->portalCount[r]) : 0;
        if (deg <= 0) continue;
        const std::uint32_t out = t >> kThreatDiffusionShift;
        const std::uint32_t per = out / std::uint32_t(deg);
        if (per == 0u) continue;
        const std::uint32_t begin = nv->portalBegin[r];
        for (int i = 0; i < deg; ++i) {
            const NavPortal& p = nv->portals[begin + std::uint32_t(i)];
            if (std::size_t(p.toRegion) >= R) continue;
            delta[p.toRegion] += std::int64_t(per);
            delta[r] -= std::int64_t(per);
        }
    }
    for (std::size_t r = 0; r < R; ++r) {
        if (delta[r] == 0) continue;
        const std::int64_t v = std::int64_t(nv->threat[r]) + delta[r];
        nv->threat[r] = v <= 0 ? 0u
                      : v > 0xFFFFFFFFll ? 0xFFFFFFFFu : std::uint32_t(v);
    }

    // РАСПАД: по календарю мира, не по фазе загрузки — сейв/лоад не сдвигает
    // день полураспада.
    if (kThreatDecayDays > 0 && day > 0 && day % kThreatDecayDays == 0) {
        for (std::uint32_t& t : nv->threat) t >>= 1;
    }
}

} // namespace sm

#include "ui/macro_eyes.h"

#include <cmath>

namespace sm::ui {

// ── Глаза карты (M-244a) — порт интегратора tick_macro_npc_visuals ─────────
// Закон тот же, носитель другой: цель глайда — КЛЕТКА + ДОЛЯ (целые кванты
// мира), сам глаз — кадровый кэш представления. dt реальных секунд законен:
// это картинка между тиками, мир его не читает.
void tick_macro_eyes(const MacroStore& st, MacroEyeCache& eyes,
                     int mapW, int mapH, float dt) {
    if (mapW <= 0 || mapH <= 0) return;
    const bool snapAll = !eyes.primed;
    eyes.primed = true;
    if (dt <= 0.0f && !snapAll) return;

    for (std::uint16_t slot = 0; slot < std::uint16_t(kUnifiedCap); ++slot) {
        if (st.alive[slot] == 0 || st.dead[slot] != 0) continue;
        const auto& c = st.cell[slot];
        // Цель — заявленная точка сквада: клетка плюс доля в 1/256 клетки.
        const float tx = float(ecs::cell_x(c, mapW))
                       + float(st.sub[slot].x) / 256.0f;
        const float ty = float(ecs::cell_y(c, mapW))
                       + float(st.sub[slot].y) / 256.0f;
        float& ex = eyes.x[slot];
        float& ey = eyes.y[slot];
        const auto& rt = st.runtime[slot];
        const auto& hp = st.pools[slot];
        if (snapAll || hp.hp <= 0 || !std::isfinite(ex) || !std::isfinite(ey)) {
            ex = tx; ey = ty;
            continue;
        }

        const float dx = tx - ex;
        const float dy = ty - ey;
        const float dSq = dx * dx + dy * dy;
        // Телепорт-бэкстоп, не сглаживание: дальше шести клеток прыгают
        // только настоящие скачки (телепортер, заворот шва, загрузка) — их
        // не идут пешком. Вывод числа — в истории интегратора (npc_ai,
        // 2026-09-09): узкая граница начинала бы скакать на законных
        // быстрых маршах.
        if (dSq > 36.0f) {
            ex = tx; ey = ty;
            continue;
        }

        const float speed = rt.visualSpeed > 0.0f ? rt.visualSpeed : 2.0f;
        const float step = speed * dt;
        if (step <= 0.0f || dSq <= 0.000001f) continue;

        const float d = std::sqrt(dSq);
        if (d <= step) {
            ex = tx; ey = ty;
        } else {
            const float ratio = step / d;
            ex += dx * ratio;
            ey += dy * ratio;
        }
    }
}

} // namespace sm::ui

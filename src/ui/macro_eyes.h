// ── ГЛАЗА КАРТЫ — ПРЕДСТАВЛЕНИЕ, И ТОЛЬКО ОНО (M-244a) ────────────────────
// Глайд спрайта между клетками. Истина мира — клетка (MacroCell) плюс целая
// доля (CellSub, кванты 1/256 клетки, M-244); этот буфер — «интерполяция
// картинки между тиками» (единственный законный float по ЗАКОНУ КАЛИБРОВКИ,
// и это место говорит о себе вслух). Симуляция его не читает НИКОГДА; в сейв
// он не идёт — прежний носитель MacroVisual ЕХАЛ в сейв, то есть float жил в
// состоянии мира, и M-244a его убил. 2 × f32 × 16384 = 128 КиБ, КЭШ.
//
// Файл нарочно без ImGui: закон глайда — чистая математика над колонками
// store, и его свидетель (macro_npc_ai_parity_test) собирается хедлесс.
#pragma once

#include "macro/store.h"
#include <array>

namespace sm::ui {

struct MacroEyeCache {
    // false → ближайший тик сажает каждый глаз ровно на цель (мир родился
    // или загружен — глайдить от прежнего мира нечестно и незачем).
    bool primed = false;
    std::array<float, kUnifiedCap> x;
    std::array<float, kUnifiedCap> y;
};

// Провести глаза к целям (клетка + доля) со скоростью rt.visualSpeed — тот же
// закон, что жил в tick_macro_npc_visuals@macro/npc_ai.cpp до M-244a:
// телепорт-бэкстоп 6 клеток, мёртвый/невалидный глаз садится на цель.
void tick_macro_eyes(const MacroStore& st, MacroEyeCache& eyes,
                     int mapW, int mapH, float dt);

} // namespace sm::ui

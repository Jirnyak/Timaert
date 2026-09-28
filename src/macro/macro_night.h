// СКОЛЬКО СЕЙЧАС НОЧИ НА КАРТЕ МИРА — ОДНА ДВЕРЬ НА ЗЕМЛЮ И НА ФИГУРЫ.
//
// Карту макромира рисуют ДВА исполнителя: землю — `shaders/macro.frag` (ей
// сила ночи приезжает push-константой из `MacroRendererVk::record`), фигуры
// сквадов — ImGui из `src/ui/macro_overlay.cpp`. Закон «насколько сейчас
// темно» у них ОДИН, а написан был ДВАЖДЫ: те же пять чисел кривой и тот же
// тон ночи стояли в обоих файлах, и комментарий в оверлее сам это признавал —
// «one law, two renderers». Тронул одно место — земля и фигуры разъехались бы
// молча, и заметно это стало бы только глазами в сумерках (перепись П-24,
// 2026-09-28, наряд M-165).
//
// Здесь дверь одна. Оба исполнителя зовут её, своих чисел ночи не держат, и
// второй ответ стал невыразим.
//
// L1 macro: заголовок не зависит ни от чего, тела O(1) по капам мира (§5 п.13).
#pragma once

namespace sm {

// ── ЧИСЛА НОЧИ. ВЫВОДА У НИХ ПОКА НЕТ, И ЭТО СКАЗАНО ВСЛУХ ────────────────
// Кривая пришла из мёртвого TS-прототипа («TS GameScreen curve»), то есть её
// цепочка вывода кончается удалённым авторитетом, а не инвариантом мира. Это
// РАСХОЖДЕНИЕ по ЗАКОНУ КОНСТАНТ, и оно записано нарядом M-162, а не
// замаскировано здесь: величины перенесены БЕЗ ИЗМЕНЕНИЯ, чтобы сведение двух
// копий в одну не двигало картинку — вывод чисел решается отдельно и с
// владельцем на стенде. Что они значат сегодня, в часах игровых суток:
// рассвет разжимает тьму с 4:48 до 8:24, закат сжимает её с 18:00 до 21:36.
inline constexpr float kNightDawnStart = 0.20f;   // 4:48 — тьма ещё полная
inline constexpr float kNightDawnEnd   = 0.35f;   // 8:24 — день
inline constexpr float kNightDuskStart = 0.75f;   // 18:00 — день ещё полный
inline constexpr float kNightDuskEnd   = 0.90f;   // 21:36 — тьма
// Предельная доля тона ночи: даже в глухую полночь земля и фигуры не уходят в
// тон целиком, иначе карта перестаёт читаться как карта.
inline constexpr float kNightTintMax = 0.82f;
// Тон ночи — холодный синий сумрак. Один на землю и на фигуры.
inline constexpr float kNightTintR = 0.05f;
inline constexpr float kNightTintG = 0.05f;
inline constexpr float kNightTintB = 0.15f;

// ДОЛЯ СУТОК ИЗ ЧАСОВ И МИНУТ — тоже одна, потому что её считали двумя
// выражениями: `(h + m / 60) / 24` у земли и `(h * 60 + m) / (24 * 60)` у
// фигур. Математически это одно число, в float32 — не обязательно то же самое
// в последнем бите, а сравнение с порогом кривой стоит ровно на этом бите.
inline constexpr float macro_time_of_day(int hour, int minute) {
    int minutes = hour * 60 + minute;
    minutes %= 24 * 60;
    if (minutes < 0) minutes += 24 * 60;
    return float(minutes) / float(24 * 60);
}

// СИЛА НОЧИ: 1 — глухая тьма, 0 — полный день. `tod01` — доля суток [0,1).
inline constexpr float macro_night_darken(float tod01) {
    if (tod01 < kNightDawnStart || tod01 > kNightDuskEnd) return 1.0f;
    if (tod01 < kNightDawnEnd)
        return 1.0f - (tod01 - kNightDawnStart)
                          / (kNightDawnEnd - kNightDawnStart);
    if (tod01 < kNightDuskStart) return 0.0f;
    return (tod01 - kNightDuskStart) / (kNightDuskEnd - kNightDuskStart);
}

} // namespace sm

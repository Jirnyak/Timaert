// СБОРЩИК ЛОКАЛЬНОГО ФРЕЙМА — макро-сторона границы (M-171, ломоть 3).
// Заполняет буфер bus/local_frame.h пятью родами с окна 5×5 вокруг клетки-
// центра. Зовётся на границе макро-тика и на ре-центре (пересинхронизация
// ПЛАНОВАЯ — ЗАКОН ШВА); из тика субмира не зовётся никогда.
#pragma once
#include "bus/local_frame.h"

namespace sm {
struct MacroWorld;

void fill_local_frame(const MacroWorld& w, std::uint32_t centerCell,
                      LocalContext& out);

} // namespace sm

// ── ТЕЛО МЕСТА — ОДНА ДВЕРЬ ОТ СТРОКИ К СЛОТУ (M-90 шаг 5) ───────────────
//
// Место есть неподвижный сквад: его плечо (склад, интересы, счёт нужд,
// благополучие, слава, анкета) — колонки MacroStore его ТЕЛА; строка
// Landmark — тонкий индекс с упакованным хэндлом (bodyBits). Место без тела
// не существует по построению: рождение даёт слот
// (birth_landmark@src/macro/npc_spawn.cpp), загрузка перелинковывает
// (relink_place_bodies@src/macro/npc_spawn.cpp). Протухший хэндл здесь —
// дефект рождения, и он называется ВСЛУХ, а не глотается; слот 0 в этом
// случае — жертвенная мишень, чтобы чтение не вышло за кап (fail-loud).
//
// Свой заголовок, а не squad.h: двери нужны и счёту душ (labour.h), и
// анкетным дверям (squad.h), а labour.h тащить весь squad.h не должен.
#pragma once

#include <cstdio>

#include "macro/state.h"
#include "macro/store.h"

namespace sm {

inline MacroHandle place_body(const Landmark& lm) {
    return macro_handle_from_bits(lm.bodyBits);
}

inline std::uint16_t place_slot(const MacroStore& st, const Landmark& lm) {
    const MacroHandle h = place_body(lm);
    if (!st.valid(h)) {
        std::fprintf(stderr, "[place] МЕСТО %d БЕЗ ТЕЛА (bits=%08x)\n",
                     lm.id, lm.bodyBits);
        return 0;
    }
    return h.slot;
}

// Согласие литерального контракта bodyBits (state.h не видит store.h):
static_assert(kMacroHandleNoneBits == 0xFFFFFFFFu,
              "bodyBits «тела нет» = все единицы — литерал в state.h");

// Склад места — его ЕДИНЫЙ контейнер, теперь колонкой тела. Самая ходовая
// пара дверей флипа: прежние читатели lm.inventory идут сюда.
inline Inventory& place_store(MacroStore& st, const Landmark& lm) {
    return st.inventory[place_slot(st, lm)].inv;
}
inline const Inventory& place_store(const MacroStore& st, const Landmark& lm) {
    return st.inventory[place_slot(st, lm)].inv;
}

} // namespace sm

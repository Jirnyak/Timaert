// SubObjects — ЕДИНЫЙ МАССИВ ОБЪЕКТОВ СЦЕНЫ (M-150; вердикты владельца
// 2026-10-05: «общий кап ОБЪЕКТОВ субмира», «арена — агностичный 3д куб, в
// котором агностичные объекты»).
//
// ЭТО ПОСТОЯННЫЙ НОСИТЕЛЬ, НЕ ПЕР-ТИКОВЫЙ СБОР: BodyCrowd (movement.h) и
// сетки пересобираются каждый драйв и индексов не хранят — здесь же слот
// живёт столько, сколько живёт объект, зануляется и переиспользуется
// (ЗАКОН ГЛАДКОЙ ПАМЯТИ: род — колонка/маска, пустота оплачена). Ссылка на
// объект — {slot, gen}; «никого» = ген 0 (ЗАКОН НУЛЯ-ОРДИНАЛА, вердикт
// 2026-10-05: признак «нет» живёт в поколении, слот полного диапазона).
//
// КАП: пока kMaxBodyCrowd (16384) — тела и есть первые жители массива;
// ломоть 6 меняет его ОДНИМ именем на kMaxSubObjects = 65536 (2^16 =
// мощность u16-индекса, «решить на века», вердикт 2026-10-05).
//
// МИГРАЦИЯ (ломти 1а..7): колонки заезжают сюда из entt-компонент
// поштучно; мост objects_attach/objects_of — тот же ctx-приём, что у
// MacroStore, и умирает вместе с реестром (ломоть 7). Слот объекта на
// теле — транзитный компонент ecs::ObjectSlot; его снимет смерть реестра.
#pragma once

#include <array>
#include <cstdint>

#include "sub/movement.h"   // kMaxBodyCrowd — кап до ломтя 6

namespace sm::sub {

// ── Маска рода и состояния объекта (ломоть 1а: пока только судьба) ──────
// Бит = бывший entt-тег; «живой слот» — отдельный бит, потому что ген
// хранит идентичность, а не занятость (слот умершего жив трупом, ломоть 5).
inline constexpr std::uint16_t kObjAlive = 1u << 0;

struct SubObjects {
    // Идентичность: поколение слота. 0 = «никого» — слот ещё не жил или
    // хэндл протух; выдача начинается с 1 и заворачивается мимо нуля.
    std::array<std::uint16_t, std::size_t(kMaxBodyCrowd)> gen{};
    // Род и состояние — МАСКА, не компоненты (ЗАКОН СТРОКИ КАТАЛОГА:
    // смена архетипа в тике невыразима по построению).
    std::array<std::uint16_t, std::size_t(kMaxBodyCrowd)> flags{};
    // ── FX-колонки (ломоть 1а): бывшие HitFlash / DamageFx / LastHit ────
    std::array<float, std::size_t(kMaxBodyCrowd)> hitFlash{};      // сек
    std::array<std::uint32_t, std::size_t(kMaxBodyCrowd)> lastHitBy{};
    // Событие «в этом тике по телу попали»: бит 0 = pending, бит 1 =
    // lethal, бит 2 = blocked. Дренируется одним проходом за тик.
    std::array<std::uint8_t, std::size_t(kMaxBodyCrowd)> damageFx{};

    int count = 0;        // живых слотов (для приборов, не для обхода)
    int cursor = 0;       // бегунок выдачи — слоты переиспользуются по кругу

    // Родить слот: первый свободный от бегунка. -1 = кап («кап стоит на
    // воплощённом»: звонящий отказывается честно, как BodyCrowd::add).
    int alloc() {
        for (int step = 0; step < kMaxBodyCrowd; ++step) {
            const int s = (cursor + step) & (kMaxBodyCrowd - 1);
            if (flags[std::size_t(s)] & kObjAlive) continue;
            cursor = (s + 1) & (kMaxBodyCrowd - 1);
            std::uint16_t g = gen[std::size_t(s)];
            g = std::uint16_t(g + 1u);
            if (g == 0u) g = 1u;          // ген 0 навсегда «никого»
            gen[std::size_t(s)] = g;
            flags[std::size_t(s)] = kObjAlive;
            hitFlash[std::size_t(s)] = 0.0f;
            lastHitBy[std::size_t(s)] = 0u;
            damageFx[std::size_t(s)] = 0u;
            ++count;
            return s;
        }
        return -1;
    }

    // Занулить слот (смерть объекта как ЗАПИСИ; труп ломтя 5 слот НЕ
    // освобождает — он гасит kObjAlive-роль маской, оставаясь жильцом).
    void free(int slot) {
        if (slot < 0 || slot >= kMaxBodyCrowd) return;
        if (!(flags[std::size_t(slot)] & kObjAlive)) return;
        flags[std::size_t(slot)] = 0u;
        --count;
    }
};
// 16384 × (2+2+4+4+1) Б колонок + служебные: цена названа и закреплена.
static_assert(sizeof(SubObjects) == std::size_t(kMaxBodyCrowd) * 13 + 8,
              "массив объектов сцены: 13 Б/слот (ломоть 1а) + count/cursor");

} // namespace sm::sub

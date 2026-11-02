// ПАЛИТРА НОЧНЫХ ТИНТОВ + УПАКОВКА КЛЕТКИ ПОЛЯ СВЕТА — словарь поля
// (M-370, вердикты владельца 2026-11-02: «свет — своё поле … по сути это
// клеточка светит»; «фича тоже может быть источником свет но сделать это
// просто/ типа я теперь понял поч нужна таблица фич»).
//
// Клетка поля света (`GameState::lightEmission`) несёт ОДИН u16 канонической
// формы FieldCell: 4 бита — ординал тинта этой палитры, 12 бит — сила.
// Значение 0 = тьма (нулевая сила; ординал 0 служебен — ЗАКОН СЛОВАРЯ п.6).
// Писатели поля агностичны (ЗАКОН АГНОСТИЧНОСТИ): сегодня это рефреш фич на
// границе ребейка (macro_lighting.cpp), впредь — сквады, объекты, события,
// каждый своим значением той же упаковки.
#pragma once
#include <cstddef>
#include <cstdint>

namespace sm {

// Ординал 0 — «никто»; строки заводятся только с живым носителем (DOD п.9):
// очаг — поселения (сила = паства, число фичи), шпиль и выемки — числовые
// жильцы (гаснут с нулём числа), руина — вечный тлен. Цвета логова, святилища
// и башни умерли с их мёртвыми строками kPlaces — вернутся со своими фичами.
inline constexpr std::uint32_t kLightPalette[] = {
    0x00000000u, // 0 — никто
    0xFFFFC76Bu, // 1 — тёплый очаг поселений
    0xFFA86CFFu, // 2 — фиолет шпиля
    0xFF8E8576u, // 3 — серо-песочный тлен руин
    0xFF8B6332u, // 4 — коричневый жар выемок и шахт
};
inline constexpr std::size_t kLightPaletteCount =
    sizeof(kLightPalette) / sizeof(kLightPalette[0]);
// Имена ординалов — авторинг строк идёт ИМИ, не числами (второе
// правописание ординала невыразимо).
inline constexpr std::uint8_t kLightHearth   = 1u; // очаг поселений
inline constexpr std::uint8_t kLightSpire    = 2u; // шпиль
inline constexpr std::uint8_t kLightRuin     = 3u; // руина
inline constexpr std::uint8_t kLightWorkings = 4u; // выемки и шахты
static_assert(kLightWorkings < kLightPaletteCount,
              "именованный ординал обязан называть живую строку палитры");

// 4 бита палитры — дополнение силы до РОВНО одного FieldCell (u16): нибл
// покупает упаковку без второго грида. 12 бит силы (0..4095) — с запасом:
// кривая яркости (pop_intensity, log10) насыщается уже к ~370.
inline constexpr std::uint16_t kLightStrengthBits = 12u;
inline constexpr std::uint16_t kLightStrengthMax =
    std::uint16_t((1u << kLightStrengthBits) - 1u);
static_assert(kLightPaletteCount <= (1u << (16u - kLightStrengthBits)),
              "палитра обязана влезать в нибл упаковки — расширяя словарь, "
              "сперва передоговорись о раскладке клетки поля света");

inline constexpr std::uint16_t light_pack(std::uint8_t palette,
                                          std::uint16_t strength) {
    return std::uint16_t(
        (std::uint16_t(palette) << kLightStrengthBits)
        | (strength > kLightStrengthMax ? kLightStrengthMax : strength));
}
inline constexpr std::uint8_t light_palette_of(std::uint16_t v) {
    return std::uint8_t(v >> kLightStrengthBits);
}
inline constexpr std::uint16_t light_strength_of(std::uint16_t v) {
    return std::uint16_t(v & kLightStrengthMax);
}
inline constexpr std::uint32_t light_tint_of(std::uint16_t v) {
    const std::uint8_t p = light_palette_of(v);
    return p < kLightPaletteCount ? kLightPalette[p] : 0u;
}

} // namespace sm

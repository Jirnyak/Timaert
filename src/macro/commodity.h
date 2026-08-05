// Commodity dictionary — СЛОВАРЬ ТОВАРОВ, единый для всей игры.
//
// Owner rulings (2026-08-05, work_vector №1):
//   * ONE universal dictionary = the world's NOUNS. Loot profiles, recipes,
//     needs, shop stocks are RELATIONS over these ids — they reference, they
//     never redeclare. This retires (step by step) the three incompatible
//     vocabularies: EconomyState resources[6]+goods[15], and eventually the
//     player item catalog joins the same id space.
//   * Materials are ROWS of the Raw tier of this same dictionary. An archetype
//     that can be made OF a material (a future sword) declares an
//     allowed-materials mask over the raw rows — the groundwork column exists
//     from day one, crafting arrives later as recipe rows + a material×archetype
//     multiplier table, no dictionary rework.
//   * Procedural items = archetype row here + tiny instance
//     {archetypeId, materialId, quality, seed} elsewhere — the same
//     catalog+instance pattern the monster table and sprite resolver use.
//
// v1 scope (owner-approved): 5 raw + 3 goods per tier = 14 rows. Values and
// weights are round house-style numbers; BALANCE is owned by the self-play
// harness (tests/econ_v1_test.cpp conservation + no-starvation laws), not by
// eyeballing these columns.
#pragma once
#include <cstdint>
#include <cstring>

namespace sm {

enum class CommodityTier : std::uint8_t {
    Raw = 0,         // сырьё; эти строки ЕСТЬ материалы будущего крафта
    Vital = 1,       // жизненно необходимое: без него голод/упадок
    Instrument = 2,  // инструментально-развитие: множители роста
    Luxury = 3,      // роскошь: счастье/богатство
};

struct CommodityDef {
    const char*   id;            // stable string id — the ONE name everywhere
    const char*   name;          // display
    CommodityTier tier;
    float         weightKg;      // carried weight (caravans, inventories)
    int           baseValue;     // anchor price in gold
    // Allowed-materials groundwork: bit i = raw row i of THIS table may be the
    // instance's material. 0 = the commodity has no material variants (all of
    // v1). A future sword archetype sets e.g. bit(iron)|bit(wood).
    std::uint16_t materialMask;
};

// Raw rows FIRST and contiguous — the material mask bit space is their index.
inline constexpr CommodityDef kCommodities[] = {
    // ── Raw (материалы) ──────────────────────────────────────────────────
    {"wood",      "Дерево",      CommodityTier::Raw,        2.0f,  1, 0},
    {"stone",     "Камень",      CommodityTier::Raw,        4.0f,  1, 0},
    {"iron",      "Железо",      CommodityTier::Raw,        4.0f,  2, 0},
    {"clay",      "Глина",       CommodityTier::Raw,        2.0f,  1, 0},
    {"grain",     "Зерно",       CommodityTier::Raw,        1.0f,  1, 0},
    // ── Vital (жизненно необходимое) ─────────────────────────────────────
    {"bread",     "Хлеб",        CommodityTier::Vital,      1.0f,  2, 0},
    {"bricks",    "Кирпичи",     CommodityTier::Vital,      4.0f,  2, 0},
    // Одежда варится из зерна как из льна-заглушки: отдельная культура волокна
    // (лён/шерсть) — будущая строка сырья, рецепт тогда меняет один вход.
    {"cloth",     "Одежда",      CommodityTier::Vital,      1.0f,  4, 0},
    // ── Instrument (инструментально-развитие) ───────────────────────────
    {"tools",     "Инструменты", CommodityTier::Instrument, 2.0f,  8, 0},
    {"furniture", "Мебель",      CommodityTier::Instrument, 8.0f,  8, 0},
    {"wagon",     "Повозка",     CommodityTier::Instrument, 32.0f, 16, 0},
    // ── Luxury (роскошь) ─────────────────────────────────────────────────
    {"jewelry",   "Украшения",   CommodityTier::Luxury,     1.0f,  32, 0},
    {"carving",   "Резьба",      CommodityTier::Luxury,     2.0f,  16, 0},
    {"statue",    "Статуя",      CommodityTier::Luxury,     64.0f, 64, 0},
};

inline constexpr int kCommodityCount =
    int(sizeof(kCommodities) / sizeof(kCommodities[0]));
inline constexpr int kRawCommodityCount = 5;

static_assert(kCommodityCount == 14, "v1 scope: 5 raw + 3 goods per tier");
static_assert(kCommodities[kRawCommodityCount - 1].tier == CommodityTier::Raw
                  && kCommodities[kRawCommodityCount].tier != CommodityTier::Raw,
              "raw rows must be first and contiguous (material mask bit space)");
static_assert(kRawCommodityCount <= 16,
              "materialMask is uint16 — widen it before adding a 17th raw row");

// Index by id; -1 if unknown. Linear scan over 14 rows — call at load/wire
// time, cache the index in hot paths (same contract as the faction registry).
inline int commodity_index(const char* id) {
    if (!id || id[0] == '\0') return -1;
    for (int i = 0; i < kCommodityCount; ++i) {
        if (std::strcmp(kCommodities[i].id, id) == 0) return i;
    }
    return -1;
}

inline const CommodityDef& commodity_def(int index) {
    return kCommodities[index];
}

} // namespace sm

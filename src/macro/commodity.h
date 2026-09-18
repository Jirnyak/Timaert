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
// v1 scope (owner-approved): 15 rows. Values and
// weights are round house-style numbers; BALANCE is owned by the self-play
// harness (tests/econ_v1_test.cpp conservation + no-starvation laws), not by
// eyeballing these columns.
#pragma once
#include <cstdint>
#include <cstring>

namespace sm {

// (CommodityTier УМЕР 2026-09-18, вердикт владельца «у нас теперь просто
// система итемов единая и точка». Он был ВТОРЫМ ответом на вопрос «что это за
// вещь», а единственный даёт категория каталога — items.h ItemType: Material
// = ресурсы, из них крафтят; Goods = товары, их производят и в них нуждаются;
// Food — категория, в которой живёт голодная строка лестницы. Все три его
// читателя — сев закромов, процедурный квест и голодная дверь — спрашивают
// теперь категорию.)

struct CommodityDef {
    const char*   id;            // stable string id — the ONE name everywhere
    const char*   name;          // display
    float         weightKg;      // carried weight (caravans, inventories)
    // NO price column — deliberately. The one price anchor is the same row's
    // ItemDef.value (macro/items.h, one-dictionary ruling): a second table of
    // gold numbers here had already drifted from the one the game reads.
    // Allowed-materials groundwork: bit i = raw row i of THIS table may be the
    // instance's material. 0 = the commodity has no material variants (all of
    // v1). A future sword archetype sets e.g. bit(iron)|bit(wood).
    std::uint16_t materialMask;
};

// Raw rows FIRST and contiguous — the material mask bit space is their index.
inline constexpr CommodityDef kCommodities[] = {
    // ── Raw (материалы) ──────────────────────────────────────────────────
    {"wood",      "Дерево", 2.0f,  0},
    {"stone",     "Камень", 4.0f,  0},
    {"iron",      "Железо", 4.0f,  0},
    {"clay",      "Глина", 2.0f,  0},
    // ПИЩА — ОДНА строка на всю еду мира (владелец, 2026-09-18: «обобщим
    // зерно до пищи, и из неё делается еда — тогда абстрактно добывается из
    // поля пищи и из фауны»). Источников много (пашня, охота, дальше рыба),
    // строка одна: иначе лестнице нужд понадобился бы механизм ЗАМЕНЫ
    // «хлеб ИЛИ мясо», а это новая система там, где нужен минимум. Зерно,
    // мясо и рыба остаются ВИДАМИ предмета для сумки — экономика говорит
    // одним словом.
    {"food",      "Пища", 1.0f,  0},
    // Монетный металл (CANON S10 чеканка): жила → слиток → монета фракции.
    {"silver",    "Серебро", 4.0f,  0},
    // ── Vital (жизненно необходимое) ─────────────────────────────────────
    {"bread",     "Хлеб", 1.0f,  0},
    {"bricks",    "Кирпичи", 4.0f,  0},
    // Одежда варится из зерна как из льна-заглушки: отдельная культура волокна
    // (лён/шерсть) — будущая строка сырья, рецепт тогда меняет один вход.
    {"cloth",     "Одежда", 1.0f,  0},
    // ── Instrument (инструментально-развитие) ───────────────────────────
    {"tools",     "Инструменты", 2.0f,  0},
    {"furniture", "Мебель", 8.0f,  0},
    {"wagon",     "Повозка", 32.0f, 0},
    // ── Luxury (роскошь) ─────────────────────────────────────────────────
    {"jewelry",   "Украшения", 1.0f,  0},
    {"carving",   "Резьба", 2.0f,  0},
    {"statue",    "Статуя", 64.0f, 0},
};

inline constexpr int kCommodityCount =
    int(sizeof(kCommodities) / sizeof(kCommodities[0]));
inline constexpr int kRawCommodityCount = 6;

static_assert(kCommodityCount == 15, "v1 scope of the economy's ordinals");
// «Сырьё идёт первым и подряд» держал ярус товарной строки; ярус умер
// 2026-09-18, а СМЫСЛ утверждения — битовое пространство materialMask — жив.
// Сторожит его теперь свидетель (econ_v1_test), который спрашивает КАТЕГОРИЮ
// каталога: первые kRawCommodityCount строк обязаны быть ItemType::Material,
// следующая — нет. Компилятору это недоступно: каталог виден только своей
// единице трансляции.
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

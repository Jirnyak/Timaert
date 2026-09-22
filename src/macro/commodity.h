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
    // NO price column — deliberately. The one price anchor is the same row's
    // ItemDef.value (macro/items.h, one-dictionary ruling): a second table of
    // gold numbers here had already drifted from the one the game reads.
    //
    // ТРИ КОЛОНКИ УМЕРЛИ ЗДЕСЬ 2026-09-22 (§57, вердикт владельца):
    //   `name`         — подпись товара с НУЛЁМ читателей во всём мире;
    //   `weightKg`     — ВТОРОЙ СЛОВАРЬ рядом с ItemDef::weight. Вся
    //                    арифметика веса шла и идёт через каталог; сторож
    //                    «две таблицы согласны о массе» существовал ровно
    //                    потому, что существовала вторая таблица;
    //   `materialMask` — заготовка под материалы вещей, все 15 строк = 0,
    //                    читателей ноль. Вернётся строкой каталога, когда
    //                    придёт крафт материалов, — не заготовкой.
    // ОСТАЛСЯ ОРДИНАЛ. Он не спутник: по нему живут построчная ведомость и
    // долг нужды — это АДРЕСНОЕ ПРОСТРАНСТВО, а не вторая правда о вещи.
};

// Raw rows FIRST and contiguous — the material mask bit space is their index.
inline constexpr CommodityDef kCommodities[] = {
    // ── Raw (материалы) ──────────────────────────────────────────────────
    {"wood"},
    {"stone"},
    {"iron"},
    {"clay"},
    // ВОЛОКНО — ЛЁН ПАШНИ (владелец, 2026-09-20). Оно существует ради одного
    // закона: НИ ОДНО БЛАГО НЕ ВАРИТСЯ ИЗ ПИЩИ. Пока ткань пряли из зерна
    // (льняная заглушка), любое давление труда в сторону благ съедало хлеб
    // мира — измерено после сноса хлеба: ткани ×40, голодавших в городах
    // ×300, мир спрял свою еду в рубахи. Вход ткани переехал сюда.
    {"fibre"},

    // Монетный металл (CANON S10 чеканка): жила → слиток → монета фракции.
    {"silver"},
    // ── ПИЩА — ПОТОК МАТЕРИИ, А НЕ СЫРЬЁ (владелец, 2026-09-20: «три потока
    // материала — 1) пища… 2) блага для роста 3) ресурсы для производства»;
    // «уберём крафт из пищи, уберём хлеб, и вся пища станет пищей без
    // ресурса»). Строка ОДНА на всю еду мира, источников много (пашня,
    // охота, дальше рыба). Стоит ПОСЛЕ сырьевого блока намеренно: битовое
    // пространство materialMask — это материалы вещей, а пища вещью не
    // бывает, и её присутствие в блоке ломало закон «сырьё = первые
    // kRawCommodityCount = категория Material».
    // ХЛЕБ УМЕР ЗДЕСЬ ЖЕ: он был вторым словом о той же нужде — лестница
    // кормилась хлебом, а поле растило «материал», и между ними стоял
    // обязательный рецепт. Сырая пища утоляет голод сама; печь не нужна
    // миру, чтобы он ел.
    {"food"},
    // ── Vital (жизненно необходимое) ─────────────────────────────────────
    {"bricks"},
    // Одежда варится из зерна как из льна-заглушки: отдельная культура волокна
    // (лён/шерсть) — будущая строка сырья, рецепт тогда меняет один вход.
    {"cloth"},
    // ── Instrument (инструментально-развитие) ───────────────────────────
    {"tools"},
    {"furniture"},
    {"wagon"},
    // ── Luxury (роскошь) ─────────────────────────────────────────────────
    {"jewelry"},
    {"carving"},
    {"statue"},
};

inline constexpr int kCommodityCount =
    int(sizeof(kCommodities) / sizeof(kCommodities[0]));
inline constexpr int kRawCommodityCount = 6;

static_assert(kCommodityCount == 15, "v1 scope of the economy's ordinals");
// «СЫРЬЁ ИДЁТ ПЕРВЫМ И ПОДРЯД» — закон живой, и после смерти materialMask
// его держит уже не битовое пространство, а САМ ПОРЯДОК ОРДИНАЛОВ: сев
// закромов и голодная дверь читают «первые kRawCommodityCount строк —
// материал». Сторожит его свидетель (econ_v1_test), который спрашивает
// КАТЕГОРИЮ каталога. Компилятору это недоступно: каталог виден только
// своей единице трансляции.

// Index by id; -1 if unknown. Linear scan over 14 rows — call at load/wire
// time, cache the index in hot paths (same contract as the faction registry).
inline int commodity_index(const char* id) {
    if (!id || id[0] == '\0') return -1;
    for (int i = 0; i < kCommodityCount; ++i) {
        if (std::strcmp(kCommodities[i].id, id) == 0) return i;
    }
    return -1;
}

// (`commodity_def(i)` вырезана 2026-09-22 — дверь с нулём вызовов: все
// читатели берут `kCommodities[i]` напрямую, и это честнее.)

} // namespace sm

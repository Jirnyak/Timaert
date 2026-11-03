// СЛОВАРЬ ВЕДОМОСТИ МАКРО-СТОКА — какие величины мира одолжимы (`MacroStock`)
// и чем одолженное адресуется (`MacroStockKey`). ПЕРЕЕХАЛ из macro/macro_stock.h
// ломтём 1б эпика пакетов v1: расписка субмира (`BodyLoan`) и строка дельты
// (`push_stock_op`) называют ряд и ключ, не касаясь мира, а §11 запрещает
// субмиру включать macro/.
//
// ЗДЕСЬ ТОЛЬКО ИМЕНА. Сама ведомость — чтение, движение, расчёт стока и закон
// отрастания — осталась в macro/macro_stock.h: у неё в подписи MacroWorld, то
// есть изменяемое состояние, которому в нейтральном слое места нет (M-181,
// граница ТИП против ЭКЗЕМПЛЯРА; тот же шов, которым ResourceFieldId уехал в
// tables/resource_field_id.h, а гриды остались в macro/resource_field.h).
#pragma once
#include <cstdint>

namespace sm {

// The borrowable quantities. One row per value in the table (macro_stock.cpp);
// the enum value IS the row index, and ecs::MacroDebt stores it as a plain byte
// so the ECS layer never has to learn this header exists.
enum class MacroStock : std::uint8_t {
    TreeCount = 0,   // macro/tree_layer.h — the forest standing in one cell
    Creatures,       // СУЩЕСТВО ВЫШЛО ИЗ КОНТЕЙНЕРА СКВАДА — один вопрос мира
                     //   и ОДИН ряд на него (M-324). Город, деревня, корован,
                     //   артель: все они сквады с инвентарём, и горожанин,
                     //   убитый в субмире, списывается ровно так же, как
                     //   охранник корована. Здесь стоял второй ряд
                     //   `Population`, делавший то же самое через
                     //   place-фильтр субъекта, — снесён вместе с фильтром
    FaunaCount,      // the wild headcount of one cell — the Fauna row's own
                     //   stored field (GameState::faunaField, M-371)
    CropCount,       // the standing wheat of one cell (fertility-derived
                     //   estimate − GameState::cropOverrides harvest scar)
    FeatureNumber,          // ЧИСЛО ФИЧИ клетки (единое плоское поле GameState::featureNumber,
                     //   ЗАКОН ПОЛЯ п.3: смысл задаёт стоящая фича — заряд орба,
                     //   руда шахты, зерно пашни). Ряд рождён ломтём 5 эпика
                     //   фрейма: дренаж орба субмиром едет СТРОКОЙ ДЕЛЬТЫ, а
                     //   строка адресует ведомость, не голую дверь feature_number_write.
    // (Garrison УНИЧТОЖЕН 2026-09-30, v122: сословия «гарнизон» больше нет —
    // все души места лежат головами в ОДНОМ его инвентаре, и строка
    // `Creatures` выше адресует их всех, именную по entityId и генерика по
    // {роду, уровню}. Две строки на один контейнер были вторым словарём —
    // и `Population` была ТРЕТЬЕЙ, пережившей этот же приговор на год.)
    Count,
};

// Where a stock lives. Everything in the world sits in a macro cell; a stock
// belonging to a NAMED thing standing in that cell also carries its id.
struct MacroStockKey {
    std::int32_t subject = 0;   // settlement / village id; -1 = the cell itself
                                 //   (the creature row: the squad's MacroSpawnId)
    std::int16_t cellX = 0;
    std::int16_t cellY = 0;
    // Which row WITHIN the subject, when the stock is a TABLE rather than a
    // count. The creature row stores the member's SoldierRecord::entityId (a bit
    // pattern — ids may use the high bit); -1 = no name, the stock is a plain
    // number. Anonymous rows ignore it, so every existing key stays valid.
    std::int32_t detail = -1;
    // The GENERIC soul's name (CANON S4: a stack member has no entityId, it
    // IS its {kind, level}). detailLevel > 0 marks the pair live — kind
    // alone cannot, because Peasant is row 0. A named key may carry the pair
    // too; the strike door prefers the entityId when one exists.
    std::uint16_t detailKind = 0;
    std::int16_t  detailLevel = 0;
    // (v54/M-37) The register bit is gone: every place draws its id from
    // the ONE subject issuer (nextMacroSpawnOrdinal), so the id names the place.
    // The bit existed because cities and villages were numbered from zero
    // independently — killing ten peasants in village 3 took ten souls off
    // CITY 3 on the other side of the map.
};

} // namespace sm

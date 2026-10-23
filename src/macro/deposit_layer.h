// ОДНО ПРИРОДНОЕ ПОЛЕ ЖИЛ (вердикт владельца 2026-10-22, дословно: «жилы
// единые то есть одно природно поле жил а что там окажется решает фича шахты
// - золотая железеная мденая и тд»; и 2026-10-23: «добыча … существует по
// шахтам … и ставятся шахты толкьо если есть деревня рядом или город если
// нет то жила просто остаётся»).
//
// ЧТО ЭТОТ СЛОЙ ЕСТЬ. Производная геология мира: ёмкость каждой клетки —
// чистая функция терраина и сида (vein_virgin_at), грид строится при
// рождении мира и при загрузке ЗАНОВО из того же сида. ГЕЙМПЛЕЙ ЕГО НЕ
// МУТИРУЕТ: добыча существует по шахтам, а залежь шахты — число фичи в
// `worked` (кап связного кластера, свёрнутый генерацией шахт). Поэтому слой
// НЕ едет в сейв: хранить чистую функцию — хранить воздух.
//
// Рода у жилы НЕТ. Шесть родов (Clay/Iron/Stone/Silver/Copper/Gold) умерли
// вердиктом: род РЕСУРСА называет строка фичи шахты (FeatureDef::yieldItem).
// Шесть генераторных компонент (kDepositGen в .cpp) остались НЮАНСОМ
// ГЕНЕРАЦИИ — однократные буферы её закона (ЗАКОН ГЕНЕРАЦИИ п.3): глина
// ложится у воды, металл по хребтам, а поле у них ОДНО — сумма.
//
// Дикая жила без шахты стоит нетронутой — будущий контент разведки
// месторождений игроком (вердикт 2026-10-23); кода разведки здесь нет.
#pragma once
#include "macro/resource_field.h"   // ResourceGrid — THE shape of a row
#include "core/torus.h"
#include <cstdint>

#include "macro/map_generator.h"

namespace sm {

struct DepositLayer {
    int width = 0;
    int height = 0;
    // ОДНО поле жил: units per cell, 0 = жилы здесь нет. Плоский массив над
    // тором — связность мира и О(1)-вопрос досягаемости (reach-поле внутри
    // грида, радиус kGathererReach — досягаемость добытчика).
    ResourceGrid cells;
    // Сколько мир родил всего — отпечаток геологии и якорь калибровки.
    // Производное, в сейв не едет: build_deposit_layer — чистая функция.
    std::int64_t virginUnits = 0;
    // Сид рождения: залог того, что boot восстановит РОВНО это поле.
    std::uint32_t birthSeed = 0;
    // Runtime dirty counter for future consumers; never serialized.
    std::uint32_t revision = 0;

    // Packs a WRAPPED cell into a flat index. The wrap itself is the one in
    // core/torus.h; it used to be written out twice inline right here.
    std::uint32_t wrap_index(int x, int y) const {
        return cell_of(x, y, width);   // ЗАКОН АДРЕСА: одна дверь, маска
    }
    // Дикая жила клетки; 0 = здесь геологии нет.
    std::int32_t vein_at(int x, int y) const {
        return cells.at(x, y);
    }
    // «Есть ли живая жила в досягаемости добытчика» — O(1) по reach-полю.
    bool vein_near(int x, int y) const {
        return cells.near(x, y);
    }
};

// Size the field to a world, zeroed, carrying the gatherer's reach. Worldgen
// calls it; so must any fixture that hand-places veins, because a field that
// was never allocated silently swallows every write.
void allocate_deposit_fields(DepositLayer& layer, int width, int height);

// ЁМКОСТЬ КЛЕТКИ — ЧИСТАЯ ФУНКЦИЯ ТЕРРАИНА И СИДА, как фертильность
// (владелец, 2026-09-22). Сумма шести генераторных компонент (kDepositGen):
// профиль/сродство/порог — нюансы генерации, поле одно.
std::int32_t vein_virgin_at(const TerrainData& terrain, std::uint32_t seed,
                            int x, int y);

DepositLayer build_deposit_layer(const TerrainData& terrain,
                                 std::uint32_t seed);

// The GENESIS door: the world creates geology — a fixture's hand-placed vein,
// a future discovery law. Inserts (or refills) the cell, bumps the revision.
void create_deposit(DepositLayer& layer, int x, int y, std::int32_t amount);

// (УМЕРЛИ 2026-10-23 с осью родов: `DepositKind`, `kDepositDefs` — колонки
// `commodityId` (0 звонящих), `mineFeature` (0 читателей), `siteWorth`
// (один вес жилы живёт у сайт-скоринга); `set_deposit_remaining` — природное
// поле геймплеем не мутируется; `restore_deposit_cells` — слой производный,
// сейв его не возит; `consolidate_deposit_cluster` — 0 прод-звонящих, его
// работу делает генерация шахт (stamp_mines@src/macro/spawners.cpp), сводя
// кластер в число фичи `worked`.)

} // namespace sm

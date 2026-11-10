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
// `featureNumber` (кап связного кластера, свёрнутый генерацией шахт). Поэтому слой
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

// ДВА ПЛОСКИХ ПОЛЯ ПОД КАПОМ КЛЕТОК МИРА, HEAP-ЧЛЕНОВ НЕТ (M-180,
// 2026-11-10). Слой весит 4 МиБ и на стеке не живёт: рождается на месте (у
// `App`, в куче фикстуры), `build_deposit_layer` ПИШЕТ в него.
struct DepositLayer {
    int width = 0;
    int height = 0;
    // Сколько мир родил всего — отпечаток геологии и якорь калибровки.
    // Производное, в сейв не едет: build_deposit_layer — чистая функция.
    std::int64_t virginUnits = 0;
    // Сид рождения: залог того, что boot восстановит РОВНО это поле.
    std::uint32_t birthSeed = 0;
    // Runtime dirty counter for future consumers; never serialized.
    std::uint32_t revision = 0;

    // ОДНО поле жил: units per cell, 0 = жилы здесь нет. ЧИТАТЬ — кому
    // угодно; ПИСАТЬ — только дверью `deposit_set` ниже, и это стена, а не
    // просьба: поле закрыто `private`, наружу отдаётся константой. Причина в
    // диске досягаемости — он производное от клеток, и запись мимо двери
    // оставила бы его врать («руда кончилась, торг ещё выдаётся» — ровно
    // тот дефект, который ловил ручной негативный контроль штампа).
    const ResourceGrid& cells() const { return cells_; }

    // Packs a WRAPPED cell into a flat index. The wrap itself is the one in
    // core/torus.h; it used to be written out twice inline right here.
    std::uint32_t wrap_index(int x, int y) const {
        return cell_of(x, y, width);   // ЗАКОН АДРЕСА: одна дверь, маска
    }
    // Дикая жила клетки; 0 = здесь геологии нет.
    std::int32_t vein_at(int x, int y) const {
        return cells_.at(x, y);
    }
    // «Есть ли живая жила в досягаемости добытчика» — O(1) по полю
    // досягаемости. Fail-closed: неродившийся слой никого не достаёт.
    bool vein_near(int x, int y) const {
        return cells_.live() && reach_[cells_.index(x, y)] != 0u;
    }

private:
    friend void allocate_deposit_fields(DepositLayer&, int, int);
    friend void deposit_set(DepositLayer&, int, int, std::int32_t);

    ResourceGrid cells_;
    // ПРОИЗВОДНОЕ, в сейв не едет: сколько живых клеток жилы лежит в радиусе
    // `kGathererReach` от каждой клетки. Счёт, а не флаг, потому что диски
    // перекрываются — клетка, до которой достают три жилы, обязана пережить
    // истощение двух. Жило вектором внутри КАЖДОГО `ResourceGrid` и было
    // непустым только здесь; теперь оно у своего единственного хозяина.
    std::uint16_t reach_[kWorldCellCap] = {};

    // Вклад одной клетки в поле досягаемости: диск клеток, с которых эту
    // жилу можно разрабатывать.
    //
    // THE DISC IS THE QUESTION'S OWN SHAPE. The gate this replaces measured
    // `torus_dist_sq <= r²` — a circle — so the stamp walks a circle, its rows'
    // half-widths taken from the same inequality. A square would have been
    // separable and faster and would grant the corner cells something they
    // cannot reach: a stamp is an implementation of a question, never a
    // licence to answer a different one.
    void stamp_(int x, int y, int delta);
};
static_assert(sizeof(DepositLayer)
                  == 24u + sizeof(ResourceGrid)
                         + sizeof(std::uint16_t) * kWorldCellCap,
              "слой жил: 24 Б шапки + поле жил + поле досягаемости");

// Size the field to a world, zeroed, carrying the gatherer's reach. Worldgen
// calls it; so must any fixture that hand-places veins, because a field that
// was never allocated silently swallows every write.
void allocate_deposit_fields(DepositLayer& layer, int width, int height);

// ЁМКОСТЬ КЛЕТКИ — ЧИСТАЯ ФУНКЦИЯ ТЕРРАИНА И СИДА, как фертильность
// (владелец, 2026-09-22). Сумма шести генераторных компонент (kDepositGen):
// профиль/сродство/порог — нюансы генерации, поле одно.
std::int32_t vein_virgin_at(const TerrainData& terrain, std::uint32_t seed,
                            int x, int y);

// Пишет в `out` на месте: слой весит 4 МиБ, возврат значением клал бы его на
// стек звонящего.
void build_deposit_layer(DepositLayer& out, const TerrainData& terrain,
                         std::uint32_t seed);

// ЕДИНСТВЕННАЯ ЗАПИСЬ КЛЕТКИ ЖИЛЫ — абсолютная установка (0 = жила иссякла).
// Двигает клетку, счёт живых и ДИСК ДОСЯГАЕМОСТИ одним шагом. Вне пульта, как
// и `field_set`: звонят рождение мира, генезис и фикстура свидетеля; геймплей
// дикую жилу не мутирует («метал не убывает как и фертильность»).
void deposit_set(DepositLayer& layer, int x, int y, std::int32_t value);

// The GENESIS door: the world creates geology — a fixture's hand-placed vein,
// a future discovery law. Inserts (or refills) the cell, bumps the revision.
void create_deposit(DepositLayer& layer, int x, int y, std::int32_t amount);

// КАП СВЯЗНОСТИ: сумма связного кластера живых жил (8-смежность) вокруг
// клетки, клампленная шириной клетки поля. ЭТО И ЕСТЬ «кап шахты использует
// локальное значение связности жил» (вердикт 2026-10-23): генерация кладёт
// его залежью при постановке шахты, и ЗАЛЕЖЬ ДОРАСТАЕТ К НЕМУ во времени
// («фундаментальные поля дают кап, поля фич — текущее, дорастают до капа»).
// 0 = жилы под клеткой нет. O(кластер) — зовут генерация и сезонный ходок
// роста на ШАХТНЫХ клетках (их десятки на мир), не горячий путь.
std::int32_t vein_cluster_cap(const DepositLayer& layer, int x, int y);

// (УМЕРЛИ 2026-10-23 с осью родов: `DepositKind`, `kDepositDefs` — колонки
// `commodityId` (0 звонящих), `mineFeature` (0 читателей), `siteWorth`
// (один вес жилы живёт у сайт-скоринга); `set_deposit_remaining` — природное
// поле геймплеем не мутируется; `restore_deposit_cells` — слой производный,
// сейв его не возит; `consolidate_deposit_cluster` — 0 прод-звонящих, его
// работу делает генерация шахт (stamp_mines@src/macro/spawners.cpp), сводя
// кластер в число фичи `featureNumber`.)

} // namespace sm

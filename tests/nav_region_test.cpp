// Стресс-тест запечённой навигации (CANON S7, владелец 2026-09-02):
// «взять любого агента, телепортнуть в глушь — и он не растеряется, а
// выберется; никто не застревает, не тупит, не попадает в аттракторы».
//
// Синтетический тор 64×64: речное кольцо режет x-обход (маршруты обязаны
// заворачиваться через разрез — шов упражняется, не декларируется), пёстрые
// цены (транслируемые), озёрный карман — негативный контроль честной
// недостижимости. Ходок — ЧИСТЫЙ читатель nav_step: никакого SP, никакой
// физики — только «три чтения» и шаг. Трансляционная инвариантность: тот же
// мир, сдвинутый по тору, обязан водить теми же длинами (тор-закон
// gigahrush2: никакого дерева, только циклический граф).
#include "check.h"
#include "macro/labour.h"   // settle_souls / souls_flock — двери душ

#include "macro/nav_field.h"
#include "macro/pathfinding.h"
#include "macro/place_birth.h"   // birth_place — место родится ТЕЛОМ
#include "macro/squad.h"         // set_place_kind — смена вида места
#include "macro/state.h"
#include "macro/store.h"
#include "core/torus.h"

#include <cstdio>
#include <cstdlib>
#include <memory>

namespace {

constexpr int W = 64, H = 64;

// ОДИН store НА ВЕСЬ СВИДЕТЕЛЬ. Место есть неподвижный сквад, и ломтиком F
// оно ЕСТЬ слот этого store целиком — ни строки рядом. Фикстуры по-прежнему
// делят один блок, потому что профиль памяти store не зависит от населения
// (ЗАКОН СТАБИЛЬНОСТИ) и по store на фикстуру было бы гигабайтами за ничто;
// но разделяет их теперь не чужой bodyBits, а `store_reset` в начале каждой
// сборки: мир фикстуры начинается ПУСТЫМ, иначе её шесть округ считали бы
// и чужие.
sm::MacroStore& places() {
    static std::unique_ptr<sm::MacroStore> st = sm::make_macro_store();
    return *st;
}

// Чем мир родил своё место: адрес для ходока, ординал для округи, слот для
// двери смены вида. Все три — колонки ТЕЛА, и фикстура запоминает их при
// рождении, потому что выбирать ординал ей больше нечем (эмитент M-37).
struct Place {
    int x = 0, y = 0;
    int id = 0;
    std::uint16_t slot = 0;
};

// Транслируемая пёстрая цена: функция ОТНОСИТЕЛЬНОЙ координаты, сдвиг мира
// сдвигает и её — иначе инвариантность нечего проверять.
float cost_at(int x, int y) {
    return 1.0f + float((x * 7 + y * 13) % 5) * 0.25f;
}

bool is_river(int x, int y) {
    if (x == 16) return true;                      // кольцо режет x-обход
    // Озёрный карман: кольцо воды вокруг (56, 8) — внутренняя клетка суша,
    // но недостижима (честный NoRegion — негативный контроль).
    const int dx = std::abs(x - 56), dy = std::abs(y - 8);
    return std::max(dx, dy) == 1;
}

struct Fixture {
    sm::GameState gs;
    sm::PathCostData pc;
    sm::NavWorld nav;
    sm::MacroWorld mw{};
    std::vector<Place> seeded;

    void build(int shiftX, int shiftY) {
        gs.mapW = W;
        gs.mapH = H;
        gs.worldSeed = 42;
        pc.width = W;
        pc.height = H;
        pc.costGrid.assign(std::size_t(W) * H, 1.0f);
        pc.water.assign(std::size_t(W) * H, 0);
        // Рельеф плоский: карта высот — СЛОВО, и нулевое слово значит нулевой
        // уровень поля, то есть ни одного подъёма в цене шага.
        pc.height16.assign(std::size_t(W) * H, 0);
        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                const int ox = sm::wrapi(x - shiftX, W);
                const int oy = sm::wrapi(y - shiftY, H);
                const std::size_t i = std::size_t(y) * W + x;
                pc.costGrid[i] = cost_at(ox, oy);
                pc.water[i] = is_river(ox, oy) ? 1 : 0;
            }
        }
        const int lmx[6] = {2, 30, 50, 63, 5, 40};
        const int lmy[6] = {2, 2, 50, 32, 60, 20};
        sm::store_reset(places());   // мир фикстуры начинается пустым
        seeded.clear();
        for (int i = 0; i < 6; ++i) {
            const int x = sm::wrapi(lmx[i] + shiftX, W);
            const int y = sm::wrapi(lmy[i] + shiftY, H);
            const sm::MacroHandle h = sm::birth_place(
                gs, places(),
                i % 2 ? sm::SquadType::City : sm::SquadType::Village,
                x, y);
            // Души — дверью мира (v122): паства в worked, головы
            // в инвентарь ТЕЛА; жилое место гейтится именно пастой.
            sm::settle_souls(gs, places(), h.slot, 100);
            seeded.push_back(
                Place{x, y, int(places().spawnId[h.slot].index), h.slot});
        }
        mw.gs = &gs;
        mw.store = &places();
        mw.pathCost = &pc;
        sm::nav_bake(mw, nav);
    }

    // Остров через пролив (водное ребро, CANON S10 «ярус на стихию»):
    // материк x<20, остров 5×5 у (40,32), вокруг — открытое море. Пеший
    // профиль обязан ОТКАЗАТЬ (сухого маршрута нет), морской — ЗНАТЬ.
    void build_island() {
        gs.mapW = W;
        gs.mapH = H;
        gs.worldSeed = 43;
        pc.width = W;
        pc.height = H;
        pc.costGrid.assign(std::size_t(W) * H, 1.0f);
        pc.water.assign(std::size_t(W) * H, 1);
        pc.height16.assign(std::size_t(W) * H, 0);   // плоский рельеф, см. build
        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                const bool mainland = x < 20;
                const bool island =
                    std::abs(x - 40) <= 2 && std::abs(y - 32) <= 2;
                if (mainland || island)
                    pc.water[std::size_t(y) * W + x] = 0;
            }
        }
        sm::store_reset(places());   // мир фикстуры начинается пустым
        seeded.clear();
        {
            const sm::MacroHandle h =
                sm::birth_place(gs, places(), sm::SquadType::City, 5, 32);
            sm::settle_souls(gs, places(), h.slot, 100);
            seeded.push_back(
                Place{5, 32, int(places().spawnId[h.slot].index), h.slot});
        }
        {
            const sm::MacroHandle h = sm::birth_place(
                gs, places(), sm::SquadType::Village, 40, 32);
            sm::settle_souls(gs, places(), h.slot, 100);
            seeded.push_back(
                Place{40, 32, int(places().spawnId[h.slot].index), h.slot});
        }
        mw.gs = &gs;
        mw.store = &places();
        mw.pathCost = &pc;
        sm::nav_bake(mw, nav);
    }

    // «Суша ли» — ОПИСАНИЕ ГЕОГРАФИИ фикстуры, а не правило мира: с
    // 2026-10-06 предиката стояния не существует, и клетка воды законна для
    // всякого ходока. Осталось затем, чтобы ИЗМЕРЯТЬ, сколько воды прошёл
    // маршрут, и чтобы выборка могла начинаться с берега.
    bool dry(int x, int y) const {
        return pc.water[std::size_t(sm::wrapi(y, H)) * W
                        + std::size_t(sm::wrapi(x, W))] == 0;
    }

    // Чистый ходок: только nav_step. Возвращает шаги до цели, -1 = не дошёл
    // (лимит — жёсткая крышка против аттракторов и топтания); `wet`, если
    // передан, получает число пройденных водных клеток.
    //
    // ЗДЕСЬ ЖИЛА ТРЕТЬЯ КОПИЯ СТЕНЫ: `if (!standable(...)) return -1; //
    // походка в воду — дефект». То есть свидетель держал СВОЁ правило мира
    // рядом с правилом мира, и когда владелец снёс второе («ВЕСА БЫЛО
    // ЕДИНОЕ РЕШЕНИЕ»), третье осталось бы краснеть на верном поведении.
    // Теперь вода считается, а не запрещается.
    int walk(int x, int y, int tx, int ty, int* wet = nullptr) const {
        const int cap = 8 * W * H;
        if (wet) *wet = 0;
        for (int s = 0; s < cap; ++s) {
            if (sm::wrapi(x, W) == sm::wrapi(tx, W)
                && sm::wrapi(y, H) == sm::wrapi(ty, H))
                return s;
            int dx = 0, dy = 0;
            if (!sm::nav_step(nav, x, y, tx, ty, dx, dy)) return -1;
            x = sm::wrapi(x + dx, W);
            y = sm::wrapi(y + dy, H);
            if (wet && !dry(x, y)) ++*wet;
        }
        return -1;
    }
};

// Детерминированный LCG — тест не имеет права на Math.random.
std::uint32_t lcg(std::uint32_t& s) {
    s = s * 1664525u + 1013904223u;
    return s;
}

} // namespace

int main() {
    Fixture base;
    base.build(0, 0);

    // Запекание: шесть округ, вся достижимая суша разобрана.
    CHECK(base.nav.baked(), "nav bakes on the synthetic torus");
    CHECK(int(base.nav.regionLandmarkId.size()) == 6,
          "every landmark seeds a region");
    // Кап порталов НЕ резал переходы — счётчик, не stderr (грабля «кричит
    // в лог, grep не ловит» убита механизмом).
    CHECK(base.nav.portalOverflows == 0,
          "no region silently lost a portal to the cap");
    // ВСЯ КАРТА РАЗОБРАНА, И ЭТО ИЗМЕРИМЫЙ СМЫСЛ СНОСА СТЕНЫ. До 2026-10-06
    // тут стоял обратный негативный контроль — «ровно одна клетка суши,
    // озёрный карман, обязана остаться ничьей», — и он был верен ровно
    // потому, что кольцо воды вокруг (56,8) ЗАПРЕЩАЛО вход. В живых мирах
    // та же стена держала 4328–4701 клетку суши вне всякой округи. Теперь
    // вода есть цена, и ничьих клеток не бывает ни одной: ни суши, ни моря.
    std::size_t cellsAll = 0, owned = 0, dryCells = 0, dryOwned = 0;
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            ++cellsAll;
            const bool isOwned =
                sm::nav_region_at(base.nav, x, y) != sm::kNavNoRegion;
            if (isOwned) ++owned;
            if (base.dry(x, y)) { ++dryCells; if (isOwned) ++dryOwned; }
        }
    CHECK(owned == cellsAll,
          "EVERY cell of the torus belongs to a region — water included");
    CHECK(dryCells > 0 && dryOwned == dryCells,
          "...and in particular every cell of LAND, which is the requirement "
          "«от любого места до любого добраться» made true by construction");
    CHECK(sm::nav_region_at(base.nav, 56, 8) != sm::kNavNoRegion,
          "the lake pocket has an округа now: its ring of water is a price");

    // Ландмарк → ландмарк: все 30 упорядоченных пар доходят (в т.ч. через
    // разрез — река x=16 заставляет заворачиваться).
    int pairSteps[6][6] = {};
    for (int a = 0; a < 6; ++a) {
        for (int b = 0; b < 6; ++b) {
            if (a == b) continue;
            const Place& A = base.seeded[std::size_t(a)];
            const Place& B = base.seeded[std::size_t(b)];
            const int steps = base.walk(A.x, A.y, B.x, B.y);
            CHECK(steps > 0, "landmark pair arrives");
            pairSteps[a][b] = steps;
        }
    }

    // ТЕЛЕПОРТ В ГЛУШЬ (критерий владельца): случайные становимые клетки ×
    // случайные ландмарки — каждый ходок выбирается, никто не зацикливается.
    std::uint32_t rng = 12345;
    int walked = 0;
    for (int i = 0; i < 160; ++i) {
        const int x = int(lcg(rng) % W);
        const int y = int(lcg(rng) % H);
        // Выборка берётся с СУШИ — не потому, что вода незаконна, а потому,
        // что «телепорт в глушь» есть критерий владельца про сухопутного
        // ходока; гейт NoRegion снят, таких клеток больше нет.
        if (!base.dry(x, y)) continue;
        const Place& T = base.seeded[lcg(rng) % 6];
        CHECK(base.walk(x, y, T.x, T.y) >= 0,
              "a stranded walker finds its way out");
        ++walked;
    }
    CHECK(walked > 100, "the wilderness sample actually sampled");

    // Трансляционная инвариантность: тот же мир, сдвинутый по тору, водит
    // теми же длинами (допуск 2 шага на равноценные развязки) — разрез не
    // существует нигде в пайплайне.
    Fixture shifted;
    shifted.build(23, 37);
    for (int a = 0; a < 6; ++a) {
        for (int b = 0; b < 6; ++b) {
            if (a == b) continue;
            const Place& A = shifted.seeded[std::size_t(a)];
            const Place& B = shifted.seeded[std::size_t(b)];
            const int steps = shifted.walk(A.x, A.y, B.x, B.y);
            CHECK(steps > 0, "shifted pair arrives");
            CHECK(std::abs(steps - pairSteps[a][b]) <= 2,
                  "route length survives the torus translation");
        }
    }

    // ── ОСТРОВ ДОСТИЖИМ, И ЭТО ГЛАВНЫЙ СВИДЕТЕЛЬ M-236 ────────────────
    // Этот блок утверждал РОВНО ОБРАТНОЕ до 2026-10-06: «пеший профиль
    // ОТКАЗЫВАЕТ (сухого маршрута нет), морской ЗНАЕТ». Вердикт владельца
    // снёс вторую стену — «да уничтодить вторую стену она портит всё (ВЕСА
    // БЫЛО ЕДИНОЕ РЕШЕНИЕ» — и вместе с ней отказ: пролив не запрещён, он
    // ДОРОГ. Морской ярус умер тем же шагом (M-237), потому что его вопрос
    // «чья эта вода» перестал существовать: у воды теперь своя округа.
    {
        Fixture sea;
        sea.build_island();
        CHECK(sea.nav.baked(), "island world bakes");
        CHECK(int(sea.nav.regionLandmarkId.size()) == 2,
              "mainland and island each seed a region");
        const auto rm = sm::nav_region_at(sea.nav, 5, 32);
        const auto ri = sm::nav_region_at(sea.nav, 40, 32);
        CHECK(rm != sm::kNavNoRegion && ri != sm::kNavNoRegion && rm != ri,
              "the strait still SPLITS the partition: water is dear, so the "
              "two shores remain two округи with a portal between them");
        const std::size_t R = sea.nav.regionLandmarkId.size();
        CHECK(sea.nav.routeNext[std::size_t(ri) * R + rm]
                  != sm::kNavNoRegion,
              "THE ISLAND IS REACHABLE ON FOOT: the one table knows the "
              "crossing, because a price is not a refusal");
        CHECK(sea.nav.routeNext[std::size_t(rm) * R + ri]
                  != sm::kNavNoRegion,
              "and it carries both directions");
        // Открытое море — тоже чья-то округа: заливка замощает ВЕСЬ тор, и
        // «ничейных» клеток не остаётся ни одной. Это та самая правда,
        // которой стена не давала быть: 4328–4701 клетка суши жила вне всякой
        // округи в каждом замеренном мире.
        CHECK(sm::nav_region_at(sea.nav, 44, 32) != sm::kNavNoRegion,
              "shore water belongs to a region like any other cell");
        std::size_t noRegion = 0;
        for (std::size_t c = 0; c < sea.nav.regionOf.size(); ++c)
            if (sea.nav.regionOf[c] == sm::kNavNoRegion) ++noRegion;
        CHECK(noRegion == 0,
              "NOT ONE CELL OF THE TORUS IS OUTSIDE A REGION — the measured "
              "point of the demolition, and the negative control is the old "
              "world itself, where this count was in the thousands");
    }

    // ── ЦЕНА ПУТИ — ОДНА ДВЕРЬ, И ОНА ЗНАЕТ ПРО ОБХОД (CANON S7) ───────
    {
        // Недостижимое честно: карман озера — это НЕТ ПУТИ, а не большое
        // число. Тот же класс тихой ошибки, что «недостижимость нулём»:
        // вес рулетки перевернулся бы, и место за водой стало бы лучшим.
        // ...И ТЕПЕРЬ ТАКИХ КАРМАНОВ НЕТ. Блок утверждал, что озёрный
        // карман (56,8) — кольцо воды вокруг одной сухой клетки — честно
        // отвечает kNavFar. После сноса стены вода есть ЦЕНА, кольцо
        // переходится, и карман стоит дорого, а не бесконечно. Сам отказ
        // никуда не делся как МЕХАНИЗМ (`kNavFar` возвращается при
        // незапечённом мире и выходе за таблицу) — но географией он больше
        // не рождается, и это ровно то требование владельца «от любого места
        // до любого добраться», ставшее правдой по построению.
        const std::uint32_t pocket = sm::nav_path_cost(base.nav, 5, 32, 56, 8);
        CHECK(pocket != sm::kNavFar,
              "a lake pocket is EXPENSIVE, not unreachable: the ring of water "
              "is a price now");
        // ...и строго дороже клетки ЗА кольцом. (56,6) — именно она: кольцо
        // есть `max(|x-56|,|y-8|) == 1`, поэтому (55,7) сама вода, и сравнение
        // с ней не измеряло бы переправу.
        CHECK(base.dry(56, 6) && !base.dry(56, 7),
              "the fixture is honest: (56,6) is dry land outside the ring and "
              "(56,7) is the ring itself");
        CHECK(pocket > sm::nav_path_cost(base.nav, 5, 32, 56, 6),
              "...and strictly dearer than the dry doorstep outside the ring, "
              "so the water really is being PAID for and not ignored");
        // ЗАКОН, НА КОТОРОМ СТОЯТ ВСЕ ЧИТАТЕЛИ: из МЕСТА дверь точна —
        // цена до любой клетки его округи равна ровно distHome этой клетки,
        // тому самому числу, которым ходит артель и которое хранит опись.
        {
            const std::int32_t rc0 = base.nav.regionCell[0];
            CHECK(rc0 >= 0, "region 0 names a cell");
            const int lx = int(rc0 % base.nav.mapW);
            const int ly = int(rc0 / base.nav.mapW);
            CHECK(base.nav.distHome[base.nav.cell(lx, ly)] == 0u,
                  "a place stands at the zero of its own field");
            CHECK(sm::nav_path_cost(base.nav, lx, ly, lx, ly) == 0u,
                  "from a place to itself the path costs nothing");
            int checked = 0;
            for (int y = 0; y < H && checked < 64; ++y)
                for (int x = 0; x < W && checked < 64; ++x) {
                    if (sm::nav_region_at(base.nav, x, y) != 0) continue;
                    const std::uint32_t dh =
                        base.nav.distHome[base.nav.cell(x, y)];
                    if (dh == sm::kNavUnreached) continue;
                    ++checked;
                    if (sm::nav_path_cost(base.nav, lx, ly, x, y) != dh) {
                        CHECK(false,
                              "from a place the door is exactly distHome");
                        y = H; break;
                    }
                }
            CHECK(checked >= 16, "the law was actually exercised");
        }

        // ЗАКОН: путь НИКОГДА не дешевле хорды — и на этой карте есть пары,
        // где он строго дороже. Именно эту разницу теряла прямая, и на ней
        // провиант вылазки недокармливал сквад, идущий в обход.
        std::uint32_t seed = 20260920u;
        int sampled = 0, cheaper = 0, detour = 0;
        for (int i = 0; i < 400 && sampled < 120; ++i) {
            const int ax = int(lcg(seed) % unsigned(W));
            const int ay = int(lcg(seed) % unsigned(H));
            const int bx = int(lcg(seed) % unsigned(W));
            const int by = int(lcg(seed) % unsigned(H));
            if (!base.dry(ax, ay) || !base.dry(bx, by)) continue;
            const std::uint32_t c = sm::nav_path_cost(base.nav, ax, ay, bx, by);
            if (c == sm::kNavFar) continue;
            ++sampled;
            const float cells = float(c) / 16.0f;
            const float chord = std::sqrt(sm::torus_dist_sq(
                float(ax), float(ay), float(bx), float(by),
                float(W), float(H)));
            if (cells + 0.125f < chord) ++cheaper;
            if (cells > chord + 1.0f) ++detour;
        }
        CHECK(sampled >= 40, "the sampler actually found reachable pairs");
        CHECK(cheaper == 0, "the path is never cheaper than the chord");
        CHECK(detour > 0,
              "and on a world with a river some pairs must go around");
    }

    // ── СВЕЖЕСТЬ — ПО СОБЫТИЮ, А НЕ ПО ОПРОСУ (CANON S9, 2026-09-20) ────
    {
        Fixture f;
        f.build(0, 0);
        // Метка в производном поле: пережила nav_ensure — значит перепёка
        // НЕ БЫЛО. Это наблюдаемый способ утверждать «работа не делалась».
        const std::uint16_t mark = 0xBEEFu;
        f.nav.regionOf[0] = mark;
        CHECK(sm::nav_ensure(f.mw, f.nav),
              "ensure answers on an already baked world");
        CHECK(f.nav.regionOf[0] == mark,
              "an unchanged world is not rebaked: no poll, no work");

        // ПЕРЕХОД ПЛЮС РОЖДЕНИЕ В ОДНОМ ОКНЕ — ровно та тихая ошибка, на
        // которой стоял старый сторож: он сравнивал ЧИСЛО ЖИВЫХ мест, а
        // деревня, ставшая руиной, живой считается по-прежнему — сторож не
        // видел НИЧЕГО, хотя реестровая строка места сменилась. Событию
        // такое не сойдёт.
        sm::set_place_kind(f.gs, places(), f.seeded[1].slot,
                           sm::SquadType::Ruin);
        const int ruinId = f.seeded[1].id;
        const sm::MacroHandle bornH =
            sm::birth_place(f.gs, places(), sm::SquadType::Village, 40, 32);
        sm::settle_souls(f.gs, places(), bornH.slot, 100);
        const int bornId = int(places().spawnId[bornH.slot].index);
        CHECK(sm::nav_ensure(f.mw, f.nav),
              "ensure still answers after the swap");
        CHECK(f.nav.regionOf[0] != mark,
              "death plus birth in one window still rebakes");
        bool sawDead = false, sawBorn = false;
        for (std::int32_t lid : f.nav.regionLandmarkId) {
            sawDead = sawDead || lid == ruinId;
            sawBorn = sawBorn || lid == bornId;
        }
        CHECK(sawDead && sawBorn,
              "the ruin still stands and seeds its region, the newborn too");
    }

    return sm::test::report("nav_region_test");
}

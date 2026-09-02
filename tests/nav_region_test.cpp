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

#include "macro/nav_field.h"
#include "macro/pathfinding.h"
#include "macro/state.h"
#include "core/torus.h"

#include <cstdio>
#include <cstdlib>

namespace {

constexpr int W = 64, H = 64;

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

    void build(int shiftX, int shiftY) {
        gs.mapW = W;
        gs.mapH = H;
        gs.worldSeed = 42;
        pc.width = W;
        pc.height = H;
        pc.costGrid.assign(std::size_t(W) * H, 1.0f);
        pc.water.assign(std::size_t(W) * H, 0);
        pc.height8.assign(std::size_t(W) * H, 0);
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
        gs.landmarks.clear();
        for (int i = 0; i < 6; ++i) {
            sm::Landmark lm{};
            lm.id = i + 1;
            lm.type = i % 2 ? sm::LandmarkType::City
                            : sm::LandmarkType::Village;
            lm.x = sm::wrapi(lmx[i] + shiftX, W);
            lm.y = sm::wrapi(lmy[i] + shiftY, H);
            lm.population = 100;
            gs.landmarks.push_back(std::move(lm));
        }
        mw.gs = &gs;
        mw.pathCost = &pc;
        sm::nav_bake(mw, nav);
    }

    bool standable(int x, int y) const {
        return pc.water[std::size_t(sm::wrapi(y, H)) * W
                        + std::size_t(sm::wrapi(x, W))] == 0;
    }

    // Чистый ходок: только nav_step. Возвращает шаги до цели, -1 = не дошёл
    // (лимит — жёсткая крышка против аттракторов и топтания).
    int walk(int x, int y, int tx, int ty) const {
        const int cap = 8 * W * H;
        for (int s = 0; s < cap; ++s) {
            if (sm::wrapi(x, W) == sm::wrapi(tx, W)
                && sm::wrapi(y, H) == sm::wrapi(ty, H))
                return s;
            int dx = 0, dy = 0;
            if (!sm::nav_step(nav, x, y, tx, ty, dx, dy)) return -1;
            x = sm::wrapi(x + dx, W);
            y = sm::wrapi(y + dy, H);
            if (!standable(x, y)) return -1;   // походка в воду — дефект
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
    std::size_t land = 0, owned = 0;
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            if (!base.standable(x, y)) continue;
            ++land;
            if (sm::nav_region_at(base.nav, x, y) != sm::kNavNoRegion)
                ++owned;
        }
    // Негативный контроль: ровно одна клетка суши — озёрный карман — обязана
    // остаться ничьей; будь она достижима, тест бы лгал о честности NoRegion.
    CHECK(land - owned == 1, "exactly the lake pocket stays unreachable");
    CHECK(sm::nav_region_at(base.nav, 56, 8) == sm::kNavNoRegion,
          "the lake pocket is honestly NoRegion");

    // Ландмарк → ландмарк: все 30 упорядоченных пар доходят (в т.ч. через
    // разрез — река x=16 заставляет заворачиваться).
    int pairSteps[6][6] = {};
    for (int a = 0; a < 6; ++a) {
        for (int b = 0; b < 6; ++b) {
            if (a == b) continue;
            const auto& A = base.gs.landmarks[std::size_t(a)];
            const auto& B = base.gs.landmarks[std::size_t(b)];
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
        if (!base.standable(x, y)) continue;
        if (sm::nav_region_at(base.nav, x, y) == sm::kNavNoRegion) continue;
        const auto& T = base.gs.landmarks[lcg(rng) % 6];
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
            const auto& A = shifted.gs.landmarks[std::size_t(a)];
            const auto& B = shifted.gs.landmarks[std::size_t(b)];
            const int steps = shifted.walk(A.x, A.y, B.x, B.y);
            CHECK(steps > 0, "shifted pair arrives");
            CHECK(std::abs(steps - pairSteps[a][b]) <= 2,
                  "route length survives the torus translation");
        }
    }

    return sm::test::report("nav_region_test");
}

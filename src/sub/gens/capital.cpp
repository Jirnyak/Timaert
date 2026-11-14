// СТОЛИЦА — модуль генерации субмира фичи `FT_Capital`, ОДНИМ ФАЙЛОМ.
//
// Копия законна (AGENTS §4 п.5: под модули общего не создаётся; копия кода
// между модулями законна и ожидаема) — сегодня это полная копия городского
// модуля под своими именами, стена и улицы вместе.
// Особый генератор столицы пишется в этом файле (вердикт владельца:
// «ФИЧА КАПИТАЛ НАДО! ТАМ БУДЕТ ОСОБЫЙ ГЕНЕРАТОР!», 2026-10-24; на развилку
// 2026-11-13 — «Свой модуль — копия городского», ряд M-522).
//
// Модуль ни с кем вбок не говорит: ни файлов соседа, ни его функций. Под ним
// — только пол: базовый рельеф (`sub/base_generator.h`), законы размера
// поселения (`sub/city_layout.h` — их же читает план толпы столицы
// `crowd_plan_capital@src/sub/crowd_plan.h`, поэтому стены и люди не
// расходятся) и примитивы `gens/kit/`.
//
// ПОРЯДОК СТРОЙКИ И ЕСТЬ ЗАМЫСЕЛ — каждый шаг читает то, что оставил на земле
// предыдущий:
//
//   1. ТРАКТ приходит первым (он был здесь раньше столицы — макрокарта кладёт
//      дорогу на клетку), нацеленный на якоря шва, в которые целятся соседи;
//   2. СТЕНА встаёт поперёк него и открывается ровно там, где находит под
//      собой мостовую: ворота — это ворота на настоящую дорогу, и пустой
//      арки рядом со вторым проёмом не бывает по построению;
//   3. УЛИЦЫ растут внутри стены и пробить её не могут (переулок кладку не
//      режет — `gens/kit/streets.cpp`);
//   4. ДОМА заполняют кварталы, ограниченные контуром стены по каждому
//      румбу, поэтому ни один не встанет там, где пойдёт кольцо;
//   5. ПОЛЯ занимают округу за бермой, и их дороги возвращаются к ВОРОТАМ.
#include "sub/gens/gens.h"

#include "sub/base_generator.h"
#include "sub/city_layout.h"
#include "sub/gens/kit/growth.h"
#include "sub/gens/kit/lanes.h"
#include "sub/gens/kit/outline.h"
#include "sub/gens/kit/plots.h"
#include "sub/gens/kit/props.h"
#include "sub/gens/kit/streets.h"
#include "sub/gens/kit/tiles.h"
#include "sub/height.h"
#include "sub/map_data.h"

#include "core/rng.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>
#include "core/math.h"  // kPi/kTwoPi — одно написание на мир

namespace sm::sub {
// Пол — словарь модуля: примитивы, а не соседний модуль.
using namespace kit;

namespace {

// ══ СТЕНА СТОЛИЦЫ — кладка, и поднимает её только этот модуль ════════════
//
// Два закона, ради которых она есть:
//
//   · ВОРОТА — ТАМ, ГДЕ ДОРОГА. Не там, где сказал компас: звонящий режет
//     свои дороги ПЕРВЫМ, и кольцо открывается ровно там, где находит под
//     собой мостовую. Поэтому всякий проём есть дорога, и пары ворот «одни
//     над травой, другие у настоящей дороги» не бывает.
//   · КОЛЬЦО РЕШЕНО ДО ТОГО, КАК НА НЁМ ЧТО-ТО ПОСТРОЕНО. Контур считается
//     первым и отдаётся каждому следующему укладчику (`kit/outline.h`),
//     поэтому ничего не встаёт там, где пойдёт стена, и дыры, через которую
//     можно пройти, в кольце не остаётся.
//
// Амплитуды шума кольца живут в полу (`sub/city_layout.h`), потому что план
// толпы выводит из них худшую ВНУТРЕННЮЮ экскурсию кольца — что значит
// «внутри стен».

// НАСКОЛЬКО ПЕРЕМЫЧКА ШИРЕ СВОЕГО ПРОЁМА — арка садится НА КОСЯКИ, а не на
// воздух. Число нужно ДВОИМ: тому, кто ставит перемычку, и тому, кто
// резервирует под ней землю; пока резерв считался бы от проёма, полоса под
// самим брусом оставалась бы свободной, и дом законно вставал бы в устье.
constexpr float kCapitalGateArchOverhangTiles = 1.2f;

struct CapitalCurtainStyle {
    float height;    // высота куртины, метры — ряды модуля стены
    bool  towers;    // круглые башни там, где кольцо поворачивает
};

// Размеры кладки — все выведены.
// Полутолщина — из той же модели кольца, которую читает план толпы, поэтому
// его «внутри стен» учитывает настоящую кладку (`sub/city_layout.h`).
constexpr float kCapitalWallHalfThick = kSettlementWallRing.halfThickness;

// Самый длинный прямой кусок куртины. Короткие ложатся по рельефу, длинные
// перекрывают впадину и висят в воздухе одним концом.
constexpr float kCapitalWallPieceLen = 8.0f;

// Башня читается башней, только если выступает из куртины с ОБЕИХ сторон хотя
// бы на её толщину — иначе это шишка в стене. Радиус = полутолщина × 3.
constexpr float kCapitalTowerRadius = kCapitalWallHalfThick * 3.0f;

// Самый широкий проём, ещё получающий арку. Шире — это ПРОЛОМ, и перемычка
// была бы мостом, а не воротами; косяки ставятся всё равно — куртина там
// кончается, и концы её обязаны быть закончены.
constexpr float kCapitalGateMaxSpan = 18.0f;

// Косяк ворот — башня, заканчивающая конец куртины. Тот же камень, то же
// правило.
constexpr float kCapitalGateJambR = kCapitalTowerRadius * 0.5f;

// Поднять куртину. Возвращает, сколько ворот прорезано, записывая до
// `maxGates` из них; проёмы сверх этого режутся всё равно, только не
// называются.
//
// `clip`, если дан, — граница, за которую кольцо не выходит: точки снаружи
// не строятся вовсе. Так внутренняя ограда ДЕЛИТ сторону с куртиной вместо
// того, чтобы вести свою стену рядом: верхний квартал прислонён к стене
// столицы, и дуга, стоявшая бы снаружи, и есть сама стена столицы.
int stamp_capital_wall(SubworldMapData& out, const Outline& outline,
                       const CapitalCurtainStyle& style, WallGate* gates,
                       int maxGates, const Outline* clip = nullptr) {
    // Точка, на которой этому кольцу строить нельзя: за общей границей.
    auto clipped = [&](float x, float y) {
        return clip != nullptr && !clip->contains(x, y, 0.0f);
    };
    constexpr int kNodes = Outline::kBearings;
    std::array<float, kNodes> xs{};
    std::array<float, kNodes> ys{};
    for (int i = 0; i < kNodes; ++i) {
        const float angle = float(i) * kTwoPi / float(kNodes);
        xs[std::size_t(i)] = outline.cx + std::cos(angle) * outline.r[std::size_t(i)];
        ys[std::size_t(i)] = outline.cy + std::sin(angle) * outline.r[std::size_t(i)];
    }

    auto tile_protected = [&](float fx, float fy) {
        const int tx = std::clamp(int(std::floor(fx)), 0, kCellSize - 1);
        const int ty = std::clamp(int(std::floor(fy)), 0, kCellSize - 1);
        const std::uint8_t t = out.tiles[std::size_t(ty) * kCellSize + tx];
        // Застроенная земля МИНУС кладка: кольцо уступает переулку, площади,
        // дому и плугу — но не своим же рядам, иначе кольцо не сомкнулось бы
        // над только что положенным отрезком.
        return tile_is(t, kTileBuilt & ~TILE_M_WALL);
    };

    // ── Проход 1 — штамп тайлов: кисть 3×3 вдоль каждой хорды. ──
    for (int i = 0; i < kNodes; ++i) {
        const int next = (i + 1) % kNodes;
        const float x1 = xs[std::size_t(i)];
        const float y1 = ys[std::size_t(i)];
        const float dx = xs[std::size_t(next)] - x1;
        const float dy = ys[std::size_t(next)] - y1;
        const float dist = std::sqrt(dx * dx + dy * dy);
        const int steps = std::max(1, int(std::ceil(dist * 2.0f)));
        for (int s = 0; s <= steps; ++s) {
            const float t = float(s) / float(steps);
            if (clipped(x1 + dx * t, y1 + dy * t)) continue;
            const int x = int(std::floor(x1 + dx * t));
            const int y = int(std::floor(y1 + dy * t));
            for (int oy = -1; oy <= 1; ++oy) {
                for (int ox = -1; ox <= 1; ++ox) {
                    const int px = x + ox;
                    const int py = y + oy;
                    if (px < 0 || py < 0 || px >= kCellSize || py >= kCellSize) continue;
                    const std::size_t idx = std::size_t(py) * kCellSize + px;
                    // Переулок и площадь не закрашиваются: эти тайлы И ЕСТЬ
                    // проёмы, а заложенные ворота — столица без входа.
                    if (tile_is(out.tiles[idx], kTileBuilt & ~TILE_M_WALL)) continue;
                    out.tiles[idx] = TILE_WALL;
                    out.trav[idx] = 0;
                }
            }
        }
    }

    // ── Проход 2 — ориентированные тела. Кольцо обходится шагом ~1 тайл,
    // каждая точка классифицируется, и каждый непрерывный отрезок выпускается:
    // стена — короткими ориентированными хордами по кривизне, проём —
    // настоящими воротами (два круглых косяка и перемычка, поднятая над
    // проезжей частью: тела проходят под ней, защитники — поверху). ──
    enum class RingCls : std::uint8_t { Wall, Gate, Shared };
    struct RingSample { float x, y; RingCls cls; };
    std::vector<RingSample> ringPts;
    for (int i = 0; i < kNodes; ++i) {
        const int next = (i + 1) % kNodes;
        const float x1 = xs[std::size_t(i)];
        const float y1 = ys[std::size_t(i)];
        const float dx = xs[std::size_t(next)] - x1;
        const float dy = ys[std::size_t(next)] - y1;
        const float dist = std::sqrt(dx * dx + dy * dy);
        const int steps = std::max(1, int(std::ceil(dist)));
        for (int s = 0; s < steps; ++s) {  // исключая: начало — у следующей хорды
            const float t = float(s) / float(steps);
            const float px = x1 + dx * t;
            const float py = y1 + dy * t;
            const int tx = std::clamp(int(std::floor(px)), 0, kCellSize - 1);
            const int ty = std::clamp(int(std::floor(py)), 0, kCellSize - 1);
            const std::uint8_t tile = out.tiles[std::size_t(ty) * kCellSize + tx];
            // ПРАВИЛО ВОРОТ: кольцо открывается там, где дорога уже идёт.
            // Больше его не открывает ничто, и всякий проём поэтому дорога.
            // ОБЩИЙ отрезок — ни то ни другое: там уже стоит граничная стена.
            ringPts.push_back({px, py,
                clipped(px, py)             ? RingCls::Shared :
                tile_is(tile, kTilePaved)   ? RingCls::Gate   : RingCls::Wall});
        }
    }

    int gateCount = 0;
    const int n = int(ringPts.size());
    int startIdx = 0;
    while (startIdx < n && ringPts[std::size_t(startIdx)].cls != RingCls::Wall) {
        ++startIdx;
    }
    if (n >= 4 && startIdx < n) {
        auto at = [&](int k) -> const RingSample& {
            return ringPts[std::size_t((startIdx + k) % n)];
        };
        auto emit_wall_run = [&](int a, int b) {  // отрезок [a, b] в повёрнутом индексе
            int piece0 = a;
            while (piece0 <= b) {
                int piece1 = std::min(b, piece0 + int(kCapitalWallPieceLen) - 1);
                const RingSample& p0 = at(piece0);
                const RingSample& p1 = at(piece1);
                const float ddx = p1.x - p0.x;
                const float ddy = p1.y - p0.y;
                const float len = std::sqrt(ddx * ddx + ddy * ddy);
                Structure w{};
                w.kind = Structure::Wall;
                w.x = (p0.x + p1.x) * 0.5f;
                w.y = (p0.y + p1.y) * 0.5f;
                w.yaw = std::atan2(ddy, ddx);
                w.hx = std::max(kCapitalWallHalfThick, len * 0.5f + 0.45f);
                w.hy = kCapitalWallHalfThick;
                w.radius = w.hx;
                w.height = style.height;
                out.structures.push_back(w);
                piece0 = piece1 + 1;
            }
        };
        auto emit_gate_run = [&](int a, int b) {
            const RingSample& g0 = at(a);
            const RingSample& g1 = at(b);
            const float ddx = g1.x - g0.x;
            const float ddy = g1.y - g0.y;
            const float span = std::sqrt(ddx * ddx + ddy * ddy);
            if (span < 2.0f) return;   // царапина мостовой, а не проход
            const float ux = ddx / span;
            const float uy = ddy / span;
            const float mx = (g0.x + g1.x) * 0.5f;
            const float my = (g0.y + g1.y) * 0.5f;
            // Косяки заканчивают концы куртины, втянутые в них; пропускаются,
            // если это место само проезжая часть.
            for (int side = 0; side < 2; ++side) {
                const float dir = side == 0 ? -1.0f : 1.0f;
                const float jx = (side == 0 ? g0.x : g1.x) + dir * ux * kCapitalGateJambR * 0.5f;
                const float jy = (side == 0 ? g0.y : g1.y) + dir * uy * kCapitalGateJambR * 0.5f;
                if (tile_protected(jx, jy)) continue;
                Structure j{};
                j.kind = Structure::Wall;
                j.x = jx;
                j.y = jy;
                j.radius = kCapitalGateJambR;
                j.height = style.height + kBodyEyeM;   // косяк смотрит поверх своих ворот
                j.shape = Structure::Cylinder;
                out.structures.push_back(j);
            }
            // Перемычка: брус через проём на высоте арки. zBase поднимает его
            // сплошной пролёт над проезжей частью. Пролом слишком широкий для
            // арки сохраняет косяки и остаётся без неё.
            if (span <= kCapitalGateMaxSpan) {
                Structure l{};
                l.kind = Structure::Wall;
                l.x = mx;
                l.y = my;
                l.yaw = std::atan2(ddy, ddx);
                l.hx = span * 0.5f + kCapitalGateArchOverhangTiles;
                l.hy = kCapitalWallHalfThick;
                l.radius = l.hx;
                // ОБЕЩАНИЕ, сказанное прямо: столько воздуха над проходом.
                // От чего его мерить, ещё не известно — землю под воротами
                // ещё режет сглаживание дорог после каждого генератора, —
                // поэтому посадку решает seat_lifted_spans (`sub/height.h`),
                // когда карта окончательна.
                l.zBase = kGateClearM;
                l.height = std::max(2.0f, style.height - kGateClearM);
                out.structures.push_back(l);
            }
            if (gates != nullptr && gateCount < maxGates) {
                gates[gateCount] = {mx, my,
                    std::atan2(my - outline.cy, mx - outline.cx), span};
            }
            ++gateCount;
        };
        int runStart = 0;
        RingCls runCls = at(0).cls;
        for (int k = 1; k <= n; ++k) {
            // Сторож закрывает последний отрезок: индекс 0 — точка Wall по
            // построению (startIdx искал её), поэтому конец на Gate заставляет
            // последний проём выпуститься.
            const RingCls cls = (k == n) ? RingCls::Wall : at(k).cls;
            if (k < n && cls == runCls) continue;
            if (runCls == RingCls::Wall)      emit_wall_run(runStart, k - 1);
            else if (runCls == RingCls::Gate) emit_gate_run(runStart, k - 1);
            // Общий: строить нечего — другая стена уже стоит.
            runStart = k;
            runCls = cls;
        }
    }

    // ── Проход 3 — башни ТАМ, ГДЕ КОЛЬЦО ПОВОРАЧИВАЕТ. ──
    // Башня фланкирует угол — поэтому она стоит в каждом локальном максимуме
    // поворота контура: без параметров, сама настраивается по форме и ставит
    // башни на выступы выросшего контура, а не на решётку.
    if (style.towers) {
        // Башня — ЭТАЖ над куртиной, а не горб на ней: её пол — потолок
        // боевого хода, поэтому она поднимается на один полный ряд того же
        // модуля стены, из которого сложена куртина.
        const float towerH = style.height + structure_min_height(Structure::Wall);
        std::array<float, kNodes> turn{};
        for (int i = 0; i < kNodes; ++i) {
            const int p = (i + kNodes - 1) % kNodes;
            const int q = (i + 1) % kNodes;
            const float ax = xs[std::size_t(i)] - xs[std::size_t(p)];
            const float ay = ys[std::size_t(i)] - ys[std::size_t(p)];
            const float bx = xs[std::size_t(q)] - xs[std::size_t(i)];
            const float by = ys[std::size_t(q)] - ys[std::size_t(i)];
            const float la = std::sqrt(ax * ax + ay * ay);
            const float lb = std::sqrt(bx * bx + by * by);
            if (la <= 0.0f || lb <= 0.0f) continue;
            // |sin| поворота между соседними хордами.
            turn[std::size_t(i)] = std::fabs(ax * by - ay * bx) / (la * lb);
        }
        for (int i = 0; i < kNodes; ++i) {
            const float t = turn[std::size_t(i)];
            if (t <= turn[std::size_t((i + kNodes - 1) % kNodes)]) continue;
            if (t <= turn[std::size_t((i + 1) % kNodes)]) continue;
            const float nx = xs[std::size_t(i)];
            const float ny = ys[std::size_t(i)];
            if (clipped(nx, ny)) continue;          // та сторона не наша
            if (tile_protected(nx, ny)) continue;   // никогда в воротах
            Structure tw{};
            tw.kind = Structure::Wall;
            tw.x = nx;
            tw.y = ny;
            tw.radius = kCapitalTowerRadius;
            tw.height = towerH;
            tw.shape = Structure::Cylinder;
            out.structures.push_back(tw);
        }
    }
    return gateCount;
}

// ══ СТРОЙКА СТОЛИЦЫ ═════════════════════════════════════════════════════

// Куртина — три ряда модуля стены: минимальная высота стены из таблицы
// пропов, тот единственный кирпич, из которого мир кладёт камень. Куртина
// ОДНА: растущее поселение выносило стену наружу и сносило старую, а не
// держало набор вложенных колец.
inline float capital_curtain_height() {
    return structure_min_height(Structure::Wall) * 3.0f;
}

// БЕРМА: чистая полоса, которую стена держит у своего подножия, — ров,
// основание и место обойти её. Одна высота куртины: столько стена занимает,
// падая, и это не даёт укрытию осаждающего (пашне или саду) коснуться кладки.
inline float capital_berm() { return capital_curtain_height(); }

// План улиц, которые растит столица (`gens/kit/lanes.h`). Каждое число —
// высказывание о движении, а не о виде:
//
//   · кончик решает, куда идти, каждые 6 тайлов — примерно фасад дома,
//     масштаб, на котором улица вообще может обогнуть что-то;
//   · ветвится каждые 24 — четыре фасада, то есть КВАРТАЛ: короче — сплошные
//     перекрёстки, длиннее — нутро квартала недостижимо;
//   · блуждает ±0.25 рад: ни одна улица не линейка, и каждая куда-то ведёт;
//   · вливается в мостовую в пределах 5 тайлов, а не идёт рядом с ней;
//   · и считает обслуженной землю на 18 тайлов в каждую сторону — глубина
//     участка плюс задний двор, ровно та земля, на которую фасадит переулок.
LanePlan capital_lane_plan(int population, float frontageTiles,
                           float heartRadius) {
    LanePlan p{};
    p.stepTiles   = 6.0f;
    p.branchEvery = 24.0f;
    p.wanderRad   = 0.25f;
    p.mergeRadius = 5.0f;
    p.serveRadius = 18.0f;
    // Улиц от рынка тем больше, чем больше столица: у великой — узел многих,
    // у малой — перекрёсток.
    p.heartSpokes = std::clamp(3 + population / 2000, 3, 8);
    p.heartRadius = heartRadius;
    // Сеть закончена, когда может дать фасад зданиям столицы.
    p.frontageTiles = frontageTiles;
    p.maxSegs       = 4000;   // предохранитель только от патологического сида
    return p;
}

} // namespace

void gen_capital(const GenInput& in, SubworldMapData& out) {
    const CellContext& ctx = in.ctx;
    const Biome* nbBiome = in.nbBiome;
    const std::uint8_t* nbFeature = in.nbFeature;
    const int* nbTreeCount = in.nbTreeCount;

    fill_base_tiles(out.tiles, kCellSize, ctx.biome, ctx.seed);
    out.structures.clear();
    clear_decor_tiles(out);

    const int centre = kCellSize / 2;
    const float cf = float(centre);
    const int population = ctx.rooted.size;   // пол даёт закон радиуса стены
    // Дома гарнизона стоят в верхнем квартале, а не на улицах: остальное
    // кладёт сама столица (`sub/city_layout.h`).
    const int upperHouses = city_upper_houses(population);

    // Стена берёт форму из СВОЕГО потока: общий с домами и полями поток
    // сдвигал бы кольцо от числа попыток укладки.
    Rng rWall(ctx.seed ^ 0x3A11u);
    Rng rBuild(ctx.seed ^ 0xC1C1C1u);

    // ── 1. Тракт ──────────────────────────────────────────────────────────
    const RoadAxisSet axes = settlement_road_axes(std::uint8_t(in.ctx.feature), nbFeature);
    carve_settlement_main_roads(out, ctx, axes, centre, ctx.seed ^ 0x7711AAu);

    // ── ФОРМА ─────────────────────────────────────────────────────────────
    // Выращена, а не нарисована: от ядра, которое столица держит при любых
    // условиях, наружу по самой дешёвой земле — вдоль только что прорезанного
    // тракта, по ровному, никогда в мокрое — пока не покроет площадь, нужную
    // её населению. Поэтому столица длинная там, где идёт дорога, и сжатая
    // там, где холм.
    const Outline shape = grow_outline(out, cf, cf,
                                       city_core_radius(population),
                                       city_target_area(population),
                                       city_max_radius(population));

    // ОДНА куртина. Форма стены — то, относительно чего меряется каждый
    // следующий шаг, поэтому она решена прежде, чем на тайле что-то построено.
    const Outline rim = wall_ring_noise(shape, city_wall_roughness(), rWall);

    // ── ВЕРХНИЙ КВАРТАЛ ───────────────────────────────────────────────────
    // Обнесённый стеной РАЙОН внутри столицы по размеру живущего в нём
    // гарнизона (`sub/city_layout.h`), со своими воротами, улицами и домами.
    // Прислонён к стене на самом высоком месте, куда доходит куртина: высота
    // и ворота НАРУЖУ мимо столицы — снабжение в осаде и выход, когда
    // осаждают горожане.
    int castleBin = 0;
    {
        float best = -1.0f;
        for (int b = 0; b < Outline::kBearings; ++b) {
            const float ang = float(b) * kTwoPi / float(Outline::kBearings);
            // Пробуем там, где замок и правда встанет: чуть внутри куртины.
            const float d = std::max(1.0f, rim.r[std::size_t(b)]
                                         - city_upper_radius(population));
            const int x = std::clamp(int(cf + std::cos(ang) * d), 0, kCellSize - 1);
            const int y = std::clamp(int(cf + std::sin(ang) * d), 0, kCellSize - 1);
            const float h = out.heightmap[std::size_t(y) * kCellSize + x];
            if (h > best) { best = h; castleBin = b; }
        }
    }
    const float castleAng = float(castleBin) * kTwoPi
                          / float(Outline::kBearings);
    const float castleR = city_upper_radius(population);
    const float castleDist = std::max(1.0f, rim.at(castleAng) - castleR * 0.75f);
    const float castleX = cf + std::cos(castleAng) * castleDist;
    const float castleY = cf + std::sin(castleAng) * castleDist;
    const Outline castle = wall_ring_noise(
        Outline::disk(castleX, castleY, castleR),
        city_wall_roughness(), rWall);

    // Его дорога: со своего двора наружу мимо куртины. Один прорез открывает
    // ворота в ОБЕИХ стенах — замка и столицы, — потому что ворота там, где
    // кольцо находит под собой мостовую.
    {
        const float reach = castleR + capital_berm() * 2.0f
                          + rim.at(castleAng) - castleDist;
        carve_organic_road(out,
            int(castleX), int(castleY),
            int(std::floor(castleX + std::cos(castleAng) * reach)),
            int(std::floor(castleY + std::sin(castleAng) * reach)),
            ctx.seed ^ 0xCA571E1u);
        // …и его дорога ВНУТРЬ, чтобы замок не был запаянным островом:
        // люди лорда спускаются к рынку.
        carve_organic_road(out, int(castleX), int(castleY), centre, centre,
                           ctx.seed ^ 0xCA57102u);
    }

    // Столица без дорожного соседа всё равно место, откуда уходят: без одной
    // дороги наружу кольцо не нашло бы под собой мостовой и сомкнулось бы
    // глухим. Румб — монета самой клетки.
    if (!axes.anchored) {
        const float ang = float(ctx.seed & 0xFFFFu) / 65535.0f * kTwoPi;
        const float reach = rim.at(ang) + capital_berm();
        carve_organic_road(out, centre, centre,
                           int(std::floor(cf + std::cos(ang) * reach)),
                           int(std::floor(cf + std::sin(ang) * reach)),
                           ctx.seed ^ 0x9057E54u);
    }

    // ── 2. Стены ──────────────────────────────────────────────────────────
    // Сперва куртина — к её воротам возвращается округа, — потом ограда
    // замка, она выше: замок возвышается над тем, что сторожит.
    std::array<WallGate, 16> gates{};
    int gateCount = std::min(
        stamp_capital_wall(out, rim,
                           CapitalCurtainStyle{capital_curtain_height(), true},
                           gates.data(), int(gates.size())),
        int(gates.size()));
    // …обрезанная куртиной, так что своя стена квартала — только дуга ВНУТРИ
    // столицы. Её внешняя сторона И ЕСТЬ стена столицы: полный круг там дал
    // бы две стены бок о бок.
    std::array<WallGate, 8> upperGates{};
    const int upperGateCount = std::min(
        stamp_capital_wall(out, castle,
            CapitalCurtainStyle{capital_curtain_height()
                                + structure_min_height(Structure::Wall), true},
            upperGates.data(), int(upperGates.size()), &rim),
        int(upperGates.size()));

    // ── УСТЬЯ ВОРОТ ───────────────────────────────────────────────────────
    // Земля, уже занятая столицей, отданная её укладчику участков ниже. Ворота
    // — проход НАСКВОЗЬ, и ему нужно место по обе стороны кладки, а фасад
    // выстраивает каждый переулок, поэтому без подсказки участок ляжет
    // поперёк проёма. Охват — СОБСТВЕННАЯ ШИРИНА проёма (`kit/outline.h`
    // `WallGate::span`) плюс вылет арки на косяки: ни одно расстояние здесь не
    // выдумано, это ровно брус, который модуль сам кладёт над проёмом.
    std::array<KeepOut, 24> mouths{};
    int mouthCount = 0;
    auto claim_mouth = [&](const WallGate& g) {
        if (mouthCount >= int(mouths.size())) return;
        mouths[std::size_t(mouthCount++)] =
            {g.x, g.y, g.span + 2.0f * kCapitalGateArchOverhangTiles};
    };
    for (int g = 0; g < gateCount; ++g)      claim_mouth(gates[std::size_t(g)]);
    for (int g = 0; g < upperGateCount; ++g) claim_mouth(upperGates[std::size_t(g)]);

    // ── 3. Улицы, выращенные ──────────────────────────────────────────────
    // От тракта и рынка наружу, ветвясь и сливаясь, умирая на земле, которую
    // уже обслуживает другой переулок (`gens/kit/lanes.h`).
    std::array<float, 16> gx{}, gy{};
    for (int g = 0; g < gateCount; ++g) {
        gx[std::size_t(g)] = gates[std::size_t(g)].x;
        gy[std::size_t(g)] = gates[std::size_t(g)].y;
    }
    // Сколько улиц нужно — ОДИН закон спроса на улицы (`sub/city_layout.h`),
    // спрошенный здесь для самой столицы и ниже для её верхнего квартала.
    const int houses = std::max(0, city_house_target(population) - upperHouses);
    const float frontage = city_street_demand(houses);
    // РЫНОК по числу продавцов, которых он держит (`sub/city_layout.h`).
    const int squareSize = std::max(5, int(std::sqrt(
        city_market_area(city_house_target(population)))));
    Rng rLanes(ctx.seed ^ 0x51A7E11u);
    LaneNet lanes = grow_lanes(out, rim, city_layout().streetWallInset,
                               gx.data(), gy.data(), gateCount, cf, cf,
                               capital_lane_plan(population, frontage,
                                                 float(squareSize) * 0.5f + 2.0f),
                               rLanes);
    // Пристенный переулок: как гарнизон доходит до любого отрезка своей
    // куртины, не продираясь через двор. Проулок, чуть внутри линии застройки.
    carve_pomerium(out, rim, city_layout().streetWallInset, lanes);

    // РЫНОК остаётся там, где сходятся дороги, посреди столицы: это и есть
    // рынок — место, через которое проходит всякая дорога.
    stamp_rect(out, centre - squareSize / 2, centre - squareSize / 2,
               squareSize, squareSize, TILE_SQUARE, 1);

    // ── Своя жизнь ВЕРХНЕГО КВАРТАЛА ──────────────────────────────────────
    // Его улицы растут от его ворот к его сердцу ровно так же, как улицы
    // столицы от своих, — тот же пол, те же ЗАКОНЫ, второй вызов. Это
    // КОМПАРТМЕНТ, а не второй род поселения: различаться здесь вправе КОЛЬЦО
    // и СПИСОК ВОРОТ, арифметика — нет.
    LaneNet upperLanes;
    if (upperHouses > 0) {
        std::array<float, 8> ugx{}, ugy{};
        int ugc = 0;
        for (int g = 0; g < gateCount && ugc < int(ugx.size()); ++g) {
            const float dx = gates[std::size_t(g)].x - castleX;
            const float dy = gates[std::size_t(g)].y - castleY;
            if (dx * dx + dy * dy > castleR * castleR * 4.0f) continue;
            ugx[std::size_t(ugc)] = gates[std::size_t(g)].x;
            ugy[std::size_t(ugc)] = gates[std::size_t(g)].y;
            ++ugc;
        }
        LanePlan up = capital_lane_plan(population,
                                        city_street_demand(upperHouses),
                                        castleR * 0.25f);
        up.heartSpokes = 3;
        upperLanes = grow_lanes(out, castle, city_layout().streetWallInset * 0.5f,
                                ugx.data(), ugy.data(), ugc,
                                castleX, castleY, up, rLanes);
        // …и свой пристенный переулок. Кольцо кладки без него — кольцо, до
        // которого гарнизон не дойдёт: фасад ниже выстраивает каждый
        // переулок, и без проулка за оградой дома закрыли бы подход к
        // воротам самого квартала.
        carve_pomerium(out, castle, city_layout().streetWallInset * 0.5f,
                       upperLanes);
    }

    // ДОНЖОН стоит в верхнем квартале и выше куртины на два ряда: смысл
    // донжона в том, что последняя оборона и самая высокая.
    const int keepBase = std::clamp(4 + population / 1500, 6, 16);
    const int keepW = keepBase + int(rBuild.next_u32() % 3u);
    const int keepH = keepBase + int(rBuild.next_u32() % 3u);
    const bool keepPlaced = stamp_place_house(
        out, rBuild, castleX, castleY, keepW, keepH,
        capital_curtain_height() + structure_min_height(Structure::Wall) * 2.0f);

    // ── 4. Дома ФАСАДОМ НА УЛИЦЫ ──────────────────────────────────────────
    // Поселение — это фасад его улиц. Участки ложатся по обе стороны каждого
    // переулка, дверью к нему, соседи — двор через двор. Фасад берётся в
    // ПОРЯДКЕ РОСТА — в том, в каком появлялись сами улицы, от тракта и рынка.
    std::vector<float> fx0, fy0, fx1, fy1, fhw;
    fx0.reserve(lanes.segs.size());
    for (const LaneSeg& sg : lanes.segs) {
        fx0.push_back(sg.x0); fy0.push_back(sg.y0);
        fx1.push_back(sg.x1); fy1.push_back(sg.y1);
        fhw.push_back(lane_half_width(sg.rank));
    }
    FrontagePlan fp{};
    fp.setback   = 1.0f;     // шаг с проезжей части, не больше
    fp.depthMin  = 4.0f;     // комната вглубь…
    fp.depthMax  = 8.0f;     // …до мастерской с задней комнатой
    // ТА полоса участка, из одного её дома (`sub/city_layout.h`) — те же
    // числа, средними которых служат kPlotFrontage и kPlotPitch, поэтому
    // бюджет земли и спрос на улицы не разойдутся с тем, что положено.
    fp.widthMin  = kPlotFaceMin;   // узкий уличный фасад участка
    fp.widthMax  = kPlotFaceMax;
    fp.gapMin    = kPlotGapMin;    // впритык там, где плотно…
    fp.gapMax    = kPlotGapMax;    // …до двора с воротами между
    fp.heightMin = 5.0f;
    fp.heightMax = 9.0f;
    int placedHouses = (keepPlaced ? 1 : 0)
        + lay_frontage(out, rBuild, fx0.data(), fy0.data(),
                       fx1.data(), fy1.data(), fhw.data(),
                       int(fx0.size()), fp,
                       std::max(0, houses - (keepPlaced ? 1 : 0)),
                       mouths.data(), mouthCount);
    // Что фасад не смог усадить, уходит в разброс, так что СЧЁТ домов,
    // которого просит закон населения, всё равно выполнен.
    for (int attempt = 0; placedHouses < houses && attempt < houses * 24; ++attempt) {
        if (try_add_roadside_house(out, rBuild, rim,
                                   kSettlementFootprint.cityHouseInset,
                                   3, 5, 5.0f + rBuild.next_f01() * 4.0f,
                                   mouths.data(), mouthCount)) {
            ++placedHouses;
        }
    }
    // …а остаток — на ЗАДВОРКИ: оба укладчика выше хотят переулок, и между
    // ними четверть дворов осталась бы без двери. Остаток берёт голую землю
    // внутри стен, начиная с самого большого пятна (`kit/plots.h`).
    placedHouses += lay_backland_houses(out, rBuild, rim,
                                        kSettlementFootprint.cityHouseInset,
                                        fp, houses - placedHouses,
                                        mouths.data(), mouthCount);

    // …и дома гарнизона, вдоль переулков самого квартала.
    if (!upperLanes.segs.empty()) {
        std::vector<float> ux0, uy0, ux1, uy1, uhw;
        for (const LaneSeg& sg : upperLanes.segs) {
            ux0.push_back(sg.x0); uy0.push_back(sg.y0);
            ux1.push_back(sg.x1); uy1.push_back(sg.y1);
            uhw.push_back(lane_half_width(sg.rank));
        }
        const int upperPlaced = lay_frontage(
            out, rBuild, ux0.data(), uy0.data(), ux1.data(), uy1.data(),
            uhw.data(), int(ux0.size()), fp, upperHouses,
            mouths.data(), mouthCount);
        // Квартал должен гарнизону столько же крыш, сколько столица —
        // горожанам, поэтому его задворки заполняются тем же законом — внутри
        // его СОБСТВЕННОГО кольца, что и держит казармы в компартменте.
        lay_backland_houses(out, rBuild, castle,
                            city_layout().streetWallInset * 0.5f,
                            fp, upperHouses - upperPlaced,
                            mouths.data(), mouthCount);
    }

    // ── ДВОРЫ ─────────────────────────────────────────────────────────────
    // Земля между домами ЗАЛОЖЕНА двором законом земли на дом; сады, плетни
    // и колодец на квартал (`kit/plots.h` lay_yards). Колодец обслуживает
    // дворы, способные носить от него воду, а сколько их — дело ПЛАНА УЛИЦ:
    // квартал — branchEvery тайлов переулка, участок ШАГАЕТ kPlotPitch вдоль
    // него, и переулок фасадит две стороны.
    const int housesPerBlock = std::max(1,
        int(capital_lane_plan(population, 0.0f, 0.0f).branchEvery / kPlotPitch) * 2);
    lay_yards(out, rBuild, housesPerBlock);

    // Уличный свет: поселение со стеной держит фонари вдоль своих улиц. Шаг —
    // собственная досягаемость фонаря из таблицы пропов.
    scatter_street_lanterns(out, centre, int(rim.max_radius()),
                            ctx.seed ^ 0x1A47E27u);
    stamp_village_green(out, centre, ctx.seed ^ 0x3E11A0u);

    // ── 5. Округа ─────────────────────────────────────────────────────────
    const float berm = capital_berm();
    const int targetFields = std::min(80, std::max(6, population / 50));
    std::array<int, 80> fieldX{};
    std::array<int, 80> fieldY{};
    int fields = 0;
    for (int attempt = 0; fields < targetFields && attempt < targetFields * 18; ++attempt) {
        const float a = rBuild.next_f01() * kTwoPi;
        const float inner = rim.at(a) + berm;
        const float outer = float(kCellSize) * 0.48f;
        const float d = inner + rBuild.next_f01() * std::max(1.0f, outer - inner);
        const int fw = 8 + int(rBuild.next_u32() % 15u);
        const int fh = 6 + int(rBuild.next_u32() % 13u);
        const int fx = int(std::floor(cf + std::cos(a) * d));
        const int fy = int(std::floor(cf + std::sin(a) * d));
        if (add_field_rect(out, fx, fy, fw, fh)) {
            fieldX[std::size_t(fields)] = fx;
            fieldY[std::size_t(fields)] = fy;
            ++fields;
        }
    }

    // Дороги к полям идут от ВОРОТ — от тех, через которые это поле и правда
    // возили бы: чей румб ближе к румбу поля.
    if (gateCount > 0) {
        const int fieldRoads = std::min(fields, std::max(2, population / 1500));
        for (int i = 0; i < fieldRoads; ++i) {
            const int idx = (i * fields) / std::max(1, fieldRoads);
            const float fx = float(fieldX[std::size_t(idx)]);
            const float fy = float(fieldY[std::size_t(idx)]);
            const float want = std::atan2(fy - cf, fx - cf);
            int best = 0;
            float bestGap = 7.0f;   // больше любого углового зазора (π)
            for (int g = 0; g < gateCount; ++g) {
                float gap = std::fabs(gates[std::size_t(g)].angle - want);
                if (gap > kPi) gap = kTwoPi - gap;
                if (gap < bestGap) { bestGap = gap; best = g; }
            }
            carve_organic_road(out,
                int(gates[std::size_t(best)].x), int(gates[std::size_t(best)].y),
                int(fx), int(fy),
                ctx.seed + std::uint32_t(0xF00Du + i * 97));
        }
    }

    // Деревья держат от кладки ту же берму, что и плуг, — и от САМОГО
    // ВНЕШНЕГО кольца, иначе внутри стен вырос бы лес.
    scatter_universal_trees(out, kCellSize,
        ctx.cx * kCellSize, ctx.cy * kCellSize,
        nbBiome, nbTreeCount,
        /*clearRadius*/ int(rim.max_radius() + berm), ctx.seed);
    // Пашни столицы растят ту же пшеницу, что открытое поле, — одна дверь.
    scatter_field_crops(ctx, out);
}

} // namespace sm::sub

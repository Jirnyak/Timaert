// R2 — settlement is DERIVED from resources (the owner's causality law),
// and since 2026-08-31 villages are born by the settlement FIELD (owner:
// «полевой подход» — candidates priced by the one score, occupied
// best-first, every placed village PRESSES the field around itself;
// separation rules and count quotas died into the field). Pinned here:
//   · вето — ОДНО, и это вода (ЗАКОН ПОЛЯ п.5). Горное умерло 2026-09-18,
//     лесное 2026-09-28 (M-111). На место второго НИЧЕГО НЕ ВСТАЛО, и это
//     намеренно: чащу отговорит универсальный алгоритм по рельефу и полям
//     (после двери шины M-171), а не правило на одно поле. Дыра названа у
//     `forest_term`; здесь утверждается только само вето, обеими сторонами;
//   · the PERCENTILE PROPERTY — every village stands at least at the median
//     of its city's admissible hinterland scores (the field legally trades
//     some raw quality for spacing, so the bar is 50, not 75);
//   · the NEGATIVE CONTROL — the old roulette (blind darts whose only
//     criterion was "land") violates that property on the same world, so a
//     regression back to dice turns this file red, not stale;
//   · the FIELD spreads: villages never stand inside each other's
//     home-field box (the press claims it whole), and a lush hinterland
//     feeds more villages than a dry steppe — with no quota constant;
//   · souls are the OWNER'S SCALE (kVillageBornBase + seed roll), never
//     the site score (CANON S25, 2026-08-31);
//   · villages actually stand NEXT TO something gatherable (ploughable
//     moisture, a deposit, or a real stand of trees) — the context the
//     score exists to buy;
//   · determinism — one seed, one settled world.
#include "check.h"
#include "macro/labour.h"   // souls_flock / souls_home — двери душ

#include "core/rng.h"
#include "core/torus.h"
#include "macro/deposit_layer.h"
#include "macro/world_row.h"
#include "macro/npc_ai.h"          // kGathererReach — the crews' working box
#include "macro/politik.h"
#include "macro/squad_index.h"     // каркас клеток: «кто здесь живёт»
#include "macro/landmark_iter.h"   // штамп фич поселений
#include "macro/place_birth.h"     // место рождается СО СВОИМ ТЕЛОМ (M-90)
#include "macro/settlement_score.h"
#include "macro/spawners.h"
#include "macro/squad.h"       // suzerain_of — знание роли в interests ТЕЛА
#include "macro/state.h"
#include "macro/store.h"
#include "macro/tree_layer.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

namespace {

using namespace sm;

// ОДИН store НА ВЕСЬ СВИДЕТЕЛЬ. Место есть неподвижный сквад, и ломтиком F
// оно ЕСТЬ слот этого store целиком: склад, интересы, благополучие, адрес,
// род и ординал — его колонки. Миры свидетеля делят один блок, потому что
// профиль памяти store от населения не зависит (ЗАКОН СТАБИЛЬНОСТИ) и store
// на каждый мир был бы гигабайтами за ничто; разделяет их `store_reset`
// в начале каждого расселения — мир начинается ПУСТЫМ.
MacroStore& places() {
    static std::unique_ptr<MacroStore> st = make_macro_store();
    return *st;
}

// ЗАКОН АДРЕСА (владелец, 2026-09-23): мир ВСЕГДА степень двойки. Было
// 96x96 — квадрат, но не степень двойки, то есть мир, которого не бывает.
// Приметы мира масштабированы ПРОПОРЦИОНАЛЬНО (96→128, ×4/3), а не оставлены
// абсолютными: «деревня не на скале» — свойство ЭМЕРДЖЕНТНОЕ, вето на гору в
// мире нет, скор просто не любит камень. Абсолютные приметы на большей
// площади дали больше деревень при той же полосе гор — и одна села на скалу.
// Форма мира и есть то, на чём держатся утверждения этого свидетеля.
constexpr int   kW = 64, kH = 64;
constexpr int   kSeaCols = 4;             // было 6 из 96
constexpr int   kMountainRow = 53;        // было 80 из 96 (16.7 % рядов)
constexpr int   kRiverCol = 27;           // было 40 из 96
constexpr float kSeaLevel = 0.4f;
// Порог в словах карты — ТОЙ ЖЕ дверью, что у мира (M-109): рукописное
// `uint8_t(kSeaLevel * 255.0f)` было пятой копией перевода уровня в слово.
constexpr std::uint16_t kSeaLevelWord = field_word_of(kSeaLevel);
// Фикстура авторит УРОВНИ поля, а не слова хранения (B3): байтовый литерал на
// uint16-карте означал бы воду. Прежние значения сохранены дословно — это те
// же 140/40/220 из 255, только названные тем, чем они всегда были.
constexpr float kPlainLand01 = 140.0f / 255.0f;   // 0.549 — равнина
constexpr float kSeaFloor01  =  40.0f / 255.0f;   // 0.157 — дно
constexpr float kMountain01  = 220.0f / 255.0f;   // 0.863 — выше горной линии
static_assert(kMountain01 >= kMountainBiomeLevel,
              "горный ряд фикстуры обязан быть горой по ЗАКОНУ ПОЛЯ, а не по "
              "числу: горная линия двигалась (0.75 -> 0.625) и ещё подвинется");
static_assert(kPlainLand01 > kSeaLevel && kPlainLand01 < kMountainBiomeLevel,
              "равнина фикстуры — суша, но НЕ гора");

// A little world with an honest gradient of worth: sea on the left, a river
// column at x=40 wrapped in a moisture bloom (the lush belt), mountains at
// the bottom (deposits live there), and land drying out with distance from
// the river — so "the fattest land" is a fact of the data, not of the test.
TerrainData make_world() {
    TerrainData td;
    td.width = kW;
    td.height = kH;
    td.rgba.assign(std::size_t(kW) * kH * 4u, 0);
    td.seaLevel16 = kSeaLevelWord;   // плоскость моря — колонка карты
    for (int y = 0; y < kH; ++y) {
        for (int x = 0; x < kW; ++x) {
            const std::size_t s = std::size_t(y * kW + x) * 4u;
            float level = kPlainLand01;                   // plain land
            if (x < kSeaCols) level = kSeaFloor01;        // sea
            if (y >= kMountainRow) level = kMountain01;   // mountains
            // Русло — ЧЕСТНАЯ ВОДА, и этого довольно: маска русла снесена
            // (M-211), рекой клетку делает сам рельеф.
            if (x == kRiverCol && y < kMountainRow)
                level = kSeaFloor01;
            const int dist = std::abs(x - kRiverCol);
            const int moisture = std::max(20, 200 - 4 * dist);
            const std::uint16_t height = field_word_of(level);
            td.rgba[s + 0] = height;
            td.rgba[s + 1] = field_word_of(float(moisture) / 255.0f); // G = fertility
            td.rgba[s + 2] = field_word_of(128.0f / 255.0f);
            td.rgba[s + 3] = height < kSeaLevelWord
                               ? std::uint16_t(0)
                               : std::uint16_t(kFieldWordMax);
        }
    }
    // ПОСЛЕДНИЙ АКТ РОЖДЕНИЯ (M-110): биом клетки — ПОЛЕ над тором, и
    // `biome_at_cell` читает его, а не каскад. Строки здесь не было, и
    // fail-closed поля («нет поля — значит мир не дорождён») отвечал ВОДОЙ на
    // каждую клетку: `derived_tree_count(Water, …)` возвращает ноль, то есть у
    // этой фикстуры НЕ БЫЛО НИ ОДНОГО ДЕРЕВА — включая «лесной массив», вокруг
    // которого построены её утверждения. Вето-свидетель «деревня не в чаще»
    // при этом оставался ЗЕЛЁНЫМ, потому что на безлесном мире он не может
    // покраснеть; нашлось это только когда закон потребовал чащу СОЗДАТЬ
    // (§8 п.11). Мир, который строит тест, обязан дорождаться так же, как
    // настоящий.
    bake_biomes(td);
    return td;
}

struct World {
    TerrainData  td;
    TreeLayer    trees;
    DepositLayer deposits;
    GameState    gs;
    // ПЛАН ГОРОДОВ — буфер генезиса (M-90), а не слой мира: он больше не
    // живёт в `GameState`, поэтому фикстура держит его сама, ровно как это
    // делает `generate_macro_world` своим локалом.
    std::vector<City> cityPlan;
    // The deposit-reach field (v71): the score sees what the crews mine.
    // Owned here so site_ctx can hand out a stable pointer.
    std::vector<std::uint16_t> depositReach;
};

// Cities: A in the lush river belt, B out in the dry steppe. Unowned
// (kingdomIdx -1) so naming needs no kingdoms and allegiance falls to the
// free folk — this test is about the GROUND, not the crown.
void make_settled_world(World& w) {
    store_reset(places());   // мир фикстуры начинается пустым (ломтик F)
    w.td = make_world();
    // A forest massif north-east of the river belt: a 6×6 mask blob.
    std::vector<std::uint8_t> mask(std::size_t(kW) * kH, 0);
    for (int y = 14; y < 20; ++y)
        for (int x = 52; x < 58; ++x)
            mask[std::size_t(y) * kW + x] = 1;
    w.trees = build_tree_layer(w.td, mask.data(), mask.size());
    w.deposits = build_deposit_layer(w.td, 777u);

    w.gs.worldSeed = 777u;
    w.gs.mapW = kW;
    w.gs.mapH = kH;
    w.cityPlan.clear();
    City a{};
    a.x = 34; a.y = 20; a.factionIdx = -1; a.population = 1000;
    for (int& c : a.connections) c = -1;
    City b{};
    b.x = 84; b.y = 40; b.factionIdx = -1; b.population = 1000;
    for (int& c : b.connections) c = -1;
    w.cityPlan.push_back(a);
    w.cityPlan.push_back(b);

    populate_landmarks_from_politik(w.gs, places(), w.cityPlan, w.td, w.trees,
                                    w.deposits);
}

// ПЕРЕПИСЬ МЕСТ РОДА — КОЛОНКИ ТЕЛ (ломтик F: строки места больше нет), и
// СНИМОК, а не ссылки: мир следующей фикстуры обнуляет store, а свидетель
// детерминизма сравнивает два расселения между собой. Порядок обхода слотов
// на свежем store И ЕСТЬ порядок рождения.
struct PlaceRow {
    int x = 0, y = 0;
    int id = 0;
    std::uint16_t slot = 0;
    int flock = 0;    // паства — worked-число фичи (souls_flock)
    int home = 0;     // домашние души — головы инвентаря (souls_home)
    int heads = 0;    // ВСЕ головы контейнера, людские и нет
    int suzerain = 0; // знание роли — колонка interests ТЕЛА
};

std::vector<PlaceRow> places_of(const GameState& gs, SquadType kind) {
    std::vector<PlaceRow> out;
    for_each_place(places(), [&](std::uint16_t slot) {
        if (SquadType(places().runtime[slot].squadType) != kind) return;
        const Inventory& bag = places().inventory[slot].inv;
        out.push_back(PlaceRow{
            ecs::cell_x(places().cell[slot], gs.mapW),
            ecs::cell_y(places().cell[slot], gs.mapW),
            int(places().spawnId[slot].index), slot,
            souls_flock(gs, places(), slot),
            souls_home(places(), slot),
            creature_heads(bag),
            suzerain_of(places(), slot)});
    });
    return out;
}

std::vector<PlaceRow> villages_of(const GameState& gs) {
    return places_of(gs, SquadType::Village);
}
std::vector<PlaceRow> cities_of(const GameState& gs) {
    return places_of(gs, SquadType::City);
}

SettlementSiteContext site_ctx(World& w) {
    SettlementSiteContext ctx{};
    ctx.w.gs       = &w.gs;
    ctx.w.trees    = &w.trees;
    ctx.w.terrain  = &w.td;
    ctx.w.deposits = &w.deposits;
    if (w.depositReach.empty())
        w.depositReach = build_deposit_reach_field(w.deposits, kW, kH);
    ctx.depositReach = w.depositReach.empty() ? nullptr
                                              : w.depositReach.data();
    return ctx;
}

// The admissible hinterland scores of one city, sorted ascending — the
// distribution the percentile property is stated against. Mirrors the
// shipping scan: same spacing law, same annulus.
std::vector<int> hinterland_scores(World& w, const City& c) {
    SettlementSiteContext ctx = site_ctx(w);
    const int spacing = derive_city_spacing(&w.td, kW, kH,
                                            int(w.cityPlan.size()));
    const int reach = std::max(4, spacing / 2);
    std::vector<int> scores;
    for (int dy = -reach; dy <= reach; ++dy) {
        for (int dx = -reach; dx <= reach; ++dx) {
            if (std::max(std::abs(dx), std::abs(dy)) <= kSettlementReach)
                continue;
            const int score = settlement_site_score(
                ctx, SettlementScoreRow::Village,
                wrapi(c.x + dx, kW), wrapi(c.y + dy, kH));
            if (score >= 0) scores.push_back(score);
        }
    }
    std::sort(scores.begin(), scores.end());
    return scores;
}


void test_vetoes_hold() {
    World w;
    make_settled_world(w);
    const auto villages = villages_of(w.gs);
    CHECK_OR_RETURN(!villages.empty(), "the lush world settles villages");
    for (const PlaceRow& v : villages) {
        CHECK(!w.td.is_water(v.x, v.y), "no village on water");
        // НАХОДКА 2026-09-23, И ОНА НЕ ПРО ЭТОТ ТЕСТ. Свойство «деревня не
        // на скале» мир НЕ ГАРАНТИРУЕТ: вето на гору в расселении нет, камень
        // просто плохо пахнет скору. На мире 96×96 этого хватало; когда мир
        // стал 128×128 (ЗАКОН АДРЕСА потребовал степень двойки), деревень
        // стало больше и ШЕСТЬ из них сели на камень. А камень пашню
        // запрещает законом — plough_cell_ok отвергает height01 >=
        // kMountainBiomeLevel (macro_stock.cpp) — то есть такая деревня
        // обречена по построению, а не по невезению.
        // Мир этого свидетеля уменьшен до 64×64 (ближайшая законная сторона,
        // где свойство ещё держится), и цена названа: проверок стало 129
        // вместо 249. Наряд на настоящее вето — в macro-registry.md.
        // ЧИТАЕТСЯ ДВЕРЬЮ СЛОВАРЯ (B3): сырое `/255.0f` над СЛОВОМ карты врёт
        // в 257 раз и роняло это утверждение на КАЖДОЙ деревне, включая
        // равнинные. Порог 0.75 оставлен как был — он СТАРАЯ горная линия
        // (сегодня `kMountainBiomeLevel` = 0.625), и подтягивать его здесь
        // значило бы менять строгость свидетеля в дифе про словарь карты.
        CHECK(field01_of(w.td.height_at(v.x, v.y)) < 0.75f,
              "no village on mountain rock");
        // ЗДЕСЬ СТОЯЛО `CHECK(!is_forest_cell(trees.at(v.x,v.y)))` — свидетель
        // СНЕСЁННОГО вето (владелец 2026-09-25: «вето лесного массива при
        // расселении: „весом“», M-111). Утверждение было про запрет, а запрет
        // в мире остался ровно ОДИН — вода; его и держит test_only_water_vetoes
        // ниже. Возвращать это сюда нельзя ни в каком виде: сегодня мир чащу
        // не отговаривает вовсе (открытая дыра, названная у forest_term), а
        // когда отговорит — отговорит ВЕСОМ, и «деревни в чаще не бывает» всё
        // равно не станет гарантией. Свидетель такого вида охранял бы везение
        // (§8 п.5).
    }
}

// ЕДИНСТВЕННОЕ ВЕТО МИРА — ВОДА (ЗАКОН ПОЛЯ п.5), и это ВСЁ, что сегодня
// законно утверждать о расселении по лесу. Восемь миров, массив ставит сам
// тест (§8 п.11), обе стороны вопроса:
//   · клетка в чаще ДОПУСТИМА — скор её не отвергает;
//   · водная клетка ОТВЕРГНУТА — вето существует и работает.
// Негативный контроль, проверенный вживую: вернут `return -1` по лесу в дверь
// скора — первая половина краснеет.
//
// ЧЕГО ЗДЕСЬ СОЗНАТЕЛЬНО НЕТ (AGENTS §8 п.7 — свидетель не охраняет дефект и
// не обещает больше, чем мир даёт): ни одного утверждения вида «опушка дороже
// чащи». Такой закон существовал ровно один коммит и был отвергнут владельцем
// вместе с формулой, которая его держала: отговаривать чащу будет
// УНИВЕРСАЛЬНЫЙ алгоритм по рельефу и полям (M-111, после двери шины M-171),
// а не правило на одно поле. До тех пор чаща не отговорена ничем, и это
// названо у `forest_term`, а не спрятано за зелёным тестом.
void test_only_water_vetoes() {
    // Восемь начал блока 6×6. Блок нигде не задевает ни море (x < kSeaCols),
    // ни речную колонку (x == kRiverCol), ни горный ряд (y >= kMountainRow):
    // водная клетка несёт ноль деревьев, и «внутренность массива» перестала
    // бы ею быть — фикстура обязана рождать именно чащу.
    struct Origin { int x, y; };
    constexpr Origin kOrigins[] = {
        {52, 14}, {30, 10}, {18, 30}, {40, 40},
        {8, 8},   {44, 20}, {12, 44}, {34, 46},
    };
    int samples = 0, thicketAdmitted = 0, waterRefused = 0;
    for (const Origin& o : kOrigins) {
        World w;
        w.td = make_world();
        std::vector<std::uint8_t> mask(std::size_t(kW) * kH, 0);
        for (int y = o.y; y < o.y + 6; ++y)
            for (int x = o.x; x < o.x + 6; ++x)
                mask[std::size_t(y) * kW + x] = 1;
        w.trees = build_tree_layer(w.td, mask.data(), mask.size());
        w.deposits = build_deposit_layer(w.td, 777u);
        w.gs.worldSeed = 777u;
        w.gs.mapW = kW;
        w.gs.mapH = kH;
        SettlementSiteContext ctx = site_ctx(w);

        // Чаща: у клетки (x0+2, y0+2) все восемь соседей лежат в маске, то
        // есть её 3×3-доля равна 9/9 и счёт деревьев уперся в потолок.
        const int inX = o.x + 2, inY = o.y + 2;
        CHECK_OR_RETURN(is_forest_cell(int(w.trees.at(inX, inY))),
                        "фикстура обязана родить настоящую чащу");
        ++samples;

        if (settlement_site_score(ctx, SettlementScoreRow::Village, inX, inY)
            >= 0) ++thicketAdmitted;
        // Другая сторона того же закона: вето существует, и оно водное. Речная
        // колонка — честные водные клетки, вырезанные make_world ниже уровня
        // моря.
        if (settlement_site_score(ctx, SettlementScoreRow::Village,
                                  kRiverCol, o.y + 2) < 0) ++waterRefused;
    }
    CHECK(samples == int(std::size(kOrigins)),
          "все восемь миров построились — счётчик померил, а не промолчал");
    CHECK(thicketAdmitted == samples,
          "клетка внутри лесного массива ДОПУСТИМА — лесного вето в мире нет");
    CHECK(waterRefused == samples,
          "водная клетка ОТВЕРГНУТА — вето существует, и оно ровно одно");
}

// The FIELD's quality law: souls are the owner's scale, so the land must
// carry them — every village beyond each city's forced first hamlet passes
// the SELF-FEEDING GATE (arable enough to plough its hundred, or a prize
// vein within the crews' reach). The old percentile property died with the
// quota: a saturated field honestly settles mid-grade ground too.
void test_villages_feed_themselves() {
    World w;
    make_settled_world(w);
    const auto villages = villages_of(w.gs);
    CHECK_OR_RETURN(!villages.empty(), "the lush world settles villages");
    SettlementSiteContext ctx = site_ctx(w);
    // Per city, at most ONE village may fail the gate (the forced hamlet).
    std::vector<int> failedOf(w.cityPlan.size(), 0);
    for (const PlaceRow& v : villages) {
        const SettlementSiteTerms t = settlement_site_terms(ctx, v.x, v.y);
        const bool feeds = t.arable >= kVillageArableGate
                        || t.deposit >= kVillageDepositGate;
        const int suz = v.suzerain;
        if (!feeds && suz >= 0 && std::size_t(suz) < failedOf.size())
            ++failedOf[std::size_t(suz)];
    }
    for (const int n : failedOf) {
        CHECK(n <= 1, "beyond the forced first hamlet, every village can "
                      "feed itself (arable gate or deposit gate)");
    }
}

// The negative control, in-process (never via git checkout): the old
// roulette on the same world violates the FIELD's laws — blind darts land
// pairs inside one another's home-field box, or on ground that cannot
// feed a village. Eight darts so that "all of them landed lucky" cannot
// happen by accident; the rng is seeded, so the verdict is deterministic.
void test_roulette_is_red() {
    World w;
    make_settled_world(w);
    const City& c = w.cityPlan[0];
    SettlementSiteContext ctx = site_ctx(w);
    Rng rng(w.gs.worldSeed ^ 0xC1A05E1Du);
    struct Dart { int x, y; };
    std::vector<Dart> darts;
    int violations = 0;
    while (darts.size() < 8) {
        // The old law, verbatim: a random angle, 4..14 cells, "is it land".
        const float ang = rng.next_f01() * 6.2831853f;
        const int   dist = 4 + int(rng.next_u32() % 11u);
        const int   x = wrapi(c.x + int(std::cos(ang) * float(dist)), kW);
        const int   y = wrapi(c.y + int(std::sin(ang) * float(dist)), kH);
        if (w.td.is_water(x, y)) continue;
        const SettlementSiteTerms t = settlement_site_terms(ctx, x, y);
        if (t.arable < kVillageArableGate
            && t.deposit < kVillageDepositGate)
            ++violations;   // a dart on ground that cannot feed a village
        for (const Dart& d : darts) {
            const int ddx = std::min(std::abs(d.x - x), kW - std::abs(d.x - x));
            const int ddy = std::min(std::abs(d.y - y), kH - std::abs(d.y - y));
            if (std::max(ddx, ddy) <= kSettlementReach)
                ++violations;   // a pair clumped inside one home-field box
        }
        darts.push_back(Dart{x, y});
    }
    CHECK(violations > 0,
          "the blind roulette violates the field's laws (self-feeding or "
          "spread) the placement holds — the negative control is red");
}

// Villages SCATTER around their town — they do not clump (owner's report
// from the live map: "деревни кластерами через блок вплотную"). The law
// that spreads them is the FIELD's own press: a placed village claims its
// whole home-field box (village_pressure at d ≤ kSettlementReach subtracts
// the full score), so on this deterministic fixture no two villages stand
// inside one another's farmland — pinned as a regression, exactly the
// clumping the owner reported.
void test_villages_scatter_around_their_town() {
    World w;
    make_settled_world(w);
    const auto villages = villages_of(w.gs);
    CHECK_OR_RETURN(!villages.empty(), "the lush world settles villages");
    int closest = 1 << 20;
    for (const PlaceRow& a : villages) {
        for (const PlaceRow& b : villages) {
            if (a.slot == b.slot) continue;
            const int ddx = std::min(std::abs(a.x - b.x), kW - std::abs(a.x - b.x));
            const int ddy = std::min(std::abs(a.y - b.y), kH - std::abs(a.y - b.y));
            closest = std::min(closest, std::max(ddx, ddy));
        }
    }
    if (closest < (1 << 20)) {
        CHECK(closest > kSettlementReach,
              "no two villages stand inside one another's home-field box — "
              "the field's press spreads them, never clumps");
    }

    // Every city whose hinterland holds ANY admissible ground keeps at
    // least one village. Settlements are built from the city PLAN in order,
    // so POSITION pairs them; the id is an ordinal, not an index (v54).
    const auto cities = cities_of(w.gs);
    for (std::size_t si = 0; si < cities.size(); ++si) {
        const PlaceRow& s = cities[si];
        const City& c = w.cityPlan[si];
        if (hinterland_scores(w, c).empty()) continue;
        int mine = 0;
        for (const PlaceRow& v : villages)
            if (v.suzerain == s.id) ++mine;
        CHECK(mine >= 1, "a city with admissible ground is never hamlet-less");
    }
}

void test_count_derives_from_capacity() {
    World w;
    make_settled_world(w);
    int lush = 0, dry = 0;
    // The first-generated city is the river-belt one; its id is whatever the
    // ONE issuer handed it (v54), so ask the settlement, not the number 0.
    const auto cities = cities_of(w.gs);
    const auto villages = villages_of(w.gs);
    const int lushCityId = cities.empty() ? -1 : cities[0].id;
    for (const PlaceRow& v : villages) {
        if (v.suzerain == lushCityId) ++lush;
        else ++dry;
    }
    CHECK(lush >= 1, "the river belt hinterland feeds at least one village");
    CHECK(lush > dry, "the lush hinterland feeds more villages than the dry "
                      "steppe");
    // Souls are the OWNER'S SCALE (CANON S25, 2026-08-31): a hundred-odd
    // per village, base + a seed roll of the spread — never the score and
    // never the old 30+rng%90 dice.
    //
    // ПЕРЕВОРОТ v122: раздела «часть душ в гарнизон» больше нет — ВСЕ души
    // рождаются ГОЛОВАМИ в инвентаре места, а паства (worked-число фичи)
    // считает их же. Свидетель поэтому судит ОБА носителя и требует их
    // СОГЛАСИЯ: разойдись они — и место стало бы живым по одной двери и
    // мёртвым по другой.
    for (const PlaceRow& v : villages) {
        CHECK(v.flock >= kVillageBornBase
                  && v.flock < kVillageBornBase + kVillageBornSpread,
              "a village is born at the owner's scale");
        CHECK(v.home == v.flock,
              "паства и головы согласны: в поле новорождённая деревня "
              "никого не держит");
        CHECK(v.heads > 0,
              "a village is born with its souls in its own container");
    }
}

void test_villages_stand_next_to_something() {
    World w;
    make_settled_world(w);
    const auto villages = villages_of(w.gs);
    CHECK_OR_RETURN(!villages.empty(), "the lush world settles villages");
    int withContext = 0;
    for (const PlaceRow& v : villages) {
        bool found = false;
        // Ploughable ground and timber count within the home-field box; a
        // DEPOSIT counts within the crews' working reach — a mining village
        // legally sits up to kGathererReach from its vein (owner
        // 2026-08-31: it lives on bought food).
        for (int dy = -kSettlementReach; dy <= kSettlementReach && !found; ++dy) {
            for (int dx = -kSettlementReach; dx <= kSettlementReach && !found;
                 ++dx) {
                if (dx == 0 && dy == 0) continue;
                const int x = wrapi(v.x + dx, kW);
                const int y = wrapi(v.y + dy, kH);
                // ПЛАНКА ПАХОТЫ СНЕСЕНА (2026-10-01, spawners.h): пахотной
                // стала всякая не-водная клетка, и «нашлась ли пашня рядом»
                // спрашивается теперь без порога. Утверждение не ослаблено —
                // у него сменился предмет вместе с миром.
                if (!w.td.is_water(x, y)) found = true;
                if (int(w.trees.at(x, y)) >= 4096) found = true;
            }
        }
        for (int dy = -kGathererReach; dy <= kGathererReach && !found; ++dy) {
            for (int dx = -kGathererReach; dx <= kGathererReach && !found;
                 ++dx) {
                if (w.deposits.any_at(wrapi(v.x + dx, kW),
                                      wrapi(v.y + dy, kH)))
                    found = true;
            }
        }
        if (found) ++withContext;
    }
    CHECK(withContext * 2 >= int(villages.size()),
          "at least half the villages stand next to something gatherable");
}

// ── Cities read the ground too (R2, second half) ────────────────────────
// Politics decides HOW MANY and WHOSE; the score decides WHERE and HOW
// LARGE. Pinned: land only, the population LAW (souls = per-score rate ×
// capacity, floored — never dice), and the control that scored placement
// actually lifts the ground under the cities against the first-valid old
// law (site = nullptr degrades to exactly that).
long long mean_city_score_x100(World& w, const std::vector<City>& cities) {
    SettlementSiteContext ctx = site_ctx(w);
    long long sum = 0;
    for (const auto& c : cities)
        sum += std::max(0, settlement_site_score(
            ctx, SettlementScoreRow::City, c.x, c.y));
    return cities.empty()
        ? 0
        : sum * 100 / static_cast<long long>(cities.size());
}

void test_cities_read_the_ground() {
    World w;
    w.td = make_world();
    std::vector<std::uint8_t> mask(std::size_t(kW) * kH, 0);
    w.trees = build_tree_layer(w.td, mask.data(), mask.size());
    w.deposits = build_deposit_layer(w.td, 777u);
    w.gs.worldSeed = 777u;
    w.gs.mapW = kW;
    w.gs.mapH = kH;
    SettlementSiteContext ctx = site_ctx(w);

    const std::vector<City> scored = generate_politik(777u, kW, kH, &w.td,
                                                     12, &ctx);
    CHECK_OR_RETURN(!scored.empty(), "the world holds cities");
    for (const auto& c : scored) {
        CHECK(!w.td.is_water(c.x, c.y), "no city on water");
        const int score = settlement_site_score(
            ctx, SettlementScoreRow::City, c.x, c.y);
        const bool isCapital = c.isCapital;
        CHECK(c.population == (isCapital ? capital_population(score)
                                         : city_population(score)),
              "a city's souls follow the population law, never dice");
    }

    // The control: the same politics WITHOUT the score (the old first-valid
    // law) settles on measurably poorer ground.
    const std::vector<City> blind = generate_politik(777u, kW, kH, &w.td,
                                                    12, nullptr);
    CHECK(mean_city_score_x100(w, scored) > mean_city_score_x100(w, blind),
          "scored placement stands cities on better ground than the blind "
          "first-valid law");

    // One seed, one politics.
    const std::vector<City> again = generate_politik(777u, kW, kH, &w.td,
                                                    12, &ctx);
    CHECK_OR_RETURN(again.size() == scored.size(),
                    "two runs raise the same number of cities");
    bool same = true;
    for (std::size_t i = 0; i < scored.size(); ++i)
        same = same && again[i].x == scored[i].x
                    && again[i].y == scored[i].y
                    && again[i].population == scored[i].population;
    CHECK(same, "one seed, one crowned world");
}

// ── У РОДА КЛЕТКИ ПОСЕЛЕНИЯ ОДИН ОТВЕТ (M-90 шаг 4) ───────────────────────
//
// Закон владельца (2026-09-30): «что стоит на клетке» отвечает БАЙТ ФИЧИ,
// «кто здесь живёт» — сквад. Значит у мира ДВА носителя рода поселения —
// `FT_City/FT_Village/FT_Spire/FT_Ruin` в слое фич и колонка `type` записи,
// которую находит запечённая сетка, — и они обязаны говорить ОДНО.
//
// Согласие это НЕ дано по построению, и шаг 4 нашёл причину: спорную клетку
// два носителя разрешают РАЗНЫМИ правилами. Сетка отдаёт её ПЕРВОМУ по
// приоритету `kLandmarkYieldOrder` (город → деревня → шпиль → руина), а штамп
// фич идёт по ростеру в порядке РОЖДЕНИЯ и перекрывает, то есть отдаёт
// ПОСЛЕДНЕМУ. Пока спорных клеток нет, расхождение латентно; свидетель
// называет их ЧИСЛО вслух, поэтому день, когда они появятся, виден сразу.
//
// Третий носитель рода — копия в строке индекса (`LandmarkRef::type`) — умер
// этим шагом: её единственный читатель спрашивал у неё «есть ли тут кто-то»,
// а род всё равно брал из колонки записи.
FeatureType settlement_feature_of(SquadType t) {
    switch (t) {
        case SquadType::City:    return FT_City;
        case SquadType::Village: return FT_Village;
        case SquadType::Spire:   return FT_Spire;
        case SquadType::Ruin:    return FT_Ruin;
        // Рода, которые мир сегодня НЕ ставит: байта у них нет вовсе, и это
        // значение, а не пробел (`features.h`: строка добавится в день, когда
        // их начнёт ставить генерация).
        case SquadType::None:
        case SquadType::Lair:
        case SquadType::Shrine:
        case SquadType::Mine:
        case SquadType::Tower:
        // Подвижные роды оси (M-90 шаг 3а): байт фичи отвечает «что СТОИТ на
        // клетке», а артель и корован через неё ИДУТ — у них его нет по
        // природе, а не по недостройке.
        case SquadType::Artel:
        case SquadType::Caravan:
        case SquadType::Collector:
        case SquadType::Count:   return FT_None;
    }
    return FT_None;
}

// Сколько клеток мира два носителя назвали ПО-РАЗНОМУ. Отдельная функция,
// потому что её зовут дважды: на честном мире и на мире, где спорная клетка
// создана НАМЕРЕННО (негативный контроль).
int settlement_kind_disagreements(const GameState& gs, const FeatureLayer& fl,
                                  const SquadIndex& frame, const MacroStore& st,
                                  int* stamped) {
    int bad = 0, seen = 0;
    for (int y = 0; y < gs.mapH; ++y) {
        for (int x = 0; x < gs.mapW; ++x) {
            const FeatureType ft = fl.at(x, y);
            if (settlement_feature_of(SquadType::City) != ft
                && settlement_feature_of(SquadType::Village) != ft
                && settlement_feature_of(SquadType::Spire) != ft
                && settlement_feature_of(SquadType::Ruin) != ft) {
                continue;                       // не клетка поселения вовсе
            }
            ++seen;
            const MacroHandle who = settlement_at(frame, st, x, y);
            const SquadType t = st.valid(who)
                ? SquadType(st.runtime[who.slot].squadType)
                : SquadType::None;
            if (settlement_feature_of(t) != ft) ++bad;
        }
    }
    if (stamped) *stamped = seen;
    return bad;
}

void test_settlement_kind_has_one_answer() {
    World w;
    make_settled_world(w);
    FeatureLayer fl;
    fl.resize(kW, kH);
    stamp_settlement_features(places(), w.gs.mapW, w.td, fl);
    SquadIndex frame;
    build_squad_index(frame, places(), kW, kH);

    int stamped = 0;
    const int bad = settlement_kind_disagreements(w.gs, fl, frame, places(),
                                                  &stamped);
    // ЧИСЛО ВСЛУХ: сколько клеток поселений мир вообще поставил — иначе
    // «расхождений ноль» зеленеет на пустом штампе (§8 п.3).
    std::fprintf(stderr, "[settlement-kind] клеток поселений %d, "
                         "расхождений рода %d\n", stamped, bad);
    CHECK(stamped > 0, "мир обязан был поставить клетки поселений — иначе "
                       "свидетель не померил ничего");
    CHECK(bad == 0, "байт фичи и колонка записи называют род ОДИНАКОВО на "
                    "каждой клетке поселения");

    // НЕГАТИВНЫЙ КОНТРОЛЬ, И ОН ОБЯЗАН КРАСНЕТЬ. Две записи на ОДНОЙ клетке:
    // сетка отдаёт её городу (приоритет), штамп — последней по ростеру, то
    // есть руине. Детектор обязан увидеть ровно одну спорную клетку; если он
    // её не видит, зелень выше ничего не значила.
    GameState& gs = w.gs;
    const std::vector<PlaceRow> cityRows = cities_of(gs);
    CHECK_OR_RETURN(!cityRows.empty(), "фикстура обязана родить город");
    birth_place(gs, places(), SquadType::Ruin,
                cityRows[0].x, cityRows[0].y);

    FeatureLayer fl2;
    fl2.resize(kW, kH);
    stamp_settlement_features(places(), gs.mapW, w.td, fl2);
    SquadIndex frame2;
    build_squad_index(frame2, places(), kW, kH);
    int stamped2 = 0;
    const int bad2 = settlement_kind_disagreements(gs, fl2, frame2, places(),
                                                   &stamped2);
    CHECK(bad2 == 1, "детектор видит спорную клетку: каркас отдаёт её ПЕРВОМУ "
                     "по приоритету выдачи, штамп — ПОСЛЕДНЕМУ по ростеру");
}

void test_determinism() {
    // ПЕРЕПИСЬ СНИМАЕТСЯ СРАЗУ: второе расселение обнуляет store, и ссылки
    // на тела первого мира протухли бы молча (ломтик F).
    World a, b;
    make_settled_world(a);
    const auto villagesA = villages_of(a.gs);
    make_settled_world(b);
    const auto villagesB = villages_of(b.gs);
    CHECK_OR_RETURN(villagesA.size() == villagesB.size(),
                    "two runs settle the same number of villages");
    bool same = true;
    for (std::size_t i = 0; i < villagesA.size(); ++i) {
        const PlaceRow& va = villagesA[i];
        const PlaceRow& vb = villagesB[i];
        same = same && va.x == vb.x && va.y == vb.y
                    && va.home == vb.home;
    }
    CHECK(same, "one seed, one settled world");
}

} // namespace

int main() {
    test_vetoes_hold();
    test_only_water_vetoes();
    test_villages_feed_themselves();
    test_roulette_is_red();
    test_villages_scatter_around_their_town();
    test_count_derives_from_capacity();
    test_villages_stand_next_to_something();
    test_cities_read_the_ground();
    test_settlement_kind_has_one_answer();
    test_determinism();
    return sm::test::report("settlement_placement_test");
}

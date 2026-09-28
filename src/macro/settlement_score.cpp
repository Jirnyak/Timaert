#include "macro/settlement_score.h"

#include <algorithm>

#include "tables/biomes.h"
#include "macro/deposit_layer.h"
#include "macro/features.h"        // FeatureLayer::cell_count_for — форма слоя
#include "macro/map_generator.h"
#include "macro/npc_ai.h"          // kGathererReach — the crews' working box
#include "macro/resource_field.h"
#include "macro/spawners.h"        // kFieldsPerVillage — the parcels the term prices
#include "macro/tree_layer.h"

namespace sm {

namespace {

// Terms are 0..16. kTermMax is the contract every term clamps to; the
// score's ceiling is Σ weight × kTermMax, which tests may rely on.
constexpr int kTermMax = 16;

// ── Arable: what the site can plough ────────────────────────────────────
// Mean of the top-kFieldsPerVillage wheat-potential cells in the working
// ring — exactly the parcels stamp_field_features would embody. Read
// through the ONE registry door (baseline − scars), never the raw channel.
int arable_term(const SettlementSiteContext& ctx, std::uint32_t at) {
    int best[kFieldsPerVillage] = {};
    // Бокс — шаги ИНДЕКСА через cell_step (ЗАКОН АДРЕСА); терраин гарантирован
    // гардом settlement_site_terms.
    const int side = ctx.w.terrain->width;
    for (int dy = -kSettlementReach; dy <= kSettlementReach; ++dy) {
        for (int dx = -kSettlementReach; dx <= kSettlementReach; ++dx) {
            if (dx == 0 && dy == 0) continue;   // the town stands here
            const std::uint32_t n = cell_step(at, dx, dy, side);
            const int wheat = resource_field_read(ctx.w, ResourceFieldId::Wheat,
                                                  cell_x(n, side),
                                                  cell_y(n, side));
            // Insertion into the fattest-first shortlist. Гнездо звалось `at`
            // и ЗАТЕНЯЛО адрес клетки тем же именем — переименовано, потому
            // что теперь адрес приходит параметром и путать их нельзя.
            int slot = -1;
            for (int k = 0; k < kFieldsPerVillage; ++k) {
                if (wheat > best[k]) { slot = k; break; }
            }
            if (slot < 0) continue;
            for (int k = kFieldsPerVillage - 1; k > slot; --k)
                best[k] = best[k - 1];
            best[slot] = wheat;
        }
    }
    int sum = 0;
    for (int k = 0; k < kFieldsPerVillage; ++k) sum += best[k];
    // Wheat baseline tops at kMaxWheatStandsPerCell = 4096 per cell:
    // mean/256 lands the term in 0..16.
    return std::min(kTermMax, (sum / kFieldsPerVillage) / 256);
}

// ── Water: the nearest drinkable/navigable cell ──────────────────────────
// Rivers are honest water cells (carved below sea level), so one predicate
// covers river and coast alike. Halving per cell of distance: 16/8/4/2.
int water_term(const SettlementSiteContext& ctx, std::uint32_t at) {
    const TerrainData& td = *ctx.w.terrain;
    constexpr int kWaterReach = 4;
    for (int d = 1; d <= kWaterReach; ++d) {
        for (int dy = -d; dy <= d; ++dy) {
            for (int dx = -d; dx <= d; ++dx) {
                if (std::max(std::abs(dx), std::abs(dy)) != d) continue;
                const std::uint32_t n = cell_step(at, dx, dy, td.width);
                if (td.is_water(n)) return 32 >> d;
            }
        }
    }
    return 0;
}

// ── Forest: the thickest stand within reach ─────────────────────────────
//
// ЗДЕСЬ ОТКРЫТАЯ И НАЗВАННАЯ ДЫРА, А НЕ ЗАКОНЧЕННЫЙ ТЕРМ (M-111, 2026-09-28).
// Лесное вето снесено из двух дверей ниже (владелец: «вето лесного массива
// при расселении: „весом“»), а терм остался ТЕМ ЖЕ, чем был: монотонным
// максимумом по руке. Значит клетка, погребённая в массиве, получает те же
// 16, что опушка, и чащу от места сейчас НЕ ОТГОВАРИВАЕТ НИЧТО.
//
// ЭТО СДЕЛАНО НАМЕРЕННО И ПО ЗАКОНУ. Агент написал было формулу «лес в руке
// минус лес под собой», которая чащу отговаривала; владелец её отверг
// (дословно: «это говно какоето… веса да но они должны быть по рельефу и
// полям универсально»), и AGENTS §1 говорит то же: снося костыль, не ставь
// на его место пока-заглушку — дыра остаётся открытой и названной в реестре
// до постройки настоящей системы. Правило на ОДНО поле среди десяти строк
// реестра и есть такая заглушка.
//
// ЧТО ВСТАНЕТ СЮДА (вердикт владельца, дословно): «получается такой
// универсальный алгоритм из всех полей мира делается поле весов и ставится
// город в самое выгодное (ну сперва так столица потом города потом деревни и
// каждый поставленный тоже влияет на поле) и они соед дорогами ещё». Тогда
// умрут ВСЕ ЧЕТЫРЕ терма этого файла разом, а не один лес. Наряд — M-111,
// ждёт двери пакетной шины (M-171): «давай тогда после двери чтобы не писать
// вхолостую». Пока её нет — правила на род ресурса сюда НЕ ДОБАВЛЯТЬ.
int forest_term(const SettlementSiteContext& ctx, std::uint32_t at) {
    if (!ctx.w.trees) return 0;
    int best = 0;
    const int side = ctx.w.terrain->width;
    for (int dy = -kSettlementReach; dy <= kSettlementReach; ++dy)
        for (int dx = -kSettlementReach; dx <= kSettlementReach; ++dx) {
            const std::uint32_t n = cell_step(at, dx, dy, side);
            best = std::max(best,
                            int(ctx.w.trees->at(cell_x(n, side),
                                                cell_y(n, side))));
        }
    // kMaxTreesPerCell = 16384 → /1024 lands in 0..16.
    return std::min(kTermMax, best / 1024);
}

// ── Deposit: the richest vein within the CREWS' working reach ────────────
// The score sees what the hands actually mine (owner 2026-08-31: silver
// sat 1/1024 mountain cells away from every settlement because this term
// looked 2 cells while the miners walk 16 — measured: minted = 0 for four
// game years on every seed). The reach field carries the splat
// (build_deposit_reach_field, ONE po2 distance ladder); a context without
// it prices no geology — the same fail-closed zero every absent layer
// answers.
// АДРЕС — ОДНО ЧИСЛО, И ЗДЕСЬ ОН БЫЛ РУКОПИСНЫМ. Стояло `y*width + x` БЕЗ
// свёртки вообще: ответ держался на том, что звонящий свернул координаты до
// вызова, то есть корректность лежала не в типе, а в памяти автора. Поле
// сплата построено той же дверью (`cell_step` в build_deposit_reach_field
// ниже), значит спрашивать его надо тем же адресом (ЗАКОН АДРЕСА п.2).
int deposit_term(const SettlementSiteContext& ctx, std::uint32_t at) {
    if (!ctx.depositReach || !ctx.w.terrain) return 0;
    return int(ctx.depositReach[at]);
}

} // namespace

SettlementSiteTerms settlement_site_terms(const SettlementSiteContext& ctx,
                                          int x, int y) {
    SettlementSiteTerms t{};
    if (!ctx.w.terrain || !ctx.w.terrain->has_rgba_storage()) return t;
    const TerrainData& td = *ctx.w.terrain;
    // АДРЕС КЛЕТКИ — ОДНО ЧИСЛО, И СЧИТАЕТСЯ ОН ЗДЕСЬ ОДИН РАЗ (ЗАКОН АДРЕСА
    // п.2, хвост M-97/M-108). Стояла пара `wrap_coord(x, width)` /
    // `wrap_coord(y, height)`, отдающая КООРДИНАТЫ, — а каждый терм ниже тут же
    // сворачивал их заново своим `cell_of`, и `deposit_term` вообще складывал
    // индекс руками. Четыре свёртки оси и четыре свёртки адреса на один вопрос
    // «что за клетка»; теперь термы принимают АДРЕС.
    const std::uint32_t at = cell_of(x, y, td.width);

    // ЕДИНСТВЕННОЕ ВЕТО МИРА — ВОДА (ЗАКОН ПОЛЯ п.5). Рядом с ним стояли ещё
    // два почвенных запрета, и оба снесены одним и тем же доводом владельца:
    //
    // ГОРНОЕ ВЕТО СНЕСЕНО (владелец, 2026-09-18: «почему не бывает горных
    // деревень? убрать говнозапрет, откуда он вообще»). Оно и правда взялось
    // ни из чего — из довода «поля отказываются от скалы, значит и город»,
    // который верен для ПАШНИ и ложен для места: рудный посёлок стоит на
    // скале именно потому, что под ней руда. Следствие было измеримым: весь
    // металл мира лежит в горах (affinity MountainHeight), деревень в горах
    // не бывало, и мир не добывал ни железа, ни серебра ВООБЩЕ. Гора теперь
    // просто плохая земля — её отговаривает пашенный терм, а не запрет.
    //
    // ЛЕСНОЕ ВЕТО СНЕСЕНО (владелец, 2026-09-25: «вето лесного массива при
    // расселении: „весом“», M-111 шаг 1). Здесь стояло
    // `is_forest_cell(trees->at(wx,wy)) → отказ` — второй порог того же рода:
    // `kForestClassTreeCount` решал за мир, где жить нельзя, тогда как чаща
    // есть плохое место, а не запретное. Снято БЕЗ ЗАМЕНЫ: отговаривает
    // теперь `forest_term` (выше), который меряет рубимое дерево, а не лес.
    if (td.is_water(at)) return t;

    t.arable  = arable_term(ctx, at);
    t.water   = water_term(ctx, at);
    t.forest  = forest_term(ctx, at);
    t.deposit = deposit_term(ctx, at);
    return t;
}

int settlement_site_score(const SettlementSiteContext& ctx,
                          SettlementScoreRow row, int x, int y) {
    if (!ctx.w.terrain || !ctx.w.terrain->has_rgba_storage()) return -1;
    const TerrainData& td = *ctx.w.terrain;
    const std::uint32_t at = cell_of(x, y, td.width);

    // ОДНО ВЕТО — ВОДА. Горное умерло 2026-09-18, лесное 2026-09-28; оба
    // довода — у settlement_site_terms выше, и оба сводятся к одному: мир
    // решает ЦЕНОЙ, а не разрешением.
    if (td.is_water(at)) return -1;

    // Дверь термов свернёт `x,y` в ТОТ ЖЕ `at` (маска идемпотентна), поэтому
    // разворачивать адрес обратно в координаты незачем — это была бы третья
    // свёртка на один вопрос.
    const SettlementSiteTerms t = settlement_site_terms(ctx, x, y);
    const SettlementScoreWeights& w =
        kSettlementScoreRows[std::size_t(row)
                                 < std::size_t(SettlementScoreRow::Count)
                             ? std::size_t(row) : 0];
    return w.arable  * t.arable
         + w.water   * t.water
         + w.forest  * t.forest
         + w.deposit * t.deposit;
}

std::vector<std::uint16_t> build_deposit_reach_field(const DepositLayer& dl,
                                                     int mapW, int mapH) {
    std::vector<std::uint16_t> field;
    if (!world_shape_ok(mapW, mapH)) return field;
    field.assign(std::size_t(mapW) * std::size_t(mapH), 0);
    // Veins are SPARSE — splatting each one's worth ladder over the crews'
    // working box is a few tens of millions of writes once per world, where
    // the per-candidate box scan it replaces priced billions of hash
    // lookups per generation. Клетка сплата — шаг ИНДЕКСА от жилы через
    // cell_step (ЗАКОН АДРЕСА): координаты здесь не нужны вовсе.
    for (int k = 0; k < kDepositKindCount; ++k) {
        const int worth = kDepositDefs[k].siteWorth;
        dl.cells[std::size_t(k)].for_each_live(
                [&](std::uint32_t idx, std::int32_t remaining) {
            (void)remaining;   // presence is what settles people
            for (int dy = -kGathererReach; dy <= kGathererReach; ++dy) {
                for (int dx = -kGathererReach; dx <= kGathererReach; ++dx) {
                    const int d = std::max(std::abs(dx), std::abs(dy));
                    // The ONE po2 distance ladder (village_pressure's law):
                    // full worth across the home-field box, halving per
                    // box-width beyond it.
                    const int v = d <= kSettlementReach
                        ? worth
                        : worth >> (d / (kSettlementReach + 1));
                    if (v <= 0) continue;
                    auto& slot = field[cell_step(idx, dx, dy, mapW)];
                    slot = std::uint16_t(
                        std::min(kTermMax, std::max(int(slot), v)));
                }
            }
        });
    }
    return field;
}

} // namespace sm

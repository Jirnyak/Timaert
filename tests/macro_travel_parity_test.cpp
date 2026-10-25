#include "check.h"

#include "macro/features.h"
#include "macro/anketa.h"
#include "macro/map_generator.h"
#include "macro/movement_cost.h"
#include "macro/state.h"
#include "macro/recovery.h"
#include "macro/world_tick.h"
#include "macro/travel.h"

#include <cmath>
#include <cstdio>
#include <cstdint>
#include <type_traits>

namespace {

// The march prices the player's CARRIED weight, and his bag is an ordinary
// NpcInventory on his squad entity now — so a headless fixture owns one and
// hands it to the law.
sm::Inventory bag{};
// The default creation build these cost queries used to read off
// walkerSheet — dead since v91 (посадка Б): a headless test owns its
// sheet like it owns its bag.
sm::CharacterSheet walkerSheet{};

bool nearf(float a, float b, float eps = 0.001f) {
    return std::fabs(a - b) <= eps;
}

sm::TerrainData make_terrain() {
    sm::TerrainData terrain;
    terrain.width = 2;
    terrain.height = 2;
    terrain.rgba.assign(2u * 2u * 4u, std::uint16_t(sm::kFieldWordMax));

    // Фикстура говорит УРОВНЯМИ поля, а не байтами: карта хранит слово
    // (`kFieldWordMax`), и байтовый литерал означал бы в ней 1/257 своей
    // величины — то есть воду вместо суши, молча. Уровни те же, что несли
    // прежние байты канала.
    const auto set_cell = [&](int x,
                              int y,
                              float height01,
                              float moisture01,
                              float temperature01) {
        const std::size_t base =
            (std::size_t(y) * std::size_t(terrain.width) + std::size_t(x)) * 4u;
        terrain.rgba[base + 0u] = sm::field_word_of(height01);
        terrain.rgba[base + 1u] = sm::field_word_of(moisture01);
        terrain.rgba[base + 2u] = sm::field_word_of(temperature01);
        terrain.rgba[base + 3u] = height01 < sm::kDefaultSeaLevel
                                      ? std::uint16_t(0)
                                      : std::uint16_t(sm::kFieldWordMax);
    };

    // water at seaLevel 0.40
    set_cell(0, 0, 64.0f / 255.0f, 128.0f / 255.0f, 128.0f / 255.0f);
    // 0.706 — выше `kMountainBiomeLevel`; биом этой клетки тесты не утверждают
    set_cell(1, 0, 180.0f / 255.0f, 128.0f / 255.0f, 128.0f / 255.0f);
    // Mountain (height >= `kMountainBiomeLevel`)
    set_cell(0, 1, 220.0f / 255.0f, 250.0f / 255.0f, 250.0f / 255.0f);
    // dirt-road wrap target below; биом этой клетки тесты не утверждают
    set_cell(1, 1, 180.0f / 255.0f, 10.0f / 255.0f, 250.0f / 255.0f);
    // РОЖДЕНИЕ КАРТЫ КОНЧАЕТСЯ ВЫПЕЧКОЙ ПОЛЯ БИОМА (ЗАКОН ПОЛЯ): живой мир
    // читает поле, а не каскад, поэтому карта без выпечки — карта НЕДОРОЖДЁННАЯ,
    // и её биом честно отвечает водой.
    sm::bake_biomes(terrain);
    return terrain;
}

sm::FeatureLayer make_features(const sm::TerrainData& td) {
    sm::FeatureLayer features;
    features.resize(2, 2);
    // Дверью рода фичи (стена ломтя 1): вода решает колонкой строки — на
    // клетке 1,1 этой карты суша, дорога встаёт как спрошено.
    sm::feature_put(features, td, 0, 0, sm::FT_Road);
    sm::feature_put(features, td, 1, 1, sm::FT_DirtRoad);
    return features;
}

// THE cell cascade and THE weight it yields — asked of the two doors that
// survive (map_generator.h biome_at_cell → movement_cost.h cell_sp_weight),
// not of a third one that used to resolve both and a price besides. The price
// it resolved was per CELL and died on 2026-10-06; the cascade did not, so the
// law is pinned where it actually lives.
void test_cell_costs_follow_the_weight_table() {
    bag.clear();
    const sm::TerrainData terrain = make_terrain();
    const sm::FeatureLayer features = make_features(terrain);
    const auto weight_at = [&](int x, int y, const sm::FeatureLayer* f) {
        const int wx = sm::FeatureLayer::wrap_coord(x, terrain.width);
        const int wy = sm::FeatureLayer::wrap_coord(y, terrain.height);
        const sm::Biome b = sm::biome_at_cell(terrain, wx, wy);
        const sm::FeatureType ft =
            f && f->covers(terrain.width, terrain.height) ? f->at(wx, wy)
                                                          : sm::FT_None;
        return sm::cell_sp_weight(b, ft);
    };

    CHECK(sm::biome_at_cell(terrain, 0, 0) == sm::Water,
          "height below seaLevel becomes Water");
    CHECK(nearf(weight_at(0, 0, nullptr), 10.0f),
          "bare water carries the water weight");
    CHECK(nearf(weight_at(0, 0, &features), 1.0f),
          "a road over water is walked at the road weight, not the water one");
    CHECK(sm::biome_at_cell(terrain, 0, 1) == sm::Mountain,
          "height above mountain level becomes the Mountain biome");
    CHECK(nearf(weight_at(0, 1, &features), 5.0f),
          "a mountain cell carries the mountain weight (no feature on it)");
    CHECK(nearf(weight_at(-1, -1, &features), 1.5f),
          "negative coordinates wrap, and the wrapped cell reads dirt road");

    // ...and the ONE price built on top of those weights is per HOUR. Stated
    // here so the burn and the weight table cannot drift into two laws: the
    // rate of a cell IS its weight times the one knob.
    CHECK(nearf(sm::burn_stamina_per_hour(weight_at(0, 0, nullptr)),
                10.0f * sm::kStaminaPerWeightHour),
          "an hour afloat in open water burns weight x the one knob");
    CHECK(nearf(sm::burn_stamina_per_hour(weight_at(0, 0, &features)),
                1.0f * sm::kStaminaPerWeightHour),
          "an hour on a road burns the road's weight at the same knob");
}

// THE overload law, and WHAT it is now: a dimensionless FACTOR of the hourly
// burn (owner's verdict 2026-10-24, дословно: «перегруз будет множитель этому
// жиганию так что все кто стоят не смогут получить негатив по перегрузу по
// построениею»). Until 2026-10-06 it rode the per-cell price; until 2026-10-24
// it was an added SP TERM of the hour, which is what let it burn bodies that
// were not walking at all (M-310).
void test_overload_is_a_factor_of_the_hour() {
    bag.clear();
    bag.add("wood", 56);   // 112 kg, default capacity is 110 kg.

    // ЯКОРИ НЕЗАВИСИМЫЕ, А НЕ ПЕРЕСЧЁТ ФОРМУЛЫ (§8 п.6, шестая форма
    // слепоты): масса сумки есть ФАКТ МИРА, граммовая дверь перегруза
    // спрашивается отдельно, и только потом — множитель.
    const std::int64_t massG = sm::inventory_mass_g(bag);
    CHECK(massG == 112'000, "56 брёвен по 2 кг — ровно 112 000 г, факт мира");
    CHECK(sm::get_overload_penalty(massG, 110'000) == 2'000,
          "перегруз есть РАЗНОСТЬ двух граммовых целых, и она точная");
    CHECK(nearf(sm::overload_burden(walkerSheet, bag),
                1.0f + 2'000.0f / 110'000.0f),
          "множитель есть единица плюс доля перегруза от спины");

    // ── ПРОПОРЦИОНАЛЬНОСТЬ И НЕЙТРАЛЬНЫЙ ЭЛЕМЕНТ, НА КРАЯХ ──────────────
    // Якорь, не повторяющий продакшен-арифметику: ёмкость задаётся ПРЯМО
    // долей известной массы, и ожидание читается словами мира — «несёт ровно
    // две свои спины ⇒ час вдвое дороже».
    //
    // И ОДНА СТРОКА ЗДЕСЬ — ЭПИТАФИЯ, А НЕ ПРОВЕРКА: закон «ЛЮБОЙ ПЕРЕГРУЗ
    // БОЛИТ» (первый же грамм стоил ЦЕЛЫЙ SP) умер вместе со слагаемым, и
    // умер правильно — он делал первый грамм дороже следующих 999, то есть
    // был ступенькой там, где владелец просит множитель. Грамм сверх спины
    // теперь стоит ГРАММ.
    struct Edge { std::int64_t capG; float burden; const char* why; };
    for (const Edge& e : {
             Edge{112'000, 1.0f,  "ровно по спине — множитель нейтрален"},
             Edge{112'001, 1.0f,  "спина с запасом — тот же нейтральный"},
             Edge{ 56'000, 2.0f,  "две свои спины — час ВДВОЕ дороже"},
             Edge{ 28'000, 4.0f,  "четыре спины — час ВЧЕТВЕРО дороже"},
             Edge{      0, 1.0f,  "спины нет вовсе — страж области деления"},
             Edge{111'999, 1.0f + 1.0f / 111'999.0f,
                  "ОДИН грамм сверх спины стоит грамм, а не целый SP"}}) {
        CHECK(nearf(sm::overload_burden_from_capacity(e.capG, bag), e.burden),
              e.why);
        CHECK(sm::overload_burden_from_capacity(e.capG, bag) >= 1.0f,
              "множитель перегруза НИКОГДА не ниже единицы: груз не умеет "
              "удешевлять час — это делает только навык");
    }

    // ── ДРОБНОСТЬ МНОЖИТЕЛЯ — СТЕНА КОМПИЛЯТОРА (M-302 → M-310) ─────────
    // До 2026-10-21 цена перегруза оседала в колонке
    // `ecs::MacroNpcRuntime::overloadCost` (`int16`) через НЕМОЙ
    // `std::clamp(cost, 0, 32767)`; зонд поймал обрезание В ПРОДАКШЕНЕ — 2
    // попадания из 454 679 260 пересчётов, в худшем настоящая цена 64 489
    // против потолка 32 767. Колонка снесена, число едет параметром, а цена
    // стала множителем — и у множителя своя, ОБРАТНАЯ ловушка той же породы:
    // ЦЕЛЫЙ тип обрезал бы всякий перегруз меньше спины вдвое до ровно 1, то
    // есть молча делал бы его БЕСПЛАТНЫМ. Самый частый перегруз мира (112 кг
    // на спине 110) дал бы ровно это.
    //
    // РАНТАЙМ-ПОЛОВИНА ЭТОГО ЗАКОНА ЖИВЁТ НЕ ЗДЕСЬ, И ЭТО КУПЛЕНО МУТАЦИЕЙ:
    // прежний прогон на капах движка спрашивал дверь цены напрямую, читался
    // образцово — и ПРОШЁЛ МУТАЦИЮ ЗЕЛЁНЫМ (кламп возвращён в цепочку свипа),
    // потому что дверь всегда была честной, а обрезал её ПУТЬ СВИПА, куда
    // этот файл не заходит (§8 п.6). Утверждение живёт в
    // `test_an_engine_cap_load_is_priced_whole@tests/squad_travel_test.cpp`.
    static_assert(!std::is_integral_v<decltype(
                      sm::overload_burden_from_capacity(1, bag))>,
                  "множитель перегруза ОБЯЗАН быть дробным: целый тип обрезал "
                  "бы 112 кг на спине 110 до ровно 1 и сделал бы перегруз "
                  "бесплатным молча — тот же немой кламп, что M-302, только в "
                  "другую сторону");

    const float road = sm::feature_bed_weight(sm::FT_Road);
    CHECK(nearf(sm::burn_stamina_per_hour(road, 2.0f),
                sm::burn_stamina_per_hour(road) * 2.0f),
          "an hour costs the ground TIMES the burden carried over it");

    // THE DISCOUNT NOW REACHES THE PACK TOO, and that is the inversion's one
    // semantic loss, named rather than mourned: while the burden was a TERM,
    // mastery of travel left it standing («but his load is not: the burden
    // survives any skill» — the line that used to be here). A FACTOR commutes
    // with the discount by construction, so at mastery the whole product is
    // free. Asserted, not assumed: if someone restores the additive form, this
    // is where it shows.
    sm::Skills master{};
    master[sm::SkillId::Travel] = sm::kMaxSkillRank;
    const float eff = sm::travel_skill_efficiency(master);
    CHECK(nearf(sm::burn_stamina_per_hour(10.0f, 1.0f, eff), 0.0f),
          "at mastery the ground is free");
    CHECK(nearf(sm::burn_stamina_per_hour(10.0f, 3.0f, eff), 0.0f),
          "...and so is the load on top of it: mastery zeroes the PRODUCT");
}

// Death by exhaustion is DESIGN, and since 2026-09-17 the ONE law of zero is
// QUADRATIC (owner, CANON S14.1, «как в Elin»): a spend that lands the bar in
// debt bites HP by debt² / kExhaustionBiteDivisor — shallow debt is a gamble,
// deep debt a sentence. Expectations are DERIVED from the law's own constant,
// so retuning the divisor is a balance decision, not a test edit. This pins
// the curve at the boundary so it can never again become an accident of the
// accounting — and pins the QUADRATIC shape: bite(2d) > 2×bite(d) once the
// floor is passed (a linear law cannot pass that gate).
void test_exhaustion_curve_bites_deeper_each_step() {
    bag.clear();
    const auto law = [](int sp) {
        return sp >= 0 ? 0 : (-sp) * (-sp) / sm::kExhaustionBiteDivisor;
    };
    sm::ecs::Pools cs{};
    cs.sp = 3;
    cs.hp = 1000;

    // Still solvent: stamina pays, the body does not.
    CHECK(sm::apply_stamina_cost(cs, 3) == 0, "stamina pays while it lasts");
    CHECK(cs.sp == 0 && cs.hp == 1000,
           "reaching exactly zero costs no health");

    // Past zero: the spend bites EVERY POINT it drove below zero, each at its
    // own depth. Not once at the final depth — that was the last place where
    // the price of a point depended on WHAT spent it (owner 2026-10-07:
    // «он привязан к трате … сделай просто красиво»), and an act and an hour
    // of ocean now cost the same per point.
    const auto law_over = [&](int from, int to) {   // sum of the curve, (from, to]
        int sum = 0;
        for (int sp = from - 1; sp >= to; --sp) sum += law(sp);
        return sum;
    };
    int hp = 1000;
    for (const int debt : {8, 16, 24}) {
        const int bite = sm::apply_stamina_cost(cs, 8);
        CHECK(bite == law_over(debt - 8 == 0 ? 0 : -(debt - 8), -debt),
              "the spend bites every point it drove below zero, each at its "
              "own depth — the curve integrated, not sampled");
        hp -= bite;
        CHECK(cs.sp == -debt && cs.hp == hp, "debt is kept, health paid");
    }
    // ...AND THAT IS STRICTLY CRUELLER THAN SAMPLING ONCE, which is the whole
    // visible consequence of the unification. Stated as the inequality rather
    // than as a number, so a retuned divisor cannot make it vacuous.
    CHECK(law_over(0, -8) > law(-8),
          "integrating the curve over a spend costs more than reading it once "
          "at the end: eight points of debt bite as 1+2+...+8 depths, not as "
          "the depth 8 alone");

    // THE ACT AND THE HOUR AGREE, which is the law this unification exists
    // for: the same ten points, spent by a swing or burned by a crossing,
    // cost the same blood.
    {
        sm::ecs::Pools byAct{};
        byAct.sp = 0; byAct.hp = 1000;
        sm::ecs::Pools byPoint{};
        byPoint.sp = 0; byPoint.hp = 1000;
        const int actBite = sm::apply_stamina_cost(byAct, 10);
        int pointBite = 0;
        for (int i = 0; i < 10; ++i) pointBite += sm::apply_stamina_cost(byPoint, 1);
        CHECK(actBite > 0 && actBite == pointBite && byAct.hp == byPoint.hp,
              "ten points taken at once cost exactly what ten points taken one "
              "at a time cost — no quantum of spending is cheaper than another");
    }
    CHECK(law(-16) > 2 * law(-8) && law(-32) > 2 * law(-16),
          "the curve is QUADRATIC: doubling the debt more than doubles the "
          "bite (a linear law fails this gate)");
    CHECK(law(-1) == 0,
          "the shallow floor is honest, like the tithe average's: a debt "
          "below the divisor's square root bites nothing");

    // A zero/negative charge is not a free heal or a free step.
    CHECK(sm::apply_stamina_cost(cs, 0) == 0, "a zero cost changes nothing");
    CHECK(cs.sp == -24 && cs.hp == hp, "and touches neither pool");
}

// (No test_both_layers_price_one_journey_alike. It pinned «one macro cell
// on the map == kCellSize tiles on foot cost the same», and that whole
// parity was retired by the owner on 2026-10-06: the subworld has no
// stamina-over-time mechanic at all, so there is no second price to agree
// with. The hole is named in the registry (M-236); inverting this witness
// into «the subworld charges nothing» would guard a hole, not a law.)

// ── The balance itself ──────────────────────────────────────────────────────
// A test about DESIGN INTENT, stated in game hours, so that any retuning has to
// be a deliberate decision rather than a drift. It reads the shipping constants
// and asks the questions a player would:
//
//   • how long can a fresh traveller march before he is spent?
//   • does the road repay the walk to it?
//   • does becoming a better traveller actually let you travel further?
//   • does a night's rest undo a day's march?
//
// The history it guards: travel used to be either lethal in two steps or free
// forever, because stamina recovered WHILE marching — a standing income of
// 10 SP per game hour against a road that cost 25, so any pause paid for the
// journey. Marching and resting are separate states now, and these numbers are
// what that separation is worth.
// Cells covered per game hour of marching on the REFERENCE bed. Since the
// march is quoted in exactly that unit (macro/movement_cost.h), this is no
// longer a derivation from two constants that could drift apart — it IS the
// shipping number, and every hour below is a game hour, whatever a day costs
// in real seconds.
float cells_per_game_hour() {
    return sm::kMacroWalkCellsPerHour;
}

// NET drain of a marching hour on ground of this weight: what the ground asks
// MINUS the passive regen that runs even in motion since 2026-10-24. Negative
// means the march PAYS FOR ITSELF — and that is not an edge case any more, it
// is the common case (see `march_hours`).
float march_drain_per_hour(int fullBarSp, const sm::Skills& skills,
                           float weight, int marathonRank = 0) {
    return sm::burn_stamina_per_hour(weight, /*overloadBurden=*/1.0f,
                                     sm::travel_skill_efficiency(skills))
           - sm::rest_stamina_per_hour(fullBarSp, marathonRank);
}

// HOURS a full bar buys of MARCHING on this ground — or 0, meaning the march
// is SUSTAINABLE and the bar never empties at all.
//
// The zero is the shape of the 2026-10-24 inversion and not a sentinel for a
// missing answer: the regen runs in motion now, so a ground whose burn sits
// below it can be walked forever. Under the 2026-10-06 order every ground had
// a finite number here, and the PACE did not appear in it; both halves of that
// sentence still hold for whatever ground remains unsustainable.
float march_hours(int fullBarSp, const sm::Skills& skills, float weight) {
    const float drain = march_drain_per_hour(fullBarSp, skills, weight);
    if (drain <= 0.0f) return 0.0f;
    return float(fullBarSp) / drain;
}

// What ONE CELL of this ground costs a marching body, which is where the pace
// enters and the ONE statement of the distance ladder that the inversion left
// untouched: heavy ground is walked slower (terrain_speed_mult), so it eats
// the bar twice — through its rate and through the hours it stretches a cell
// into. Gross, not net: this is the price of the ground, and the regen that
// may or may not cover it is a property of the BODY, not of the cell.
float burn_per_cell(const sm::Skills& skills, float weight) {
    const float perHour = sm::burn_stamina_per_hour(
        weight, /*overloadBurden=*/1.0f, sm::travel_skill_efficiency(skills));
    const float cellsPerHour =
        cells_per_game_hour() * sm::terrain_speed_mult(weight)
        * float(sm::calculate_derived(sm::default_attributes(), skills)
                    .moveSpeedPct) / 100.0f;
    return perHour / cellsPerHour;
}

// Hours of camp until a bar refills from empty, lived through THE door hour by
// hour — never a restated rate. The road's burn rate is still handed in, as
// production hands it in, precisely so that this loop WOULD see a camp that
// pays for its ground: since 2026-10-24 it does not, and the number below is
// the bare regen law again.
int rest_hours_to_full(int maxSp, int marathonRank) {
    sm::ecs::Pools p{};
    p.maxSp = maxSp;
    p.maxHp = 1;
    p.hp = 1;
    int hours = 0;
    while (p.sp < maxSp && hours < 64) {
        sm::settle_pools_over_time(
            p, 1.0f, marathonRank,
            sm::burn_stamina_per_hour(sm::feature_bed_weight(sm::FT_Road)),
            /*resting=*/true);
        ++hours;
    }
    return hours;
}

void test_travel_balance_holds_its_intent() {
    bag.clear();
    const sm::Attributes attrs = sm::default_attributes();
    const sm::Skills skills = sm::default_skills();
    const sm::BarCeilings fresh = sm::bar_ceilings(attrs, skills, 100, 100, 100);

    const float roadW = sm::cell_sp_weight(sm::Meadow, sm::FT_Road);
    const float meadowW = sm::cell_sp_weight(sm::Meadow, sm::FT_None);
    const float mountainW = sm::cell_sp_weight(sm::Mountain, sm::FT_None);
    const float waterW = sm::cell_sp_weight(sm::Water, sm::FT_None);

    const float road = march_hours(fresh.maxSp, skills, roadW);
    const float meadow = march_hours(fresh.maxSp, skills, meadowW);
    const float mountain = march_hours(fresh.maxSp, skills, mountainW);
    const float water = march_hours(fresh.maxSp, skills, waterW);

    // ── THE ANCHOR MOVED TWICE, AND BOTH MOVES ARE THE LAW ───────────────
    // 2026-10-06 took it off «a fresh bar buys a DAY of road, camp by
    // nightfall» and put it on the DEAR end of the table: how long a bar keeps
    // a body alive where rest cannot repay the burn. 2026-10-24 then narrowed
    // that end to ONE ROW. The regen runs in motion now, so a march is
    // sustainable wherever the ground asks less than 13.75 SP an hour — and on
    // a fresh bar that is every row of the weight table except open water
    // (meadow 4, swamp 8, full thicket 9, mountain 10 against water's 20).
    //
    // THIS IS THE BALANCE SHIFT, AND IT IS STATED HERE RATHER THAN SMOOTHED:
    // the numbers below are not a band re-fitted around an observation, they
    // are the shape the owner's verdict produces, and if the shape is not the
    // one he wants, the knob to move is `kStaminaPerWeightHour` or
    // `kRestRegenPctPerHour` — never this witness (ЗАКОН ПРИОРИТЕТА СТРУКТУРЫ).
    CHECK(nearf(water, sm::kWaterDriftHoursPerFreshBar, 0.05f),
          "a fresh bar buys the better part of a day of SWIMMING, and the "
          "shipping formula agrees with the compile-time anchor to the point");
    CHECK(road == 0.0f && meadow == 0.0f && mountain == 0.0f,
          "...and OPEN WATER IS THE ONLY GROUND LEFT THAT CAN EXHAUST A "
          "TRAVELLER: road, meadow and mountain all ask less per hour than the "
          "passive regen pays, so a march across them never empties the bar");

    // THE RATE ladder is what survives of the per-hour law, and it is exactly
    // proportional to WEIGHT — no √, no pace. Stated as a ratio rather than a
    // number, so a recalibration of the knob cannot touch it (AGENTS §8 п.4).
    // It is the gross rate, so it is a statement about the GROUND and holds
    // whoever walks it, sustainably or not.
    const auto rate = [&](float w) {
        return sm::burn_stamina_per_hour(w, 1.0f,
                                         sm::travel_skill_efficiency(skills));
    };
    CHECK(nearf(rate(meadowW) / rate(roadW), meadowW / roadW, 0.01f),
          "the hourly rate scales as the weight, exactly");
    CHECK(nearf(rate(waterW) / rate(mountainW), waterW / mountainW, 0.01f),
          "and the same single ratio holds at the dear end of the table");

    // THE PER-CELL ladder scales as weight^1.5: the pace enters here and only
    // here. Heavy ground is paid for TWICE — richer rate and longer hours per
    // cell — which is why a road is worth building even now that marching
    // across a mountain is survivable. Untouched by the inversion, because it
    // prices the CELL and the regen belongs to the body.
    const float roadCell = burn_per_cell(skills, roadW);
    const float meadowCell = burn_per_cell(skills, meadowW);
    const float mountainCell = burn_per_cell(skills, mountainW);
    const float waterCell = burn_per_cell(skills, waterW);
    CHECK(nearf(meadowCell / roadCell, std::pow(meadowW / roadW, 1.5f), 0.01f),
          "the price of a cell scales as weight^1.5 — the rate times the pace");
    CHECK(roadCell * 2.5f < meadowCell, "a road is worth walking to");
    CHECK(mountainCell > meadowCell * 2.5f, "mountains are a real obstacle");
    CHECK(waterCell > mountainCell * 2.5f,
          "swimming is still the most expensive way to travel");

    // Progression: an RPG must reward the character sheet. A veteran carries a
    // bigger pool (END and WILL by half each — the canon eight, 2026-09-03)
    // AND burns less on the same ground (travel).
    sm::Attributes vetAttrs = attrs;
    vetAttrs[sm::AttributeId::End] = 20;
    vetAttrs[sm::AttributeId::Wil] = 20;
    sm::Skills vetSkills = skills;
    vetSkills[sm::SkillId::Travel] = 10;
    const sm::BarCeilings veteran = sm::bar_ceilings(vetAttrs, vetSkills, 100, 100, 100);
    // AND SINCE 2026-10-24 HIS REWARD IS THE SEA ITSELF, which is the second
    // consequence of the inversion and the louder of the two: the regen is a
    // PERCENT of the bar while the burn is an ABSOLUTE rate, so a big enough
    // bar outgrows every ground in the world. 300 SP pays 37.5 an hour against
    // the ocean's 18 (travel 10 discounts it), so he swims indefinitely where
    // a fresh body drowns in 17.6 hours. Named, not hidden: this is the shape
    // the verdict produces, and whether the world should be crossable on foot
    // by a veteran is the owner's call, not a witness's.
    CHECK(march_drain_per_hour(veteran.maxSp, vetSkills, waterW)
              < march_drain_per_hour(fresh.maxSp, skills, waterW),
          "training lowers the net drain of the same ground");
    CHECK(march_hours(fresh.maxSp, skills, waterW) > 0.0f
              && march_hours(veteran.maxSp, vetSkills, waterW) == 0.0f,
          "...far enough that the ONE unsustainable ground in the world "
          "becomes sustainable for a veteran");

    // The Session 21 lever split, pinned. The bar belongs to the ATTRIBUTES
    // alone (END and WILL by half each); `marathon` buys the RATE of recovery
    // and never the bar. Full rest is therefore the same 8 hours for every
    // sheet in the world (regen is a PERCENT of the bar), and only marathon
    // shortens it.
    sm::Skills marathoner = skills;
    marathoner[sm::SkillId::Marathon] = 20;
    CHECK(sm::bar_ceilings(attrs, marathoner, 100, 100, 100).maxSp == fresh.maxSp,
          "marathon does not grow the bar");
    CHECK(rest_hours_to_full(fresh.maxSp, 20)
              < rest_hours_to_full(fresh.maxSp, 0),
          "marathon does speed the recovery");
    const int freshRestH = rest_hours_to_full(fresh.maxSp, 0);
    const int vetRestH = rest_hours_to_full(veteran.maxSp, 0);
    // EQUALITY, AND IT IS BACK. Between 2026-10-06 and 2026-10-24 a camp paid
    // its own ground, so the two differed by the share that flat 2 SP/h ate of
    // each bar's percentage recovery (measured then: 10 h against 9). The
    // inversion took the burn out of standing, so nothing but the percentage
    // enters, and full rest is the SAME NIGHT for every sheet in the world.
    CHECK(vetRestH == freshRestH,
          "full rest is the same night whatever the bar: the regen is a "
          "percent of it and a camp costs nothing at all");
    // ...and that night is EXACTLY the bare regen law — eight hours, 1/(1/8).
    // Equality here is the strongest form this claim has ever had: it says
    // the camp has no second term, rather than that its second term is small.
    CHECK(freshRestH == int(1.0f / sm::kRestRegenPctPerHour),
          "a night of camp is the bare regen law and nothing else: no ground "
          "under a halted body costs it a point");

    CHECK(sm::travel_skill_efficiency(vetSkills) < 1.0f
              && sm::travel_skill_efficiency(vetSkills) > 0.0f,
          "the travel skill discounts terrain without ever making it free");
    CHECK(nearf(sm::travel_skill_efficiency(skills), 1.0f),
          "an untrained traveller gets no discount");

    // One skill, one meaning — AND THE MEANING OF `athletics` SHARPENED on
    // 2026-10-06 instead of staying orthogonal. `travel` lowers the RATE, so
    // it buys HOURS; `athletics` shortens the exposure, so it buys only
    // DISTANCE. Under the dead per-cell price speed bought nothing at all for
    // the bar; under the hour it buys ground, and that is a consequence of the
    // quantum rather than a design change, so it is stated rather than hidden.
    sm::Skills sprinter = skills;
    sprinter[sm::SkillId::Athletics] = 20;
    CHECK(nearf(sm::travel_skill_efficiency(sprinter), 1.0f),
          "athletics does not make ground cheaper per hour");
    CHECK(nearf(march_drain_per_hour(fresh.maxSp, sprinter, meadowW),
                march_drain_per_hour(fresh.maxSp, skills, meadowW)),
          "so a sprinter's HOUR costs exactly what a plodder's costs");
    CHECK(burn_per_cell(sprinter, meadowW) < meadowCell * 0.96f,
          "...but his CELL costs less, which is what speed now buys: the same "
          "hour laid over more ground");
    CHECK(sm::calculate_derived(attrs, sprinter).moveSpeedPct
              > sm::calculate_derived(attrs, skills).moveSpeedPct,
          "athletics does make the traveller faster");
    sm::Skills pathfinder = skills;
    pathfinder[sm::SkillId::Travel] = 20;
    CHECK(sm::calculate_derived(attrs, pathfinder).moveSpeedPct
              == sm::calculate_derived(attrs, skills).moveSpeedPct,
          "the travel skill does not make the traveller faster");
    CHECK(sm::travel_skill_efficiency(pathfinder) < 1.0f,
          "the travel skill does make ground cheaper");

    // A night undoes a day, and the march half of the pin is ONE ARGUMENT
    // (settle_pools_over_time's `resting`), not the shape of the callers.
    sm::ecs::Pools resting{};
    resting.maxHp = fresh.maxHp;
    resting.hp = fresh.maxHp;
    resting.maxMp = fresh.maxMp;
    resting.maxSp = fresh.maxSp;
    resting.sp = 0;
    // EIGHT hours — the bare law, measured through the door by `freshRestH`
    // above rather than restated here.
    sm::settle_pools_over_time(
        resting, float(freshRestH), 0,
        sm::burn_stamina_per_hour(sm::feature_bed_weight(sm::FT_Road)),
        /*resting=*/true);
    CHECK(resting.sp >= fresh.maxSp - 1,
          "a night of camp on the road refills the whole bar");
    // THE NEGATIVE CONTROL, AND ITS GROUND HAD TO MOVE TO OPEN WATER. On the
    // ROAD the same hours spent marching now REFILL the bar (2 SP/h of burn
    // against 13.75 of regen), so the old fixture would have asserted the
    // opposite of the world. Water is the one row where a march still outruns
    // the regen, which is exactly the claim `march_hours` makes above — so
    // this control and that assertion stand or fall together.
    sm::ecs::Pools marchingBody = resting;
    marchingBody.sp = 0;
    marchingBody.hp = 1;
    sm::settle_pools_over_time(
        marchingBody, float(freshRestH), 0,
        sm::burn_stamina_per_hour(sm::biome_sp_weight(sm::Water)),
        /*resting=*/false);
    CHECK(marchingBody.sp < 0 && marchingBody.hp <= 1,
          "the same eight hours spent SWIMMING refill nothing and cost blood "
          "— the negative control on the gate above");
    // ...and the SAME hours on the SAME road, marching, are the second half of
    // that control: the inversion means a cheap march is a net GAIN, and a
    // silent revert to the 2026-10-06 order lands right here.
    sm::ecs::Pools roadMarcher = resting;
    roadMarcher.sp = 0;
    roadMarcher.hp = 1;
    sm::settle_pools_over_time(
        roadMarcher, float(freshRestH), 0,
        sm::burn_stamina_per_hour(sm::feature_bed_weight(sm::FT_Road)),
        /*resting=*/false);
    CHECK(roadMarcher.sp > 0 && roadMarcher.hp == 1,
          "while a ROAD march over those hours ends with stamina in the bar "
          "and no blood paid: the passive regen outruns the best ground");

    // THE SKILL LAW, pinned through the one door (skill_mult_of): a rank is
    // the row's percent, and the cap is the ceiling.
    CHECK(nearf(sm::skill_mult_of(sm::SkillId::Marathon, 0), 1.0f),
          "rank 0 grants nothing");
    CHECK(nearf(sm::skill_mult_of(sm::SkillId::Marathon, 37), 1.37f),
          "rank reads as the row's percent");
    CHECK(nearf(sm::skill_mult_of(sm::SkillId::Travel, 37), 0.63f),
          "and as percent off a cost");
    CHECK(nearf(sm::skill_mult_of(sm::SkillId::Travel, sm::kMaxSkillRank), 0.0f),
          "mastery of a cost skill removes that cost entirely");
    CHECK(nearf(sm::skill_mult_of(sm::SkillId::Marathon, sm::kMaxSkillRank + 500),
                sm::skill_mult_of(sm::SkillId::Marathon, sm::kMaxSkillRank)),
          "nothing above the cap counts, however it got there");
    CHECK(nearf(sm::skill_mult_of(sm::SkillId::Travel, -5), 1.0f),
          "and nothing below zero does");

    // The cap is enforced at the one door into a rank, so no path can exceed
    // it. Learning comes first (THE learn law): rank 0 refuses a spend, so
    // mastery is 1 (learned) + 99 spends.
    sm::LevelData ld{};
    sm::Skills capped{};
    ld.skillPoints = sm::kMaxSkillRank + 10;
    CHECK(!sm::spend_skill_point(ld, capped, sm::SkillId::Travel),
          "an unknown skill refuses the point: learn first");
    CHECK(sm::learn_skill(capped, sm::SkillId::Travel),
          "the world teaches, and rank 1 is the knowing");
    int spent = 0;
    while (sm::spend_skill_point(ld, capped, sm::SkillId::Travel)) ++spent;
    CHECK(spent == sm::kMaxSkillRank - 1
              && capped.of(sm::SkillId::Travel) == sm::kMaxSkillRank,
          "a rank stops at mastery");
    CHECK(ld.skillPoints == 11,
          "and a refused spend keeps the point for another skill");

    // The balance, printed on every run: a number you can read is a number you
    // can argue with. A ZERO in the hours row means SUSTAINABLE, not «no
    // answer» — the row's whole point since 2026-10-24 is how few grounds are
    // left with a finite number in it.
    std::printf("   travel balance per full bar (%d SP, passive regen %.2f "
                "SP/h)\n"
                "     game HOURS of marching (0 = sustainable): road %.1f  "
                "meadow %.1f  mountain %.1f  water %.1f\n"
                "     SP per macro CELL:      road %.2f  meadow %.2f  "
                "mountain %.2f  water %.2f\n"
                "     veteran water %.1f h  (bar %d, terrain x%.2f)  "
                "camp-to-full %d h\n",
                fresh.maxSp,
                double(sm::rest_stamina_per_hour(fresh.maxSp, 0)),
                double(road), double(meadow), double(mountain), double(water),
                double(roadCell), double(meadowCell),
                double(mountainCell), double(waterCell),
                double(march_hours(veteran.maxSp, vetSkills, waterW)),
                veteran.maxSp,
                double(sm::travel_skill_efficiency(vetSkills)), freshRestH);
}

// (No test_invalid_terrain_fails_closed. It pinned that a REJECTED terrain
// query clears its stale cost output — a property of macro_travel_cost_for_cell,
// which no longer exists. The fail-closed law itself is alive and witnessed
// where it lives: pathfinding_parity_test asserts cell_sp_weight returns the
// default weight for a garbage biome and a garbage feature, never 0.0.)

} // namespace

int main() {
    test_cell_costs_follow_the_weight_table();
    test_overload_is_a_factor_of_the_hour();
    test_exhaustion_curve_bites_deeper_each_step();
    test_travel_balance_holds_its_intent();
    return sm::test::report("macro_travel_parity_test");
}

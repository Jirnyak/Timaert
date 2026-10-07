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

sm::FeatureLayer make_features() {
    sm::FeatureLayer features;
    features.resize(2, 2);
    features.set(0, 0, sm::FT_Road);
    features.set(1, 1, sm::FT_DirtRoad);
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
    const sm::FeatureLayer features = make_features();
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

// THE overload law, and WHERE it is paid now: it is a TERM of the hourly burn,
// added after the terrain is discounted — so a trained traveller walks cheap
// ground for nothing and still carries what he carries. Until 2026-10-06 this
// term rode the per-cell price; it moved because killing the step's price
// without rehousing the load would have silently retired the owner's own
// ruling («да, перегруз универсальный всем», 2026-08-27).
void test_overload_is_a_term_of_the_hour() {
    bag.clear();
    bag.add("wood", 56);   // 112 kg, default capacity is 110 kg.
    const sm::OverloadCharge oc = sm::overload_charge(walkerSheet, bag);
    CHECK(std::fabs(oc.overload - 2.0f) < 0.001f,
          "overload is weight minus carry capacity");
    CHECK(oc.cost == 2, "any overload hurts: kilos over, rounded up");

    const float road = sm::feature_bed_weight(sm::FT_Road);
    CHECK(nearf(sm::burn_stamina_per_hour(road, oc.cost),
                sm::burn_stamina_per_hour(road) + 2.0f),
          "an hour costs the ground PLUS the burden carried over it");

    // The discount is on the GROUND alone. At mastery the world stops
    // resisting the traveller — and the pack on his back still weighs what it
    // weighs, which is the whole reason the two terms are added and not
    // multiplied together.
    sm::Skills master{};
    master[sm::SkillId::Travel] = sm::kMaxSkillRank;
    const float eff = sm::travel_skill_efficiency(master);
    CHECK(nearf(sm::burn_stamina_per_hour(10.0f, 0, eff), 0.0f),
          "at mastery the ground is free");
    CHECK(nearf(sm::burn_stamina_per_hour(10.0f, 3, eff), 3.0f),
          "but his load is not: the burden survives any skill");
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

    // Past zero: each spend bites the SQUARED outstanding debt.
    int hp = 1000;
    for (const int debt : {8, 16, 24}) {
        const int bite = sm::apply_stamina_cost(cs, 8);
        CHECK(bite == law(-debt), "the spend bites debt squared, from the law");
        hp -= bite;
        CHECK(cs.sp == -debt && cs.hp == hp, "debt is kept, health paid");
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

// HOURS a full bar buys on ground of this weight. Under the hour quantum this
// is the whole of it: bar / burn-rate, and the PACE does not appear — which is
// the single most load-bearing consequence of the 2026-10-06 law and the thing
// `march_cells` below exists to contrast.
float march_hours(int fullBarSp, const sm::Skills& skills, float weight) {
    const float perHour = sm::burn_stamina_per_hour(
        weight, 0, sm::travel_skill_efficiency(skills));
    if (perHour <= 0.0f) return 0.0f;
    return float(fullBarSp) / perHour;
}

// ...and the DISTANCE those hours cover, which is where the pace enters: heavy
// ground is walked slower (terrain_speed_mult), so it eats the bar twice —
// once through its rate and once through the hours it stretches a cell into.
float march_cells(int fullBarSp, const sm::Skills& skills, float weight) {
    return march_hours(fullBarSp, skills, weight)
           * cells_per_game_hour() * sm::terrain_speed_mult(weight)
           * float(sm::calculate_derived(sm::default_attributes(), skills)
                       .moveSpeedPct) / 100.0f;
}

// Hours of camp until a bar refills from empty, lived through THE door hour by
// hour — never a restated rate. On the ROAD, because the door takes a burn
// rate now and a camp stands somewhere: the road is the reference bed, and its
// compile-time guarantee (it heals more than it burns) is what makes the loop
// terminate at all.
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
            /*regenerates=*/true);
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

    // ── THE ANCHOR MOVED, AND THE MOVE IS THE LAW (2026-10-06) ───────────
    // The anchor used to be «a fresh bar buys a DAY of road, camp by
    // nightfall», and it cannot be that any more: the road now burns 2 SP an
    // hour against 13.75 of rest, so a body ON a road gains ground by standing
    // on it and the hours it can march are not what the economy is balanced
    // against. What IS balanced is the DEAR end of the table — how long a bar
    // keeps a body alive where rest cannot repay the burn — and
    // movement_cost.h asserts exactly that at compile time
    // (kWaterDriftHoursPerFreshBar). This checks the marching half of the same
    // ladder through the shipping formula, sheet and all.
    //
    // The owner ruled the shift ACCEPTED rather than tuned: «если сдвинет мир
    // не важно не надо даже подгонять … тот был не идеален так что делаем
    // чисто системно». So the numbers below state the new shape; they are not
    // the old band re-fitted.
    CHECK(water > 4.0f && water < 8.0f,
          "a fresh bar buys the better part of a day of SWIMMING, and that is "
          "the end of the ladder the economy is anchored on");
    CHECK(road > 24.0f,
          "...while the road is days of it: under the hour quantum a cheap bed "
          "is cheap in TIME, not merely in points per step");

    // HOURS are inversely proportional to WEIGHT, exactly — no √, no pace.
    // This is the law stated as a ratio rather than as a number, so a
    // recalibration of the knob cannot touch it (AGENTS §8 п.4).
    CHECK(nearf(road / meadow, meadowW / roadW, 0.01f),
          "hours scale as 1/weight: the road's bed is the whole of its gain");
    CHECK(nearf(meadow / mountain, mountainW / meadowW, 0.01f),
          "and the same single ratio holds at the dear end of the table");

    // DISTANCE, though, scales as weight^-1.5: the pace enters here and only
    // here. Heavy ground is paid for TWICE — richer rate and longer hours per
    // cell — which is why a road is worth building even though standing on any
    // ground is now survivable.
    const float roadCells = march_cells(fresh.maxSp, skills, roadW);
    const float meadowCells = march_cells(fresh.maxSp, skills, meadowW);
    const float mountainCells = march_cells(fresh.maxSp, skills, mountainW);
    const float waterCells = march_cells(fresh.maxSp, skills, waterW);
    CHECK(nearf(roadCells / meadowCells,
                std::pow(meadowW / roadW, 1.5f), 0.01f),
          "distance scales as weight^-1.5 — the rate times the pace");
    CHECK(roadCells > meadowCells * 2.5f, "a road is worth walking to");
    CHECK(mountainCells < meadowCells * 0.4f, "mountains are a real obstacle");
    CHECK(waterCells < mountainCells * 0.4f,
          "swimming is still the most expensive way to travel");

    // Progression: an RPG must reward the character sheet. A veteran carries a
    // bigger pool (END and WILL by half each — the canon eight, 2026-09-03)
    // AND burns less on the same ground (travel), so his day becomes several.
    sm::Attributes vetAttrs = attrs;
    vetAttrs[sm::AttributeId::End] = 20;
    vetAttrs[sm::AttributeId::Wil] = 20;
    sm::Skills vetSkills = skills;
    vetSkills[sm::SkillId::Travel] = 10;
    const sm::BarCeilings veteran = sm::bar_ceilings(vetAttrs, vetSkills, 100, 100, 100);
    const float vetMeadow = march_hours(veteran.maxSp, vetSkills, meadowW);
    CHECK(vetMeadow > meadow * 3.0f,
          "training triples the time a traveller lasts on the same ground");

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
    // A bigger bar rests no LONGER — and since 2026-10-06 it rests slightly
    // SHORTER, which is the burn being real: the regen is a PERCENT of the bar
    // and the burn is an ABSOLUTE rate, so the ground's 2 SP/h eats 1/7 of a
    // fresh body's hourly recovery and only 1/19 of a veteran's. Measured on
    // the road: 10 h against 9. Equality was the old law, when nothing burned
    // in camp at all.
    CHECK(vetRestH <= freshRestH,
          "a bigger bar rests no longer: the regen is a percent of it, while "
          "the camp's own burn is a flat rate the big bar barely feels");
    // ...and a camp on the road is SLOWER to fill than the bare 1/8 law, by
    // exactly the hour it also burns. The design's eight hours became nine,
    // and that is the burn being real rather than a rounding.
    CHECK(freshRestH > int(1.0f / sm::kRestRegenPctPerHour),
          "a night of camp costs its own burn: the road's 2 SP/h is paid even "
          "asleep, so a full bar takes LONGER than the bare regen law says");
    CHECK(freshRestH < 2 * int(1.0f / sm::kRestRegenPctPerHour),
          "...but not twice as long: the road must stay a place one can rest");

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
    CHECK(nearf(march_hours(fresh.maxSp, sprinter, meadowW), meadow),
          "so a sprinter lasts exactly as many HOURS as a plodder");
    CHECK(march_cells(fresh.maxSp, sprinter, meadowW) > meadowCells * 1.05f,
          "...and covers MORE GROUND in them, which is what speed now buys");
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

    // A night undoes a day, and the march half of the pin is ONE ARGUMENT now
    // (settle_pools_over_time's `regenerates`), not the shape of the callers:
    // legs in motion pass false, and that covers all three bars.
    sm::ecs::Pools resting{};
    resting.maxHp = fresh.maxHp;
    resting.hp = fresh.maxHp;
    resting.maxMp = fresh.maxMp;
    resting.maxSp = fresh.maxSp;
    resting.sp = 0;
    // TEN hours, not the bare law's eight: a camp on the road burns its own
    // 2 SP/h even asleep, so the night the design promises got longer by
    // exactly that. `freshRestH` above measures the same number through the
    // door rather than restating it.
    sm::settle_pools_over_time(
        resting, float(freshRestH), 0,
        sm::burn_stamina_per_hour(sm::feature_bed_weight(sm::FT_Road)),
        /*regenerates=*/true);
    CHECK(resting.sp >= fresh.maxSp - 1,
          "a night of camp on the road refills the whole bar");
    sm::ecs::Pools marchingBody = resting;
    marchingBody.sp = 0;
    marchingBody.hp = 1;
    sm::settle_pools_over_time(
        marchingBody, float(freshRestH), 0,
        sm::burn_stamina_per_hour(sm::feature_bed_weight(sm::FT_Road)),
        /*regenerates=*/false);
    CHECK(marchingBody.sp < 0 && marchingBody.hp <= 1,
          "the same nine hours spent MARCHING refill nothing and cost blood — "
          "the negative control on the gate above");

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
    // can argue with.
    std::printf("   travel balance per full bar (%d SP)\n"
                "     game HOURS of marching: road %.1f  meadow %.1f  "
                "mountain %.1f  water %.1f\n"
                "     macro CELLS covered:    road %.0f  meadow %.0f  "
                "mountain %.0f  water %.0f\n"
                "     veteran meadow %.1f h  (bar %d, terrain x%.2f)  "
                "camp-to-full %d h\n",
                fresh.maxSp,
                double(road), double(meadow), double(mountain), double(water),
                double(roadCells), double(meadowCells),
                double(mountainCells), double(waterCells),
                double(vetMeadow), veteran.maxSp,
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
    test_overload_is_a_term_of_the_hour();
    test_exhaustion_curve_bites_deeper_each_step();
    test_travel_balance_holds_its_intent();
    return sm::test::report("macro_travel_parity_test");
}

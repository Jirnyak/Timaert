// Movement stamina — what standing in the world costs a body per hour of it,
// and what happens when the body cannot pay.
//
// ONE UNIT: THE GAME HOUR (owner, 2026-10-06). A body BURNS `cell weight ×
// kStaminaPerWeightHour` every game hour, moving or not, and RECOVERS a flat
// percent of its bars per hour whenever it is not moving (macro/recovery.h
// settle_pools_over_time). Two always-on processes, added by SIGN, no branch
// between them.
//
// THERE IS NO PRICE OF A STEP, and that is the point. A march is dear because
// heavy ground is SLOWER (terrain_speed_mult below, from the same weight row),
// so the body spends more hours on it — the spread of the weight table arrives
// as TIME, and stamina follows the time. The per-cell price this replaced was
// the last thing in the world that answered «можно ли тут идти» with a number
// of its own; with it went the standing predicate that answered the same
// question with a WALL (`nav_can_stand` — see burn_stamina_per_hour).
//
// WHAT THIS LEFT OPEN, NAMED RATHER THAN PLUGGED (owner, 2026-10-06, дословно:
// «давай тогда ща sp за движение в субмире не тратится и регена нет и всё
// просто временно для субмира нет этой механик типа самое простое чистое
// минимальеон решение»): the SUBWORLD has no stamina-over-time mechanic at
// all — neither burn nor regen. The hole is in the registry (M-236), not
// behind a temporary stand-in: a placeholder here would be the second law
// this whole change exists to remove.
//
// Costs are FRACTIONAL and accumulate through the body's own signed carry
// (settle_sp_carry below); SP moves in whole points. Nothing is lost to
// rounding and nothing is stored in the save — a load starts the carry at
// zero, worth at most 1 SP.
#pragma once
#include <cmath>
#include "core/time.h"       // the ladder: kSubworldWalkTilesPerSecond derives from it
#include "ecs/pools.h"       // the bars this law spends and bites — one home
#include "macro/anketa.h"
#include "tables/biomes.h"
#include "macro/features.h"

namespace sm {

// SP per weight-unit per GAME HOUR. THE knob for how long a body can stand
// anywhere at all: this number × the cell's weight, and every modifier
// (travel skill, overload) rides ON TOP, never folded in — the owner's shape,
// 2026-08-24, unchanged by the move to the hour.
//
// TWO, and the two keeps a compile-time gate under it
// (kWaterDriftHoursPerFreshBar, below the bed tables) because THIS NUMBER
// ALONE SAYS NOTHING: what is balanced is GAME HOURS, and before 2026-10-06
// the hours were the PRODUCT of this knob and the PACE — a product with no
// name of its own to fail under. It drifted exactly there: the 2026-08-24
// recalibration moved the pace 32 → 8 cells/h and this knob 7/16 → 1, the
// product fell 14 → 8 SP/h, and every quoted hour grew 1.75× inside a comment.
// The hour quantum takes the pace OUT of the price, so the product is gone and
// this knob now says the whole thing by itself — the drift of that class is
// not fixed, it is unspellable.
//
// Priced per HOUR, never per cell — the reverse of what stood here until
// 2026-10-06, and the reversal is the whole law. Walking faster now covers
// more ground for the same stamina, because what is paid for is the TIME spent
// on the ground, and heavy ground takes longer per cell (terrain_speed_mult).
// So `travel` (price of ground) and `spd` (pace) still never fight: one buys
// the rate down, the other shortens the exposure.
//
// With the terrain speed law folded in, a fresh level-1 bar (110 SP —
// attributes.h, the bare sheet) is worth, per cell crossed, about
//
//    road 0.25 · meadow 0.7 · full thicket 1.2 · mountain 1.4 · water 7.9 SP
//
// — a road costs 8× less than the per-cell law charged, and the owner ruled
// the shift accepted rather than tuned («если сдвинет мир не важно не надо
// даже подгонять … тот был не идеален так что делаем чисто системно»). What
// holds the economy now is not the road's price but the LADDER: see the two
// static_asserts under burn_stamina_per_hour — water must out-burn rest, road
// must not.
constexpr float kStaminaPerWeightHour = 2.0f;

// (No kMarchRecoveryPct, and no «the march simply never calls the rest law»
// either. «Марш не лечит НИЧЕГО» is now ONE ARGUMENT of the one door
// (settle_pools_over_time's `regenerates`), asked of every body on every
// slice: moving bodies pass false. A rate knob that could be set to 0.2 was a
// door back to the road healing for free; a caller-shaped law was a door to
// the opposite — a body nobody remembered to call simply stopped living.)

// What the `travel` skill does, and the only thing it does: it buys down the
// stamina cost of ground, one percent per rank, under THE skill law
// (macro/attributes.h): rank == percent, capped at kMaxSkillRank. At mastery the
// terrain costs nothing — a hundred levels poured into travelling, and the world
// stops resisting you. What that does NOT buy is a free ride: an overloaded pack
// is a separate term in the cost, and the exhaustion curve is untouched.
//
// One skill, one effect. The RATE of ground is this skill's business; SPEED is
// `spd` and `athletics`. The two never fight, and under the hour quantum they
// compose instead: this rank buys the burn-per-hour down, the pace shortens the
// hours exposed to it.
inline float travel_skill_efficiency(const Skills& s) {
    return skill_mult(s, SkillId::Travel);
}

// Death by exhaustion, and it is DESIGN, not an accident — and since
// 2026-09-17 the ONE law of zero is QUADRATIC (owner, CANON S14.1, дословно:
// «один закон! ЛЮБАЯ ЗАТРАТА SP ниже 0 универсально квадратично бьёт по хп
// (как в Elin)»): every spend that leaves the bar in debt bites HP by the
// debt SQUARED over this divisor. Shallow debt is a gamble (−4 → 2 HP),
// deep debt is a sentence — pressing on is priced, never gated: no writer
// of SP refuses at zero, refusing is an AI head's DECISION.
// The divisor is a дубль-прогон knob; po2, integer, house style. 32 crosses
// the old linear curve at −32: shallower debt is GENTLER than before (−8 →
// 2 HP, was 8 — a narrow ford costs blood, not life), deeper is crueller
// (−64 → 128, was 64 — the open sea drowns faster). The floor is honest: a
// debt whose square sits below the divisor bites nothing — the same
// asymmetric floor the tithe average keeps by design.
constexpr int kExhaustionBiteDivisor = 32;

// How fast the macro march covers ground, in cells per GAME HOUR. Not per real
// second — that is the whole point. Stamina is priced per cell and recovery per
// game hour, so this constant IS the exchange rate between the two, and quoting
// it in game time means the length of a day can be tuned as a matter of feel
// without moving the travel economy a single point.
//
// The subworld's own walk (kSubworldWalkTilesPerSecond, below) stays in
// REAL seconds on purpose, and the difference is not an oversight: down there
// you are a body doing a thing in real time, up here you are an abstraction of
// a journey. Two different denominators for two different kinds of motion.
// EIGHT — the owner's word (2026-08-24, «степени двойки!»): a brisk paved
// pace at the world's own scale (cell ≈ 1 km, S1): 8 km/h on the road bed,
// /√weight elsewhere — meadow ~5.7, thicket and mountain ~3. A day's march
// lands at 30–60 km, which is what a day's march IS. The old 32 was a
// courier's gallop miscalled walking: the player crossed 125 km before the
// morning ended, and every distance in the world meant nothing.
constexpr float kMacroWalkCellsPerHour = 8.0f;

// Tiles of subworld scene per macro cell. sub/map_data.h kCellSize asserts
// against THIS number, so the two scales cannot quietly disagree about how
// long a cell is (it lives here because the macro side may not include sub/).
constexpr float kSubworldTilesPerMacroCell = 1024.0f;

// Base on-foot speed in the SUBWORLD, in tiles per REAL second, before the
// character's own pace (DerivedBonuses::moveSpeedMult) and haste.
//
// DERIVED, NOT TUNED — and since 2026-09-03 derived IN CODE, because it was
// derived in a comment and the comment could not follow a knob: it is the
// same body walking the same world at the other scale, so crossing a cell's
// scene costs exactly the game minutes the macro march charges for that cell
// (the owner's parity anchor, «клетка ≈ клетка»). The arithmetic:
// kMacroWalkCellsPerHour × kSubworldTilesPerMacroCell tiles per game hour,
// over what a subworld game hour lasts in real seconds
// (kTicksPerDay/24/kTicksPerRealSecond × kSubworldTickDivisor). The TIME
// RUNG is therefore the one visual-pace knob: ÷16 gave 96 t/s (the racing
// the owner rejected), ÷64 gives 24 (canon-audit A8's «the map was
// galloping» stays honoured — the map's own 8 is untouched).
//
// It is the speed on the REFERENCE bed — the road (bed 1.0) — because the
// march it derives from is; every other ground divides it by √weight through
// terrain_speed_mult, which is the one law both scales walk by.
constexpr float kSubworldWalkTilesPerSecond =
    kMacroWalkCellsPerHour * kSubworldTilesPerMacroCell
    * float(kTicksPerRealSecond) * 24.0f
    / (float(kTicksPerDay) * float(kSubworldTickDivisor));
static_assert(kSubworldWalkTilesPerSecond == 24.0f,
              "the ÷64 rung reads as 24 tiles/s — a moved knob shows here");

// THE pace of a body, from what its row says it is against a walking man
// (army.h CombatTemplate::speedMarchMult). One scale for everything that
// moves in the subworld — man, beast and player alike.
inline constexpr float march_speed(float marchMult) {
    return kSubworldWalkTilesPerSecond * marchMult;
}

// A WALKING MAN — the row every human body is stated against, and the one the
// player wears (his own sheet's moveSpeedMult rides on top, exactly as a
// hasted guard's would). 1.0 by construction: the march IS a man walking.
inline constexpr float kHumanMarchMult = 1.0f;

// How much the GROUND slows the march: speed = base / √weight, derived from
// the SAME weight table that prices stamina (owner ruling, Session 21) —
// heavy ground is automatically both slower and costlier, a new biome is one
// weight row, and there is no second table to drift out of lockstep with the
// first (the target_radius lesson). √ rather than 1/weight so terrain bites
// but does not crawl: water (10×) walks at a third of road pace, not a tenth.
//
// Composition note, INVERTED on 2026-10-06 and load-bearing now: stamina is
// priced per HOUR, so slowing down is EXACTLY how heavy ground costs stamina.
// Per cell crossed the burn is (weight^1.5 × kStaminaPerWeightHour /
// kMacroWalkCellsPerHour) — the √ of this law multiplying the weight of the
// burn — so the table's ORDER is not merely preserved, it is SHARPENED: road
// to water spreads 1:10 per hour and 1:32 per cell. This function is the only
// place that spread comes from; a second pace law would silently re-price
// every ground in the game.
inline float terrain_speed_mult(float weight) {
    if (weight <= 1.0f) return 1.0f;
    return 1.0f / std::sqrt(weight);
}

// ── THE step-cost law: BED + CONTRIBUTIONS (CANON S6/S7, 2026-08-24) ──────
//
// The optics idiom (macro/optics.h — the canon's exemplar table): an
// engineered FEATURE lays the bed — road 1.0 (the reference: speed =
// base/√weight, so the road IS the base march), dirt 1.5, ploughed field
// 1.8 — and where nothing is built the biome's own ground is the bed.
// CONTINUOUS contributions then ADD on top:
//
// · canopy — kCanopySpWeight × tree density (count / kMaxTreesPerCell). The
//   boolean forest-class cliff is gone: thickening woods slow the march
//   smoothly, exactly as they dim the light (optics kCanopyOpticalCost). An
//   engineered bed gates the canopy off — a road through the wood is a CUT
//   (просека), the trees stand beside it, not on it.
// · climb — kClimbSpWeight × the UPHILL height difference of the edge being
//   walked (downhill is free), priced where the step happens because a slope
//   is a fact of an EDGE, not of a cell. It makes the cost directional —
//   both A*s and the greedy squad step price it at expansion — and it obeys
//   every bed: a mountain road is honestly dearer than a valley road.
//
// It replaced a PRIORITY ladder (feature OVERRODE forest OVERRODE biome), in
// which a contribution like weather had no place to stand: under a sum, a
// new world system is one more term (S6), zero when silent.
inline constexpr float biome_sp_weight(Biome b) {
    // The bed of unimproved ground, by biome id (Tundra..Water, Mountain).
    // Recalibrated 2026-08-24 with the sum law (owner: заново, not parity):
    // open walking country 2.0, hard country 2.5–3.0, bog 4.0, the mountain
    // ground itself 5.0 (its WALL is the climb term now, not the byte),
    // water 10.0 — unpayable on foot, the ocean still drowns a lord.
    // Each row carries its own enum as a COLUMN, so a grown Biome refuses to
    // compile instead of quietly walking on a neighbour's ground.
    struct BiomeBedRow { Biome biome; float weight; };
    constexpr BiomeBedRow kW[std::size_t(Mountain) + 1] = {
        {Tundra,  2.5f}, {Taiga,   2.5f}, {Snow,     3.0f}, {Valley, 2.0f},
        {Meadow,  2.0f}, {Swamp,   4.0f}, {Desert,   3.0f}, {Steppe, 2.0f},
        {Tropics, 2.5f}, {Water,  10.0f}, {Mountain, 5.0f},
    };
    static_assert(rows_in_enum_order(kW, &BiomeBedRow::biome),
                  "the biome bed table must mirror Biome");
    const std::size_t idx = std::size_t(b);
    return idx < std::size(kW) ? kW[idx].weight : 2.0f;
}

// The engineered beds — THE registry's column (macro/features.h kFeatureDefs;
// this was a switch with a silent 0.0 default until 2026-08-29). 0 = nothing
// built here — the biome ground is the bed (the silent zero of the law, not a
// sentinel to branch on).
inline constexpr float feature_bed_weight(FeatureType f) {
    return feature_def(f).bedWeight;
}

// ── THE anchor: the bar the whole ladder is measured against ──────────────
//
// The economy is BALANCED in game hours, and until 2026-09-09 the arithmetic
// joining the hours to the price lived in a comment — which cannot follow a
// moved knob. It failed exactly that way (kStaminaPerWeightHour's epitaph):
// two knobs moved, their product fell 1.75×, and every test stayed green
// because every test DERIVED its expectation from the same constants it was
// meant to be guarding. A tautology guards nothing.
//
// So the design numbers are stated here as literals, and the derived ones are
// asserted against them. `kFreshBarSp` is the bare level-1 bar — 100 base plus
// the untouched sheet's END/WIL pair (attributes.h calculate_combat_stats);
// squad_travel_test pins that it still reads 110, so this literal cannot drift
// away from the sheet in silence either.
inline constexpr float kFreshBarSp = 110.0f;

// ── ЖЖЕНИЕ — ВТОРОЙ ВСЕГДА-ВКЛЮЧЁННЫЙ ПРОЦЕСС ────────────────────────────
// Вердикт владельца 2026-10-06, дословно: «можно просто сделать реген от веса
// гладкую функцию … можно чтобы даже жгло сп всегда просто при движении реген
// откл … 2 процесса агностичных системных — реген который всегда одинаковый
// НЕ от веса … и жжение которое от веса».
//
// ЭТИМ УМИРАЕТ СТЕНА, И ЭТО ГЛАВНОЕ. До этого дня на вопрос «можно ли тут
// встать» отвечал ОТДЕЛЬНЫЙ предикат (`nav_can_stand`: по воде нельзя, кроме
// моста) — второй ответ на вопрос, на который уже отвечал ВЕС, — и он же
// держал 4328 клеток суши вне всякой округи навигации (остров без моста был
// недостижим не потому, что дорого, а потому, что запрещено). После смены
// вопроса нет вовсе: стоять можно где угодно, а смертельность места есть
// СЛЕДСТВИЕ двух процессов, идущих всегда. Предел наступает обратной связью,
// как и требует ЗАКОН КЛАМПА, а не стеной.
//
// ОДИН СКАЛЯР И ОДИН КВАНТ — ЧАС. Три члена — ровно те, что несла мёртвая
// `travel_stamina_cost`, минус `cells`: грунт под телом, НАВЫК, сбивающий
// цену грунта, и ПЕРЕГРУЗ, который навык не сбивает никогда (владелец
// 2026-08-27: «да, перегруз универсальный всем»). Переезд был вынужден:
// пошаговая цена была их единственным плательщиком, и оставить их без двери
// значило снять два стоячих закона молча.
inline constexpr float burn_stamina_per_hour(float cellWeight,
                                             int overloadCost = 0,
                                             float efficiency = 1.0f) {
    return cellWeight * kStaminaPerWeightHour * efficiency
           + float(overloadCost);
}

// ЛЕСТНИЦА ПРИБИТА КОМПИЛЯТОРОМ, А НЕ НАБЛЮДЕНИЕМ. Два конца таблицы весов
// задают её целиком, и между ними она выходит сама (реген 13.75 SP/ч на
// свежей планке): дорога и мост 1.0 → +11.75, луг 2.0 → +9.75, болото 4.0 →
// +5.75, полный лес 4.5 → +4.75, гора 5.0 → +3.75, вода 10.0 → −6.25.
static_assert(burn_stamina_per_hour(biome_sp_weight(Biome::Water))
                  > kFreshBarSp * kRestRegenPctPerHour,
              "ВОДА ОБЯЗАНА ТОПИТЬ: жжение больше отдыха. Иначе океан "
              "становится медленной, но БЕЗОПАСНОЙ дорогой, и закон "
              "«неоплатный океан топит лорда» (CANON S7) умирает молча");
static_assert(burn_stamina_per_hour(feature_bed_weight(FT_Road))
                  < kFreshBarSp * kRestRegenPctPerHour,
              "ДОРОГА ОБЯЗАНА ЛЕЧИТЬ: жжение меньше отдыха. Иначе "
              "отдохнуть нельзя нигде и мир встаёт");

// ДОЕЗЖАЕТ ЛИ ОТДЫХ ЗДЕСЬ ДО ЖЖЕНИЯ — ОДИН ВОПРОС, ОДНА ДВЕРЬ, ДВА ВОДИТЕЛЯ.
// Не «можно ли тут встать» (на это отвечает ВЕС, и ответ всегда «да»), а
// «стоит ли тут стоять»: окупается ли час привала его собственным часом
// жжения. Место в вопросе не названо ни разу — названы две ставки, которые у
// тела и так есть.
//
// ЗАЧЕМ ОТДЕЛЬНАЯ ДВЕРЬ, А НЕ ДВА СРАВНЕНИЯ НА МЕСТАХ: решение принимают ДВА
// водителя времени — свип макро-ИИ для сквадов и ход главного цикла для
// игрока (свип игрока не водит никогда), — и это один закон, а не дубль. Две
// рукописные копии сравнения разъехались бы первой же правкой ставки отдыха.
//
// И ЭТО НЕ ВОЗВРАТ СТЕНЫ, купленной сносом в тот же день. Стена ВЕТИРОВАЛА
// проход и привал по роду клетки; это ничего не запрещает — тело, которому
// стоянка невыгодна, идёт дальше и платит долг кровью («pressing on is
// priced, never gated», CANON S14.1). Без этого вопроса снос стены убил бы
// собственную цель: пробуждение требует ПОЛОВИНЫ планки, а там, где жжение
// обгоняет отдых, планка только падает — значит сквад, у которого ноги
// кончились в клетке от берега острова, встал бы лагерем В МОРЕ, не проснулся
// никогда и утонул на месте. То же «от любого места до любого не добраться»,
// только смертью вместо запрета.
inline float rest_stamina_per_hour(int maxSp, int marathonRank) {
    return float(maxSp > 1 ? maxSp : 1) * kRestRegenPctPerHour
           * skill_mult_of(SkillId::Marathon, marathonRank);
}
inline bool camp_repays_its_hour(float burnPerHour, int maxSp,
                                 int marathonRank) {
    return rest_stamina_per_hour(maxSp, marathonRank) > burnPerHour;
}

// ЧАСОВОЙ ЯКОРЬ НОВОГО ЗАКОНА — он заменил `kRoadHoursPerFreshBar`, и замена
// НЕ косметическая: прежний якорь мерил, сколько ДОРОГИ покупает планка, а
// дорога теперь дешевле отдыха, то есть её часы бесконечны и мерить там
// нечего. Мерить стало смысл у ХУДШЕГО конца лестницы: сколько планка держит
// в открытом море, где жжение честно обгоняет отдых. Это и есть то число,
// которым владелец балансирует «океан топит, но не стеной».
//
// 17.6 ч: 110 SP / (20 жжения − 13.75 отдыха). Дальше долг и квадратичный
// укус (exhaustion_bite ниже). Река в одну-две клетки стоит 8–16 SP из 110 —
// брод открыт всем и без моста, ровно как требует вердикт «вода — ВЕС».
inline constexpr float kWaterDriftHoursPerFreshBar =
    kFreshBarSp / (burn_stamina_per_hour(biome_sp_weight(Biome::Water))
                   - kFreshBarSp * kRestRegenPctPerHour);
static_assert(kWaterDriftHoursPerFreshBar > 8.0f
                  && kWaterDriftHoursPerFreshBar < 32.0f,
              "полная планка обязана держать в море ПОЧТИ СУТКИ, но не "
              "больше: меньше восьми часов — и море стена, больше суток — и "
              "оно безопасно. Двинули kStaminaPerWeightHour, вес воды или "
              "ставку отдыха — эта строка говорит об этом вслух");

// Full-thicket drag: at density 1.0 (kMaxTreesPerCell) the wood adds 2.5 on
// top of its ground — a meadow choked to full forest walks at 4.5, the old
// forest-class 3.0 sits near density ~0.4, which is what a typical massif
// interior actually carries.
inline constexpr float kCanopySpWeight = 2.5f;

// Climbing surcharge per full normalized height (h01 = field01_of the R word):
// an ascent over the WHOLE world relief weighs as much again as ten cells of
// open meadow (20 = 10 × meadow 2.0) — spread over however many cells the
// approach takes, and refunded by nothing on the way down.
//
// ROUTING ONLY SINCE 2026-10-06, and that is a consequence, not a decision: a
// climb is a fact of an EDGE, and the hour quantum has no edges. So this term
// no longer bills stamina anywhere — it prices edges for the things that CHOOSE
// edges (nav_bake's Dijkstra, find_path, the greedy step), which is where it
// does its real work: a mountain route stays dearer than the valley around it.
// Relief still costs the body, through the bed (mountain ground 5.0) and
// through terrain_speed_mult holding it there longer — 1.4 SP a cell against
// the road's 0.25.
inline constexpr float kClimbSpWeight = 20.0f;

// The CELL half of the law — bed + canopy — and the half the BURN reads. The
// climb half lives on the edge and is read only by the route-choosers:
//   edge cost = cell_sp_weight(to) × step + kClimbSpWeight × max(0, Δh01).
inline float cell_sp_weight(Biome b, FeatureType f, float treeDensity01 = 0.0f) {
    const float bed = feature_bed_weight(f);
    if (bed > 0.0f) return bed;             // an engineered bed is a CUT
    return biome_sp_weight(b) + kCanopySpWeight
               * (treeDensity01 < 0.0f ? 0.0f
                  : treeDensity01 > 1.0f ? 1.0f : treeDensity01);
}

// (No travel_stamina_cost. THE per-cell price died 2026-10-06 — see the file
// header: what a journey costs is the HOURS it spends on ground, and the one
// formula is burn_stamina_per_hour above. Its four callers — the squad's step,
// the player's macro cell, the player's subworld distance, and the UI preview —
// all went with it, and so did the standing wall that lived beside it.)

// THE bite, for a body of either scale (owner's rulings, 2026-08-27 +
// 2026-09-17): what one spend-in-debt takes, given the debt — QUADRATIC, the
// one law of zero (CANON S14.1). Zero while stamina lasts, so it can be
// asked unconditionally, and integer through and through.
//
// BILLED BY THE SPEND, NEVER BY THE STATE, and the owner named the defect that
// settles it (2026-10-07, дословно): «он привязан к трате — любая трата sp
// снимает также хп если оно отрицательно (квадратично) … иначе просто стоя на
// месте с нулём sp будут умирать». A state-billed bite would bleed a body
// standing on a road with an empty bar, where the regen outruns the burn and
// NOTHING is being spent — nonsense. In the open sea the burn outruns the
// regen, SP is spent every hour, and the bite follows the spend: that is how
// «ночёвка в море смертельна» works without naming water anywhere.
//
// ITS QUANTUM IS ONE POINT OF SP, and the quantum carries two properties at
// once. It is CADENCE-FREE: the continuous burn is settled by two drivers at
// two rates — the player's turn is 0.176 game minutes, a squad's think is
// 5.625, exactly 32× apart — so a bite charged PER CALL would have made the
// depth of the sea a function of who was walking in it. And it is AGNOSTIC
// ABOUT WHAT SPENT: ten points taken by a swing and ten points burned by a
// crossing are the same ten points. Until 2026-10-07 they were not — the act
// bit once at its final depth (3 HP) where the crossing bit each point at its
// own (12 HP) — and the owner asked for one law, plainly: «сделай просто
// красиво». One law, one quantum, one door (`bite_spent_debt` below).
//
// This used to be inlined in the player's charge and hand-copied in the macro
// AI's per-think settle, where it was also gated on WATER: a squad marching
// itself into the ground on dry meadow just made camp and paid nothing, while
// the player bled for the same step. One law, one line, both scales — march,
// labour and craft all bite here and nowhere else.
inline int exhaustion_bite(int sp) {
    if (sp >= 0) return 0;
    return (-sp) * (-sp) / kExhaustionBiteDivisor;
}

// THE BITE OF A SPEND — THE one door, for an hour of ground and for a swing of
// a sword alike. `before` is the bar as it stood before the spend, `pools.sp`
// as it stands after; every whole point the spend drove below zero is charged
// at ITS OWN depth, the quadratic curve integrated honestly instead of sampled
// once at the end.
//
// A RISING BAR BITES NOTHING, and that is the owner's own guard made
// structural: a body standing where rest outruns the burn spent nothing, so
// nothing may be taken from it («иначе просто стоя на месте с нулём sp будут
// умирать»). No branch is needed at any call site to say so.
inline int bite_spent_debt(ecs::Pools& pools, int before) {
    if (pools.sp >= before) return 0;        // the bar rose: nothing was spent
    const int from = before < 0 ? before : 0;   // only the part below zero
    int lost = 0;
    for (int sp = from - 1; sp >= pools.sp; --sp) lost += exhaustion_bite(sp);
    if (lost <= 0) return 0;
    pools.hp -= lost;
    return lost;
}

// THE DISCRETE SPEND — a swing, a cast, a day of labour — debited and billed
// through the SAME two lines an hour of ground goes through. Returns the HP
// lost (0 while stamina lasts).
//
// The body keeps its debt: stamina is NOT floored at zero, so the state is
// visible in the UI and has to be recovered before the bar refills.
//
// It bit ONCE at its final depth until 2026-10-07, and that was the last place
// in the game where the price of a point depended on WHAT spent it.
inline int apply_stamina_cost(ecs::Pools& pools, int cost) {
    if (cost <= 0) return 0;
    const int before = pools.sp;
    pools.sp -= cost;
    return bite_spent_debt(pools, before);
}

// THE fractional stamina carry, settled — one shape for every body on the map.
// SIGNED and BIDIRECTIONAL: a march pushes it down, a rest pushes it up, and
// whole points move to the bar in whichever direction they accumulated.
// Truncation is toward zero, so a part-point never rounds into existence.
//
// The player used to carry TWO of these, both unsigned and each blind to the
// other: a spend-only `TravelStamina::pending` that refused to act below 1.0,
// and a separate regen-only accumulator in PlayerRecoveryAccumulator that
// zeroed itself at a full bar. A macro squad carried one signed `spCarry` and
// did the same job with half the parts. Same idea, three implementations, and
// the player's pair could not even represent the state his own bar was in —
// an exhaustion debt with a fractional part owed.
//
// The bar clamps at `maxSp` going up and NOT at zero going down: the debt is
// the state the exhaustion law bills (exhaustion_bite above), so it has to be
// expressible. Returns the whole points moved — negative when spent.
inline int settle_sp_carry(int& sp, int maxSp, float& carry) {
    const int whole = int(carry);
    if (whole == 0) return 0;
    carry -= float(whole);
    sp = std::min(std::max(1, maxSp), sp + whole);
    return whole;
}

} // namespace sm

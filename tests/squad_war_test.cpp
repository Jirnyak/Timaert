// Squad↔squad perception and war on the macro map (Session 15, Inc 5).
//
// Before this, macro NPCs were ghosts to each other: no behaviour could see
// another squad, and two armies could share a cell for years. What is pinned
// here is the owner's design made live:
//   · a hostile squad NEARBY is perceived through the transient SquadIndex
//     and answered by the strength law — the weak flee, fighters pursue;
//   · a hostile squad ON THE SAME CELL is a battle, resolved by the one
//     auto-battle law and settled through the one ledger: creature rows die by
//     name, a fallen avatar is hp=0 + Dead, the victor takes the spoils and
//     the XP — and levels by the same curve the player climbs;
//   · neutral squads ignore each other, and the underground drive
//     (allowAutoBattle=false) perceives but does not resolve.
// Every fixture builds its relations explicitly: a world with no hostility
// rows must behave exactly as the world always did (the parity test guards
// that side).
#include "check.h"

#include "macro/npc_ai.h"
#include "macro/world_row.h"
#include "macro/npc_spawn.h"
#include "macro/squad.h"
#include "macro/place_birth.h"   // birth_place — фикстура рождает свой неподвижный сквад
#include "macro/player_entity.h"
#include "tables/faction.h"
#include "tables/npc.h"
#include "core/torus.h"
#include "macro/store.h"

#include <cstdint>
#include <initializer_list>

namespace {

using namespace sm;

constexpr int kMap = 64;

// Both avatars wear real registry factions; the fixture then writes the
// relation it wants between them — through the same ensure_faction_row door
// the game uses — so hostility is data here exactly as it is in the world.
GameState make_world(int relation) {
    GameState gs{};
    gs.mapW = kMap;
    gs.mapH = kMap;
    // One symmetric write through THE door: the two-line form could set one
    // direction and forget the other, which is exactly what the matrix's
    // set_relation makes impossible.
    set_relation(gs.factions, faction_index("bandits"),
                 faction_index("timaert"), relation);
    return gs;
}

sm::MacroHandle make_squad_at(sm::MacroStore& st, NPCType type,
                              const char* faction,
                              int level, float x, float y,
                              std::uint32_t ordinal,
                              std::initializer_list<std::uint32_t> memberIds,
                              NPCType memberKind, int memberLevel) {
    sm::MacroHandle h = sm::store_birth(st);
    st.cell[h.slot] = ecs::MacroCell{ecs::cell_index(int(x), int(y), kMap)};
    st.sub[h.slot] = {};   // доля клетки: фикстура рождает в центре (M-244a)
    st.kind[h.slot] = ecs::NPCKind{std::uint16_t(type),
                                   std::uint16_t(faction_index(faction))};
    ecs::MacroNpcRuntime rt{};
    // Цель новорождённого — «ничего» (kNoCell, колонку нулит рождение).
    rt.state = std::uint8_t(NPCState::Idle);
    const CharacterSheet sheet = make_character_sheet(
        type, level, avatar_sheet_seed(ordinal));
    const int hp = std::max(
        1, int(project_combat(sheet, npc_def(type).combat).hp));
    ecs::Pools pools{};
    pools.hp = pools.maxHp = hp;
    // same door make_npc uses — it caps the bar it is handed
    refresh_body_from_sheet(pools, &rt, sheet, type);
    pools.sp = pools.maxSp;               // rested: fatigue 1.0, as before
    st.pools[h.slot] = pools;
    st.runtime[h.slot] = rt;
    sm::store_rename_ordinal(st, h, ordinal);
    h = sm::handle_at(st, h.slot);
    // Уровень несёт АНКЕТА (M-246а): `sheet` построен из `level` выше.
    st.sheet[h.slot] = sheet;   // колонка листа — как make_npc рождает всем
    auto& bag = st.inventory[h.slot];
    for (std::uint32_t id : memberIds) {
        creatures_push(bag.inv,
                       make_soldier(std::uint8_t(memberKind), memberLevel, id));
    }
    return h;
}

int container_count(sm::MacroStore& st, GameState& gs, std::uint32_t ordinal) {
    MacroWorld mw{.gs = &gs, .store = &st};
    return macro_stock_read(mw, MacroStock::Creatures,
                            MacroStockKey{std::int32_t(ordinal), 0, 0});
}

float dist(sm::MacroStore& st, sm::MacroHandle a, sm::MacroHandle b) {
    const auto& ca = (*sm::body_state<ecs::MacroCell>(st, a));
    const auto& cb = (*sm::body_state<ecs::MacroCell>(st, b));
    return std::sqrt(torus_dist_sq(
        float(ecs::cell_x(ca, kMap)), float(ecs::cell_y(ca, kMap)),
        float(ecs::cell_x(cb, kMap)), float(ecs::cell_y(cb, kMap)),
        float(kMap), float(kMap)));
}

void drive(GameState& gs, sm::MacroStore& st, MacroNpcAiRuntime& rt, int thinks) {
    for (int i = 0; i < thinks; ++i) {
        MacroWorld mw{.gs = &gs, .store = &st};
        tick_macro_npc_ai(mw, rt, kAiTicks);
    }
}

// A geometric meeting of hostiles IS a battle, settled through the ledger.
void test_hostiles_on_one_cell_fight_and_the_ledger_pays() {
    GameState gs = make_world(-80);
    auto wStore_ = sm::make_macro_store();
    sm::MacroStore& st = *wStore_;
    // A strong bandit warband and a weak caravan, standing on the same cell.
    const auto bandit = make_squad_at(st, NPCType::Bandit, "bandits", 5,
                                      10.0f, 10.0f, 1u, {11u, 12u, 13u, 14u},
                                      NPCType::Bandit, 4);
    const auto caravan = make_squad_at(st, NPCType::Merchant, "timaert", 1,
                                       10.0f, 10.0f, 2u, {21u},
                                       NPCType::Peasant, 1);
    (*sm::body_state<ecs::NpcInventory>(st, caravan)).inv.add("wood", 5);

    MacroNpcAiRuntime rt{};
    reset_macro_npc_ai_runtime(rt, 42u);
    drive(gs, st, rt, 1);

    CHECK(container_count(st, gs, 2u) == 0,
          "the caravan's creatures died by name through the creature row");
    // The loser fell (hp=0 + Dead inside the settle) — and by CANON S4
    // («убили всех — сквада на карте нет», 2026-08-29) the end-of-tick sweep
    // then destroyed the drained corpse-row: a dead squad LEAVES the map.
    // 6.3: сквад ЕСТЬ слот store — «ушёл с карты» = смерть слота.
    CHECK(!st.valid(
              sm::handle_at(st,
                            caravan.slot)),
          "a loser whose whole creatures fell falls with it and leaves the map");
    CHECK(!sm::macro_dead(st, bandit),
          "the crushing winner survives");
    CHECK((*sm::body_state<ecs::NpcInventory>(st, bandit)).inv.count("wood") == 5,
          "the raid PAYS: the fallen owner's goods pass to the victor");
    // ОПЫТ ИДЁТ В АНКЕТУ, И ТОЛЬКО В НЕЁ (M-246а): ветка «или транзиентный
    // счётчик rt.xp» умерла вместе со вторым законом роста — у всякого
    // сквада рост один, через свой лист.
    {
        const CharacterSheet& own = *sm::body_state<CharacterSheet>(st, bandit);
        CHECK(own.levelData.exp > 0 || own.levelData.level > 5,
              "victory pays experience through the one reward law");
    }
}

// The weak run from strength they can see; fighters close in on prey.
void test_the_weak_flee_and_fighters_pursue() {
    GameState gs = make_world(-80);
    auto wStore_ = sm::make_macro_store();
    sm::MacroStore& st = *wStore_;
    const auto bandit = make_squad_at(st, NPCType::Bandit, "bandits", 6,
                                      10.0f, 10.0f, 1u, {11u, 12u, 13u, 14u},
                                      NPCType::Bandit, 5);
    const auto caravan = make_squad_at(st, NPCType::Merchant, "timaert", 1,
                                       14.0f, 10.0f, 2u, {},
                                       NPCType::Peasant, 1);
    // Freeze the bandits: resting with empty stamina, so the caravan's
    // flight is measured against a fixed threat.
    {
        (*sm::body_state<ecs::Pools>(st, bandit)).sp = 0;
        (*sm::body_state<ecs::MacroNpcRuntime>(st, bandit)).state =
            std::uint8_t(NPCState::Resting);
    }
    MacroNpcAiRuntime rt{};
    reset_macro_npc_ai_runtime(rt, 43u);
    const float before = dist(st, bandit, caravan);
    // Enough thinks for one whole CELL of flight at the derived budget
    // (kMacroWalkCellsPerHour × kAiTickGameHours per think — the pace is the
    // owner's data, the test never pins a literal): flee-the-state shows on
    // the first think, the opened distance on the first MOVING one.
    const int thinksPerCell = int(std::ceil(
        1.0f / (kMacroWalkCellsPerHour * kAiTickGameHours)));
    drive(gs, st, rt, thinksPerCell);
    CHECK((*sm::body_state<ecs::MacroNpcRuntime>(st, caravan)).state
              == std::uint8_t(NPCState::Fleeing),
          "a squad that cannot win runs - the strength law says so");
    CHECK(dist(st, bandit, caravan) > before,
          "and running away actually opens distance");

    // Now the mirror: freeze the caravan, wake the bandits - the fighter row
    // pursues the prey the same law says it beats.
    GameState gs2 = make_world(-80);
    auto w2Store_ = sm::make_macro_store();
    sm::MacroStore& st2 = *w2Store_;
    const auto hunter = make_squad_at(st2, NPCType::Bandit, "bandits", 6,
                                      10.0f, 10.0f, 1u, {11u, 12u, 13u, 14u},
                                      NPCType::Bandit, 5);
    const auto prey = make_squad_at(st2, NPCType::Merchant, "timaert", 1,
                                    15.0f, 10.0f, 2u, {},
                                    NPCType::Peasant, 1);
    {
        (*sm::body_state<ecs::Pools>(st2, prey)).sp = 0;
        (*sm::body_state<ecs::MacroNpcRuntime>(st2, prey)).state =
            std::uint8_t(NPCState::Resting);
    }
    MacroNpcAiRuntime rt2{};
    reset_macro_npc_ai_runtime(rt2, 44u);
    const float before2 = dist(st2, hunter, prey);
    // Same derived budget: enough thinks for one cell of pursuit — far too
    // few to close the whole gap and RESOLVE the battle, which is a
    // different law's test.
    drive(gs2, st2, rt2, thinksPerCell);
    CHECK((*sm::body_state<ecs::MacroNpcRuntime>(st2, hunter)).state
              == std::uint8_t(NPCState::Chasing),
          "a fighter row pursues prey the law says it beats");
    CHECK(dist(st2, hunter, prey) < before2,
          "and pursuit actually closes distance");
}

// Neutral squads pass each other exactly as they always did.
void test_neutral_squads_ignore_each_other() {
    GameState gs = make_world(0);
    auto wStore_ = sm::make_macro_store();
    sm::MacroStore& st = *wStore_;
    const auto a = make_squad_at(st, NPCType::Bandit, "bandits", 5,
                                 10.0f, 10.0f, 1u, {11u, 12u},
                                 NPCType::Bandit, 4);
    const auto b = make_squad_at(st, NPCType::Merchant, "timaert", 1,
                                 10.0f, 10.0f, 2u, {21u},
                                 NPCType::Peasant, 1);
    MacroNpcAiRuntime rt{};
    reset_macro_npc_ai_runtime(rt, 45u);
    drive(gs, st, rt, 3);
    CHECK(!sm::macro_dead(st, a) && !sm::macro_dead(st, b),
          "no relation below the line, no war - hostility is data");
    CHECK(container_count(st, gs, 1u) == 2 && container_count(st, gs, 2u) == 1,
          "nobody's creatures paid for a meeting of neutrals");
}

// ИЗЪЯТИЕ ВСТРЕЧИ АДРЕСУЕТСЯ КЛЕТКАМИ, А НЕ РОДОМ СЦЕНЫ (M-226, вердикт
// владельца 2026-10-06: «это плохо нельзя отклюячать автобой во всём мире
// толкьо в этих 3х3 клетках»).
//
// Здесь стоял свидетель ГЛОБАЛЬНОГО ФЛАГА: он водил думку с
// `allowAutoBattle=false` и утверждал «с выключенным резолвером встреча не
// решается». Флаг умер, и вместе с ним умер вопрос, на который тот
// свидетель отвечал: выключать резолвер во всём мире больше нечем.
//
// Новый свидетель судит ОБЕ полярности одной аренды — это и есть весь смысл
// правки: клетка встречи в аренде ⇒ наверху не решается (её ведёт субмир);
// аренда на ДРУГОЙ клетке ⇒ мир дерётся как всегда. Вторая половина до
// M-226 была невыразима: пока игрок внизу, не дрался НИКТО.
void test_a_meeting_in_the_leased_window_is_not_resolved_upstairs() {
    // ── ПОЛЯРНОСТЬ 1: встреча НА арендованной клетке ────────────────────
    {
        GameState gs = make_world(-80);
        auto wStore_ = sm::make_macro_store();
        sm::MacroStore& st = *wStore_;
        const auto a = make_squad_at(st, NPCType::Bandit, "bandits", 5,
                                     10.0f, 10.0f, 1u, {11u, 12u},
                                     NPCType::Bandit, 4);
        const auto b = make_squad_at(st, NPCType::Merchant, "timaert", 1,
                                     10.0f, 10.0f, 2u, {21u},
                                     NPCType::Peasant, 1);
        MacroNpcAiRuntime rt{};
        reset_macro_npc_ai_runtime(rt, 46u);
        // Свидетель РОЖДАЕТ своё предусловие сам (AGENTS §8 п.11): аренда
        // сдаётся дверью мира, а не выставлением поля руками.
        //
        // ЦЕНТР СТОИТ НА СОСЕДЕ, А НЕ НА САМОЙ КЛЕТКЕ ВСТРЕЧИ — и это не
        // придирка: с центром на (10,10) мутация «рамки нет, только центр»
        // прошла бы ЗЕЛЁНОЙ (проверено прогоном). Пинится РАМКА, то есть
        // `cell_step` по румбам, а не совпадение с центром.
        sm::lease_window_at(gs, sm::cell_of(11, 9, gs.mapW));
        CHECK(sm::cell_is_leased(gs, sm::cell_of(10, 10, gs.mapW))
                  && gs.leasedWindow != sm::cell_of(10, 10, gs.mapW),
              "фикстура: клетка встречи в арендованной РАМКЕ, но не центр");
        drive(gs, st, rt, 3);
        CHECK(!sm::macro_dead(st, a)
                  && !sm::macro_dead(st, b)
                  && container_count(st, gs, 2u) == 1,
              "встреча на арендованной клетке наверху НЕ решается: её ведёт "
              "субмир, и второго писателя быть не должно");
    }
    // ── ПОЛЯРНОСТЬ 2: та же встреча, аренда ЗА рамкой ───────────────────
    // Это и есть выигрыш правки: мир за девятью клетками дерётся, пока
    // игрок внизу. До M-226 здесь не дрался никто.
    {
        GameState gs = make_world(-80);
        auto wStore_ = sm::make_macro_store();
        sm::MacroStore& st = *wStore_;
        const auto a = make_squad_at(st, NPCType::Bandit, "bandits", 5,
                                     10.0f, 10.0f, 1u, {11u, 12u},
                                     NPCType::Bandit, 4);
        const auto b = make_squad_at(st, NPCType::Merchant, "timaert", 1,
                                     10.0f, 10.0f, 2u, {21u},
                                     NPCType::Peasant, 1);
        MacroNpcAiRuntime rt{};
        reset_macro_npc_ai_runtime(rt, 46u);
        // Две клетки от места встречи — вне рамки 3×3 по построению.
        sm::lease_window_at(gs, sm::cell_of(14, 14, gs.mapW));
        CHECK(!sm::cell_is_leased(gs, sm::cell_of(10, 10, gs.mapW)),
              "фикстура: клетка встречи вне арендованной рамки");
        drive(gs, st, rt, 3);
        CHECK(sm::macro_dead(st, b)
                  || container_count(st, gs, 2u) == 0,
              "за рамкой мир дерётся, пока игрок внизу: купец пал или его "
              "состав выбит");
        (void)a;
    }
}

// The wandering-tsar seed: any avatar that wins fights levels by the same
// curve the player climbs - a data row, not a branch. Battle XP inflow is
// pinned by the first test; here the CONSUMPTION law: enough experience
// turns into a level and a bigger macro ceiling, wounds preserved as the
// fraction they always travel in.
void test_a_victorious_avatar_levels() {
    GameState gs = make_world(-80);
    auto wStore_ = sm::make_macro_store();
    sm::MacroStore& st = *wStore_;
    const auto tsar = make_squad_at(st, NPCType::Peasant, "timaert", 1,
                                    10.0f, 10.0f, 1u, {}, NPCType::Peasant, 1);
    auto& hp = (*sm::body_state<ecs::Pools>(st, tsar));
    const int maxHp0 = hp.maxHp;
    hp.hp = hp.maxHp / 2;   // walks in wounded
    const float frac0 = float(hp.hp) / float(hp.maxHp);

    CHECK(squad_award_exp(st, tsar, exp_to_next_level(1) / 2) == 0,
          "half a bar is not a level");
    CHECK(squad_award_exp(st, tsar, exp_to_next_level(1)) >= 1,
          "a full bar turns into a level by the player's own curve");
    CHECK(sm::squad_level(st, tsar.slot) >= 2,
          "the level landed on the avatar — В ЕГО АНКЕТЕ (M-246а)");
    const auto& hp1 = (*sm::body_state<ecs::Pools>(st, tsar));
    // Under the 1:1 economy a single level pays ONE attribute point, and the
    // role's weighted roll may legally land it outside END/Bodybuilding — so
    // the honest claim is re-derivation (never stale, never smaller), not
    // unconditional growth; the wound-fraction check below is what proves the
    // ceiling was actually recomputed across the level.
    CHECK(hp1.maxHp >= maxHp0,
          "the macro ceiling re-derives with the level and never shrinks");
    const float frac1 = float(hp1.hp) / float(hp1.maxHp);
    CHECK(std::fabs(frac1 - frac0) < 0.1f,
          "the wound crossed the level-up as a fraction, not a free heal");
}

// ── РОСТ НЕ РАЗДЕВАЕТ (M-301) ────────────────────────────────────────────
// Эффективный лист несёт ячейки АТРИБУТОВ и РАНГОВ надетого, а ПРОИЗВОДНЫЕ
// ячейки (`MovePct`, `CarryKg` — хвост реестра бонусов) дверь листа
// накладывает отдельно, четвёртым аргументом. До 2026-10-21 `award_kill_xp`
// звала дверь БЕЗ него, поэтому уровень-ап стирал надетую спину из колонки
// `carryPerSoulG` и надетую скорость из `moveMult`; возвращались они только
// переодеванием, а полл-гейт главного цикла этого не ловит по построению —
// он сторожит ИЗМЕНЕНИЕ суммы надетого, а на уровне-апе она не менялась.
//
// СВИДЕТЕЛЬ РОЖДАЕТ СВОЁ ПРЕДУСЛОВИЕ (§8 п.11): плащ кладётся в сумку и
// НАДЕВАЕТСЯ здесь же, через прод-дверь `equip`, а не в надежде, что
// генератор выдал одетый сквад. Отказ двери — честный провал фикстуры.
void test_a_level_does_not_strip_what_is_worn() {
    // Мира этому свидетелю не нужно вовсе: `squad_award_exp` читает и пишет
    // ТОЛЬКО стор, и фикстура рождает сквад прямо в нём.
    auto wStore_ = sm::make_macro_store();
    sm::MacroStore& st = *wStore_;
    const auto mule = make_squad_at(st, NPCType::Peasant, "timaert", 1,
                                    10.0f, 10.0f, 1u, {}, NPCType::Peasant, 1);

    // Ячейки тела — от строки сквада, как их ставит рождение (`gear_init`);
    // фикстура выше строит запись руками и до них не доходит.
    sm::Gear& gear = st.gear[mule.slot].gear;
    sm::gear_init(gear, sm::npc_def(NPCType::Peasant).slots);
    sm::Inventory& bag = st.inventory[mule.slot].inv;

    // Плащ с ПРОИЗВОДНЫМ аффиксом — ровно тот род ячейки, который стирался.
    // 40 кг взяты числом, которое ни с чем не спутать и которое заведомо
    // вмещается в `int16` значения аффикса.
    constexpr int kWornKg = 40;
    sm::ItemRef coat{};
    coat.def = std::uint16_t(sm::item_index("arm_leather"));
    coat.count = 1;
    coat.set_affix(0, sm::Bonus{std::uint8_t(sm::BonusId::CarryKg), kWornKg});
    CHECK(bag.add_ref(coat), "фикстура: плащ лёг в сумку");
    int coatSlot = -1;
    for (int i = 0; i < sm::kMaxInventorySlots; ++i) {
        if (!bag.slots[std::size_t(i)].empty()) { coatSlot = i; break; }
    }
    CHECK(coatSlot >= 0 && sm::equip(gear, bag, coatSlot) >= 0,
          "фикстура: тело ПРИНЯЛО плащ — иначе надетого нет и мерить нечего");
    CHECK(sm::standing_bonuses_of(st, mule)
                  .derived_of(sm::DerivedModId::CarryKg) == kWornKg,
          "фикстура честна: надетое действительно даёт производную ячейку");

    // Колонка до роста — через ту же дверь, что зовёт весь мир.
    const sm::BonusTotals standing0 = sm::standing_bonuses_of(st, mule);
    sm::refresh_body_from_sheet(st.pools[mule.slot], &st.runtime[mule.slot],
                                sm::effective_sheet_of(st, mule),
                                NPCType::Peasant, &standing0);

    CHECK(squad_award_exp(st, mule, exp_to_next_level(1) * 3) >= 1,
          "фикстура: опыта хватило на уровень");

    // ЯКОРЬ НЕЗАВИСИМЫЙ: колонка обязана быть РОВНО на 40 000 г выше голой
    // спины той же анкеты. Это утверждение об ОТНОШЕНИИ («надетое внутри»),
    // а не пересчёт продакшен-формулы: голая спина берётся у двери БЕЗ
    // надетого, и разница между ними и есть то, что стиралось. Голая спина
    // перечитывается ПОСЛЕ роста — уровень мог поднять Str или Weightlifting.
    const sm::CharacterSheet& own = st.sheet[mule.slot];
    const std::int32_t bareG =
        sm::get_carry_capacity(own.attributes, own.skills);
    const std::int32_t columnG = st.runtime[mule.slot].carryPerSoulG;
    CHECK(columnG == bareG + kWornKg * 1000,
          "рост НЕ раздевает: надетые 40 кг остались в спине анкеты и после "
          "уровня-апа (M-301 — дверь зовётся с `standing`)");
    // НЕГАТИВНЫЙ КОНТРОЛЬ САМОГО ДЕТЕКТОРА: без надетого эти два числа
    // СОВПАЛИ БЫ, и утверждение выше прошло бы при любом дефекте. Разница
    // обязана быть ненулевой, иначе детектор слеп.
    CHECK(kWornKg * 1000 > 0 && columnG > bareG,
          "негативный контроль: разница голой и одетой спины НЕ нулевая, "
          "значит утверждение выше способно покраснеть");
}

// ── ОДНО НАДЕВАНИЕ, БЕЗ УРОВНЯ-АПА, ОСВЕЖАЕТ ПРОИЗВОДНЫЕ (M-301) ─────────
// ЭТОТ СВИДЕТЕЛЬ ДЕРЖИТ ДЫРУ, КОТОРУЮ ДО M-301 НАКРЫВАЛИ ДВЕ СТРАХОВКИ, И
// ИМЕННО ПОЭТОМУ ЕГО НЕ СУЩЕСТВОВАЛО. Ни один из пяти прод-сайтов надевания
// не звал пересчёт; свежесть держали сторож главного цикла (сравнивал сумму
// надетого каждый тик) и безусловный пересчёт листа каждый макро-тик в хвосте
// `ensure_macro_player_entity`. Обе снесены, пересчёт стоит внутри
// `squad_equip@src/macro/squad.h` — и спросить это можно только отсюда:
// `ctest` и паритет мира не видят ни сторожа, ни UI-пути (§8 п.6, пятая форма
// слепоты), а макро-сквады в проде не носят ничего вовсе, поэтому дефект
// пришёл бы первым надетым НПЦ-плащом молча.
//
// ТАБЛИЦА «МУТАЦИЯ → ИСХОД», ПРОГНАНА, ОБЕ ПОЛЯРНОСТИ (§8 п.6):
//   М1  `refresh_squad_body(st, h)` снят из `squad_equip`   → КРАСНЫЙ 2 из 77
//   М2  пересчёт снят из `squad_unequip`                    → КРАСНЫЙ 1 из 77
//   М3  (ОБЯЗАНА МОЛЧАТЬ) пересчёт позван ДВАЖДЫ            → ЗЕЛЁНЫЙ 77
//   М4  (ОБЯЗАНА МОЛЧАТЬ) пересчёт и при ОТКАЗЕ двери       → ЗЕЛЁНЫЙ 77
// М3 и М4 обязаны молчать потому, что пересчёт ИДЕМПОТЕНТЕН: дверь листа
// сама гейтится по сдвигу потолка, значит лишний звонок не пишет ни байта.
// Детектор, краснеющий и на них, был бы не детектором, а пином реализации.
void test_wearing_alone_refreshes_the_derived_columns() {
    auto wStore_ = sm::make_macro_store();
    sm::MacroStore& st = *wStore_;
    const auto mule = make_squad_at(st, NPCType::Peasant, "timaert", 1,
                                    10.0f, 10.0f, 1u, {}, NPCType::Peasant, 1);
    sm::Gear& gear = st.gear[mule.slot].gear;
    sm::gear_init(gear, sm::npc_def(NPCType::Peasant).slots);
    sm::Inventory& bag = st.inventory[mule.slot].inv;

    // ГОЛАЯ спина — через ту же дверь, которой мир ставит колонку. Это
    // предусловие, а не утверждение: оно рождается здесь же (§8 п.11).
    sm::refresh_squad_body(st, mule);
    const std::int32_t bareColumn = st.runtime[mule.slot].carryPerSoulG;
    const sm::CharacterSheet& own = st.sheet[mule.slot];
    CHECK(bareColumn == sm::get_carry_capacity(own.attributes, own.skills),
          "фикстура: голая колонка равна голой спине анкеты");

    constexpr int kWornKg = 40;
    sm::ItemRef coat{};
    coat.def = std::uint16_t(sm::item_index("arm_leather"));
    coat.count = 1;
    coat.set_affix(0, sm::Bonus{std::uint8_t(sm::BonusId::CarryKg), kWornKg});
    CHECK(bag.add_ref(coat), "фикстура: плащ лёг в сумку");
    int coatSlot = -1;
    for (int i = 0; i < sm::kMaxInventorySlots; ++i) {
        if (!bag.slots[std::size_t(i)].empty()) { coatSlot = i; break; }
    }
    CHECK_OR_RETURN(coatSlot >= 0, "фикстура: слот плаща найден");

    // НИ УРОВНЯ, НИ ТИКА, НИ СТОРОЖА — ровно одно надевание прод-дверью.
    const int cell = sm::squad_equip(st, mule, coatSlot);
    CHECK_OR_RETURN(cell >= 0,
                    "фикстура: тело ПРИНЯЛО плащ — иначе мерить нечего");

    // ЯКОРЬ НЕЗАВИСИМЫЙ (§8 п.6, шестая форма): утверждение об ОТНОШЕНИИ —
    // колонка обязана стоять РОВНО на 40 000 г выше голой, — а не пересчёт
    // продакшен-формулы. Голая спина перечитывается у двери БЕЗ надетого.
    const std::int32_t wornColumn = st.runtime[mule.slot].carryPerSoulG;
    CHECK(wornColumn == sm::get_carry_capacity(own.attributes, own.skills)
                            + kWornKg * 1000,
          "надевание САМО освежило спину анкеты — пересчёт внутри двери");
    CHECK(wornColumn > bareColumn,
          "негативный контроль: колонка СДВИНУЛАСЬ, значит утверждение выше "
          "способно покраснеть (без сдвига оно сошлось бы на старом числе)");

    // И ОБРАТНО: снятие тоже есть изменение анкеты, и своя дверь у него своя.
    CHECK_OR_RETURN(sm::squad_unequip(st, mule, cell),
                    "фикстура: плащ снялся");
    CHECK(st.runtime[mule.slot].carryPerSoulG == bareColumn,
          "снятие САМО вернуло спину к голой — у обеих половин класса "
          "«надевание» пересчёт внутри двери");
}


// The player's auto-resolve settles through the SAME halves (Inc 6): the
// enemy through the ledger and the tracked-death shape, the player where his
// truth lives — army rows by name, the wound fraction into his squad Pools, XP
// by award_exp. Outcomes are CRAFTED here: this pins the settling law, not
// the resolver's dice (those have their own tests).
void test_player_auto_resolve_settles_through_the_same_doors() {
    GameState gs = make_world(-80);
    auto wStore_ = sm::make_macro_store();
    sm::MacroStore& st = *wStore_;
    // The player's men are a creatures on his own SQUAD ENTITY now — the same
    // shape the enemy lord below has, settled through the same doors.
    ensure_macro_player_entity(gs, st);
    Inventory* army = player_inventory(gs, *wStore_);
    creatures_push(*army, make_soldier(std::uint8_t(NPCType::Guard), 3, 501u));
    creatures_push(*army, make_soldier(std::uint8_t(NPCType::Guard), 3, 502u));
    player_pools(gs, *wStore_)->maxHp = 100;
    player_pools(gs, *wStore_)->hp = 100;
    const int level0 = sm::player_sheet(gs, *wStore_)->levelData.level;
    const int exp0 = sm::player_sheet(gs, *wStore_)->levelData.exp;
    const auto enemy = make_squad_at(st, NPCType::Bandit, "bandits", 3,
                                     10.0f, 10.0f, 9u, {31u, 32u},
                                     NPCType::Bandit, 2);
    (*sm::body_state<ecs::NpcInventory>(st, enemy)).inv.add("wood", 4);

    // The player WINS: one of his men fell, he limps out at 60%; the enemy
    // is wiped — creatures and avatar both.
    AutoBattleOutcome win{};
    win.winner = 0;                       // player is side A
    win.casualtiesA = {make_soldier(std::uint8_t(NPCType::Guard), 3, 501u)};
    win.casualtiesB = {make_soldier(std::uint8_t(NPCType::Bandit), 2, 31u),
                       make_soldier(std::uint8_t(NPCType::Bandit), 2, 32u)};
    win.avatarFractionA = 0.6f;
    win.avatarFractionB = 0.0f;
    // The settle takes THE envelope now (damage-door Inc 6): it needs the
    // world's own layers to price the spoils, and its facts leave through
    // the envelope's channel. A fixture that plugs in no channel is the
    // honest headless case.
    MacroWorld mw{}; mw.gs = &gs; mw.store = &st;
    const int xp = settle_player_auto_battle(mw, enemy, win,
                                             /*playerIsA*/true);
    CHECK(creature_count(*army) == 1
              && army->slots[std::size_t(army->creature_first())].entityId
                     == 502u,
          "the player's fallen soldier left the army by name");
    CHECK(player_pools(gs, *wStore_)->hp == 60,
          "the player's wound landed as the fraction, in THE store — his squad's Pools");
    CHECK(container_count(st, gs, 9u) == 0 && sm::macro_dead(st, enemy),
          "the enemy died through the ledger and the tracked-death shape");
    CHECK(player_inventory(gs, *wStore_)->count("wood") == 4,
          "the fallen owner's goods landed in the player's own bag");
    CHECK(xp > 0
              && (sm::player_sheet(gs, *wStore_)->levelData.exp > exp0
                  || sm::player_sheet(gs, *wStore_)->levelData.level > level0),
          "victory paid the player experience through award_exp");

    // The player LOSES with a man still standing: wounded, never dead — the
    // avatar rule holds for the player exactly as for any lord.
    GameState gs2 = make_world(-80);
    auto w2Store_ = sm::make_macro_store();
    sm::MacroStore& st2 = *w2Store_;
    ensure_macro_player_entity(gs2, st2);
    Inventory* army2 = player_inventory(gs2, *w2Store_);
    creatures_push(*army2,
                   make_soldier(std::uint8_t(NPCType::Guard), 3, 601u));
    creatures_push(*army2,
                   make_soldier(std::uint8_t(NPCType::Guard), 3, 602u));
    player_pools(gs2, *w2Store_)->maxHp = 100;
    player_pools(gs2, *w2Store_)->hp = 100;
    const auto victor = make_squad_at(st2, NPCType::Bandit, "bandits", 6,
                                      10.0f, 10.0f, 9u, {41u},
                                      NPCType::Bandit, 5);
    AutoBattleOutcome loss{};
    loss.winner = 1;
    loss.casualtiesA = {                  // one of two fell — not a wipe
        make_soldier(std::uint8_t(NPCType::Guard), 3, 601u)};
    loss.avatarFractionA = 0.15f;
    loss.avatarFractionB = 0.9f;
    MacroWorld mw2{}; mw2.gs = &gs2; mw2.store = &st2;
    settle_player_auto_battle(mw2, victor, loss, /*playerIsA*/true);
    CHECK(creature_count(*army2) == 1,
          "defeat took the fallen and left the survivor");
    CHECK(player_pools(gs2, *w2Store_)->hp >= 1,
          "while one of his men stands, defeat wounds the player - "
          "never kills him");
    CHECK(!sm::macro_dead(st2, victor),
          "the victor rides on");
}

// Squad creation as data (Inc 7): one spec, one door. The avatar comes out
// of make_npc whole (the ONE creation path), the creatures and the route are
// fields of the spec — and the ROUTE'S PRESENCE overrides the type row's ai
// (owner's ruling: one knob), which is what makes a player patrol a data
// row and not a code path.
void test_spawn_squad_is_one_spec_one_door() {
    GameState gs = make_world(0);
    gs.worldSeed = 777u;
    TerrainData absent{};
    absent.width = 0;
    absent.height = 0;
    auto wStore_ = sm::make_macro_store();

    SquadSpec spec{};
    // A Peasant row's ai is HomeWanderer with no home (homeId -1 does
    // nothing) — so any march this squad makes is the ROUTE acting.
    spec.avatarType = NPCType::Peasant;
    spec.avatarLevel = 4;
    spec.x = 20;
    spec.y = 20;
    spec.factionIndex = faction_index("timaert");
    spec.members.push(make_soldier(
        std::uint8_t(NPCType::Guard), 3, 0x40000001u));
    spec.members.push(make_soldier(
        std::uint8_t(NPCType::Guard), 3, 0x40000002u));
    spec.waypointCount = 2;
    spec.waypoints[0] = 24; spec.waypoints[1] = 20;   // 4 cells east
    spec.waypoints[2] = 20; spec.waypoints[3] = 20;   // and back

    sm::MacroStore& st = *wStore_;
    const MacroHandle avatar = spawn_squad(gs, st, absent, spec);
    CHECK_OR_RETURN(st.valid(avatar),
                    "the spec became a squad (одна дверь рождения отвечает "
                    "хэндлом — слот store несёт все колонки по построению)");
    CHECK(sm::squad_level(st, avatar.slot) == 4,
          "the spec's level pinned the avatar's level — в анкете (M-246а)");
    CHECK(creature_count(st.inventory[avatar.slot].inv) == 2,
          "the creature rows are the spec's rows");
    CHECK(st.orders[avatar.slot].waypointCount == 2,
          "the route landed as data on the squad");

    MacroNpcAiRuntime rt{};
    reset_macro_npc_ai_runtime(rt, 50u);
    const float x0 = float(ecs::cell_x(st.cell[avatar.slot], kMap));
    drive(gs, st, rt, 3);
    const float p1x = float(ecs::cell_x(st.cell[avatar.slot], kMap));
    CHECK(p1x > x0,
          "waypoint orders MARCH the squad east toward its route - the "
          "override, not the row, is steering");

    // Reaching a waypoint advances the route.
    for (int i = 0; i < 20; ++i) drive(gs, st, rt, 1);
    CHECK(st.orders[avatar.slot].currentWaypoint != 0
              || float(ecs::cell_x(st.cell[avatar.slot], kMap)) < 23.0f,
          "the route advances at a reached waypoint (or is already homing "
          "back on the second leg)");

    // A second squad continues the ordinal line — two squads, two names.
    SquadSpec other = spec;
    other.x = 40;
    const MacroHandle second = spawn_squad(gs, st, absent, other);
    CHECK_OR_RETURN(st.valid(second), "the second squad spawned");
    CHECK(st.spawnId[second.slot].index != st.spawnId[avatar.slot].index,
          "each created squad gets its own save-stable ordinal");

    // ── ПРИКАЗ РУКОЙ ХОДИТ ТОЙ ЖЕ ДВЕРЬЮ, ЧТО ЧИТАЕТ МИР (M-136) ─────────
    // Охраняемый закон: у приказа ОДИН дом (DOD п.6). Консоль до 2026-09-26
    // писала entt-компоненту `ecs::SquadOrders`, которую не читает никто:
    // `body_state` первой же ветвью отвечает КОЛОНКОЙ всякому носителю
    // `MacroSlot`. Свидетель спрашивает ровно то, что команда обещает в своём
    // выводе, — и спрашивает у ЧИТАТЕЛЯ МИРА, а не у писателя. Марш по
    // маршруту утверждён выше (маршрут из спеки), здесь он не переутверждается
    // сознательно: эта секция про ДОМ приказа, не про ноги.
    const std::uint32_t ord = st.spawnId[avatar.slot].index;
    CHECK(sm::order_squad_route(st, ord, ecs::SquadOrders{}),
          "дверь нашла сквад по его ординалу");
    CHECK(st.orders[avatar.slot].waypointCount == 0,
          "«released to its own life» видно ЧИТАТЕЛЮ: колонка пуста");
    ecs::SquadOrders hand{};
    hand.waypointCount = 2;
    hand.waypoints[0] = 31; hand.waypoints[1] = 29;
    hand.waypoints[2] = 20; hand.waypoints[3] = 20;
    CHECK(sm::order_squad_route(st, ord, hand), "приказ рукой принят");
    const auto& live = st.orders[avatar.slot];
    CHECK(live.waypointCount == 2 && live.waypoints[0] == 31
              && live.waypoints[1] == 29 && live.waypoints[2] == 20,
          "«now patrols 2 waypoint(s)» — те самые клетки в колонке мира");
    // Негативный контроль: ординала 0xFFFFFFFF в мире нет (эмитент
    // монотонный и начинается с нуля), и отказ обязан быть ПОЛНЫМ — дверь
    // не трогает ничей приказ, промахнувшись адресом.
    CHECK(!sm::order_squad_route(st, 0xFFFFFFFFu, ecs::SquadOrders{}),
          "негативный контроль: неизвестный ординал получает отказ");
    CHECK(st.orders[avatar.slot].waypointCount == 2,
          "...и отказ ничего не переписал");
}

} // namespace

// ── Phase 6: the avatar's TRAINING reads at the three new doors ──────────
// Trade → the price law's bargaining argument (a literal 0 since the law
// was written), Scouting → the runtime's sight cache. Each with a negative
// control. (Третьей дверью здесь стоял «харч по рангу фуражирования» — она
// умерла 2026-09-22 вместе со скидкой, а сам навык 2026-10-12, M-144. Блок 3
// ниже охраняет то, что встало на её место: лист ведущего счёт НЕ меняет.)
void test_the_avatars_training_reads_at_the_new_doors() {
    // 1. The scouting rank rides the runtime cache through the ONE refresh
    // door — the same door the march caches cannot drift through.
    {
        ecs::MacroNpcRuntime rt{};
        ecs::Pools pools{};
        CharacterSheet trained{};
        trained.skills[SkillId::Scouting] = 25;
        refresh_body_from_sheet(pools, &rt, trained, NPCType::Bandit);
        CHECK(rt.scoutRank == 25, "the scouting rank rides the runtime cache");
        CharacterSheet bare{};
        refresh_body_from_sheet(pools, &rt, bare, NPCType::Bandit);
        CHECK(rt.scoutRank == 0,
              "negative control: a rankless sheet clears the cache");
    }
    // 2. Торговая сила ЖИВА в законе цены — и она ДВУСТОРОННЯЯ (S25):
    //    считается разница анкет, поэтому «натренированный» выигрывает
    //    ровно настолько, насколько превосходит того, с кем торгует.
    {
        CHECK(trade_buy_price(1000, 50, 0) < trade_buy_price(1000, 0, 0),
              "перевес торговой силы покупает дешевле — один закон цены");
        CHECK(trade_sell_price(1000, 50, 0) > trade_sell_price(1000, 0, 0),
              "и продаёт дороже тем же законом");
        CHECK(trade_buy_price(1000, 50, 50) == trade_sell_price(1000, 50, 50)
                  && trade_buy_price(1000, 50, 50) == 1000,
              "равные анкеты: наценки нет вовсе, у сделки нет «дома»");
    }
    // 3. СЧЁТ ХАРЧА — СУММА РАЦИОНОВ СТРОК, И НИЧЕГО БОЛЬШЕ (2026-09-22).
    // Здесь стоял свидетель СКИДКИ ФУРАЖИРА («a trained forager's camp lives
    // partly off the land»). Вердикт владельца снял её вместе с половинной
    // ставкой у мест: «один закон без исключений». Тест переписан не в
    // «проверку отсутствия», а в свидетеля ДЕЙСТВУЮЩЕГО закона, и у него
    // два конца:
    //   · лист ведущего на счёт НЕ ВЛИЯЕТ — богатый лист сорокового уровня и
    //     бедный первого тянут ОДНО И ТО ЖЕ (прежде этот CHECK был бы
    //     красным). До 2026-10-12 носителем этого конца был ранг
    //     фуражирования; навык снесён M-144, закон жив, носителем стал ВЕСЬ
    //     лист — и прибор перестал зависеть от везения генератора;
    //   · рот объявляет КОЛОНКА РАЦИОНА строки, а не колонка платы: зверь с
    //     `kNpcUpkeepNone` ест наравне с людьми. До этой правки он в поле не
    //     ел вовсе, хотя у места ел — то самое расхождение двух копий.
    {
        GameState gs = make_world(0);
        auto wStore_ = sm::make_macro_store();
        sm::MacroStore& st = *wStore_;
        // СВИДЕТЕЛЬ РОЖДАЕТ СВОЁ ПРЕДУСЛОВИЕ САМ (§8 п.11), и это сильнее
        // прежней формы: здесь стоял СКАН по сидам в поисках ведущего с
        // ненулевым рангом фуражирования, то есть прибор зависел от того,
        // повезёт ли генератору. Навык снесён 2026-10-12 (M-144), а ЗАКОН,
        // который тот скан обслуживал, ЖИВ дословно — «лист ведущего счёт НЕ
        // МЕНЯЕТ» (вердикт 2026-09-22: один закон без исключений), — поэтому
        // утверждение перевешено с одной мёртвой колонки на ВЕСЬ ЛИСТ:
        // сороковой уровень против первого, и оба тянут одно и то же.
        const int kRichLevel = 40, kPoorLevel = 1;
        const sm::MacroHandle rich = make_squad_at(
            st, NPCType::Peasant, "timaert", kRichLevel, 10.0f, 10.0f, 1u,
            {1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u}, NPCType::Peasant, 1);
        // РОСТЕР ОБОИХ — ОДИН РОД: иначе менялись бы ДВЕ величины сразу (лист
        // ведущего и рацион состава), и равенство ниже ничего бы не доказало.
        const sm::MacroHandle poor = make_squad_at(
            st, NPCType::Guard, "timaert", kPoorLevel, 20.0f, 20.0f, 999u,
            {11u, 12u, 13u, 14u, 15u, 16u, 17u, 18u}, NPCType::Peasant, 1);
        // ЗВЕРЬ В КОНТЕЙНЕРЕ — ТРЕТИЙ ЛАГЕРЬ: его строка несёт kNpcUpkeepNone,
        // то есть жалованья он не берёт, а рацион по своей строке — берёт.
        const sm::MacroHandle beasts = make_squad_at(
            st, NPCType::Peasant, "timaert", 1, 30.0f, 30.0f, 777u,
            {21u, 22u, 23u, 24u, 25u, 26u, 27u, 28u}, NPCType::Wolf, 1);
        // НЕГАТИВНЫЙ КОНТРОЛЬ САМОГО СРАВНЕНИЯ: два листа обязаны РЕАЛЬНО
        // расходиться, иначе ниже сравниваются два одинаковых счёта и закон
        // не проверен вовсе (§8 п.3 — цикл, который меряет, утверждает, что
        // ПОМЕРИЛ). Сумма рангов — мера грубая и этого достаточно: она
        // ненулевая у богатого листа и строго меньше у бедного.
        int richRanks = 0, poorRanks = 0;
        const CharacterSheet richSheet = make_character_sheet(
            NPCType::Peasant, kRichLevel, avatar_sheet_seed(1u));
        const CharacterSheet poorSheet = make_character_sheet(
            NPCType::Guard, kPoorLevel, avatar_sheet_seed(999u));
        for (int i = 0; i < int(SkillId::Count); ++i) {
            richRanks += richSheet.skills.of(SkillId(i));
            poorRanks += poorSheet.skills.of(SkillId(i));
        }
        CHECK(richRanks > poorRanks && poorRanks >= 0,
              "негативный контроль: листы ведущих и правда расходятся, "
              "значит равенство счёта ниже что-то утверждает");
        CHECK(npc_def(NPCType::Wolf).upkeepGoldPerDay == kNpcUpkeepNone,
              "негативный контроль строки: волк и правда не на жалованье");
        // A season of harch and a season of wages in each bag: the window
        // judges BOTH needs whole, and an uncovered wage would bleed the
        // creatures before the harch law under test ever showed.
        const int stock = 8 * kDaysPerSeason * 2;
        for (const sm::MacroHandle e : {rich, poor, beasts}) {
            auto& bag = (*sm::body_state<ecs::NpcInventory>(st, e)).inv;
            bag.add("food", stock);
            bag.add("coin_empire_copper", 8 * 3 * kDaysPerSeason * 4);
        }
        // (Конверт `MacroWorld` здесь больше не нужен: дверь просит
        // ровно то, чем судит — `gs` и `st`, — и от entt-реестра
        // отвязана, M-233.)
        CHECK(squad_season_window(gs, st, 2) == 0,
              "no window off the boundary (negative control)");
        CHECK((*sm::body_state<ecs::NpcInventory>(st, poor)).inv.count("food")
                  == stock,
              "an ordinary day draws no harch at all");
        squad_season_window(gs, st, 1);
        const int richLeft =
            (*sm::body_state<ecs::NpcInventory>(st, rich)).inv.count("food");
        const int poorLeft =
            (*sm::body_state<ecs::NpcInventory>(st, poor)).inv.count("food");
        const int beastsLeft =
            (*sm::body_state<ecs::NpcInventory>(st, beasts)).inv.count("food");
        // Восемь душ × рацион строки × сезон — счёт читается прямо из
        // таблицы существ, а не из литерала теста.
        const int draw = 8 * npc_board_per_day(NPCType::Peasant)
                       * kDaysPerSeason;
        CHECK(poorLeft == stock - draw,
              "лагерь ест сумму рационов своих строк за сезон");
        CHECK(richLeft == poorLeft,
              "лист ведущего счёт НЕ МЕНЯЕТ: один закон без исключений");
        CHECK(beastsLeft
                  == stock - 8 * npc_board_per_day(NPCType::Wolf)
                                 * kDaysPerSeason,
              "рот объявляет КОЛОНКА РАЦИОНА: зверь без жалованья всё равно "
              "ест по своей строке");
        CHECK(beastsLeft < stock,
              "негативный контроль: зверьё не бесплатно — до 2026-09-22 эта "
              "строка была бы равна stock");
    }
}

// ── ПОБЕДА НЕ СТИРАЕТ АНКЕТУ, И КРИВАЯ ОПЫТА НЕ БЫВАЕТ НУЛЕВОЙ (M-246а) ───
// Закон: рост идёт ОДНОЙ дверью ЧЕРЕЗ АНКЕТУ (AGENTS ЗАКОН АНКЕТЫ: «анкета
// источник»; п.4: «пустой лист есть ЗАКОННОЕ значение… отдельной ветки „а это
// место“ для них не нужно»). До слияния дверей невладеющий лист шёл вторым
// законом, который на каждом уровне ПЕРЕКАТЫВАЛ анкету из сида — и сквад,
// получивший свою анкету ТАБЛИЦЕЙ, терял её навсегда с первой же победой.
//
// Свидетеля у этого закона не было ВОВСЕ, поэтому дефект и жил: ни один тест
// не спрашивал, цела ли анкета ПОСЛЕ роста. Фикстура рождает предусловие сама
// (§8 п.11): сквад с табличной анкетой непустых ремёсел — ровно то, что
// кладёт `birth_place` городу.
//
// ДВА УТВЕРЖДЕНИЯ, И ОНИ РАЗНЫЕ:
//   · ремёсла целы после роста — перекат анкеты невыразим;
//   · один-единственный опыт НЕ даёт уровня — кривая `exp_to_next_level`
//     читает уровень АНКЕТЫ, а он не бывает нулём (LevelData::level = 1).
//     Через мёртвую колонку он был нулём у всякого неподвижного сквада, и
//     порог `exp_to_next_level(0)` = 0 выдавал уровень за любой xp > 0.
void test_a_victory_does_not_erase_a_table_born_sheet() {
    GameState gs = make_world(-80);
    auto wStore_ = sm::make_macro_store();
    sm::MacroStore& st = *wStore_;

    const MacroHandle town = sm::birth_place(gs, st, SquadType::City,
                                             12, 12, std::int16_t(0), "Kamen");
    CHECK_OR_RETURN(st.valid(town), "фикстура родила свой неподвижный сквад");

    const CharacterSheet born = st.sheet[town.slot];
    const int smithBorn = born.skills[SkillId::Blacksmith];
    const int tradeBorn = born.skills[SkillId::Trade];
    const int chaBorn   = born.attributes[AttributeId::Cha];
    // Предусловие утверждается: табличная анкета обязана быть НЕПУСТОЙ, иначе
    // свидетель ниже зелен по пустоте, а не по закону.
    CHECK_OR_RETURN(smithBorn > 0 && tradeBorn > 0 && chaBorn > 1,
                    "строка стола дала этому роду непустые ремёсла и торг — "
                    "иначе проверять нечего");

    // (1) КРИВАЯ НЕ НУЛЕВАЯ: один опыт — это не уровень.
    const int lvlBorn = sm::squad_level(st, town.slot);
    CHECK(lvlBorn >= 1, "уровень анкеты не бывает нулём");
    CHECK(squad_award_exp(st, town, 1) == 0,
          "один опыт НЕ даёт уровня: порог читает уровень анкеты, а не "
          "мёртвую колонку, где он был нулём");

    // (2) РОСТ НЕ СТИРАЕТ АНКЕТУ: платим заведомо выше порога.
    const int gained = squad_award_exp(st, town, exp_to_next_level(lvlBorn) * 3);
    CHECK(gained >= 1, "полная шкала превращается в уровень общей дверью");
    CHECK(sm::squad_level(st, town.slot) > lvlBorn,
          "и уровень лёг в ТУ ЖЕ анкету");

    const CharacterSheet& after = st.sheet[town.slot];
    CHECK(after.skills[SkillId::Blacksmith] >= smithBorn
              && after.skills[SkillId::Trade] >= tradeBorn
              && after.attributes[AttributeId::Cha] >= chaBorn,
          "РЕМЁСЛА И ТОРГ ЦЕЛЫ ПОСЛЕ РОСТА: перекат анкеты из сида умер "
          "вместе со вторым законом роста (до него тут были нули, и мир "
          "переставал производить навсегда)");
    // Негативный контроль утверждается сам: веса роли, которой этот сквад
    // носит строку, этих рангов НЕ дают — значит выжить они могли ТОЛЬКО
    // потому, что анкету не перекатили, а не потому, что бросок их вернул.
    const SkillWeights& w = npc_skill_weights(
        NPCType(std::uint8_t(st.kind[town.slot].type)));
    CHECK(w[std::size_t(SkillId::Blacksmith)] == 0
              && w[std::size_t(SkillId::Trade)] == 0,
          "негативный контроль: бросок строки этих рангов не рождает — "
          "будь перекат жив, они стали бы нулями безвозвратно");
}

int main() {
    test_hostiles_on_one_cell_fight_and_the_ledger_pays();
    test_a_victory_does_not_erase_a_table_born_sheet();
    test_the_avatars_training_reads_at_the_new_doors();
    test_the_weak_flee_and_fighters_pursue();
    test_neutral_squads_ignore_each_other();
    test_a_meeting_in_the_leased_window_is_not_resolved_upstairs();
    test_a_victorious_avatar_levels();
    test_a_level_does_not_strip_what_is_worn();
    test_wearing_alone_refreshes_the_derived_columns();
    test_player_auto_resolve_settles_through_the_same_doors();
    test_spawn_squad_is_one_spec_one_door();
    return sm::test::report("squad_war_test");
}

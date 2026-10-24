// ПОЛЯ СЛЕДОВ И ОХОТА (CANON S10 «хищник-жертва», владелец 2026-09-03) —
// инварианты, каждый как обещание:
//   · ФИЗИКА: квант вклада po2, сатурация = честный потолок, диффузия
//     изотропна и хранит массу на торе, распад — по календарю мира;
//   · ПИСАТЕЛЬ: сила следа = squad_power (единственный закон боя), цена =
//     души по строкам найма + ценность груза (одна таблица цен);
//   · ОХОТА: вверх по градиенту чужой ЦЕНЫ под фильтром СИЛЫ; страшный след
//     не преследуется, пылинки ниже пола не дёргают, локальный максимум =
//     конец погони; макроцель и память роли отвлечением НЕ трогаются.
#include "check.h"

#include "ecs/components.h"
#include "macro/anketa.h"
#include "macro/world_row.h"
#include "macro/currency.h"
#include "tables/npc.h"
#include "macro/npc_ai.h"
#include "macro/factions.h"   // set_relation — вражду ставим отношением
#include "macro/scent_field.h"
#include "macro/store.h"
#include <memory>

#include <cstdint>

namespace {

using namespace sm;

// ── Физика поля ──────────────────────────────────────────────────────────

void test_deposit_quant_and_saturation() {
    ScentField sf{};
    scent_reset(sf, 8, 8);
    CHECK(sf.sized_for(8, 8), "reset кроит поле под мир и реестр");

    scent_deposit(sf, 0, 3, 3, /*power*/40u, /*worth*/400u);
    CHECK(scent_strength_at(sf, 0, 3, 3) == (40u >> kScentQuantShift),
          "сила ложится квантом po2 — сдвиг один, у поля");
    CHECK(scent_wealth_at(sf, 0, 3, 3) == (400u >> kScentQuantShift),
          "цена ложится тем же квантом");

    scent_deposit(sf, 0, 3, 3, 0xFFFFFFFFu, 0xFFFFFFFFu);
    scent_deposit(sf, 0, 3, 3, 0xFFFFFFFFu, 0xFFFFFFFFu);
    CHECK(scent_strength_at(sf, 0, 3, 3) == 0xFFFFu
              && scent_wealth_at(sf, 0, 3, 3) == 0xFFFFu,
          "сатурация — честный потолок, не враппинг");

    scent_deposit(sf, kFactionCount + 5, 3, 3, 100u, 100u);
    scent_deposit(sf, -1, 3, 3, 100u, 100u);
    CHECK(scent_strength_at(sf, 1, 3, 3) == 0u,
          "чужой/бесфракционный индекс не пишет никуда (fail closed)");
}

void test_diffusion_isotropic_on_torus() {
    ScentField sf{};
    scent_reset(sf, 8, 8);
    // Угол тора: все восемь соседей (0,0) — через wrap, изотропия обязана
    // пережить шов. 6400 → доля 1/8 = 800, поровну 100 на соседа.
    scent_deposit(sf, 0, 0, 0, 6400u << kScentQuantShift, 0u);
    scent_field_daily(sf, /*day*/1);   // день 1 — не кратен распаду (4)

    const std::uint32_t per = (6400u >> kScentDiffusionShift) >> 3;
    CHECK(per == 100u, "фикстура: доля на соседа = 100");
    std::uint32_t sum = 0;
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x)
            sum += scent_strength_at(sf, 0, x, y);
    CHECK(sum == 6400u, "диффузия хранит массу — ни единицы в шов");
    CHECK(scent_strength_at(sf, 0, 0, 0) == 6400u - 8u * per,
          "центр отдал восемь долей");
    CHECK(scent_strength_at(sf, 0, 7, 7) == per
              && scent_strength_at(sf, 0, 1, 1) == per
              && scent_strength_at(sf, 0, 7, 0) == per
              && scent_strength_at(sf, 0, 0, 7) == per,
          "восемь соседей через шов тора получили поровну — изотропия");
}

void test_decay_by_world_calendar() {
    ScentField sf{};
    scent_reset(sf, 8, 8);
    scent_deposit(sf, 0, 4, 4, 0u, 4000u << kScentQuantShift);
    const std::uint16_t before = scent_wealth_at(sf, 0, 4, 4);

    scent_field_daily(sf, /*day*/5);   // 5 % 4 != 0 — не день распада
    std::uint32_t sum = 0;
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x)
            sum += scent_wealth_at(sf, 0, x, y);
    CHECK(sum == before, "не-календарный день массу не режет");

    scent_field_daily(sf, /*day*/kScentDecayDays);
    std::uint32_t after = 0;
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x)
            after += scent_wealth_at(sf, 0, x, y);
    CHECK(after < sum, "календарный день полураспада режет поле");
}

void test_ensure_resets_only_on_mismatch() {
    ScentField sf{};
    scent_ensure(sf, 8, 8);
    scent_deposit(sf, 0, 1, 1, 400u, 400u);
    scent_ensure(sf, 8, 8);
    CHECK(scent_wealth_at(sf, 0, 1, 1) > 0u,
          "совпавший размер не трогает накопленное");
    scent_ensure(sf, 16, 16);
    CHECK(sf.sized_for(16, 16) && scent_wealth_at(sf, 0, 1, 1) == 0u,
          "чужой размер = холодный пересбор");
}

// ── Писатель и охота ─────────────────────────────────────────────────────

int faction_idx(const char* id) {
    for (int i = 0; i < kFactionCount; ++i)
        if (kFactionDefs[i].id == id
            || std::strcmp(kFactionDefs[i].id, id) == 0)
            return i;
    return -1;
}

struct HuntRig {
    GameState gs{};
    std::unique_ptr<sm::MacroStore> wStore_ = sm::make_macro_store();
    TerrainData absent{};
    Rng rng{123u};
    MacroWorld mw{};
    TickContext ctx{};

    HuntRig() {
        gs.mapW = 64;
        gs.mapH = 64;
        scent_ensure(gs.scent, 64, 64);
        mw.gs = &gs;
        mw.store = wStore_.get();
        mw.terrain = &absent;
        ctx.mw = mw;
        ctx.mapW = 64;
        ctx.mapH = 64;
        ctx.rng = &rng;
    }

    // ВРАЖДУ СТАВИМ ОТНОШЕНИЕМ, А НЕ ЯРЛЫКОМ (M-268). Прежде фикстура
    // поджигала бит `ctx.factionHostileMask` — то есть свидетель гонял
    // ЗАПЕЧЁННУЮ копию, а не закон, и снос копии он бы не заметил. Теперь
    // он двигает матрицу той же симметричной дверью, что и мир, и охота
    // читает её живьём.
    void make_enemies(int a, int b) {
        sm::set_relation(gs.factions, a, b, sm::kHostileThreshold - 1);
    }

    sm::MacroHandle spawn(NPCType type, int factionIdx, float x, float y) {
        sm::MacroStore& st = *wStore_;
        const sm::MacroHandle h = sm::store_birth(st);
        st.cell[h.slot] = ecs::MacroCell{
            ecs::cell_index(int(x), int(y), ctx.mapW)};
        st.kind[h.slot] = ecs::NPCKind{std::uint16_t(type),
                                       std::uint16_t(factionIdx)};
        ecs::MacroNpcRuntime rt{};
        rt.moveMult = 1.0f;
        st.runtime[h.slot] = rt;
        ecs::Pools pools{};
        pools.hp = pools.maxHp = 50;
        pools.sp = pools.maxSp = 100;
        st.pools[h.slot] = pools;
        return h;
    }
};

void test_deposit_writes_both_channels() {
    HuntRig rig;
    const int fVil = faction_idx("timaert");
    CHECK(fVil >= 0, "фикстура: фракция реестра существует");
    auto e = rig.spawn(NPCType::Peasant, fVil, 20.0f, 20.0f);

    auto& bag = rig.wStore_->inventory[e.slot];
    creatures_push(bag.inv,
                   make_soldier(std::uint16_t(NPCType::Peasant), 1, 1u));
    creatures_push(bag.inv,
                   make_soldier(std::uint16_t(NPCType::Peasant), 1, 2u));
    bag.inv.add("food", 10);

    const auto& dcell = (*sm::body_state<ecs::MacroCell>(*rig.wStore_, e));
    scent_squad_deposit(e, MacroPos{float(ecs::cell_x(dcell, rig.ctx.mapW)),
                                    float(ecs::cell_y(dcell, rig.ctx.mapW))},
                        (*sm::body_state<ecs::NPCKind>(*rig.wStore_, e)), rig.ctx);

    CHECK(scent_strength_at(rig.gs.scent, fVil, 20, 20) > 0u,
          "сила следа легла: squad_power сквада — не ноль");
    const std::uint32_t soulGold =
        2u * std::uint32_t(npc_def(NPCType::Peasant).hireGold);
    const std::uint32_t goods = std::uint32_t(inventory_value(bag.inv));
    CHECK(scent_wealth_at(rig.gs.scent, fVil, 20, 20)
              == ((soulGold + goods) >> kScentQuantShift),
          "цена следа = души по строке найма + груз по таблице цен");
}

void test_hunter_climbs_wealth_gradient() {
    HuntRig rig;
    const int fBandit = faction_idx("bandits");
    const int fPrey = faction_idx("timaert");
    auto e = rig.spawn(NPCType::Bandit, fBandit, 10.0f, 10.0f);
    rig.make_enemies(fBandit, fPrey);

    // Жирный след цены на востоке, силы в нём нет — чистая добыча.
    scent_deposit(rig.gs.scent, fPrey, 11, 10, 0u, 400u);

    const auto& pc = (*sm::body_state<ecs::MacroCell>(*rig.wStore_, e));
    MacroPos p{float(ecs::cell_x(pc, rig.ctx.mapW)),
               float(ecs::cell_y(pc, rig.ctx.mapW))};
    auto& rt = (*sm::body_state<ecs::MacroNpcRuntime>(*rig.wStore_, e));
    CHECK(scent_hunt_step(e, p, (*sm::body_state<ecs::NPCKind>(*rig.wStore_, e)), rt, (*sm::body_state<ecs::Pools>(*rig.wStore_, e)), rig.ctx),
          "запах добычи съедает think — охота пошла");
    // Один think копит бюджет ног (0.75 клетки), второй шагает — та же
    // честная походка, что у любого марша.
    scent_hunt_step(e, p, (*sm::body_state<ecs::NPCKind>(*rig.wStore_, e)), rt, (*sm::body_state<ecs::Pools>(*rig.wStore_, e)), rig.ctx);
    CHECK(int(p.x) == 11 && int(p.y) == 10,
          "шаг строго вверх по градиенту цены");
}

void test_fear_filter_and_scent_floor() {
    HuntRig rig;
    const int fBandit = faction_idx("bandits");
    const int fPrey = faction_idx("timaert");
    auto e = rig.spawn(NPCType::Bandit, fBandit, 10.0f, 10.0f);
    rig.make_enemies(fBandit, fPrey);

    const auto& pc = (*sm::body_state<ecs::MacroCell>(*rig.wStore_, e));
    MacroPos p{float(ecs::cell_x(pc, rig.ctx.mapW)),
               float(ecs::cell_y(pc, rig.ctx.mapW))};
    auto& rt = (*sm::body_state<ecs::MacroNpcRuntime>(*rig.wStore_, e));
    const auto& kind = (*sm::body_state<ecs::NPCKind>(*rig.wStore_, e));

    // Богато, но след силы страшнее моей смелости — не преследуем: пусть
    // решает визуальный рефлекс (закон боя), не запах.
    scent_deposit(rig.gs.scent, fPrey, 11, 10, 4u << 20, 400u);
    CHECK(!scent_hunt_step(e, p, kind, rt, (*sm::body_state<ecs::Pools>(*rig.wStore_, e)), rig.ctx),
          "страшный след не преследуется");

    // Пылинка диффузии ниже пола не дёргает бойца с места.
    scent_reset(rig.gs.scent, 64, 64);
    scent_deposit(rig.gs.scent, fPrey, 11, 10, 0u,
                  (kHuntScentFloor / 2u) << kScentQuantShift);
    CHECK(!scent_hunt_step(e, p, kind, rt, (*sm::body_state<ecs::Pools>(*rig.wStore_, e)), rig.ctx),
          "запах беднее пола — не стоит и шага");
    CHECK(int(p.x) == 10 && int(p.y) == 10, "ноги не тронуты");
}

void test_local_maximum_ends_the_hunt_and_keeps_role_memory() {
    HuntRig rig;
    const int fBandit = faction_idx("bandits");
    const int fPrey = faction_idx("timaert");
    auto e = rig.spawn(NPCType::Bandit, fBandit, 10.0f, 10.0f);
    rig.make_enemies(fBandit, fPrey);

    const auto& pc = (*sm::body_state<ecs::MacroCell>(*rig.wStore_, e));
    MacroPos p{float(ecs::cell_x(pc, rig.ctx.mapW)),
               float(ecs::cell_y(pc, rig.ctx.mapW))};
    auto& rt = (*sm::body_state<ecs::MacroNpcRuntime>(*rig.wStore_, e));
    const auto& kind = (*sm::body_state<ecs::NPCKind>(*rig.wStore_, e));

    // Макроцель на месте до и после охоты: отвлечение — пауза, не амнезия.
    // Колонка здесь — ЛЮБАЯ живая память роли: закон не про конкретную
    // величину, а про то, что рефлекс не затирает решение роли. Взят
    // `prevStationId` — «с какой станции крю пришло» многоскачкового рейса
    // (components.h), ординал-идентичность, где 42 осмысленно, а 0 значило бы
    // «никто» (ЗАКОН СЛОВАРЯ п.6) и закон бы не проверялся.
    // (Прежде стоял ErrandVerb::Patrol — снят 2026-09-21 с патрульной
    // механикой; затем `errandObject` — снесён 2026-10-24, ломоть 4 M-72,
    // как второй носитель `SquadGoal::what`.)
    rt.squadType = std::uint8_t(SquadType::Caravan);
    rt.prevStationId = 42;
    sm::ecs::goal_set(rig.wStore_->goal[e.slot],
                      ecs::cell_index(50, 50, rig.ctx.mapW), 0, 0, 0);

    // Сам стою на максимуме — подъёма нет, погоня окончена, думает роль.
    scent_deposit(rig.gs.scent, fPrey, 10, 10, 0u, 4000u);
    scent_deposit(rig.gs.scent, fPrey, 11, 10, 0u, 400u);
    CHECK(!scent_hunt_step(e, p, kind, rt, (*sm::body_state<ecs::Pools>(*rig.wStore_, e)), rig.ctx),
          "локальный максимум = конец погони");

    scent_reset(rig.gs.scent, 64, 64);
    scent_deposit(rig.gs.scent, fPrey, 11, 10, 0u, 400u);
    CHECK(scent_hunt_step(e, p, kind, rt, (*sm::body_state<ecs::Pools>(*rig.wStore_, e)), rig.ctx), "охота пошла (фикстура)");
    // РОЛЬ ЦЕЛА — ЭТО ЖИВОЙ ЗАКОН, И ОН ОСТАЁТСЯ: отвлечение не превращает
    // корована в кого-то другого и не стирает его память.
    CHECK(rt.squadType == std::uint8_t(SquadType::Caravan)
              && rt.prevStationId == 42,
          "рефлекс не затирает РЕШЕНИЕ РОЛИ: тип и память роли целы");
    // А КУРС — НАОБОРОТ, И ЭТО ПЕРЕВЁРНУТОЕ УТВЕРЖДЕНИЕ, НЕ СНЯТОЕ.
    // Здесь в ТОЙ ЖЕ проверке стояло «и курс цел» (цель оставалась на
    // 50,50): охота вела сквад голым `try_move`, то есть «куда» жило в
    // аргументе функции, а не в колонке. Хвост ломтя 3 M-72 свёл «куда» в
    // ОДНУ колонку рода 2 — и охота теперь пишет её, ровно как давно писал
    // соседний порог угрозы. Старое утверждение охраняло не закон, а
    // отсутствие записи; новое охраняет саму запись.
    CHECK(rig.wStore_->goal[e.slot].cell
              == ecs::cell_index(11, 10, rig.ctx.mapW),
          "охота ЗАПИСАЛА курс в колонку цели — клетка подъёма по запаху, "
          "а не прежние 50,50 и не безымянный аргумент try_move");
    CHECK(rt.state == std::uint8_t(NPCState::Chasing),
          "и состояние назвало плечо: по запаху сквад ГОНИТ, а не бредёт");
}

void test_civilian_never_hunts() {
    HuntRig rig;
    const int fVil = faction_idx("timaert");
    const int fPrey = faction_idx("empire");
    auto e = rig.spawn(NPCType::Peasant, fVil, 10.0f, 10.0f);
    rig.make_enemies(fVil, fPrey);
    scent_deposit(rig.gs.scent, fPrey, 11, 10, 0u, 4000u);

    const auto& pc = (*sm::body_state<ecs::MacroCell>(*rig.wStore_, e));
    MacroPos p{float(ecs::cell_x(pc, rig.ctx.mapW)),
               float(ecs::cell_y(pc, rig.ctx.mapW))};
    auto& rt = (*sm::body_state<ecs::MacroNpcRuntime>(*rig.wStore_, e));
    CHECK(!scent_hunt_step(e, p, (*sm::body_state<ecs::NPCKind>(*rig.wStore_, e)), rt, (*sm::body_state<ecs::Pools>(*rig.wStore_, e)), rig.ctx),
          "не-combatant не охотится: кто хищник — решает колонка поведения");
}

} // namespace

int main() {
    test_deposit_quant_and_saturation();
    test_diffusion_isotropic_on_torus();
    test_decay_by_world_calendar();
    test_ensure_resets_only_on_mismatch();
    test_deposit_writes_both_channels();
    test_hunter_climbs_wealth_gradient();
    test_fear_filter_and_scent_floor();
    test_local_maximum_ends_the_hunt_and_keeps_role_memory();
    test_civilian_never_hunts();
    return sm::test::report("scent_field_test");
}

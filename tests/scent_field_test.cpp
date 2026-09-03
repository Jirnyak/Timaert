// ПОЛЯ СЛЕДОВ И ОХОТА (CANON S10 «хищник-жертва», владелец 2026-09-03) —
// инварианты, каждый как обещание:
//   · ФИЗИКА: квант вклада po2, сатурация = честный потолок, диффузия
//     изотропна и хранит массу на торе, распад — по календарю мира;
//   · ПИСАТЕЛЬ: сила следа = squad_power (единственный закон боя), цена =
//     души по строкам найма + ценность груза (одна таблица цен);
//   · ОХОТА: вверх по градиенту чужой ЦЕНЫ под фильтром СИЛЫ; страшный след
//     не преследуется, пылинки ниже пола не дёргают, локальный максимум =
//     конец погони; макроцель (errand) отвлечением НЕ трогается.
#include "check.h"

#include "ecs/components.h"
#include "macro/army.h"
#include "macro/currency.h"
#include "macro/npc.h"
#include "macro/npc_ai.h"
#include "macro/scent_field.h"

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
    ecs::World w;
    TerrainData absent{};
    Rng rng{123u};
    MacroWorld mw{};
    TickContext ctx{};

    HuntRig() {
        gs.mapW = 64;
        gs.mapH = 64;
        scent_ensure(gs.scent, 64, 64);
        mw.gs = &gs;
        mw.world = &w;
        mw.terrain = &absent;
        ctx.mw = mw;
        ctx.mapW = 64;
        ctx.mapH = 64;
        ctx.rng = &rng;
    }

    entt::entity spawn(NPCType type, int factionIdx, float x, float y) {
        auto e = w.reg.create();
        w.reg.emplace<ecs::Position>(e, x, y, 0.0f);
        w.reg.emplace<ecs::NPCKind>(e, std::uint16_t(type),
                                    std::uint16_t(factionIdx));
        ecs::MacroNpcRuntime rt{};
        rt.sp = 100;
        rt.maxSp = 100;
        rt.moveMult = 1.0f;
        rt.targetX = x;
        rt.targetY = y;
        w.reg.emplace<ecs::MacroNpcRuntime>(e, rt);
        w.reg.emplace<ecs::Health>(e, 50.0f, 50.0f);
        return e;
    }
};

void test_deposit_writes_both_channels() {
    HuntRig rig;
    const int fVil = faction_idx("timaert");
    CHECK(fVil >= 0, "фикстура: фракция реестра существует");
    auto e = rig.spawn(NPCType::Peasant, fVil, 20.0f, 20.0f);

    auto& roster = rig.w.reg.emplace<ecs::SquadRoster>(e);
    roster.squad.push(make_soldier(std::uint16_t(NPCType::Peasant), 1, 1u));
    roster.squad.push(make_soldier(std::uint16_t(NPCType::Peasant), 1, 2u));
    auto& bag = rig.w.reg.emplace<ecs::NpcInventory>(e);
    bag.inv.add("bread", 10);

    scent_squad_deposit(e, rig.w.reg.get<ecs::Position>(e),
                        rig.w.reg.get<ecs::NPCKind>(e), rig.ctx);

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
    rig.ctx.factionHostileMask[fBandit] = 1ull << fPrey;

    // Жирный след цены на востоке, силы в нём нет — чистая добыча.
    scent_deposit(rig.gs.scent, fPrey, 11, 10, 0u, 400u);

    auto& p = rig.w.reg.get<ecs::Position>(e);
    auto& rt = rig.w.reg.get<ecs::MacroNpcRuntime>(e);
    CHECK(scent_hunt_step(e, p, rig.w.reg.get<ecs::NPCKind>(e), rt, rig.ctx),
          "запах добычи съедает think — охота пошла");
    // Один think копит бюджет ног (0.75 клетки), второй шагает — та же
    // честная походка, что у любого марша.
    scent_hunt_step(e, p, rig.w.reg.get<ecs::NPCKind>(e), rt, rig.ctx);
    CHECK(int(p.x) == 11 && int(p.y) == 10,
          "шаг строго вверх по градиенту цены");
}

void test_fear_filter_and_scent_floor() {
    HuntRig rig;
    const int fBandit = faction_idx("bandits");
    const int fPrey = faction_idx("timaert");
    auto e = rig.spawn(NPCType::Bandit, fBandit, 10.0f, 10.0f);
    rig.ctx.factionHostileMask[fBandit] = 1ull << fPrey;

    auto& p = rig.w.reg.get<ecs::Position>(e);
    auto& rt = rig.w.reg.get<ecs::MacroNpcRuntime>(e);
    const auto& kind = rig.w.reg.get<ecs::NPCKind>(e);

    // Богато, но след силы страшнее моей смелости — не преследуем: пусть
    // решает визуальный рефлекс (закон боя), не запах.
    scent_deposit(rig.gs.scent, fPrey, 11, 10, 4u << 20, 400u);
    CHECK(!scent_hunt_step(e, p, kind, rt, rig.ctx),
          "страшный след не преследуется");

    // Пылинка диффузии ниже пола не дёргает бойца с места.
    scent_reset(rig.gs.scent, 64, 64);
    scent_deposit(rig.gs.scent, fPrey, 11, 10, 0u,
                  (kHuntScentFloor / 2u) << kScentQuantShift);
    CHECK(!scent_hunt_step(e, p, kind, rt, rig.ctx),
          "запах беднее пола — не стоит и шага");
    CHECK(int(p.x) == 10 && int(p.y) == 10, "ноги не тронуты");
}

void test_local_maximum_ends_the_hunt_and_keeps_errand() {
    HuntRig rig;
    const int fBandit = faction_idx("bandits");
    const int fPrey = faction_idx("timaert");
    auto e = rig.spawn(NPCType::Bandit, fBandit, 10.0f, 10.0f);
    rig.ctx.factionHostileMask[fBandit] = 1ull << fPrey;

    auto& p = rig.w.reg.get<ecs::Position>(e);
    auto& rt = rig.w.reg.get<ecs::MacroNpcRuntime>(e);
    const auto& kind = rig.w.reg.get<ecs::NPCKind>(e);

    // Макроцель на месте до и после охоты: отвлечение — пауза, не амнезия.
    rt.errandVerb = std::uint8_t(ErrandVerb::Patrol);
    rt.errandObject = 42u;
    rt.targetX = 50.0f;
    rt.targetY = 50.0f;

    // Сам стою на максимуме — подъёма нет, погоня окончена, думает роль.
    scent_deposit(rig.gs.scent, fPrey, 10, 10, 0u, 4000u);
    scent_deposit(rig.gs.scent, fPrey, 11, 10, 0u, 400u);
    CHECK(!scent_hunt_step(e, p, kind, rt, rig.ctx),
          "локальный максимум = конец погони");

    scent_reset(rig.gs.scent, 64, 64);
    scent_deposit(rig.gs.scent, fPrey, 11, 10, 0u, 400u);
    CHECK(scent_hunt_step(e, p, kind, rt, rig.ctx), "охота пошла (фикстура)");
    CHECK(rt.errandVerb == std::uint8_t(ErrandVerb::Patrol)
              && rt.errandObject == 42u
              && int(rt.targetX) == 50 && int(rt.targetY) == 50,
          "макроцель не тронута: глагол, объект и курс поручения целы");
}

void test_civilian_never_hunts() {
    HuntRig rig;
    const int fVil = faction_idx("timaert");
    const int fPrey = faction_idx("empire");
    auto e = rig.spawn(NPCType::Peasant, fVil, 10.0f, 10.0f);
    rig.ctx.factionHostileMask[fVil] = 1ull << fPrey;
    scent_deposit(rig.gs.scent, fPrey, 11, 10, 0u, 4000u);

    auto& p = rig.w.reg.get<ecs::Position>(e);
    auto& rt = rig.w.reg.get<ecs::MacroNpcRuntime>(e);
    CHECK(!scent_hunt_step(e, p, rig.w.reg.get<ecs::NPCKind>(e), rt, rig.ctx),
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
    test_local_maximum_ends_the_hunt_and_keeps_errand();
    test_civilian_never_hunts();
    return sm::test::report("scent_field_test");
}

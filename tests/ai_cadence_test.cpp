// МИР — ФУНКЦИЯ ТИКОВ, НЕ ДРОБЛЕНИЯ ВЫЗОВОВ (M-379; вердикт владельца:
// «не должно зависить от КАДРОВ НИЧЕГО - только тики игры»). Единый драйвер
// макро-думок обязан дать байтово ОДИН мир, как бы ни резали те же N тиков:
// по одному (каденс живой игры), одним прыжком (addtime), случайными кусками
// (любой будущий звонящий). До M-379 фаза сквада копила ВЫЗОВЫ
// (rt.tickAccum += min(ticks, 32)) — пачка 32 схлопывала все фазы в ноль,
// и эволюция мира зависела от каданса вызовов; срез прыжка терял вход молча.
//
// ТАБЛИЦА «МУТАЦИЯ → ИСХОД» (прогнано руками 2026-11-03):
//  - петля драйвера `i < min(ticks, 1)` — старый закон «одна думка на вызов»,
//    срез прыжка → КРАСНЫЙ, 6 из 11 (строки, поток RNG и метр думок
//    разошлись у прыжка и у кусков против по-тикового);
//  - петля драйвера `i += 2` — потеря половины тиков на прыжке → КРАСНЫЙ,
//    6 из 11 (по-тиковый прогон не теряет ничего, куски теряют половину);
//  - ДОЛЖНА МОЛЧАТЬ: раскрой C заменён на 128×5 → ЗЕЛЁНЫЙ 11/11 — закон
//    судит дробление вообще, а не конкретный раскрой.
#include "check.h"

#include "ecs/components.h"
#include "macro/birth_squad.h"   // birth_squad — место родится ТЕЛОМ
#include "macro/labour.h"        // settle_souls — одна дверь душ
#include "macro/npc_ai.h"
#include "macro/store.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace {

// Снимок эволюции: всё целочисленное состояние сквада, которое двигает думка,
// плюс поток RNG и метр думок. Представление (visualSpeed) сознательно не
// сравнивается — оно не мир.
struct SquadRow {
    std::uint32_t ordinal = 0;
    std::uint32_t cell = 0;
    int subX = 0, subY = 0;
    std::uint32_t accum = 0;
    std::uint8_t state = 0;
    std::uint32_t goal = 0;
    int hp = 0, sp = 0;

    bool operator==(const SquadRow&) const = default;
};

struct WorldShot {
    std::vector<SquadRow> rows;
    std::uint32_t jitter = 0;
    std::uint64_t thinks = 0;
};

WorldShot snapshot(const sm::MacroStore& st, const sm::MacroNpcAiRuntime& ai) {
    WorldShot shot;
    for (std::uint32_t slot = 0; slot < sm::kUnifiedCap; ++slot) {
        if (st.alive[slot] == 0 || st.dead[slot] != 0) continue;
        SquadRow row;
        row.ordinal = st.spawnId[slot].index;
        row.cell = st.cell[slot].idx;
        row.subX = int(st.runtime[slot].sub.x);
        row.subY = int(st.runtime[slot].sub.y);
        row.accum = std::uint32_t(st.runtime[slot].tickAccum);
        row.state = st.runtime[slot].state;
        row.goal = st.goal[slot].cell;
        row.hp = int(st.pools[slot].hp);
        row.sp = int(st.pools[slot].sp);
        shot.rows.push_back(row);
    }
    shot.jitter = ai.jitter.state;
    shot.thinks = ai.thinksDone;
    return shot;
}

// Фикстура рожает СВОЁ предусловие (§8 п.11): город с паствой и пятеро
// сквадов с РАЗНЫМИ фазами думки — без разных фаз пер-вызовный приток
// неотличим от честного, и детектор слеп к собственному предмету.
struct Fixture {
    sm::GameState gs{};
    std::unique_ptr<sm::MacroStore> store;
    sm::MacroNpcAiRuntime ai;
};

void spawn_wanderer(sm::MacroStore& st, int ordinal, float x, float y,
                    int homeId, std::uint32_t phase, int mapW) {
    sm::MacroHandle h = sm::store_birth(st);
    sm::store_rename_ordinal(st, h, std::uint32_t(ordinal));
    st.cell[h.slot] =
        sm::ecs::MacroCell{sm::ecs::cell_index(int(x), int(y), mapW)};
    st.kind[h.slot] =
        sm::ecs::NPCKind{std::uint16_t(sm::NPCType::Peasant), std::uint16_t{0}};
    sm::ecs::MacroNpcRuntime rt{};
    rt.state = std::uint8_t(sm::NPCState::Idle);
    rt.tickAccum = phase;   // фаза думки — предмет прибора
    st.runtime[h.slot] = rt;
    sm::set_relation(st, int(h.id), homeId, sm::Stance::Parent);
    sm::ecs::Pools pools{};
    pools.hp = pools.maxHp = 50;
    pools.sp = pools.maxSp = 100;
    st.pools[h.slot] = pools;
}

Fixture build_world() {
    Fixture f;
    f.gs.mapW = 128;
    f.gs.mapH = 128;
    f.store = sm::make_macro_store();
    sm::MacroStore& st = *f.store;
    const sm::MacroHandle home =
        sm::birth_squad(f.gs, st, sm::SquadType::City, 50, 50, 0, "Cad");
    sm::settle_souls(f.gs, st, home.slot, 1000);
    const int homeId = int(st.spawnId[home.slot].index);
    // Фазы покрывают квант неравномерно и несовпадающе — схлопывание фаз
    // (пер-вызовный приток) меняет, КТО думает первым, и порядок общего RNG.
    spawn_wanderer(st, 1000, 80.0f, 50.0f, homeId, 0u, f.gs.mapW);
    spawn_wanderer(st, 1001, 52.0f, 50.0f, homeId, 5u, f.gs.mapW);
    spawn_wanderer(st, 1002, 50.0f, 53.0f, homeId, 11u, f.gs.mapW);
    spawn_wanderer(st, 1003, 47.0f, 49.0f, homeId, 23u, f.gs.mapW);
    spawn_wanderer(st, 1004, 30.0f, 70.0f, homeId, 31u, f.gs.mapW);
    sm::reset_macro_npc_ai_runtime(f.ai, 42u);
    return f;
}

WorldShot run_schedule(const std::vector<std::uint64_t>& chunks) {
    Fixture f = build_world();
    for (const std::uint64_t c : chunks) {
        sm::MacroWorld mw{.gs = &f.gs, .store = f.store.get()};
        sm::tick_macro_npc_ai(mw, f.ai, c);
    }
    return snapshot(*f.store, f.ai);
}

int diff_rows(const WorldShot& a, const WorldShot& b, int& samples) {
    int mismatches = 0;
    const std::size_t n = std::min(a.rows.size(), b.rows.size());
    for (std::size_t i = 0; i < n; ++i) {
        ++samples;
        if (!(a.rows[i] == b.rows[i])) ++mismatches;
    }
    return mismatches;
}

} // namespace

int main() {
    // 20 квантов думки на сквада: хватает на марши, возвраты и десятки
    // бросков общего RNG; сумма одна на все три расписания.
    const std::uint64_t kDriveTicks = 20ull * sm::kAiTicks;

    std::vector<std::uint64_t> perTick(std::size_t(kDriveTicks), 1ull);
    const std::vector<std::uint64_t> oneJump{kDriveTicks};
    const std::vector<std::uint64_t> mixed{1, 7, 32, 3, 64, 13, 5, 100,
                                           2, 31, 9, 128, 17, 4, 24, 200};
    {
        std::uint64_t sum = 0;
        for (const std::uint64_t c : mixed) sum += c;
        CHECK(sum == kDriveTicks,
              "раскрой C покрывает ровно те же тики (иначе сравнение ложно)");
    }

    const WorldShot shot0 = [] {
        Fixture f = build_world();
        return snapshot(*f.store, f.ai);
    }();
    const WorldShot a = run_schedule(perTick);
    const WorldShot b = run_schedule(oneJump);
    const WorldShot c = run_schedule(mixed);

    // Тавтология пустоты запрещена (§8 п.3): мир обязан был ЖИТЬ.
    CHECK(a.thinks > 0, "мир думал: квантов оплачено больше нуля");
    {
        int samples = 0;
        const int moved = diff_rows(shot0, a, samples);
        CHECK(samples > 0 && moved > 0,
              "мир эволюционировал: хоть одна строка ушла от рождения");
    }

    CHECK(a.rows.size() == b.rows.size(),
          "население совпало: по-тиково против прыжка");
    CHECK(a.rows.size() == c.rows.size(),
          "население совпало: по-тиково против кусков");
    {
        int samples = 0;
        const int mm = diff_rows(a, b, samples);
        CHECK(samples > 0 && mm == 0,
              "N вызовов по 1 тику == 1 вызов с N тиками, построчно");
    }
    CHECK(a.jitter == b.jitter, "поток RNG не зависит от дробления (прыжок)");
    CHECK(a.thinks == b.thinks, "число думок не зависит от дробления (прыжок)");
    {
        int samples = 0;
        const int mm = diff_rows(a, c, samples);
        CHECK(samples > 0 && mm == 0,
              "любой раскрой кусков даёт тот же мир, построчно");
    }
    CHECK(a.jitter == c.jitter, "поток RNG не зависит от дробления (куски)");
    CHECK(a.thinks == c.thinks, "число думок не зависит от дробления (куски)");

    return sm::test::report("ai_cadence_test");
}

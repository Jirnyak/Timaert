// «Я СРАЖАЮСЬ» — ОДНА ОПЕРАЦИЯ БОЯ СУБМИРА НАВЕРХ, И ЧИТАЕТ ЕЁ МОДУЛЬ, НЕ НОГИ
// (ломоть 6 ЗАХВАТА M-419; ЗАХВАТ п.3 ЗАКОНА ШВА; вердикт владельца: «если
// ктото из сквада вступает в файт В СУБМИРЕ!! … переходит смена сосотояния
// что он сражается (а это уже от модуля)»).
//
// ЧТО СУДИТСЯ — ЧЕТЫРЕ ПЛЕЧА ОДНОГО ЗАКОНА:
//   1. ШТАМП: дверь урона (apply_damage, ДО смягчения) взводит `fighting`
//      сквада ЖЕРТВЫ полным сроком kFightThinks — обеими ступенями лестницы
//      slot_squad_of (бэклинк аватара И расписка головы инвентаря); «ничей»
//      бой наверху не существует (пустой хэндл — no-op).
//   2. УГАСАНИЕ: срок тает САМ, раз в думку, одной точкой драйвера; sub_sat
//      не заворачивает. «Бой кончился» есть ОТСУТСТВИЕ удара — снятия снизу
//      нет как операции.
//   3. НОГИ И ВОЛЯ ЖИВЫ ПОД ШТАМПОМ: гейта ног НЕ СУЩЕСТВУЕТ ВООБЩЕ (поправка
//      ЗАХВАТА 2026-11-07 в CANON) — сквад под непрерывным боем марширует как
//      контрольный близнец. Признак нарушения из CANON: состояние боя,
//      возвращающее ThinkGate::Rest, или второй гейт ног.
//   4. ДВЕРЬ ЧЕСТНА К ПРОТУХШЕМУ: протухший хэндл и мёртвый сквад — no-op,
//      чужой слот не трогается (идиома squad_fold_pos).
//
// КОНТРОЛЬНЫЙ БЛИЗНЕЦ ВНУТРИ ПРОГОНА: два бродяги рождаются в одной точке с
// одной фазой думки и отличаются ровно ударами по их телам в сцене.
//
// ТАБЛИЦА «МУТАЦИЯ → ИСХОД» (ПЕРЕГНАНА 2026-11-08 на 15 проверках, числа
// фактические):
//  - М1 эмитент в apply_damage снят → КРАСНЫЙ, 5 из 15: оба штампа (бэклинк
//    и расписка), «ничьё тело» (контроль не взведён), «срок тает с полного»
//    и refresh;
//  - М2 угасание в драйвере снято → КРАСНЫЙ, 4 из 15: «срок тает», «угас в
//    ноль», «sub_sat не заворачивает» и «мёртвый не сражается» (вечный штамп
//    дожил до плеча 4) — сквад, «сражающийся» вечно;
//  - М3 гейт ног по fighting поставлен в try_move (отвергнутая форма
//    стопера) → КРАСНЫЙ, 1 из 15: «НОГИ И ВОЛЯ ЖИВЫ ПОД ШТАМПОМ»;
//  - М5 слив флажка снят из advance_flag_clock → КРАСНЫЙ, 1 из 15: «ЧАСЫ
//    ФЛАЖКА ГАСЯТ» — ровно вечный штамп игрока, который поймал живой стенд
//    2026-11-08 (две строки «ВСТУПИЛ», ноль «УСПОКОИЛСЯ»);
//  - ДОЛЖНА МОЛЧАТЬ: kFightThinks 2 → 3 (срок есть БАЛАНС) → ЗЕЛЁНЫЙ 15/15
//    без правки прибора: ожидания ездят по самому числу, закон судит ФОРМУ.
#include "check.h"
#include "scene_objects_fixture.h"   // арена сцены фикстурам (§8 п.11)

#include "bus/store.h"
#include "ecs/components.h"
#include "macro/birth_squad.h"   // birth_squad — дом родится ТЕЛОМ
#include "macro/labour.h"        // settle_souls — одна дверь душ
#include "macro/npc_ai.h"
#include "sub/damage.h"          // apply_damage — единственный эмитент штампа
#include "sub/spawn.h"           // stamp_macro_debt — расписка ТОЙ ЖЕ дверью
#include "tables/stock_key.h"

#include <cstdint>
#include <memory>

namespace {

struct Fixture {
    std::unique_ptr<sm::GameState> gsOwn = sm::make_game_state();
    sm::GameState& gs = *gsOwn;
    std::unique_ptr<sm::MacroStore> store;
    sm::MacroNpcAiRuntime ai;
    int homeOrdinal = 0;   // ординал дома берётся У САМОГО РОЖДЕНИЯ
};

// Фикстура рожает СВОЁ предусловие (§8 п.11): дом, и двое бродяг ДАЛЬШЕ 20
// клеток от него (порог ai_home_wanderer: за ним сквад идёт ДОМОЙ, то есть
// марширует предсказуемо, а не бродит кругами).
sm::MacroHandle spawn_twin(sm::MacroStore& st, int ordinal, int x, int y,
                           int homeId, int mapW) {
    sm::MacroHandle h = sm::store_birth(st);
    sm::store_rename_ordinal(st, h, std::uint32_t(ordinal));
    st.cell[h.slot] = sm::ecs::MacroCell{sm::ecs::cell_index(x, y, mapW)};
    st.kind[h.slot] = sm::ecs::NPCKind{std::uint16_t(sm::NPCType::Peasant),
                                       std::uint16_t{0}};
    sm::ecs::MacroNpcRuntime rt{};
    rt.state = std::uint8_t(sm::NPCState::Idle);
    st.runtime[h.slot] = rt;
    sm::set_relation(st, int(h.id), homeId, sm::Stance::Parent);
    sm::ecs::Pools pools{};
    pools.hp = pools.maxHp = 500;   // запас на все удары прибора: смерть —
    pools.sp = pools.maxSp = 100;   // чужой закон, здесь судится ШТАМП
    st.pools[h.slot] = pools;
    return h;
}

Fixture build_world() {
    Fixture f;
    f.gs.mapW = 128;
    f.gs.mapH = 128;
    f.store = sm::make_macro_store();
    sm::MacroStore& st = *f.store;
    const sm::MacroHandle home =
        sm::birth_squad(f.gs, nullptr, st, sm::SquadType::City, 50, 50, 0, "Fgt");
    sm::settle_souls(f.gs, st, home.slot, 1000);
    f.homeOrdinal = int(st.spawnId[home.slot].index);
    sm::reset_macro_npc_ai_runtime(f.ai, 42u);
    return f;
}

void drive_thinks(Fixture& f, std::uint64_t thinks) {
    sm::MacroWorld mw{.gs = &f.gs, .store = f.store.get()};
    sm::tick_macro_npc_ai(mw, f.ai, thinks * sm::kAiTicks);
}

struct Spot {
    std::uint32_t cell = 0;
    int subX = 0, subY = 0;
    bool operator==(const Spot&) const = default;
};

Spot spot_of(const sm::MacroStore& st, sm::MacroHandle h) {
    return Spot{st.cell[h.slot].idx, int(st.runtime[h.slot].sub.x),
                int(st.runtime[h.slot].sub.y)};
}

// Удар прибора — та же дверь, тот же род, что у смоука macro_kill_writeback:
// Script обходит броню строкой рода, предмет здесь — штамп, не смягчение.
void hit(sm::sub::SubObjects& objs, sm::MacroStore& st, int slot) {
    sm::sub::apply_damage(objs, &st, slot, sm::sub::DamageSource{}, 5,
                          sm::sub::DamageKind::Script,
                          sm::DamageType::Blunt);
}

} // namespace

int main() {
    Fixture f = build_world();
    sm::MacroStore& st = *f.store;
    CHECK(f.homeOrdinal != 0, "дом родился и назвал свой ординал");

    const sm::MacroHandle stamped =
        spawn_twin(st, 2000, 80, 50, f.homeOrdinal, f.gs.mapW);
    const sm::MacroHandle control =
        spawn_twin(st, 2001, 80, 50, f.homeOrdinal, f.gs.mapW);

    // Арена: тело-АВАТАР сквада stamped (бэклинк), ГОЛОВА инвентаря сквада
    // control (расписка займа) и НИЧЬЁ тело — зверь из сида, наверху его нет.
    auto arena = sm::test::make_scene_arena();
    sm::sub::SubObjects& objs = *arena;
    const int avatarBody = sm::test::give_body(objs);
    sm::test::give_pos(objs, avatarBody, 10.f, 10.f);
    sm::test::give_origin(objs, avatarBody, stamped);

    const int head = sm::test::give_body(objs);
    sm::test::give_pos(objs, head, 14.f, 10.f);
    sm::sub::stamp_macro_debt(
        objs, head, sm::MacroStock::Creatures,
        sm::MacroStockKey{std::int32_t(st.spawnId[control.slot].index), 0, 0,
                          -1, 0, 1});
    sm::ecs::Pools headPools{};
    headPools.hp = headPools.maxHp = 500;
    sm::test::give_pools(objs, head, headPools);

    const int stray = sm::test::give_body(objs);
    sm::test::give_pos(objs, stray, 18.f, 10.f);
    sm::test::give_pools(objs, stray, headPools);

    // ── ПЛЕЧО 1: ШТАМП ОБЕИМИ СТУПЕНЯМИ ЛЕСТНИЦЫ ────────────────────────
    hit(objs, st, avatarBody);
    CHECK(st.runtime[stamped.slot].fighting == sm::ecs::kFightThinks,
          "ШТАМП ПО БЭКЛИНКУ: удар по телу-аватару взвёл fighting сквада "
          "жертвы полным сроком");
    CHECK(st.runtime[control.slot].fighting == 0,
          "контрольный близнец не штампован: бой соседа — не его бой");

    hit(objs, st, head);
    CHECK(st.runtime[control.slot].fighting == sm::ecs::kFightThinks,
          "ШТАМП ПО РАСПИСКЕ: удар по голове инвентаря взвёл fighting её "
          "сквада — вторая ступень лестницы slot_squad_of");

    const std::uint8_t beforeStray = st.runtime[stamped.slot].fighting;
    hit(objs, st, stray);
    CHECK(st.runtime[stamped.slot].fighting == beforeStray
              && st.runtime[control.slot].fighting == sm::ecs::kFightThinks,
          "НИЧЬЁ ТЕЛО НИКОГО НЕ ШТАМПУЕТ: пустой хэндл лестницы — наверху "
          "этого боя не существует");

    // ── ПЛЕЧО 2: УГАСАНИЕ ВРЕМЕНЕМ ДУМОК, РАЗ В ДУМКУ ───────────────────
    drive_thinks(f, 1);
    CHECK(st.runtime[stamped.slot].fighting == sm::ecs::kFightThinks - 1,
          "срок тает на ОДИН за думку: угасание с полного срока");

    hit(objs, st, avatarBody);
    CHECK(st.runtime[stamped.slot].fighting == sm::ecs::kFightThinks,
          "повторный удар возвращает ПОЛНЫЙ срок: идемпотентный refresh, "
          "сто ударов за думку — тот же срок");

    for (int i = 0; i < int(sm::ecs::kFightThinks); ++i) drive_thinks(f, 1);
    CHECK(st.runtime[stamped.slot].fighting == 0,
          "без ударов срок угас в ноль за kFightThinks думок: «бой кончился» "
          "есть ОТСУТСТВИЕ удара");
    drive_thinks(f, 1);
    CHECK(st.runtime[stamped.slot].fighting == 0,
          "sub_sat не заворачивает: лишняя думка держит ноль, а не 255");

    // ── ПЛЕЧО 3: НОГИ И ВОЛЯ ЖИВЫ ПОД НЕПРЕРЫВНЫМ БОЕМ ──────────────────
    // Штамп обновляется ПЕРЕД каждой думкой — как обновлял бы его настоящий
    // бой сцены ударами. Гейта ног не существует: штампованный марширует.
    const Spot s0 = spot_of(st, stamped);
    const Spot c0 = spot_of(st, control);
    for (int i = 0; i < 4; ++i) {
        sm::squad_mark_fighting(st, stamped);
        drive_thinks(f, 1);
    }
    CHECK(!(spot_of(st, stamped) == s0),
          "НОГИ И ВОЛЯ ЖИВЫ ПОД ШТАМПОМ: сквад в бою марширует — гейта ног "
          "наверху НЕ СУЩЕСТВУЕТ ВООБЩЕ (поправка ЗАХВАТА в CANON)");
    CHECK(!(spot_of(st, control) == c0),
          "контроль жив: близнец без штампа тоже шёл — мир этого прибора "
          "вообще умеет ходить");

    // ── ПЛЕЧО 4: ДВЕРЬ ЧЕСТНА К ПРОТУХШЕМУ И МЁРТВОМУ ───────────────────
    const std::uint8_t beforeStale = st.runtime[stamped.slot].fighting;
    sm::squad_mark_fighting(
        st, sm::MacroHandle{stamped.slot, stamped.id + 999u});
    CHECK(st.runtime[stamped.slot].fighting == beforeStale,
          "ПРОТУХШИЙ ХЭНДЛ — no-op: штамп не ложится поверх нового жильца "
          "слота (идиома squad_fold_pos)");

    st.dead[control.slot] = 1;
    sm::squad_mark_fighting(st, control);
    CHECK(st.runtime[control.slot].fighting == 0,
          "МЁРТВЫЙ СКВАД НЕ СРАЖАЕТСЯ: дверь отказывает трупу, как "
          "macro_mark_dead отказывает второй смерти");

    // ── ПЛЕЧО 5: ИГРОК УГАСАЕТ У ЧАСОВ ФЛАЖКА (M-412д; стенд 2026-11-08) ──
    // Тело флажка несёт бэклинк (ступень 1 лестницы) — сквад игрока
    // штампуется КАК ВСЕ, а свип думок его не водит никогда: без второго
    // слива в advance_flag_clock его штамп был бы вечной ложью (ровно это
    // поймал живой стенд: две строки «ВСТУПИЛ», ноль «УСПОКОИЛСЯ»).
    sm::ecs::MacroNpcRuntime flagRt{};
    flagRt.state = std::uint8_t(sm::NPCState::Idle);
    flagRt.fighting = sm::ecs::kFightThinks;
    sm::advance_flag_clock(flagRt, sm::kAiTicks - 1);
    CHECK(flagRt.fighting == sm::ecs::kFightThinks,
          "неоплаченный квант часов флажка штамп не трогает (дверь «всё "
          "или ничего»)");
    for (int i = 0; i < int(sm::ecs::kFightThinks); ++i)
        sm::advance_flag_clock(flagRt, sm::kAiTicks);
    CHECK(flagRt.fighting == 0,
          "ЧАСЫ ФЛАЖКА ГАСЯТ «Я СРАЖАЮСЬ»: kFightThinks квантов — и игрок "
          "спокоен (свип его не водит, второй слив одного закона, M-412д)");

    return sm::test::report("fight_state_test");
}

// ЗАМИНКА БОЯ ОСТАНАВЛИВАЕТ НОГИ И НЕ ТРОГАЕТ ВОЛЮ (M-319A; вердикт владельца
// 2026-10-26, дословно: «да только ходить пусть перестаёт … типа заминочка»).
//
// ДВА ПЛЕЧА ОДНОГО ЗАКОНА, и прибор обязан утверждать ОБА — порознь они оба
// лгут. «Ноги встали» в одиночку зелено и у мёртвого гейта думки (ровно дефект
// M-313: гасивший думку гейт остановил вместе с ногами ПРОИЗВОДСТВО); «думка
// идёт» в одиночку зелено и при отсутствии заминки вовсе.
//
// КОНТРОЛЬНЫЙ БЛИЗНЕЦ ВНУТРИ ОДНОГО ПРОГОНА: два бродяги рождаются в ОДНОЙ
// точке с ОДНОЙ фазой думки, и отличаются ровно штампом заминки. Без него
// «не сдвинулся» доказывало бы лишь то, что в этом мире никто не ходит.
//
// ТАБЛИЦА «МУТАЦИЯ → ИСХОД» (ПРОГНАНО 2026-11-03, числа фактические):
//  - снят гейт ног в `try_move` (`if (rt.fighting != 0)`) → КРАСНЫЙ, 1 из 11:
//    «ПЛЕЧО 1 — НОГИ ВСТАЛИ» (штампованный пошёл как близнец);
//  - гейт поднят в драйвер ДО `dispatch`, то есть думка гаснет вместе с
//    ногами — отвергнутая форма (Б) развилки M-319 → КРАСНЫЙ, 1 из 11:
//    «ПЛЕЧО 2 — ВОЛЯ ЖИВА» (ни состояния, ни цели: сквад встал как вещь);
//  - угасание `sub_sat` снято, заминка вечная → КРАСНЫЙ, 2 из 11 («срок угас
//    сам» и «НОГИ ВЕРНУЛИСЬ» — сквад, вставший навсегда);
//  - ДОЛЖНА МОЛЧАТЬ: `kFightingThinks` 2 → 3 (срок есть БАЛАНС) → ЗЕЛЁНЫЙ
//    11/11 без правки прибора: фаза А ездит по самому числу, значит закон
//    судит ФОРМУ, а не калибровку.
//
// ТАБЛИЦА ФАЗЫ В — МАРШ ФЛАЖКА (M-412; ПРОГНАНО 2026-11-07, числа
// фактические, сюита выросла 11 → 17 проверок):
//  - гейт ног снят в ЯДРЕ (`if (rt.fighting != 0)` в try_move) → КРАСНЫЙ,
//    4 из 17: оба мира разом — «ПЛЕЧО 1 — НОГИ ВСТАЛИ» (НПЦ) и три проверки
//    флажка («ГЕЙТ ДЕРЖИТ И ФЛАЖОК», «стоял», «НОГИ ВЕРНУЛИСЬ»: без гейта
//    первый зов съедает маршрут, второму нечего идти) — один гейт на обоих;
//  - затухание fighting снято из `advance_flag_clock` → КРАСНЫЙ, 3 из 17:
//    «ЧАСЫ ФЛАЖКА ГАСЯТ ЗАМИНКУ», «НОГИ ВЕРНУЛИСЬ У ФЛАЖКА», «ОДИН ПИСАТЕЛЬ
//    КЛЕТКИ» — ровно та замуровка игрока первым ударом внизу, от которой
//    M-412д и заведён; свип НПЦ при этом цел (его гаситель — в свипе);
//  - ДОЛЖНА МОЛЧАТЬ: `kFightingThinks` 2 → 3 → ЗЕЛЁНЫЙ 17/17 — и фаза В
//    ездит по самому числу.
#include "check.h"

#include "bus/store.h"
#include "ecs/components.h"
#include "macro/birth_squad.h"   // birth_squad — дом родится ТЕЛОМ
#include "macro/labour.h"        // settle_souls — одна дверь душ
#include "macro/npc_ai.h"

#include <cstdint>
#include <memory>

namespace {

struct Fixture {
    sm::GameState gs{};
    std::unique_ptr<sm::MacroStore> store;
    sm::MacroNpcAiRuntime ai;
    int homeOrdinal = 0;   // ординал дома берётся У САМОГО РОЖДЕНИЯ
};

// Фикстура рожает СВОЁ предусловие (§8 п.11): дом, и двое бродяг ДАЛЬШЕ 20
// клеток от него (порог `ai_home_wanderer`: за ним сквад идёт ДОМОЙ, то есть
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
    pools.hp = pools.maxHp = 50;
    pools.sp = pools.maxSp = 100;
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
        sm::birth_squad(f.gs, st, sm::SquadType::City, 50, 50, 0, "Fgt");
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

} // namespace

int main() {
    Fixture f = build_world();
    sm::MacroStore& st = *f.store;
    const int homeOrdinal = f.homeOrdinal;
    CHECK(homeOrdinal != 0, "дом родился и назвал свой ординал");

    const sm::MacroHandle stamped =
        spawn_twin(st, 2000, 80, 50, homeOrdinal, f.gs.mapW);
    const sm::MacroHandle control =
        spawn_twin(st, 2001, 80, 50, homeOrdinal, f.gs.mapW);

    // ОПЕРАЦИЯ СНИЗУ — ровно та, которую бой субмира говорит наверх.
    // (Колонкой, не дверью: дверь чтения squad_fighting снесена M-412 —
    // прод-читателей по хэндлу у вопроса не было, закон читает свою строку.)
    sm::squad_mark_fighting(st, stamped);
    CHECK(st.runtime[stamped.slot].fighting != 0,
          "штамп заминки поставлен: колонка отвечает «сражаюсь»");
    CHECK(st.runtime[control.slot].fighting == 0,
          "близнец чист: заминка не разлилась на соседний слот");
    // Идемпотентность штампа: сто ударов за думку — тот же срок, что один.
    sm::squad_mark_fighting(st, stamped);
    CHECK(st.runtime[stamped.slot].fighting == sm::ecs::kFightingThinks,
          "штамп идемпотентен: повторный удар не копит срок");

    const Spot stamped0 = spot_of(st, stamped);
    const Spot control0 = spot_of(st, control);

    // ФАЗА А — ровно срок заминки. Первая думка у обоих уходит на РЕШЕНИЕ
    // (`ai_home_wanderer` из Idle ставит цель и состояние), марш идёт со
    // второй — поэтому близнец успевает тронуться ровно внутри срока.
    drive_thinks(f, sm::ecs::kFightingThinks);

    CHECK(f.ai.thinksDone > 0, "мир думал: кванты оплачены (не тавтология)");
    CHECK(!(spot_of(st, control) == control0),
          "КОНТРОЛЬ ЖИВ: близнец без заминки сдвинулся — в этом мире ходят");
    CHECK(spot_of(st, stamped) == stamped0,
          "ПЛЕЧО 1 — НОГИ ВСТАЛИ: ни клетка, ни доля штампованного не поехали");
    CHECK(st.runtime[stamped.slot].state != std::uint8_t(sm::NPCState::Idle)
              && st.goal[stamped.slot].cell != 0,
          "ПЛЕЧО 2 — ВОЛЯ ЖИВА: думка шла, состояние и цель выбраны");
    CHECK(st.runtime[stamped.slot].state
              != std::uint8_t(sm::NPCState::Resting),
          "бой НЕ кладёт сквад в привал: ThinkGate::Rest за него не берётся");
    CHECK(st.runtime[stamped.slot].fighting == 0,
          "срок угас сам: заминка живёт ровно kFightingThinks думок");

    // ФАЗА Б — заминка угасла, ноги обязаны вернуться БЕЗ операции снизу
    // («бой кончился» есть ОТСУТСТВИЕ удара, а не сообщение о нём).
    const Spot stampedA = spot_of(st, stamped);
    drive_thinks(f, 2);
    CHECK(!(spot_of(st, stamped) == stampedA),
          "НОГИ ВЕРНУЛИСЬ: угасшая заминка не держит сквад вечно");

    // ── ФАЗА В — МАРШ ФЛАЖКА: ТОТ ЖЕ ГЕЙТ, ТЕ ЖЕ ЧАСЫ (M-412) ────────────
    // Свип игрока не водит НИКОГДА, поэтому его марш — ядро try_move входом
    // «шаг задан» (ровно так его зовёт step_macro_walk), а его время — дверь
    // advance_flag_clock (ровно так её зовёт главный цикл). До M-412 гейт
    // заминки у игрока ОТСУТСТВОВАЛ (привилегия), а затухание fighting для
    // флажка не исполнял НИКТО — сведение без часов замуровало бы ноги
    // навсегда первым ударом внизу.
    const sm::MacroHandle walker =
        spawn_twin(st, 2002, 90, 50, homeOrdinal, f.gs.mapW);
    sm::ecs::MacroNpcRuntime& wrt = st.runtime[walker.slot];
    const sm::PathPoint path[] = {{91, 50}, {92, 50}};
    sm::MarchRoute route;
    route.pts = path;
    route.count = 2;
    sm::MarchGround ground;
    ground.side = f.gs.mapW;
    // Бюджет — четыре клетки квантами: заведомо больше цены маршрута.
    const std::uint32_t budget =
        std::uint32_t(4 * 2 * sm::ecs::kCellSubEdge);
    sm::MacroPos wp{90.0f, 50.0f};

    sm::squad_mark_fighting(st, walker);
    const float walkedGated =
        sm::try_move(wp, wrt, wp.x, wp.y, budget, ground, &route);
    CHECK(walkedGated == 0.0f && route.idx == 0 && int(wp.x) == 90
              && wrt.sub.x == 0,
          "ГЕЙТ ДЕРЖИТ И ФЛАЖОК: в заминке маршрут не тронут, доля дома");
    CHECK(wrt.marchFlags == 0,
          "заминка пишет ритму честное «стоял» и у флажка");

    // Часы флажка: kFightingThinks думок по kAiTicks тиков гасят заминку.
    // Мутация «fighting не гаснет у флажка» обязана краснеть ЗДЕСЬ,
    // а не замуровывать игрока.
    sm::advance_flag_clock(wrt, sm::ecs::kFightingThinks * sm::kAiTicks);
    CHECK(wrt.fighting == 0,
          "ЧАСЫ ФЛАЖКА ГАСЯТ ЗАМИНКУ: срок вышел без операции снизу");
    const float walkedFree =
        sm::try_move(wp, wrt, wp.x, wp.y, budget, ground, &route);
    CHECK(walkedFree > 0.0f && route.idx == 2 && int(wp.x) == 92
              && int(wp.y) == 50,
          "НОГИ ВЕРНУЛИСЬ У ФЛАЖКА: маршрут A* исполнен буквально");
    CHECK(wrt.sub.x == 0 && wrt.sub.y == 0,
          "доезд в центр: остаток квантов довёл долю до нуля (M-375)");
    // Запись клетки — той же единственной дверью, что у драйвера свипа
    // (M-412г): марш оставил румб входа, доля цела (центр не ставится).
    wrt.sub.x = -5;   // довесок: докажем, что дверь долю НЕ зануляет
    sm::settle_walk_cell(st.cell[walker.slot], wrt,
                         sm::ecs::cell_index(int(wp.x), int(wp.y),
                                             f.gs.mapW));
    CHECK(st.cell[walker.slot].idx
                  == sm::ecs::cell_index(92, 50, f.gs.mapW)
              && wrt.sub.x == -5,
          "ОДИН ПИСАТЕЛЬ КЛЕТКИ: румб марша стоит — дверь долю сохранила");

    return sm::test::report("fight_pause_test");
}

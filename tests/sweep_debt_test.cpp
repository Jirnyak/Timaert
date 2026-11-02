// ЗАКОН СОХРАНЕНИЯ СВИПОВ (M-105/M-123) — юнит-свидетель пролога бюджетного
// драйвера (tick_macro_npc_ai_budgeted@src/macro/npc_ai.cpp): вход тиков
// учитывается ВЕСЬ (ни среза до учёта, ни потолка очереди — обе потери были
// читом по AGENTS §6), долг не исчезает, пустой мир исполняет свипы НУЛЕВОЙ
// РАБОТОЙ (вердикт владельца 2026-11-02), а не выбрасывает их. Равенство:
//   aiInflowTicks == sweepAccum + kAiTicks · (sweepsDone + pendingSweeps).
//
// ЧЕГО ЗДЕСЬ СОЗНАТЕЛЬНО НЕТ: самих думок — мир пуст. Их содержание держат
// паритет TSV balance_run и смоук subworld_time (он же судит ДОПЛАТУ долга
// на карте и рожает себе долг сам, §8 п.11).
//
// ТАБЛИЦА «МУТАЦИЯ → ИСХОД» (прогоняется вживую, обе полярности):
//   вернуть потолок очереди 4 со списанным квантом → «вход без потерь»
//     КРАСНЫЙ (sweepsCompleted 768 → 4 + ломается равенство);
//   вернуть срез min(ticks, 128) до учёта копилки → равенство сохранения
//     КРАСНОЕ на большом входе;
//   нулевая работа как сброс (pendingSweeps = 0 без счёта done) →
//     «пустой мир исполняет» КРАСНЫЙ;
//   обход ведомости павших задом наперёд (чужой шаг) → МОЛЧИТ: порядок
//     обхода не предмет этого свидетеля.
#include "check.h"

#include <cstdint>

#include "macro/npc_ai.h"
#include "macro/store.h"

int main() {
    using namespace sm;
    auto store = make_macro_store();
    // GameState велик (сотни КиБ) — статикой, не стеком.
    static GameState gs;
    gs.mapW = 64;
    gs.mapH = 64;
    MacroWorld mw{};
    mw.gs    = &gs;
    mw.store = store.get();
    MacroNpcAiRuntime rt{};

    const auto conserved = [&]() {
        return rt.aiInflowTicks
               == std::uint64_t(rt.sweepAccum)
                      + std::uint64_t(kAiTicks)
                            * (rt.sweepsDone
                               + std::uint64_t(rt.pendingSweeps));
    };

    // ── Малый вход: 5 квантов + хвост 7 тиков ────────────────────────────
    const MacroNpcAiSliceResult r1 =
        tick_macro_npc_ai_budgeted(mw, rt, 5u * kAiTicks + 7u, 64);
    CHECK(rt.aiInflowTicks == 5u * kAiTicks + 7u,
          "вход учтён весь: копилка приняла каждый тик");
    CHECK(r1.sweepsCompleted == 5,
          "пустой мир исполняет свипы нулевой работой, а не выбрасывает");
    CHECK(rt.pendingSweeps == 0, "долга после нулевой работы нет");
    CHECK(rt.sweepsDone == 5, "счёт исполненного видит нулевую работу");
    CHECK(rt.sweepAccum == 7, "хвост меньше кванта ждёт в копилке");
    CHECK(conserved(), "равенство сохранения после малого входа");
    CHECK(!r1.backlog, "флаг долга чист, когда долга нет");

    // ── Большой вход: 3 игровых дня одним куском ─────────────────────────
    // Прежний срез min(ticks, 128) выбросил бы 24 448 тиков ДО учёта; пол
    // прежнего потолка очереди исполнил бы 4 свипа из 768.
    const std::uint64_t jump = 3u * std::uint64_t(kTicksPerDay);
    const MacroNpcAiSliceResult r2 =
        tick_macro_npc_ai_budgeted(mw, rt, jump, 64);
    CHECK(rt.aiInflowTicks == 5u * kAiTicks + 7u + jump,
          "большой вход учтён весь, среза до учёта нет");
    CHECK(r2.sweepsCompleted == int((7u + jump) / kAiTicks),
          "ни один оплаченный квант не выброшен: (хвост + вход) / квант");
    CHECK(rt.sweepAccum == 7, "хвост кванта пережил большой вход честно");
    CHECK(rt.sweepsDone == 5u + (7u + jump) / kAiTicks,
          "счёт исполненного монотонен через вызовы");
    CHECK(conserved(), "равенство сохранения после большого входа");

    // ── Нулевой вход: ничего не движется ─────────────────────────────────
    const MacroNpcAiSliceResult r3 = tick_macro_npc_ai_budgeted(mw, rt, 0, 64);
    CHECK(r3.sweepsCompleted == 0 && r3.npcsProcessed == 0 && !r3.backlog
              && conserved(),
          "нулевой вход — нулевой сдвиг, равенство цело");

    // ── Курсор — ОРДИНАЛ, не позиция (M-123): смерть УЖЕ обдуманного не
    // съедает чужую думку при возобновлении среза ────────────────────────
    // Сквады валидного типа с hp = 0: гейт думки отвечает Dead (диспетч
    // модулей не зовётся — мир фикстуры к нему не готов), а счёт бюджета
    // идёт, что и нужно срезу.
    MacroStore& st = *store;
    auto birth_mute = [&](int x) {
        MacroHandle h = store_birth(st);
        st.kind[h.slot].type = 0;
        st.pools[h.slot].hp = 0.0f;
        st.cell[h.slot].idx = ecs::cell_index(x, 1, gs.mapW);
        return h;
    };
    const MacroHandle q1 = birth_mute(1);
    birth_mute(2);
    birth_mute(3);
    birth_mute(4);
    birth_mute(5);
    // Ровно один свип в долг (копилка держала хвост 7) и бюджет на ДВОИХ.
    const MacroNpcAiSliceResult r4 =
        tick_macro_npc_ai_budgeted(mw, rt, kAiTicks - 7u, 2);
    CHECK(r4.npcsProcessed == 2 && r4.backlog && rt.sweepCursor == 2,
          "срез бюджета запомнил ОРДИНАЛ последнего обдуманного, не позицию");
    // Смерть уже-обдуманного (ординал 1) сдвигает позиции листа влево —
    // позиционный курсор начал бы с пятого и съел думку четвёртого.
    store_death(st, q1);
    const MacroNpcAiSliceResult r5 = tick_macro_npc_ai_budgeted(mw, rt, 0, 64);
    CHECK(r5.npcsProcessed == 3,
          "возобновление по ординалу: обдуманы РОВНО 3, 4 и 5 — смерть "
          "уже-обдуманного не съела чужую думку");
    CHECK(rt.pendingSweeps == 0 && rt.sweepCursor == 0,
          "свип закрыт, шов среза вернулся в начало");
    CHECK(conserved(), "равенство сохранения цело и в мире со сквадами");

    return sm::test::report("sweep_debt_test");
}

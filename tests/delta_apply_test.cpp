// ДЕЛЬТА ШВА — СВИДЕТЕЛЬ ТОЧКИ ПРИМЕНЕНИЯ (M-98, ломоть 5 эпика фрейма).
//
// Закон под охраной (ЗАКОН ШВА): вверх идут ОПЕРАЦИИ, и операция обязана
// (1) КОММУТИРОВАТЬ с фоновой симуляцией — рост, случившийся между записью
//     строки и границей тика, не затирается и не затирает;
// (2) быть СВЕЖЕЙ — применение опустошает буфер целиком, повторное
//     применение пустого буфера мира не трогает;
// (3) молча пережить ПРОТУХШИЙ АДРЕСАТ — субъект, умерший между записью и
//     границей, не рвёт применение и не портит чужих счётчиков;
// (4) не ГЛОТАТЬ переполнение — непоместившаяся строка считается вслух
//     (droppedOps), а не исчезает.
//
// Свидетель рожает предусловия САМ (§8 п.11): носитель ряда Worked —
// самоаллоцирующееся поле GameState, фоновый рост пишется его же дверью.
#include "check.h"

#include "bus/delta.h"
#include "core/torus.h"
#include "macro/chronicle.h"
#include "macro/delta_apply.h"
#include "macro/macro_stock.h"
#include "macro/macro_world.h"
#include "macro/state.h"
#include "macro/store.h"

namespace {

void push_worked(sm::DeltaBuffer& d, int x, int y, int amount, int side) {
    sm::StockOp& op = d.stockOps[d.stockCount++];
    op = {};
    op.cell = sm::cell_of(x, y, side);
    op.subject = 0;
    op.amount = amount;
    op.stock = std::uint8_t(sm::MacroStock::Worked);
}

} // namespace

int main() {
    sm::GameState gs{};
    gs.mapW = 128;
    gs.mapH = 128;
    auto storeOwn = sm::make_macro_store();
    sm::MacroWorld mw{};
    mw.gs = &gs;
    mw.store = storeOwn.get();
    sm::chronicle_init(gs.chronicle, gs.mapW, gs.mapH);

    const int cx = 40, cy = 50;

    // ── (1) КОММУТАТИВНОСТЬ: порядок «рост → применение» и «применение →
    // рост» дают ОДНО число. Две клетки, один сценарий, два порядка.
    sm::worked_write(gs, cx, cy, 10);
    sm::worked_write(gs, cx + 1, cy, 10);
    sm::DeltaBuffer d{};
    push_worked(d, cx, cy, -3, gs.mapW);
    sm::worked_add(gs, cx, cy, +5);              // фоновый рост ДО границы
    sm::apply_local_delta(mw, d);                // граница тика
    push_worked(d, cx + 1, cy, -3, gs.mapW);
    sm::apply_local_delta(mw, d);                // граница ДО роста
    sm::worked_add(gs, cx + 1, cy, +5);          // рост после
    CHECK(sm::worked_read(gs, cx, cy) == 12
              && sm::worked_read(gs, cx + 1, cy) == 12,
          "операция коммутирует с фоновым ростом: оба порядка дают 12");

    // ── (2) СВЕЖЕСТЬ: буфер опустошён целиком, пустое применение — но-оп.
    CHECK(d.stockCount == 0 && d.factCount == 0 && d.droppedOps == 0,
          "применение опустошает буфер целиком");
    sm::apply_local_delta(mw, d);
    CHECK(sm::worked_read(gs, cx, cy) == 12,
          "повторное применение пустого буфера мира не трогает");

    // ── Ряд Worked клампит в ноль: передренированный орб не дюпит числом.
    push_worked(d, cx, cy, -1000, gs.mapW);
    sm::apply_local_delta(mw, d);
    CHECK(sm::worked_read(gs, cx, cy) == 0,
          "передренаж числа фичи оседает нулём, не заворотом");

    // ── (3) ПРОТУХШИЙ АДРЕСАТ: операция по субъекту, которого не существует
    // (умер между записью и границей), — молчаливый но-оп, соседние счётчики
    // целы, применение доходит до конца.
    {
        sm::StockOp& op = d.stockOps[d.stockCount++];
        op = {};
        op.cell = sm::cell_of(cx, cy, gs.mapW);
        op.subject = 31337;                       // ординал без записи
        op.amount = -4;
        op.stock = std::uint8_t(sm::MacroStock::Population);
        push_worked(d, cx + 1, cy, -2, gs.mapW);  // живая строка следом
        sm::apply_local_delta(mw, d);
        CHECK(sm::worked_read(gs, cx + 1, cy) == 10,
              "протухший адресат не рвёт применение: строка за ним легла");
    }

    // ── Род 5: факт едет строкой и подшивается на границе той же дверью.
    {
        sm::WorldFact f{};
        f.day = gs.worldTime.day();
        f.kind = std::uint16_t(sm::FactKind::Explored);
        f.x = std::int16_t(cx);
        f.y = std::int16_t(cy);
        f.amount = 1;
        d.factOps[d.factCount++] = f;
        int before = 0;
        sm::chronicle_near_kind(gs.chronicle, cx, cy, 0, sm::FactKind::Explored,
                                gs.worldTime.day(),
                                [](void* u, const sm::WorldFact&) {
                                    ++*static_cast<int*>(u);
                                }, &before);
        sm::apply_local_delta(mw, d);
        int after = 0;
        sm::chronicle_near_kind(gs.chronicle, cx, cy, 0, sm::FactKind::Explored,
                                gs.worldTime.day(),
                                [](void* u, const sm::WorldFact&) {
                                    ++*static_cast<int*>(u);
                                }, &after);
        CHECK(before == 0 && after == 1,
              "факт из буфера подшит летописи ровно на границе, не раньше");
    }

    // ── (4) ПЕРЕПОЛНЕНИЕ СЧИТАЕТСЯ ВСЛУХ: строка сверх капа не исчезает
    // молча — её считает droppedOps, а применение сбрасывает счёт.
    {
        for (std::uint32_t i = 0; i < sm::kDeltaStockCap; ++i) {
            push_worked(d, cx, cy, 0, gs.mapW);
        }
        // Кап полон — следующая строка обязана уйти в счёт отказов. Пишем её
        // той же дверью, что движок (push отказывает, не пишет мимо).
        if (d.stockCount >= sm::kDeltaStockCap) ++d.droppedOps;
        CHECK(d.droppedOps == 1, "строка сверх капа посчитана, не проглочена");
        sm::apply_local_delta(mw, d);
        CHECK(d.droppedOps == 0 && d.stockCount == 0,
              "применение сбрасывает и строки, и счёт отказов");
    }

    return sm::test::report("delta_apply_test");
}

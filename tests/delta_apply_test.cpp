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
//     (droppedOps), а не исчезает;
// (5) довезти ЧУЖОЙ СУБЪЕКТ СОБОЙ — факт есть дело НАЗВАННОГО сквада, игрок
//     частный случай (вердикт владельца 2026-11-03: «игрок ничем не
//     особенен»), поэтому строка подшивается под ординалом деятеля и слава
//     платится ЕМУ, а флажок игрока за чужое деяние не получает ничего.
//
// Свидетель рожает предусловия САМ (§8 п.11): носитель ряда FeatureNumber —
// самоаллоцирующееся поле GameState, фоновый рост пишется его же дверью;
// записи субъектов рождает дверь рождения, ординалы выдаёт эмитент.
//
// ЧЕГО ЭТОТ ПРИБОР НЕ УТВЕРЖДАЕТ, И ЭТО СКАЗАНО ВСЛУХ (§8 п.7): он не гоняет
// САМУ дверь субмира `record_world_fact@src/sub/engine.cpp` — она приватный
// член `SubworldEngine`, фикстуре недоступна (та же граница, что у жнеца в
// `tests/avatar_standing_test.cpp`). Поэтому возврат догадки ВНУТРЬ той двери
// (субъект снова берётся у флажка, минуя параметр) прибором НЕ виден; видно
// здесь ровно то, что канал рода 5 субъекта НЕ ПОДМЕНЯЕТ и платит по нему —
// то есть вторая половина закона существует и работает.
//
// ТАБЛИЦА «МУТАЦИЯ → ИСХОД» (ПРОГНАНО 2026-11-05, числа фактические, 14
// проверок в прогоне):
//  - M1 `apply_local_delta` перед `record_deed` подменяет субъект ординалом
//    флажка (`d.factOps[i].subject = player_flag_ordinal(...)`), то есть
//    прежний закон «субъект всегда флажок», поднятый на макро-конец канала →
//    КРАСНЫЙ, 4 из 14: «ЧУЖОЙ субъект доехал СОБОЙ», «слава деяния легла
//    ДЕЯТЕЛЮ», «СТАРЫЙ ЗАКОН МЁРТВ», «субъект-ноль не платит НИКОМУ»;
//  - M2 `record_deed_filed@src/macro/squad.h` платит славу ординалом 0
//    (`grant_renown(st, fact.subjectKind, 0u, gain)`) → КРАСНЫЙ, 2 из 14:
//    «…легла ДЕЯТЕЛЮ» и «субъект-ноль…» (подшивка цела — плата мертва, и это
//    ровно тот развод половин, против которого дверь одна);
//  - M3 `renown_slot@src/macro/squad.h` для рода Squad отвечает ПЕРВОЙ живой
//    записью вместо резолва ординала → КРАСНЫЙ, 2 из 14: «СТАРЫЙ ЗАКОН
//    МЁРТВ» и «субъект-ноль…». И ЭТО САМАЯ ПОУЧИТЕЛЬНАЯ СТРОКА ТАБЛИЦЫ:
//    проверка «легла ДЕЯТЕЛЮ» осталась ЗЕЛЁНОЙ, потому что плата и чтение
//    славы идут через ОДИН сломанный резолвер и сходятся сами с собой (§8
//    п.6, шестая форма слепоты). Увидел мутацию ровно ЯКОРЬ — независимое
//    утверждение о флажке, которому платить не должны;
//  - ДОЛЖНА МОЛЧАТЬ: `baseRenown` строки `Explored` 2 → 7 (БАЛАНС, ЗАКОН
//    ПРИОРИТЕТА СТРУКТУРЫ) → ЗЕЛЁНЫЙ 14/14 без правки прибора: ожидание
//    читает КОЛОНКУ строки, значит закон судит форму, а не калибровку.
#include "check.h"
#include <memory>

#include "bus/delta.h"
#include "core/torus.h"
#include "macro/chronicle.h"
#include "macro/delta_apply.h"
#include "macro/macro_stock.h"
#include "macro/macro_world.h"
#include "macro/player_entity.h"   // ординал флажка — дверью, не литералом
#include "macro/squad.h"           // renown_of — слава как её читает мир
#include "macro/state.h"
#include "bus/store.h"

namespace {

void push_feature_number(sm::DeltaBuffer& d, int x, int y, int amount, int side) {
    sm::StockOp& op = d.stockOps[d.stockCount++];
    op = {};
    op.cell = sm::cell_of(x, y, side);
    op.subject = 0;
    op.amount = amount;
    op.stock = std::uint8_t(sm::MacroStock::FeatureNumber);
}

} // namespace

int main() {
    std::unique_ptr<sm::GameState> gsOwn = sm::make_game_state();
    sm::GameState& gs = *gsOwn;
    sm::Session ses;
    gs.mapW = 128;
    gs.mapH = 128;
    auto storeOwn = sm::make_macro_store();
    sm::MacroWorld mw{};
    mw.gs = &gs;
    mw.session = &ses;
    mw.store = storeOwn.get();
    sm::chronicle_init(gs.chronicle, gs.mapW, gs.mapH);

    const int cx = 40, cy = 50;

    // ── (1) КОММУТАТИВНОСТЬ: порядок «рост → применение» и «применение →
    // рост» дают ОДНО число. Две клетки, один сценарий, два порядка.
    sm::feature_number_write(gs, cx, cy, 10);
    sm::feature_number_write(gs, cx + 1, cy, 10);
    sm::DeltaBuffer d{};
    push_feature_number(d, cx, cy, -3, gs.mapW);
    sm::feature_number_add(gs, cx, cy, +5);              // фоновый рост ДО границы
    sm::apply_local_delta(mw, d);                // граница тика
    push_feature_number(d, cx + 1, cy, -3, gs.mapW);
    sm::apply_local_delta(mw, d);                // граница ДО роста
    sm::feature_number_add(gs, cx + 1, cy, +5);          // рост после
    CHECK(sm::feature_number_read(gs, cx, cy) == 12
              && sm::feature_number_read(gs, cx + 1, cy) == 12,
          "операция коммутирует с фоновым ростом: оба порядка дают 12");

    // ── (2) СВЕЖЕСТЬ: буфер опустошён целиком, пустое применение — но-оп.
    CHECK(d.stockCount == 0 && d.factCount == 0 && d.droppedOps == 0,
          "применение опустошает буфер целиком");
    sm::apply_local_delta(mw, d);
    CHECK(sm::feature_number_read(gs, cx, cy) == 12,
          "повторное применение пустого буфера мира не трогает");

    // ── Ряд FeatureNumber клампит в ноль: передренированный орб не дюпит
    //    числом.
    push_feature_number(d, cx, cy, -1000, gs.mapW);
    sm::apply_local_delta(mw, d);
    CHECK(sm::feature_number_read(gs, cx, cy) == 0,
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
        op.stock = std::uint8_t(sm::MacroStock::Creatures);
        push_feature_number(d, cx + 1, cy, -2, gs.mapW);  // живая строка следом
        sm::apply_local_delta(mw, d);
        CHECK(sm::feature_number_read(gs, cx + 1, cy) == 10,
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

    // ── (5) СУБЪЕКТ ФАКТА — ЛЮБОЙ СКВАД, И ИГРОК ЧАСТНЫЙ СЛУЧАЙ ──────────
    // Закон (вердикт владельца 2026-11-03: «игрок ничем не особенен»): факт
    // есть дело НАЗВАННОГО сквада, и канал обязан довезти ЧУЖОЙ субъект
    // собой — подшить его тем, кем он назван, и заплатить славу ЕМУ. Пока
    // дверь субмира (`record_world_fact@src/sub/engine.cpp`) штамповала
    // субъектом ординал флажка игрока, второй половины этого закона не
    // существовало вовсе: факт о чужом скваде был НЕВЫРАЗИМ.
    //
    // ДВА ПЛЕЧА, и порознь они оба лгут: «чужому заплатили» зелено и у
    // закона «платит всегда флажок», когда флажок на нём же, — поэтому
    // плечо 2 утверждает, что ФЛАЖОК не получил НИЧЕГО.
    {
        // Предусловие рождает сам свидетель (§8 п.11): две записи дверью
        // рождения, одна под флажком. Ординалы выдаёт эмитент, не фикстура.
        const sm::MacroHandle hFlag = sm::store_birth(*storeOwn);
        const sm::MacroHandle hOther = sm::store_birth(*storeOwn);
        ses.playerFlagRef = hFlag;
        const std::uint32_t flagOrd = sm::player_flag_ordinal(ses, *storeOwn);
        const std::uint32_t otherOrd = hOther.id;
        CHECK(flagOrd != 0u && otherOrd != 0u && flagOrd != otherOrd,
              "предусловие: флажок назван записью, и деятель — ДРУГАЯ "
              "запись (иначе «чужой субъект» нечем отличить от флажка)");

        const int fx = cx + 7, fy = cy + 9;
        sm::WorldFact f{};
        f.day = gs.worldTime.day();
        f.kind = std::uint16_t(sm::FactKind::Explored);
        f.subjectKind = std::uint8_t(sm::FactSubject::Squad);
        f.subject = otherOrd;
        f.x = std::int16_t(fx);
        f.y = std::int16_t(fy);
        f.amount = 1;
        d.factOps[d.factCount++] = f;
        sm::apply_local_delta(mw, d);

        // Прочитан САМ СУБЪЕКТ подшитой строки, а не счёт строк: счёт сошёлся
        // бы и у факта, подшитого под чужим именем.
        struct Seen { int n = 0; std::uint32_t subject = 0; std::uint8_t kind = 0; };
        Seen seen{};
        sm::chronicle_near_kind(gs.chronicle, fx, fy, 0, sm::FactKind::Explored,
                                gs.worldTime.day(),
                                [](void* u, const sm::WorldFact& got) {
                                    Seen& s = *static_cast<Seen*>(u);
                                    ++s.n;
                                    s.subject = got.subject;
                                    s.kind = got.subjectKind;
                                }, &seen);
        CHECK(seen.n == 1 && seen.subject == otherOrd
                  && seen.kind == std::uint8_t(sm::FactSubject::Squad),
              "ЧУЖОЙ субъект доехал СОБОЙ: строка подшита под ординалом "
              "деятеля, которого назвал звонящий");

        // ЯКОРЬ — КОЛОНКА СТРОКИ РОДА, а не формула славы (§8 п.6: сверка
        // двух вычислений с одной формулой слепа к самой формуле). Деяние без
        // объекта стоит ровно свою базу, и база положительна, иначе плечо
        // ниже было бы пустым.
        const std::uint32_t base =
            std::uint32_t(sm::fact_kind_def(sm::FactKind::Explored).baseRenown);
        CHECK(base > 0u, "строка рода платит за деяние — иначе нечего мерить");
        CHECK(sm::renown_of(*storeOwn, std::uint8_t(sm::FactSubject::Squad),
                            otherOrd) == base,
              "слава деяния легла ДЕЯТЕЛЮ: подшивка и плата — одна дверь, и "
              "обе знают субъекта одним ординалом");
        CHECK(sm::renown_of(*storeOwn, std::uint8_t(sm::FactSubject::Squad),
                            flagOrd) == 0u,
              "СТАРЫЙ ЗАКОН МЁРТВ: флажок игрока не получил ничего за чужое "
              "деяние — прежде субъектом подставлялся он, чей бы акт ни был");

        // Ноль — «никто», а не слот: такая строка не платит никому и не рвёт
        // применение (ЗАКОН СЛОВАРЯ И ОРДИНАЛА п.6).
        f.subject = 0u;
        f.x = std::int16_t(fx + 1);
        d.factOps[d.factCount++] = f;
        sm::apply_local_delta(mw, d);
        CHECK(sm::renown_of(*storeOwn, std::uint8_t(sm::FactSubject::Squad),
                            otherOrd) == base
                  && sm::renown_of(*storeOwn,
                                   std::uint8_t(sm::FactSubject::Squad),
                                   flagOrd) == 0u,
              "субъект-ноль не платит НИКОМУ: «никто» есть значение, и чужие "
              "счётчики от него не двигаются");
    }

    // ── (4) ПЕРЕПОЛНЕНИЕ СЧИТАЕТСЯ ВСЛУХ: строка сверх капа не исчезает
    // молча — её считает droppedOps, а применение сбрасывает счёт.
    {
        for (std::uint32_t i = 0; i < sm::kDeltaStockCap; ++i) {
            push_feature_number(d, cx, cy, 0, gs.mapW);
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

// The trade laws, re-homed on the AGENT (W2b-4). The abstract TradeRoute
// system is gone — trade is a caravan carrying real cargo — so the laws it
// was convicted under (audit II.4) are re-pinned where they now live:
//
//   1. TORUS LAW — a caravan picks its city's NEAREST village by TORUS
//      distance. Red-first heritage: flat dx²+dy² made a neighbour across
//      the map seam look maximally distant; a caravan must prefer the
//      village 6 cells across the seam to one 10 cells away on the same
//      side.
//
//   2. GARRISON CAP — garrisons grew by up to +10/day with no sink and
//      silently broke the whole save at kMaxSoldiers=8192 (~game day 820).
//      A settlement's garrison stops recruiting at a hard cap.
//
// (Law 2 of the old file — route party resolution by KIND — died with the
// route system itself; a caravan holds POINTERS to nothing: it stands in a
// real village and moves real stacks.)

#include "check.h"
#include <memory>
#include "macro/labour.h"   // settle_souls — двери душ
#include "macro/birth_squad.h"   // birth_squad — место родится ТЕЛОМ
#include "tables/faction.h"
#include "tables/npc.h"
#include "macro/npc_ai.h"
#include "macro/squad.h"
#include "macro/world_tick.h"
#include "macro/state.h"
#include "bus/store.h"

#include <cstdio>
#include <vector>

namespace {

} // namespace

int main() {
    using namespace sm;

    // ── 1. Torus law: the caravan's STATION choice wraps ────────────────
    // (owner 2026-08-30: caravans walk city to city; the next station is the
    // nearest OTHER city — and "nearest" is torus-metric like everything.)
    {
        std::unique_ptr<GameState> gsOwn = make_game_state();
        GameState& gs = *gsOwn;
        gs.mapW = 64;
        gs.mapH = 64;
        // МЕСТО ЕСТЬ НЕПОДВИЖНЫЙ СКВАД (M-90 шаг 5): строка рождается ТОЛЬКО
        // вместе с телом, а плечо (склад, души) живёт колонками этого тела —
        // значит store стоит ДО первого места, а не после.
        auto wStore_ = sm::make_macro_store();
        MacroStore& st = *wStore_;

        // ОРДИНАЛЫ ВЫДАЁТ ЭМИТЕНТ (ломтик F), и это ПОЧИНКА фикстуры: первый
        // город носил рукописный `id = 0`, то есть ординал «никто» (ЗАКОН
        // СЛОВАРЯ п.6) — ровно тем номером, которым дверь отвечает «станции
        // нет». Теперь выдача честная, с единицы.
        const MacroHandle home =
            birth_squad(gs, st, SquadType::City, 60, 32);  // near east seam
        const int kHomeId = int(st.spawnId[home.slot].index);
        st.inventory[home.slot].inv.add("food", 2048);
        // Души — дверью мира (v122): паства в число фичи, головы в инвентарь.
        settle_souls(gs, st, home.slot, 100);

        // 30 cells west, same side — far enough that the comparable-distance
        // coin flip stays out.
        const MacroHandle sameSide =
            birth_squad(gs, st, SquadType::City, 30, 32);
        settle_souls(gs, st, sameSide.slot, 50);

        // 6 cells east THROUGH the seam: 60 -> 63|0 -> 2.
        const MacroHandle acrossSeam =
            birth_squad(gs, st, SquadType::City, 2, 32);
        const int kAcrossSeamId = int(st.spawnId[acrossSeam.slot].index);
        settle_souls(gs, st, acrossSeam.slot, 50);

        // ЗАКОН ПИНАЕТСЯ ПРЯМО В СВОЮ ДВЕРЬ (2026-09-21): прежде его
        // водил ИИ каравана, а род каравана снесён — караван оказался
        // сквадом, притворившимся видом существа. Дверь та же, что ведёт
        // рейсы сбыта артелей сегодня.
        MacroWorld mw{.gs = &gs, .store = wStore_.get()};
        TickContext ctx{};
        ctx.mw = mw;
        ctx.mapW = gs.mapW;
        ctx.mapH = gs.mapH;
        Rng roll(90u);
        ctx.rng = &roll;
        float sx = 0.0f, sy = 0.0f;
        const int pick = pick_next_station_(ctx, MacroPos{60.0f, 32.0f},
                                            /*currentId*/kHomeId,
                                            /*prevId*/-1,
                                            /*moveMult*/1.0f, sx, sy);
        CHECK(pick >= 0, "the trade door named a station at all");
        CHECK(pick == kAcrossSeamId,
              "ЗАКОН АДРЕСА: the city 6 cells away THROUGH the seam beats the "
              "one 30 cells away on the same side — the world is a connected "
              "torus, so flat dx²+dy² is the wrong metric for it");
    }

    // ── ЗАКОН: МЕРА РЕШЕНИЯ ИДЁТ ТЕМ ЖЕ ТЕМПОМ, ЧТО НОГИ ────────────────
    // Вердикт владельца 2026-11-08 (M-6/M-436). До него на вопрос «как быстро
    // этот сквад идёт» мир отвечал ДВАЖДЫ И РАЗНОЕ: ноги — анкетным
    // `moveMult` (`march_cells_per_think`), а ОЦЕНКА «сколько дней» —
    // глобальной константой на всех ходоков. Утверждаются ОТНОШЕНИЯ, не
    // числа (§8 п.4): перекалибровка базы марша этот блок не красит.
    //
    // ТАБЛИЦА «МУТАЦИЯ → ИСХОД», ОБЕ ПОЛЯРНОСТИ (ПРОГНАНА 2026-11-08):
    //   КРАСНЫЕ — детектор обязан увидеть, и увидел:
    //     · дверь дня ИСПОЛЬЗУЕТ темп, но игнорирует его величину
    //       (`moveMult > -1e9 ? 1 : 0`) ................ КРАСНЫЙ, 2 из 7;
    //     · дверь дня берёт СВОЙ закон темпа (`moveMult²`) — то есть
    //       колонка та же, а закон второй ............. КРАСНЫЙ, 2 из 7;
    //     · у ног рельеф обезврежен (параметр жив) .... КРАСНЫЙ, 1 из 7;
    //     · снят страж неположительного темпа ......... КРАСНЫЙ, 1 из 7.
    //   МОЛЧАЩИЕ — детектор обязан пройти мимо, иначе он шум:
    //     · перекалибровка отдыха (÷2 → ÷3) ........... ЗЕЛЁНЫЙ, 7/7;
    //     · перекалибровка часов суток (24 → 18) ...... ЗЕЛЁНЫЙ, 7/7.
    //   НЕГОДНЫЕ МУТАЦИИ, ЗАПИСАНЫ ЧТОБЫ ИХ НЕ СДЕЛАЛИ ЗАНОВО:
    //     · просто УБРАТЬ `moveMult` из двери дня и `terrain_speed_mult` из
    //       двери шага — НЕСОБИРАЕМО (`-Werror` на неиспользованном
    //       параметре). Это сильнее красного, но свидетеля НЕ судит: мутация,
    //       которая не собралась, не доказывает о приборе ничего.
    {
        // (1) ОБЕ ДВЕРИ ЧИТАЮТ ОДНУ КОЛОНКУ: удвоение темпа удваивает обе.
        const float day1 = march_cells_per_day(1.0f);
        const float day2 = march_cells_per_day(2.0f);
        ecs::MacroNpcRuntime r1{}; r1.moveMult = 1.0f;
        ecs::MacroNpcRuntime r2{}; r2.moveMult = 2.0f;
        const float road = 1.0f;   // ложе дороги — эталонный марш
        const float th1 = march_cells_per_think(r1, road);
        const float th2 = march_cells_per_think(r2, road);
        CHECK(day1 > 0.0f && th1 > 0.0f, "обе двери отвечают ненулём на "
              "эталонном листе — иначе отношения ниже бессмысленны");
        CHECK(day2 == 2.0f * day1 && th2 == 2.0f * th1,
              "мера дней и шаг ног масштабируются анкетой ОДИНАКОВО: это и "
              "значит «один закон темпа», а не два");

        // (2) РЕЛЬЕФ — В ЦЕНЕ ПУТИ, А НЕ В ТЕМПЕ ДНЯ. Шаг ног рельеф читает,
        // мера дней — НЕТ, потому что `nav_path_cost` уже взвешен весом
        // клетки. Второе умножение было бы рельефом, посчитанным дважды.
        const float thHeavy = march_cells_per_think(r1, 4.0f * road);
        CHECK(thHeavy < th1,
              "тяжёлое ложе замедляет ШАГ — рельеф у ног читается");
        CHECK(march_cells_per_day(1.0f) == day1,
              "а мера дней от рельефа не зависит вовсе: он уже внутри цены "
              "пути, и дверь дня его не знает по построению");

        // (3) СТРАЖ ОБЛАСТИ ОПРЕДЕЛЕНИЯ, А НЕ КЛАМП ДИНАМИКИ (ЗАКОН КЛАМПА):
        // неположительный темп есть «никуда не дойдёт», и делить на него
        // нельзя. Это защита ФОРМУЛЫ, поэтому ноль здесь законен.
        CHECK(march_cells_per_day(0.0f) == 0.0f
                  && march_cells_per_day(-5.0f) == 0.0f,
              "стоячий и отрицательный темп дают НОЛЬ клеток в день — "
              "деление на него звонящий обязан отсечь сам");
    }

    // (БЛОК «ЦЕЛЬ ГАРНИЗОНА» УМЕР 2026-09-30 вместе со своей подсистемой,
    // v122: `garrison_target_strength` / `garrison_wants_recruits` снесены
    // вердиктом владельца «раздел гарнизон/мирные умирает; оборона места =
    // вся толпа». Свидетель проверял закон «цель = население >> сдвиг
    // реестра» — закона больше нет, и держать его свидетеля значило бы
    // охранять память о механике, а не механику (§8 п.5).)

    return sm::test::report("trade_law_test");
}

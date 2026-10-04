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
#include "macro/labour.h"   // settle_souls — двери душ
#include "macro/place_birth.h"   // birth_landmark — место рождается с ТЕЛОМ
#include "macro/place_body.h"    // place_store — склад места колонкой тела
#include "macro/agent_memory.h"
#include "tables/faction.h"
#include "tables/npc.h"
#include "macro/npc_ai.h"
#include "macro/squad.h"
#include "macro/world_tick.h"
#include "macro/state.h"
#include "macro/store.h"

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
        GameState gs{};
        gs.mapW = 64;
        gs.mapH = 64;
        // МЕСТО ЕСТЬ НЕПОДВИЖНЫЙ СКВАД (M-90 шаг 5): строка рождается ТОЛЬКО
        // вместе с телом, а плечо (склад, души) живёт колонками этого тела —
        // значит store стоит ДО первого места, а не после.
        ecs::World w;
        auto wStore_ = sm::make_macro_store();
        MacroStore& st = *wStore_;
        sm::store_attach(w, wStore_.get());

        constexpr int kAcrossSeamId = 2;   // ответ, который ждёт закон ниже

        {
            Landmark city{};
            city.type = LandmarkType::City;
            city.id = 0;
            city.x = 60;                     // near the east seam
            city.y = 32;
            Landmark& row = birth_landmark(gs, st, std::move(city));
            place_store(st, row).add("food", 2048);
            // Души — дверью мира (v122): паства в worked, головы в инвентарь.
            settle_souls(gs, st, row, 100);
        }

        {
            Landmark sameSide{};             // 30 cells west, same side — far
            sameSide.type = LandmarkType::City;   // enough that the comparable-
            sameSide.id = 1;                      // distance coin flip stays out
            sameSide.x = 30;
            sameSide.y = 32;
            Landmark& row = birth_landmark(gs, st, std::move(sameSide));
            settle_souls(gs, st, row, 50);
        }

        {
            Landmark acrossSeam{};           // 6 cells east THROUGH the seam
            acrossSeam.type = LandmarkType::City;
            acrossSeam.id = kAcrossSeamId;
            acrossSeam.x = 2;            // 60 -> 63|0 -> 2 = 6 cells by torus
            acrossSeam.y = 32;
            Landmark& row = birth_landmark(gs, st, std::move(acrossSeam));
            settle_souls(gs, st, row, 50);
        }

        // ЗАКОН ПИНАЕТСЯ ПРЯМО В СВОЮ ДВЕРЬ (2026-09-21): прежде его
        // водил ИИ каравана, а род каравана снесён — караван оказался
        // сквадом, притворившимся видом существа. Дверь та же, что ведёт
        // рейсы сбыта артелей сегодня.
        MacroWorld mw{.gs = &gs, .world = &w};
        TickContext ctx{};
        ctx.mw = mw;
        ctx.mapW = gs.mapW;
        ctx.mapH = gs.mapH;
        Rng roll(90u);
        ctx.rng = &roll;
        float sx = 0.0f, sy = 0.0f;
        const int pick = pick_next_station_(ctx, MacroPos{60.0f, 32.0f},
                                            /*currentId*/0, /*prevId*/-1,
                                            sx, sy);
        CHECK(pick >= 0, "the trade door named a station at all");
        CHECK(pick == kAcrossSeamId,
              "ЗАКОН АДРЕСА: the city 6 cells away THROUGH the seam beats the "
              "one 30 cells away on the same side — the world is a connected "
              "torus, so flat dx²+dy² is the wrong metric for it");
    }

    // (БЛОК «ЦЕЛЬ ГАРНИЗОНА» УМЕР 2026-09-30 вместе со своей подсистемой,
    // v122: `garrison_target_strength` / `garrison_wants_recruits` снесены
    // вердиктом владельца «раздел гарнизон/мирные умирает; оборона места =
    // вся толпа». Свидетель проверял закон «цель = население >> сдвиг
    // реестра» — закона больше нет, и держать его свидетеля значило бы
    // охранять память о механике, а не механику (§8 п.5).)

    return sm::test::report("trade_law_test");
}

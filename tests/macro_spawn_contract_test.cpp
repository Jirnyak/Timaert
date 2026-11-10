// КОНТРАКТ МАКРО-СПАВНА — `spawn_npc_at@src/macro/npc_spawn.h`.
//
// ПРИБОР ПЕРЕИМЕНОВАН 2026-11-08 (M-436), И ЭТО НЕ КОСМЕТИКА. Он звался
// `spawn_entity_event_test`, а шапка начиналась словами «Locks the SpawnEntity
// consumer half» — то есть прибор был назван по СОБЫТИЮ, которого в игре нет с
// M-410: во всём дереве `SpawnEntity` остался тремя некрологами (тег без
// читателя, мёртвый обработчик, мёртвый счётчик), а с 2026-11-08 шины событий
// не существует вовсе (ФРЕЙМ, род 5). Имя прибора — тоже свидетель, и врущее
// имя посылает следующего агента искать систему, которой нет; в этом же файле
// такая посылка уже стоила вопроса владельцу (ЗАКОН ИМЕНИ).
//
// ЧТО ОН СУДИЛ ВСЁ ЭТО ВРЕМЯ — ровно контракт двери, и ни одного события:
//   * a known token spawns exactly one macro NPC of that registry type;
//   * an Aggressive type joins the "bandits" faction (boot-spawner parity);
//   * level > 0 pins NpcLevel;
//   * the body lands within the spawn scatter of the named cell (torus);
//   * the MacroSpawnId ordinal continues past the existing maximum
//     (possession identity stays unique);
//   * an unknown token spawns NOTHING and returns false (no silent Bandit
//     fallback here — that is the subworld console's own historical rule).
//
// И ОДНА ИЗ ЭТИХ ПРОВЕРОК ЕСТЬ ПРЕДУСЛОВИЕ ЧУЖОГО СВИДЕТЕЛЯ: той же дверью
// `squad_creatures_test` рождает себе бандита, прежде чем спрашивать про его
// ростер (§8 п.11 — свидетель обязан сам рождать своё предусловие). Поэтому
// у двери ноль прод-звонящих и она НЕ сирота.

#include "check.h"
#include <memory>
#include "macro/npc_spawn.h"
#include "tables/npc.h"
#include "tables/faction.h"
#include "macro/state.h"
#include "core/torus.h"
#include "ecs/components.h"
#include "bus/store.h"
#include "macro/squad.h"   // squad_level — дверь уровня над анкетой (M-246а)

#include <cstdio>

namespace {

int count_npcs(const sm::MacroStore& st) {
    // 6.3: население — живые слоты store, моста нет.
    return int(st.aliveCount);
}

// The whole scenario lives in a VOID function so CHECK_OR_RETURN can bail
// out of a broken fixture without inventing a verdict: `main` only reports.
void run_spawn_contract() {
    std::unique_ptr<sm::GameState> gsOwn = sm::make_game_state();
    sm::GameState& gs = *gsOwn;
    gs.mapW = 64;
    gs.mapH = 64;
    gs.worldSeed = 777u;
    auto wStore_ = sm::make_macro_store();
    sm::TerrainData terrain{};  // no RGBA storage => everywhere is land

    // NEGATIVE CONTROL FIRST, and asserted in two halves: a token the
    // catalogue does not know must be REFUSED, and — the half that is easy to
    // forget — must leave nothing behind. A spawner that half-built a body
    // and then said no would pass the first check alone.
    CHECK(!sm::spawn_npc_at(gs, *wStore_, terrain, "grue", 10, 12, 3),
          "a token the catalogue does not know is REFUSED");
    CHECK(count_npcs(*wStore_) == 0, "...and left no half-built entity behind");

    // Known token, case-insensitive, level pinned.
    CHECK_OR_RETURN(
        sm::spawn_npc_at(gs, *wStore_, terrain, "Bandit", 10, 12, 3),
        "a known token spawns — and the lookup ignores case");
    CHECK(count_npcs(*wStore_) == 1, "exactly one body was raised, not two");

    std::uint32_t firstOrdinal = 0;
    sm::MacroStore& st = *wStore_;
    for (std::size_t s32 = 0; s32 < sm::kUnifiedCap; ++s32) {
        const std::uint16_t slot = std::uint16_t(s32);
        if (st.alive[slot] == 0) continue;
        const auto& kind = st.kind[slot];
        CHECK(kind.type == std::uint16_t(sm::NPCType::Bandit),
              "the body wears the ROW the token named");
        CHECK(kind.factionIdx == std::uint16_t(sm::faction_index("bandits")),
              "and the faction comes from the ONE registry, resolved by name "
              "at the border and carried as an ordinal");
        CHECK(sm::squad_level(st, slot) == 3,
              "the level asked for is the level pinned — в АНКЕТЕ (M-246а)");
        const auto& pc = st.cell[slot];
        // find_valid_spawn scatters within +-6 cells of the wrapped target.
        const float d2 = sm::torus_dist_sq(
            float(sm::ecs::cell_x(pc, gs.mapW)),
            float(sm::ecs::cell_y(pc, gs.mapW)), 10.0f, 12.0f,
            float(gs.mapW), float(gs.mapH));
        CHECK(d2 <= 2.0f * 6.0f * 6.0f,
              "the body landed within find_valid_spawn's scatter of the cell "
              "asked for — measured by TORUS distance, so the seam is near");
        firstOrdinal = st.spawnId[slot].index;
    }

    // Second spawn: ordinal strictly continues (possession identity unique).
    CHECK_OR_RETURN(
        sm::spawn_npc_at(gs, *wStore_, terrain, "bandit", 40, 40, 2),
        "the same token spawns again in lower case");
    CHECK(count_npcs(*wStore_) == 2, "two bodies stand, not one and not three");
    bool sawSecond = false;
    int wentBackwards = 0;
    for (std::size_t s32 = 0; s32 < sm::kUnifiedCap; ++s32) {
        const std::uint16_t slot = std::uint16_t(s32);
        if (st.alive[slot] == 0) continue;
        const std::uint32_t idx = st.spawnId[slot].index;
        if (idx == firstOrdinal) continue;
        if (idx <= firstOrdinal) ++wentBackwards;
        sawSecond = true;
    }
    CHECK(sawSecond, "the second body carries an ordinal of its own");
    CHECK(wentBackwards == 0,
          "and it CONTINUES the sequence — a spawn ordinal is an identity, so "
          "it may never be reused or walked back");
}

} // namespace

int main() {
    run_spawn_contract();
    return sm::test::report("macro_spawn_contract_test");
}

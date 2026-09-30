// The macro-ECS snapshot (Session 17): the living map survives the save.
//
// Before this, a load cleared the registry and re-spawned lords from the
// seed: a killed squad rose again, a levelled leader forgot his campaigns,
// and the runtime ordinal issuer (max-over-living) could hand a dead man's
// identity to a stranger (problems.md 19.24). What is pinned here:
//   · snapshot -> save -> load -> restore round-trips the ENTITIES — wounds,
//     debt, xp, orders, roster, death — not just their scalars;
//   · the killed lord STAYS dead across the save;
//   · MacroSpawnId ordinals are for LIFE: destroy the highest-ordinal squad,
//     save, load, spawn anew — the dead man's ordinal is never reissued
//     (with the old max-over-living scan that exact sequence reissued it).
#include "check.h"

#include "macro/macro_snapshot.h"
#include "macro/world_row.h"
#include "macro/npc_spawn.h"
#include "macro/deposit_layer.h"
#include "macro/save.h"
#include "macro/state.h"
#include "tables/faction.h"
#include "tables/npc.h"
#include "events/quests/quest_types.h"
#include "macro/store.h"
#include "macro/squad.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <type_traits>
#include <vector>

namespace {

using namespace sm;

std::string temp_path(const char* name) {
    const char* dir = std::getenv("TEMP");
    if (!dir || dir[0] == '\0') dir = std::getenv("TMP");
    if (!dir || dir[0] == '\0') dir = ".";
    std::string p(dir);
    const char last = p.empty() ? '\0' : p[p.size() - 1u];
    if (last != '/' && last != '\\') p += '/';
    return p + name;
}

MacroHandle find_by_ordinal(ecs::World& w, std::uint32_t ordinal) {
    return macro_handle_by_spawn_id(sm::store_of(w), ordinal);
}

void test_snapshot_round_trips_the_living_map() {
    GameState gs{};
    gs.mapW = 64;
    gs.mapH = 64;
    gs.worldSeed = 777u;
    TerrainData absent{};
    absent.width = 0;
    absent.height = 0;
    ecs::World w;
    auto wStore_ = sm::make_macro_store();
    sm::store_attach(w, wStore_.get());

    // Squad A: a bandit lord with two guards and a route.
    SquadSpec specA{};
    specA.leaderType = NPCType::Bandit;
    specA.leaderLevel = 5;
    specA.x = 20;
    specA.y = 20;
    specA.factionIndex = faction_index("bandits");
    specA.members.push(make_soldier(std::uint8_t(NPCType::Guard), 3, 1001u));
    specA.members.push(make_soldier(std::uint8_t(NPCType::Guard), 4, 1002u));
    specA.waypointCount = 2;
    specA.waypoints[0] = 24; specA.waypoints[1] = 20;
    specA.waypoints[2] = 20; specA.waypoints[3] = 20;
    const MacroHandle a = spawn_squad(gs, sm::store_of(w), absent, specA);
    CHECK_OR_RETURN(wStore_->valid(a), "squad A spawned");

    // Squad B: a lone peasant — a squad of one, its own leader.
    SquadSpec specB{};
    specB.leaderType = NPCType::Peasant;
    specB.leaderLevel = 2;
    specB.x = 40;
    specB.y = 40;
    specB.factionIndex = faction_index("timaert");
    const MacroHandle b = spawn_squad(gs, sm::store_of(w), absent, specB);
    CHECK_OR_RETURN(wStore_->valid(b), "squad B spawned");

    const std::uint32_t ordinalA = wStore_->spawnId[a.slot].index;
    const std::uint32_t ordinalB = wStore_->spawnId[b.slot].index;

    // Life happened: A campaigned (xp, debt, a march), B was killed through
    // the tracked-death shape (hp=0 + Dead) the whole game uses.
    auto& rtA = wStore_->runtime[a.slot];
    rtA.xp = 777;
    wStore_->pools[a.slot].sp = -15;
    wStore_->cell[a.slot].idx = ecs::cell_index(25, 21, 64);
    wStore_->pools[b.slot].hp = 0.0f;
    sm::macro_mark_dead(*wStore_, b);
    // …and the player POSSESSES lord A (5б): истина — колонка playerFlag
    // анкеты, биты GameState — кэш, флажок двигает дверь переноса; загрузка
    // обязана вернуть флажок на ТОГО ЖЕ лорда (SAVE-5: второй склад «кем
    // управляю» вне снимка мёртв).
    sm::transfer_player_flag(sm::store_of(w), gs.playerFlagBits, a);
    // Имя анкеты — колонка (вердикт 3, ход 2): рождение кладёт непустой
    // дефолт генерации, МУТАЦИЯ колонки обязана пережить сейв (в этом весь
    // смысл колонки против перевывода из nameIdx).
    CHECK(sm::store_of(w).name[a.slot].text[0] != '\0',
          "рождённый сквад несёт непустое имя-дефолт");
    std::snprintf(sm::store_of(w).name[a.slot].text,
                  sizeof(sm::store_of(w).name[a.slot].text), "%s",
                  "Chornyy Voron");
    // A bandit chief is a NAMED character (v90): born OWNING his sheet.
    // His campaign diverges it from the birth roll — the owner's ММОРПГ
    // point is that exactly this divergence survives the save.
    CHECK_OR_RETURN(owned_sheet(*wStore_, a) != nullptr,
                    "a named kind is born owning his sheet");
    owned_sheet(*wStore_, a)->attributes[AttributeId::End] = 13;
    // A peasant crew is TRANSIENT: no component, nothing stored — its
    // generic sheet derives from its row (the negative control).
    CHECK(owned_sheet(*wStore_, b) == nullptr,
          "a transient crew owns no sheet");

    // Snapshot -> save -> load -> restore, through the REAL save file.
    const std::string path = temp_path("timaert_macro_snapshot_test.bin");
    std::remove(path.c_str());
    const std::vector<Quest> noQuests;
    const std::vector<std::uint16_t> noTrees;
    const DepositLayer noDeposits;
    CHECK_OR_RETURN(save_game(gs, noQuests, snapshot_macro_ecs(*wStore_), noTrees,
                              noDeposits, path),
                    "the snapshot saved");

    GameState gs2{};
    std::vector<Quest> quests2;
    std::vector<MacroNpcRecord> records2;
    std::vector<std::uint16_t> trees2;
    DepositLayer deposits2;
    CHECK_OR_RETURN(load_game(gs2, quests2, records2, trees2, deposits2,
                              path),
                    "the snapshot loaded");
    CHECK(gs2.nextMacroSpawnOrdinal == gs.nextMacroSpawnOrdinal,
          "the identity issuer survives the save");

    ecs::World w2;

    auto w2Store_ = sm::make_macro_store();

    sm::store_attach(w2, w2Store_.get());
    restore_macro_ecs(records2, *w2Store_, gs2);
    resolve_player_handles_after_load(gs2, *w2Store_);

    const MacroHandle a2 = find_by_ordinal(w2, ordinalA);
    CHECK_OR_RETURN(w2Store_->valid(a2), "leader A restored under his ordinal");
    CHECK(w2Store_->runtime[a2.slot].xp == 777,
          "the leader's campaigns (xp) survive the save");
    CHECK(w2Store_->pools[a2.slot].sp == -15,
          "the leader's exhaustion debt survives the save");
    CHECK(ecs::cell_x(w2Store_->cell[a2.slot], 64) == 25
              && ecs::cell_y(w2Store_->cell[a2.slot], 64) == 21,
          "the march stands - the cell is the saved one, not the spawn one");
    CHECK(w2Store_->level[a2.slot].value == 5, "the level survives");
    CHECK(std::strcmp(w2Store_->name[a2.slot].text, "Chornyy Voron") == 0,
          "мутированное имя анкеты пережило сейв колонкой (v119)");
    CHECK(creature_heads(w2Store_->inventory[a2.slot].inv) == 2,
          "the roster rows survive");
    CHECK(w2Store_->orders[a2.slot].waypointCount == 2,
          "the squad's route survives");
    CHECK(!sm::macro_dead(*w2Store_, a2), "the living leader is not dead");
    // The flag rode the snapshot honestly and landed on the SAME lord — and
    // on nobody else (the negative control: B carried no flag and must not
    // grow one; a restore that stamps everyone would also pass a bare
    // "A has it" check).
    CHECK(player_flag_handle(gs2) == a2,
          "the possessed lord keeps the player flag across the save");
    CHECK_OR_RETURN(owned_sheet(*w2Store_, a2) != nullptr,
                    "the named chief still OWNS his sheet after the save");
    CHECK(w2Store_->sheet[a2.slot].attributes.of(AttributeId::End) == 13,
          "…and his campaign's divergence from the birth roll survived");
    const MacroHandle b2pre = find_by_ordinal(w2, ordinalB);
    CHECK(w2Store_->valid(b2pre)
              && owned_sheet(*w2Store_, b2pre) == nullptr,
          "the transient crew still stores nothing (owned_sheet law)");
    CHECK(w2Store_->valid(player_flag_handle(gs2)),
          "exactly one player flag restored — the handle is live");

    const MacroHandle b2 = find_by_ordinal(w2, ordinalB);
    CHECK_OR_RETURN(w2Store_->valid(b2), "the dead leader is still ON the map");
    CHECK(sm::macro_dead(*w2Store_, b2)
              && w2Store_->pools[b2.slot].hp == 0.0f,
          "the KILLED lord stays dead across the save");

    // ── Ordinals are for life (19.24). Remove the HIGHEST-ordinal squad
    // entirely — under the old max-over-living issuer the next spawn re-took
    // exactly that ordinal; the persistent counter must never.
    const std::uint32_t highest = ordinalA > ordinalB ? ordinalA : ordinalB;
    const MacroHandle doomed = find_by_ordinal(w2, highest);
    CHECK_OR_RETURN(w2Store_->valid(doomed), "the highest-ordinal squad exists");
    sm::store_death(*w2Store_, doomed);

    SquadSpec specC{};
    specC.leaderType = NPCType::Bandit;
    specC.leaderLevel = 1;
    specC.x = 10;
    specC.y = 10;
    specC.factionIndex = faction_index("bandits");
    const MacroHandle c = spawn_squad(gs2, sm::store_of(w2), absent, specC);
    CHECK_OR_RETURN(w2Store_->valid(c), "a new squad spawned after the load");
    CHECK(w2Store_->spawnId[c.slot].index > highest,
          "a dead man's ordinal is NEVER reissued - identity is for life");

    std::remove(path.c_str());
}

// ── 1д (сессия 19): раундтрип-свидетель STORE, весь формат по построению ──
// Тест выше проверяет поля ПОИМЁННО — и потому слеп к колонке, выпавшей из
// restore, или попавшей в снапшот мимо него (грабля
// handwritten-foldup-drops-fields). Этот свидетель закрывает формат целиком:
// снапшот → restore в СВЕЖИЙ store → снапшот обязан дать тот же байтовый
// поток — записи зеро-инициализированы, сортировка по ординалу делает одно
// состояние мира одним потоком байт (та же пара условий, на которых стоит
// save_payload_fingerprint).
void test_resnapshot_is_byte_identical() {
    static_assert(std::is_trivially_copyable_v<MacroNpcRecord>,
                  "запись снапшота обязана сниматься байтами (DOD)");
    GameState gs{};
    gs.mapW = 64;
    gs.mapH = 64;
    gs.worldSeed = 777u;
    TerrainData absent{};
    ecs::World w;
    auto wStore_ = sm::make_macro_store();
    sm::store_attach(w, wStore_.get());

    // Та же пара судеб, что в тесте выше: походивший именной лорд с
    // приказами и владеемым листом + убитый транзиент — обе ветки формата
    // (hasOrders, дивергенция листа, байт судьбы) живые.
    SquadSpec specA{};
    specA.leaderType = NPCType::Bandit;
    specA.leaderLevel = 5;
    specA.x = 20;
    specA.y = 20;
    specA.factionIndex = faction_index("bandits");
    specA.members.push(make_soldier(std::uint8_t(NPCType::Guard), 3, 1001u));
    specA.waypointCount = 1;
    specA.waypoints[0] = 24; specA.waypoints[1] = 20;
    const MacroHandle a = spawn_squad(gs, sm::store_of(w), absent, specA);
    CHECK_OR_RETURN(wStore_->valid(a), "squad A spawned");
    wStore_->runtime[a.slot].xp = 777;
    CHECK_OR_RETURN(owned_sheet(*wStore_, a) != nullptr,
                    "named lord owns his sheet");
    owned_sheet(*wStore_, a)->attributes[AttributeId::End] = 13;

    SquadSpec specB{};
    specB.leaderType = NPCType::Peasant;
    specB.leaderLevel = 2;
    specB.x = 40;
    specB.y = 40;
    specB.factionIndex = faction_index("timaert");
    const MacroHandle b = spawn_squad(gs, sm::store_of(w), absent, specB);
    CHECK_OR_RETURN(wStore_->valid(b), "squad B spawned");
    wStore_->pools[b.slot].hp = 0.0f;
    sm::macro_mark_dead(*wStore_, b);

    const std::vector<MacroNpcRecord> snap1 = snapshot_macro_ecs(*wStore_);
    CHECK_OR_RETURN(snap1.size() == 2, "the snapshot names both squads");

    ecs::World w2;
    auto w2Store_ = sm::make_macro_store();
    sm::store_attach(w2, w2Store_.get());
    restore_macro_ecs(snap1, *w2Store_, gs);
    std::vector<MacroNpcRecord> snap2 = snapshot_macro_ecs(*w2Store_);
    CHECK_OR_RETURN(snap2.size() == snap1.size(),
                    "the re-snapshot names the same count");

    int samples = 0, mismatches = 0;
    for (std::size_t i = 0; i < snap1.size(); ++i) {
        ++samples;
        if (std::memcmp(&snap1[i], &snap2[i], sizeof(MacroNpcRecord)) != 0)
            ++mismatches;
    }
    CHECK(samples > 0 && mismatches == 0,
          "snapshot -> restore -> snapshot is the SAME byte stream");

    // Негативный контроль, и он утверждается сам: детектор обязан ВИДЕТЬ
    // дельту — иначе зелёный memcmp выше не доказывает ничего.
    const MacroHandle a2 = find_by_ordinal(
        w2, wStore_->spawnId[a.slot].index);
    CHECK_OR_RETURN(w2Store_->valid(a2), "lord A restored for the control");
    w2Store_->runtime[a2.slot].xp += 1;
    const std::vector<MacroNpcRecord> snap3 = snapshot_macro_ecs(*w2Store_);
    int controlDiffs = 0;
    for (std::size_t i = 0; i < snap3.size(); ++i) {
        if (std::memcmp(&snap2[i], &snap3[i], sizeof(MacroNpcRecord)) != 0)
            ++controlDiffs;
    }
    CHECK(controlDiffs == 1,
          "the byte witness SEES a one-column mutation (negative control)");
}

} // namespace

int main() {
    test_snapshot_round_trips_the_living_map();
    test_resnapshot_is_byte_identical();
    return sm::test::report("macro_snapshot_test");
}

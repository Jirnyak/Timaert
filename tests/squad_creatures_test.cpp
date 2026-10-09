// THE macro entity is a squad (Session 15, Inc 1). This test pins the doctrine
// structurally: every macro NPC born through the one creation path carries an
// ecs::SquadUpkeep, and it is born EMPTY — a lone wanderer is a squad of one
// whose leader is the entity itself, not a special kind of thing. Both spawn
// doors are checked (the boot spawner and the runtime console/event spawner),
// because a doctrine that holds at one door and not the other is two dialects.
// The reverse direction guards against orphan containers: a SquadUpkeep on a
// non-macro entity would be a second squad representation growing beside the
// real one.
#include "check.h"

#include "ecs/components.h"
#include "macro/npc_spawn.h"
#include "macro/place_birth.h"   // место рождается СО СВОИМ ТЕЛОМ (M-90 шаг 5)
#include "macro/world_row.h"
#include "macro/store.h"
#include <cstdio>

namespace {

// Свидетель рождает своё предусловие САМ (AGENTS §8 п.11): место приходит в
// мир одной дверью — ТЕЛОМ в store (ломтик F: строки места больше нет), — и
// души кладутся головами в инвентарь того же тела.
sm::MacroHandle make_settlement(sm::GameState& gs, sm::MacroStore& st,
                                int x, int y) {
    const sm::MacroHandle h = sm::birth_place(gs, st, sm::SquadType::City,
                                              x, y, 0, "Test Settlement");
    // Души — ГОЛОВАМИ в инвентарь тела (v122): фабрика мира не
    // видит, поэтому пасту (worked-число фичи) ставит звонящий,
    // если она ему нужна; домашние души живут в теле места.
    sm::raise_flock_into_container(st.inventory[h.slot].inv, 1000);
    return h;
}

// Terrain the spawner treats as absent — spawn positions fall back safely.
sm::TerrainData absent_terrain() {
    sm::TerrainData t;
    t.width = 0;
    t.height = 0;
    return t;
}

void test_every_macro_npc_is_a_squad_of_one() {
    sm::GameState gs{};
    gs.mapW = 16;
    gs.mapH = 16;

    const sm::TerrainData terrain = absent_terrain();
    sm::ecs::World world;
    auto worldStore_ = sm::make_macro_store();
    sm::store_attach(world, worldStore_.get());
    make_settlement(gs, sm::store_of(world), 8, 8);

    // ТЕЛА МЕСТ СТОЯТ В ТОМ ЖЕ STORE (M-90 шаг 5), и закон этого свидетеля —
    // про СПАВН-ДВЕРЬ, а не про всякое тело мира: город законно носит в
    // инвентаре свою тысячу голов. Поэтому отсечка берётся ДО спавна, и
    // спрашиваются ровно те слоты, которые дверь родила.
    std::uint8_t before[sm::kUnifiedCap];
    {
        const sm::MacroStore& st = sm::store_of(world);
        for (std::size_t s32 = 0; s32 < sm::kUnifiedCap; ++s32)
            before[s32] = st.alive[s32];
    }
    const int placeBodies = int(sm::store_of(world).aliveCount);
    sm::spawn_macro_npcs(gs, world, sm::store_of(world), terrain, 123u);

    int macroNpcs = 0;
    {
        const sm::MacroStore& st = sm::store_of(world);
        for (std::size_t s32 = 0; s32 < sm::kUnifiedCap; ++s32) {
            if (st.alive[s32] == 0 || before[s32] != 0) continue;
            ++macroNpcs;
            CHECK(sm::creatures_empty(st.inventory[s32].inv),
                  "a freshly spawned wanderer is a squad of ONE: empty creatures, "
                  "the slot itself is the leader");
        }
    }
    CHECK(macroNpcs > 0, "fixture must spawn macro NPCs to say anything");

    // Runtime door: the console/event spawner goes through the same make_npc.
    CHECK(sm::spawn_npc_at(gs, world, sm::store_of(world), terrain, "bandit", 4, 4, /*level*/ 3),
          "runtime spawn door must accept a registry label");
    int containers = 0;
    containers = int(sm::store_of(world).aliveCount) - placeBodies;
    CHECK(containers == macroNpcs + 1,
          "both spawn doors must attach exactly one creatures per macro NPC");
}

} // namespace

int main() {
    test_every_macro_npc_is_a_squad_of_one();
    return sm::test::report("squad_creatures_test");
}

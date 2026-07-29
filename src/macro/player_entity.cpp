#include "macro/player_entity.h"
#include "ecs/components.h"
#include <array>

namespace sm {

void ensure_macro_player_entity(GameState& gs, ecs::World& world) {
    auto& reg = world.reg;

    // At most one PlayerTag entity exists at any time (the subworld lifecycle and
    // this function jointly maintain that invariant), so the first hit is it.
    entt::entity flag = entt::null;
    for (auto e : reg.view<ecs::PlayerTag>()) { flag = e; break; }

    // Defensive: never touch a live subworld combat flag — that entity's
    // lifecycle belongs to SubworldEngine. The wiring only calls this at boot and
    // on the macro tick, where no SubworldTag PlayerTag exists, so this guard is
    // belt-and-suspenders against a future mis-call.
    if (flag != entt::null && reg.any_of<ecs::SubworldTag>(flag)) return;

    // Fresh boot, or we just returned from a subworld (leave() destroyed the
    // subworld flag). Materialise the macro flag as a bare PlayerTag entity.
    if (flag == entt::null) {
        flag = reg.create();
        reg.emplace<ecs::PlayerTag>(flag);
    }

    // Project the authoritative scalar onto the flag's Position (one-way sync;
    // gs.player stays authoritative). emplace_or_replace also heals a flag that
    // somehow lost its Position. Note: no +0.5 here — Position is the raw cell
    // coordinate; the macro overlay applies the render-time centring offset.
    reg.emplace_or_replace<ecs::Position>(flag, gs.player.x, gs.player.y);
}

bool reattach_player_to_macro_spawn(ecs::World& world, int id, float px, float py) {
    if (id < 0) return false;
    auto& reg = world.reg;

    // Find the regenerated macro NPC that carries this deterministic ordinal.
    // spawn_macro_npcs recreated the whole population from `worldSeed` in the same
    // order, so the ordinal that was possessed at save time names the same NPC.
    entt::entity target = entt::null;
    for (auto e : reg.view<ecs::MacroSpawnId, ecs::MacroNpcRuntime>()) {
        if (int(reg.get<ecs::MacroSpawnId>(e).index) == id) { target = e; break; }
    }
    if (target == entt::null) return false;  // died before save / seed changed

    // Collect prior flag holders first — never mutate a pool while iterating it.
    // Normally there is exactly one: the hero husk ensure_macro_player_entity just
    // created (Position + PlayerTag, no MacroNpcRuntime).
    std::array<entt::entity, 8> prior{};
    int n = 0;
    for (auto e : reg.view<ecs::PlayerTag>()) {
        if (e == target) continue;
        if (n >= int(prior.size())) break;
        prior[std::size_t(n++)] = e;
    }
    for (int i = 0; i < n; ++i) {
        const entt::entity e = prior[std::size_t(i)];
        if (!reg.valid(e)) continue;
        reg.remove<ecs::PlayerTag>(e);
        // Destroy the bare hero husk; never destroy a real macro NPC (strip only,
        // so any other possessed-like body would stay an autonomous NPC).
        if (!reg.all_of<ecs::MacroNpcRuntime>(e)) reg.destroy(e);
    }

    if (!reg.all_of<ecs::PlayerTag>(target)) reg.emplace<ecs::PlayerTag>(target);
    // The loaded scalar is authoritative for WHERE the player is; the ordinal is
    // authoritative for WHO. Snap the adopted body to the saved cell.
    reg.emplace_or_replace<ecs::Position>(target, px, py);
    return true;
}

} // namespace sm

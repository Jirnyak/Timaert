#include "macro/player_entity.h"
#include "ecs/components.h"

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

} // namespace sm

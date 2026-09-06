// The melee reach law (damage-door track Inc 4, owner ruling 2026-08-27):
// reach is ONE number (the attacker's attackRange — a mob's row states it, a
// man's spear will modify it through the equipment increment), and the blow
// lands on the target's SURFACE: in reach ⇔ dist − body_radius(target) ≤
// range. The same law the NPC strike walks (battle.cpp reach + radius),
// finally read by the player's own swing.
//
// Pinned, with negative controls:
//   * a troll is struck from farther away than a frog, because a troll is
//     wider — same range, same distance, different answer, no branches;
//   * hostiles-first survives the radius law: a hostile within reach beats a
//     nearer neutral, the neutral fallback (the legal crime) survives;
//   * the grid arm and the full-scan arm agree on the same scene, and a
//     broad phase that cannot promise completeness (-1) falls back rather
//     than blinding the swing.

#include "check.h"
#include "sub/targeting.h"
#include "sub/body.h"
#include "macro/npc.h"

#include <cstdio>

namespace {

using sm::NPCType;
using sm::sub::melee_pick_target;

entt::entity body(entt::registry& reg, NPCType type, float x, float y) {
    const entt::entity e = reg.create();
    reg.emplace<sm::ecs::Position>(e, x, y, 0.0f);
    reg.emplace<sm::ecs::Health>(e, 30, 30);
    reg.emplace<sm::ecs::NPCKind>(e, std::uint16_t(type), std::uint16_t{0});
    reg.emplace<sm::ecs::SubworldTag>(e);
    return e;
}

bool never_hostile(void*, entt::entity) { return false; }

void test_wide_body_is_struck_from_farther() {
    const float range = 5.0f;
    const float trollR = sm::npc_body_radius(sm::npc_def(NPCType::Troll));
    const float frogR = sm::npc_body_radius(sm::npc_def(NPCType::Frog));
    CHECK(trollR > frogR + 0.5f,
          "the fixture's premise: a troll IS much wider than a frog");
    // Both stand at the same centre distance — inside the troll's surface
    // reach, outside the frog's.
    const float d = range + (trollR + frogR) * 0.5f;

    {
        entt::registry reg;
        const entt::entity troll = body(reg, NPCType::Troll, d, 0.0f);
        CHECK(melee_pick_target(reg, 0, 0, 0, range, &never_hostile, nullptr)
                  == troll,
              "the troll's flank is in reach at a distance its centre is not");
    }
    {
        entt::registry reg;
        body(reg, NPCType::Frog, d, 0.0f);
        CHECK(melee_pick_target(reg, 0, 0, 0, range, &never_hostile, nullptr)
                  == entt::null,
              "the frog at the same distance is out of reach — the width is "
              "the whole difference");
    }
}

void test_hostiles_first_by_surface_gap() {
    entt::registry reg;
    const entt::entity nearFrog = body(reg, NPCType::Frog, 2.0f, 0.0f);
    const entt::entity farTroll = body(reg, NPCType::Troll, 5.5f, 0.0f);
    struct Ctx { entt::entity hostile; } ctx{farTroll};
    const auto oracle = [](void* user, entt::entity e) {
        return e == static_cast<Ctx*>(user)->hostile;
    };
    CHECK(melee_pick_target(reg, 0, 0, 0, 5.0f, oracle, &ctx) == farTroll,
          "a hostile in surface reach beats a nearer neutral");
    CHECK(melee_pick_target(reg, 0, 0, 0, 5.0f, &never_hostile, nullptr)
              == nearFrog,
          "with no hostile in reach the swing falls back to the nearest "
          "neutral — the deliberate crime stays possible");
}

// The grid arm must agree with the full scan it replaces — and a broad phase
// that cannot promise completeness must fall back, not blind the swing.
void test_grid_arm_parity_and_fallback() {
    entt::registry reg;
    const entt::entity troll = body(reg, NPCType::Troll, 5.5f, 0.0f);
    body(reg, NPCType::Frog, 9.0f, 0.0f);   // out of reach either way

    struct Feed { std::uint32_t ids[8]; int n; };
    Feed feed{};
    feed.ids[feed.n++] = std::uint32_t(entt::to_integral(troll));
    const auto superset = [](void* user, float, float, float,
                             std::uint32_t* out, int maxOut) {
        auto* f = static_cast<Feed*>(user);
        const int n = f->n < maxOut ? f->n : maxOut;
        for (int i = 0; i < n; ++i) out[i] = f->ids[i];
        return n;
    };
    const auto refuses = [](void*, float, float, float, std::uint32_t*, int) {
        return -1;
    };

    const entt::entity fullScan =
        melee_pick_target(reg, 0, 0, 0, 5.0f, &never_hostile, nullptr);
    CHECK(fullScan == troll, "the reference arm reaches the troll");
    CHECK(melee_pick_target(reg, 0, 0, 0, 5.0f, &never_hostile, nullptr,
                            superset, &feed)
              == fullScan,
          "the grid arm answers exactly what the full scan answers");
    CHECK(melee_pick_target(reg, 0, 0, 0, 5.0f, &never_hostile, nullptr,
                            refuses, nullptr)
              == fullScan,
          "a broad phase that answers -1 falls back to the full scan");
}

void test_player_side_and_dead_are_invisible() {
    entt::registry reg;
    const entt::entity soldier = body(reg, NPCType::Guard, 2.0f, 0.0f);
    reg.emplace<sm::ecs::PlayerSoldierTag>(soldier);
    const entt::entity corpse = body(reg, NPCType::Frog, 2.5f, 0.0f);
    reg.emplace<sm::ecs::Dead>(corpse);
    CHECK(melee_pick_target(reg, 0, 0, 0, 5.0f, &never_hostile, nullptr)
              == entt::null,
          "the swing sees neither the player's own soldier nor a corpse");
}

} // namespace

int main() {
    test_wide_body_is_struck_from_farther();
    test_hostiles_first_by_surface_gap();
    test_grid_arm_parity_and_fallback();
    test_player_side_and_dead_are_invisible();
    return sm::test::report("melee_reach_test");
}

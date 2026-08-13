#include "macro/faction.h"
#include "macro/npc.h"
#include "ecs/components.h"
#include "ecs/world.h"
#include "sub/ai.h"
#include "sub/spawn.h"

#include <cstdio>
#include <vector>

namespace {

int fail(const char* msg) {
    std::fprintf(stderr, "FAIL combat_squad_test: %s\n", msg);
    return 1;
}

struct FixedRng {
    float values[8] = {0.10f, 0.72f, 0.92f, 0.40f,
                       0.20f, 0.82f, 0.55f, 0.05f};
    int idx = 0;
    float operator()() {
        const float v = values[idx % 8];
        ++idx;
        return v;
    }
};

} // namespace

int main() {
    const sm::SoldierRecord lowLevel = sm::make_soldier(
        static_cast<std::uint8_t>(sm::NPCType::Peasant), -400, 7u);
    const sm::SoldierRecord highLevel = sm::make_soldier(
        static_cast<std::uint8_t>(sm::NPCType::Guard), 1000000, 8u);
    if (lowLevel.level != 1
        || highLevel.level != sm::kMaxSoldierLevel
        || sm::soldier_level_factor(1000000) <= 0) {
        return fail("soldier level normalization failed");
    }

    sm::SoldierSquad player{};
    sm::SoldierSquad garrison{};
    garrison.members.push_back(sm::make_soldier(
        static_cast<std::uint8_t>(sm::NPCType::Guard), 3, 42u));
    garrison.members.push_back(sm::make_soldier(
        static_cast<std::uint8_t>(sm::NPCType::Peasant), 1, 43u));

    int gold = 1000;
    const int expectedPrice = sm::hire_price_for(garrison.members.front());
    const int paid = sm::hire_npc(player, garrison, sm::NPCType::Guard, gold);
    if (paid != expectedPrice || gold != 1000 - expectedPrice) {
        return fail("hire_npc did not charge exact NPC-kind price");
    }
    if (player.members.size() != 1 || player.members[0].kind != std::uint8_t(sm::NPCType::Guard)
        || player.members[0].entityId != 42u || garrison.members.size() != 1) {
        return fail("hire_npc did not move the concrete soldier record");
    }

    const int denied = sm::hire_npc(player, garrison, sm::NPCType::Merchant, gold);
    if (denied != 0 || player.members.size() != 1 || garrison.members.size() != 1) {
        return fail("non-hireable NPC kind was recruitable");
    }

    sm::SoldierSquad duplicateIds{};
    duplicateIds.members.push_back(sm::make_soldier(
        static_cast<std::uint8_t>(sm::NPCType::Guard), 3, 77u));
    duplicateIds.members.push_back(sm::make_soldier(
        static_cast<std::uint8_t>(sm::NPCType::Guard), 4, 77u));
    duplicateIds.members.push_back(sm::make_soldier(
        static_cast<std::uint8_t>(sm::NPCType::Peasant), 1, 78u));
    if (!sm::remove_one_soldier_by_entity_id(duplicateIds, 77u)
        || duplicateIds.members.size() != 2
        || sm::count_soldiers_with_entity_id(duplicateIds, 77u) != 1
        || sm::remove_one_soldier_by_entity_id(duplicateIds, 999u)) {
        return fail("soldier death removal did not remove exactly one record");
    }

    const std::uint32_t dayOneBase = sm::garrison_soldier_id_base(12, 1);
    const std::uint32_t dayTwoBase = sm::garrison_soldier_id_base(12, 2);
    if (dayOneBase == dayTwoBase
        || dayOneBase + 9u == dayTwoBase
        || sm::npc_hire_price_base(sm::NPCType::Guard) != expectedPrice) {
        return fail("garrison id base or preview hire price is unstable");
    }

    const int baseUpkeep = sm::calculate_squad_upkeep(player, 0);
    const int charismaUpkeep = sm::calculate_squad_upkeep(player, 50);
    if (baseUpkeep <= 0 || charismaUpkeep >= baseUpkeep) {
        return fail("NPC-kind upkeep or charisma discount invalid");
    }

    FixedRng rng{};
    const sm::GarrisonResult generated = sm::generate_garrison(900, rng, 5000u);
    if (generated.garrison.members.empty()) {
        return fail("population garrison generator returned empty squad");
    }
    for (const sm::SoldierRecord& s : generated.garrison.members) {
        if (!sm::valid_npc_kind(s.kind)
            || !sm::npc_hireable(static_cast<sm::NPCType>(s.kind))
            || s.entityId < 5000u) {
            return fail("generated garrison contains invalid soldier record");
        }
    }

    if (sm::npc_xp_reward(sm::NPCType::Guard, 4)
        <= sm::npc_xp_reward(sm::NPCType::Guard, 1)) {
        return fail("NPC XP reward does not scale by level");
    }

    sm::SoldierSquad selfAppend = generated.garrison;
    const std::size_t selfAppendBase = selfAppend.members.size();
    sm::add_squad(selfAppend, selfAppend);
    if (selfAppend.members.size() != selfAppendBase * 2u
        || selfAppend.members[selfAppendBase].entityId
            != selfAppend.members[0].entityId) {
        return fail("self squad append is not stable");
    }

    sm::ecs::World world{};
    std::vector<std::uint8_t> emptyTiles;
    const std::uint16_t playerFaction =
        std::uint16_t(sm::faction_index(sm::kPlayerFactionId));
    sm::sub::spawn_player_squad(world, player, emptyTiles, 512.0f, 512.0f, 99u,
                                playerFaction);
    auto view = world.reg.view<sm::ecs::PlayerSoldierTag, sm::ecs::SoldierLink,
                               sm::ecs::NPCKind, sm::ecs::Combat,
                               sm::ecs::Health, sm::ecs::NpcLevel,
                               sm::ecs::SubworldAi>();
    int projected = 0;
    for (auto e : view) {
        ++projected;
        const auto& link = view.get<sm::ecs::SoldierLink>(e);
        const auto& kind = view.get<sm::ecs::NPCKind>(e);
        const auto& ai = view.get<sm::ecs::SubworldAi>(e);
        if (link.entityId != 42u
            || link.kind != std::uint8_t(sm::NPCType::Guard)
            || kind.type != std::uint16_t(sm::NPCType::Guard)
            || ai.kind != sm::ecs::SubworldAi::Combat) {
            return fail("subworld projection lost soldier identity or AI contract");
        }
        // A squad member fights for whoever raised the squad, whatever his kind:
        // the faction is the OWNER's, and it is real data on the body — not a
        // tag the battle pass has to special-case. Anything else (the old
        // hardcoded "empire") would make your own troops foreign subjects.
        if (kind.factionIdx != playerFaction) {
            return fail("squad member does not wear its owner's faction");
        }
    }
    if (projected != 1) {
        return fail("subworld projection did not create exactly one soldier entity");
    }

    sm::ecs::World malformedWorld{};
    std::vector<std::uint8_t> malformedTiles(1, 0u);
    sm::sub::spawn_player_squad(malformedWorld, player, malformedTiles,
                                512.0f, 512.0f, 100u, playerFaction);
    auto malformedView = malformedWorld.reg.view<sm::ecs::PlayerSoldierTag,
                                                 sm::ecs::SoldierLink>();
    int malformedProjected = 0;
    for (auto e : malformedView) {
        (void)e;
        ++malformedProjected;
    }
    if (malformedProjected != 1) {
        return fail("malformed tile buffer blocked squad projection");
    }

    auto hostile = world.reg.create();
    world.reg.emplace<sm::ecs::Position>(hostile, 100.0f, 100.0f, 0.0f);
    world.reg.emplace<sm::ecs::Combat>(hostile, 5.0f, 20.0f, 3.0f, 1.0f, 0.0f,
                                       sm::ecs::Combat::Melee);
    world.reg.emplace<sm::ecs::SubworldAi>(hostile, sm::ecs::SubworldAi::Combat,
                                           0.0f, 4.0f, 4.0f, 8.0f, 1.0f);
    sm::sub::tick_npc_ai(world, 140.0f, 100.0f, 0u, 0.5f);
    const auto& combatPos = world.reg.get<sm::ecs::Position>(hostile);
    const auto& combatAi = world.reg.get<sm::ecs::SubworldAi>(hostile);
    // Ownership contract, TIGHTENED. tick_npc_ai must not touch a combat body at
    // ALL — neither its position nor its velocity. The mass-battle pass
    // (sub/battle.h) owns both, and it READS the stored velocity as the body's
    // current momentum to apply an acceleration limit. The previous expectation
    // here was that tick_npc_ai zeroed vx/vy; that is incompatible with carrying
    // momentum across frames (a body restarted from rest every tick is capped at
    // one acceleration step, i.e. ~2 units/s instead of its real 35), so the
    // zeroing is gone and the assertion is now "left strictly untouched".
    if (combatPos.x != 100.0f || combatPos.y != 100.0f
        || combatAi.vx != 4.0f || combatAi.vy != 4.0f) {
        return fail("tick_npc_ai must leave ecs::Combat actors untouched");
    }

    auto fallback = world.reg.create();
    world.reg.emplace<sm::ecs::Position>(fallback, 100.0f, 100.0f, 0.0f);
    world.reg.emplace<sm::ecs::SubworldAi>(fallback, sm::ecs::SubworldAi::Combat,
                                           0.0f, 0.0f, 0.0f, 8.0f, 1.0f);
    sm::sub::tick_npc_ai(world, 140.0f, 100.0f, 0u, 0.5f);
    const auto& fallbackPos = world.reg.get<sm::ecs::Position>(fallback);
    // A body with combat AI but NO ecs::Combat has no damage, no speed and no
    // reach — it cannot fight. The old code still walked it at the player
    // scalar ("degrade to chase-only movement"), which was a hidden global
    // player attractor and, in shipping, unreachable: every spawn path that
    // emplaces SubworldAi::Combat also emplaces ecs::Combat (sub/spawn.cpp
    // fauna + squad projection). That path is deleted, so such a body is now
    // inert — nothing invents stats for it.
    if (fallbackPos.x != 100.0f || fallbackPos.y != 100.0f) {
        return fail("statless combat-AI body must be inert, not chase the player");
    }

    // ── The brain writes INTENT, never a position ──────────────────────────
    // One owner of legs: tick_npc_ai for a Wander mind decides wantVx/Vy and
    // must leave Position strictly alone (the battle steering pass executes).
    // And a STANDING mind must still be able to change it: the decision seed
    // folds in SubworldAi.seq, because entity⊕position alone froze a standing
    // body — its position stopped changing, so it re-rolled the same "stand"
    // forever, and deer petrified on their first idle roll.
    auto wanderer = world.reg.create();
    world.reg.emplace<sm::ecs::Position>(wanderer, 200.0f, 200.0f, 0.0f);
    world.reg.emplace<sm::ecs::SubworldAi>(wanderer,
                                           sm::ecs::SubworldAi::Wander,
                                           0.0f, 0.0f, 0.0f, 8.0f, 0.6f);
    bool decidedToWalk = false;
    bool decidedToStand = false;
    for (int i = 0; i < 400; ++i) {
        sm::sub::tick_npc_ai(world, 140.0f, 100.0f, 0u, 0.5f);
        const auto& wai = world.reg.get<sm::ecs::SubworldAi>(wanderer);
        if (wai.wantVx != 0.0f || wai.wantVy != 0.0f) decidedToWalk = true;
        else decidedToStand = true;
    }
    const auto& wpos = world.reg.get<sm::ecs::Position>(wanderer);
    if (wpos.x != 200.0f || wpos.y != 200.0f) {
        return fail("a wander brain must never write Position");
    }
    if (!decidedToWalk || !decidedToStand) {
        return fail("a standing mind stopped deciding (seq missing from seed)");
    }

    std::printf("OK combat_squad_test hired=%zu garrison=%zu upkeep=%d discounted=%d generated=%zu projected=%d malformed_tiles=%d ai_owner=1 unique_ids=1\n",
                player.members.size(), garrison.members.size(),
                baseUpkeep, charismaUpkeep, generated.garrison.members.size(),
                projected, malformedProjected);
    return 0;
}

// THE contract every subworld humanoid body must satisfy, whoever made it.
//
// The defect this exists to catch, stated as it was found: the player's squad
// was INVISIBLE. Its bodies were built by a hand-written emplace sequence that
// forgot `NpcCharacter`, and the paper-doll pass draws `Position + NpcCharacter`
// — so ten hired soldiers walked beside the player as nothing at all, swinging
// swords no one could see. Nothing was null, nothing crashed, no test failed:
// the body was simply less of a body than the renderer assumed.
//
// A component list cannot be defended by remembering it. It is defended by
// asserting it — here, over the bodies the SHIPPING spawner actually produces.
// Every field checked below is checked against the ONE table row the body was
// made from (macro/npc.h kNpcTypeDefs → project_combat), never against a number
// restated in this file: retuning a soldier must never require editing a test,
// and a spawner that invents its own numbers must fail.
#include "check.h"

#include "sub/spawn.h"
#include "sub/map_data.h"
#include "ecs/components.h"
#include "ecs/npc_character.h"
#include "macro/army.h"
#include "macro/character_sheet.h"
#include "macro/faction.h"
#include "macro/npc.h"

#include <entt/entt.hpp>
#include <vector>

namespace {

// A roster of mixed kinds: the owner's rule is that a squad is CONTEXT from the
// macro world and may hold anyone the tables know, so the contract must hold
// for every kind alike, not just for the one the fixture happened to pick.
sm::SoldierSquad mixed_squad() {
    sm::SoldierSquad squad{};
    squad.members.push_back(sm::make_soldier(std::uint8_t(sm::NPCType::Guard),    4, 11u));
    squad.members.push_back(sm::make_soldier(std::uint8_t(sm::NPCType::Peasant),  1, 22u));
    squad.members.push_back(sm::make_soldier(std::uint8_t(sm::NPCType::Bandit),   3, 33u));
    squad.members.push_back(sm::make_soldier(std::uint8_t(sm::NPCType::Sorceress),5, 44u));
    return squad;
}

void test_every_squad_body_is_a_whole_body() {
    using namespace sm;
    const std::uint16_t playerFaction =
        std::uint16_t(faction_index(kPlayerFactionId));

    ecs::World world{};
    std::vector<std::uint8_t> ground(
        std::size_t(sub::kFullSize) * sub::kFullSize, sub::TILE_GRASS);
    sub::spawn_player_squad(world, mixed_squad(), ground,
                            512.0f, 512.0f, 123u, playerFaction);

    auto& reg = world.reg;
    auto view = reg.view<ecs::PlayerSoldierTag>();

    int bodies = 0;
    int missingFace = 0, missingCombat = 0, missingHealth = 0, missingSheet = 0;
    int missingAi = 0, missingSprite = 0, missingLevel = 0, missingTag = 0;
    int wrongFaction = 0, wrongSpriteKind = 0, offTableRadius = 0, deadOnArrival = 0;

    for (auto e : view) {
        ++bodies;
        const auto* kind   = reg.try_get<ecs::NPCKind>(e);
        const auto* health = reg.try_get<ecs::Health>(e);
        const auto* combat = reg.try_get<ecs::Combat>(e);
        const auto* sprite = reg.try_get<ecs::Sprite>(e);
        const auto* ai     = reg.try_get<ecs::SubworldAi>(e);

        // The paper-doll pass draws Position + NpcCharacter. A body without a
        // face is a body nobody can see — the exact defect of 2026-08-06.
        if (!reg.all_of<ecs::NpcCharacter>(e))   ++missingFace;
        if (!reg.all_of<ecs::Position, ecs::VisualPos>(e)) ++missingTag;
        if (!health) ++missingHealth;
        if (!combat) ++missingCombat;
        if (!reg.all_of<CharacterSheet>(e))      ++missingSheet;
        if (!ai)     ++missingAi;
        if (!sprite) ++missingSprite;
        if (!reg.all_of<ecs::NpcLevel>(e))       ++missingLevel;

        if (kind && kind->factionIdx != playerFaction) ++wrongFaction;
        // The sprite must name the SAME kind the body is, or the renderer draws
        // one creature wearing another's picture.
        if (kind && sprite && sprite->atlasId != kind->type) ++wrongSpriteKind;
        if (health && !(health->hp > 0.0f && health->hp == health->maxHp))
            ++deadOnArrival;

        // Body width comes from the TABLE (CombatTemplate::bodyRadius), so what
        // blocks, what is hit and what is drawn are one number. A spawner that
        // hardcodes its own width shows up right here.
        if (kind && ai && kind->type < std::uint16_t(NPCType::Count)) {
            const NpcTypeDef& def = npc_def(NPCType(std::uint8_t(kind->type)));
            const auto* sheet = reg.try_get<CharacterSheet>(e);
            if (sheet) {
                const CombatTemplate pc = project_combat(*sheet, def.combat);
                if (ai->radius != pc.bodyRadius) ++offTableRadius;
                if (sprite && sprite->scale != pc.bodyRadius) ++offTableRadius;
            }
        }
    }

    CHECK(bodies == 4,
          "the squad embodies every member of its roster, whatever kind it is");
    CHECK(bodies > 0 && missingFace == 0,
          "every body has a face: the paper-doll pass can see all of them");
    CHECK(bodies > 0 && missingTag == 0,
          "every body has a place in the world and a drawn position");
    CHECK(bodies > 0 && missingHealth == 0 && missingCombat == 0,
          "every body can be hurt and can hurt back");
    CHECK(bodies > 0 && missingSheet == 0,
          "every body carries the character sheet its combat is derived from");
    CHECK(bodies > 0 && missingAi == 0 && missingLevel == 0,
          "every body knows how to act and what rank it holds");
    CHECK(bodies > 0 && missingSprite == 0,
          "every body has something to draw");
    CHECK(bodies > 0 && wrongFaction == 0,
          "a squad fights for whoever raised it: the leader's faction, not its own kind's");
    CHECK(bodies > 0 && wrongSpriteKind == 0,
          "a body is drawn as the kind it actually is");
    CHECK(bodies > 0 && deadOnArrival == 0,
          "a body arrives alive and at full health");
    CHECK(bodies > 0 && offTableRadius == 0,
          "body width comes from the table row, so what is drawn is what collides");
}

} // namespace

int main() {
    test_every_squad_body_is_a_whole_body();
    return sm::test::report("body_contract_test");
}

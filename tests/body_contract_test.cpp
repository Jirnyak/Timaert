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

// ── The other half of the axis ────────────────────────────────────────────
//
// A DERIVED body stands for a number and remembers nothing; a TRACKED body IS a
// macro entity made visible and remembers everything. The four hand-written
// spawners differed in exactly this and nothing else, so these are the checks
// that keep a sixth one from inventing a third answer.

// One macro entity, shaped the way the overworld shapes them.
entt::entity make_macro_lord(entt::registry& reg, sm::NPCType type,
                             std::uint16_t faction, int level,
                             float hp, float maxHp,
                             std::uint32_t visualSeed) {
    const auto e = reg.create();
    reg.emplace<sm::ecs::MacroNpcRuntime>(e);
    reg.emplace<sm::ecs::Position>(e, 10.0f, 12.0f, 0.0f);
    reg.emplace<sm::ecs::NPCKind>(e, std::uint16_t(type), faction);
    reg.emplace<sm::ecs::Health>(e, hp, maxHp);
    reg.emplace<sm::ecs::NpcLevel>(e, std::int16_t(level));
    sm::ecs::NpcCharacter face{};
    face.visualSeed = visualSeed;
    face.nameIdx = 3;
    reg.emplace<sm::ecs::NpcCharacter>(e, face);
    sm::ecs::NpcTraits traits{};
    traits.count = 1;
    traits.traits[0] = 2;
    reg.emplace<sm::ecs::NpcTraits>(e, traits);
    sm::ecs::NpcInventory bag{};
    bag.inv.add("mat_wood", 3);
    reg.emplace<sm::ecs::NpcInventory>(e, std::move(bag));
    return e;
}

void test_a_tracked_body_is_the_entity_it_embodies() {
    using namespace sm;
    ecs::World world{};
    auto& reg = world.reg;

    // Half dead on the map, with a face and belongings of his own.
    const entt::entity macro = make_macro_lord(
        reg, NPCType::Guard, /*faction*/5, /*level*/4,
        /*hp*/15.0f, /*maxHp*/30.0f, /*visualSeed*/0xFEEDu);

    const entt::entity body =
        sub::spawn_tracked_body(reg, macro, 40.0f, 41.0f, 777u,
                                /*combatant*/true);
    CHECK_OR_RETURN(body != entt::null && reg.valid(body),
                    "a body-shaped macro entity can be embodied");

    CHECK((reg.all_of<ecs::NpcCharacter, ecs::Position, ecs::Health, ecs::Combat,
                      CharacterSheet, ecs::SubworldAi, ecs::NpcLevel,
                      ecs::Sprite, ecs::SubworldTag>(body)),
          "a tracked body is as whole a body as a derived one");
    CHECK(reg.get<ecs::NpcCharacter>(body).visualSeed == 0xFEEDu,
          "the same lord wears the same face in both worlds");
    CHECK(reg.get<ecs::NPCKind>(body).factionIdx == 5,
          "a tracked body wears its own allegiance, read from the entity itself");
    CHECK(reg.get<ecs::NpcLevel>(body).value == 4,
          "a tracked body holds its own rank");

    // The wound crosses as a fraction, not as points: half above, half below,
    // whatever either layer thinks a health bar is worth.
    {
        const auto& h = reg.get<ecs::Health>(body);
        const float frac = h.maxHp > 0.0f ? h.hp / h.maxHp : -1.0f;
        CHECK(frac > 0.4f && frac < 0.6f,
              "a wounded entity arrives wounded, in proportion");
    }
    // Belongings and personality are STATE up there, so they come down with it.
    CHECK(reg.all_of<ecs::NpcInventory>(body)
              && reg.get<ecs::NpcInventory>(body).inv.count("mat_wood") == 3,
          "a tracked body carries what its entity carries");
    CHECK(reg.all_of<ecs::NpcTraits>(body)
              && reg.get<ecs::NpcTraits>(body).count == 1,
          "a tracked body keeps its entity's personality");
    // The backlink is the address the return trip writes to — wounds up, death
    // up. Without it the encounter is a stranger who happens to look like him.
    CHECK(reg.all_of<ecs::MacroOrigin>(body)
              && reg.get<ecs::MacroOrigin>(body).macro == macro,
          "a tracked body knows which entity it is");

    // A whole entity arrives whole: the control that stops "arrives wounded"
    // from passing by wounding everyone.
    const entt::entity whole = make_macro_lord(
        reg, NPCType::Bandit, /*faction*/2, /*level*/3,
        /*hp*/9.0f, /*maxHp*/9.0f, /*visualSeed*/0xB0B0u);
    const entt::entity wholeBody =
        sub::spawn_tracked_body(reg, whole, 60.0f, 61.0f, 778u, true);
    CHECK_OR_RETURN(wholeBody != entt::null, "the control body was embodied");
    {
        const auto& h = reg.get<ecs::Health>(wholeBody);
        CHECK(h.maxHp > 0.0f && h.hp == h.maxHp,
              "an untouched entity arrives untouched");
    }
}

void test_a_body_that_is_not_an_entity_is_refused() {
    using namespace sm;
    ecs::World world{};
    auto& reg = world.reg;

    // Half-tracked is the failure mode the two forms exist to make impossible:
    // an entity missing what a body is made of yields NO body, never a body with
    // a hole in it.
    const auto bare = reg.create();
    reg.emplace<ecs::Position>(bare, 1.0f, 1.0f, 0.0f);
    CHECK(sub::spawn_tracked_body(reg, bare, 5.0f, 5.0f, 1u, false) == entt::null,
          "an entity that is not body-shaped is refused, not half-embodied");
    CHECK(sub::spawn_tracked_body(reg, entt::null, 5.0f, 5.0f, 1u, false)
              == entt::null,
          "nothing embodies nothing");

    // A monster id (0x100 | index) is not a humanoid row and must never alias
    // one — the guard is on the WIDE type, before any narrowing.
    const auto monster = reg.create();
    reg.emplace<ecs::NPCKind>(monster, std::uint16_t(0x103), std::uint16_t(1));
    reg.emplace<ecs::Health>(monster, 10.0f, 10.0f);
    reg.emplace<ecs::NpcLevel>(monster, std::int16_t(2));
    reg.emplace<ecs::NpcCharacter>(monster, ecs::NpcCharacter{});
    CHECK(sub::spawn_tracked_body(reg, monster, 5.0f, 5.0f, 1u, false)
              == entt::null,
          "a monster id cannot pass for a humanoid row");
}

void test_a_derived_body_stores_only_what_its_seed_cannot_say() {
    using namespace sm;
    ecs::World world{};
    auto& reg = world.reg;

    // THE RULE (owner, 2026-08-06): a derived body stores nothing its seed
    // already decides. Its loot is rolled from its row at the moment it dies
    // (macro/items.h — one registry, one door), so carrying a pre-rolled bag
    // would cost a city of five thousand people five thousand allocations to
    // say what one integer says.
    const entt::entity e = sub::spawn_derived_body(reg,
        sub::HumanoidBody{NPCType::Peasant, 8.0f, 9.0f, /*faction*/1,
                          /*level*/2, /*seed*/4242u, /*combatant*/false},
        /*faceSalt*/7u);
    CHECK_OR_RETURN(e != entt::null && reg.valid(e), "a derived body is born");
    CHECK(!reg.all_of<ecs::NpcInventory>(e),
          "a derived body carries no bag: its loot is rolled when it dies");
    CHECK(!reg.all_of<ecs::MacroOrigin>(e),
          "a derived body is nobody in particular up there");
    CHECK(!reg.all_of<ecs::MacroDebt>(e),
          "a body borrowed from nothing owes nothing");

    // Borrowed, and it says so. The receipt is stamped BY the birth, so a
    // spawner cannot draw on a stock and forget to write it down.
    const entt::entity borrowed = sub::spawn_derived_body(reg,
        sub::HumanoidBody{NPCType::Guard, 8.0f, 9.0f, 1, 2, 99u, false},
        /*faceSalt*/1u,
        sub::BodyLoan::from(MacroStock::Population, MacroStockKey{7, 3, 4}));
    CHECK_OR_RETURN(borrowed != entt::null, "a borrowed body is born");
    const auto* debt = reg.try_get<ecs::MacroDebt>(borrowed);
    CHECK(debt != nullptr
              && debt->stock == std::uint8_t(MacroStock::Population)
              && debt->subject == 7 && debt->amount == 1,
          "a borrowed body carries the receipt naming what lent it");

    // Two bodies from the same seed but a different salt are different people —
    // otherwise a crowd is one man standing in many places.
    const entt::entity a = sub::spawn_derived_body(reg,
        sub::HumanoidBody{NPCType::Peasant, 1.0f, 1.0f, 1, 1, 5150u, false}, 1u);
    const entt::entity b = sub::spawn_derived_body(reg,
        sub::HumanoidBody{NPCType::Peasant, 2.0f, 2.0f, 1, 1, 5150u, false}, 2u);
    CHECK_OR_RETURN(a != entt::null && b != entt::null, "both crowd bodies born");
    CHECK(reg.get<ecs::NpcCharacter>(a).visualSeed
              != reg.get<ecs::NpcCharacter>(b).visualSeed,
          "the salt makes a crowd out of one seed");
}

} // namespace

int main() {
    test_every_squad_body_is_a_whole_body();
    test_a_tracked_body_is_the_entity_it_embodies();
    test_a_body_that_is_not_an_entity_is_refused();
    test_a_derived_body_stores_only_what_its_seed_cannot_say();
    return sm::test::report("body_contract_test");
}

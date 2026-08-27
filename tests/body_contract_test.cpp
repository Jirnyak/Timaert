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
#include "sub/body.h"
#include "sub/map_data.h"
#include "ecs/components.h"
#include "ecs/npc_character.h"
#include "macro/army.h"
#include "macro/character_sheet.h"
#include "macro/faction.h"
#include "macro/npc.h"

#include <entt/entt.hpp>
#include <algorithm>
#include <vector>

namespace {

// A roster of mixed kinds: the owner's rule is that a squad is CONTEXT from the
// macro world and may hold anyone the tables know, so the contract must hold
// for every kind alike, not just for the one the fixture happened to pick.
sm::SoldierSquad mixed_squad() {
    sm::SoldierSquad squad{};
    squad.push(sm::make_soldier(std::uint8_t(sm::NPCType::Guard),    4, 11u));
    squad.push(sm::make_soldier(std::uint8_t(sm::NPCType::Peasant),  1, 22u));
    squad.push(sm::make_soldier(std::uint8_t(sm::NPCType::Bandit),   3, 33u));
    squad.push(sm::make_soldier(std::uint8_t(sm::NPCType::Sorceress),5, 44u));
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
    int offTableHeight = 0;

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

        // Body width comes from the TABLE's one column (NpcTypeDef::radius,
        // man-shaped default resolved by npc_body_radius), so what blocks,
        // what is hit and what is drawn are one number. A spawner that
        // hardcodes its own width shows up right here.
        if (kind && ai && kind->type < std::uint16_t(NPCType::Count)) {
            const NpcTypeDef& def = npc_def(NPCType(std::uint8_t(kind->type)));
            const auto* sheet = reg.try_get<CharacterSheet>(e);
            if (sheet) {
                if (ai->radius != npc_body_radius(def)) ++offTableRadius;
                if (sprite && sprite->scale != npc_body_radius(def))
                    ++offTableRadius;
            }
            // …and so does its HEIGHT, which the renderer used to invent as a
            // flat 2 metres for everyone. Derived from the same row, varied by
            // the body's own shape byte — so this expectation moves when the
            // table moves and cannot be satisfied by a literal.
            const auto* face = reg.try_get<ecs::NpcCharacter>(e);
            if (sprite && face) {
                const float want = sub::body_height_m(def)
                    * sub::body_shape_height_scale(face->bodyShape);
                if (sprite->height != want) ++offTableHeight;
                if (!(sprite->height > 0.0f)) ++offTableHeight;
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
    CHECK(bodies > 0 && offTableHeight == 0,
          "body height comes from the table row too, varied by the body's own shape");
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
    bag.inv.add("wood", 3);
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
              && reg.get<ecs::NpcInventory>(body).inv.count("wood") == 3,
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

// A leader's buff reaches a trooper THROUGH THE SHEET, and it is not one
// number (Session 15, owner ruling №2): the aura is a SET of modifiers
// collected from the leader's own sheet (macro/aura.h — perk rows today;
// charisma, skills and items are future source functions at the same door),
// applied into the member's sheet before combat is projected from it.
void test_a_leaders_aura_reaches_his_men() {
    using namespace sm;

    // The collector: aura sources are data rows, not branches.
    CharacterSheet leader{};
    CHECK(collect_leader_aura(leader).count == 0,
          "a sheet with no aura sources buffs nobody");
    add_perk(leader.perks, PerkID::Leader);
    const AuraMods aura = collect_leader_aura(leader);
    CHECK(aura.count == 1
              && aura.mods[0].row == std::uint8_t(BonusId::Vit)
              && aura.mods[0].value == 1,
          "the Leader perk row collects as +1 vit: the '+10 HP to every "
          "soldier' the owner named, said through the ONE bonus registry");

    // The applier clamps at the same doors a legitimate point spend does.
    CharacterSheet clamped{};
    AuraMods curse{};
    aura_add(curse, {std::uint8_t(BonusId::Vit), -50});
    aura_add(curse, {std::uint8_t(BonusId::Bodybuilding), std::int16_t(500)});
    apply_aura(clamped, curse);
    CHECK(clamped.attributes.of(sm::AttributeId::Vit) == 1,
          "an aura cannot curse an attribute below its base of 1");
    CHECK(clamped.skills.of(sm::SkillId::Bodybuilding) == kMaxSkillRank,
          "an aura cannot push a skill past mastery");

    // End to end through the ONE birth: the same squad born twice from the
    // same seed — once led, once alone. Every soldier must be tougher led,
    // by the vit point's worth (10 HP scaled by his own bodybuilding, never
    // more than the capstone allows) — a constant resolver cannot satisfy a
    // difference between two live runs.
    const std::uint16_t f = std::uint16_t(faction_index(kPlayerFactionId));
    std::vector<std::uint8_t> ground(
        std::size_t(sub::kFullSize) * sub::kFullSize, sub::TILE_GRASS);
    ecs::World led{}, alone{};
    sub::spawn_player_squad(led, mixed_squad(), ground, 512.0f, 512.0f, 123u,
                            f, &aura);
    sub::spawn_player_squad(alone, mixed_squad(), ground, 512.0f, 512.0f, 123u,
                            f);

    int compared = 0;
    for (auto eLed : led.reg.view<ecs::SoldierLink>()) {
        const auto& linkLed = led.reg.get<ecs::SoldierLink>(eLed);
        for (auto eAlone : alone.reg.view<ecs::SoldierLink>()) {
            if (alone.reg.get<ecs::SoldierLink>(eAlone).entityId
                    != linkLed.entityId) {
                continue;
            }
            const float withAura = led.reg.get<ecs::Health>(eLed).maxHp;
            const float without  = alone.reg.get<ecs::Health>(eAlone).maxHp;
            const float delta = withAura - without;
            CHECK(delta >= 9.0f && delta <= 61.0f,
                  "a led soldier is tougher by his leader's vit point - "
                  "10 HP through his own sheet, never a flat second number");
            ++compared;
        }
    }
    CHECK(compared == 4, "every soldier of the roster was born in both runs");
}

// A squad on the map projects its ROSTER, not just its leader (Session 15):
// the leader arrives as the tracked body it always was, and every roster row
// arrives as a derived body wearing the OWNER's faction and carrying the
// receipt that pays its death back into the roster — the same one-table,
// one-door law as every other borrowed thing.
void test_a_squad_on_the_map_projects_its_roster() {
    using namespace sm;
    ecs::World world{};
    auto& reg = world.reg;

    const entt::entity macro = make_macro_lord(
        reg, NPCType::Guard, /*faction*/5, /*level*/4,
        /*hp*/30.0f, /*maxHp*/30.0f, /*visualSeed*/0xCAFEu);
    reg.emplace<ecs::MacroSpawnId>(macro, std::uint32_t(9));
    {
        auto& roster = reg.emplace<ecs::SquadRoster>(macro);
        roster.squad.push(make_soldier(
            std::uint8_t(NPCType::Guard), 4, 77u));
        roster.squad.push(make_soldier(
            std::uint8_t(NPCType::Bandit), 2, 88u));
    }

    std::vector<std::uint8_t> ground(
        std::size_t(sub::kFullSize) * sub::kFullSize, sub::TILE_GRASS);
    // The lord stands at macro cell (10,12) — the helper's position — so
    // centre the window there.
    const int projected = sub::project_macro_npcs_into_subworld(
        world, ground, /*centerCx*/10, /*centerCy*/12,
        /*mapW*/64, /*mapH*/64, /*seed*/5u);
    CHECK(projected == 3,
          "a squad of three projects three bodies: the leader and both members");

    int leaders = 0, members = 0, wrongFaction = 0, wholeMembers = 0;
    bool saw77 = false, saw88 = false;
    for (auto e : reg.view<ecs::SubworldTag>()) {
        if (reg.all_of<ecs::MacroOrigin>(e)) {
            ++leaders;
            continue;
        }
        const auto* debt = reg.try_get<ecs::MacroDebt>(e);
        if (!debt) continue;
        ++members;
        CHECK(debt->stock == std::uint8_t(MacroStock::Roster)
                  && debt->subject == 9,
              "a member's receipt names the roster row and its own squad");
        saw77 = saw77 || debt->detail == 77;
        saw88 = saw88 || debt->detail == 88;
        const auto* kind = reg.try_get<ecs::NPCKind>(e);
        if (kind && kind->factionIdx != 5) ++wrongFaction;
        if (reg.all_of<ecs::NpcCharacter, CharacterSheet, ecs::Sprite,
                       ecs::Health, ecs::Combat>(e)) {
            ++wholeMembers;
        }
    }
    CHECK(leaders == 1, "the leader is the one tracked body");
    CHECK(members == 2 && saw77 && saw88,
          "every member arrives once, each receipt naming its own man");
    CHECK(wrongFaction == 0,
          "members fight under the squad owner's banner, whoever they are");
    CHECK(wholeMembers == 2,
          "a projected member is as whole a body as any other");

    // The return trip, end to end: a member's death pays the roster row, the
    // second death empties it — and an empty roster around a LIVE leader is a
    // squad of one, alive and well, not a special case anyone must clean up.
    MacroWorld w{nullptr, nullptr, &world};
    for (auto e : reg.view<ecs::MacroDebt, ecs::SubworldTag>()) {
        settle_macro_debt(w, reg.get<ecs::MacroDebt>(e), -1);
    }
    CHECK(reg.get<ecs::SquadRoster>(macro).squad.empty(),
          "both deaths below emptied the roster above, by name");
    CHECK(!reg.all_of<ecs::Dead>(macro),
          "the leader outlives his men: an empty roster is a squad of one");
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
        sub::BodySpec{NPCType::Peasant, 8.0f, 9.0f, /*faction*/1,
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
        sub::BodySpec{NPCType::Guard, 8.0f, 9.0f, 1, 2, 99u, false},
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
        sub::BodySpec{NPCType::Peasant, 1.0f, 1.0f, 1, 1, 5150u, false}, 1u);
    const entt::entity b = sub::spawn_derived_body(reg,
        sub::BodySpec{NPCType::Peasant, 2.0f, 2.0f, 1, 1, 5150u, false}, 2u);
    CHECK_OR_RETURN(a != entt::null && b != entt::null, "both crowd bodies born");
    CHECK(reg.get<ecs::NpcCharacter>(a).visualSeed
              != reg.get<ecs::NpcCharacter>(b).visualSeed,
          "the salt makes a crowd out of one seed");
}

} // namespace

void test_two_bodies_of_one_kind_can_differ_in_height() {
    using namespace sm;
    ecs::World world{};
    auto& reg = world.reg;

    // A crowd of one kind, drawn from one row: the row fixes what a peasant is,
    // the face's shape byte decides how tall THIS peasant is. Before this, the
    // renderer drew all of them at exactly two metres, so the variation existed
    // in the data and nowhere on screen.
    float shortest = 1e9f, tallest = 0.0f;
    int seen = 0;
    for (std::uint32_t i = 0; i < 64u; ++i) {
        const entt::entity e = sub::spawn_derived_body(reg,
            sub::BodySpec{NPCType::Peasant, 5.0f, 5.0f, 1, 2,
                              /*seed*/1000u + i, false},
            /*faceSalt*/i * 7919u);
        if (e == entt::null) continue;
        const auto* spr = reg.try_get<ecs::Sprite>(e);
        const auto* face = reg.try_get<ecs::NpcCharacter>(e);
        if (!spr || !face) continue;
        ++seen;
        shortest = std::min(shortest, spr->height);
        tallest = std::max(tallest, spr->height);
        // Every body's height is its row's height times its own shape — no
        // literal can satisfy this, because both factors are read back.
        const float want = sub::body_height_m(npc_def(NPCType::Peasant))
            * sub::body_shape_height_scale(face->bodyShape);
        CHECK_OR_RETURN(spr->height == want,
                        "a body is as tall as its row and its shape say");
    }
    CHECK_OR_RETURN(seen >= 32, "the crowd was actually born and measured");
    CHECK(tallest > shortest,
          "one man is taller than another: the shape byte reaches the screen");
    // A human stays human-sized: the variation is a person's spread, not a
    // licence for two-metre peasants and gnomes.
    CHECK(shortest > 1.0f && tallest < 2.5f,
          "the spread stays within human proportions");
}

void test_a_stated_height_is_obeyed() {
    using namespace sm;
    // The checks above derive their expectation from body_height_m() itself, so
    // they pin the SPAWNER (it must not invent numbers) but they cannot catch
    // the resolver being replaced by a constant — a test that compares a
    // function with itself passes on any answer. This one cannot: it hands the
    // resolver a row that states an unmistakable height and demands exactly it.
    NpcTypeDef tall = npc_def(NPCType::Peasant);
    tall.combat.bodyHeight = 3.25f;
    CHECK(sub::body_height_m(tall) == 3.25f,
          "a humanoid row that states its height is obeyed");
    CHECK(sub::body_height_m(npc_def(NPCType::Peasant)) != 3.25f,
          "...and a silent row is not accidentally the same number");

    sm::FaunaEntry giant = *sm::creature_catalog()[0];
    giant.combat.bodyHeight = 7.5f;
    CHECK(sub::body_height_m(giant) == 7.5f,
          "a creature row that states its height is obeyed");
    CHECK(sub::body_height_m(*sm::creature_catalog()[0]) != 7.5f,
          "...and a silent creature row is not accidentally the same number");

    // The shape byte spans a real range: four values, not one repeated.
    CHECK(sub::body_shape_height_scale(0) != sub::body_shape_height_scale(3),
          "the shape byte actually changes the answer");
}

void test_a_creature_is_as_tall_as_its_own_row() {
    using namespace sm;
    // A creature that states no height keeps EXACTLY the proportion the
    // renderer used to hardcode (radius × 1.5), so nothing that has not opted
    // in changes appearance; a row that states one is obeyed.
    int checked = 0;
    for (const sm::FaunaEntry* row : sm::creature_catalog()) {
        if (!row) continue;
        const float h = sub::body_height_m(*row);
        if (row->combat.bodyHeight > 0.0f) {
            CHECK_OR_RETURN(h == row->combat.bodyHeight,
                            "a creature that states its height gets it");
        } else {
            CHECK_OR_RETURN(h == row->radius * sub::kCreatureHeightPerRadius,
                            "a silent creature row keeps its old proportion");
        }
        ++checked;
    }
    CHECK(checked > 0, "the creature table was actually walked");
}

int main() {
    test_every_squad_body_is_a_whole_body();
    test_two_bodies_of_one_kind_can_differ_in_height();
    test_a_stated_height_is_obeyed();
    test_a_creature_is_as_tall_as_its_own_row();
    test_a_tracked_body_is_the_entity_it_embodies();
    test_a_body_that_is_not_an_entity_is_refused();
    test_a_leaders_aura_reaches_his_men();
    test_a_squad_on_the_map_projects_its_roster();
    test_a_derived_body_stores_only_what_its_seed_cannot_say();
    return sm::test::report("body_contract_test");
}

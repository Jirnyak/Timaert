// THE damage door (sub/damage.h) — the law that every weapon strikes through
// one function, and death from any of them is indistinguishable by protocol.
//
// What is pinned, with negative controls:
//   * one protocol: any lethal blow leaves the same component set (Dead +
//     DamageFx{lethal} + HitFlash) and emits exactly one NpcDeath with the
//     right attribution (a = victim, b = attacker, ix = kind, iy = spellId);
//   * attribution is DATA: the Fall/Script rows stamp no LastHit (nobody gets
//     XP for gravity), the Melee/Spell/Dev rows do;
//   * the ONE PlayerTag guard: a dead player emits no NpcDeath from ANY kind —
//     the spell path used to miss this guard and count a player death toward
//     quest kill-tallies;
//   * a kindless body still emits (ix = kNoNpcType) — the spell path used to
//     stay silent for it while every other weapon spoke;
//   * the ONE already-dead guard: a corpse takes no second blow, no matter the
//     weapon — the spell path used to have none;
//   * MITIGATION by the hybrid law (macro/damage_types.h): a body in its own
//     skin keeps the identity (armour 0 is the limiting case, not a branch),
//     an armoured row cuts the larger of its column's threshold or the
//     halving fraction — full block of a blow the plate outweighs is REAL
//     (owner verdict 2026-09-05) — and whether armour is in the way at all is
//     the damage KIND's column: plate does not soften a fall.

#include "check.h"
#include "sub/damage.h"
#include "macro/npc.h"
#include "ecs/components.h"
#include "events/event_bus.h"
#include "events/event_types.h"

#include <cstdio>

namespace {

using sm::sub::apply_damage;
using sm::sub::apply_lethal_damage;
using sm::sub::DamageKind;
using sm::sub::DamageResult;
using sm::sub::DamageSource;

constexpr std::uint16_t kTestNpcType = 7;

entt::entity make_body(entt::registry& reg, float hp, bool withKind = true) {
    const entt::entity e = reg.create();
    reg.emplace<sm::ecs::Health>(e, hp, hp);
    if (withKind) reg.emplace<sm::ecs::NPCKind>(e, kTestNpcType,
                                                std::uint16_t{0});
    return e;
}

int death_events(const sm::EventBus& bus) {
    int n = 0;
    for (const auto& ev : bus.tick_events())
        if (ev.tag == sm::EventTag::NpcDeath) ++n;
    return n;
}

const sm::GameEvent* last_death(const sm::EventBus& bus) {
    const sm::GameEvent* found = nullptr;
    for (const auto& ev : bus.tick_events())
        if (ev.tag == sm::EventTag::NpcDeath) found = &ev;
    return found;
}

// One lethal blow of each kind must leave the identical death protocol.
void test_death_is_indistinguishable() {
    const DamageKind kinds[] = {DamageKind::Melee, DamageKind::Spell,
                                DamageKind::Fall, DamageKind::Script,
                                DamageKind::Dev};
    for (const DamageKind kind : kinds) {
        entt::registry reg;
        sm::EventBus bus;
        const entt::entity e = make_body(reg, 10.0f);
        const DamageSource src{42u, false,
                               kind == DamageKind::Spell ? 900u : 0u};
        const DamageResult hit = apply_damage(reg, e, src, 25.0f, kind,
                                              sm::DamageType::Blunt, &bus);

        CHECK(hit.applied == 25.0f, "lethal blow applies its full amount");
        CHECK(hit.lethal, "a blow past remaining hp is lethal");
        CHECK(reg.all_of<sm::ecs::Dead>(e), "every kind stamps Dead");
        CHECK(reg.all_of<sm::ecs::HitFlash>(e), "every kind stamps HitFlash");
        CHECK(reg.all_of<sm::ecs::DamageFx>(e), "every kind stamps DamageFx");
        CHECK(reg.get<sm::ecs::DamageFx>(e).lethal,
              "the killing blow's DamageFx is lethal");
        CHECK(death_events(bus) == 1, "every kind emits exactly one NpcDeath");
        if (const sm::GameEvent* ev = last_death(bus)) {
            CHECK(ev->a == std::uint32_t(entt::to_integral(e)),
                  "NpcDeath.a names the victim");
            CHECK(ev->b == 42u, "NpcDeath.b names the attacker");
            CHECK(ev->ix == int(kTestNpcType),
                  "NpcDeath.ix carries the victim's kind");
            CHECK(ev->iy == (kind == DamageKind::Spell ? 900 : 0),
                  "NpcDeath.iy carries the spell id and only for spells");
        }

        // Attribution is the kind row's DATA, not a per-site omission.
        const bool wantsKiller =
            sm::sub::kDamageKinds[std::size_t(kind)].attributesKiller;
        CHECK(reg.all_of<sm::ecs::LastHit>(e) == wantsKiller,
              "LastHit follows the kind row's attributesKiller column");
        if (wantsKiller) {
            CHECK(reg.get<sm::ecs::LastHit>(e).attackerId == 42u,
                  "LastHit names the attacker the source named");
        }
    }
}

// ── ARMOUR ───────────────────────────────────────────────────────────────
// The socket the damage-door track deliberately left open (work_vector §5).
// Pinned as three separate claims, because they can each be broken alone.
void test_armour_softens_by_the_row_and_the_kind() {
    entt::registry reg;
    sm::EventBus bus;

    // The Guard row wears plate; the test's own kind (7) wears nothing. Same
    // door, same blow, two rows — this is the negative control that says the
    // number is READ and not assumed.
    const entt::entity bare = make_body(reg, 100.0f);
    const entt::entity plated = reg.create();
    reg.emplace<sm::ecs::Health>(plated, 100.0f, 100.0f);
    reg.emplace<sm::ecs::NPCKind>(
        plated, std::uint16_t(sm::NPCType::Guard), std::uint16_t{0});

    const float blow = 20.0f;
    const DamageResult onBare =
        apply_damage(reg, bare, DamageSource{}, blow, DamageKind::Melee, sm::DamageType::Blunt, &bus);
    const DamageResult onPlate =
        apply_damage(reg, plated, DamageSource{}, blow, DamageKind::Melee, sm::DamageType::Blunt, &bus);

    CHECK(onBare.applied == blow, "an unarmoured row takes the whole blow");
    CHECK(onPlate.applied < onBare.applied,
          "and an armoured one takes less of the SAME blow");

    // The door routes through THE law: expectation is mitigate_amount over
    // the row's own column, not a pinned number (testing law #4). The law's
    // own shape is asserted separately below.
    const int armour = sm::npc_def(sm::NPCType::Guard).armor
                           .of(sm::DamageType::Blunt);
    const float expect = float(sm::mitigate_amount(int(blow), armour));
    CHECK(onPlate.applied == expect,
          "the door applies exactly the hybrid law of the blow's own column");

    // The hybrid's THRESHOLD branch (owner verdict 2026-09-05): a blow no
    // bigger than the plate finds no flesh at all — full block is real, and
    // a blocked blow is a silent no-op like any zero contribution.
    const entt::entity turtle = reg.create();
    reg.emplace<sm::ecs::Health>(turtle, 100.0f, 100.0f);
    reg.emplace<sm::ecs::NPCKind>(
        turtle, std::uint16_t(sm::NPCType::Guard), std::uint16_t{0});
    const DamageResult tink =
        apply_damage(reg, turtle, DamageSource{}, float(armour),
                     DamageKind::Melee, sm::DamageType::Blunt, &bus);
    CHECK(tink.applied == 0.0f,
          "a blow the plate outweighs never lands — 100% reduction is real");
    CHECK(!reg.any_of<sm::ecs::HitFlash>(turtle),
          "and a fully blocked blow stamps nothing, like any no-op");

    // ...and whether armour is in the way at all is the KIND's column.
    const entt::entity falling = reg.create();
    reg.emplace<sm::ecs::Health>(falling, 100.0f, 100.0f);
    reg.emplace<sm::ecs::NPCKind>(
        falling, std::uint16_t(sm::NPCType::Guard), std::uint16_t{0});
    const DamageResult fell =
        apply_damage(reg, falling, DamageSource{}, blow, DamageKind::Fall, sm::DamageType::Blunt, &bus);
    CHECK(fell.applied == blow,
          "plate does not soften the ground: the fall row says armour is not "
          "in the way, and that is DATA, not an `if` in the door");
}

// THE hybrid law's own shape (macro/damage_types.h) — properties, not a
// recomputation of the formula (testing law #5): each claim can break alone.
void test_mitigation_law_shape() {
    int probes = 0, wrong = 0;
    // Threshold regime: everything up to the armour itself is a full block.
    for (int dmg = 0; dmg <= 10; ++dmg) {
        ++probes;
        if (sm::mitigate_amount(dmg, 10) != 0) ++wrong;
    }
    // Percent regime: past the crossover (dmg > A + kArmorHalving) the flat
    // cut is UNDER the percent cut, so more damage must get through than the
    // flat branch alone would allow, and the kept share must shrink below
    // the raw blow — both branches visibly at work.
    for (int dmg = 21; dmg <= 200; dmg += 20) {
        ++probes;
        const int kept = sm::mitigate_amount(dmg, 10);
        if (!(kept > 0 && kept < dmg - 10 + 1 && kept <= dmg)) ++wrong;
    }
    // Monotone in armour: more plate never lets MORE through.
    for (int a = 0; a < 40; ++a) {
        ++probes;
        if (sm::mitigate_amount(50, a + 1) > sm::mitigate_amount(50, a))
            ++wrong;
    }
    // Armour 0 is the identity — the limiting case, not a branch.
    ++probes;
    if (sm::mitigate_amount(37, 0) != 37) ++wrong;
    CHECK(probes == 61 && wrong == 0,
          "the hybrid law: full block under the threshold, softening past "
          "the crossover, monotone in armour, identity at zero");
}

void test_survivor_protocol() {
    entt::registry reg;
    sm::EventBus bus;
    const entt::entity e = make_body(reg, 30.0f);
    const DamageResult hit = apply_damage(reg, e, DamageSource{7u, true},
                                          10.0f, DamageKind::Melee, sm::DamageType::Blunt, &bus);
    CHECK(hit.applied == 10.0f,
          "a body in its own skin keeps the whole blow: armour 0 is the "
          "limiting case of the law, applied == asked to the bit");
    CHECK(!hit.lethal, "a survivable blow is not lethal");
    CHECK(reg.get<sm::ecs::Health>(e).hp == 20.0f,
          "hp drops by exactly the applied amount");
    CHECK(!reg.any_of<sm::ecs::Dead>(e), "a survivor is not Dead");
    CHECK(death_events(bus) == 0, "a survivor emits nothing");
    CHECK(reg.all_of<sm::ecs::HitFlash>(e) && reg.all_of<sm::ecs::DamageFx>(e),
          "HitFlash and DamageFx travel together on every hit");
    CHECK(!reg.get<sm::ecs::DamageFx>(e).lethal,
          "a survivable blow's DamageFx is not lethal");
    CHECK(reg.get<sm::ecs::LastHit>(e).playerOwned,
          "LastHit carries playerOwned for the reaper's XP");
}

// A dead player is a game-over, not an NPC kill — from EVERY weapon. The
// spell path used to miss this guard.
void test_player_death_is_not_an_npc_kill() {
    const DamageKind kinds[] = {DamageKind::Melee, DamageKind::Spell,
                                DamageKind::Fall, DamageKind::Dev};
    for (const DamageKind kind : kinds) {
        entt::registry reg;
        sm::EventBus bus;
        const entt::entity e = make_body(reg, 5.0f);
        reg.emplace<sm::ecs::PlayerTag>(e);
        const DamageResult hit =
            apply_damage(reg, e, DamageSource{3u, false}, 50.0f, kind,
                         sm::DamageType::Blunt, &bus);
        CHECK(hit.lethal, "the player body does die");
        CHECK(reg.all_of<sm::ecs::Dead>(e),
              "Dead is stamped so the reconcile sees the death");
        CHECK(death_events(bus) == 0,
              "no NpcDeath for a player death, whatever the weapon");
    }
}

// The spell path used to stay silent for a body with no NPCKind while every
// other weapon reported kNoNpcType. One door, one answer.
void test_kindless_body_still_reports() {
    entt::registry reg;
    sm::EventBus bus;
    const entt::entity e = make_body(reg, 5.0f, /*withKind=*/false);
    apply_damage(reg, e, DamageSource{1u, false, 33u}, 50.0f,
                 DamageKind::Spell, sm::DamageType::Blunt, &bus);
    CHECK(death_events(bus) == 1, "a kindless death still emits");
    if (const sm::GameEvent* ev = last_death(bus)) {
        CHECK(ev->ix == sm::kNoNpcType,
              "a body with no NPCKind reports kNoNpcType, not a plausible 0");
    }
}

// The ONE already-dead guard: a corpse takes no second blow.
void test_no_second_blow() {
    entt::registry reg;
    sm::EventBus bus;
    const entt::entity e = make_body(reg, 10.0f);
    apply_damage(reg, e, DamageSource{1u, false}, 50.0f, DamageKind::Melee, sm::DamageType::Blunt,
                 &bus);
    const float hpAfterDeath = reg.get<sm::ecs::Health>(e).hp;
    const DamageResult again = apply_damage(reg, e, DamageSource{2u, false},
                                            50.0f, DamageKind::Spell, sm::DamageType::Blunt, &bus);
    CHECK(again.applied == 0.0f, "a corpse takes no damage");
    CHECK(!again.lethal, "a no-op blow is not lethal");
    CHECK(reg.get<sm::ecs::Health>(e).hp == hpAfterDeath,
          "a corpse's hp does not move");
    CHECK(death_events(bus) == 1, "a corpse dies once — one event, ever");
    CHECK(reg.get<sm::ecs::LastHit>(e).attackerId == 1u,
          "the kill stays attributed to the killer, not the corpse-kicker");
}

void test_execution_helper() {
    entt::registry reg;
    sm::EventBus bus;
    const entt::entity e = make_body(reg, 37.5f);
    const DamageResult hit = apply_lethal_damage(
        reg, e, DamageSource{0u, true}, DamageKind::Dev, &bus);
    CHECK(hit.lethal, "an execution is lethal by construction");
    CHECK(hit.applied == 37.5f, "an execution strikes exactly remaining hp");
    CHECK(reg.get<sm::ecs::Health>(e).hp == 0.0f,
          "an execution lands the body at exactly zero");
    const DamageResult again = apply_lethal_damage(
        reg, e, DamageSource{0u, true}, DamageKind::Dev, &bus);
    CHECK(again.applied == 0.0f, "executing a corpse is a no-op");
    CHECK(death_events(bus) == 1, "one execution, one event");
}

void test_zero_and_missing_target() {
    entt::registry reg;
    sm::EventBus bus;
    const entt::entity e = make_body(reg, 10.0f);
    const DamageResult zero =
        apply_damage(reg, e, DamageSource{}, 0.0f, DamageKind::Melee, sm::DamageType::Blunt, &bus);
    CHECK(zero.applied == 0.0f, "a zero blow is a no-op");
    CHECK(!reg.any_of<sm::ecs::HitFlash>(e),
          "a no-op stamps nothing — zero is a silent contribution");
    const entt::entity bare = reg.create();  // no Health at all
    const DamageResult none =
        apply_damage(reg, bare, DamageSource{}, 10.0f, DamageKind::Melee,
                     sm::DamageType::Blunt, &bus);
    CHECK(none.applied == 0.0f, "a body without Health cannot be struck");
}

} // namespace

int main() {
    test_death_is_indistinguishable();
    test_armour_softens_by_the_row_and_the_kind();
    test_mitigation_law_shape();
    test_survivor_protocol();
    test_player_death_is_not_an_npc_kill();
    test_kindless_body_still_reports();
    test_no_second_blow();
    test_execution_helper();
    test_zero_and_missing_target();
    return sm::test::report("damage_door_test");
}

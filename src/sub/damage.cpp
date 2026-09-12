#include "sub/damage.h"
#include "sub/record.h"   // pools_of — a blow lands on the RECORD, not on a copy

#include "ecs/components.h"
#include "macro/npc.h"
#include "macro/anatomy.h"
#include <algorithm>
#include <cmath>
#include "events/event_bus.h"
#include "events/event_types.h"

namespace sm::sub {

namespace {

// WHAT STANDS BETWEEN THIS BODY AND A BLOW.
//
// The idiom is the body radius's: an INSTANCE answers if it can, otherwise the
// ROW does, and there is one reader either way. The crowd's armour is a number
// on its creature row (owner's ruling: «броня массовки = ЧИСЛО ИЗ СТРОКИ») —
// a troll's hide and a guard's plate are what those rows ARE, and sixteen
// thousand equipment containers saying so would be one fact stored ten
// thousand times. A body that also WEARS things adds them on top; that sum is
// one line here when the equipment component lands, and no damage site
// changes to gain it.
int defense_of(entt::registry& reg, entt::entity target, DamageType type) {
    int armour = 0;
    if (const auto* kind = reg.try_get<ecs::NPCKind>(target)) {
        if (kind->type < std::uint16_t(NPCType::Count)) {
            armour = npc_def(NPCType(std::uint8_t(kind->type))).armor.of(type);
        }
    }
    // ...and what it WEARS, asked through THE door (sub/record.h): gear is the
    // RECORD's state, and a body wears what its record wears. A body with no
    // gear anywhere stays the limiting case rather than a branch — the same
    // sentence armour 0 already was.
    //
    // Two branches used to say this, and they said it twice: «what the body
    // carries», plus a player-only arm that walked to his squad entity because
    // «his gear is macro state, read where it lives». That arm had the law
    // right and the shape wrong — under the mirror it is not his exception, it
    // is everyone's rule, so it collapses into the line above.
    if (const auto* eq = state_of<ecs::BodyEquipment>(reg, target)) {
        // Two contributions from the same gear, one law point: the rows'
        // authored columns (worn_armor) and the instances' Armor-target
        // bonus rows (bonus.h affix tail) — a rolled "+3 Fire Armor" lands
        // here and nowhere else, so it cannot be counted twice.
        armour += worn_armor(eq->gear).of(type)
                + int(worn_bonuses(eq->gear).armor[std::size_t(type)]);
    }
    return std::max(0, armour);
}

// Mitigation, second step inside the door.
//
// THE LAW is mitigate_amount (macro/damage_types.h): the hybrid — armour cuts
// the larger of itself (a blow no bigger than the plate finds no flesh) and
// the halving fraction (a big blow is softened, never zeroed). The armour
// NUMBER is the column of the blow's own type: nine damage types against
// nine armour columns, and the meeting point is this one call.
//
// Whether armour is even in the way is the damage KIND's column, not an `if`
// here: plate does not soften a fall, and a scripted settlement must not be
// argued with by a breastplate.
//
int mitigate(entt::registry& reg, entt::entity target, int amount,
             DamageKind kind, DamageType type) {
    const DamageKindRow& row = kDamageKinds[std::size_t(kind)];
    if (!row.armourApplies) return amount;
    const int armour = defense_of(reg, target, type);
    if (armour <= 0) return amount;
    return mitigate_amount(amount, armour);
}

} // namespace

DamageResult apply_damage(entt::registry& reg, entt::entity target,
                          const DamageSource& src, int amount,
                          DamageKind kind, DamageType type, EventBus* bus) {
    DamageResult out{};
    if (!reg.valid(target)) return out;
    // THE bar this blow spends is the RECORD's (mirror law, sub/record.h): a
    // projected lord's wound is that lord's wound the instant it lands, and a
    // hit on the player lands on the store that drives his death screen. The
    // per-tick block on the body is a mirror of this one — it is what the eye
    // reads, never what the world remembers. This single substitution is what
    // retired the fold-up: there is no longer a second number to carry up, and
    // no fraction to convert between two bars that were sized by two sheets.
    auto* hp = pools_of(reg, target);
    if (hp == nullptr || hp->hp <= 0) return out;
    // A crit found the armour gap: mitigation is not in the way, exactly as
    // the Fall row's column says plate is not in the way of the ground.
    const int amt = src.critical
                        ? amount
                        : mitigate(reg, target, amount, kind, type);
    if (amt <= 0) {
        // BLOCKED, not silent (owner 2026-09-06: «пусть пишет всё равно»).
        // A real blow the armour swallowed whole is a fact the world shows:
        // the same flash + fx pair as a wound, with the blocked flag riding
        // DamageFx so the drain throws a spark off the plate instead of
        // blood. Nothing happened to the BODY — no Health change, no LastHit,
        // no event — only to the armour, so the protocol below is not walked.
        if (amount > 0) {
            reg.emplace_or_replace<ecs::HitFlash>(
                target, ecs::HitFlash{kHitFlashDuration});
            reg.emplace_or_replace<ecs::DamageFx>(
                target, ecs::DamageFx{false, true});
            out.blocked = true;
        }
        return out;
    }

    hp->hp -= amt;
    out.applied = amt;
    out.lethal = hp->hp <= 0;

    const DamageKindRow& row = kDamageKinds[std::size_t(kind)];
    if (row.attributesKiller) {
        reg.emplace_or_replace<ecs::LastHit>(target, src.attackerId);
    }
    reg.emplace_or_replace<ecs::HitFlash>(target,
                                          ecs::HitFlash{kHitFlashDuration});
    reg.emplace_or_replace<ecs::DamageFx>(target,
                                          ecs::DamageFx{out.lethal, false});

    if (out.lethal && !reg.any_of<ecs::Dead>(target)) {
        reg.emplace<ecs::Dead>(target);
        if (bus != nullptr && !reg.any_of<ecs::AvatarTag>(target)) {
            GameEvent ev{EventTag::NpcDeath};
            ev.a = std::uint32_t(entt::to_integral(target));
            ev.b = src.attackerId;
            const auto* kindRow = reg.try_get<ecs::NPCKind>(target);
            ev.ix = kindRow ? int(kindRow->type) : kNoNpcType;
            ev.iy = int(src.spellId);
            bus->emit(ev);
        }
    }
    return out;
}

DamageResult apply_lethal_damage(entt::registry& reg, entt::entity target,
                                 const DamageSource& src, DamageKind kind,
                                 EventBus* bus) {
    const auto* hp = pools_of(reg, target);
    if (hp == nullptr || hp->hp <= 0) return {};
    // The bar is integer now (4г), so "everything it has left" needs no ceil
    // — the whole remaining number is exactly one lethal blow.
    return apply_damage(reg, target, src, hp->hp, kind,
                        DamageType::Blunt, bus);
}

} // namespace sm::sub

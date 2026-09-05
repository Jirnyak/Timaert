#include "sub/damage.h"

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
    // ...and what it WEARS, for the few bodies that wear anything. This is the
    // one line the socket was waiting for: no damage site changed to gain it,
    // and a body with no BodyEquipment is the limiting case rather than a
    // branch — which is the same sentence armour 0 already was.
    if (const auto* eq = reg.try_get<ecs::BodyEquipment>(target)) {
        armour += worn_armor(eq->gear).of(type);
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
    auto* hp = reg.try_get<ecs::Health>(target);
    if (hp == nullptr || hp->hp <= 0.0f) return out;
    // A crit found the armour gap: mitigation is not in the way, exactly as
    // the Fall row's column says plate is not in the way of the ground.
    const int amt = src.critical
                        ? amount
                        : mitigate(reg, target, amount, kind, type);
    if (amt <= 0) return out;

    hp->hp -= float(amt);
    out.applied = amt;
    out.lethal = hp->hp <= 0.0f;

    const DamageKindRow& row = kDamageKinds[std::size_t(kind)];
    if (row.attributesKiller) {
        reg.emplace_or_replace<ecs::LastHit>(target, src.attackerId,
                                             src.playerOwned);
    }
    reg.emplace_or_replace<ecs::HitFlash>(target,
                                          ecs::HitFlash{kHitFlashDuration});
    reg.emplace_or_replace<ecs::DamageFx>(target, ecs::DamageFx{out.lethal});

    if (out.lethal && !reg.any_of<ecs::Dead>(target)) {
        reg.emplace<ecs::Dead>(target);
        if (bus != nullptr && !reg.any_of<ecs::PlayerTag>(target)) {
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
    const auto* hp = reg.try_get<ecs::Health>(target);
    if (hp == nullptr || hp->hp <= 0.0f) return {};
    // ceil: a fractional bar (float storage, integer writers) still dies to
    // one blow — overkill by under a point, never a survivor.
    return apply_damage(reg, target, src, int(std::ceil(hp->hp)), kind,
                        DamageType::Blunt, bus);
}

} // namespace sm::sub

#include "sub/damage.h"

#include "ecs/components.h"
#include "macro/npc.h"
#include <algorithm>
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
int defense_of(entt::registry& reg, entt::entity target) {
    const auto* kind = reg.try_get<ecs::NPCKind>(target);
    if (!kind || kind->type >= std::uint16_t(NPCType::Count)) return 0;
    return std::max(0, npc_def(NPCType(std::uint8_t(kind->type))).armor);
}

// Mitigation, second step inside the door.
//
// THE LAW: a blow keeps the fraction kArmorHalving / (kArmorHalving + armour).
// Asymptotic on purpose — armour SOFTENS, it never makes a body immune, so no
// amount of plate turns a fight into an impossibility and there is no
// threshold anywhere for a designer to fall off. A creature with no defences
// is the limiting case (armour 0 keeps everything), not a branch around the
// law.
//
// Whether armour is even in the way is the damage KIND's column, not an `if`
// here: plate does not soften a fall, and a scripted settlement must not be
// argued with by a breastplate.
float mitigate(entt::registry& reg, entt::entity target, float amount,
               DamageKind kind) {
    const DamageKindRow& row = kDamageKinds[std::size_t(kind)];
    if (!row.armourApplies) return amount;
    const int armour = defense_of(reg, target);
    if (armour <= 0) return amount;
    return amount * (kArmorHalving / (kArmorHalving + float(armour)));
}

} // namespace

DamageResult apply_damage(entt::registry& reg, entt::entity target,
                          const DamageSource& src, float amount,
                          DamageKind kind, EventBus* bus) {
    DamageResult out{};
    if (!reg.valid(target)) return out;
    auto* hp = reg.try_get<ecs::Health>(target);
    if (hp == nullptr || hp->hp <= 0.0f) return out;
    const float amt = mitigate(reg, target, amount, kind);
    if (amt <= 0.0f) return out;

    hp->hp -= amt;
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
    return apply_damage(reg, target, src, hp->hp, kind, bus);
}

} // namespace sm::sub

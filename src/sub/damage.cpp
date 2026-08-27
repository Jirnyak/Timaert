#include "sub/damage.h"

#include "ecs/components.h"
#include "events/event_bus.h"
#include "events/event_types.h"

namespace sm::sub {

namespace {

// Mitigation, second step inside the door. amount' = f(amount, target, kind);
// for a creature with no defences f is the identity — the limiting case of the
// law, not a branch. When equipment lands (work_vector §5), armour becomes
// columns this function reads; no damage site changes.
float mitigate(entt::registry& reg, entt::entity target, float amount,
               DamageKind kind) {
    (void)reg;
    (void)target;
    (void)kind;
    return amount;
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

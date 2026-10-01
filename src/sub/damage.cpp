#include "sub/damage.h"
#include "sub/record.h"   // pools_of — a blow lands on the RECORD, not on a copy

#include "ecs/components.h"
#include "tables/npc.h"
#include <algorithm>
#include <cmath>
#include "events/event_bus.h"
#include "events/event_types.h"
#include "macro/store.h"

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
DefenseSum defense_of(entt::registry& reg, entt::entity target,
                      DamageType type) {
    // WHAT HE WAS TRAINED TO WEAR (CANON S14). The sheet is the body's own —
    // through THE door (state_of), like every other piece of its state — and a
    // body without one (a prop, a headless test fixture) answers with the
    // untrained ×1 the law already gave everyone.
    static const Skills kUntrained{};
    const Skills& skills = [&]() -> const Skills& {
        if (const auto* cs = state_of<CharacterSheet>(reg, target))
            return cs->skills;
        return kUntrained;
    }();
    // The ROW's own defence — hide, scale, issued plate. All-zero for a body
    // whose row omits it, which is a body in its own skin, not a branch.
    Defense row{};
    if (const auto* kind = reg.try_get<ecs::NPCKind>(target)) {
        if (kind->type < std::uint16_t(NPCType::Count))
            row = npc_def(NPCType(std::uint8_t(kind->type))).defense;
    }
    // ...and what it WEARS, asked through THE door (sub/record.h): gear is the
    // RECORD's state, and a body wears what its record wears. Both nulls is the
    // limiting case (a bare crowd body), not a branch — and it is exactly the
    // case the «Без брони» rank speaks in.
    const auto* wornBag = state_of<ecs::NpcInventory>(reg, target);
    const auto* eq = state_of<ecs::BodyEquipment>(reg, target);
    const bool dressed = eq != nullptr && wornBag != nullptr;
    // ONE assembly for both laws of battle (macro/anketa.h body_defense): the
    // fought body and the auto-resolve read the SAME sentence. Нет ни клампа в
    // ноль, ни раннего выхода: ОТРИЦАТЕЛЬНАЯ броня есть УЯЗВИМОСТЬ и обязана
    // дойти до закона живой (CANON S13) — прежний `std::max(0, armour)` делал
    // её невыразимой в мире.
    return body_defense(row, skills, dressed ? &eq->gear : nullptr,
                        dressed ? &wornBag->inv : nullptr, type);
}

// Mitigation, second step inside the door.
//
// THE LAW is mitigate_amount (tables/damage_types.h): the flat BLOCK comes off
// first and never goes offline, then what is left argues with the PERCENT
// armour — one expression, no branch on the sign, vulnerability included. Both
// NUMBERS are the columns of the blow's own type: eight damage types against
// eight pairs of defence columns, and the meeting point is this one call.
//
// Whether armour is even in the way is the damage KIND's column, not an `if`
// here: plate does not soften a fall, and a scripted settlement must not be
// argued with by a breastplate.
//
int mitigate(entt::registry& reg, entt::entity target, int amount,
             DamageKind kind, DamageType type) {
    const DamageKindRow& row = kDamageKinds[std::size_t(kind)];
    if (!row.armourApplies) return amount;
    const DefenseSum d = defense_of(reg, target, type);
    return mitigate_amount(amount, d.armor, d.block);
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
        // Смерть тела сцены — тег Dead (кластер 4/7); запись-макро судит
        // свой байт судьбы своим путём (пулы записи, жнец).
        reg.emplace_or_replace<ecs::Dead>(target);
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

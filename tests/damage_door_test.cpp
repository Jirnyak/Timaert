// THE damage door (sub/damage.h) — the law that every weapon strikes through
// one function, and death from any of them is indistinguishable by protocol.
//
// What is pinned, with negative controls:
//   * one protocol: any lethal blow leaves the same component set (Dead +
//     DamageFx{lethal} + HitFlash) and emits exactly one NpcDeath with the
//     right attribution (a = victim, b = attacker, ix = kind, iy = spellId);
//   * attribution is DATA: the Fall/Script rows stamp no LastHit (nobody gets
//     XP for gravity), the Melee/Spell/Dev rows do;
//   * the ONE AvatarTag guard: a dead player body emits no NpcDeath from ANY kind —
//     the spell path used to miss this guard and count a player death toward
//     quest kill-tallies;
//   * a kindless body still emits (ix = kNoNpcType) — the spell path used to
//     stay silent for it while every other weapon spoke;
//   * the ONE already-dead guard: a corpse takes no second blow, no matter the
//     weapon — the spell path used to have none;
//   * MITIGATION by the hybrid law (tables/damage_types.h): a body in its own
//     skin keeps the identity (armour 0 is the limiting case, not a branch),
//     an armoured row cuts the larger of its column's threshold or the
//     halving fraction — full block of a blow the plate outweighs is REAL
//     (owner verdict 2026-09-05) — and whether armour is in the way at all is
//     the damage KIND's column: plate does not soften a fall.

#include "check.h"
#include "sub/damage.h"
#include "tables/npc.h"
#include "ecs/components.h"
#include "events/event_bus.h"
#include "events/event_types.h"
#include "macro/store.h"

#include <cstdio>

namespace {

using sm::sub::apply_damage;
using sm::sub::apply_lethal_damage;
using sm::sub::DamageKind;
using sm::sub::DamageResult;
using sm::sub::DamageSource;

constexpr std::uint16_t kTestNpcType = 7;

entt::entity make_body(entt::registry& reg, int hp, bool withKind = true) {
    const entt::entity e = reg.create();
    reg.emplace<sm::ecs::Pools>(e, hp, hp);
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
    reg.emplace<sm::ecs::Pools>(plated, 100, 100);
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
    const sm::Defense& guard = sm::npc_def(sm::NPCType::Guard).defense;
    const float expect = float(sm::mitigate_amount(
        int(blow), guard.armor_of(sm::DamageType::Blunt),
        guard.block_of(sm::DamageType::Blunt)));
    CHECK(onPlate.applied == expect,
          "the door applies exactly the defence law of the blow's own columns");

    // The BLOCK column (owner verdict 2026-09-30, M-193 — the hybrid's old
    // threshold branch, promoted to a column of its own): a blow no bigger than
    // the plate's block finds no flesh at all — full block is real. And
    // since 2026-09-06 (owner: «пусть пишет всё равно») a block is NOT a
    // silent no-op: the flesh is untouched, but the world SHOWS the blow —
    // HitFlash + DamageFx{blocked} so the drain sparks off the plate instead
    // of bleeding, and the result says `blocked` so the striker can speak.
    // The silence here was the shipped «как будто не попадаю» feel: every
    // early-game fist swing against mail vanished without a trace.
    const entt::entity turtle = reg.create();
    reg.emplace<sm::ecs::Pools>(turtle, 100, 100);
    reg.emplace<sm::ecs::NPCKind>(
        turtle, std::uint16_t(sm::NPCType::Guard), std::uint16_t{0});
    const DamageResult tink =
        apply_damage(reg, turtle, DamageSource{},
                     float(guard.block_of(sm::DamageType::Blunt)),
                     DamageKind::Melee, sm::DamageType::Blunt, &bus);
    CHECK(guard.block_of(sm::DamageType::Blunt) > 0,
          "предусловие своё: у выданных лат строки стража колонка блока есть");
    CHECK(tink.applied == 0.0f,
          "a blow the plate's block eats never lands — 100% reduction is real");
    CHECK(tink.blocked && !tink.lethal,
          "and the result names it BLOCKED, distinct from a dead-target no-op");
    CHECK((*reg.try_get<sm::ecs::Pools>(turtle)).hp == 100,
          "the flesh under the plate is untouched");
    CHECK(reg.all_of<sm::ecs::HitFlash>(turtle)
              && reg.all_of<sm::ecs::DamageFx>(turtle),
          "a blocked blow still shows: HitFlash + DamageFx travel together");
    CHECK(reg.get<sm::ecs::DamageFx>(turtle).blocked
              && !reg.get<sm::ecs::DamageFx>(turtle).lethal,
          "and the fx is the spark flavour, not blood");
    CHECK(!reg.any_of<sm::ecs::LastHit>(turtle),
          "nothing happened to the BODY: no LastHit, no killer named");
    // Negative control for the flag itself: a blow that DOES wound is not
    // blocked — the two exits of the door stay distinguishable.
    CHECK(!onPlate.blocked && !onBare.blocked,
          "a landing blow never reads as blocked");

    // ...and whether armour is in the way at all is the KIND's column.
    const entt::entity falling = reg.create();
    reg.emplace<sm::ecs::Pools>(falling, 100, 100);
    reg.emplace<sm::ecs::NPCKind>(
        falling, std::uint16_t(sm::NPCType::Guard), std::uint16_t{0});
    const DamageResult fell =
        apply_damage(reg, falling, DamageSource{}, blow, DamageKind::Fall, sm::DamageType::Blunt, &bus);
    CHECK(fell.applied == blow,
          "plate does not soften the ground: the fall row says armour is not "
          "in the way, and that is DATA, not an `if` in the door");
}

// Phase 4б (owner verdict 2026-09-06): the player's worn armour protects him
// UNDERGROUND, read where it lives — the BodyEquipment on his macro squad
// entity («макро — это контекст для микромира», no projected copy). His
// subworld body carries the flag, not a wardrobe.
void test_players_worn_plate_stands_underground() {
    entt::registry reg;

    // His body down here: AvatarTag (the scene flag) and the BACKLINK to the
    // record it projects, which is how the engine builds it (mirror law,
    // sub/record.h) — no equipment of its own.
    //
    // The backlink is what this test used to do without: the door found his
    // gear by scanning the registry for a PlayerSquadTag, i.e. by knowing who
    // the player is. It asks an address now, so the fixture must give the body
    // the address the game gives it. Same claim, one less thing the damage door
    // has to know about.
    const entt::entity body = make_body(reg, 100.0f, /*withKind*/false);
    reg.emplace<sm::ecs::AvatarTag>(body);
    // His squad on the map: the gear's one home — СЛОТ STORE (шаг 2 1е:
    // запись есть хэндл по построению, entt-двойник фикстуре не нужен).
    // arm_leather is the phase's own promise made flesh — «+2 END» AND a
    // coat worth its column.
    auto store = sm::make_macro_store();
    reg.ctx().insert_or_assign(store.get());
    const sm::MacroHandle squad = sm::store_birth(*store);
    store->pools[squad.slot].hp = 100;
    store->pools[squad.slot].maxHp = 100;
    reg.emplace<sm::ecs::MacroOrigin>(body, squad);
    auto& eq = store->gear[squad.slot];
    auto& bag = store->inventory[squad.slot].inv;
    const int coatIdx = sm::item_index("arm_leather");
    CHECK_OR_RETURN(coatIdx >= 0, "the catalog knows the leather coat");
    // M-183: плащ лежит В ИНВЕНТАРЕ сквада, ячейка тела указывает на слот.
    sm::gear_init(eq.gear, sm::npc_def(sm::NPCType::Adventurer).slots);
    sm::ItemRef coat{};
    coat.def = std::uint16_t(coatIdx);
    coat.count = 1;
    CHECK_OR_RETURN(bag.add_ref(coat), "the squad bag takes the coat");
    int coatSlot = -1;
    for (int i = 0; i < sm::kMaxInventorySlots; ++i) {
        if (!bag.slots[std::size_t(i)].empty()
            && int(bag.slots[std::size_t(i)].def) == coatIdx) {
            coatSlot = i;
            break;
        }
    }
    CHECK_OR_RETURN(coatSlot >= 0 && sm::equip(eq.gear, bag, coatSlot) >= 0,
                    "and the body wears it by index");

    const sm::Defense& coatDef = sm::item_def_at(coatIdx)->defense;
    const int armour = coatDef.armor_of(sm::DamageType::Blunt);
    const int block  = coatDef.block_of(sm::DamageType::Blunt);
    CHECK_OR_RETURN(armour > 0 && block > 0, "and the coat is worth something");

    // A poke the coat's BLOCK column eats never reaches the flesh — three
    // cells of separation between the body hit and the entity wearing it. The
    // full block is the BLOCK column's job now: the percent armour column
    // alone never zeroes a blow (M-193 split the two jobs apart).
    const DamageResult tink =
        apply_damage(reg, body, DamageSource{}, float(block),
                     DamageKind::Melee, sm::DamageType::Blunt, nullptr);
    CHECK(tink.applied == 0,
          "the map-side coat blocks the dungeon-side poke in full");
    // ...and a big blow is softened by exactly THE law over the coat's columns.
    const DamageResult big =
        apply_damage(reg, body, DamageSource{}, 20,
                     DamageKind::Melee, sm::DamageType::Blunt, nullptr);
    CHECK(big.applied == sm::mitigate_amount(20, armour, block),
          "the worn columns meet the defence law like any other defence");

    // Negative control: an ordinary body beside the same squad wears nothing
    // of it — the read is keyed to the ADDRESS this body carries, not to
    // proximity and not to who the player is.
    const entt::entity bystander = make_body(reg, 100.0f, /*withKind*/false);
    const DamageResult bare =
        apply_damage(reg, bystander, DamageSource{}, 20,
                     DamageKind::Melee, sm::DamageType::Blunt, nullptr);
    CHECK(bare.applied == 20,
          "negative control: the player's coat covers the player alone");
}

// THE defence law's own shape (tables/damage_types.h) — properties, not a
// recomputation of the formula (testing law #5): each claim can break alone.
// Rewritten 2026-10-01 (M-193) because the law it guarded CHANGED: the hybrid's
// threshold branch became the BLOCK column, so «armour 10 eats a blow of 10»
// is no longer true and asserting it would be guarding a case, not a law.
void test_mitigation_law_shape() {
    int probes = 0, wrong = 0;
    // 1. БЛОК — плоский и полный: всё до B включительно не доходит до плоти,
    //    при любой броне. Это бывшая пороговая ветвь, ставшая колонкой.
    for (int dmg = 0; dmg <= 10; ++dmg) {
        ++probes;
        if (sm::mitigate_amount(dmg, 40, 10) != 0) ++wrong;
    }
    // 2. БРОНЯ — ТОЛЬКО ПРОЦЕНТ, и она НИКОГДА не обнуляет удар. Ровно это
    //    отличает новый закон от прежнего: без блока даже тяжёлая плита
    //    пропускает долю. (От 2: при dmg = 1 ноль даёт целочисленное
    //    усечение, а не закон.)
    for (int dmg = 2; dmg <= 200; dmg += 9) {
        ++probes;
        if (sm::mitigate_amount(dmg, 10, 0) <= 0) ++wrong;
    }
    // 3. ПОСЛЕДОВАТЕЛЬНОСТЬ (вердикт владельца «да давай последовательно»):
    //    блок вычитается ПЕРВЫМ, остаток идёт в процент — значит закон с
    //    блоком тождественно равен закону без блока от уменьшенного удара.
    for (int dmg = 11; dmg <= 120; dmg += 7) {
        ++probes;
        if (sm::mitigate_amount(dmg, 25, 10)
            != sm::mitigate_amount(dmg - 10, 25, 0)) ++wrong;
    }
    // 4. Монотонность по броне: больше плиты никогда не пропускает БОЛЬШЕ.
    for (int a = 0; a < 40; ++a) {
        ++probes;
        if (sm::mitigate_amount(50, a + 1, 0) > sm::mitigate_amount(50, a, 0))
            ++wrong;
    }
    // 5. Ноль — ЗНАЧЕНИЕ, а не ветка: защита 0/0 есть тождество.
    ++probes;
    if (sm::mitigate_amount(37, 0, 0) != 37) ++wrong;
    // 6. УЯЗВИМОСТЬ: отрицательная броня УСИЛИВАЕТ удар, монотонно, и кап
    //    ровно ×2 — асимптота, которую владелец назвал «удвоение урона».
    ++probes;
    if (!(sm::mitigate_amount(100, -1, 0) > 100)) ++wrong;
    for (int a = -126; a < 0; ++a) {
        ++probes;
        if (sm::mitigate_amount(100, a, 0) < sm::mitigate_amount(100, a + 1, 0))
            ++wrong;
    }
    ++probes;
    const int worst = sm::mitigate_amount(100, -127, 0);
    if (!(worst == 192 && worst < 200)) ++wrong;
    // 7. ГЛАДКОСТЬ В НУЛЕ — не фигура речи: обе половины сходятся в ×1 и в
    //    одной производной (−0.1 на пункт брони), поэтому шага на переходе
    //    защиты в уязвимость нет. Мера на крупном ударе, где целочисленное
    //    усечение не глушит разницу: шаг вверх и шаг вниз от нуля обязаны
    //    быть ОДНОЙ величины.
    //    ДОПУСК РОВНО ОДНА ЕДИНИЦА, И ЭТО НЕ ПОСЛАБЛЕНИЕ, А АРИФМЕТИКА:
    //    математически оба шага равны dmg/(d+1) = 1000/11 = 90.909, но закон
    //    целочислен и УСЕКАЕТ — вверх получается floor(1090.9) − 1000 = 90, вниз
    //    1000 − floor(909.09) = 91. Требовать здесь точного равенства значит
    //    требовать от целой арифметики того, чего она не умеет; гладкость
    //    закона этим не нарушена, а шаг в ДВЕ единицы её бы уже нарушил.
    ++probes;
    const int at0 = sm::mitigate_amount(1000, 0, 0);
    const int up  = sm::mitigate_amount(1000, -1, 0) - at0;
    const int dn  = at0 - sm::mitigate_amount(1000, 1, 0);
    if (!(up > 0 && dn > 0 && (up - dn <= 1) && (dn - up <= 1))) ++wrong;
    CHECK(probes == 220 && wrong == 0,
          "закон защиты: блок плоский и полный, броня только процентная и "
          "никогда не обнуляет, порядок последователен, монотонность по "
          "броне, тождество в нуле, уязвимость с капом ×2, гладкость в нуле");

    // АБСОЛЮТНОЙ НЕУЯЗВИМОСТИ НЕ СУЩЕСТВУЕТ — обещание CANON S13 вслух:
    // «при ранге 100 и лучшем снаряжении каталога удар в 1000 обязан
    // ПРОХОДИТЬ». Мера берётся на ПРЕДЕЛАХ ОБОИХ типов, а не на сегодняшнем
    // контенте: броня на верхушке `int8` и блок на верхушке `uint8` — выше
    // авторской колонке подняться некуда по построению.
    CHECK(sm::mitigate_amount(1000, 127, 255) > 0,
          "неуязвимости нет: даже на пределах обеих колонок крупный удар "
          "проходит — ни процент, ни плоский блок стеной не становятся");
    // ...и это свойство ЗАКОНА, а не калибровки: процентная колонка не
    // обнуляет удар ни при каком значении, какое вмещает её тип.
    {
        int zeroed = 0;
        for (int a = 0; a <= 127; ++a)
            if (sm::mitigate_amount(1000, a, 0) <= 0) ++zeroed;
        CHECK(zeroed == 0,
              "ни одно значение колонки брони не обнуляет крупный удар");
    }

    // НЕГАТИВНЫЙ КОНТРОЛЬ, который обязан падать, если вернуть старый закон:
    // гибрид срезал max(A, процент), то есть удар РОВНО в броню уходил в ноль
    // при пустом блоке. Новый закон обязан пропускать долю.
    CHECK(sm::mitigate_amount(10, 10, 0) > 0,
          "негативный контроль: возврат пороговой ветви в колонку брони "
          "(max(A, процент)) обнулил бы этот удар");
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
    CHECK((*reg.try_get<sm::ecs::Pools>(e)).hp == 20.0f,
          "hp drops by exactly the applied amount");
    CHECK(!reg.any_of<sm::ecs::Dead>(e), "a survivor is not Dead");
    CHECK(death_events(bus) == 0, "a survivor emits nothing");
    CHECK(reg.all_of<sm::ecs::HitFlash>(e) && reg.all_of<sm::ecs::DamageFx>(e),
          "HitFlash and DamageFx travel together on every hit");
    CHECK(!reg.get<sm::ecs::DamageFx>(e).lethal,
          "a survivable blow's DamageFx is not lethal");
    CHECK(reg.get<sm::ecs::LastHit>(e).attackerId == 7u,
          "LastHit carries the killer's BODY — the reaper resolves its "
          "leader through the one kill-XP door (§41 root 5)");
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
        reg.emplace<sm::ecs::AvatarTag>(e);
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
    const float hpAfterDeath = (*reg.try_get<sm::ecs::Pools>(e)).hp;
    const DamageResult again = apply_damage(reg, e, DamageSource{2u, false},
                                            50.0f, DamageKind::Spell, sm::DamageType::Blunt, &bus);
    CHECK(again.applied == 0.0f, "a corpse takes no damage");
    CHECK(!again.lethal, "a no-op blow is not lethal");
    CHECK((*reg.try_get<sm::ecs::Pools>(e)).hp == hpAfterDeath,
          "a corpse's hp does not move");
    CHECK(death_events(bus) == 1, "a corpse dies once — one event, ever");
    CHECK(reg.get<sm::ecs::LastHit>(e).attackerId == 1u,
          "the kill stays attributed to the killer, not the corpse-kicker");
}

void test_execution_helper() {
    entt::registry reg;
    sm::EventBus bus;
    // Whole hp: every combat writer is integer now (phase 3), so a whole bar
    // is the world's own case. The fractional-bar edge (ceil = overkill by
    // under a point, still one blow) is asserted separately below.
    const entt::entity e = make_body(reg, 37.0f);
    const DamageResult hit = apply_lethal_damage(
        reg, e, DamageSource{0u, true}, DamageKind::Dev, &bus);
    CHECK(hit.lethal, "an execution is lethal by construction");
    CHECK(hit.applied == 37, "an execution strikes exactly remaining hp");
    CHECK((*reg.try_get<sm::ecs::Pools>(e)).hp == 0.0f,
          "an execution lands the body at exactly zero");
    const DamageResult again = apply_lethal_damage(
        reg, e, DamageSource{0u, true}, DamageKind::Dev, &bus);
    CHECK(again.applied == 0, "executing a corpse is a no-op");
    CHECK(death_events(bus) == 1, "one execution, one event");

    // The bar is INTEGER now (4г): the execution blow is exactly what is
    // left, and one blow is always enough — no ceil, no survivor.
    const entt::entity odd = make_body(reg, 13);
    const DamageResult oddHit = apply_lethal_damage(
        reg, odd, DamageSource{0u, true}, DamageKind::Dev, &bus);
    CHECK(oddHit.lethal && oddHit.applied == 13,
          "the whole remaining bar is one lethal blow, never a survivor");
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

// THE BLOW LANDS ON THE RECORD, NOT ON THE MIRROR (owner's form 2026-09-12,
// «ЗЕРКАЛО ДЛЯ ВСЕХ» — sub/record.h).
//
// A body in the subworld owns nothing: the bar it spends belongs to the macro
// record it projects. This is the substitution that retired the fold-up, so it
// is worth an assertion that can genuinely come out false — and the negative
// control is the same body with its backlink removed, which must then spend its
// own block.
//
// Deliberately NOT asserted here: that the mirror follows. That is the engine's
// tick-top pass (mirror_bodies_from_record), and this door links no engine.
void test_the_blow_lands_on_the_record() {
    entt::registry reg;
    sm::EventBus bus;

    // Запись — СЛОТ STORE (шаг 2 1е): фикстура рожает слот, как рожает мир.
    auto store = sm::make_macro_store();
    reg.ctx().insert_or_assign(store.get());
    const sm::MacroHandle record = sm::store_birth(*store);
    store->pools[record.slot].hp = 100;
    store->pools[record.slot].maxHp = 100;
    const entt::entity body = make_body(reg, 100);
    reg.emplace<sm::ecs::MacroOrigin>(body, record);

    const DamageResult hit =
        apply_damage(reg, body, DamageSource{}, 30, DamageKind::Script,
                     sm::DamageType::Blunt, &bus);
    CHECK(hit.applied == 30, "the blow landed");
    CHECK((*sm::body_state<sm::ecs::Pools>(*store, record)).hp == 70,
          "a projected body's wound is its RECORD's wound, in the tick it "
          "lands — there is nothing left to fold up");
    CHECK((*reg.try_get<sm::ecs::Pools>(body)).hp == 100,
          "...and the body's own block is untouched: it is the scene's copy, "
          "not a second memory the world must reconcile");

    // The protocol still stamps the BODY — the flash, the corpse tag and the
    // killer attribution describe the thing standing in the scene, which is
    // what the eye and the reaper look at. (Запись — слот store: entt-штампа
    // на ней не существует по построению, вторая половина старой проверки
    // умерла вместе с entt-записью.)
    CHECK(reg.any_of<sm::ecs::HitFlash>(body),
          "the visible protocol stamps the body, not the record");

    // NEGATIVE CONTROL: no backlink, no record — the very same call spends the
    // body's own bar. Without this, the assertion above could be passing for
    // any reason at all.
    const entt::entity orphan = make_body(reg, 100);
    apply_damage(reg, orphan, DamageSource{}, 30, DamageKind::Script,
                 sm::DamageType::Blunt, &bus);
    CHECK((*reg.try_get<sm::ecs::Pools>(orphan)).hp == 70,
          "a body nothing above remembers spends its own bar — the detector "
          "above reads a real difference");

    // A LETHAL blow judges by the record: the door must not read one bar and
    // kill by another. Three more Script blows of 30 leave the record at -20.
    for (int i = 0; i < 3; ++i) {
        apply_damage(reg, body, DamageSource{}, 30, DamageKind::Script,
                     sm::DamageType::Blunt, &bus);
    }
    CHECK(reg.any_of<sm::ecs::Dead>(body)
              && (*sm::body_state<sm::ecs::Pools>(*store, record)).hp <= 0,
          "lethality is judged on the record, and the corpse tag lands on the "
          "body that fell");
}

} // namespace

int main() {
    test_death_is_indistinguishable();
    test_armour_softens_by_the_row_and_the_kind();
    test_players_worn_plate_stands_underground();
    test_mitigation_law_shape();
    test_survivor_protocol();
    test_player_death_is_not_an_npc_kill();
    test_kindless_body_still_reports();
    test_no_second_blow();
    test_execution_helper();
    test_zero_and_missing_target();
    test_the_blow_lands_on_the_record();
    return sm::test::report("damage_door_test");
}

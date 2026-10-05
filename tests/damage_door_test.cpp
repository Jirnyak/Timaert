// THE damage door (sub/damage.h) — the law that every weapon strikes through
// one function, and death from any of them is indistinguishable by protocol.
//
// What is pinned, with negative controls:
//   * one protocol: any lethal blow leaves the same component set (Dead +
//     DamageFx{lethal}) and emits exactly one NpcDeath with the right
//     attribution (a = victim, b = attacker, ix = kind, iy = spellId);
//     (HitFlash снесена вердиктом 2026-10-05 — «удар виден» несёт DamageFx);
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
#include "scene_objects_fixture.h"   // арена объектов: fx — колонки (M-150 1а)
#include "sub/damage.h"
#include "sub/record.h"    // pools_of — удар ложится на ЗАПИСЬ
#include "sub/ability.h"   // tick_body_recovery — та же дверь слива (кусок 2)
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
    // Слот арены — как выдала бы дверь спавна (fx — колонки, M-150 1а).
    sm::test::give_slot(reg, e);
    sm::test::give_pools(reg, e, sm::ecs::Pools{hp, hp});
    // Род — колонка арены (кусок 1); без give_kind слот честно несёт
    // kObjNoKind — прежнее «тело без компоненты».
    if (withKind) sm::test::give_kind(
        reg, e, sm::ecs::NPCKind{kTestNpcType, std::uint16_t{0}});
    return e;
}

using sm::test::fx_of;
using sm::test::last_hit_of;

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
        CHECK(sm::test::flag_of(reg, e, sm::sub::kObjDead),
              "every kind stamps the Dead bit");
        CHECK((fx_of(reg, e) & sm::sub::kDmgFxPending) != 0,
              "every kind stamps DamageFx");
        CHECK((fx_of(reg, e) & sm::sub::kDmgFxLethal) != 0,
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
        CHECK((last_hit_of(reg, e) != sm::sub::kObjNoAttacker) == wantsKiller,
              "LastHit follows the kind row's attributesKiller column");
        if (wantsKiller) {
            CHECK(last_hit_of(reg, e) == 42u,
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
    sm::test::give_pools(reg, plated, sm::ecs::Pools{100, 100});
    sm::test::give_kind(
        reg, plated,
        sm::ecs::NPCKind{std::uint16_t(sm::NPCType::Guard), std::uint16_t{0}});

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
    // DamageFx{blocked} so the drain sparks off the plate instead of
    // bleeding, and the result says `blocked` so the striker can speak.
    // The silence here was the shipped «как будто не попадаю» feel: every
    // early-game fist swing against mail vanished without a trace.
    const entt::entity turtle = reg.create();
    sm::test::give_slot(reg, turtle);
    sm::test::give_pools(reg, turtle, sm::ecs::Pools{100, 100});
    sm::test::give_kind(
        reg, turtle,
        sm::ecs::NPCKind{std::uint16_t(sm::NPCType::Guard), std::uint16_t{0}});
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
    CHECK((*sm::sub::body_pools(reg, turtle)).hp == 100,
          "the flesh under the plate is untouched");
    CHECK((fx_of(reg, turtle) & sm::sub::kDmgFxPending) != 0,
          "a blocked blow still shows: DamageFx is stamped");
    CHECK((fx_of(reg, turtle) & sm::sub::kDmgFxBlocked) != 0
              && (fx_of(reg, turtle) & sm::sub::kDmgFxLethal) == 0,
          "and the fx is the spark flavour, not blood");
    CHECK(last_hit_of(reg, turtle) == sm::sub::kObjNoAttacker,
          "nothing happened to the BODY: no LastHit, no killer named");
    // Negative control for the flag itself: a blow that DOES wound is not
    // blocked — the two exits of the door stay distinguishable.
    CHECK(!onPlate.blocked && !onBare.blocked,
          "a landing blow never reads as blocked");

    // ...and whether armour is in the way at all is the KIND's column.
    const entt::entity falling = reg.create();
    sm::test::give_pools(reg, falling, sm::ecs::Pools{100, 100});
    sm::test::give_kind(
        reg, falling,
        sm::ecs::NPCKind{std::uint16_t(sm::NPCType::Guard), std::uint16_t{0}});
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
    sm::test::make_avatar(reg, body);
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

// ПРОСТОЙ БРОНИ — ВТОРОЙ СУБЪЕКТ ЗАКОНА ВОССТАНОВЛЕНИЯ (CANON S13, M-194).
// Свидетель рождает своё предусловие сам (§8 п.11): надевает телу вещь, у
// которой ЕСТЬ вес и ЕСТЬ обе колонки защиты, и только потом спрашивает.
void test_armor_downtime() {
    entt::registry reg;
    const entt::entity body = make_body(reg, 10000.0f, /*withKind*/false);
    sm::test::make_avatar(reg, body);
    sm::test::give_combat(reg, body, sm::ecs::Combat{});

    auto store = sm::make_macro_store();
    reg.ctx().insert_or_assign(store.get());
    const sm::MacroHandle squad = sm::store_birth(*store);
    store->pools[squad.slot].hp = 10000;
    store->pools[squad.slot].maxHp = 10000;
    reg.emplace<sm::ecs::MacroOrigin>(body, squad);
    auto& eq = store->gear[squad.slot];
    auto& bag = store->inventory[squad.slot].inv;
    const int coatIdx = sm::item_index("arm_leather");
    CHECK_OR_RETURN(coatIdx >= 0, "каталог знает кожаную куртку");
    sm::gear_init(eq.gear, sm::npc_def(sm::NPCType::Adventurer).slots);
    sm::ItemRef coat{};
    coat.def = std::uint16_t(coatIdx);
    coat.count = 1;
    CHECK_OR_RETURN(bag.add_ref(coat), "сумка сквада приняла её");
    int coatSlot = -1;
    for (int i = 0; i < sm::kMaxInventorySlots; ++i) {
        if (!bag.slots[std::size_t(i)].empty()
            && int(bag.slots[std::size_t(i)].def) == coatIdx) {
            coatSlot = i;
            break;
        }
    }
    CHECK_OR_RETURN(coatSlot >= 0 && sm::equip(eq.gear, bag, coatSlot) >= 0,
                    "и тело её надело");

    const sm::Defense& coatDef = sm::item_def_at(coatIdx)->defense;
    const int block = coatDef.block_of(sm::DamageType::Blunt);
    CHECK_OR_RETURN(block > 0 && sm::item_def_at(coatIdx)->weight > 0.0f,
                    "предусловие своё: у куртки есть и колонка блока, и ВЕС — "
                    "без веса простою не из чего взяться");

    auto& clock = *sm::sub::body_combat(reg, body);
    CHECK(clock.armorSteps == 0u, "броня рождается В СТРОЮ: простой есть факт "
                                  "удара, а не свойство рождения");

    // 1. ТЫЧКА, КОТОРУЮ СЪЕЛ БЛОК, НЕ СБИВАЕТ НИЧЕГО — ровно смысл второй
    //    колонки, названный владельцем: «чтобы слабые тычки не сбивали
    //    рековери». Это НЕГАТИВНЫЙ КОНТРОЛЬ всей механики: если бы простой
    //    ставил любой удар, он покраснел бы здесь.
    const DamageResult poke =
        apply_damage(reg, body, DamageSource{}, float(block),
                     DamageKind::Melee, sm::DamageType::Blunt, nullptr);
    CHECK(poke.applied == 0.0f && poke.blocked,
          "тычку в размер блока съел блок");
    CHECK(clock.armorSteps == 0u,
          "и броня осталась В СТРОЮ — блок простоя не вызывает НИКОГДА");

    // 2. УДАР СКВОЗЬ БЛОК ВЫБИВАЕТ БРОНЮ, и часы берутся от ВЕСА надетого.
    apply_damage(reg, body, DamageSource{}, 200.0f, DamageKind::Melee,
                 sm::DamageType::Blunt, nullptr);
    const std::uint16_t charged = clock.armorSteps;
    CHECK(charged > 0u, "удар сквозь блок выбил броню из строя");

    // 3. ПОКА ПРОСТОЙ ИДЁТ — ПРОЦЕНТ ВЫКЛЮЧЕН, А БЛОК В СТРОЮ. Мера: тот же
    //    удар проходит БОЛЬШЕ, но ровно на величину блока меньше сырого.
    const int hp0 = int(sm::sub::pools_of(reg, body)->hp);
    const DamageResult naked =
        apply_damage(reg, body, DamageSource{}, 200.0f, DamageKind::Melee,
                     sm::DamageType::Blunt, nullptr);
    CHECK(naked.applied == float(200 - block),
          "в простое проходит удар МИНУС блок: процентная колонка выключена, "
          "плоская осталась");
    CHECK(int(sm::sub::pools_of(reg, body)->hp) == hp0 - (200 - block),
          "и это легло на запись, а не на копию");

    // 4. УДАР ВО ВРЕМЯ ПРОСТОЯ ЕГО НЕ ПРОДЛЕВАЕТ (владелец: «НЕ перезаводить
    //    НО СТАВИТЬ ЕСЛИ БРОНЯ В СТРОЮ»). Иначе рой крыс держал бы рыцаря
    //    голым вечно.
    CHECK(clock.armorSteps == charged,
          "простой не перезаводится ударом по уже выбитой броне");

    // 5. ЧАСЫ СЛИВАЕТ ТА ЖЕ ДВЕРЬ, что гейт занятости тела — одна система
    //    восстановления, а не две.
    sm::ecs::World w{};
    w.reg.ctx().insert_or_assign(store.get());
    const entt::entity drained = w.reg.create();
    sm::test::give_pools(w.reg, drained, sm::ecs::Pools{100, 100});
    sm::ecs::Combat c{};
    c.armorSteps = 64;
    c.recoverySteps = 64u;
    sm::test::give_combat(w.reg, drained, c);
    sm::sub::tick_body_recovery(sm::test::arena_of(w.reg), 64u);
    const auto& after = *sm::sub::body_combat(w.reg, drained);
    CHECK(after.armorSteps == 0u && after.recoverySteps == 0u,
          "один tick_body_recovery сливает ОБА субъекта закона");

    // 6. У ТЕЛА БЕЗ НАДЕТОЙ БРОНИ ПРОСТОЯ НЕТ ВОВСЕ — вросшую шкуру строки
    //    существа не сбивают (владелец: «0 для строки существа»). Это не
    //    ветка в коде, а предельный случай: вес надетого нулевой, значит и
    //    база нулевая.
    const entt::entity hide = make_body(reg, 1000.0f, /*withKind*/true);
    sm::test::give_combat(reg, hide, sm::ecs::Combat{});
    apply_damage(reg, hide, DamageSource{}, 200.0f, DamageKind::Melee,
                 sm::DamageType::Blunt, nullptr);
    CHECK(sm::sub::body_combat(reg, hide)->armorSteps == 0u,
          "шкура строки существа простоя не знает: надетого веса ноль");
}

// THE defence law's own shape (tables/damage_types.h) — свойства, а не
// перевычисление формулы (§8 п.4-5): каждое утверждение падает отдельно.
// ПЕРЕПИСАН 2026-10-01 (M-197) ВТОРОЙ РАЗ ЗА ДЕНЬ, и оба раза потому, что менялся
// ЗАКОН, а не потому, что свидетель был неудобен: сперва порог стал колонкой
// блока, теперь гипербола стала процентом. Утверждения прежней редакции
// («иммунитета нет нигде») охраняли СЛУЧАЙ той формулы, и держать их значило бы
// держать мир на старом законе.
void test_mitigation_law_shape() {
    int probes = 0, wrong = 0;
    // 1. БЛОК — плоский и полный: всё до B включительно не доходит до плоти,
    //    при любой броне. Это бывшая пороговая ветвь, ставшая колонкой.
    for (int dmg = 0; dmg <= 10; ++dmg) {
        ++probes;
        if (sm::mitigate_amount(dmg, 40, 10) != 0) ++wrong;
    }
    // 2. ПОСЛЕДОВАТЕЛЬНОСТЬ (вердикт владельца «да давай последовательно»):
    //    блок вычитается ПЕРВЫМ, остаток идёт в процент — значит закон с блоком
    //    тождественен закону без блока от уменьшенного удара.
    for (int dmg = 11; dmg <= 1200; dmg += 7) {
        ++probes;
        if (sm::mitigate_amount(dmg, 25, 10)
            != sm::mitigate_amount(dmg - 10, 25, 0)) ++wrong;
    }
    // 3. МОНОТОННОСТЬ по броне на всём диапазоне типа, включая минус.
    for (int a = -127; a < 127; ++a) {
        ++probes;
        if (sm::mitigate_amount(1000, a + 1, 0)
            > sm::mitigate_amount(1000, a, 0)) ++wrong;
    }
    // 4. ВЕТКИ ПО ЗНАКУ НЕТ, и это проверяется ТОЧНЫМ равенством: проценты
    //    симметричных значений складываются в двести, значит и урон — в два
    //    удара. Дефект, завёвший для минуса отдельную формулу, покраснеет здесь.
    for (int a = 0; a <= 99; ++a) {
        ++probes;
        if (sm::mitigate_amount(1000, a, 0) + sm::mitigate_amount(1000, -a, 0)
            != 2000) ++wrong;
    }
    CHECK(probes > 0 && wrong == 0,
          "закон защиты: блок плоский и полный, порядок последователен, "
          "монотонность по всей оси, и ветки по знаку нет");

    // ТОЧНЫЕ ЗНАЧЕНИЯ ШКАЛЫ — то, за что процентная форма и выбрана: число ЕСТЬ
    // механика, поэтому каждое из этих утверждений читается без формулы.
    CHECK(sm::mitigate_amount(1000, 0, 0) == 1000,
          "ноль — ЗНАЧЕНИЕ: защита 0 есть тождество");
    CHECK(sm::mitigate_amount(1000, 50, 0) == 500,
          "броня 50 снимает ровно половину — число есть процент");
    CHECK(sm::mitigate_amount(1000, -100, 0) == 2000,
          "броня −100 ровно УДВАИВАЕТ урон: точное равенство, не асимптота");

    // ИММУНИТЕТ НАЧИНАЕТСЯ РОВНО НА `kArmorFull` И НИ ПУНКТОМ РАНЬШЕ — он
    // выпадает из шкалы, а не из сентинела и не из маски (вердикт владельца
    // 2026-10-01: «100% это и есть 100 а всё что выше это сверх»).
    CHECK(sm::mitigate_amount(100000, sm::kArmorFull, 0) == 0,
          "сто процентов есть «не берёт вовсе» — при любом размере удара");
    CHECK(sm::mitigate_amount(100000, 127, 0) == 0,
          "и ЗАПАС сверх ста остаётся иммунитетом: ему ещё предстоит служить "
          "магии снятия иммунитетов, вычитающей из этого запаса");
    // НЕГАТИВНЫЙ КОНТРОЛЬ, без которого утверждение выше ничего не значит: на
    // 99 процентах удар ОБЯЗАН проходить, иначе «ровно на сотне» не проверено.
    CHECK(sm::mitigate_amount(1000, sm::kArmorFull - 1, 0) == 10,
          "негативный контроль: 99 процентов пропускают ровно сотую — "
          "иммунитет не наступает раньше ста");

    // И СТРАЖ СКАЛЯРА: множитель эффективного HP у сотни упирается в единицу,
    // потому что бесконечность скаляр не выражает (auto_battle.h). Смещение
    // названо вслух там же; здесь — что деления на ноль не случится.
    CHECK(sm::armor_hp_mult_den(sm::kArmorFull) == 1
              && sm::armor_hp_mult_den(127) == 1,
          "знаменатель эффективного HP не обнуляется ни на сотне, ни за ней");
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
    CHECK((*sm::sub::body_pools(reg, e)).hp == 20.0f,
          "hp drops by exactly the applied amount");
    CHECK(!sm::test::flag_of(reg, e, sm::sub::kObjDead),
          "a survivor is not Dead");
    CHECK(death_events(bus) == 0, "a survivor emits nothing");
    CHECK((fx_of(reg, e) & sm::sub::kDmgFxPending) != 0,
          "DamageFx is stamped on every hit that lands");
    CHECK((fx_of(reg, e) & sm::sub::kDmgFxLethal) == 0,
          "a survivable blow's DamageFx is not lethal");
    CHECK(last_hit_of(reg, e) == 7u,
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
        sm::test::make_avatar(reg, e);
        const DamageResult hit =
            apply_damage(reg, e, DamageSource{3u, false}, 50.0f, kind,
                         sm::DamageType::Blunt, &bus);
        CHECK(hit.lethal, "the player body does die");
        CHECK(sm::test::flag_of(reg, e, sm::sub::kObjDead),
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
    const float hpAfterDeath = (*sm::sub::body_pools(reg, e)).hp;
    const DamageResult again = apply_damage(reg, e, DamageSource{2u, false},
                                            50.0f, DamageKind::Spell, sm::DamageType::Blunt, &bus);
    CHECK(again.applied == 0.0f, "a corpse takes no damage");
    CHECK(!again.lethal, "a no-op blow is not lethal");
    CHECK((*sm::sub::body_pools(reg, e)).hp == hpAfterDeath,
          "a corpse's hp does not move");
    CHECK(death_events(bus) == 1, "a corpse dies once — one event, ever");
    CHECK(last_hit_of(reg, e) == 1u,
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
    CHECK((*sm::sub::body_pools(reg, e)).hp == 0.0f,
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
    CHECK(fx_of(reg, e) == 0u,
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
    CHECK((*sm::sub::body_pools(reg, body)).hp == 100,
          "...and the body's own block is untouched: it is the scene's copy, "
          "not a second memory the world must reconcile");

    // The protocol still stamps the BODY — the fx event, the corpse tag and
    // the killer attribution describe the thing standing in the scene, which
    // is what the eye and the reaper look at. (Запись — слот store: entt-штампа
    // на ней не существует по построению, вторая половина старой проверки
    // умерла вместе с entt-записью.)
    CHECK((fx_of(reg, body) & sm::sub::kDmgFxPending) != 0,
          "the visible protocol stamps the body, not the record");

    // NEGATIVE CONTROL: no backlink, no record — the very same call spends the
    // body's own bar. Without this, the assertion above could be passing for
    // any reason at all.
    const entt::entity orphan = make_body(reg, 100);
    apply_damage(reg, orphan, DamageSource{}, 30, DamageKind::Script,
                 sm::DamageType::Blunt, &bus);
    CHECK((*sm::sub::body_pools(reg, orphan)).hp == 70,
          "a body nothing above remembers spends its own bar — the detector "
          "above reads a real difference");

    // A LETHAL blow judges by the record: the door must not read one bar and
    // kill by another. Three more Script blows of 30 leave the record at -20.
    for (int i = 0; i < 3; ++i) {
        apply_damage(reg, body, DamageSource{}, 30, DamageKind::Script,
                     sm::DamageType::Blunt, &bus);
    }
    CHECK(sm::test::flag_of(reg, body, sm::sub::kObjDead)
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
    test_armor_downtime();
    test_survivor_protocol();
    test_player_death_is_not_an_npc_kill();
    test_kindless_body_still_reports();
    test_no_second_blow();
    test_execution_helper();
    test_zero_and_missing_target();
    test_the_blow_lands_on_the_record();
    return sm::test::report("damage_door_test");
}

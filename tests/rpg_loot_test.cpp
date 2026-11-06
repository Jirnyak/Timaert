// Standalone unit tests for the shipped RPG spine + unified loot table.
//
// These cover pure, deterministic logic that previously had NO direct test:
//   • character_sheet.h  — project_combat() and make_character_sheet()
//   • items.cpp          — roll_loot_profile() (только вещи мира)
//
// Assertions go through tests/check.h (CHECK + sm::test::report).
// project_combat assertions re-derive their expected values from the SAME
// public formulas in attributes.h (calculate_combat_stats / calculate_derived)
// so the test pins the projection *contract* (melee→physical, missile→spell,
// HP floor, attack identity preserved) rather than hand-copied magic numbers.

#include "check.h"

#include "bus/anketa.h"
#include "tables/items.h"
#include "tables/npc.h"
#include "tables/progression.h"   // kMaxTier — лестница, у которой свидетель и спрашивает

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace sm;

static bool approx(float a, float b, float eps = 1e-3f) {
    return std::fabs(a - b) <= eps;
}

// ── Deterministic RNG stubs (RngFn is float(*)() — needs file-scope state) ──

static float rng_zero() { return 0.0f; }      // fires every chance>0, qty = min
static float rng_high() { return 0.9999f; }   // fires only chance==1.0, qty ~ max

// (Сценарный поток `rng_seq`/`seq_set` жил ровно для поштучной сверки
//  хардкод-таблиц лута существ и умер вместе с ними — M-139.)

static int count_of(const std::vector<ItemRef>& v, const char* id) {
    const int idx = item_index(id);
    for (const ItemRef& s : v) {
        if (idx >= 0 && s.def == std::uint16_t(idx)) return s.count;
    }
    return -1; // absent
}

// ── project_combat: pins the documented projection contract ─────────────────

static void test_project_combat_melee() {
    // A melee body: known sheet, distinctive authored base with missile params
    // that MUST be preserved verbatim even though attackKind is Melee.
    CharacterSheet cs;
    cs.attributes[sm::AttributeId::Str] = 12;
    cs.attributes[sm::AttributeId::Intl] = 7;
    cs.attributes[sm::AttributeId::End] = 9;
    cs.skills[sm::SkillId::Armsmaster] = 4;
    cs.skills[sm::SkillId::Spellcraft] = 3;
    cs.skills[sm::SkillId::Bodybuilding] = 2;

    CombatTemplate base{};
    base.hp = 50; base.dice = {6, 1}; base.speedMarchMult = 1.65f; base.attackRange = 3.5f;
    base.recoveryTicks = sm::steps_from_seconds(1.25f); base.label = "TST";
    base.attackKind = CombatTemplate::Melee;
    base.missileSpeed = 12.5f; base.missileBlast = 4.0f; base.missileColorRGBA = 0xAABBCCDDu;

    const CombatTemplate out = project_combat(cs, base);

    const BarCeilings expCs =
        bar_ceilings(cs.attributes, cs.skills, int(base.hp), 100, 100);
    const DerivedBonuses expD = calculate_derived(cs.attributes, cs.skills);

    CHECK(approx(out.hp, float(expCs.maxHp)),
          "melee: hp == bar_ceilings(sheet, base.hp, 100, 100, 100).maxHp");
    CHECK(out.dice.n == base.dice.n && out.dice.m == base.dice.m,
          "melee: the row's dice pass through the projection verbatim");
    CHECK(out.flatAdd == std::int16_t(std::floor(expD.rawPhysDamage)),
          "melee: flatAdd == floor(rawPhysDamage) — attributes ADD to the dice");
    CHECK(int(out.luck) == int(cs.attributes[sm::AttributeId::Lck]),
          "melee: the sheet's LCK rides to the crit door");
    // Attack identity preserved verbatim.
    CHECK(approx(out.speedMarchMult, base.speedMarchMult),
          "melee: pace (as a fraction of the march) preserved");
    CHECK(approx(out.attackRange, base.attackRange), "melee: range preserved");
    // Tempo is the ONE exception to "identity preserved verbatim" since the
    // recovery door (CANON S14 «один рычаг», 2026-09-07): the row's base
    // is the BASE and the sheet divides it — through exactly the door, so
    // this expectation moves when the law moves, never a pinned number.
    // Целые тики насквозь (M-396): равенство точное, approx умер.
    CHECK(out.recoveryTicks
              == std::uint32_t(recovery_steps(base.recoveryTicks,
                                              cs.attributes, cs.skills,
                                              sm::SkillId::Armsmaster)),
          "melee: recoveryTicks == the recovery door's verdict (Armsmaster)");
    CHECK(out.recoveryTicks < base.recoveryTicks,
          "melee: a quick sheet strikes faster than its row's base");
    CHECK(out.attackKind == CombatTemplate::Melee, "melee: kind preserved");
    CHECK(out.label == base.label, "melee: label pointer preserved");
    CHECK(approx(out.missileSpeed, base.missileSpeed), "melee: missileSpeed preserved");
    CHECK(approx(out.missileBlast, base.missileBlast), "melee: missileBlast preserved");
    CHECK(out.missileColorRGBA == base.missileColorRGBA, "melee: missile color preserved");
}

static void test_project_combat_missile() {
    // РАСКЛЕЙКА КАСТА И ВЫСТРЕЛА (2026-09-19, CANON S14). This witness used
    // to pin the OPPOSITE law — «a Missile row takes the SPELL bonus» — and
    // that was exactly the defect: `attackKind == Missile` meant "caster"
    // only because every Missile row in the game happened to be one, so the
    // first NPC archer would have drawn INT into his arrows. A row now NAMES
    // its spell, and the three cases are told apart by the ROW, not by a
    // column about delivery.
    CharacterSheet cs;
    cs.attributes[sm::AttributeId::Str] = 12;
    cs.attributes[sm::AttributeId::Intl] = 7;
    cs.skills[sm::SkillId::Armsmaster] = 4;
    cs.skills[sm::SkillId::Spellcraft] = 3;
    const DerivedBonuses expD = calculate_derived(cs.attributes, cs.skills);
    // The branch is meaningful only while the two adds differ — else every
    // claim below would pass on an arithmetic accident.
    CHECK(!approx(expD.rawSpellDamage, expD.rawPhysDamage),
          "fixture: phys and spell adds differ, so the split is observable");

    // A SHOT: Missile, no spell named.
    CombatTemplate shot{};
    shot.hp = 60; shot.dice = {8, 1};
    shot.attackKind = CombatTemplate::Missile;
    const CombatTemplate shotOut = project_combat(cs, shot);
    CHECK(shotOut.flatAdd == 0,
          "shot: NO attribute add — not INT and not STR (S14 «урон "
          "стрелкового БЕЗ добавки атрибута»)");
    CHECK(shotOut.attackKind == CombatTemplate::Missile,
          "shot: delivery preserved");
    CHECK(shotOut.dice.n == shot.dice.n && shotOut.dice.m == shot.dice.m,
          "shot: a row with no spell keeps its OWN dice");

    // A CAST: the row names a spell, and the blow IS that spell.
    const int ord = sm::spell_ordinal("magic_bolt");
    CHECK(sm::spell_ordinal_ok(ord), "fixture: the named spell exists");
    CombatTemplate cast = shot;
    cast.actions[0] = std::uint8_t(sm::action_of_spell(ord));
    const CombatTemplate castOut = project_combat(cs, cast);
    CHECK(castOut.flatAdd == std::int16_t(std::floor(expD.rawSpellDamage)),
          "cast: the add is the caster's INT — magic, and it says so");
    CHECK(castOut.dice.n == sm::kSpellDefs[ord].dice.n,
          "cast: a caster has no dice of his own — the SPELL's are the blow");

    // TEMPO follows the same split: Spellcraft paces a cast, Armsmaster
    // paces a shot (a bow is drawn by arms, not by a casting hand).
    CombatTemplate cdShot{};
    cdShot.recoveryTicks = sm::steps_from_seconds(2.0f);
    cdShot.attackKind = CombatTemplate::Missile;
    CombatTemplate cdCast = cdShot;
    cdCast.actions[0] = std::uint8_t(sm::action_of_spell(ord));
    CHECK(project_combat(cs, cdCast).recoveryTicks
              == std::uint32_t(recovery_steps(cdCast.recoveryTicks,
                                              cs.attributes, cs.skills,
                                              sm::SkillId::Spellcraft)),
          "cast: recoveryTicks == the recovery door's verdict (Spellcraft)");
    CHECK(project_combat(cs, cdShot).recoveryTicks
              == std::uint32_t(recovery_steps(cdShot.recoveryTicks,
                                              cs.attributes, cs.skills,
                                              sm::SkillId::Armsmaster)),
          "shot: recoveryTicks == the recovery door's verdict (Armsmaster)");
}

// ── make_character_sheet: the level point-budget identity ────────────────────

static int attr_sum(const Attributes& a) {
    // Walk the NAMED scores: an attribute the game names later joins this sum
    // by existing, and the reserved tail is not a score anybody spent.
    int sum = 0;
    for (int i = 0; i < int(AttributeId::Count); ++i) sum += a.of(AttributeId(i));
    return sum;
}
static int skill_sum(const Skills& s) {
    // Walk the envelope, not a hand-listed eight: a skill added to the
    // registry joins this sum by existing, which is the whole point of the
    // ranks being a flat array.
    int sum = 0;
    for (std::uint8_t r : s.rank) sum += int(r);
    return sum;
}

static void test_sheet_budget_identity() {
    // Every attribute starts at 1 (8 total baseline — the canon eight); every
    // skill starts at 0. A level-N sheet must spend EXACTLY the player
    // economy (CANON S14, 2026-09-03): creation = 5 attribute points + 5
    // learn picks (each pick = rank 1), then +1 attribute and +1 skill point
    // per level — pools AND picks end at zero.
    const int levels[] = {1, 5, 20, 32};
    for (int lvl : levels) {
        for (NPCType role : {NPCType::Peasant, NPCType::Bandit, NPCType::Sorceress}) {
            const CharacterSheet cs = make_character_sheet(role, lvl, 0xC0FFEEu);
            const int expAttr = 5 + (lvl - 1);
            const int expSkill = 5 + (lvl - 1);   // 5 learned ranks + points

            char msg[128];
            std::snprintf(msg, sizeof msg,
                "sheet L%d role %d: attribute budget fully spent", lvl, int(role));
            CHECK(attr_sum(cs.attributes) == 8 + expAttr, msg);

            std::snprintf(msg, sizeof msg,
                "sheet L%d role %d: skill budget fully spent", lvl, int(role));
            CHECK(skill_sum(cs.skills) == expSkill, msg);

            std::snprintf(msg, sizeof msg,
                "sheet L%d role %d: pools drained to zero", lvl, int(role));
            // «Опустошён» теперь спрашивается у РАЗНОСТИ выданного и
            // потраченного (M-383): счётчики умерли как дубли выводимого.
            // Утверждение то же — генератор тратит бюджет ступени ДО КОНЦА.
            CHECK(sm::attribute_points_left(cs.levelData, cs.attributes) == 0
                  && sm::skill_points_left(cs.levelData, cs.skills) == 0
                  && sm::learn_picks_left(cs.levelData, cs.skills) == 0, msg);

            std::snprintf(msg, sizeof msg,
                "sheet L%d role %d: level + xp curve set", lvl, int(role));
            CHECK(sm::level_of(cs.levelData) == lvl
                  && sm::exp_to_next_of(cs.levelData)
                         == exp_to_next_level(lvl), msg);
        }
    }
}

static void test_sheet_determinism() {
    // Same (role, level, seed) is bit-identical; a different seed keeps the
    // SAME budget totals (identity is seed-independent) while (almost surely)
    // redistributing the points.
    const CharacterSheet a = make_character_sheet(NPCType::Sorceress, 20, 42u);
    const CharacterSheet b = make_character_sheet(NPCType::Sorceress, 20, 42u);
    const CharacterSheet c = make_character_sheet(NPCType::Sorceress, 20, 43u);

    CHECK(attr_sum(a.attributes) == attr_sum(b.attributes)
          && skill_sum(a.skills) == skill_sum(b.skills),
          "determinism: same seed -> same totals");
    CHECK(a.attributes.of(sm::AttributeId::Str) == b.attributes.of(sm::AttributeId::Str)
          && a.attributes.of(sm::AttributeId::Intl) == b.attributes.of(sm::AttributeId::Intl)
          && a.attributes.of(sm::AttributeId::Wil) == b.attributes.of(sm::AttributeId::Wil)
          && a.skills.of(sm::SkillId::Spellcraft) == b.skills.of(sm::SkillId::Spellcraft),
          "determinism: same seed -> identical field allocation");
    CHECK(attr_sum(a.attributes) == attr_sum(c.attributes)
          && skill_sum(a.skills) == skill_sum(c.skills),
          "determinism: different seed -> same budget totals");
}

// ── Unified loot table ───────────────────────────────────────────────────────

// РЕЕСТР ЛУТА ДЕРЖИТ ТОЛЬКО ВЕЩИ МИРА (M-139, вердикт владельца 2026-09-26).
// Здесь стояли `test_npc_loot_id` (роль → профиль) и проверки десяти
// хардкод-таблиц существ — свидетели ТАБЛИЦ, которых больше нет: и таблицы, и
// колонка `lootId` строки снесены («захардкоженый говносрак на существо?
// конечно сноси»). Осталось то, что таблицей не является, а есть КУРС
// ПЕРЕНОСА из поля: срубленное дерево платит ровно списанное с клетки дерево,
// сжатый стебель — ровно списанное зерно.
static void test_prop_loot_is_the_transfer_rate() {
    // 1 стебель = 1 зерно, и это ЗАКОН, а не бросок: оба конца rng прибиты к
    // нему (дверь жатвы отдельно масштабирует по высоте стебля).
    auto crop = roll_loot_profile("crop", 1, rng_zero, 0);
    CHECK(crop.size() == 1 && count_of(crop, "food") == 1,
          "crop/rng_zero: one stand pays its one unit of food");
    auto cropHigh = roll_loot_profile("crop", 1, rng_high, 0);
    CHECK(count_of(cropHigh, "food") == 1,
          "crop/rng_high: still one unit — the rate is a law, not a roll");
    // 1 дерево = 1 дерево — ровно то, что ведомость макро-стока списывает с
    // клетки (`macro_stock_apply(TreeCount, -1)`).
    auto tree = roll_loot_profile("tree", 1, rng_zero, 0);
    CHECK(tree.size() == 1 && count_of(tree, "wood") == 1,
          "tree/rng_zero: one felled tree pays its one wood");
    auto treeHigh = roll_loot_profile("tree", 9, rng_high, 0);
    CHECK(count_of(treeHigh, "wood") == 1,
          "tree/rng_high: уровень рубящего курса переноса не двигает");

    // Fail-closed: неизвестный, пустой и нулевой id не дают ничего.
    CHECK(roll_loot_profile("does_not_exist", 5, rng_zero, 0).empty(),
          "unknown lootId -> empty");
    CHECK(roll_loot_profile("", 5, rng_zero, 0).empty(), "empty lootId -> empty");
    CHECK(roll_loot_profile(nullptr, 5, rng_zero, 0).empty(),
          "null lootId -> empty");

    // НЕГАТИВНЫЙ КОНТРОЛЬ СНОСА (§8 п.6): профили РОЛЕЙ обязаны перестать
    // резолвиться — вернётся любой из них, и этот свидетель покраснеет.
    for (const char* role : {"peasant", "woodcutter", "merchant", "caravan",
                             "bandit", "guard", "witch", "sorceress",
                             "wildlife", "demons", "bandits"}) {
        CHECK(roll_loot_profile(role, 5, rng_zero, 0).empty(),
              "профиль РОЛИ в реестре лута больше не живёт (M-139)");
    }
    // И реестр целиком — ровно две строки мира, счёт выводится из таблицы.
    CHECK(loot_profile_count() == 2,
          "в реестре лута ровно две строки — вещи мира, не роли");
}

// ЗДЕСЬ СТОЯЛ СВИДЕТЕЛЬ КОШЕЛЬКА (`test_generate_loot_gold`, 12 проверок):
// «строка говорит, сколько тварь стоит ограбить, место модулирует». Он
// охранял ВЫДАЧУ МОНЕТ ИЗ ВОЗДУХА, а не закон мира, и умер вместе с ней —
// M-139, вердикт владельца 2026-09-26. Носитель снесён целиком: колонки
// `purseMin`/`purseMax`, дверь `npc_purse`, функция `generate_loot_gold` и
// её две константы. Сколько стоимости несёт тварь, решит ПУЛ ЛУТА по
// контексту и весам единой таблицы; когда он придёт, свидетеля ему писать
// заново — и уже про его закон, а не про эту пару авторских чисел.

// ── The affix door (owner's design 2026-09-07) ─────────────────────────────

static void test_affix_door() {
    // THE coin run: geometric, capped, and the same primitive for "how many"
    // and "how strong" (the one-formula verdict).
    CHECK(coin_run(rng_zero, 16, kAffixChanceDen, 8) == 8,
          "an always-heads coin runs to the cap and STOPS there");
    CHECK(coin_run(rng_high, 271, kAffixChanceDen, 8) == 0,
          "a tails coin runs zero times — no floor is invented");

    // ── СИЛА АФФИКСА ЕСТЬ ФУНКЦИЯ ТОЛЬКО ТИРА ЖЕРТВЫ ──────────────────────
    //
    // ЗАКОН (вердикт владельца 2026-11-04, M-388): «РЕШАЕТ ТИР ЖЕРТВЫ, А
    // ОПАСНОСТЬ КЛЕТКИ НЕ ПРИБАВЛЯЕТ НИЧЕГО». Здесь стояли ТРИ проверки,
    // утверждавшие вклад опасности («the danger byte rides in whole», «both
    // axes act at once»), и четвёртая — «a fresh body on open land: power 0».
    // Они охраняли МЕХАНИКУ, которую вердикт снял, поэтому ПЕРЕВЕРЖДЕНЫ на
    // новый закон, а не подогнаны под новое число (§8 ЗАКОН НУЛЕВОЙ п.3): у
    // свидетеля сменился ОХРАНЯЕМЫЙ ЗАКОН, а не порог.
    //
    // САМ ВХОД УМЕР, И ЭТО СИЛЬНЕЕ ЛЮБОЙ ПРОВЕРКИ: вернуть опасность в формулу
    // теперь НЕСОБИРАЕМО — у двери один параметр, — а запрет под компилятором
    // не флачит и не подгоняется (§8 п.6). Третья ось, богатство места, умерла
    // тем же способом 2026-10-18 (M-223).
    //
    // Утверждаются ОТНОШЕНИЯ и ступени ЛЕСТНИЦЫ, не пересказанные числа (§8
    // п.4): всё ожидаемое берётся у `tables/progression.h` и у каталога
    // существ — у тех же таблиц, что читает сама дверь.
    CHECK(affix_power(kMaxTier) == kMaxTier && affix_power(1) == 1,
          "байт ЕСТЬ ступень: верх лестницы и её пол проходят дверь как есть");
    CHECK(affix_power(kMaxTier + 1000) == kMaxTier && affix_power(-7) == 1,
          "сужение int → байт идёт ДВЕРЬЮ лестницы: ступень выше верха не "
          "заворачивается в ноль, ниже пола не проваливается");
    // МОНОТОННОСТЬ ПО ВСЕЙ ЛЕСТНИЦЕ, а не в двух точках, и это ровно то, что
    // прежний курс ломал: `(тир−1)·8` сатурировал байт на тире 33, после чего
    // весь верх каталога становился НЕРАЗЛИЧИМ.
    int rungs = 0, notRising = 0;
    for (int t = 2; t <= kMaxTier; ++t) {
        ++rungs;
        if (affix_power(t) <= affix_power(t - 1)) ++notRising;
    }
    CHECK(rungs == kMaxTier - 1, "лестница обязана была пройтись целиком");
    CHECK(notRising == 0,
          "каждая следующая ступень сыплет СТРОГО богаче предыдущей — "
          "сатурации, схлопывавшей верх каталога, больше нет ни на одной");
    // ЯКОРЬ В РЕАЛЬНЫХ СТРОКАХ МИРА (§8 п.6): довод вердикта был про кролика и
    // дракона, и утверждается он ИХ ЖЕ тирами, взятыми у каталога, а не
    // переписанными сюда. Прежняя формула в красной клетке давала обоим ровно
    // 255 — именно это и значило «кролик сыпал как дракон».
    CHECK(affix_power(creature_tier(NPCType::Dragon))
              > affix_power(creature_tier(NPCType::Rabbit)),
          "дракон сыплет богаче кролика, и это ЕДИНСТВЕННОЕ, что их у двери "
          "различает: клетки у неё больше нет");

    // The door refuses what cannot be worn — food rolls nothing, spends no
    // seed, and the caller did not have to know the distinction.
    ItemRef provisions{};
    provisions.def = std::uint16_t(item_index("food"));
    provisions.count = 1;
    grant_affixes(provisions, 255, rng_zero);
    CHECK(affix_count(provisions) == 0 && provisions.seed == 0,
          "an affix is a WORN thing: food walks through the door untouched");

    // A wearable at the deep end, always-heads: the format's 8 cells, each
    // at the roll cap — and rng_zero deterministically picks the FIRST
    // weighted row (of Strength), so the whole instance is pinned.
    ItemRef sword{};
    sword.def = std::uint16_t(item_index("wpn_sword"));
    sword.count = 1;
    grant_affixes(sword, 255, rng_zero);
    CHECK(affix_count(sword) == kMaxItemAffixes,
          "always-heads at power 255 fills every cell the format has");
    CHECK(sword.affix_at(0).row == std::uint8_t(BonusId::Str)
              && sword.affix_at(0).value == kAffixValueCapUnits,
          "value = cap units × the row's step; rng_zero picks the first row");
    CHECK(sword.seed != 0, "a rolled instance stamps its seed");
    for (int i = 0; i < kMaxItemAffixes; ++i) {
        CHECK(sword.affix_at(i).row < std::uint8_t(BonusId::Count),
              "every granted row is a registry ordinal");
    }

    // The stacking law reads the roll: a rolled sword and a plain sword are
    // different KINDS and never merge.
    ItemRef plain{};
    plain.def = sword.def;
    plain.count = 1;
    CHECK(!plain.same_kind_as(sword),
          "a rolled instance never merges with the plain row");

    // What it is worth, and what it is called.
    CHECK(value_of(plain) == item_def_at(int(plain.def))->value,
          "a plain stack answers exactly its row's price — the economy "
          "does not move");
    CHECK(value_of(sword) > value_of(plain),
          "«чем реже, тем ценнее» made arithmetic");
    CHECK(std::string(affix_suffix(sword)) == "of Strength",
          "the first speaking cell names the instance");
    CHECK(affix_suffix(plain)[0] == '\0', "a plain thing has no suffix");

    // A curse subtracts but the floor holds: worthless, never a debt.
    ItemRef cursed = plain;
    cursed.set_affix(0, {std::uint8_t(BonusId::Str), -32767});
    CHECK(value_of(cursed) == 0, "a ruined thing is worthless, not a debt");
}

int main() {
    test_project_combat_melee();
    test_project_combat_missile();
    test_sheet_budget_identity();
    test_sheet_determinism();
    test_prop_loot_is_the_transfer_rate();
    test_affix_door();
    return sm::test::report("rpg_loot_test");
}

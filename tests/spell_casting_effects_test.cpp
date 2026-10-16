#include "check.h"
#include "scene_objects_fixture.h"   // арена объектов: fx/атрибуция — колонки

#include "core/time.h"
#include "content/spells/casting.h"
#include "content/spells/spell_book.h"
#include "ecs/world.h"
#include "sub/spell_effects.h"
#include "sub/body.h"
#include "sub/ability.h"   // tick_body_recovery — один слив (кусок 2)
#include "macro/store.h"

#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cctype>

namespace {

// Снаряд — ЧИСТЫЙ ЖИЛЕЦ АРЕНЫ (ступень (v) ломтя 7): счёт и поиск — слотовый
// цикл по роли-биту, те же выражения, что у продакшена (smoke_projectile_count).
// objects_find, не objects_of: мир без арены честно отвечает «снарядов нет».
int projectile_count(sm::ecs::World& w) {
    const sm::sub::SubObjects* objs = sm::sub::objects_find(w.reg);
    if (objs == nullptr) return 0;
    int count = 0;
    for (int s = 0; s < int(sm::kMaxSubObjects); ++s) {
        const std::uint16_t f = objs->flags[std::size_t(s)];
        if ((f & sm::sub::kObjAlive) != 0u
            && (f & sm::sub::kObjProjectile) != 0u) {
            ++count;
        }
    }
    return count;
}

bool nearf(float a, float b) {
    return std::fabs(a - b) <= 0.001f;
}

// Слот первого живого снаряда со спеллом spellId, -1 = нет такого.
int projectile_slot_by_spell(sm::ecs::World& w, std::uint32_t stableId) {
    const sm::sub::SubObjects* objs = sm::sub::objects_find(w.reg);
    if (objs == nullptr) return -1;
    for (int s = 0; s < int(sm::kMaxSubObjects); ++s) {
        const std::uint16_t f = objs->flags[std::size_t(s)];
        if ((f & sm::sub::kObjAlive) == 0u
            || (f & sm::sub::kObjProjectile) == 0u) continue;
        if (objs->projectile[std::size_t(s)].spellId == stableId) return s;
    }
    return -1;
}

bool find_projectile_by_spell(sm::ecs::World& w, const char* spellId,
                              sm::ecs::Projectile& out) {
    const int s = projectile_slot_by_spell(w, sm::stable_spell_id(spellId));
    if (s < 0) return false;
    out = sm::sub::objects_of(w.reg).projectile[std::size_t(s)];
    return true;
}

bool find_projectile_pose_by_spell(sm::ecs::World& w, const char* spellId,
                                   sm::ecs::Projectile& outProjectile,
                                   sm::ecs::Position& outPosition) {
    const int s = projectile_slot_by_spell(w, sm::stable_spell_id(spellId));
    if (s < 0) return false;
    const sm::sub::SubObjects& objs = sm::sub::objects_of(w.reg);
    outProjectile = objs.projectile[std::size_t(s)];
    outPosition = objs.pos[std::size_t(s)];
    return true;
}

// Тело — СЛОТ арены (ломоть D: сущностей у тел сцены нет). Атрибуция
// «кем ранен» — колонка lastHitBy того же слота (1а).
int add_target(sm::ecs::World& w, float x, float y,
               int hp, bool playerSide) {
    const int s = sm::test::give_body(w.reg);
    sm::test::give_pos(w.reg, s, x, y, 0.0f);
    sm::test::give_pools(w.reg, s, sm::ecs::Pools{hp, hp});
    sm::test::give_sprite(w.reg, s, sm::ecs::Sprite{std::uint16_t(0),
        std::uint8_t(255), std::uint8_t(255), std::uint8_t(255), 6.0f});
    if (playerSide) {
        sm::test::give_flag(w.reg, s, sm::sub::kObjPlayerSoldier);
    } else {
        sm::test::give_kind(w.reg, s, sm::ecs::NPCKind{2, 2});
    }
    return s;
}

// A real player-tagged body to own player-cast projectiles. Inc 4d retired
// the ownerId==0 sentinel — ownership/faction now key off the owner body's
// columns, exactly as production does via the avatar scene reference (iii-б).
// Placed far from targets so it is never itself in a blast/line by accident.
sm::sub::ObjRef add_player(sm::ecs::World& w, float x, float y) {
    // Кастер — НЕ слот 0: на фикстуре, где игрок — первый жилец арены,
    // мутация «писатель кладёт чужой слот 0» прошла бы зелёной (слепота
    // слота 0 — родня слепоты сущности 0, пойманной в (iii-д)).
    (void)sm::test::arena_of(w.reg).alloc(sm::ecs::Position{});
    const int s = sm::test::give_body(w.reg);
    sm::test::give_pos(w.reg, s, x, y, 0.0f);
    sm::test::give_pools(w.reg, s, sm::ecs::Pools{1000, 1000});
    sm::test::make_avatar(w.reg, s);
    return sm::sub::objects_of(w.reg).avatar;
}

float hp_of(sm::ecs::World& w, int s) {
    sm::sub::SubObjects& objs = sm::test::arena_of(w.reg);
    const auto* hp = s >= 0 ? sm::sub::slot_pools(objs, s) : nullptr;
    return hp ? hp->hp : -1.0f;
}

// Атрибуция «кем убит/ранен» — колонка lastHitBy единого массива объектов,
// ссылка сцены ObjRef (ступень (iii); §41 корень 5: жнец резолвит лидера
// по телу). Ожидание строится ТОЙ ЖЕ дверью ref_of, что у писателя.
bool last_hit_by(sm::ecs::World& w, int s, int attackerSlot) {
    return attackerSlot >= 0
        && sm::test::last_hit_of(w.reg, s)
               == sm::sub::ref_of(sm::sub::objects_of(w.reg), attackerSlot);
}
bool last_hit_by(sm::ecs::World& w, int s, sm::sub::ObjRef owner) {
    return owner.id != 0u && sm::test::last_hit_of(w.reg, s) == owner;
}

struct SeqRng {
    const float* values = nullptr;
    int count = 0;
    int index = 0;
};

float seq_rng01(void* user) {
    auto* rng = static_cast<SeqRng*>(user);
    if (!rng || !rng->values || rng->count <= 0) return 0.0f;
    if (rng->index >= rng->count) return 0.0f;
    return rng->values[rng->index++];
}

} // namespace

int main() {

    sm::ecs::World world;

    auto worldStore_ = sm::make_macro_store();

    sm::store_attach(world, worldStore_.get());

    // Арена — предусловие снарядов (ступень (v): чистые жильцы

    // арены; §8 п.11 — свидетель сам рожает предусловие).

    sm::test::arena_of(world.reg);
    // Арена сцены — ПРЕДУСЛОВИЕ свидетеля (§8 п.11): с ломтя 5 место снаряда
    // живёт колонкой слота, значит рождённый без арены снаряд НЕ ИМЕЕТ места
    // вовсе, и закон дула ниже спрашивать было бы не у кого.
    sm::test::arena_of(world.reg);
    sm::SpellBook book;
    sm::ecs::Pools combat{};
    combat.mp = 2000;
    combat.maxMp = 2000;
    sm::Attributes attributes{};
    sm::Skills skills{};

    const sm::SpellDef* fireDef = sm::spell_find("fireball");
    const sm::SpellDef* iceDef = sm::spell_find("ice_shard");
    const sm::SpellDef* magicDef = sm::spell_find("magic_bolt");
    const sm::SpellDef* chainDef = sm::spell_find("lightning_chain");
    const sm::SpellDef* beamDef = sm::spell_find("energy_beam");
    const sm::SpellDef* armDef = sm::spell_find("armageddon");
    const sm::SpellDef* hasteDef = sm::spell_find("haste");
    const sm::SpellDef* flightDef = sm::spell_find("flight");
    const bool ok1 = !(!fireDef || fireDef->secondaryTag != fireDef->tag
                       || !sm::spell_has_tag(*fireDef, sm::SpellTag::Fire)
                       || std::strcmp(fireDef->statusEffect, "burning") != 0
                       || !nearf(fireDef->statusDuration, 3.0f));
    CHECK(ok1, "fireball metadata wrong");
    if (!ok1) return 1;
    const bool ok2 = !(!iceDef || iceDef->secondaryTag != iceDef->tag
                       || !sm::spell_has_tag(*iceDef, sm::SpellTag::Ice)
                       || std::strcmp(iceDef->statusEffect, "chilled") != 0
                       || !nearf(iceDef->statusDuration, 4.0f));
    CHECK(ok2, "ice_shard metadata wrong");
    if (!ok2) return 1;
    const bool ok3 = !(!chainDef || chainDef->secondaryTag != chainDef->tag
                       || !sm::spell_has_tag(*chainDef, sm::SpellTag::Lightning)
                       || std::strcmp(chainDef->statusEffect, "shocked") != 0
                       || !nearf(chainDef->statusDuration, 2.0f));
    CHECK(ok3, "lightning_chain metadata wrong");
    if (!ok3) return 1;
    const bool ok4 = !(!beamDef || beamDef->secondaryTag == beamDef->tag
                       || !sm::spell_has_tag(*beamDef, sm::SpellTag::Arcane)
                       || !sm::spell_has_tag(*beamDef, sm::SpellTag::Light)
                       || beamDef->statusEffect[0] != '\0');
    CHECK(ok4, "energy_beam metadata wrong");
    if (!ok4) return 1;
    const bool ok5 = !(!hasteDef || hasteDef->secondaryTag == hasteDef->tag
                       || !sm::spell_has_tag(*hasteDef, sm::SpellTag::Body)
                       || !sm::spell_has_tag(*hasteDef, sm::SpellTag::Air)
                       || std::strcmp(hasteDef->statusEffect, "hasted") != 0);
    CHECK(ok5, "haste metadata wrong");
    if (!ok5) return 1;
    const bool ok6 = !(!flightDef || flightDef->secondaryTag == flightDef->tag
                       || !sm::spell_has_tag(*flightDef, sm::SpellTag::Air)
                       || !sm::spell_has_tag(*flightDef, sm::SpellTag::Arcane)
                       || std::strcmp(flightDef->statusEffect, "flying") != 0);
    CHECK(ok6, "flight metadata wrong");
    if (!ok6) return 1;
    const bool ok7 = !(!fireDef->icon || std::strcmp(fireDef->icon, "*") != 0
                       || !fireDef->sourceIcon || std::strcmp(fireDef->sourceIcon, "\xF0\x9F\x94\xA5") != 0
                       || fireDef->rarity != sm::SpellRarity::Common
                       || !nearf(fireDef->recovery, 2.3f));
    CHECK(ok7, "fireball identity metadata wrong");
    if (!ok7) return 1;
    const bool ok8 = !(!iceDef->icon || std::strcmp(iceDef->icon, "I") != 0
                       || !iceDef->sourceIcon || std::strcmp(iceDef->sourceIcon, "\xE2\x9D\x84") != 0
                       || iceDef->rarity != sm::SpellRarity::Uncommon
                       || !nearf(iceDef->recovery, 1.7f));
    CHECK(ok8, "ice_shard identity metadata wrong");
    if (!ok8) return 1;
    const bool ok9 = !(!magicDef || !magicDef->icon || std::strcmp(magicDef->icon, "+") != 0
                       || !magicDef->sourceIcon || std::strcmp(magicDef->sourceIcon, "\xE2\x9C\xA6") != 0
                       || magicDef->rarity != sm::SpellRarity::Common
                       || !nearf(magicDef->recovery, 0.0f));
    CHECK(ok9, "magic_bolt identity metadata wrong");
    if (!ok9) return 1;
    const bool ok10 = !(!chainDef->icon || std::strcmp(chainDef->icon, "Z") != 0
                        || !chainDef->sourceIcon || std::strcmp(chainDef->sourceIcon, "\xE2\x9B\xA7") != 0
                        || chainDef->rarity != sm::SpellRarity::Rare
                        || !nearf(chainDef->recovery, 4.1f));
    CHECK(ok10, "lightning_chain identity metadata wrong");
    if (!ok10) return 1;
    const bool ok11 = !(!beamDef->icon || std::strcmp(beamDef->icon, "=") != 0
                        || !beamDef->sourceIcon || std::strcmp(beamDef->sourceIcon, "\xE2\x9A\xA1") != 0
                        || beamDef->rarity != sm::SpellRarity::Uncommon
                        || !nearf(beamDef->recovery, 2.9f));
    CHECK(ok11, "energy_beam identity metadata wrong");
    if (!ok11) return 1;
    const bool ok12 = !(!armDef || !armDef->icon || std::strcmp(armDef->icon, "X") != 0
                        || !armDef->sourceIcon || std::strcmp(armDef->sourceIcon, "\xE2\x98\xA0") != 0
                        || armDef->rarity != sm::SpellRarity::Mythic
                        || !nearf(armDef->recovery, 122.0f));
    CHECK(ok12, "armageddon identity metadata wrong");
    if (!ok12) return 1;
    const bool ok13 = !(!hasteDef->icon || std::strcmp(hasteDef->icon, ">") != 0
                        || !hasteDef->sourceIcon || std::strcmp(hasteDef->sourceIcon, "\xF0\x9F\x92\xA8") != 0
                        || hasteDef->rarity != sm::SpellRarity::Uncommon
                        || !nearf(hasteDef->recovery, 0.0f));
    CHECK(ok13, "haste identity metadata wrong");
    if (!ok13) return 1;
    const bool ok14 = !(!flightDef->icon || std::strcmp(flightDef->icon, "^") != 0
                        || !flightDef->sourceIcon || std::strcmp(flightDef->sourceIcon, "\xF0\x9F\x95\x8A") != 0
                        || flightDef->rarity != sm::SpellRarity::Rare
                        || !nearf(flightDef->recovery, 0.0f));
    CHECK(ok14, "flight identity metadata wrong");
    if (!ok14) return 1;
    const bool ok15 = !(!std::strstr(fireDef->description, "allies included")
                        || !std::strstr(iceDef->description, "bosses and elites")
                        || !std::strstr(magicDef->description, "bread and butter")
                        || !std::strstr(chainDef->description, "losing force")
                        || !std::strstr(beamDef->description, "line up")
                        || !std::strstr(armDef->description, "moral bankruptcy")
                        || !std::strstr(hasteDef->description, "supernatural speed")
                        || !std::strstr(flightDef->description, "fall is unforgiving"));
    CHECK(ok15, "spell description metadata wrong");
    if (!ok15) return 1;
    const bool ok16 = !(!magicDef || magicDef->rule != sm::SpellRuleId::None
                        || sm::spell_flavor_count(magicDef->pros) != 3
                        || std::strcmp(magicDef->pros[0], "No recovery") != 0
                        || std::strcmp(magicDef->cons[2], "No utility") != 0);
    CHECK(ok16, "magic_bolt flavor metadata wrong");
    if (!ok16) return 1;
    // A DAMAGING spell carries no stat row and no rule: damage is a BLOW, and
    // blows have exactly one door. Its DICE are THE WHOLE of what it strikes
    // for — 36d1 since 2026-09-19 (session Е), when the `scalingPower` knob
    // was folded into the row it was multiplying (30 × 1.2): a row's strength
    // lives in ONE column, not in a column times a float beside it.
    // The old `macroType = DamageRegion, macroPower = 10` was the same fact
    // told a second time, in a vocabulary nobody read.
    const bool ok17 = !(fireDef->rule != sm::SpellRuleId::None
                        || fireDef->effects[0].row != 0
                        || fireDef->dice.n != 36 || fireDef->dice.m != 1
                        || sm::spell_flavor_count(fireDef->pros) < 1);
    CHECK(ok17, "fireball effect/flavor metadata wrong");
    if (!ok17) return 1;
    // A FLAVOUR LINE MAY NOT PROMISE A MECHANIC THAT DOES NOT EXIST.
    // This assertion used to pin the literal "Burning DOT" in fireball's
    // pros — and that line sold the `statusEffect` column, which NOTHING but
    // the tooltip reads: the word "burning" is printed and never applied. So
    // the test was guarding the defect (testing law #7), and it reddened the
    // day the lie was cut instead of the day it was told.
    //
    // What replaces it is the LAW, swept over the whole table: no pros/cons
    // line of any spell may quote its own statusEffect word. The negative
    // control is the sweep itself — it must have looked at rows with a
    // status word to have proved anything.
    {
        int statusRowsSeen = 0, promises = 0;
        for (const sm::SpellDef& d : sm::kSpellDefs) {
            if (!d.statusEffect || d.statusEffect[0] == '\0') continue;
            ++statusRowsSeen;
            // Case-insensitive stem match: "burning" finds "Burning DOT".
            const auto quotes_status = [&](const char* line) {
                if (!line) return false;
                for (const char* h = line; *h; ++h) {
                    const char* a = h;
                    const char* b = d.statusEffect;
                    while (*b && std::tolower((unsigned char)*a)
                                     == std::tolower((unsigned char)*b)) {
                        ++a; ++b;
                    }
                    if (!*b) return true;
                }
                return false;
            };
            const int np = sm::spell_flavor_count(d.pros);
            for (int i = 0; i < np; ++i)
                if (quotes_status(d.pros[i])) ++promises;
            const int nc = sm::spell_flavor_count(d.cons);
            for (int i = 0; i < nc; ++i)
                if (quotes_status(d.cons[i])) ++promises;
        }
        sm::test::check(statusRowsSeen > 0,
                        "the statusEffect sweep must have rows to judge",
                        "tests/spell_casting_effects_test.cpp", 0);
        sm::test::check(promises == 0,
                        "no spell flavour line may promise its UI-only "
                        "statusEffect as a mechanic",
                        "tests/spell_casting_effects_test.cpp", 0);
    }
    const bool ok18 = !(!armDef || armDef->rule != sm::SpellRuleId::None
                        || armDef->effects[0].row != 0
                        || sm::spell_flavor_count(armDef->cons) != 4
                        || std::strcmp(armDef->cons[3], "2 min recovery") != 0);
    CHECK(ok18, "armageddon effect/flavor metadata wrong");
    if (!ok18) return 1;

    // ...and the two SUSTAINED spells say what they do in the two halves of
    // the hybrid: haste moves a NUMBER the body already has, flight changes a
    // RULE. Neither is a literal at a call site any more.
    const bool ok19 = !(!hasteDef || hasteDef->rule != sm::SpellRuleId::None
                        || hasteDef->effects[0].row != std::uint8_t(sm::BonusId::Spd)
                        || hasteDef->effects[0].value <= 0);
    CHECK(ok19, "haste must be a STAT row: speed is a number a body has");
    if (!ok19) return 1;
    const bool ok20 = !(!flightDef || flightDef->rule != sm::SpellRuleId::Flight
                        || flightDef->effects[0].row != 0);
    CHECK(ok20, "flight must be a RULE: leaving the ground is not a "
                "bigger number");
    if (!ok20) return 1;
    // The magnitude follows the CASTER, through the one door that turns a
    // rank into a multiplier — with its own negative control. Haste's tag
    // (Body) SLEEPS (phase 5, CANON S15), so its school is none and
    // Spellcraft alone carries it — exactly the pre-school law.
    sm::Skills novice{}, adept{};
    adept[sm::SkillId::Spellcraft] = 10;   // 5 %/rank -> x1.5
    const sm::SkillId hasteSchool = sm::spell_school(*hasteDef);
    const bool ok21 = !(hasteSchool != sm::SkillId::Count);
    CHECK(ok21, "Body is a sleeping tag: it must have no school yet");
    if (!ok21) return 1;
    const sm::Bonus weak =
        sm::spell_bonus(hasteDef->effects[0], novice, hasteSchool);
    const sm::Bonus strong =
        sm::spell_bonus(hasteDef->effects[0], adept, hasteSchool);
    const bool ok22 = !(weak.row != strong.row || strong.value <= weak.value);
    CHECK(ok22, "a trained caster's haste must be worth more than a "
                "novice's, and land in the same place");
    if (!ok22) return 1;
    const bool ok23 = !(weak.value != hasteDef->effects[0].value);
    CHECK(ok23, "an untrained caster gets exactly what the row says");
    if (!ok23) return 1;
    // ── Phase 5: the school is the typed half of the damage stack ────────
    // A fire adept's fireball hits harder; water training buys fire nothing
    // (the negative control that says the SPELL'S OWN school is read).
    {
        sm::Attributes caster{};
        caster[sm::AttributeId::Intl] = 10;
        sm::Skills bare{}, fireAdept{}, waterAdept{};
        fireAdept[sm::SkillId::FireMagic] = 50;
        waterAdept[sm::SkillId::WaterMagic] = 50;
        const int plain = sm::spell_damage(*fireDef, caster, bare);
        const int trained = sm::spell_damage(*fireDef, caster, fireAdept);
        const int wrong = sm::spell_damage(*fireDef, caster, waterAdept);
        const bool ok24 = !(!(trained > plain));
        CHECK(ok24, "fire training must strengthen a fire spell");
        if (!ok24) return 1;
        const bool ok25 = !(wrong != plain);
        CHECK(ok25, "negative control: water training buys fire nothing");
        if (!ok25) return 1;
        // ── THE WHOLE FORMULA, stated (CANON S13, session Е 2026-09-19) ──
        // урон = (бросок + добавка) · скилл-процент, and NOTHING else. Two
        // undercanon multipliers stood between the dice and this line until
        // today: `scalingPower` (an author's float over the row's own dice)
        // and `tierPct` (the spire ladder's tier, multiplying the CASTER's
        // attribute). Both are gone, so the expectation is computable in one
        // line from the row and the sheet — and a third knob reappearing
        // reddens here rather than drifting the balance silently.
        const int add = caster.of(sm::AttributeId::Intl);
        const bool ok26 = !(plain != int(fireDef->dice.n) + add);
        CHECK(ok26, "untrained: the expectation is dice + INT, exactly");
        if (!ok26) return 1;
        const int schoolPct = sm::skill_mult_pct(fireAdept,
                                                 sm::SkillId::FireMagic);
        const bool ok27 =
            !(trained != (int(fireDef->dice.n) + add) * schoolPct / 100);
        CHECK(ok27, "trained: the school percent is the ONLY multiplier");
        if (!ok27) return 1;
        // And the TIER buys nothing: armageddon is tier 5, magic_bolt tier 1,
        // and neither tier appears in either expectation.
        const sm::SpellDef* bolt = sm::spell_find("magic_bolt");
        const sm::SpellDef* doom = sm::spell_find("armageddon");
        const bool ok28 = !(!bolt || !doom || bolt->tier == doom->tier);
        CHECK(ok28, "fixture: the two rows must differ in tier");
        if (!ok28) return 1;
        const bool ok29 = !(sm::spell_damage(*bolt, caster, bare) != int(bolt->dice.n) + add
                            || sm::spell_damage(*doom, caster, bare)
                                   != int(doom->dice.n) + add);
        CHECK(ok29, "a spell's TIER is spire gating, never damage");
        if (!ok29) return 1;
    }
    // Both are castable on the map because their ROW says so — the old test
    // read a macro-effect enum instead, which is the field that had to agree
    // with `hasMacro` and had no mechanism to.
    const bool ok30 = !(!hasteDef->hasMacro || !hasteDef->sustained
                        || std::strcmp(hasteDef->pros[2], "Works on world map") != 0);
    CHECK(ok30, "haste world-map metadata wrong");
    if (!ok30) return 1;
    const bool ok31 = !(!flightDef->hasMacro || !flightDef->sustained
                        || std::strcmp(flightDef->cons[2], "Blocked indoors") != 0);
    CHECK(ok31, "flight world-map metadata wrong");
    if (!ok31) return 1;

    sm::spellbook_learn(book, sm::spell_ordinal("magic_bolt"));
    const bool ok32 = !(!sm::spellbook_cast(world, book, combat, attributes, skills,
                                            sm::spell_ordinal("magic_bolt"), sm::sub::ObjRef{}, 100.0f, 100.0f, 0.0f,
                                            1.0f, 0.0f, 0.0f, true));
    CHECK(ok32, "magic_bolt cast rejected");
    if (!ok32) return 1;
    const bool ok33 = !(combat.mp != 1990);
    CHECK(ok33, "magic_bolt mana cost");
    if (!ok33) return 1;
    const bool ok34 = !(projectile_count(world) != 1);
    CHECK(ok34, "magic_bolt projectile spawn");
    if (!ok34) return 1;
    sm::ecs::Projectile magicBolt{};
    sm::ecs::Position magicBoltPos{};
    const bool ok35 = !(!find_projectile_pose_by_spell(world, "magic_bolt",
                                                       magicBolt, magicBoltPos));
    CHECK(ok35, "magic_bolt descriptor missing");
    if (!ok35) return 1;
    const bool ok36 = !(magicBolt.kind != sm::ecs::Projectile::Bolt
                        || magicBolt.blastRadius != 0.0f
                        || !nearf(magicBolt.radius, 1.5f)
                        // WHAT THE MUZZLE IS FOR, stated as the law instead of as the number
                        // it happens to produce: the bolt is born clear of the caster's own
                        // shell AND of its own body, so it can never detonate on the one who
                        // cast it. The old assertion restated 5.0 = 1.5 + 1.5 + 2, and that
                        // first 1.5 was ONE constant standing in for every caster's shell —
                        // the same constant that, through ecs::BodyRadius, had become the
                        // player's physical width. The shell belongs to the body now, so an
                        // expectation that names a width names the wrong thing.
                        //
                        // ЭТОТ каст — БЕЗ кастера (ObjRef{}), и шелл берётся из того же
                        // имени, из которого его берёт продакшен в этом случае
                        // (`casterSlot >= 0 ? slot_body_radius(...) : kBodyRadiusFallback`,
                        // spell_book.cpp). Прежний носитель — `slot_of(reg, entt::entity(0))`
                        // — умер с сущностями тел (ломоть D), а сущности 0 в этом мире и не
                        // было: дверь читалась по слоту −1.
                        || !(magicBoltPos.x - 100.0f
                             > sm::sub::kBodyRadiusFallback + magicBolt.radius)
                        || !nearf(magicBoltPos.y, 100.0f)
                        || !nearf(magicBoltPos.y, 100.0f)
                        || !nearf(magicBolt.maxLifeTimer, 3.0f));
    CHECK(ok36, "magic_bolt descriptor wrong");
    if (!ok36) return 1;
    const sm::CastCheck macroBolt =
        sm::spellbook_can_cast_ex(book, combat, sm::spell_ordinal("magic_bolt"),
                                  false, 0u);
    const bool ok37 =
        !(macroBolt.ok || macroBolt.reason != "Cannot use on world map");
    CHECK(ok37, "magic_bolt macro gate failed");
    if (!ok37) return 1;

    // ── THE recovery law (owner verdict 2026-09-09) ──────────────────────
    // A cast charges the CASTER BODY's one gate — ecs::Combat::recoverySteps,
    // the same field a sword swing charges — priced by the row's `recovery`
    // through the S14 door. No per-spell timer exists anywhere, and a busy
    // body starts NO action, whatever occupied it.
    sm::spellbook_learn(book, sm::spell_ordinal("fireball"));
    const int casterSlot = sm::test::give_body(world.reg);
    sm::test::give_pos(world.reg, casterSlot, 100.0f, 100.0f, 0.0f);
    sm::test::give_combat(world.reg, casterSlot, sm::ecs::Combat{});
    // Кастер — ссылка сцены (iii-б): слот тела через ту же дверь, что у
    // прод-писателя.
    const sm::sub::ObjRef casterId = sm::sub::ref_of(
        sm::sub::objects_of(world.reg), casterSlot);
    const bool ok38 = !(!sm::spellbook_cast(world, book, combat, attributes, skills,
                                            sm::spell_ordinal("fireball"), casterId, 100.0f, 100.0f, 0.0f,
                                            1.0f, 0.0f, 0.0f, true));
    CHECK(ok38, "fireball cast rejected");
    if (!ok38) return 1;
    const std::uint32_t fireGate =
        sm::sub::slot_combat(sm::test::arena_of(world.reg), casterSlot)->recoverySteps;
    const auto wantGate = std::uint32_t(sm::recovery_steps(
        fireDef->recovery, attributes, skills, sm::SkillId::Spellcraft));
    const bool ok39 = !(fireGate == 0u || fireGate != wantGate);
    CHECK(ok39, "fireball recovery did not charge the body gate");
    if (!ok39) return 1;
    // Agnostic: the gate blocks even the no-recovery bolt, and the reason
    // speaks recovery, not the spell that caused it.
    const sm::CastCheck fireBlocked = sm::spellbook_can_cast_ex(
        book, combat, sm::spell_ordinal("magic_bolt"), true, fireGate);
    const bool ok40 = !(fireBlocked.ok || fireBlocked.recoveryRemaining <= 0.0f
                        || fireBlocked.reason.rfind("Recovery", 0) != 0);
    CHECK(ok40, "busy body was allowed to act");
    if (!ok40) return 1;
    const int busyProjectiles = projectile_count(world);
    const bool ok41 = !(sm::spellbook_cast(world, book, combat, attributes, skills,
                                           sm::spell_ordinal("magic_bolt"), casterId, 100.0f, 100.0f, 0.0f,
                                           1.0f, 0.0f, 0.0f, true)
                        || projectile_count(world) != busyProjectiles);
    CHECK(ok41, "busy body cast through the gate");
    if (!ok41) return 1;
    // The ONE drain — the same system that frees a swordarm frees the caster
    // (проход по арене с куска 2).
    sm::sub::tick_body_recovery(sm::test::arena_of(world.reg), fireGate);
    const bool ok42 =
        !(sm::sub::slot_combat(sm::test::arena_of(world.reg), casterSlot)->recoverySteps != 0u);
    CHECK(ok42, "tick_body_recovery did not drain the gate");
    if (!ok42) return 1;

    sm::ecs::Projectile fireball{};
    const bool ok43 = !(!find_projectile_by_spell(world, "fireball", fireball));
    CHECK(ok43, "fireball descriptor missing");
    if (!ok43) return 1;
    const bool ok44 = !(fireball.kind != sm::ecs::Projectile::Bolt
                        || !nearf(fireball.radius, 2.5f)
                        || !nearf(fireball.blastRadius, 48.0f)
                        || fireball.explodeOnExpiry
                        || !nearf(fireball.maxLifeTimer, 3.0f));
    CHECK(ok44, "fireball descriptor wrong");
    if (!ok44) return 1;

    sm::ecs::World fireExpiryWorld;

    auto fireExpiryWorldStore_ = sm::make_macro_store();

    sm::store_attach(fireExpiryWorld, fireExpiryWorldStore_.get());

    // Арена — предусловие снарядов (ступень (v): чистые жильцы

    // арены; §8 п.11 — свидетель сам рожает предусловие).

    sm::test::arena_of(fireExpiryWorld.reg);
    sm::SpellBook fireExpiryBook;
    sm::ecs::Pools fireExpiryCombat{};
    fireExpiryCombat.mp = 1000;
    fireExpiryCombat.maxMp = 1000;
    sm::spellbook_learn(fireExpiryBook, sm::spell_ordinal("fireball"));
    const auto fireExpiryVictim =
        add_target(fireExpiryWorld, 120.0f, 100.0f, 100.0f, false);
    const bool ok45 = !(!sm::spellbook_cast(fireExpiryWorld, fireExpiryBook,
                                            fireExpiryCombat, attributes, skills,
                                            sm::spell_ordinal("fireball"), sm::sub::ObjRef{}, 100.0f, 100.0f, 0.0f,
                                            1.0f, 0.0f, 0.0f, true));
    CHECK(ok45, "fireball expiry setup cast rejected");
    if (!ok45) return 1;
    sm::sub::tick_spell_projectiles(fireExpiryWorld, nullptr, 3.1f,
                                   &sm::test::all_live_slots,
                                   &sm::test::arena_of(fireExpiryWorld.reg));
    const bool ok46 = !(!nearf(hp_of(fireExpiryWorld, fireExpiryVictim), 100.0f)
                        || projectile_count(fireExpiryWorld) != 0);
    CHECK(ok46,
          "a fireball that ran out of life EXPIRES, it does not detonate");
    if (!ok46) return 1;

    sm::ecs::World blastWorld;

    auto blastWorldStore_ = sm::make_macro_store();

    sm::store_attach(blastWorld, blastWorldStore_.get());

    // Арена — предусловие снарядов (ступень (v): чистые жильцы

    // арены; §8 п.11 — свидетель сам рожает предусловие).

    sm::test::arena_of(blastWorld.reg);
    // Снаряд — чистый жилец арены (ступень (v)): рождение дверью выстрела,
    // сущности нет; позиция — аргумент рождения.
    sm::test::give_projectile(blastWorld.reg,
        sm::ecs::Projectile{
            .vx = 0.0f, .vy = 0.0f, .vz = 0.0f, .radius = 2.5f,
            .lifeTimer = 0.0f, .maxLifeTimer = 0.0f, .damage = 10,
            .blastRadius = 48.0f, .originX = 0.0f, .originY = 0.0f,
            .beamLength = 0.0f, .chainDecayPct = std::uint8_t(0),
            .ownerSlot = 0, .chainRadius = 0.0f,
            .spellId = sm::stable_spell_id("fireball"),
            .ownerId = 0u,   // «ничей» взрыв — id 0 (iii-б)
            .chainRemaining = std::int16_t{0},
            .kind = sm::ecs::Projectile::Bolt,
            .visualOnly = true, .explodeOnExpiry = true,
            .dmgType = std::uint8_t(sm::DamageType::Fire), .critical = false},
        sm::ecs::Position{0.0f, 0.0f, 0.0f});
    const auto blastEdge =
        add_target(blastWorld, 48.0f, 0.0f, 100.0f, false);
    const auto blastOutside =
        add_target(blastWorld, 53.0f, 0.0f, 100.0f, false);
    sm::sub::tick_spell_projectiles(blastWorld, nullptr, 0.0f,
                                   &sm::test::all_live_slots,
                                   &sm::test::arena_of(blastWorld.reg));
    const bool ok47 = !(!(hp_of(blastWorld, blastEdge) < 100.0f)
                        || !nearf(hp_of(blastWorld, blastOutside), 100.0f)
                        || projectile_count(blastWorld) != 0);
    CHECK(ok47,
          "the blast reaches a target at exactly blastRadius and no further - the edge is measured centre to centre");
    if (!ok47) return 1;

    sm::ecs::World iceWorld;

    auto iceWorldStore_ = sm::make_macro_store();

    sm::store_attach(iceWorld, iceWorldStore_.get());

    // Арена — предусловие снарядов (ступень (v): чистые жильцы

    // арены; §8 п.11 — свидетель сам рожает предусловие).

    sm::test::arena_of(iceWorld.reg);
    sm::SpellBook iceBook;
    sm::ecs::Pools iceCombat{};
    iceCombat.mp = 1000;
    iceCombat.maxMp = 1000;
    sm::spellbook_learn(iceBook, sm::spell_ordinal("ice_shard"));
    const bool ok48 = !(!sm::spellbook_cast(iceWorld, iceBook, iceCombat, attributes, skills,
                                            sm::spell_ordinal("ice_shard"), sm::sub::ObjRef{}, 10.0f, 10.0f, 0.0f,
                                            1.0f, 0.0f, 0.0f, true));
    CHECK(ok48, "ice_shard cast rejected");
    if (!ok48) return 1;
    sm::ecs::Projectile ice{};
    const bool ok49 = !(!find_projectile_by_spell(iceWorld, "ice_shard", ice)
                        || ice.kind != sm::ecs::Projectile::Bolt
                        || !nearf(ice.radius, 1.5f)
                        || ice.blastRadius != 0.0f
                        || !nearf(ice.maxLifeTimer, 3.0f));
    CHECK(ok49, "ice_shard descriptor wrong");
    if (!ok49) return 1;

    sm::SpellBook lowBook;
    sm::spellbook_learn(lowBook, sm::spell_ordinal("fireball"));
    sm::ecs::Pools lowCombat{};
    lowCombat.mp = 0;
    lowCombat.maxMp = 10;
    const sm::CastCheck lowMana =
        sm::spellbook_can_cast_ex(lowBook, lowCombat, sm::spell_ordinal("fireball"),
                                  true, 0u);
    const bool ok50 = !(lowMana.ok || lowMana.reason != "Not enough mana");
    CHECK(ok50, "low mana gate failed");
    if (!ok50) return 1;

    sm::SpellBook macroBook;
    const char* macroSpellIds[] = {
        "fireball", "ice_shard", "lightning_chain",
        "armageddon", "haste", "flight",
    };
    for (const char* id : macroSpellIds) {
        sm::spellbook_learn(macroBook, sm::spell_ordinal(id));
    }
    sm::ecs::Pools macroCombat{};
    macroCombat.mp = 2000;
    macroCombat.maxMp = 2000;
    for (const char* id : macroSpellIds) {
        const sm::CastCheck macroCheck =
            sm::spellbook_can_cast_ex(macroBook, macroCombat, sm::spell_ordinal(id),
                                      false, 0u);
        const bool ok51 = !(!macroCheck.ok);
        CHECK(ok51,
              "on the macro map can_cast still answers YES - the restriction lives in cast, not in the question");
        if (!ok51) return 1;
    }
    const int beforeMacroProjectiles = projectile_count(world);
    const bool ok52 = !(sm::spellbook_cast(world, macroBook, macroCombat, attributes, skills,
                                           sm::spell_ordinal("fireball"), sm::sub::ObjRef{}, 100.0f, 100.0f, 0.0f,
                                           1.0f, 0.0f, 0.0f, false));
    CHECK(ok52, "world-map fireball spawned micro projectile");
    if (!ok52) return 1;
    const bool ok53 = !(projectile_count(world) != beforeMacroProjectiles
                        || macroCombat.mp != 2000);
    CHECK(ok53, "world-map fireball mutated state");
    if (!ok53) return 1;

    sm::spellbook_learn(book, sm::spell_ordinal("energy_beam"));
    const bool ok54 = !(!sm::spellbook_cast(world, book, combat, attributes, skills,
                                            sm::spell_ordinal("energy_beam"), sm::sub::ObjRef{}, 100.0f, 100.0f, 0.0f,
                                            1.0f, 0.0f, 0.0f, true));
    CHECK(ok54, "energy_beam cast rejected");
    if (!ok54) return 1;
    bool beamFound = false;
    {
        const sm::sub::SubObjects& bo = sm::sub::objects_of(world.reg);
        for (int s = 0; s < int(sm::kMaxSubObjects); ++s) {
            const std::uint16_t f = bo.flags[std::size_t(s)];
            if ((f & sm::sub::kObjAlive) == 0u
                || (f & sm::sub::kObjProjectile) == 0u) continue;
            const auto& p = bo.projectile[std::size_t(s)];
            if (p.kind == sm::ecs::Projectile::Beam
                && p.visualOnly
                && p.beamLength > 0.0f) {
                beamFound = true;
            }
        }
    }
    const bool ok55 = !(!beamFound);
    CHECK(ok55, "energy_beam did not spawn beam visual");
    if (!ok55) return 1;
    sm::ecs::Projectile beamDesc{};
    const bool ok56 = !(!find_projectile_by_spell(world, "energy_beam", beamDesc)
                        || !nearf(beamDesc.radius, 1.5f)
                        // The beam's origin obeys the same muzzle law as a bolt, and is
                        // asserted as that law rather than as the number it came to.
                        // Кастера у этого каста нет (ObjRef{}) — шелл тем же именем, что
                        // у болта выше (ломоть D: сущности 0 больше не существует).
                        || !(beamDesc.originX - 100.0f
                             > sm::sub::kBodyRadiusFallback + beamDesc.radius)
                        || !nearf(beamDesc.originY, 100.0f));
    CHECK(ok56,
          "the beam is born OUTSIDE the caster body - muzzle clear of body_radius plus its own");
    if (!ok56) return 1;

    sm::spellbook_learn(book, sm::spell_ordinal("lightning_chain"));
    const bool ok57 = !(!sm::spellbook_cast(world, book, combat, attributes, skills,
                                            sm::spell_ordinal("lightning_chain"), sm::sub::ObjRef{}, 100.0f, 100.0f, 0.0f,
                                            1.0f, 0.0f, 0.0f, true));
    CHECK(ok57, "lightning_chain cast rejected");
    if (!ok57) return 1;
    sm::ecs::Projectile chain{};
    const bool ok58 =
        !(!find_projectile_by_spell(world, "lightning_chain", chain));
    CHECK(ok58, "lightning_chain descriptor missing");
    if (!ok58) return 1;
    // WHAT THIS ASSERTS, AND WHAT IT DELIBERATELY DOES NOT.
    //
    // Lightning Chain flies; it does not chain. `emplace_projectile`
    // (registry.cpp) hardcodes chainRemaining/chainDecay/chainRadius to zero
    // and never feeds them from the row, which declares chainCount 4 and
    // chainDecay 0.70 — so `apply_spell_chain` returns on its first line and
    // the spell is a plain bolt (history/audit.md III.15, problems.md §19.4).
    //
    // Until 2026-08-06 this block REQUIRED those three zeros. That made a
    // green ctest the proof that the feature is missing: wiring the chain up
    // would have turned this test red, and the next reader would have assumed
    // they broke something. A test may guard a behaviour; it must never guard
    // a defect. The chain fields are therefore asserted NEITHER way here —
    // when the feature is wired, its own test comes with it.
    //
    // What IS promised is asserted, and asserted against the registry row
    // rather than against restated numbers: the row is the single source of
    // truth, so retuning the spell retunes the expectation with it.
    const bool ok59 = !(chain.kind != sm::ecs::Projectile::Bolt);
    CHECK(ok59, "lightning_chain must spawn a bolt");
    if (!ok59) return 1;
    const bool ok60 = !(!nearf(chain.radius, chainDef->projectileRadius));
    CHECK(ok60, "the bolt's radius must be the one the registry row states");
    if (!ok60) return 1;
    const bool ok61 =
        !(!(chain.maxLifeTimer > 0.0f) || !nearf(chain.lifeTimer, chain.maxLifeTimer));
    CHECK(ok61, "a freshly cast bolt starts with a full life timer");
    if (!ok61) return 1;
    // Cast direction was (1, 0, 0): the bolt travels along the aim vector, at
    // whatever speed the spawner gives it.
    const bool ok62 =
        !(!(chain.vx > 0.0f) || !nearf(chain.vy, 0.0f) || !nearf(chain.vz, 0.0f));
    CHECK(ok62, "the bolt must fly along the direction it was aimed");
    if (!ok62) return 1;

    sm::spellbook_learn(book, sm::spell_ordinal("haste"));
    const bool ok63 = !(!sm::spellbook_cast(world, book, combat, attributes, skills,
                                            sm::spell_ordinal("haste"), sm::sub::ObjRef{}, 0.0f, 0.0f, 0.0f,
                                            1.0f, 0.0f, 0.0f, true));
    CHECK(ok63, "haste toggle rejected");
    if (!ok63) return 1;
    const bool ok64 =
        !(!sm::spellbook_has_sustained(book, sm::spell_ordinal("haste")));
    CHECK(ok64, "haste not sustained");
    if (!ok64) return 1;
    const int beforeDrain = combat.mp;
    sm::spellbook_tick(book, combat, sm::steps_from_seconds(1.2f));
    const bool ok65 = !(beforeDrain - combat.mp != 12);
    CHECK(ok65, "haste fractional drain wrong");
    if (!ok65) return 1;
    const bool ok66 =
        !(!sm::spellbook_has_sustained(book, sm::spell_ordinal("haste")));
    CHECK(ok66, "haste dropped while mana remained");
    if (!ok66) return 1;
    combat.mp = 1;
    sm::spellbook_tick(book, combat, sm::steps_from_seconds(1.0f));
    const bool ok67 =
        !(combat.mp != 0 || sm::spellbook_has_sustained(book, sm::spell_ordinal("haste")));
    CHECK(ok67, "haste did not stop on mana depletion");
    if (!ok67) return 1;

    sm::SpellBook zeroTickBook;
    sm::spellbook_learn(zeroTickBook, sm::spell_ordinal("haste"));
    sm::ecs::Pools zeroTickCombat{};
    zeroTickCombat.mp = 1;
    zeroTickCombat.maxMp = 1;
    const bool ok68 = !(!sm::spellbook_cast(world, zeroTickBook, zeroTickCombat,
                                            attributes, skills, sm::spell_ordinal("haste"), sm::sub::ObjRef{},
                                            0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, true));
    CHECK(ok68, "zero-mp sustained setup rejected");
    if (!ok68) return 1;
    // The PROPERTY, not a pinned boundary: a sustained spell burns the pool one
    // step at a time and stays up for exactly as long as there is mana, then
    // drops on the first step after it runs out. This used to be two magic
    // durations (0.10 s then 0.05 s) chosen to land exactly on the drain's
    // float boundary — a test that broke the moment the clock became integer
    // and proved nothing about intent either way.
    int stepsToEmpty = 0;
    while (zeroTickCombat.mp > 0 && stepsToEmpty < int(sm::kStepsPerSecond)) {
        sm::spellbook_tick(zeroTickBook, zeroTickCombat, 1u);
        ++stepsToEmpty;
    }
    const bool ok69 = !(zeroTickCombat.mp != 0);
    CHECK(ok69, "sustained drain never emptied a one-point pool");
    if (!ok69) return 1;
    const bool ok70 = !(stepsToEmpty <= 0);
    CHECK(ok70, "the pool emptied without a single step being taken");
    if (!ok70) return 1;
    const bool ok71 =
        !(!sm::spellbook_has_sustained(zeroTickBook, sm::spell_ordinal("haste")));
    CHECK(ok71, "sustained spell dropped while it was still being paid for");
    if (!ok71) return 1;
    sm::spellbook_tick(zeroTickBook, zeroTickCombat, 1u);
    const bool ok72 =
        !(sm::spellbook_has_sustained(zeroTickBook, sm::spell_ordinal("haste")));
    CHECK(ok72, "zero-mp sustained spell survived positive drain tick");
    if (!ok72) return 1;

    sm::SpellBook flightBook;
    sm::spellbook_learn(flightBook, sm::spell_ordinal("flight"));
    sm::ecs::Pools flightCombat{};
    flightCombat.mp = 50;
    flightCombat.maxMp = 50;
    const bool ok73 = !(!sm::spellbook_cast(world, flightBook, flightCombat, attributes, skills,
                                            sm::spell_ordinal("flight"), sm::sub::ObjRef{}, 0.0f, 0.0f, 0.0f,
                                            0.0f, 1.0f, 0.0f, false));
    CHECK(ok73, "flight macro toggle rejected");
    if (!ok73) return 1;
    const bool ok74 =
        !(!sm::spellbook_has_sustained(flightBook, sm::spell_ordinal("flight")));
    CHECK(ok74, "flight not sustained");
    if (!ok74) return 1;
    sm::spellbook_tick(flightBook, flightCombat, sm::steps_from_seconds(0.5f));
    const bool ok75 = !(flightCombat.mp != 40);
    CHECK(ok75, "flight drain wrong");
    if (!ok75) return 1;

    sm::SpellBook multiSustainBook;
    sm::ecs::Pools multiSustainCombat{};
    multiSustainCombat.mp = 25;
    multiSustainCombat.maxMp = 25;
    sm::spellbook_learn(multiSustainBook, sm::spell_ordinal("haste"));
    sm::spellbook_learn(multiSustainBook, sm::spell_ordinal("flight"));
    const bool ok76 = !(!sm::spellbook_cast(world, multiSustainBook, multiSustainCombat,
                                            attributes, skills, sm::spell_ordinal("haste"), sm::sub::ObjRef{},
                                            0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, true)
                        || !sm::spellbook_cast(world, multiSustainBook, multiSustainCombat,
                                               attributes, skills, sm::spell_ordinal("flight"), sm::sub::ObjRef{},
                                               0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, true));
    CHECK(ok76, "multi sustained setup cast rejected");
    if (!ok76) return 1;
    sm::spellbook_tick(multiSustainBook, multiSustainCombat,
                       sm::steps_from_seconds(1.0f));
    const bool ok77 = !(multiSustainCombat.mp != 0
                        || sm::spellbook_has_sustained(multiSustainBook, sm::spell_ordinal("haste"))
                        || !sm::spellbook_has_sustained(multiSustainBook, sm::spell_ordinal("flight"))
                        || [&]{ int n = 0;
                                for (int i = 0; i < sm::kSpellCount; ++i)
                                    n += sm::spellbook_has_sustained(multiSustainBook, i)
                                             ? 1 : 0;
                                return n; }() != 1);
    CHECK(ok77, "multi sustained partial depletion wrong");
    if (!ok77) return 1;

    sm::ecs::World hitWorld;

    auto hitWorldStore_ = sm::make_macro_store();

    sm::store_attach(hitWorld, hitWorldStore_.get());

    // Арена — предусловие снарядов (ступень (v): чистые жильцы

    // арены; §8 п.11 — свидетель сам рожает предусловие).

    sm::test::arena_of(hitWorld.reg);
    sm::SpellBook hitBook;
    sm::ecs::Pools hitCombat{};
    hitCombat.mp = 1000;
    hitCombat.maxMp = 1000;
    sm::spellbook_learn(hitBook, sm::spell_ordinal("magic_bolt"));
    // Agnostic projectiles (owner design decision 2026-07-30): there is NO
    // friendly filter — a player bolt flying through the player's own soldier
    // CONNECTS. Friendly fire is real; watch where you aim.
    const auto hitPlayer = add_player(hitWorld, -1000.0f, -1000.0f);
    const auto friendly = add_target(hitWorld, 104.0f, 100.0f, 100.0f, true);
    const bool ok78 = !(!sm::spellbook_cast(hitWorld, hitBook, hitCombat, attributes, skills,
                                            sm::spell_ordinal("magic_bolt"), hitPlayer, 0.0f, 100.0f, 0.0f,
                                            1.0f, 0.0f, 0.0f, true));
    CHECK(ok78, "magic_bolt agnostic-hit setup cast rejected");
    if (!ok78) return 1;
    sm::sub::tick_spell_projectiles(hitWorld, nullptr, 0.25f,
                                   &sm::test::all_live_slots,
                                   &sm::test::arena_of(hitWorld.reg));
    const bool ok79 = !(nearf(hp_of(hitWorld, friendly), 100.0f)
                        || projectile_count(hitWorld) != 0);
    CHECK(ok79,
          "agnostic projectiles: bolt must strike a friendly in its path");
    if (!ok79) return 1;
    // The bolt was consumed by the friendly it struck; the hostile hit runs in
    // its own fresh world (fresh cooldown, clear lane) — the stages no longer
    // share one projectile now that nothing flies through a friendly.
    sm::ecs::World hostWorld;
    auto hostWorldStore_ = sm::make_macro_store();
    sm::store_attach(hostWorld, hostWorldStore_.get());
    // Арена — предусловие снарядов (ступень (v): чистые жильцы
    // арены; §8 п.11 — свидетель сам рожает предусловие).
    sm::test::arena_of(hostWorld.reg);
    sm::SpellBook hostBook;
    sm::ecs::Pools hostCombat{};
    hostCombat.mp = 1000;
    hostCombat.maxMp = 1000;
    sm::spellbook_learn(hostBook, sm::spell_ordinal("magic_bolt"));
    const auto hostPlayer = add_player(hostWorld, -1000.0f, -1000.0f);
    const auto hostile = add_target(hostWorld, 204.0f, 100.0f, 100.0f, false);
    const bool ok80 = !(!sm::spellbook_cast(hostWorld, hostBook, hostCombat, attributes, skills,
                                            sm::spell_ordinal("magic_bolt"), hostPlayer, 0.0f, 100.0f, 0.0f,
                                            1.0f, 0.0f, 0.0f, true));
    CHECK(ok80, "magic_bolt hostile-hit setup cast rejected");
    if (!ok80) return 1;
    sm::sub::tick_spell_projectiles(hostWorld, nullptr, 0.25f,
                                   &sm::test::all_live_slots,
                                   &sm::test::arena_of(hostWorld.reg));
    sm::sub::tick_spell_projectiles(hostWorld, nullptr, 0.25f,
                                   &sm::test::all_live_slots,
                                   &sm::test::arena_of(hostWorld.reg));
    const bool ok81 = !(!(hp_of(hostWorld, hostile) < 100.0f)
                        || !last_hit_by(hostWorld, hostile, hostPlayer)
                        || projectile_count(hostWorld) != 0);
    CHECK(ok81, "magic_bolt did not hit hostile target");
    if (!ok81) return 1;

    sm::ecs::World beamWorld;

    auto beamWorldStore_ = sm::make_macro_store();

    sm::store_attach(beamWorld, beamWorldStore_.get());

    // Арена — предусловие снарядов (ступень (v): чистые жильцы

    // арены; §8 п.11 — свидетель сам рожает предусловие).

    sm::test::arena_of(beamWorld.reg);
    sm::SpellBook beamBook;
    sm::ecs::Pools beamCombat{};
    beamCombat.mp = 1000;
    beamCombat.maxMp = 1000;
    sm::spellbook_learn(beamBook, sm::spell_ordinal("energy_beam"));
    const auto beamPlayer = add_player(beamWorld, -1000.0f, -1000.0f);
    const auto beamHit = add_target(beamWorld, 160.0f, 10.0f, 100.0f, false);
    const auto beamNearMiss = add_target(beamWorld, 160.0f, 21.0f, 100.0f, false);
    const auto beamMiss = add_target(beamWorld, 160.0f, 40.0f, 100.0f, false);
    const bool ok82 = !(!sm::spellbook_cast(beamWorld, beamBook, beamCombat, attributes, skills,
                                            sm::spell_ordinal("energy_beam"), beamPlayer, 0.0f, 10.0f, 0.0f,
                                            1.0f, 0.0f, 0.0f, true));
    CHECK(ok82, "energy_beam effect setup cast rejected");
    if (!ok82) return 1;
    sm::sub::tick_spell_projectiles(beamWorld, nullptr, 0.40f,
                                   &sm::test::all_live_slots,
                                   &sm::test::arena_of(beamWorld.reg));
    const bool ok83 = !(!(hp_of(beamWorld, beamHit) < 100.0f)
                        || !last_hit_by(beamWorld, beamHit, beamPlayer)
                        || !nearf(hp_of(beamWorld, beamNearMiss), 100.0f)
                        || !nearf(hp_of(beamWorld, beamMiss), 100.0f)
                        || projectile_count(beamWorld) != 0);
    CHECK(ok83, "energy_beam line damage wrong");
    if (!ok83) return 1;

    sm::ecs::World chainWorld;

    auto chainWorldStore_ = sm::make_macro_store();

    sm::store_attach(chainWorld, chainWorldStore_.get());

    // Арена — предусловие снарядов (ступень (v): чистые жильцы

    // арены; §8 п.11 — свидетель сам рожает предусловие).

    sm::test::arena_of(chainWorld.reg);
    sm::SpellBook chainBook;
    sm::ecs::Pools chainCombat{};
    chainCombat.mp = 1000;
    chainCombat.maxMp = 1000;
    sm::spellbook_learn(chainBook, sm::spell_ordinal("lightning_chain"));
    const auto chainPlayer = add_player(chainWorld, -1000.0f, -1000.0f);
    // The FRIENDLY stands nearest the caster, the hostile behind him. That
    // ordering is the point: the bolt reaches the ally first, so striking him
    // proves faction plays no part. It used to be the other way round, and the
    // ally was hit only because the old point-sampled collision happened to
    // land nearer 113 than 109 — the test was resting on a sampling artefact,
    // which the swept collision (spell_effects.cpp find_projectile_hit) removed.
    const auto chainFriendly = add_target(chainWorld, 109.0f, 50.0f, 100.0f, true);
    const auto chainFirst = add_target(chainWorld, 113.0f, 50.0f, 100.0f, false);
    const auto chainSecond = add_target(chainWorld, 120.0f, 50.0f, 100.0f, false);
    const bool ok84 = !(!sm::spellbook_cast(chainWorld, chainBook, chainCombat, attributes, skills,
                                            sm::spell_ordinal("lightning_chain"), chainPlayer, 0.0f, 50.0f, 0.0f,
                                            1.0f, 0.0f, 0.0f, true));
    CHECK(ok84, "lightning_chain effect setup cast rejected");
    if (!ok84) return 1;
    sm::sub::tick_spell_projectiles(chainWorld, nullptr, 0.35f,
                                   &sm::test::all_live_slots,
                                   &sm::test::arena_of(chainWorld.reg));
    // Agnostic projectiles (owner design decision 2026-07-30): the bolt strikes
    // the body it REACHES FIRST — faction plays no part. Here that is the
    // friendly at x=109; with a faction shield he would be filtered out of the
    // candidates and the hostile behind him would take the hit instead. One
    // victim, bolt consumed, the bodies further along untouched.
    const bool ok85 = !(!(hp_of(chainWorld, chainFriendly) < 100.0f)
                        || !nearf(hp_of(chainWorld, chainFirst), 100.0f)
                        || !nearf(hp_of(chainWorld, chainSecond), 100.0f)
                        || projectile_count(chainWorld) != 0);
    CHECK(ok85, "lightning_chain agnostic collision wrong");
    if (!ok85) return 1;

    sm::ecs::World armWorld;

    auto armWorldStore_ = sm::make_macro_store();

    sm::store_attach(armWorld, armWorldStore_.get());

    // Арена — предусловие снарядов (ступень (v): чистые жильцы

    // арены; §8 п.11 — свидетель сам рожает предусловие).

    sm::test::arena_of(armWorld.reg);
    sm::SpellBook armBook;
    sm::ecs::Pools armCombat{};
    armCombat.mp = 2000;
    armCombat.maxMp = 2000;
    sm::spellbook_learn(armBook, sm::spell_ordinal("armageddon"));
    const bool ok86 = !(!armDef);
    CHECK(ok86, "armageddon definition missing");
    if (!ok86) return 1;
    sm::Attributes armAttributes{};
    armAttributes[sm::AttributeId::Intl] = 200;
    const bool ok87 = !(sm::spell_radius(*armDef, armAttributes, skills)
                        <= int(armDef->baseRadius));
    CHECK(ok87, "armageddon high-radius test does not exceed base radius");
    if (!ok87) return 1;
    // `expectedMeteors` used to come from `armageddon_meteor_count()`, a
    // VERBATIM copy of spawn_armageddon's formula living in this file
    // (ceil(spread * 0.2f), floored at 16 — with kArmageddonMinMeteors
    // respelled as the literal 16), checked against a hardcoded 32. Both
    // halves were defects of the same kind: a test that recomputes what the
    // code computes proves only that the copy was typed correctly (§8 п.5),
    // and 32 is a number with no source but today's run (§8 п.4). Retune the
    // spread or the floor by design and the witness reddens for no defect.
    //
    // What is asserted instead is what the spell OWES and what no retune can
    // take away: armageddon falls as a STORM. The exact count is the spawner's
    // business. (The floor itself — kArmageddonMinMeteors — cannot be read
    // from here: it lives in an anonymous namespace in effects.cpp. Exposing
    // it so this could assert the floor from its ONE source is part of M-132.)
    constexpr int kStormAtLeast = 2;
    const int armBefore = armCombat.mp;
    const float armRngValues[] = {0.0f, 0.5f, 0.25f};
    SeqRng armRng{armRngValues, 3, 0};
    const auto armPlayer = add_player(armWorld, -1000.0f, -1000.0f);
    const bool ok88 = !(!sm::spellbook_cast(armWorld, armBook, armCombat, armAttributes, skills,
                                            sm::spell_ordinal("armageddon"), armPlayer, 100.0f, 100.0f, 0.0f,
                                            1.0f, 0.0f, 0.0f, true,
                                            &seq_rng01, &armRng));
    CHECK(ok88, "armageddon cast rejected");
    if (!ok88) return 1;
    const bool ok89 = !(armCombat.mp != int(armBefore) - int(armDef->manaCost));
    CHECK(ok89, "armageddon charged exactly the mana its row names");
    if (!ok89) return 1;
    const bool ok90 = !(projectile_count(armWorld) < kStormAtLeast);
    CHECK(ok90, "armageddon falls as a STORM — many meteors, not one bolt");
    if (!ok90) return 1;

    const std::uint32_t armId = sm::stable_spell_id("armageddon");
    int armSeen = 0;
    bool haveMeteorPosition = false;
    float meteorX = 0.0f;
    float meteorY = 0.0f;
    const sm::sub::SubObjects& ao = sm::sub::objects_of(armWorld.reg);
    for (int s = 0; s < int(sm::kMaxSubObjects); ++s) {
        const std::uint16_t f = ao.flags[std::size_t(s)];
        if ((f & sm::sub::kObjAlive) == 0u
            || (f & sm::sub::kObjProjectile) == 0u) continue;
        const auto& pos = ao.pos[std::size_t(s)];
        const auto& p = ao.projectile[std::size_t(s)];
        const bool ok91 = !(p.spellId != armId);
        CHECK(ok91, "armageddon spawned wrong spell id");
        if (!ok91) return 1;
        const bool ok92 = !(p.kind != sm::ecs::Projectile::Bolt
                            || !p.visualOnly
                            || !p.explodeOnExpiry
                            || !nearf(p.blastRadius, 25.0f)
                            || !nearf(p.radius, 2.5f)
                            || p.lifeTimer < 0.3f
                            || p.lifeTimer > 0.8f
                            || !nearf(p.lifeTimer, p.maxLifeTimer));
        CHECK(ok92, "armageddon meteor descriptor wrong");
        if (!ok92) return 1;
        if (!haveMeteorPosition) {
            meteorX = pos.x;
            meteorY = pos.y;
            haveMeteorPosition = true;
        }
        ++armSeen;
    }
    // Three facts, three verdicts — they used to share one string, and one of
    // them (`sawTsRngMeteor`) asked whether a meteor landed on the exact spot
    // the DEAD prototype's RNG would have put it. That is not a law of the
    // spell; it is a fingerprint of an implementation that no longer exists.
    // What survives is that every meteor the storm spawned was inspected, and
    // that the storm has a position to aim a victim at.
    const bool ok93 = !(armSeen != projectile_count(armWorld));
    CHECK(ok93, "every meteor the cast spawned was actually inspected");
    if (!ok93) return 1;
    const bool ok94 = !(!haveMeteorPosition);
    CHECK(ok94, "the storm put a meteor somewhere to read a position from");
    if (!ok94) return 1;
    const auto armVictim =
        add_target(armWorld, meteorX, meteorY, 1000.0f, false);
    sm::sub::tick_spell_projectiles(armWorld, nullptr, 1.0f,
                                   &sm::test::all_live_slots,
                                   &sm::test::arena_of(armWorld.reg));
    const bool ok95 = !(!(hp_of(armWorld, armVictim) < 1000.0f)
                        || !last_hit_by(armWorld, armVictim, armPlayer)
                        || projectile_count(armWorld) != 0);
    CHECK(ok95, "armageddon expiry blast wrong");
    if (!ok95) return 1;

    // ---- Inc 4d: the owner self-exclusion is gone. Whether a caster is
    // caught by its OWN projectile is decided purely by geometry (does the
    // caster stand in the hit region?) — there is no faction shield at all.
    // No player special-casing — the owner is a real scene reference,
    // exactly like an NPC firer's. -----------------------------------
    {
        // (a) Q1 — "свой взрыв ловит и тебя": a player's own AoE blast
        // (blastRadius > 0) catches the player when the player stands in it, and the hit
        // is still attributed player-owned (XP/log route to the player).
        sm::ecs::World selfWorld;
        auto selfWorldStore_ = sm::make_macro_store();
        sm::store_attach(selfWorld, selfWorldStore_.get());
        // Арена — предусловие снарядов (ступень (v): чистые жильцы
        // арены; §8 п.11 — свидетель сам рожает предусловие).
        sm::test::arena_of(selfWorld.reg);
        const int selfPlayer = sm::test::give_body(selfWorld.reg);
        sm::test::give_pos(selfWorld.reg, selfPlayer, 0.0f, 0.0f, 0.0f);
        sm::test::give_pools(selfWorld.reg, selfPlayer, sm::ecs::Pools{100, 100});
        sm::test::make_avatar(selfWorld.reg, selfPlayer);
        const sm::sub::ObjRef selfRef = sm::sub::ref_of(
            sm::sub::objects_of(selfWorld.reg), selfPlayer);
        // Снаряд — чистый жилец арены (ступень (v)): слот без сущности.
        sm::test::give_projectile(selfWorld.reg,
            sm::ecs::Projectile{
                .vx = 0.0f, .vy = 0.0f, .vz = 0.0f, .radius = 2.5f,
                .lifeTimer = 0.0f, .maxLifeTimer = 0.0f, .damage = 10,
                .blastRadius = 48.0f, .originX = 0.0f, .originY = 0.0f,
                .beamLength = 0.0f, .chainDecayPct = std::uint8_t(0),
                .ownerSlot = selfRef.slot, .chainRadius = 0.0f,
                .spellId = sm::stable_spell_id("fireball"),
                .ownerId = selfRef.id,
                .chainRemaining = std::int16_t{0},
                .kind = sm::ecs::Projectile::Bolt,
                .visualOnly = true, .explodeOnExpiry = true,
                .dmgType = std::uint8_t(sm::DamageType::Fire),
                .critical = false},
            sm::ecs::Position{0.0f, 0.0f, 0.0f});
        sm::sub::tick_spell_projectiles(selfWorld, nullptr, 0.0f,
                                   &sm::test::all_live_slots,
                                   &sm::test::arena_of(selfWorld.reg));
        const bool ok96 = !(!(hp_of(selfWorld, selfPlayer) < 100.0f)
                            || !last_hit_by(selfWorld, selfPlayer, selfPlayer));
        CHECK(ok96, "Inc 4d: player AoE blast did not catch its "
                    "own caster (Q1)");
        if (!ok96) return 1;
    }
    {
        // (b) NO faction shield (owner design decision 2026-07-30): projectiles
        // are faction-agnostic and strike whoever stands in their path — the
        // caster's own side included. The muzzle offset (spawn geometry), not
        // any exclusion rule, is all that keeps a live cast off its own shell.
        // This bolt is placed ON its player caster, so it must connect.
        sm::ecs::World shieldWorld;
        auto shieldWorldStore_ = sm::make_macro_store();
        sm::store_attach(shieldWorld, shieldWorldStore_.get());
        // Арена — предусловие снарядов (ступень (v): чистые жильцы
        // арены; §8 п.11 — свидетель сам рожает предусловие).
        sm::test::arena_of(shieldWorld.reg);
        const int shieldPlayer = sm::test::give_body(shieldWorld.reg);
        sm::test::give_pos(shieldWorld.reg, shieldPlayer, 0.0f, 0.0f, 0.0f);
        sm::test::give_pools(shieldWorld.reg, shieldPlayer, sm::ecs::Pools{100, 100});
        sm::test::make_avatar(shieldWorld.reg, shieldPlayer);
        const sm::sub::ObjRef shieldRef = sm::sub::ref_of(
            sm::sub::objects_of(shieldWorld.reg), shieldPlayer);
        sm::test::give_projectile(shieldWorld.reg,
            sm::ecs::Projectile{
                .vx = 0.0f, .vy = 0.0f, .vz = 0.0f, .radius = 1.5f,
                .lifeTimer = 1.0f, .maxLifeTimer = 1.0f, .damage = 10,
                .blastRadius = 0.0f, .originX = 0.0f, .originY = 0.0f,
                .beamLength = 0.0f, .chainDecayPct = std::uint8_t(0),
                .ownerSlot = shieldRef.slot, .chainRadius = 0.0f,
                .spellId = sm::stable_spell_id("magic_bolt"),
                .ownerId = shieldRef.id,
                .chainRemaining = std::int16_t{0},
                .kind = sm::ecs::Projectile::Bolt,
                .visualOnly = false, .explodeOnExpiry = false,
                .dmgType = std::uint8_t(sm::DamageType::Arcane),
                .critical = false},
            sm::ecs::Position{0.0f, 0.0f, 0.0f});
        sm::sub::tick_spell_projectiles(shieldWorld, nullptr, 0.0f,
                                   &sm::test::all_live_slots,
                                   &sm::test::arena_of(shieldWorld.reg));
        const bool ok97 = !(!nearf(hp_of(shieldWorld, shieldPlayer), 90.0f));
        CHECK(ok97, "agnostic projectiles: bolt on its own player caster "
                    "must connect (friendly fire is real)");
        if (!ok97) return 1;
    }
    {
        // (c) Owner-agnostic — no player special-case: an NPC-owned
        // AoE blast catches its OWN NPC caster too, and the hit is
        // NOT player-owned (no XP leaks to the player sheet).
        sm::ecs::World npcWorld;
        auto npcWorldStore_ = sm::make_macro_store();
        sm::store_attach(npcWorld, npcWorldStore_.get());
        // Арена — предусловие снарядов (ступень (v): чистые жильцы
        // арены; §8 п.11 — свидетель сам рожает предусловие).
        sm::test::arena_of(npcWorld.reg);
        const int npcCaster = sm::test::give_body(npcWorld.reg);
        sm::test::give_pos(npcWorld.reg, npcCaster, 0.0f, 0.0f, 0.0f);
        sm::test::give_pools(npcWorld.reg, npcCaster, sm::ecs::Pools{100, 100});
        sm::test::give_kind(npcWorld.reg, npcCaster, sm::ecs::NPCKind{2, 2});
        const sm::sub::ObjRef npcRef = sm::sub::ref_of(
            sm::sub::objects_of(npcWorld.reg), npcCaster);
        sm::test::give_projectile(npcWorld.reg,
            sm::ecs::Projectile{
                .vx = 0.0f, .vy = 0.0f, .vz = 0.0f, .radius = 2.5f,
                .lifeTimer = 0.0f, .maxLifeTimer = 0.0f, .damage = 10,
                .blastRadius = 48.0f, .originX = 0.0f, .originY = 0.0f,
                .beamLength = 0.0f, .chainDecayPct = std::uint8_t(0),
                .ownerSlot = npcRef.slot, .chainRadius = 0.0f,
                .spellId = sm::stable_spell_id("fireball"),
                .ownerId = npcRef.id,
                .chainRemaining = std::int16_t{0},
                .kind = sm::ecs::Projectile::Bolt,
                .visualOnly = true, .explodeOnExpiry = true,
                .dmgType = std::uint8_t(sm::DamageType::Fire),
                .critical = false},
            sm::ecs::Position{0.0f, 0.0f, 0.0f});
        sm::sub::tick_spell_projectiles(npcWorld, nullptr, 0.0f,
                                   &sm::test::all_live_slots,
                                   &sm::test::arena_of(npcWorld.reg));
        const bool ok98 = !(!(hp_of(npcWorld, npcCaster) < 100.0f)
                            || !last_hit_by(npcWorld, npcCaster, npcCaster));
        CHECK(ok98, "Inc 4d: NPC AoE blast self-catch/ownership "
                    "wrong (must hit, attributed to the NPC body)");
        if (!ok98) return 1;
    }

    {
        // A BOLT MUST CONNECT AT EVERY RANGE, point-blank included.
        //
        // The collision used to sample a single POINT after each step. A magic
        // bolt crosses 6.25 units per tick (400 u/s at 1/64 s) while its contact
        // radius against an ordinary body is ~2.7, so the probe landed at 11.25,
        // 17.5, 23.75 … and saw nothing in between: everything nearer than ~8.5
        // units was untouchable — the whole melee band, kPlayerMeleeRange being
        // 5 — and further out a ~0.85-unit gap recurred every 6.25. Reported
        // from play as "the closer the enemy, the more the bolt flies through
        // him", which is exactly the shape those numbers predict.
        //
        // The ladder below is deliberately irregular and straddles the muzzle
        // offset (5.0) and the old probe points, so a return to point sampling
        // fails here loudly instead of passing by luck. Radii are AUTHORED BY
        // THIS FIXTURE to pin that geometry: caster 1.5 puts the muzzle at
        // exactly 5.0 (caster_spawn_offset = 1.5 + bolt 1.5 + 2.0), target 1.2
        // keeps the contact radius ~2.7 the gap arithmetic above is built on —
        // not the fat 6.0 the other fixtures use, because a fat target hides
        // precisely this bug. Both ride SubworldAi.radius, the rung a body
        // with no table row answers from (sub/body.h; the explicit BodyRadius
        // override died in M-150 кусок 0). No AI pass ticks here, so the
        // component is inert beyond its width column.
        const float kRanges[] = {1.5f, 3.0f, 4.9f, 6.0f, 8.0f, 9.4f,
                                 14.3f, 20.6f, 27.0f, 35.8f};
        for (const float range : kRanges) {
            sm::ecs::World sweepWorld;
            auto sweepWorldStore_ = sm::make_macro_store();
            sm::store_attach(sweepWorld, sweepWorldStore_.get());
            // Арена — предусловие снарядов (ступень (v): чистые жильцы
            // арены; §8 п.11 — свидетель сам рожает предусловие).
            sm::test::arena_of(sweepWorld.reg);
            sm::SpellBook sweepBook;
            sm::ecs::Pools sweepCombat{};
            sweepCombat.mp = 1000;
            sweepCombat.maxMp = 1000;
            sm::spellbook_learn(sweepBook, sm::spell_ordinal("magic_bolt"));
            // Caster at the origin; his authored 1.5 shell pins the muzzle at
            // 5.0 (see the ladder note above), and caster_spawn_offset's +2.0
            // margin is exactly what keeps the muzzle clear of that shell.
            const auto sweepCaster = add_player(sweepWorld, 0.0f, 0.0f);
            sm::test::give_ai(
                sweepWorld.reg,
                sm::sub::avatar_slot(sm::test::arena_of(sweepWorld.reg)),
                sm::ecs::SubworldAi{sm::ecs::SubworldAi::Wander,
                                    0.0f, 0.0f, 0.0f, 0.0f, 1.5f});
            const int sweepTarget = sm::test::give_body(sweepWorld.reg);
            sm::test::give_pos(sweepWorld.reg, sweepTarget, range, 0.0f, 0.0f);
            sm::test::give_pools(sweepWorld.reg, sweepTarget, sm::ecs::Pools{100, 100});
            sm::test::give_ai(
                sweepWorld.reg, sweepTarget,
                sm::ecs::SubworldAi{sm::ecs::SubworldAi::Wander,
                                    0.0f, 0.0f, 0.0f, 0.0f, 1.2f});
            const bool ok99 = !(!sm::spellbook_cast(sweepWorld, sweepBook, sweepCombat,
                                                    attributes, skills, sm::spell_ordinal("magic_bolt"),
                                                    // ОТ ДУЛА, А НЕ ОТ СТУПНЕЙ:
                                                    // продакшен кастует с
                                                    // `player_muzzle_z()`
                                                    // («The MUZZLE, not the
                                                    // feet» — main.cpp), и
                                                    // фикстура, рождавшая болт
                                                    // на полу, моделировала
                                                    // кастера, которого в игре
                                                    // не бывает. Поймано, когда
                                                    // страж обратного прочёса
                                                    // стал трёхмерным: он мерит
                                                    // до ДУЛА, и фальшивая
                                                    // высота рождения съедала
                                                    // его допуск.
                                                    sweepCaster, 0.0f, 0.0f,
                                                    sm::sub::kBodyEyeM,
                                                    1.0f, 0.0f, 0.0f, true));
            CHECK(ok99, "sweep: magic_bolt cast rejected");
            if (!ok99) return 1;
            // Production-sized ticks, until the bolt is spent.
            for (int i = 0; i < 64 && projectile_count(sweepWorld) > 0; ++i) {
                sm::sub::tick_spell_projectiles(
                    sweepWorld, nullptr, 1.0f / 64.0f,
                    &sm::test::all_live_slots,
                    &sm::test::arena_of(sweepWorld.reg));
            }
            const bool ok100 = !(!(hp_of(sweepWorld, sweepTarget) < 100.0f));
            if (!ok100) {
                std::fprintf(stderr, "  range=%.1f survived untouched\n",
                             double(range));
            }
            CHECK(ok100, "sweep: bolt passed through a body in its path");
            if (!ok100) return 1;
            // The caster stands at the origin, behind the muzzle: the stretch
            // swept back to his hand must never wound him.
            const bool ok101 = !(!nearf(
                hp_of(sweepWorld,
                      sm::sub::avatar_slot(sm::test::arena_of(sweepWorld.reg))),
                1000.0f));
            CHECK(ok101, "sweep: muzzle stretch wounded its own caster");
            if (!ok101) return 1;
        }
    }

    // ── КАП АРЕНЫ: СНАРЯДУ ОТКАЗЫВАЮТ ЧЕСТНО, И ЧИСЛА ЖИВУТ В КОЛОНКЕ ──────
    // До ломтя 4 (M-150) у снарядов КАПА НЕ БЫЛО ВОВСЕ: рождалось столько,
    // сколько позовут. Теперь снаряд — жилец арены объектов, и предел у него
    // один с телами — СЛОТ; закон отказа тот же, что у `BodyCrowd::add`: нет
    // слота — нет объекта, и сущности за собой он не оставляет. Каст при этом
    // ОПЛАЧЕН (мана списана), как промах мечом — вердикт владельца 2026-09-17
    // о касте, который «фыркнул и ничего не нашёл».
    // Обе полярности в одном блоке: забитая арена обязана дать НОЛЬ снарядов,
    // а освобождённый слот — ровно один, и числа его обязаны лежать В КОЛОНКЕ
    // арены (ступень (v): компонента умерла, снаряд — чистый жилец слота).
    {
        sm::ecs::World capWorld;
        auto capStore_ = sm::make_macro_store();
        sm::store_attach(capWorld, capStore_.get());
        // Арена — предусловие снарядов (ступень (v): чистые жильцы
        // арены; §8 п.11 — свидетель сам рожает предусловие).
        sm::test::arena_of(capWorld.reg);
        sm::sub::SubObjects& capArena = sm::test::arena_of(capWorld.reg);
        int capFilled = 0;
        while (capArena.alloc(sm::ecs::Position{}) >= 0) ++capFilled;
        CHECK(capFilled == int(sm::kMaxSubObjects),
              "арена набивается ровно до СВОЕГО капа (kMaxSubObjects), не дальше");

        sm::SpellBook capBook;
        sm::ecs::Pools capCombat{};
        capCombat.mp = 2000;
        capCombat.maxMp = 2000;
        sm::spellbook_learn(capBook, sm::spell_ordinal("magic_bolt"));
        const bool capCast = sm::spellbook_cast(
            capWorld, capBook, capCombat, attributes, skills,
            sm::spell_ordinal("magic_bolt"), sm::sub::ObjRef{},
            100.0f, 100.0f, 0.0f, 1.0f, 0.0f, 0.0f, true);
        CHECK(capCast && capCombat.mp < 2000,
              "каст состоялся и ОПЛАЧЕН, хотя снаряду отказал кап");
        CHECK(projectile_count(capWorld) == 0,
              "кап арены: снаряда нет вовсе — сущность за собой не остаётся");

        // Вторая полярность: слот освободился — снаряд рождается снова, и его
        // числа отвечает КОЛОНКА.
        capArena.free(0);
        const bool capCast2 = sm::spellbook_cast(
            capWorld, capBook, capCombat, attributes, skills,
            sm::spell_ordinal("magic_bolt"), sm::sub::ObjRef{},
            100.0f, 100.0f, 0.0f, 1.0f, 0.0f, 0.0f, true);
        int capWithColumn = 0;
        {
            const sm::sub::SubObjects& co = sm::sub::objects_of(capWorld.reg);
            for (int s = 0; s < int(sm::kMaxSubObjects); ++s) {
                const std::uint16_t f = co.flags[std::size_t(s)];
                if ((f & sm::sub::kObjAlive) == 0u
                    || (f & sm::sub::kObjProjectile) == 0u) continue;
                if (co.projectile[std::size_t(s)].spellId
                    == sm::stable_spell_id("magic_bolt")) {
                    ++capWithColumn;
                }
            }
        }
        CHECK(capCast2 && capWithColumn == 1,
              "освободился слот — снаряд родился, и числа его несёт КОЛОНКА");
    }

    // ОСИРОТЕВШИЙ СНАРЯД (ступень (iii-б)): владелец — ссылка сцены парой
    // {ownerSlot, ownerId}, и протухание судит ТОЛЬКО ref_slot. Мёртвый
    // кастер и перерождённый жилец его слота равно «не владелец»: репутация
    // и лог игрока не достаются чужому. До пары закон держался версией
    // entt-битов и свидетеля не имел.
    {
        sm::ecs::World orphanWorld;
        const int orphanCaster = sm::test::give_body(orphanWorld.reg);
        sm::test::make_avatar(orphanWorld.reg, orphanCaster);
        const sm::sub::ObjRef ownerRef = sm::sub::ref_of(
            sm::sub::objects_of(orphanWorld.reg), orphanCaster);
        sm::ecs::Projectile orphanBolt{};
        orphanBolt.ownerSlot = ownerRef.slot;
        orphanBolt.ownerId = ownerRef.id;
        CHECK(sm::sub::projectile_owner_is_player_side(
                  sm::sub::objects_of(orphanWorld.reg),
                                                       orphanBolt),
              "живой аватар-владелец опознан по ссылке");
        const int ownerSlot = int(ownerRef.slot);
        // Смерть тела — ЗАНУЛЕНИЕ СЛОТА (ломоть D): reg.destroy сносить было
        // некого, слот освобождает та же дверь, что у всех путей смерти.
        sm::test::arena_of(orphanWorld.reg).free(ownerSlot);
        CHECK(!sm::sub::projectile_owner_is_player_side(
                  sm::sub::objects_of(orphanWorld.reg),
                                                        orphanBolt),
              "мёртвый владелец — протухшая ссылка: снаряд осиротел");
        // Форс курсора (прецедент body_contract): перерождаем РОВНО его слот
        // жильцом, который БЫЛ БЫ на стороне игрока, — только сверка ID
        // отличает его от погибшего кастера.
        sm::sub::objects_of(orphanWorld.reg).cursor = ownerSlot;
        const int reborn = sm::test::give_body(orphanWorld.reg);
        sm::test::give_flag(orphanWorld.reg, reborn,
                            sm::sub::kObjPlayerSoldier);
        CHECK(reborn == ownerSlot,
              "предусловие своё: слот владельца перерождён (форс курсора)");
        CHECK(!sm::sub::projectile_owner_is_player_side(
                  sm::sub::objects_of(orphanWorld.reg),
                                                        orphanBolt),
              "перерождённый жилец ЧУЖОЙ снаряд не наследует: ID сменился");
    }

    int susActive = 0;
    for (int i = 0; i < sm::kSpellCount; ++i) {
        susActive += sm::spellbook_has_sustained(book, i) ? 1 : 0;
    }
    std::fprintf(stderr,
                 "PASS: projectiles=%d mp=%d sustained=%d\n",
                 projectile_count(world), combat.mp,
                 susActive);
    return sm::test::report("spell_casting_effects_test");
}

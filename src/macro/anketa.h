// АНКЕТА — числа КОНКРЕТНОГО носителя, род 2 пакетной шины (наряд M-181).
//
// Владелец, дословно (2026-09-28): «есть таблица скилов таблица спелов будут
// потом перки таблица объектов (нпц/мобы/предметы) … это типа как
// организованный системный игровой контент»; и про анкету: «там вообще всё и
// инвентарь на данный момент и HP SP MP и скилы и опыт и уровень и где он ща
// ну типа это прям как знаешь в DND анкета персонажа это полноценный сквад
// житель макромира в этом моменте и это гладкий плоский массив то есть он
// заранее фиксированной длины и новые просто заполняют его где пусто а если
// кто-то удалён зануляется».
//
// Образец, которым владелец объяснил разделение: «у нас есть гоблин и мы можем
// расставить кучи гоблинов копии» — ОДНА строка каталога (`tables/`) рождает
// сколько угодно экземпляров, и каждый дальше живёт СВОЕЙ анкетой: качается,
// тратит бары, носит вещи, ложится в сейв.
//
// Отсюда граница, и она односторонняя: анкета ЧИТАЕТ каталог, каталог об
// анкете не знает (AGENTS §11). Что здесь лежит — колонки строки сквада; что
// в `tables/attributes.h` — что эти колонки ЗНАЧАТ.
#pragma once
#include "core/time.h"          // steps_from_seconds — квант двери восстановления
#include "tables/attributes.h"  // каталог: AttributeId/SkillId, строки, THE skill law
#include "tables/army.h"        // каталог: CombatTemplate — боевой лист строки
#include "tables/bonus.h"       // каталог: строки бонусов, чью сумму копит анкета
#include "tables/npc.h"         // каталог: строка существа — цена найма, содержание, награда
#include "tables/spells.h"      // каталог: строка спелла — что делает каст новичка
#include "tables/items.h"       // каталог: строки предметов, ItemRef — что ХРАНИТ контейнер ниже
#include "tables/role_weights.h" // каталог: веса ролей — куда строка тратит бюджет анкеты
#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <array>
#include <cstddef>
#include <cstdint>

namespace sm {

// ── Attributes: ЧИСЛА ТЕЛА ─────────────────────────────────────

// The same shape the ranks have: a fixed envelope of scores, and a TABLE of
// what they mean. 16 slots for the 9 the game names today — work_vector §5
// asks for exactly this, and the reason is the save: a new attribute is a row
// under a fixed cap, so naming one does not move the format.
//
// A byte per score. The cap that makes a byte honest is stated, not assumed
// (kMaxAttributeScore below) and enforced at the one door into a score, the
// same way the rank cap is.
inline constexpr int kMaxAttributes = 16;
static_assert(int(AttributeId::Count) <= kMaxAttributes,
              "the attribute envelope must hold every score the game names");

// Every slot starts at 1 — INCLUDING the reserved tail, so a score that gets
// named later begins where every other score began, rather than at a zero that
// would divide differently in the asymptotic formulas.
constexpr std::array<std::uint8_t, kMaxAttributes> attribute_bases() {
    std::array<std::uint8_t, kMaxAttributes> a{};
    for (std::uint8_t& v : a) v = 1;
    return a;
}

struct Attributes {
    std::array<std::uint8_t, kMaxAttributes> score = attribute_bases();

    // constexpr, как у Skills ниже: анкета МЕСТА — таблица времени
    // компиляции (characters.h kLandmarkSheets), и атрибут в ней должен
    // писаться там же, где скилл. Асимметрия двух соседних операторов была
    // случайной — одна из них просто не понадобилась раньше.
    constexpr std::uint8_t& operator[](AttributeId id) {
        return score[std::size_t(id)];
    }
    constexpr std::uint8_t operator[](AttributeId id) const {
        return score[std::size_t(id)];
    }
    constexpr int of(AttributeId id) const {
        return int(score[std::size_t(id)]);
    }
};

// ── Skills: РАНГИ ТЕЛА ─────────────────────────────────────────
//
// The ranks are a flat array under a po2 cap and the MEANINGS are rows in the
// catalog — the same shape factions, biomes and creatures already have, and the
// shape work_vector §5 asks for by name. Adding a skill used to touch five
// places (a named field here, the SkillId enum, two `skill_value` switches,
// the UI row table in ui/overlays.cpp and the per-role weight table in
// character_sheet.h); it is one row and one weight per role now, and the save
// format does not move at all because the envelope is fixed.
//
// 64 slots for the canon ~35 (CANON S14 says the envelope by name): the
// envelope is the thing the save promises, so it is sized once, generously,
// in a power of two. A byte per rank because the rank cap is 100 and rank
// READS as a percent (kMaxSkillRank, tables/attributes.h).
inline constexpr int kMaxSkills = 64;
static_assert(int(SkillId::Count) <= kMaxSkills,
              "the skill envelope must hold every skill the game names");

struct Skills {
    std::array<std::uint8_t, kMaxSkills> rank{};

    // constexpr: анкеты МЕСТ (characters.h kLandmarkSheets) собираются на
    // этапе компиляции — одна на род, ни байта в рантайме и в сейве.
    constexpr std::uint8_t& operator[](SkillId id) {
        return rank[std::size_t(id)];
    }
    constexpr std::uint8_t operator[](SkillId id) const {
        return rank[std::size_t(id)];
    }
    constexpr int of(SkillId id) const { return int(rank[std::size_t(id)]); }
};

// THE skill law applied to a SHEET. The law itself (row → multiplier) lives in
// the catalog; these two say «чьи ранги» and nothing more.
inline float skill_mult(const Skills& s, SkillId id) {
    return skill_mult_of(id, s.of(id));
}

inline int skill_mult_pct(const Skills& s, SkillId id) {
    return skill_mult_pct_of(id, s.of(id));
}

// ── Perks: THE STUB (owner verdict 2026-09-19; graph is content-stage) ────
//
// The TS-era perk block (24 ids, 8 tooltip rows, effects on TWO) died whole
// 2026-09-03: six of the eight rows promised mechanics that did not exist.
// The redesigned system is CANON S14 «СОЗВЕЗДИЯ ПЕРКОВ»: a flat constexpr
// node graph (kPerkNodes, edges[4]), constellation quadrants, effects as
// rows of the ONE bonus registry. That graph is CONTENT — weeks of authored
// nodes — and the owner ruled it consciously ABSENT for the demo. Когда он
// придёт, СТРОКИ узлов лягут в `tables/`, а эта маска останется здесь: узел
// — контент, выученность узла — анкета (владелец 2026-09-28: «перки будут в
// будущем будет со скилами и атрибутами там звёздный граф но он тоже как
// таблица плоский массив»).
//
// What exists NOW is the third currency and its storage, symmetric with the
// other two (5-5-5 at creation, 1-1-1 per level, CANON S14 2026-09-14):
//   - LevelData.perkPoints — 5 at creation, +1 EVERY level; nothing spends
//     them until the graph lands, so they honestly accrue;
//   - PerkMask below — the 256-bit learned-set (32 bytes) inside every
//     CharacterSheet, all zeroes until kPerkNodes exists. has_perk is the
//     1-instruction reader the graph was architected around (S26).
// The aura DOOR (character_sheet.h squad_bonuses) still stands: when nodes
// arrive they feed BonusTotals rows through it, not new code.

struct PerkMask {
    std::uint64_t bits[4] = {};   // 256 node ordinals — kPerkNodes' envelope

    bool has_perk(int ordinal) const {
        return (bits[std::size_t(ordinal >> 6) & 3]
                >> (std::uint32_t(ordinal) & 63u)) & 1u;
    }
    void set_perk(int ordinal) {
        bits[std::size_t(ordinal >> 6) & 3] |=
            std::uint64_t(1) << (std::uint32_t(ordinal) & 63u);
    }
};
static_assert(sizeof(PerkMask) == 32, "CANON S26: 256-bit mask, 32 bytes");

// ── Bar ceilings ───────────────────────────────────────────────
//
// What a sheet says the three bars of a body CAP at — derived, never stored.
// This struct was `CombatStats`: nine fields, current values beside the
// ceilings plus three cached hourly rest rates, existing in exactly ONE
// instance in the whole game — PlayerState::combatStats, the player's private
// second home for bars every other body kept in ecs::Pools. Landing 4 of the
// «полосы на тело» track killed the store: bars live in Pools for everyone,
// the rest rate is derived on the spot by the one law (recovery.h
// rest_pools — a cached rate could FREEZE, and did, seed-999 2026-09-06),
// and what remains of the type is the only thing that was ever derived
// truth: the ceilings.

struct BarCeilings {
    int maxHp = 100;
    int maxMp = 100;
    int maxSp = 100;
};

// ── Derived bonuses (ephemeral) ────────────────────────────────

// (critBase and relationBonus died in the 2026-09-03 sweep: zero readers
// beyond one UI print — LCK's real home is the dice-roll door, CANON S13.)
//
// INTEGER, phase 4в (the discrete house): the adds are whole points and the
// multipliers are whole PERCENT (100 = ×1) — the same currency the strike
// assembly already multiplies by (multPct/100, tables/damage_types.h). The
// floats they were forced every reader to floor at its own doorstep; the
// floor now happens once, in calculate_derived, and a drifted rounding
// cannot exist.
struct DerivedBonuses {
    int rawPhysDamage = 0;
    int rawSpellDamage = 0;
    int expMultPct = 100;
    int moveSpeedPct = 100;
    int tradeDiscountPct = 0;
};

// ── Level data ─────────────────────────────────────────────────

struct LevelData {
    int level             = 1;
    int exp               = 0;
    int expToNext         = 0; // populated by `default_level_data`
    // The creation budget (CANON S14, owner 2026-09-03): 5 attribute points
    // and 5 LEARN PICKS — a pick teaches a skill (rank 0 → 1), it is not a
    // rank point. Skill points arrive with levels and spend only into what
    // is already known.
    int attributePoints   = 5;
    int skillPoints       = 0;
    int learnPicks        = 5;
    // The third K of the creation budget (CANON S14 1-1-1, owner 2026-09-14):
    // 5 perk points toward the starting constellation lanes, +1 every level.
    // The graph they spend into is consciously absent (stub, owner
    // 2026-09-19) — they accrue, and no door spends them yet.
    int perkPoints        = 5;
};

// (No `attribute_value` switches either. A score is `attributes[AttributeId::X]`
// — an index into a flat array — so there is nothing to switch on, and naming
// a new attribute cannot forget a case.)

// (No `skill_value` switches. A rank is `skills[SkillId::X]` — an index into
// a flat array — so there is nothing left to switch on, and adding a skill
// cannot forget to update a case.)

// A score is a BYTE, so the ceiling is the byte's — stated here rather than
// discovered by a wrap. It is enormous by design (a hundred levels of nothing
// but one stat lands nowhere near it); what matters is that the one door into
// a score refuses rather than rolls over.
constexpr int kMaxAttributeScore = 255;

inline bool spend_attribute_point(LevelData& ld, Attributes& a, AttributeId id) {
    if (id >= AttributeId::Count || ld.attributePoints <= 0) return false;
    std::uint8_t& score = a[id];
    if (int(score) >= kMaxAttributeScore) return false;
    ++score;
    --ld.attributePoints;
    return true;
}

// ── THE LEARN LAW (CANON S14, owner 2026-09-03) ────────────────
//
// Rank 0 IS "you do not know this skill" — no bit beside the rank, zero and
// ignorance are one fact. Points spend only into what is KNOWN; knowing comes
// from the WORLD (teachers, events) or from creation's learn picks. To learn
// is to reach rank 1.

// The one door from ignorance to rank 1. Refuses what is already known — a
// teacher cannot teach you twice, and a world-source that lands on a known
// skill should say so rather than silently burn.
inline bool learn_skill(Skills& s, SkillId id) {
    if (id >= SkillId::Count) return false;
    std::uint8_t& rank = s[id];
    if (rank != 0) return false;
    rank = 1;
    return true;
}

// A creation pick is a learn with a budget: 5 at character creation
// (default_level_data), consumed through this door so the pool cannot
// over-spend and a refused learn keeps the pick.
inline bool spend_learn_pick(LevelData& ld, Skills& s, SkillId id) {
    if (ld.learnPicks <= 0) return false;
    if (!learn_skill(s, id)) return false;
    --ld.learnPicks;
    return true;
}

// The cap is enforced HERE, at the only door into a skill rank, so no caller
// can push one past mastery and no formula has to defend itself against a rank
// nobody could legitimately have. A refused spend keeps the point. Rank 0
// refuses too — THE learn law above: you cannot train what you do not know.
inline bool spend_skill_point(LevelData& ld, Skills& s, SkillId id) {
    if (id >= SkillId::Count || ld.skillPoints <= 0) return false;
    std::uint8_t& rank = s[id];
    if (rank == 0) return false;               // unknown: learn first
    if (int(rank) >= kMaxSkillRank) return false;
    ++rank;
    --ld.skillPoints;
    return true;
}

// ── Formulas (verbatim from attributes.ts) ─────────────────────

// EXP_next(lvl) = floor(1000 * lvl * (0.1 * lvl + 1))
inline int exp_to_next_level(int level) {
    return int(1000.0 * level * (0.1 * level + 1.0));
}
// (exp_from_fight — the flat 10·lvl kill payout — died 2026-08-29: every
// kill pays through the ONE npc_xp_reward law in macro/npc.h now.)
inline Attributes default_attributes() { return {}; }
inline Skills     default_skills()     { return {}; }

inline LevelData default_level_data() {
    LevelData ld{};
    ld.expToNext = exp_to_next_level(1);
    return ld;
}

// THE ONE RECOVERY LAW (CANON S14; owner rulings Session 21 and 2026-09-03):
// every bar recovers as a PERCENT of itself per GAME HOUR of REST, and rest —
// standing still, doing nothing — is the ONLY thing that recovers a bar
// (the march heals nothing: a marching body never calls the rest law).
// A percent, not a flat number, so a full rest takes the same 8 hours for
// every body in the world — the veteran's bigger bar refills proportionally
// faster in absolute points, and nobody "rests longer because he is tougher"
// (the perversity the flat 10·(1+vit·0.01) legacy had). Since only waiting
// refills a bar, SP is literally time in a bar: END/WILL do not shorten the
// night, they fatten the day. The ONLY thing that shortens a rest is the
// `marathon` skill on SP, multiplying this rate by THE skill law (+1%/rank;
// capstone rank 100 halves the rest to 4 h). One fraction for all three bars
// (owner, 2026-09-03): 1/8 (po2) — a full bar in 8 game hours, a night
// refills any traveller with room to spare.
constexpr float kRestRegenPctPerHour = 0.125f;

// FinalStat = (base + attrRaw) × (1 + skillRank × skillMult)
//
// Ceilings ONLY — no current values (they live in the body's ecs::Pools and
// nothing here may touch them), no rest rates (the rate is derived from the
// ceiling on the spot by rest_pools, so it cannot freeze the way the cached
// spRegen did). How a moved ceiling meets the bar it caps is the ONE rescale
// law, squad.h refresh_body_from_sheet: the fraction survives, for everyone
// (owner 2026-09-10 «доля у всех»).
//
// The bases are MANDATORY (§41 root 2): they are columns of the caller's
// CombatTemplate row (`hp`/`mp`/`sp`), and a default argument here was that
// row smuggled past the table — every body's MP/SP base was one hidden 100
// the ROW could not override, and the row lied about what it built. Callers
// that speak for a body go through the three body_max_* doors
// (character_sheet.h), which unpack the row; only tests state bases raw.
inline BarCeilings bar_ceilings(const Attributes& a, const Skills& s,
                                int baseHp, int baseMp, int baseSp) {
    const float rawHp = float(baseHp + a.of(AttributeId::End) * 10);
    const float rawMp = float(baseMp + a.of(AttributeId::Wil) * 10);
    BarCeilings c;
    c.maxHp = int(rawHp * skill_mult(s, SkillId::Bodybuilding));
    c.maxMp = int(rawMp * skill_mult(s, SkillId::Meditation));
    // The SP bar has TWO owners by half each (CANON S14): the warrior's END
    // and the mage's WILL both buy the day. Integer floor, house style; no
    // skill multiplies the bar — `marathon` multiplies the RECOVERY RATE
    // instead (kRestRegenPctPerHour above), so bar and rest are two levers.
    c.maxSp = baseSp + ((a.of(AttributeId::End) + a.of(AttributeId::Wil)) >> 1)
                           * 10;
    return c;
}

// THE one CHA→trade-discount formula (1 % per point). Both of its doors read
// it — the trade price (economy.cpp trade_buy/sell_price) and the payroll
// (calculate_squad_upkeep via the derived column below). The 2026-09-03 sweep
// found it spelled inline in TWO places; a drifted copy here would desync the
// shop from the sheet panel silently.
// Whole percent (1 % per point), phase 4в — the float twin was ×0.01 of this.
inline int cha_trade_discount_pct(int cha) {
    return cha;
}
// (Дробный близнец `cha_trade_discount` вырезан 2026-09-22: ноль вызовов, а
// его комментарий утверждал, что «economy.cpp works in fractional
// multipliers» — в economy.cpp вызова не было. Закон один и целочисленный.)

// ── Natural quickness — THE Spd asymptote ──────────────────────
// The body's own tempo curve, one shape for every limb (CANON S14 «один рычаг
// на ручку», 2026-09-07): legs, sword arm and casting hand all divide their
// base tempo by this. Asymptotic so a monstrous score cannot run away with
// the game: half-saturation at 50 — half the endgame anchor (~100, S14), so a
// mid-invested body has half its natural ceiling and the byte cap 255 lands
// at ×1.84, still under the hard ×2 limit. Whole percents, 100 = ×1.
inline int quickness_pct(int spd) {
    if (spd < 0) spd = 0;
    return 100 + (100 * spd) / (spd + 50);
}

inline DerivedBonuses calculate_derived(const Attributes& a, const Skills& s) {
    DerivedBonuses d;
    // RAW adds (CANON S14 «один рычаг на ручку», 2026-09-07): the attribute
    // is the whole add — «природная мощь». The 2026-09-03 form multiplied
    // these by Armsmaster/Spellcraft, which counted the generic pair into
    // damage while the same pair now governs TEMPO (recovery_steps below) —
    // one handle in two totals is a hidden square, not a synergy. The TYPED
    // half — weapon skill, school — still multiplies at the damage door /
    // school wiring (S15), each lever exactly once.
    d.rawPhysDamage  = a.of(AttributeId::Str);
    d.rawSpellDamage = a.of(AttributeId::Intl);
    d.expMultPct     = 100 + a.of(AttributeId::Wis);
    // Attributes add, skills multiply. `spd` is the body's own quickness;
    // `athletics` is training on top of it — the legs' GENERIC tempo skill,
    // exactly what Armsmaster is to the arms. `travel` has no business here —
    // it buys DISTANCE per bar of stamina, not speed (macro/movement_cost.h).
    d.moveSpeedPct   = quickness_pct(a.of(AttributeId::Spd))
                       * skill_mult_pct(s, SkillId::Athletics) / 100;
    // ТОРГОВАЯ СИЛА — ОДНО ПРОИЗВОДНОЕ (CANON S25, владелец 2026-09-18:
    // «наценка от производного, который уже после атрибута харизмы и скила
    // эффективного всего… она берёт финалочку»). Форма ровно как у скорости
    // строкой выше: атрибут — природная мощь, скилл МНОЖИТ её. До этого
    // торговая сила считалась в ДВУХ местах — здесь без скилла и ещё раз в
    // economy.cpp (bargaining_edge), то есть ровно дефект S26; вместе с этой
    // свёрткой второе место умерло. Спеллы, артефакты и аффиксы попадают в
    // цену бесплатно: они меняют лист, а сделка спрашивает только это число.
    d.tradeDiscountPct = cha_trade_discount_pct(a.of(AttributeId::Cha))
                         * skill_mult_pct(s, SkillId::Trade) / 100;
    return d;
}

// ── THE recovery door (CANON S14 «один рычаг на ручку», 2026-09-07) ────────
// One tempo law for a swing, a cast — and the legs, which already walk this
// exact shape inside moveSpeedPct above: the row's authored base, divided by
// natural quickness (the Spd asymptote) and by the rank of the GENERIC skill
// of the domain — Armsmaster for any physical act, Spellcraft for any cast.
// The TYPED skill (Sword, FireMagic) is deliberately ABSENT: it is the POWER
// lever, and one handle counted into both power and tempo is a hidden square
// (×121 DPS from one skill), not a synergy.
//
// The base is DATA — the item row's `recovery`, the spell row's `cooldown`,
// the NPC row's `cooldown` — so a dagger is faster than a mace by its row,
// never by code. Floor = one simulation step: the time quantum itself, not an
// invented cap. Integer end to end and healthy to the ENGINE caps (attribute
// 255 → quickness ≤ 184, rank 100 → tempo ≤ 600 at 5 %/rank): worst numerator
// is a 120 s base = 7680 steps × 10⁴ ≈ 7.7·10⁷, thirty-fold inside int; the
// law test locks the caps explicitly.
inline int recovery_steps(float baseSeconds, const Attributes& a,
                          const Skills& s, SkillId genericSkill) {
    const int quickPct = quickness_pct(a.of(AttributeId::Spd));
    const int tempoPct = skill_mult_pct(s, genericSkill);
    const int base = int(steps_from_seconds(baseSeconds));
    const int steps = (base * 100 * 100) / (quickPct * tempoPct);
    return steps < 1 ? 1 : steps;
}

// ── Carry weight ───────────────────────────────────────────────

constexpr float kBaseCarryKg = 100.0f;

inline float get_carry_capacity(const Attributes& a, const Skills& s) {
    return (kBaseCarryKg + float(a.of(AttributeId::Str)) * 10.0f)
           * skill_mult(s, SkillId::Weightlifting);
}
inline float get_overload_penalty(float weightKg, float capacityKg) {
    return weightKg > capacityKg ? (weightKg - capacityKg) : 0.0f;
}

// ── Level-up ───────────────────────────────────────────────────

// THE level grant (CANON S14, owner verdict 2026-09-14): +1 attribute point,
// +1 skill point AND +1 perk point EVERY level — 1-1-1, full level isotropy:
// no «special» levels, no artificial gates. Specialization is held by the
// perk graph's TOPOLOGY (distance to a keystone), not by point scarcity.
inline bool try_level_up(LevelData& ld) {
    if (ld.exp < ld.expToNext) return false;
    ld.exp           -= ld.expToNext;
    ld.level         += 1;
    ld.expToNext      = exp_to_next_level(ld.level);
    ld.attributePoints += 1;
    ld.skillPoints     += 1;
    ld.perkPoints      += 1;
    return true;
}

// THE way experience is granted, and the only one. Awarding XP and consuming it
// into levels is a SINGLE act: every source that split them — a quest reward, a
// scripted grant_xp effect — silently handed the player experience he could
// never spend, because the one place that drained the pool was the subworld kill
// path. A hero could finish ten contracts, sit on four levels' worth of exp, and
// stay level 1 until he stabbed a wolf.
//
// A single grant can cross several thresholds (a chapter reward at low level),
// hence the loop. Returns how many levels it produced, which is what a caller
// needs to say so, and 0 for a grant that only fills the bar.
inline int award_exp(LevelData& ld, int amount) {
    if (amount > 0) ld.exp += amount;
    int gained = 0;
    while (try_level_up(ld)) ++gained;
    return gained;
}

// The wis dividend (owner ruling 2026-08-05: the first formerly-dead
// attribute wired live): every award multiplies by the recipient's expMultPct
// (calculate_derived — +1% per wis point; 100 = ×1). Integer round half up
// ((·+50)/100), so small grants still feel the attribute.
inline int award_exp(LevelData& ld, int amount, int expMultPct) {
    const int scaled = amount > 0
        ? (amount * expMultPct + 50) / 100
        : amount;
    return award_exp(ld, scaled);
}

// ══ БОНУСЫ, СТОЯЩИЕ НА ТЕЛЕ ═══════════════════════════════════════════════
// Строки бонусов — КАТАЛОГ (`tables/bonus.h`): их встраивает в себя строка
// спелла и ординалом носит аффикс предмета. А СУММА того, что стоит на
// КОНКРЕТНОМ теле, — анкета, и живёт здесь, рядом с ранга́ми, которые она
// сдвигает.

// ── Standing: accumulate, then read through a modified COPY ───────────────

// Everything standing, summed by address. Flat arrays over the sheet's own
// envelopes, so a body's whole modifier set is one small POD on the stack and
// summing it allocates nothing.
struct BonusTotals {
    std::array<std::int16_t, kMaxAttributes> attr{};
    std::array<std::int16_t, kMaxSkills>     skill{};
    // The affix track's two new address spaces, summed exactly like the
    // first two. `armor` is read by the defence assembly (sub/damage.cpp)
    // BESIDE the worn rows' own columns; `derived` is read by each law at
    // its own output (see DerivedModId).
    std::array<std::int16_t, kDamageTypeCount>                  armor{};
    std::array<std::int16_t, std::size_t(DerivedModId::Count)>  derived{};
    // "Did what stands on him CHANGE?" is a question the per-step bar
    // refresh asks (app loop) — equality is the whole answer.
    bool operator==(const BonusTotals&) const = default;

    // Whole-struct merge — what player_standing_bonuses does with the worn
    // sum. A member so a NEW array here cannot be forgotten at a call site
    // that copies field by field (a comment asking for lockstep is not a
    // mechanism).
    BonusTotals& operator+=(const BonusTotals& o) {
        for (std::size_t i = 0; i < attr.size(); ++i)    attr[i]    += o.attr[i];
        for (std::size_t i = 0; i < skill.size(); ++i)   skill[i]   += o.skill[i];
        for (std::size_t i = 0; i < armor.size(); ++i)   armor[i]   += o.armor[i];
        for (std::size_t i = 0; i < derived.size(); ++i) derived[i] += o.derived[i];
        return *this;
    }

    int derived_of(DerivedModId id) const {
        return int(derived[std::size_t(id)]);
    }
};

inline void accumulate(BonusTotals& t, Bonus b) {
    if (b.row == 0 || b.row >= std::uint8_t(BonusId::Count)) return;
    const BonusDef& d = bonus_def(BonusId(b.row));
    switch (d.target) {
        case BonusTarget::Attribute:
            if (d.index < kMaxAttributes) t.attr[d.index] += b.value;
            break;
        case BonusTarget::SkillRank:
            if (d.index < kMaxSkills) t.skill[d.index] += b.value;
            break;
        case BonusTarget::Armor:
            if (d.index < kDamageTypeCount) t.armor[d.index] += b.value;
            break;
        case BonusTarget::Derived:
            if (d.index < std::uint8_t(DerivedModId::Count)) {
                t.derived[d.index] += b.value;
            }
            break;
        case BonusTarget::Pool:
            break;   // instant rows do not stand; apply_instant takes them
    }
}

inline void accumulate(BonusTotals& t, const Bonus* first, int count) {
    for (int i = 0; i < count; ++i) accumulate(t, first[i]);
}

// (`effective_sheet(base, totals)` — the standing half's one reader — lives in
// macro/character_sheet.h, beside the type it copies. This file stays a leaf
// above attributes.h so the item catalog can include it without dragging the
// creature registry in behind it.)

// ── Derived rows meeting their laws ──────────────────────────────────────
// ONE clamp for every whole-percent derived row (SwingPct, MovePct): ×4
// either way, po2 — a curse cannot freeze a body and a stack of hastes
// cannot divide time by zero. Spelled once; the strike assembly
// (anatomy.cpp) and the overloads below both read it.
inline constexpr int kDerivedPctFloor = -75;
inline constexpr int kDerivedPctCeil  = 300;

inline int derived_pct_mult(int base, const BonusTotals& t, DerivedModId id) {
    const int pct = 100 + std::clamp(t.derived_of(id),
                                     kDerivedPctFloor, kDerivedPctCeil);
    const int out = base * pct / 100;
    return out < 1 ? 1 : out;
}

// The sheet laws, WITH what stands on the body. These live here and not in
// attributes.h because that file is below this one and cannot see the totals;
// a call site that has no totals keeps calling the two-argument law.
inline DerivedBonuses calculate_derived(const Attributes& a, const Skills& s,
                                        const BonusTotals& t) {
    DerivedBonuses d = calculate_derived(a, s);
    d.moveSpeedPct = derived_pct_mult(d.moveSpeedPct, t, DerivedModId::MovePct);
    return d;
}

inline float get_carry_capacity(const Attributes& a, const Skills& s,
                                const BonusTotals& t) {
    const float kg = get_carry_capacity(a, s)
                   + float(t.derived_of(DerivedModId::CarryKg));
    // A back cannot hold a negative load; zero is the honest floor and the
    // overload law upstairs already prices every carried kilogram over it.
    return kg < 0.0f ? 0.0f : kg;
}

// ── Instant: act once on the pools ───────────────────────────────────────

// The three pools, named so this layer can speak about them without knowing
// which container a particular body keeps them in (every body's Pools,
// a body's ecs::Pools). Values are ints because pools are ints everywhere.
struct PoolSlice {
    int* current[int(PoolId::Count)] = {nullptr, nullptr, nullptr};
    int  maximum[int(PoolId::Count)] = {0, 0, 0};
};

// Apply one instant row. Returns how much actually moved — which is NOT the
// value asked for when the pool was already full or nearly empty, and callers
// that report to the player ("+30 HP") want the truth rather than the wish.
inline int apply_instant(const PoolSlice& pools, Bonus b) {
    if (b.row == 0 || b.row >= std::uint8_t(BonusId::Count)) return 0;
    const BonusDef& d = bonus_def(BonusId(b.row));
    if (d.target != BonusTarget::Pool) return 0;
    if (d.index >= std::uint8_t(PoolId::Count)) return 0;
    int* cur = pools.current[d.index];
    if (!cur) return 0;
    // A wound is a blow, and blows have one door (see the rows above).
    if (d.index == std::uint8_t(PoolId::Hp) && b.value < 0) return 0;
    const int max = pools.maximum[d.index];
    const int before = *cur;
    *cur = std::clamp(before + int(b.value), 0, std::max(0, max));
    return *cur - before;
}

// ══ СЛОТ КОНТЕЙНЕРА ══════════════════════════════════════════════════════════
// `SoldierRecord` — душа В КОНТЕЙНЕРЕ конкретного сквада: ординал строки
// каталога (`kind`), её уровень и стабильный id сейва. Боевой лист самой
// строки — `CombatTemplate@src/tables/army.h`, и он один на весь вид.
//
// ЭТО ДОЖИВАЮЩАЯ ФОРМА, и названо вслух: контейнер и инвентарь по вердикту
// владельца (2026-09-28: «ростер и инвентарь уже по сути почти слиты»)
// сходятся в ОДИН контейнер 32×32, и тогда слот контейнера перестаёт быть
// отдельным типом. До тех пор он стоит здесь — в анкете, где и был.

struct SoldierRecord {
    std::uint32_t entityId = 0; // stable save id, not an EnTT handle; 0 =
                                // a GENERIC soul (no history, stackable)
    // WHAT this member is, in the ONE id space every body already shares with
    // the ECS (`ecs::NPCKind.type`): an ordinal of the one npc table — a wolf
    // is as legal a row as a spearman, so a wolf pack IS a squad, and the byte
    // this used to be could not say so (CANON.md S4/S16). The old
    // `0x100 | catalog index` monster encoding is dead with the second table
    // (npc.h). Sixteen bits, validated by npc.h `valid_npc_kind`.
    std::uint16_t kind     = 0;
    std::int16_t  level    = 1;
};

inline bool operator==(const SoldierRecord& a, const SoldierRecord& b) {
    return a.entityId == b.entityId && a.kind == b.kind && a.level == b.level;
}
inline bool operator!=(const SoldierRecord& a, const SoldierRecord& b) {
    return !(a == b);
}

// ── СЛИЯНИЕ M-71 (2026-09-24): ПЛОТНЫЙ КОНТЕЙНЕР УМЕР ─────────────────────────
// SoldierSquad / SoldierSlot / kMaxSquadSlots / souls()-развёртка вырезаны:
// существа лежат СТРОКАМИ МИРА в едином контейнере (Inventory, закон двух
// областей — items.h), все операции — дверями macro/world_row.h
// (creatures_push / pop_back / remove_one / move / add, головы, развёртка).
// Здесь остались боевой лист строки и МОНЕТА ПЕРЕНОСА души.

inline SoldierRecord make_soldier(std::uint16_t kind, int level,
                                  std::uint32_t entityId) {
    SoldierRecord s{};
    s.entityId = entityId;
    s.kind = kind;
    s.level = std::int16_t(normalize_soldier_level(level));
    return s;
}

// ══ ЦЕНА ЭФФЕКТА В РУКАХ ЭТОГО КАСТЕРА ════════════════════════════════════
// Строка спелла — каталог (`tables/spells.h`), она говорит, что делает КАСТ
// НОВИЧКА. А что из неё выйдет у конкретного тела, решают ЕГО ранги — значит
// функция анкетная, и стоять ей здесь, рядом с ранга́ми, которые она читает.

// What ONE effect cell of a spell is worth in the caster's hands.
//
// THE LAW is the project's own, said about magic: attributes ADD, skills
// MULTIPLY. The row states what a novice's casting does; the caster's
// training multiplies it through the one door that turns a rank into a
// multiplier (`skill_mult` выше в этом файле) — so magic's mastery curve is
// not a private formula and cannot drift from the rest of the sheet.
//
// Phase 5 (CANON S15): the training that scales an EFFECT is the spell's own
// SCHOOL — the line this shape was chosen for. A sleeping tag (school ==
// SkillId::Count) falls back to Spellcraft, which is exactly what every
// spell read before schools woke. The school is REQUIRED, not defaulted:
// callers hold the SpellDef and must say `spell_school(def)` — a default
// would be silently wrong for exactly the six tags that just woke.
inline Bonus spell_bonus(const Bonus& base, const Skills& caster,
                         SkillId school) {
    if (base.row == 0) return {};
    const SkillId trained =
        school != SkillId::Count ? school : SkillId::Spellcraft;
    const float scaled = float(base.value) * skill_mult(caster, trained);
    const int rounded = int(scaled < 0.0f ? scaled - 0.5f : scaled + 0.5f);
    Bonus out = base;
    out.value = std::int16_t(std::clamp(rounded, -32768, 32767));
    return out;
}

// ══ ЗАПИСЬ КОНТЕЙНЕРА, СПРОШЕННАЯ У КАТАЛОГА ═════════════════════════════════
// Четыре двери, у которых ОДИН аргумент — слот контейнера, а весь ответ лежит в
// строке каталога (`tables/npc.h`). Стоят здесь, а не там, ровно по границе
// наряда M-181: каталог не знает, в каком контейнере лежит его экземпляр.
// Умрут вместе с `SoldierRecord`, когда контейнер сольётся с инвентарём.
inline NPCType soldier_npc_type(const SoldierRecord& s) {
    return soldier_npc_type(s.kind);
}

inline int soldier_upkeep(const SoldierRecord& s) {
    return soldier_upkeep(s.kind, s.level);
}

inline int hire_price_for(const SoldierRecord& s) {
    return hire_price_for(s.kind, s.level);
}

inline int npc_hire_price_base(NPCType t) {
    const SoldierRecord preview = make_soldier(
        static_cast<std::uint8_t>(t), npc_def(t).baseLevel, 0u);
    return hire_price_for(preview);
}

// ── ЕДИНЫЙ КОНТЕЙНЕР АНКЕТЫ — Inventory 32×32 и двери над ним ─────────────
// (переехал из macro/items.h нарядом M-181: контейнер — СОСТОЯНИЕ носителя,
// колонка анкеты; каталог, из которого он читает строки, — tables/items.h.)

// 32×32 — ёмкость ЕДИНОГО контейнера (вердикт владельца 2026-09-22, эпик
// единой таблицы: «слияние и расширение до 32×32 единого контейнера у каждого
// сквада, и мобы == предметы»). Предметы и существа лежат в ОДНИХ слотах;
// пустота у деревни оплачена сознательно — «страх перед пустотой» отвергнут.
inline constexpr int kMaxInventorySlots = 1024;  // 32×32, the one container

// ── ЗАКОН ДВУХ ОБЛАСТЕЙ ЕДИНОГО КОНТЕЙНЕРА (M-71, слияние 2026-09-24) ─────
// Предметы и существа лежат в ОДНИХ 1024 слотах, но селятся с разных концов:
// предметы — СНИЗУ ВВЕРХ (первый пустой слот, как всегда), существа —
// ПЛОТНОЙ ОБЛАСТЬЮ СВЕРХУ ВНИЗ, зеркалом старого плотного контейнера. Зачем:
// закон ходока «свежие уходят первыми» (дезертирство, upkeep_window) есть
// закон ПОРЯДКА слотов, и порядок держится ЗАКОНОМ РАЗМЕЩЕНИЯ двери, а не
// тегом в слоте — тега «существо» не бывает (CANON:5599). Отображение на
// старый контейнер точное: старый slots[0..n-1] = новые [1023..first],
// новейший слот области — НАИМЕНЬШИЙ индекс; дыра от снятого слота
// затыкается НОВЕЙШИМ (зеркало swap-with-last), поэтому дырок в области
// существ не бывает по построению. Пустая середина между областями —
// оплаченная пустота (вердикт владельца).
struct Inventory {
    std::array<ItemRef, kMaxInventorySlots> slots{};

    // ── Reading ───────────────────────────────────────────────────────────
    int count_of(int defIdx) const noexcept {
        if (defIdx < 0) return 0;
        int n = 0;
        for (const ItemRef& s : slots) {
            if (!s.empty() && s.def == std::uint16_t(defIdx)) n += s.count;
        }
        return n;
    }
    int count(const std::string& id) const noexcept {
        return count_of(item_index(id));
    }
    bool has(const std::string& id) const noexcept { return count(id) > 0; }
    int total() const noexcept {
        int n = 0;
        for (const ItemRef& s : slots) n += s.count;
        return n;
    }
    int used_slots() const noexcept {
        int n = 0;
        for (const ItemRef& s : slots) if (!s.empty()) ++n;
        return n;
    }
    bool full() const noexcept { return used_slots() >= kMaxInventorySlots; }

    // Нижняя граница плотной области существ: [first, kMaxInventorySlots)
    // заняты строками существ, всё ниже — мир предметов. Пустой области —
    // kMaxInventorySlots. Цена — проход по области, не по контейнеру.
    int creature_first() const noexcept {
        int first = kMaxInventorySlots;
        while (first > 0) {
            const ItemRef& s = slots[std::size_t(first - 1)];
            if (s.empty() || world_row_is_item(s.def)) break;
            --first;
        }
        return first;
    }

    // ── Writing ───────────────────────────────────────────────────────────
    // Returns FALSE when the container has no room (owner's ruling: the thing
    // stays where it was — a refused pickup leaves the corpse holding it, a
    // refused trade rolls back whole, a town whose store is full stops
    // producing. Goods never evaporate; that is the economy's conservation
    // law).
    bool add_ref(const ItemRef& what) {
        if (what.count <= 0) return true;          // nothing to add
        for (ItemRef& s : slots) {
            if (!s.empty() && s.same_kind_as(what)) {
                // Отказ переполнения ГРОМКИЙ (закон старого контейнера, теперь
                // общий): int32-стак — единственный оставшийся кап.
                if (s.count > std::numeric_limits<std::int32_t>::max()
                                   - what.count) {
                    return false;
                }
                s.count += what.count;
                return true;
            }
        }
        if (world_row_is_item(what.def)) {
            for (ItemRef& s : slots) {
                if (s.empty()) { s = what; return true; }
            }
            return false;
        }
        // Существо: новый слот — ровно ПОД областью (плотность = закон).
        // Слот занят предметом — области столкнулись, отказ громкий.
        const int first = creature_first();
        if (first == 0) return false;
        ItemRef& s = slots[std::size_t(first - 1)];
        if (!s.empty()) return false;
        s = what;
        return true;
    }
    // By ORDINAL — what a system that already knows the row uses (the economy
    // day, the loot roll). The string forms below are the authoring-facing
    // convenience over exactly these.
    bool add_of(int defIdx, int n) {
        if (defIdx < 0 || n <= 0) return true;
        ItemRef r{};
        r.def = std::uint16_t(defIdx);
        r.count = n;
        return add_ref(r);
    }
    // By SLOT — what a panel that already stands on the exact stack uses.
    // remove_of(def, n) is blind to affixes: with procedural instances in
    // play, wearing a rolled sword and removing "a sword" by ordinal could
    // strip the PLAIN stack and leave the rolled one duplicated — the
    // conservation law broken in both directions at once.
    bool remove_at(int slot, int n) {
        if (slot < 0 || slot >= kMaxInventorySlots || n <= 0) return false;
        ItemRef& s = slots[std::size_t(slot)];
        if (s.empty() || s.count < n) return false;
        const bool creature = !world_row_is_item(s.def);
        const int first = creature ? creature_first() : 0;
        s.count -= n;
        if (s.empty()) {
            s = ItemRef{};
            // Дыра в плотной области существ затыкается НОВЕЙШИМ слотом
            // (slots[first]) — зеркало swap-with-last старого контейнера.
            if (creature && first < slot) {
                slots[std::size_t(slot)] = slots[std::size_t(first)];
                slots[std::size_t(first)] = ItemRef{};
            }
        }
        return true;
    }
    // ПРЕДМЕТНАЯ дверь: строка существа отказывается громко — её снятие
    // обязано чинить плотность области и ходит типизированной дверью
    // (macro/world_row.h), а не ординальной.
    bool remove_of(int defIdx, int n) {
        if (defIdx < 0 || n <= 0 || count_of(defIdx) < n) return false;
        if (!world_row_is_item(std::uint16_t(defIdx))) return false;
        int left = n;
        for (ItemRef& s : slots) {
            if (s.empty() || s.def != std::uint16_t(defIdx)) continue;
            const int take = s.count < left ? s.count : left;
            s.count -= take;
            left -= take;
            if (s.empty()) s = ItemRef{};
            if (left == 0) return true;
        }
        return left == 0;
    }
    bool add(const std::string& id, int n) {
        const int idx = item_index(id);
        if (n <= 0) return true;
        // An id the catalog does not know is a FAILURE, not a silent no-op:
        // with string ids a fabricated name used to land in the bag and only
        // reveal itself as "Unknown item" in the UI much later.
        if (idx < 0) return false;
        ItemRef r{};
        r.def = std::uint16_t(idx);
        r.count = n;
        return add_ref(r);
    }
    bool remove(const std::string& id, int n = 1) {
        const int idx = item_index(id);
        if (idx < 0 || n <= 0 || count_of(idx) < n) return false;
        int left = n;
        for (ItemRef& s : slots) {
            if (s.empty() || s.def != std::uint16_t(idx)) continue;
            const int take = s.count < left ? s.count : left;
            s.count -= take;
            left -= take;
            if (s.empty()) s = ItemRef{};
            if (left == 0) return true;
        }
        return left == 0;
    }
    void clear() { slots.fill(ItemRef{}); }
};
// РАЗМЕР ЗАКРЕПЛЁН (AGENTS п.10). САМАЯ ТЯЖЁЛАЯ СТРУКТУРА МИРА: 40 960 Б
// несёт КАЖДЫЙ сквад и КАЖДОЕ место. Форма НАМЕРЕННАЯ (AGENTS п.2, вердикт
// владельца 2026-09-22: единый контейнер 32×32, пустота оплачена сознательно;
// слот 40 Б — вердикт слота В, 2026-09-24); число записано здесь, чтобы
// следующий читал его, а не догадывался.
static_assert(sizeof(Inventory) == kMaxInventorySlots * sizeof(ItemRef),
              "инвентарь = 1024 плоских слота, без счётчика и без дырок");
static_assert(sizeof(Inventory) == 40960, "и это 40 960 Б ровно");

// Player combat slice consumed by `useItem` (mirrors TS inline type).
struct PlayerCombatSlice {
    int currentHp, maxHp;
    int currentMp, maxMp;
    int currentSp, maxSp;
};

// Total inventory weight in kg (sum of def.weight × count).
float inventory_weight(const Inventory& inv) noexcept;

// ── The reversible reaction (CANON «Крафт/Скрап», owner 2026-09-11/12) ─────
// FORWARD — craft: consume exactly n BATCHES of the composition (full
// price), emit n × yield WHITE base items (seed 0, no affixes — «закон
// нулевых аффиксов»: affixes are born in the world, never at a bench).
// Refuses terminal rows (nothing composes them), missing materials or a
// full bag — NOTHING ELSE (owner verdict 2026-09-12, ЗАГЛАВНЫМИ: «У НАС
// БАРТЕРНАЯ ЭКОНОМИКА... ПРОСТО ГОРОД ДЕЛАЕТ МОНЕТЫ ЧЕРЕЗ СИСТЕМУ КРАФТА
// ПО СВОЕМУ АИ»): coin is a commodity like cloth, the mint IS this door run
// by the town's own AI, and hand-striking coin is harmless by arithmetic —
// the reaction is value-neutral (the static_assert beside the table), so a
// forger earns nothing a smith doesn't. All-or-nothing on a copy: a refused
// craft leaves the bag untouched (CANON S5 — goods never evaporate).
bool craft_item(Inventory& inv, int defIdx, int n);
// REVERSE — scrap: n units of the SLOT (the instance is what is scrapped,
// not the id — a rolled sword and its bare twin are different stacks) melt
// back HALF THEIR TOTAL MATTER, floored per part: floor(n × count / (2 ×
// yield)) («закон энтропии» pooled — owner verdict 2026-09-12: one sword
// still returns 1 iron, one dagger's handle still burns whole, and 64 coins
// melt to 1 silver through this same door — no coin special case exists).
// The seed and every affix burn with no return («запрет вечного реролла» as
// arithmetic). A non-zero material byte substitutes part 0 (see
// ItemRef.material). Terminal rows refuse: raw matter has no reverse.
// All-or-nothing on a copy.
bool scrap_at(Inventory& inv, int slot, int n);
// The AI's slot hygiene (CANON: «склад не забивается говном»). While MORE
// than half the container is occupied, scrap the CHEAPEST non-fungible stacks
// (rolled/affixed instances — plain rows stack into one slot and cannot clog)
// whole, cheapest first by value_of × count, into raw matter. Returns stacks
// scrapped. The player's own bag NEVER passes through here — his scrap is a
// manual act (owner law, same CANON section).
inline constexpr int kAutoScrapSlots = kMaxInventorySlots / 2;  // 512 = 50%
int auto_scrap_overflow(Inventory& inv);

// Apply consumable effect to player. Returns the player-visible message
// (empty string if item not found, not consumable, or out of stock).
std::string use_item(Inventory& inv, const std::string& itemId, PlayerCombatSlice& pc);

// ── ЕДИНАЯ АНКЕТА ПЕРСОНАЖА — CharacterSheet и производные боя ────────────
// (сведено из macro/character_sheet.h нарядом M-181: «свести всю анкету в
// один файл» — вердикт владельца 2026-09-28. Файл велик, и это названо вслух:
// анкета есть естественно капсулированный модуль по §12.)

// ── Universal character sheet ──────────────────────────────────────────────
//
// The SINGLE representation shared by the player and every humanoid NPC. It
// bundles the persistent RPG facets — attributes, skills and the
// level/XP economy — exactly as the player carries them today (see
// `PlayerState` in macro/state.h). Combat numbers (HP/MP/SP, damage) are
// DERIVED from this sheet, never stored inside it (every body keeps its bars
// in ECS `Pools` beside its `Combat` — the player's on his squad entity).
//
// EVERY body carries one — humanoid, creature and player alike (CANON S14).
// The one birth door (`emplace_body`, sub/spawn.cpp) builds the sheet from the
// body's `kNpcTypeDefs` row, applies the leader's aura, then projects combat
// numbers from it; "monsters are sheet-less" died with the second table
// (2026-08-20).
//
// Field order mirrors the player's save layout so the same struct can later be
// embedded in `PlayerState` without changing the on-disk save bytes.
//
// Header-only (like attributes.h) so it links into every consumer — the game
// and each hand-curated test executable — with no CMake source-list edits.
struct CharacterSheet {
    Attributes attributes;
    Skills     skills;
    LevelData  levelData;
    // The 256-bit learned-perk set (CANON S26 DOD architecture) — all zeroes
    // until the constellation graph lands (stub, owner 2026-09-19). Saved
    // with the sheet (v100) so the graph arrives without a save move.
    PerkMask   perks;
};
// РАЗМЕР ЗАКРЕПЛЁН (AGENTS п.10): анкета носится ИМЕНОВАННЫМИ телами
// (npc_named), а не каждым сквадом, — потому 144 Б здесь стоят долю мира, а
// не 2.25 МиБ. 4 Б из 144 — выравнивание перед маской перков.
static_assert(sizeof(CharacterSheet) == 144,
              "анкета = атрибуты 16 + скиллы 64 + уровень 28 + перки 32 + 4");

// ── ТОРГОВАЯ СИЛА АНКЕТЫ — ОДНА ДВЕРЬ НА ВЕСЬ МИР (CANON S25) ─────────────
// Сделка двусторонняя, и обе стороны называют ОДНО число — финальное
// производное листа (attributes.h tradeDiscountPct: харизма × ранг торговли).
// Кто эта сторона — сквад, место, игрок — двери безразлично: у всякого в
// макромире есть анкета, и спрашивается только она. Отсюда спеллы, артефакты
// и перки входят в цену бесплатно.
//
// ЗАЧЕМ ДВЕРЬ, А НЕ ПОВТОР ВЫРАЖЕНИЯ. Эта же строка была написана в трёх
// местах, и ТРЕТЬЕ написание врало (поймано 2026-09-21): торговая панель
// ИГРОКА звала цену как `trade_price(база, харизма, ранг торговли)`, тогда
// как подпись двери — `trade_price(база, МОЯ сила, ЕГО сила)`. То есть
// анкета контрагента не спрашивалась вовсе, а собственный ранг Торговли
// игрока стоял на месте ЧУЖОЙ силы — и прокачка Торговли делала игрока ХУЖЕ
// покупателем. Ровно тот дефект S26, о смерти которого написано в
// attributes.h: пока сила считается выражением, а не дверью, третья копия
// заводится молча и расходится с первыми двумя.
inline int trade_power_of(const CharacterSheet& sh) {
    return calculate_derived(sh.attributes, sh.skills).tradeDiscountPct;
}

namespace csheet_detail {

// Tiny deterministic LCG (Numerical Recipes constants). Not for security — it
// exists only to make per-seed stat allocation stable and reproducible.
struct SheetRng {
    std::uint32_t s;
    std::uint32_t next() {
        s = s * 1664525u + 1013904223u;
        return s;
    }
};

// Fold three inputs into one non-zero seed (boost::hash_combine style).
inline std::uint32_t mix32(std::uint32_t a, std::uint32_t b, std::uint32_t c) {
    std::uint32_t h = a * 2654435761u;
    h ^= b + 0x9E3779B9u + (h << 6) + (h >> 2);
    h ^= c + 0x9E3779B9u + (h << 6) + (h >> 2);
    return h ? h : 0x1u;
}

// Pick an index in [0,N) with probability proportional to its weight.
template <std::size_t N>
int weighted_pick(const std::uint8_t (&w)[N], std::uint32_t roll) {
    std::uint32_t total = 0;
    for (std::size_t i = 0; i < N; ++i) total += w[i];
    if (total == 0) return 0; // degenerate row — should be impossible
    std::uint32_t r = roll % total;
    for (std::size_t i = 0; i < N; ++i) {
        if (r < w[i]) return int(i);
        r -= w[i];
    }
    return int(N - 1);
}

} // namespace csheet_detail

// THE seed of a macro leader's sheet, derived from his save-stable spawn
// ordinal (ecs::MacroSpawnId — the one identity that survives save/load).
// The macro layer never stored a leader's birth sheet seed, so every consumer
// that needs the leader AS A SHEET — the auto-resolve, the level-up ceiling
// recompute, the SP/travel caches — must derive it from the ordinal, and must
// derive it IDENTICALLY. This function is that law's single home; it used to
// be restated at each call site (squad.h twice, tests once), which is exactly
// how twin formulas drift.
inline std::uint32_t leader_sheet_seed(std::uint32_t spawnOrdinal) {
    return spawnOrdinal * 2654435761u + 0x51ADu;
}

// Procedurally builds a sheet for a humanoid NPC `role` at a given `level`.
//
// Deterministic in `seed`: no global RNG and no external Rng object — the same
// (role, level, seed) always yields the same sheet, matching the subworld's
// "everything regenerates from the seed" contract. The generator spends the
// EXACT player point economy for `level` (CANON S14, 2026-09-03): creation is
// 5 attribute points, 5 LEARN PICKS and 5 perk points, every level adds
// +1-+1-+1 (isotropy, 2026-09-14), and skill points spend ONLY into what the
// creation picks taught — so a level-N NPC is budget-identical to a level-N
// player, merely allocated toward its role. Attribute and skill pools are
// fully consumed (end at 0); perk points ACCRUE unspent while the graph is a
// stub (owner 2026-09-19) — when kPerkNodes lands, this is where the role's
// constellation vector walks its N steps (CANON S26 §5). Plot NPCs supply an
// authored sheet instead of calling this.
inline CharacterSheet make_character_sheet(NPCType role, int level,
                                           std::uint32_t seed) {
    if (level < 1) level = 1;

    CharacterSheet cs;
    cs.levelData           = default_level_data();
    cs.levelData.level     = level;
    cs.levelData.exp       = 0;
    cs.levelData.expToNext = exp_to_next_level(level);
    cs.levelData.attributePoints = 5 + (level - 1);
    cs.levelData.skillPoints     = (level - 1);
    cs.levelData.perkPoints      = 5 + (level - 1);

    const RoleWeights& w = role_weights(role);
    // TWO streams, and LEVEL is in NEITHER seed — this is what makes a
    // levelling leader's sheet MONOTONIC: the level-N sheet is the level-N-1
    // sheet plus exactly one more attribute pick and one more skill pick
    // (each loop below draws a prefix-stable sequence). The old generator
    // mixed the level into one stream, so every level-up RE-ROLLED the whole
    // sheet and a leader could visibly get WEAKER by growing — the perversity
    // hid under the fat legacy +3/level and surfaced with the 1:1 economy.
    // Растёт то, что мир хранит (S14) — a sheet only ever grows.
    csheet_detail::SheetRng attrRng{
        csheet_detail::mix32(seed, std::uint32_t(role), 0xA77Bu)};
    csheet_detail::SheetRng skillRng{
        csheet_detail::mix32(seed, std::uint32_t(role), 0x5C11u)};

    while (cs.levelData.attributePoints > 0) {
        const int pick = csheet_detail::weighted_pick(w.attr, attrRng.next());
        if (!spend_attribute_point(cs.levelData, cs.attributes,
                                   AttributeId(std::uint8_t(pick))))
            break; // safety net; the loop guard already prevents this
    }
    // CREATION: learn 5 distinct skills by the role's weights — a weighted
    // pick that lands on a known skill rerolls, and if the row runs out of
    // distinct weighted skills (a narrow beast), the pick learns the first
    // still-unknown weighted skill instead of spinning.
    while (cs.levelData.learnPicks > 0) {
        bool learned = false;
        for (int attempt = 0; attempt < 16 && !learned; ++attempt) {
            const int pick =
                csheet_detail::weighted_pick(w.skill, skillRng.next());
            learned = spend_learn_pick(cs.levelData, cs.skills,
                                       SkillId(std::uint8_t(pick)));
        }
        if (learned) continue;
        for (int i = 0; i < int(SkillId::Count) && !learned; ++i)
            if (w.skill[std::size_t(i)] > 0)
                learned = spend_learn_pick(cs.levelData, cs.skills,
                                           SkillId(std::uint8_t(i)));
        if (!learned) break;   // fewer than 5 weighted skills in the row
    }
    // LEVELS: points spend only into the learned set (THE learn law) — the
    // weights are re-read through a mask so an unknown skill cannot draw.
    while (cs.levelData.skillPoints > 0) {
        std::uint8_t known[std::size_t(SkillId::Count)] = {};
        for (int i = 0; i < int(SkillId::Count); ++i)
            if (cs.skills.rank[std::size_t(i)] > 0
                && cs.skills.rank[std::size_t(i)] < kMaxSkillRank)
                known[std::size_t(i)] = w.skill[std::size_t(i)] > 0
                                            ? w.skill[std::size_t(i)] : 1;
        const int pick = csheet_detail::weighted_pick(known, skillRng.next());
        if (!spend_skill_point(cs.levelData, cs.skills,
                               SkillId(std::uint8_t(pick))))
            break;   // every learned skill at mastery (or nothing learned)
    }
    return cs;
}

// THE application: the base sheet plus everything standing, as a COPY.
//
// Non-destructive on purpose, and it is the whole reason this file exists. The
// old `apply_aura` wrote deltas into the member's stored sheet at birth — no
// source, no removal, no recompute — so a buff outlived every reason for it.
// Here the stored sheet stays the character the player built, and what fights
// is the sum of that character and what he is currently wearing, standing in
// and blessed by. Take the item off and the next call simply does not add it.
inline CharacterSheet effective_sheet(const CharacterSheet& base,
                                      const BonusTotals& t) {
    CharacterSheet out = base;
    for (int i = 0; i < kMaxAttributes; ++i) {
        const int v = int(out.attributes.score[std::size_t(i)])
                    + int(t.attr[std::size_t(i)]);
        // A score floors at 1, never 0: the asymptotic formulas divide by
        // (score + 50) and a character reduced to nothing is a design
        // question, not an arithmetic one.
        out.attributes.score[std::size_t(i)] =
            std::uint8_t(std::clamp(v, 1, kMaxAttributeScore));
    }
    for (int i = 0; i < kMaxSkills; ++i) {
        const int v = int(out.skills.rank[std::size_t(i)])
                    + int(t.skill[std::size_t(i)]);
        out.skills.rank[std::size_t(i)] =
            std::uint8_t(std::clamp(v, 0, kMaxSkillRank));
    }
    return out;
}

// ── What a LEADER's sheet gives his squad ────────────────────────────────
//
// Owner, 2026-08-27: «хватит контекста макросквада (минимум систем) — при
// загрузке отряда в субмире они всё равно берутся из сквада/ландмарка/зон/
// таблиц, так что просто скиллы и перки СКВАДА (это и есть лидер) модифицируют
// юнитов в бою, и всё универсально».
//
// So there is no aura SYSTEM. There was one — its own header, its own
// `AuraMods` type, its own add/collect/apply verbs — and every bit of it said
// the same thing this one function says: the leader's sheet contributes
// bonuses, and bonuses are rows of the one registry. A second container for a
// number that already had one is exactly what CANON S26 forbids; killing it
// costs the game nothing because squad == leader (S4) and the leader's sheet
// was always the only source.
//
// SOURCES are the extension axis: perk rows (returning with the perk
// redesign, CANON S14), and the leader's skills, charisma and carried gear
// when their turns come. Each is a few lines appending into the same totals,
// and no consumer ever learns where a modifier came from.
//
// EMPTY since the 2026-09-03 perk purge: the one aura row (Leader → +1 vit)
// died with the perk system it hung on. The DOOR stays — every body-birth
// already walks through it — so the redesigned perks feed rows, not code.
inline BonusTotals squad_bonuses(const CharacterSheet&) {
    return BonusTotals{};
}

// Derive combat numbers for a humanoid from its CharacterSheet, layered on top
// of an authored per-role `base` (the NPC registry's CombatTemplate). Returns a
// CombatTemplate so spawn code stays a one-line swap (base → projected) before
// it fills ECS Health/Combat.
//
// The projection reuses the EXACT player formulas from attributes.h, so player
// and NPC sit on ONE combat curve:
//   hp     = (base.hp + end·10) · (1 + bodybuilding·0.05)      // calculate_combat_stats
//   damage = base.damage + (missile ? intl·(1+spellcraft·0.05)  // caster: spell stats
//                                    : str ·(1+fighter   ·0.05)) // melee : physical stats
// The authored template supplies the per-role HP/damage FLOOR plus the attack
// identity (speed / range / cooldown / kind / missile params / label), all
// preserved verbatim. Level is captured implicitly by the sheet's spent points
// (a level-N sheet has more attributes/skills), so callers MUST NOT apply an
// additional per-level multiplier on top of this — the sheet IS the scaling.
//
// Every body passes through here — a wolf exactly like a spearman (CANON S14):
// its row supplies the floor and the attack identity, its sheet supplies the
// scaling.
// The NPC's typed damage percent (CANON S13/S14, session Е 2026-09-19). An
// NPC row carries no item in a Grip, so the door the player walks through
// (anatomy.cpp hand_strike_fields — the worn weapon's own skill column)
// reads his TRAINING instead: the best-trained skill of the attack's domain.
// Melee — the eight weapon skills (the fist included); a Missile row today
// is always a CAST (Witch/Sorceress/Dragon/Cultist/Lich), so its domain is
// the six schools (the Е4 расклейка will split cast from shot honestly).
// Untrained = 100, exactly the player's bare fist. Same skill_mult_pct law,
// same 100-scale currency the strike assembly multiplies by.
inline int sheet_strike_mult_pct(const CharacterSheet& sheet,
                                 const CombatTemplate& base) {
    // A CAST reads the rank of the spell's OWN school — the very sentence the
    // player's cast reads (spell_book.cpp spell_mult_pct). A sleeping tag
    // (no school) multiplies by nothing, exactly as it does for the player.
    if (spell_ordinal_ok(base.castSpell)) {
        const SkillId school = spell_school(kSpellDefs[base.castSpell]);
        return school == SkillId::Count
                   ? 100 : skill_mult_pct(sheet.skills, school);
    }
    // A SHOT reads the SHOOTING skill, and only it: a row that looses a
    // missile is drawing a bow, whatever else its hands know. «Best of all
    // weapon skills» would have let a swordsman shoot better for his sword —
    // a lever reaching a domain it was never about.
    if (base.attackKind == CombatTemplate::Missile)
        return skill_mult_pct(sheet.skills, SkillId::Bow);
    // Otherwise it is a SWING, and the typed lever is the weapon skill. An
    // NPC row carries no item in a Grip, so the door the player walks
    // through (hand_strike_fields, the worn weapon's own skill column) reads
    // his TRAINING instead: the best-trained weapon skill, the fist
    // included. Untrained = 100, exactly the player's bare hand.
    int best = 100;
    for (int i = int(SkillId::Sword); i <= int(SkillId::Staff); ++i) {
        const int pct = skill_mult_pct(sheet.skills, SkillId(std::uint8_t(i)));
        if (pct > best) best = pct;
    }
    const int fist = skill_mult_pct(sheet.skills, SkillId::Unarmed);
    return fist > best ? fist : best;
}

inline CombatTemplate project_combat(const CharacterSheet& sheet,
                                     const CombatTemplate& base) {
    CombatTemplate out = base; // keep attack identity + label + missile params
    const BarCeilings cs =
        bar_ceilings(sheet.attributes, sheet.skills,
                     int(base.hp), base.mp, base.sp);
    const DerivedBonuses d =
        calculate_derived(sheet.attributes, sheet.skills);
    // РАСКЛЕЙКА КАСТА И ВЫСТРЕЛА (owner verdict 2026-09-17, built
    // 2026-09-19): three cases, and the ROW says which — no column has to
    // mean something it never claimed.
    //
    //   CAST  — the row names a spell: the blow IS that spell. Its dice, its
    //           damage type and the caster's INT, through the very doors the
    //           player's hand casts through (owner: «кубы СПЕЛЛА»). A caster
    //           has no dice of his own, exactly like the player.
    //   SHOT  — Missile with no spell named: dice + typed skill + LCK and NO
    //           attribute add (CANON S14 «урон стрелкового БЕЗ добавки
    //           атрибута» — range is the compensation).
    //   SWING — Melee: the row's dice plus STR, as always.
    //
    // Until the spell column existed, `Missile` MEANT "caster", so the first
    // NPC archer would have drawn an INT bonus from a column about delivery.
    const SpellDef* cast = spell_ordinal_ok(base.castSpell)
                               ? &kSpellDefs[base.castSpell] : nullptr;
    float atkBonus = 0.0f;
    if (cast) {
        out.dice    = cast->dice;
        out.dmgType = spell_damage_type(*cast);
        atkBonus    = float(d.rawSpellDamage);
    } else if (base.attackKind != CombatTemplate::Missile) {
        atkBonus    = float(d.rawPhysDamage);
    }
    out.hp      = float(cs.maxHp);
    // Attributes ADD to the row's dice (CANON S14: «атрибуты складывают»),
    // floored to the int house — the strike assembly (roll_strike) does the
    // rest. The sheet's LCK rides along for the crit door.
    out.flatAdd = std::int16_t(std::floor(atkBonus));
    out.luck    = std::uint8_t(sheet.attributes.of(AttributeId::Lck));
    // TEMPO through the same recovery door as the player's hand (CANON S14
    // «один рычаг», 2026-09-07 — S4: спец-кода игрока нет, значит и спец-
    // кривой НПЦ нет): the row's authored cooldown is the BASE, the sheet's
    // Spd + the generic of the attack's domain divide it — a quick veteran
    // bandit genuinely strikes faster than a peasant with the same club. One
    // rounding, in the door; seconds again for the float carrier the strike
    // pass converts per swing (steps_from_seconds).
    // The GENERIC of the act's own domain: Spellcraft paces a CAST, and
    // Armsmaster paces every physical act — a swing and a SHOT alike (a bow
    // is drawn by arms, not by a casting hand; before the split, the row's
    // delivery column decided this too).
    out.cooldown = seconds_from_steps(std::uint32_t(recovery_steps(
        base.cooldown, sheet.attributes, sheet.skills,
        cast ? SkillId::Spellcraft : SkillId::Armsmaster)));
    // The POWER half of the same split: the typed skill multiplies the dice
    // (the door above already gave the generic pair to TEMPO — one handle,
    // one lever). Both consumers read THIS field now: the fought body
    // (spawn combat_from_sheet) and the auto-resolve (fighter_power) — the
    // two ends of S13's one law of battle, moved in one commit on purpose.
    out.multPct = std::int16_t(sheet_strike_mult_pct(sheet, base));
    return out;
}

// THE whole-number bar a body of this sheet carries — one question, one
// answer. It was written twice, once per birth (sub/spawn.cpp's derived body
// and macro/npc_spawn.cpp's tracked one), and a wound crosses between those
// two layers as a FRACTION of exactly this number: the day the two floors
// disagreed by one point, the crossing would have leaked hp in one direction.
inline int body_max_hp(const CharacterSheet& sheet, const CombatTemplate& base) {
    return std::max(1, int(std::floor(project_combat(sheet, base).hp)));
}

// THE mana bar of a body of this sheet — the same question as body_max_hp,
// asked of the other pool, and answered by the same derivation
// (bar_ceilings: the WILL bar times Meditation over the row's `mp` floor).
//
// It had no door because it had no readers: `project_combat` computed maxMp
// on every single birth in the game and dropped it on the floor, so mana was
// the player's private property and every other body in the world was a
// cripple with one bar (owner, 2026-09-09: «это РПГ, у всех должна быть HP SP
// MP»). The row joins the derivation exactly the way `base.hp` joins above
// (§41 root 2): its `mp` column IS the species' well — 100 for everyone
// until a row says otherwise.
inline int body_max_mp(const CharacterSheet& sheet,
                       const CombatTemplate& base) {
    return std::max(0, bar_ceilings(sheet.attributes, sheet.skills,
                                    int(base.hp), base.mp, base.sp).maxMp);
}

// THE stamina bar — the third of the three, same door shape, same reason:
// every caller that spelled `bar_ceilings(...).maxSp` inline was quietly
// accepting the smuggled 100 for a row it never consulted. `std::max(1,…)`
// belongs to the door: SP is the divisor of fatigue everywhere it is read.
inline int body_max_sp(const CharacterSheet& sheet,
                       const CombatTemplate& base) {
    return std::max(1, bar_ceilings(sheet.attributes, sheet.skills,
                                    int(base.hp), base.mp, base.sp).maxSp);
}

// ── ЭКИПИРОВКА АНКЕТЫ — Gear: слоты глобальной раскладки, надетое = ИНДЕКС
// В ИНВЕНТАРЬ (M-183, вердикты владельца 2026-09-28) ───────────────────────
//
// Анатомия как СИСТЕМА умерла: плана тела (AnatomyDef) больше нет, слот
// адресуется `тип×16+n` одинаково для всех (tables/body_parts.h), «есть ли у
// тела слот» — БИТ экземплярной маски (редактор тел: отрастить конечность =
// поставить бит, в рантайме, без новой строки контента).
//
// ИСТИНА ОДНА — ИНВЕНТАРЬ: надетая вещь ЛЕЖИТ в контейнере анкеты, ячейка
// хранит лишь `uint16`-индекс её слота (вердикт: «надо ещё знать что одето
// но это я думаю можно просто указатель на место в инвентаре»; «отлично
// индекс идеально»). Двух копий вещи больше не существует (DOD п.6).
// «Нет элемента» — ПОСЛЕДНЕЕ значение типа, не −1 (закон узкого индекса).
inline constexpr std::uint16_t kWornNothing = 0xFFFFu;
// Ячейка занята БЛОКОМ соседа (двуручник в Grip занимает OffGrip). Маркер
// ПРОИЗВОДНЫЙ: блоки не хранятся истиной, а пересчитываются от надетых строк
// (remark_gear_blocks) — вторая копия того, что каталог уже говорит
// колонкой blocksMask, разъехалась бы с ним молча.
inline constexpr std::uint16_t kWornBlocked = 0xFFFEu;
static_assert(kMaxInventorySlots <= int(kWornBlocked),
              "оба маркера обязаны лежать ВЫШЕ любого законного индекса слота");

constexpr std::array<std::uint16_t, std::size_t(kEquipCells)> worn_empty() {
    std::array<std::uint16_t, std::size_t(kEquipCells)> a{};
    for (std::uint16_t& v : a) v = kWornNothing;
    return a;
}

// РАЗМЕР ЗАКРЕПЛЁН: 64 (маска тела) + 480×2 (ячейки) = 1024 Б ровно — po2
// даром; по капу 32768 сквадов это 32 МиБ против 160 МиБ прежней формы
// (worn[128] × ItemRef 40 Б): система ушла, и 128 МиБ вместе с ней.
struct Gear {
    SlotMask has{};   // тело ЭКЗЕМПЛЯРА; рождается от колонки slots строки существа
    std::array<std::uint16_t, std::size_t(kEquipCells)> worn = worn_empty();
};
static_assert(sizeof(Gear) == 1024, "экипировка анкеты = маска 64 + 480 ячеек по 2");

// Рождение тела: маска — от строки существа (tables/npc.h `slots`), ячейки пусты.
void gear_init(Gear& g, const SlotMask& bodySlots);

// Подходит ли строка каталога этой ячейке: слот у тела есть, тип ячейки
// назван маской строки. Пустота ячейки здесь НЕ проверяется — это вопрос
// надевания, не совместимости.
bool item_fits_cell(const Gear& g, int cell, const ItemDef& def);

// Надеть вещь ИЗ СЛОТА ИНВЕНТАРЯ. Возвращает ячейку или -1 — ОТКАЗ, никогда
// не молчаливая потеря: вещь остаётся в инвентаре в любом исходе (она и так
// там — надевание лишь ставит указатель). Стак > 1 расщепляется: одна штука
// уезжает в свободный предметный слот, ячейка указывает на неё (ячейка
// держит РОВНО ОДИН экземпляр по построению).
int equip(Gear& g, Inventory& inv, int invSlot);
int equip_at(Gear& g, Inventory& inv, int invSlot, int cell);

// Снять ячейку. Вещь никуда не движется — она уже в инвентаре; освобождаются
// блоки. false = ячейка пуста или занята блокером.
bool unequip(Gear& g, const Inventory& inv, int cell);

// Надет ли этот слот инвентаря на теле — страж продажи/скрапа/выброса:
// вещь, на которую смотрит ячейка, не смеет уйти из инвентаря молча.
bool slot_is_worn(const Gear& g, int invSlot);

// Пересчитать производные маркеры блоков от надетых строк (после загрузки и
// после каждого надевания/снятия); индекс, чей слот инвентаря опустел,
// снимается — вещи нет, значит она не надета.
void remark_gear_blocks(Gear& g, const Inventory& inv);

int worn_cells(const Gear& g);
BonusTotals worn_bonuses(const Gear& g, const Inventory& inv);
// ── ЗАЩИТА ТЕЛА — ОДНА ДВЕРЬ НА ОБА КОНЦА БОЯ (CANON S13, наряд M-193) ────
// Эффективная защита по ОДНОЙ колонке урона: строка существа × обучение
// носителя + надетое × ранги своих родов + «Без брони», если не надето ничего.
// `row` передаётся ЗНАЧЕНИЕМ, а не как `NpcTypeDef`, сознательно: этот
// заголовок остаётся листом над attributes.h, чтобы каталог предметов включал
// анкету без реестра существ за ней.
//
// ОДНА ДВЕРЬ, ДВА ЧИТАТЕЛЯ: сцена (`defense_of@src/sub/damage.cpp`) и
// авторезолв (`fighter_power@src/macro/auto_battle.h`). Два ответа на «сколько
// держит тело» были бы двумя законами боя (S13), поэтому сборка живёт здесь, а
// не по разу на каждой стороне.
DefenseSum body_defense(const Defense& row, const Skills& skills,
                        const Gear* g, const Inventory* inv, DamageType type);
// Половина этой сборки, нужная панели и свидетелям поимённо: что даёт НАДЕТОЕ.
DefenseSum worn_defense(const Gear& g, const Inventory& inv,
                        const Skills& skills, DamageType type);
// Гейт строки «Без брони»: надето ли хоть что-то рода `ItemType::Armor`.
bool wears_armor(const Gear& g, const Inventory& inv);

// ── ПРОСТОЙ БРОНИ — ВТОРОЙ СУБЪЕКТ ЗАКОНА ВОССТАНОВЛЕНИЯ (M-194) ──────────
// Суммарный вес НАДЕТОЙ брони (кг) — база простоя по закону массы, тому же,
// которым вес оружия задаёт темп замаха.
float worn_armor_weight(const Gear& g, const Inventory& inv);
// ...и сама база, пропущенная через дверь темпа: шаги простоя, которые ставит
// удар, дошедший сквозь блок. НОЛЬ, если брони не надето — и это не ветка, а
// предельный случай: у голого тела и у вросшей шкуры сбивать нечего, поэтому
// «рековери у строки существа нет вовсе» выпадает из закона САМО.
int armor_recovery_steps(const Gear& g, const Inventory& inv,
                         const Attributes& a, const Skills& s);
const ItemDef* weapon_in_hand(const Gear& g, const Inventory& inv);

// The percent a CREATURE ROW's own defence is multiplied by (npc_def().defense —
// a troll's hide: bodies with no gear at all). Its род is in the WEARER's
// training: the best-trained of the living armour skills. A beast trains
// none of them and stays ×1, so the world's monsters do not silently thicken.
inline int sheet_armor_mult_pct(const Skills& skills) {
    int best = 100;
    for (SkillId id : {SkillId::HeavyArmor, SkillId::LightArmor,
                       SkillId::Shield}) {
        const int pct = skill_mult_pct(skills, id);
        if (pct > best) best = pct;
    }
    return best;
}

// What a sheet strikes with, given what its body holds — the ONE assembly of
// the strike fields (dice + type + attribute add + skill percent + LCK) that
// every carrier of ecs::Combat copies from.
struct StrikeFields {
    Dice          dice{};
    DamageType    dmgType = DamageType::Blunt;
    std::int16_t  flatAdd = 0;
    std::int16_t  multPct = 100;
    std::uint8_t  luck    = 0;
    // Steps the arm needs between blows — the recovery door's verdict over
    // the mass law's base. Default = the bare hand at a zero sheet
    // (kHandSwingS × 64), so a fields{} harness literal swings honestly.
    int           recoverySteps = 96;
    Delivery      delivery = Delivery::Melee;
    float         range    = 0.0f;
};
StrikeFields hand_strike_fields(const Attributes& attributes,
                                const Skills& skills, const Gear* g,
                                const Inventory* inv);

} // namespace sm

// Включение стоит ЗДЕСЬ, а не наверху файла, и это не небрежность:
// заголовок пользуется контуром только в блоке ниже, а вставленная
// наверху строка сдвинула бы ВСЁ содержимое на единицу и сгноила бы
// каждую ссылку документов в этот файл (замер: одна такая вставка в 17
// заголовков сломала 10 ссылок в SKELETON.md и промтах). Номер строки —
// производное (§13 п.5), и дешевле не двигать его вовсе.
#include "core/row_law.h"
// ── КОНТУР СТРОК ЭТОГО ФАЙЛА ────────────────────────────────────────────────
// Судит КОМПИЛЯТОР, а не ревью: строка мира, в которую заложили вектор, строку,
// карту, умный указатель, функтор или виртуальный метод, отсюда НЕ СОБЕРЁТСЯ, и
// сообщение назовёт тип поимённо (`core/row_law.h`, наряд M-169).
// Новая структура в этом файле обязана появиться и в этом списке — за полнотой
// списка следит `arch_guard_test`, иначе стену обходили бы молча, новым типом.
TIMAERT_ROW(sm::Attributes);
TIMAERT_ROW(sm::Skills);
TIMAERT_ROW(sm::PerkMask);
TIMAERT_ROW(sm::BarCeilings);
TIMAERT_ROW(sm::DerivedBonuses);
TIMAERT_ROW(sm::LevelData);
TIMAERT_ROW(sm::BonusTotals);
TIMAERT_ROW(sm::PoolSlice);
TIMAERT_ROW(sm::SoldierRecord);
TIMAERT_ROW(sm::Inventory);
TIMAERT_ROW(sm::PlayerCombatSlice);
TIMAERT_ROW(sm::CharacterSheet);
TIMAERT_ROW(sm::csheet_detail::SheetRng);
TIMAERT_ROW(sm::Gear);
TIMAERT_ROW(sm::StrikeFields);

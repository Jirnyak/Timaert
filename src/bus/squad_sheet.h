// ДВЕРИ АНКЕТЫ НАД СТОРОМ — слой между арифметикой листа (bus/anketa.h, мир
// там не виден) и колонками рода 2 (bus/store.h, анкета там не суммируется).
//
// Переехали из macro/squad.h ломтём 5 M-171 ЦЕЛЫМ КЛАСТЕРОМ: «дверь рода 2
// формализуется НА шине» (вердикт владельца 2026-11-03), и каждое тело здесь
// читает ТОЛЬКО MacroStore + bus/anketa.h + tables/ — GameState не трогает
// ни одно. В bus/store.h им нельзя по §5 п.13: standing_bonuses_of сканирует
// 480 ячеек тела + 10 ординалов спеллов, а store.h включает каждый, кто
// спрашивает любую колонку. В bus/anketa.h — нельзя по слою: там нет стора.
//
// ЗАКОН АНКЕТЫ: «анкета источник»; сквад растёт ЧЕРЕЗ СВОЮ АНКЕТУ, потолки
// полос следуют за листом через ОДНУ дверь пересборки, и пересчёт живёт
// ВНУТРИ дверей изменения (разряд А, вердикт «давай А») — забыть его
// невыразимо.
#pragma once

#include "bus/anketa.h"
#include "bus/spell_book_state.h"
#include "bus/store.h"
#include "tables/npc.h"
#include "tables/spells.h"

#include <algorithm>
#include <cmath>

namespace sm {

// THE «sheet → body» refresh — ONE door for every body of the macro map,
// the player's squad included: the sheet is the law, and everything derived
// from it — the three bar ceilings AND the four cached scalars a march reads
// (ecs::MacroNpcRuntime) — is refreshed here and nowhere else. Called by
// make_npc at birth, by squad_award_exp when a level changes the sheet, by
// the player's creation / point-spend / gear-change moments through the same
// signature. It grew out of `refresh_leader_travel_stats` (maxSp + caches)
// when landing 4 made the lord's fraction-preserving level-up rescale THE
// rescale for all three bars of every body — before that the player's
// ceilings moved by «keep the number, clamp» in one place and a full heal in
// another, while the lord's kept the fraction: three laws for one event.
// The KIND is required, not defaulted: the back this avatar hauls with is a
// column of his row (npc.h NpcTypeDef::haulMult), and a default of "a person"
// would be silently wrong for exactly the rows that matter — a caravan given a
// man's shoulders is a caravan that can no longer afford to travel. A missing
// argument should be a compile error, not a stranded trade route.
// `standing` — what stands on the body beyond its sheet (worn Derived rows:
// MovePct, CarryKg — bonus.h affix tail). nullptr = nothing stands, which is
// every avatar until macro NPCs wear gear; the sheet passed in is already the
// EFFECTIVE one, so only the derived cells are read from it here.
// `pools` is the body whose bars this sheet caps: the ceiling and the value
// it caps must be refreshed by one door, or `maxSp` drifts away from the `sp`
// it bounds — the drift the old helper was written to prevent. `rt` may be
// null for a body with no march (none exists today on the macro map; the
// argument keeps the door honest about what is bars and what is legs).
inline void refresh_body_from_sheet(ecs::Pools& pools,
                                    ecs::MacroNpcRuntime* rt,
                                    const CharacterSheet& sheet,
                                    NPCType type,
                                    const BonusTotals* standing = nullptr) {
    // ── The ceilings, each bar preserving its FRACTION ────────────────────
    // Owner 2026-09-10: «доля у всех» — the lord's level-up law is now the
    // ONLY rescale in the game, the player's included. A level, a spent point
    // or a donned coat is never a free heal and never a theft: the ceiling
    // follows the sheet, the fraction is what survives. A bar whose ceiling
    // did not move is not touched AT ALL — this door is walked on every macro
    // tick of the player's squad, and an unconditional rescale would be a
    // slow rounding drain, not an identity. Dead stays dead: a growing
    // ceiling must not resurrect a zero hp. An exhaustion DEBT (sp <= 0)
    // survives any rescale as-is — levelling mid-collapse does not forgive
    // it. The roundings are the ones the lord's level-up always used
    // (truncation on hp/mp, lround on sp) — kept verbatim so the one door is
    // byte-identical to the law it absorbed.
    {
        const int newMax = body_max_hp(sheet, npc_def(type).combat);
        if (newMax != pools.maxHp) {
            const float frac = pools.maxHp > 0
                ? std::clamp(float(pools.hp) / float(pools.maxHp), 0.0f, 1.0f)
                : 1.0f;
            pools.maxHp = newMax;
            if (pools.hp > 0) {
                pools.hp = std::clamp(int(float(newMax) * frac), 1, newMax);
            }
        }
    }
    {
        const int newMax = body_max_mp(sheet, npc_def(type).combat);
        if (newMax != pools.maxMp) {
            const float frac = pools.maxMp > 0
                ? std::clamp(float(pools.mp) / float(pools.maxMp), 0.0f, 1.0f)
                : 1.0f;
            pools.maxMp = newMax;
            pools.mp = std::clamp(int(float(newMax) * frac), 0, newMax);
        }
    }
    {
        const int newMax = body_max_sp(sheet, npc_def(type).combat);
        if (newMax != pools.maxSp) {
            const float frac =
                float(pools.sp) / float(std::max<int>(1, pools.maxSp));
            pools.maxSp = newMax;
            if (pools.sp > 0) {
                pools.sp = std::clamp(
                    int(std::lround(frac * float(newMax))), 1, newMax);
            }
        }
    }

    // ── The march caches, when the body has legs ──────────────────────────
    if (!rt) return;
    rt->travelRank = std::uint8_t(
        std::clamp(sheet.skills.of(SkillId::Travel), 0, kMaxSkillRank));
    rt->marathonRank = std::uint8_t(
        std::clamp(sheet.skills.of(SkillId::Marathon), 0, kMaxSkillRank));
    rt->scoutRank = std::uint8_t(
        std::clamp(sheet.skills.of(SkillId::Scouting), 0, kMaxSkillRank));
    // The cache is the walk's own float; the LAW is whole percent (4в).
    rt->moveMult = float(
        (standing
             ? calculate_derived(sheet.attributes, sheet.skills, *standing)
             : calculate_derived(sheet.attributes, sheet.skills))
            .moveSpeedPct)
        / 100.0f;
    // СВОЯ СПИНА — ЭТО АНКЕТА И ЕЁ СИЛА, И КОЛОНКИ СТРОКИ ЗДЕСЬ НЕТ ВОВСЕ
    // (ЗАКОН ДВУХ ТАБЛИЦ, вердикт владельца 2026-10-20: «атрибут сила за
    // пернеосимый вес … а существа дают доп вес»). Здесь стоял множитель
    // `haul > 0.0f ? haul : 1.0f`, и он врал ДВАЖДЫ: переворачивал АВТОРСКИЙ
    // НОЛЬ колонки (та объявляет безрукие строки прямо — «ни кролика, ни
    // волка, ни ястреба, ни дракона в этом мире не запрягают», а умолчание
    // колонки есть 1.0, так что перехватить он мог ТОЛЬКО авторский ноль) и
    // делал переносимый вес сквада функцией его СТРОКИ, тогда как строка —
    // понятие таблицы ОБЪЕКТОВ, а сквад есть АНКЕТА. `haulMult` читается
    // теперь ровно в одном месте — по головам инвентаря, дверью
    // `carry_capacity_g@src/macro/squad.h`.
    rt->carryPerSoulG = standing
        ? get_carry_capacity(sheet.attributes, sheet.skills, *standing)
        : get_carry_capacity(sheet.attributes, sheet.skills);
}

// Лист по хэндлу — ОДИН ОТВЕТ: КОЛОНКА. Протухший хэндл отвечает дефолтным
// деривативом — читатель обязан был спросить valid() раньше.
// (Дуализм owned/derived умер 2026-10-18; его полная история и цена числом
// — у предиката владения `sheet_owned_at@src/macro/squad.h`.)
inline CharacterSheet sheet_of(const MacroStore& st, MacroHandle h) {
    if (!st.valid(h))
        return make_character_sheet(NPCType::Peasant, 1, avatar_sheet_seed(0));
    return st.sheet[h.slot];
}

// WHAT STANDS ON A MACRO BODY, summed once: what it is wearing
// (ecs::BodyEquipment) and what is burning on it (the sustained bits of its
// own SpellBook) — both opt-in components any macro body may carry, both
// rows of the one bonus registry. Grew out of player_standing_bonuses
// (посадка Б): the player is this door's ordinary case — the coat hangs on
// his squad entity like on any lord's. Sustained magnitudes are scaled by
// the BASE training on purpose: the standing sum cannot read the sheet it
// is itself a term of.
// Сам закон суммирования — ОДИН, от указателей: слот-дверь ниже зовёт его,
// второй копии закона не существует (метод §5 п.1).
inline BonusTotals standing_bonuses_sum(const ecs::BodyEquipment* eq,
                                        const Inventory* inv,
                                        const SpellBook* book,
                                        const Skills& base) {
    BonusTotals t{};
    if (eq && inv) t += worn_bonuses(eq->gear, *inv);
    if (book) {
        for (int ord = 0; ord < kSpellCount; ++ord) {
            if (!spellbook_has_sustained(*book, ord)) continue;
            const SpellDef& def = kSpellDefs[ord];
            for (const Bonus& b : def.effects) {
                accumulate(t, spell_bonus(b, base, spell_school(def)));
            }
        }
    }
    return t;
}
// Дверь по хэндлу — целиком по колонкам.
inline BonusTotals standing_bonuses_of(const MacroStore& st, MacroHandle h) {
    if (!st.valid(h)) return BonusTotals{};
    return standing_bonuses_sum(&st.gear[h.slot], &st.inventory[h.slot].inv,
                                &st.spellBook[h.slot],
                                sheet_of(st, h).skills);
}

// The sheet the world should actually ask about ANY macro body — THE
// effective door (phase 4 law, generalized by посадка Б): the character it
// is (sheet_of) PLUS everything standing on it. Every reader of a body's
// numbers (bars, damage, march, carry, prices, XP, the daily bread law)
// walks through here; writes (level-up, learning) go to the OWNED base
// sheet, never to this copy.
// Эффективный лист по хэндлу — та же композиция, оба слагаемых по колонкам.
inline CharacterSheet effective_sheet_of(const MacroStore& st, MacroHandle h) {
    return effective_sheet(sheet_of(st, h), standing_bonuses_of(st, h));
}

// ── ПЕРЕСЧЁТ ПРОИЗВОДНЫХ АНКЕТЫ — ОДНА ДВЕРЬ УРОВНЕМ СКВАДА (M-301) ──────
// Вся сводка в одном месте: что на скваде стоит (`standing`), какой у него
// эффективный лист, какая строка — и единственная дверь листа. Ниже стоят
// ДВЕРИ КЛАССОВ ИЗМЕНЕНИЙ, и каждая кончается этим звонком, поэтому забыть
// пересчёт в них невыразимо (разряд А, вердикт владельца «давай А»).
// (Полная история пяти ответов на «свежа ли производная» и цена трёх
// лишних — П-26 — записана рядом M-301 реестра.)
inline void refresh_squad_body(MacroStore& st, MacroHandle h) {
    if (!st.valid(h)) return;
    const BonusTotals bt = standing_bonuses_of(st, h);
    refresh_body_from_sheet(st.pools[h.slot], &st.runtime[h.slot],
                            effective_sheet(st.sheet[h.slot], bt),
                            squad_row_type(st, h.slot), &bt);
}

// Pay a squad's victory. XP flows through the ONE reward law
// (npc_xp_reward) and is consumed by the SAME curve the player climbs
// (exp_to_next_level, inside award_exp); a level gained recomputes the
// body's ceilings from its OWN grown sheet while PRESERVING the wound
// fraction — the currency wounds already travel in. This is what makes the
// "wandering tsar" a data row: any squad that wins fights, levels.
// ── ОПЛАТА УБИЙСТВА: ОДНА дверь роста для ВСЯКОГО сквада ─────────────────
// §41 корень 5, вердикт владельца 2026-09-10: «байт умирает; жнец резолвит
// лидера убийцы для ВСЕХ — игрок просто лидер своего сквада». Всякий сквад
// растёт ЧЕРЕЗ СВОЮ АНКЕТУ: exp в лист с WIS-дивидендом его эффективного
// листа, очки атрибутов/скиллов КОПЯТСЯ нетраченными до контента трат
// (учителя/ИИ-траты — вердикт «копить»); потолки полос следуют за листом
// через ту же одну дверь пересборки.
// (Второй закон роста, стиравший анкеты перекатом из сида, слит 2026-10-18
// — вердикт «одна дверь роста для всех»; его история и цена — у двери
// владения листом в macro/squad.h и в ряду M-246а.)
inline int squad_award_exp(MacroStore& st, MacroHandle h, int xp) {
    if (xp <= 0 || !st.valid(h)) return 0;
    const std::uint16_t slot = h.slot;
    CharacterSheet& own = st.sheet[slot];
    // ЧТО СТОИТ НА СКВАДЕ — СЧИТАЕТСЯ ОДИН РАЗ И ЕДЕТ ДАЛЬШЕ ЦЕЛИКОМ (M-301).
    // Здесь дважды звалась `effective_sheet_of`, то есть сумма надетого
    // считалась ДВА раза, и — хуже — второй её результат уходил в дверь
    // БЕЗ ЧЕТВЁРТОГО АРГУМЕНТА. Эффективный лист несёт только ячейки
    // атрибутов и рангов; ПРОИЗВОДНЫЕ ячейки надетого (`MovePct`, `CarryKg`
    // — хвост реестра бонусов) дверь накладывает отдельно, по `standing`.
    // Значит пересчёт на уровне-апе СТИРАЛ их из обеих кэшированных колонок:
    // надетый аффикс `of_the_mule` пропадал из спины `carryPerSoulG`, а
    // хаста — из `moveMult`, и возвращались они только переодеванием.
    //
    // СЕГОДНЯ ЭТО СТРЕЛЯЕТ ТОЛЬКО ПО ИГРОКУ, И ЭТО ЗАМЕРЕНО, А НЕ ВЫВЕДЕНО:
    // зонд на 3 сидах × 128 дней дал 15 уровней-апов, из них с непустыми
    // производными ячейками — НОЛЬ, потому что макро-сквад не носит ничего
    // (`gear_init` ставит пустые ячейки при рождении, надевает только игрок
    // через вкладку снаряжения и консоль). Правка всё равно нужна: пустота
    // здесь — свойство КОНТЕНТА, а не формы, и первый надетый НПЦ-плащ сделал
    // бы дефект живым молча.
    const BonusTotals standing = standing_bonuses_of(st, h);
    const CharacterSheet eff = effective_sheet(own, standing);
    const int gained = award_exp(
        own.levelData, xp,
        calculate_derived(eff.attributes, eff.skills).expMultPct);
    if (gained > 0) {
        // Потолки и доли следуют за листом через ТУ ЖЕ одну дверь пересчёта
        // — она сама читает колонки слота ПОСЛЕ роста (`own` — ссылка на
        // колонку, и `award_exp` уже сдвинул её `levelData`). Сумма надетого
        // считается внутри двери второй раз, и это осознанно: разойтись она
        // не может (между двумя звонками мир не менялся), а цена — один скан
        // против своего пути пересчёта рядом с общим.
        refresh_squad_body(st, h);
    }
    return gained;
}

// ── ДВЕРЬ КЛАССА «ТРАТА ОЧКОВ» (M-301) ───────────────────────────────────
// Уровень даёт ОЧКИ, а лист двигает их ТРАТА — значит классов изменений тут
// два, и у второго три входа: атрибут, ранг умения, изучение с нуля. Низкие
// двери (`spend_*@src/bus/anketa.h`) остаются и остаются ПУБЛИЧНЫМИ: их
// законно зовёт создание персонажа (`src/ui/screens.cpp`) и прокат листа
// (`make_character_sheet`), где тела ещё нет вовсе и пересчитывать нечего.
// Разница названа вслух: ниже — АРИФМЕТИКА ЛИСТА, здесь — ИЗМЕНЕНИЕ АНКЕТЫ
// ЖИВОГО СКВАДА, и только у второго есть производные.
inline bool squad_spend_attribute(MacroStore& st, MacroHandle h,
                                  AttributeId id) {
    if (!st.valid(h)) return false;
    CharacterSheet& own = st.sheet[h.slot];
    if (!spend_attribute_point(own.levelData, own.attributes, id)) return false;
    refresh_squad_body(st, h);
    return true;
}
inline bool squad_spend_skill(MacroStore& st, MacroHandle h, SkillId id) {
    if (!st.valid(h)) return false;
    CharacterSheet& own = st.sheet[h.slot];
    if (!spend_skill_point(own.levelData, own.skills, id)) return false;
    refresh_squad_body(st, h);
    return true;
}
inline bool squad_spend_learn(MacroStore& st, MacroHandle h, SkillId id) {
    if (!st.valid(h)) return false;
    CharacterSheet& own = st.sheet[h.slot];
    if (!spend_learn_pick(own.levelData, own.skills, id)) return false;
    refresh_squad_body(st, h);
    return true;
}

} // namespace sm

// Squad lifecycle helpers — the macro side of "THE macro entity is a squad"
// (CANON S4/S13 (бывший macrosim.md), ecs::SquadUpkeep doctrine). The squad IS its leader entity;
// what lives here is what happens to the creatures around the leader's own
// life and death. Header-only: pure ECS + army.h record moves, no engine,
// no renderer, so every layer (subworld leave, the coming auto-resolve,
// tests) settles squads through the same functions.
#pragma once

#include "macro/anketa.h"
#include "macro/auto_battle.h"
#include "core/rng.h"
#include <cstdio>
#include "macro/currency.h"
#include "macro/econ_day.h"   // EconFact/EconFactSink — ведомость склада душ
#include "macro/landmark_registry.h"
#include "macro/macro_stock.h"
#include "macro/player_entity.h"
#include "macro/spell_book_state.h"
#include "macro/anketa.h"
#include "macro/squad_walk.h"
#include "macro/state.h"
#include "macro/zones.h"
#include "macro/store.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace sm {

// THE «sheet → body» refresh — ONE door for every body of the macro map,
// the player's squad included: the sheet is the law, and everything derived
// from it — the three bar ceilings AND the four cached scalars a march reads
// (ecs::MacroNpcRuntime) — is refreshed here and nowhere else. Called by
// make_npc at birth, by award_kill_xp when a level changes the sheet, by
// the player's creation / point-spend / gear-change moments through the same
// signature. It grew out of `refresh_leader_travel_stats` (maxSp + caches)
// when landing 4 made the lord's fraction-preserving level-up rescale THE
// rescale for all three bars of every body — before that the player's
// ceilings moved by «keep the number, clamp» in one place and a full heal in
// another, while the lord's kept the fraction: three laws for one event.
// The KIND is required, not defaulted: the back this leader hauls with is a
// column of his row (npc.h NpcTypeDef::haulMult), and a default of "a person"
// would be silently wrong for exactly the rows that matter — a caravan given a
// man's shoulders is a caravan that can no longer afford to travel. A missing
// argument should be a compile error, not a stranded trade route.
// `standing` — what stands on the body beyond its sheet (worn Derived rows:
// MovePct, CarryKg — bonus.h affix tail). nullptr = nothing stands, which is
// every leader until macro NPCs wear gear; the sheet passed in is already the
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
    // теперь ровно в одном месте — по головам инвентаря, дверью ниже.
    rt->carryPerSoulG = standing
        ? get_carry_capacity(sheet.attributes, sheet.skills, *standing)
        : get_carry_capacity(sheet.attributes, sheet.skills);
    // (ЗДЕСЬ СТОЯЛО `rt->carryCap = rt->carryPerSoul` — засев второй колонки,
    //  которую потом догоняла `refresh_squad_carry`. Обе снесены 2026-10-20:
    //  весь обоз теперь СКЛАДЫВАЕТСЯ У МЕСТА УПОТРЕБЛЕНИЯ дверью
    //  `carry_capacity_g` ниже, поэтому «засеять, а потом не забыть
    //  догнать» перестало существовать как форма.)
}

// ── ОБОЗ = СВОЯ СПИНА ОТ АНКЕТЫ ПЛЮС ГРАММЫ ОБЪЕКТОВ ─────────────────────
// CANON S10 дословно: «берёт по своей грузоподъёмности — СУММА листов
// членов», и вердикт владельца 2026-10-20 назвал форму суммы: «предметы/
// существа должны давать грузоподъёмность из анкеты просто ПЛЮСОВАТЬ а не
// множить … у теб есть 10 коней то есть ты носишь +стока коней веса (ну и
// твой базовый вес от силы)». Значит:
//
//     обоз = спина АНКЕТЫ (её СИЛА) + Σ haulMult(строка) × kHaulBackG × count
//
// Авторский ноль колонки честно даёт НОЛЬ: волк, кролик, ястреб и дракон в
// контейнере не несут ничего, потому что их не запрягают, — и ветки «а если
// ноль, считай единицей» здесь больше нет (она переворачивала ровно
// авторское высказывание, M-297).
//
// ЗДЕСЬ СТОЯЛА ДОЛЯ, И ОНА БЫЛА СМЕШЕНИЕМ ДВУХ ТАБЛИЦ. Слагаемое считалось
// как `спина лидера × (haulMult ЕЁ строки / haulMult строки лидера)`, то есть
// вклад ОБЪЕКТА масштабировался АНКЕТОЙ сквада: коэффициент 8, выведенный из
// килограммов (120/15), применялся к анкете 154 кг и давал 1232 кг — перекос
// 10.3× против собственной цепочки колонки. Объект несёт СВОИ граммы
// (ЗАКОН ДВУХ ТАБЛИЦ — наверху массовка есть циферки в инвентаре, а не актор).
//
// ── ПОЧЕМУ ЭТО ДВЕРЬ ЧТЕНИЯ, А НЕ ПЕРЕСЧЁТ КОЛОНКИ (вердикт «давай А») ────
// ЗДЕСЬ СТОЯЛА `refresh_squad_carry` — пересчёт, писавший колонку
// `MacroNpcRuntime::carryCap`, и своё слабое место она признавала сама:
// «каждое место, меняющее контейнер, обязано позвать эту дверь». Это РАЗРЯД
// Б — тотал, свежесть которого держится ДИСЦИПЛИНОЙ; четыре звонка стояли,
// пятый забыли бы, а устаревший тотал врёт МОЛЧА. Владелец назвал форму
// 2026-10-20: «задать на уровне архитектуры системы агностично что может
// меняться в анкете … и на каждом из них пересчитывать … хранить тока
// итоговые», и на развилку разрядов ответил «давай А».
//
// РАЗРЯД А ЗДЕСЬ ДОСТИГАЕТСЯ СНОСОМ, А НЕ ЕЩЁ ОДНОЙ ДВЕРЬЮ, и это не выбор
// вкуса — оба слагаемых УЖЕ разряда А:
//   · спина анкеты — колонка `carryPerSoulG`, её пишет дверь листа;
//   · граммы объектов — `sum().haulG`, и его правит САМА запись слота
//     (M-296: у сводки писателя нет вовсе, забыть позвать НЕЛЬЗЯ).
// Сумма двух свежих чисел свежа по построению, поэтому третьей колонке
// хранить нечего. Доказательство избыточности — ЧИСЛОМ: все 10 прод-читателей
// снесённого `carryCap` держали в руках ТОТ ЖЕ инвентарь, функцией которого
// он был, то есть ни один не спросил обоз отдельно от сумки.
//
// Цена: O(1) — чтение хранимой сводки и сложение, ни одного прохода по
// слотам; минус колонка (80 → 76 Б × 16384) и минус четыре звонка-дисциплины.
inline std::int64_t carry_capacity_g(const ecs::MacroNpcRuntime& rt,
                                     const Inventory& bag) noexcept {
    // `int64`, потому что `haulG` таков с M-296 и сужать его было бы
    // молчаливым обрезанием: структурного потолка у голов нет (`ItemRef::
    // count` — `int32`), и `int32` заворачивается на 17 895 головах по 120 кг,
    // тогда как замеренный город давал 11 774 тела — 66 % диапазона.
    return std::int64_t(rt.carryPerSoulG) + bag.sum().haulG;
}
inline std::int64_t carry_capacity_g(const MacroStore& st,
                                     MacroHandle squad) noexcept {
    if (!st.valid(squad)) return 0;
    return carry_capacity_g(st.runtime[squad.slot],
                            st.inventory[squad.slot].inv);
}

// (Резолв по ординалу — `macro_handle_by_spawn_id@src/macro/store.h`: он
//  читает ТОЛЬКО стор и живёт у эмитента, рядом со справочником, который
//  отвечает. Здесь стоял его СКАН всех 16384 слотов — M-271.)
// ── ИМЯ СКВАДА — ОДНА ДВЕРЬ ЧТЕНИЯ КОЛОНКИ (вердикт 3, ход 2) ───────────
// Имя анкеты живёт колонкой store (ecs::SquadName, рождение рендерит дефолт
// из names[...]); всякий показ имени читает ЕЁ, а не перевыводит из индекса
// — перевывод был бы вторым ответом на «как его зовут» (DOD п.6) и
// сломался бы первым же переименованием. Пустая колонка (порченые данные) —
// честный fail-soft в ярлык строки каталога.
inline const char* squad_name(const MacroStore& st, std::uint16_t slot) {
    const char* t = st.name[slot].text;
    if (t[0] != '\0') return t;
    const std::uint16_t raw = st.kind[slot].type;
    const NPCType kind = raw < std::uint16_t(NPCType::Count)
        ? NPCType(std::uint8_t(raw)) : NPCType::Peasant;
    return npc_def(kind).label;
}

// ── УРОВЕНЬ СКВАДА — ОДНА ДВЕРЬ НАД АНКЕТОЙ (ЗАКОН АНКЕТЫ, M-246а) ───────
// Владелец, дословно: «анкета источник». Здесь жила колонка `ecs::NpcLevel`
// РЯДОМ с `sheet.levelData.level` — два ответа на один вопрос о мире, и
// приговор ей закон вынес раньше наряда. Колонка умерла 2026-10-18; всякий
// вопрос «какой он уровня» читает ЭТУ дверь.
//
// И СНОС БЫЛ НЕ КОСМЕТИКОЙ — КОЛОНКА ДЕРЖАЛА ЖИВОЙ БАГ, НЕДОСТУПНЫЙ АНКЕТЕ
// ПО ПОСТРОЕНИЮ. Колонка умела быть НУЛЁМ: `birth_place` её не писал вовсе,
// а рождение слота обнуляет (`store_birth`). Нулевой уровень обнулял и
// кривую — `exp_to_next_level(0)` = 0 (`anketa.h`), — поэтому `while (xp >=
// порог)` срабатывал на ЛЮБОМ положительном xp, и первая же победа
// неподвижного сквада выдавала ему даровой уровень, а вместе с ним перекат
// анкеты из сида (см. шрам у двери роста ниже). В анкете нуля не бывает:
// `LevelData::level = 1` по умолчанию, и `make_character_sheet` клампит
// `level < 1` в 1 — то есть после этой двери нулевой порог НЕВЫРАЗИМ.
inline int squad_level(const MacroStore& st, std::uint16_t slot) {
    return st.sheet[slot].levelData.level;
}

// ── СКОРОСТЬ КАРТИНКИ — ОДНА ДВЕРЬ, И ВЕЛИЧИНА У НЕЁ «РАССТОЯНИЕ ЗА ВРЕМЯ» ─
// Колонка `visualSpeed@src/ecs/components.h` сама это и объявляет
// («travelled distance»), а читает её ОДИН интегратор представления
// (`tick_macro_eyes@src/ui/macro_eyes.cpp`), который ведёт глаз по ЕВКЛИДОВОЙ
// дуге: `d = sqrt(dSq)`, шаг `speed * dt`. Значит писатель обязан называть
// РАССТОЯНИЕ, и дверь не даёт назвать ничего другого.
//
// ШРАМ, КУПИВШИЙ ДВЕРЬ, — ЧИСЛОМ (Б4, замер 2026-10-19). Писателей было два,
// и расходились они не знаменателем, а ЧИСЛИТЕЛЕМ: думка НПЦ писала
// пройденное расстояние, а марш игрока — ТЕМП В КЛЕТКАХ (`cellsPerSec`). На
// прямом шаге это одно число, на диагонали — разные в √2, потому что клетка
// стоит 1.0 бюджета в любом румбе (вердикт владельца 2026-09-10), а дуга у
// диагонали 1.414. Глазу выдавалось 1.0 на 1.414 ушедшей цели, то есть
// дефицит 0.414 клетки за диагональ, и он НЕ закрывался: прямой шаг его
// держал, диагональный наращивал. Замер на 26 клетках сида 12345: дуга
// заявленной точки 28.436 за 25 шагов, предсказанный дефицит 3.436,
// `maxGap` 3.990 — и ОДИНАКОВО при simSpeed 1.0 (3.990) и 4.0 (3.906), то
// есть ускорение времени к механизму отношения не имело вовсе. Оно было
// УСИЛИТЕЛЕМ: тиков на кадр 1.00 → 3.97, те же диагонали проезжают вчетверо
// быстрее, а на маршруте длиннее ~14.5 диагоналей дефицит доходит до
// телепорт-бэкстопа 6 клеток и спрайт срывается.
//
// Период называет ЗВОНЯЩИЙ, потому что он у них честно разный: у думки это
// `kAiPeriodSeconds`, у марша — время одного звена (1/pace). Единица обоих
// одна — секунды МИРОВЫХ часов, те же, что интегратор получает как
// `ticksAdvanced * kStepSeconds`.
inline void set_visual_speed(ecs::MacroNpcRuntime& rt, float dist,
                             float periodSeconds) {
    rt.visualSpeed = (dist > 0.0f && periodSeconds > 0.0f)
        ? dist / periodSeconds : 0.0f;
}

// ── ГЛАГОЛЫ СКВАДА — ОДНА ДВЕРЬ НАД ОДНОЙ КОЛОНКОЙ (CANON S22, M-281) ────
// Канон называет её прямо: «глаголы (Talk/Trade/Attack/Enter) — ОДНА дверь
// `actions_of` для клика и для AI». Дверь переехала сюда из `map_subject.h`,
// умершего целиком, и при переезде перестала ветвиться по РОДУ СУБЪЕКТА.
//
// ЧТО ИМЕННО УМЕРЛО, И ПОЧЕМУ ЭТО БЫЛ ВТОРОЙ ОТВЕТ. Прежняя дверь брала
// `MapSubject` — хранимый enum {None, Squad, Landmark} плюс два адреса — и
// по нему выбирала одно из двух: неподвижному отдавала КОЛОНКУ его строки
// реестра, ходоку — МАСКУ, вписанную литералом в код. То есть на один
// вопрос мира («какие глаголы у этого сквада») в дереве стояло два ответа
// в разных формах (DOD п.6), а хранимый род мог с ними разойтись — ровно
// дефект M-246а, где хранение выводимого и держало живой баг. Литерал
// переехал строками в `kLandmarks` (там же `static_assert` на закон), род
// субъекта не хранится и не выводится, и осталась ОДНА строка кода.
//
// ПОДМЕС `Enter` ОТСЮДА УШЁЛ ВМЕСТЕ С САМИМ ГЛАГОЛОМ (M-290): вход в субмир
// субъекта не спрашивает — он в клетку под ногами, — и ни бит, ни колонка
// `walkable` не имели ни одного прод-читателя.
//
// Fail-closed: протухший или нерождённый хэндл не предлагает НИЧЕГО —
// нулевая маска, нулевой вклад S6, как у всякой макро-двери.
inline std::uint16_t actions_of(const MacroStore& st, MacroHandle h) {
    if (!st.valid(h)) return 0;
    const LandmarkDef& def =
        landmark_def(SquadType(st.runtime[h.slot].squadType));
    return def.actions;
}

// РЕЗОЛВ МЕСТА: тот же резолв субъекта + гейт оси рода. Пространство
// ординалов ОДНО (M-37), и после смерти строки мест «это место?» отвечает
// только ось тела — звонящий, которому нужен именно НЕПОДВИЖНЫЙ сквад,
// обязан спрашивать этой дверью, а не голым резолвом.
inline MacroHandle place_handle_by_ordinal(const MacroStore& st,
                                           std::uint32_t index) {
    const MacroHandle h = macro_handle_by_spawn_id(st, index);
    return st.valid(h)
            && squad_archetype(SquadType(st.runtime[h.slot].squadType)) == SquadArchetype::Place
        ? h : MacroHandle{};
}

// (ВТОРОЙ ДВЕРИ У ЭТОГО ВОПРОСА БОЛЬШЕ НЕТ — M-271. Здесь стояла её «горячая»
//  пара, бравшая порядок драйва отдельным параметром: пока голый резолв был
//  сканом капа, звонящий с порядком на руках обходил его стороной, и на один
//  вопрос мира в дереве стояло ДВА ответа (DOD п.6). Теперь дешёв сам резолв
//  — справочник у эмитента, — и параметр покупать нечего. Вместе с ней ушло
//  последнее, третье написание закона: лямбда `slot_of` дневной ротации и
//  `place_slot_by_id` думки сведены к ОДНОМУ переходнику в `npc_ai.cpp`.)

// ДВЕРЬ ПЕРЕХОДА ВИДА — второе событие места, и оно же его СМЕРТЬ (владелец
// 2026-09-20: «уничтожение ландмарка и рождение будет как механика»). Смерть
// места не освобождает слот, а МЕНЯЕТ ВИД: деревня становится руиной и
// остаётся стоять следом (CANON S9). Дверь объявляет событие (navEpoch), по
// которому поднимается всё запечённое от состава. Вызывателей пока нет:
// механика перехода не построена, дверь названа, чтобы второго способа
// (`set_place_kind` УМЕРЛА 2026-10-18, вердикт владельца «снести»: дверь
//  смены рода неподвижного сквада имела НОЛЬ прод-вызовов, и её шапка
//  это признавала сама. Закон «переход рода поднимает событие состава»
//  остался без пути, а не без носителя: событие поднимает рождение
//  (`birth_place@src/macro/place_birth.h`, ++gs.navEpoch), и свидетель
//  навигации судит его там. Понадобится переход — придёт вместе со
//  своим звонящим, как всякая дверь.)

// ПАВШИЙ СКВАД ГИБНЕТ ЦЕЛИКОМ — ОДНА СУДЬБА, НИ ОДНОЙ ВЕТКИ ПО РОДУ (M-228).
//
// Здесь стоял СЛИВ УЦЕЛЕВШИХ В ПУЛ ДЕЗЕРТИРОВ (owner ruling 3, CANON S4/S13):
// лидер падал, сквад жил БЕЗЛИКИМ до конца боя, и затем его люди «перестают
// быть сквадом» и уезжают в пул, из которого мир поднимал банды. Слив умер по
// трём причинам, и все три названы:
//   1. ЗАКОН ЭТОГО ЗАПРЕЩАЛ С 2026-09-21 (CANON S9 п.6, дословно: «при бое
//      убитый сквад не должен идти в дезертиры он погибает»): у пула ровно
//      один законный источник — неоплата на границе сезона, — а слив был
//      ВТОРЫМ, и жил рядом с запретом полмесяца;
//   2. РАЗГРУЗКИ У ПУЛА НЕТ С ТОГО ЖЕ ДНЯ (`raise_deserter_bands` вырезана,
//      npc_spawn.h), то есть «из которого мир поднимает банды» было ложью
//      шапки: души уезжали в контейнер без оттока и копились там навсегда;
//   3. ВЕРДИКТ ВЛАДЕЛЬЦА 2026-10-06 выбрал гибель прямо: «да давай пока тупо
//      уничтожение мы же потом всегда сможем расширить?»; «просто убрать всё
//      это просто универсально не важно кто умер и умер всё никаких».
//
// ЧТО ДЕЛАЕТ ЭТА ДВЕРЬ. Обходит каждый сквад, чей лидер мёртв, и УНИЧТОЖАЕТ
// его область существ целиком (`creatures_kill_all`), списывая паству дома на
// погибшие ДУШИ и докладывая их же в ведомость склада душ
// (`EconFact::Kind::SoulsKilled`). Сквад с живым лидером не трогается.
// Возвращает погибшие ГОЛОВЫ — любого рода.
//
// ПОЧЕМУ ОТКАЗА БОЛЬШЕ НЕТ: прежний слив мог упереться в кап слотов пула, и
// тогда души ОСТАВАЛИСЬ стоять в мёртвом скваде до следующего тика. У смерти
// приёмника нет, значит нет и капа: область пуста с первого прохода.
//
// ГДЕ ВЕДОМОСТЬ МОЛЧИТ, И ЭТО НАЗВАНО: `sink` есть у четырёх звонящих из
// шести — у всех, кто держит `MacroWorld`. Два пути субмира
// (`SubworldEngine::resolve_subworld_deaths`, `::leave`) своего канала фактов
// не имеют и передают nullptr: наверх субмир отчитывается ДЕЛЬТОЙ, а не
// макро-фактами (ЗАКОН ШВА). Это предел доклада, а не предел закона —
// существа гибнут одинаково на всех шести путях.
// ── ДУША ПОКИНУЛА ПАСТВУ СВОЕГО ДОМА (переворот населения, v122) ──────────
// Паства поселения — worked-ЧИСЛО его фичи, и она считает ВСЕХ своих: и тех,
// кто стоит дома головой в инвентаре, и тех, кто ушёл в поле сквадом. Отсюда
// закон: пока душа жива и числится за домом, worked её держит; как только она
// ВЫШЛА из мира этого дома — умерла в бою, ушла в пул дезертиров, — число
// обязано упасть. Без этой двери паства завышалась бы НАВСЕГДА: место
// кормило бы, растило и облагало данью людей, которых у него нет.
//
// Сюда НЕ входит возвращение домой (dissolve_population_crew): там душа
// переходит из поля в дом, оставаясь той же паствой, и число не меняется.
//
// У ДАНЖА ДВЕРИ НЕТ ПО ПОСТРОЕНИЮ: его паства и есть головы инвентаря
// (souls_flock), а worked под FT_Spire занят спеллом — списывать там значило
// бы гасить чужое число.
// ЗНАК СВОБОДЕН, И ЭТО НЕ ОБОБЩЕНИЕ «НА ВСЯКИЙ СЛУЧАЙ», А ВТОРАЯ ПОЛОВИНА
// ТОГО ЖЕ СОБЫТИЯ (M-292, 2026-10-20). Пока души уходили из места только
// наймом, смертью и дезертирством, у двери был один знак, и имя
// `leave_home_flock` было полной правдой. Прилавок двусторонен по построению
// (владелец: «если понадобится обмен двух прям инвентарей то тоже норм»):
// игрок вправе ОТДАТЬ голову месту, и тогда носитель-число обязан вырасти —
// иначе «в поле» = паства − дома уходит в минус, то есть мир начинает врать
// о том, где его люди. Второй двери на это заводить нельзя (ЗАКОН КОНТЕЙНЕРА
// п.3: два носителя одного события пишутся ОДНОЙ дверью), поэтому дверь
// осталась одна и приняла знак; прежнее имя умерло вместе с односторонностью.
//
// Приход душ, у которого НЕТ второго носителя в контейнере, по-прежнему идёт
// `settle_souls@src/macro/labour.h`: она ставит голову И поднимает число
// разом. Эта дверь — для случая, когда контейнер УЖЕ записан кем-то другим
// (сделка `barter_swap`, найм `hire_npc`, смерть), и числу осталось догнать
// измеренный факт.
inline void shift_home_flock(GameState& gs, const MacroStore& st,
                             std::int32_t homeId, int delta) {
    if (homeId <= 0 || delta == 0) return;
    const MacroHandle home = place_handle_by_ordinal(st, std::uint32_t(homeId));
    if (!st.valid(home)) return;
    const SquadType kind = SquadType(st.runtime[home.slot].squadType);
    if (landmark_def(kind).bornPopBase != 0) return;
    const auto& c = st.cell[home.slot];
    const int x = ecs::cell_x(c, gs.mapW), y = ecs::cell_y(c, gs.mapW);
    worked_write(gs, x, y, std::max(0, worked_read(gs, x, y) + delta));
}

// ── ВТОРАЯ ПОЛОВИНА ПАРЫ, ИЗМЕРЕННАЯ ФАКТОМ ──────────────────────────────
// `shift_home_flock` выше умеет ПИСАТЬ, но не умеет узнать СКОЛЬКО, и
// каждый звонящий отвечал на это сам — четырьмя разными способами, из
// которых верным был один. Эта дверь отвечает за всех, и ответ у неё
// измеренный: ДЕЛЬТА = было минус стало.
//
// ПОЧЕМУ ИМЕННО ИЗМЕРЕНИЕ, А НЕ НАМЕРЕНИЕ. Всякая дверь убыли вправе взять
// МЕНЬШЕ запрошенного — пустой стак, кап приёмника, отказ пула (ходок
// возвращается в состав). Списать с паствы запрошенное значило бы вычесть
// людей, которые остались стоять. Это ровно закон `drain` (ЗАКОН ТРЁХ
// ДВЕРЕЙ) на втором носителе той же души. С M-292 измерение стало работать
// и в ОБРАТНУЮ сторону бесплатно: сделка через прилавок вправе ПРИВЕСТИ
// голову, и «было минус стало» назовёт это отрицательной дельтой без единой
// ветки — поэтому возврат теперь ЗНАКОВЫЙ («ушло», минус — «пришло»).
//
// ПОЧЕМУ СЧЁТ ЧЕЛОВЕЧЕСКИЙ. Паства поселения — число worked, и приход в неё
// идёт людьми (`settle_souls` → `raise_flock_into_container`, строка
// Peasant). Значит и убыль обязана считаться людьми: конь в стойле —
// имущество, и его падёж человеческую паству уменьшать не смеет. До этой
// двери `settle_landmark_day` вычитал из worked ЛЮБУЮ павшую голову, считая
// её душой (латентно: табун у мест сегодня нулевой).
//
// ВЕТКИ ПО РОДУ ЗДЕСЬ НЕТ И НЕ НУЖНО: у данжа паства И ЕСТЬ головы, поэтому
// `shift_home_flock` сам уходит пустым по колонке `bornPopBase`, а
// безымянный дом (homeId = 0) отсекается там же. Оба — ЗНАЧЕНИЯ, а не
// исключения.
inline int flock_left_home(GameState& gs, const MacroStore& st,
                           std::uint16_t slot, int folkBefore) {
    const int lost = folkBefore - count_human_souls(st.inventory[slot].inv);
    shift_home_flock(gs, st, st.runtime[slot].homeSettlementId, -lost);
    return lost;
}

inline int kill_fallen_squad_creatures(MacroStore& st, GameState& gs,
                                      EconFactSink sink = nullptr,
                                      void* user = nullptr) {
    int killed = 0;
    int souls = 0;
    // The player's own squad is never swept: он не лидер, чьи люди уходят,
    // когда он падает, — его родная запись есть БИТ КОЛОНКИ (playerFlag.home,
    // M-255), тот же предикат, каким владение листом узнаёт сквад игрока
    // (sheet_owned_at).
    // Это вопрос «ЧЬЯ ЭТО ЗАПИСЬ», а не «кто умер», поэтому веткой по роду
    // убитого он не является и вердикт «не важно кто умер» его не касается.
    // Существа живут в ЕДИНОМ контейнере сквада (M-71).
    // Порядок обхода — закон (squad_walk.h): «чьи люди легли первыми» не
    // должно зависеть от кишки хранилища. Вектор пуст почти каждый тик
    // (смерть — редкое событие), аллокации нет.
    std::vector<SquadWalkEntry> order;
    collect_squads_by_ordinal(
        st, order,
        [&](std::uint16_t slot) {
            return st.dead[slot] != 0
                && st.playerFlag[slot].home == 0;
        });
    for (const SquadWalkEntry& sw : order) {
        auto& bag = st.inventory[sw.slot];
        if (creatures_empty(bag.inv)) continue;
        // Паства считается ДО смерти: после неё спрашивать уже некого.
        const int homeSouls = count_human_souls(bag.inv);
        killed += creatures_kill_all(bag.inv);
        souls += homeSouls;
        // Душа вышла из мира своего дома — число паствы обязано упасть.
        // Списывается ВСЁ, а не разница «до и после»: отказа у смерти нет,
        // поэтому намерение и факт здесь — одно и то же.
        shift_home_flock(gs, st, st.runtime[sw.slot].homeSettlementId,
                         -homeSouls);
    }
    // ОДИН ДОКЛАД НА ПРОХОД, А НЕ НА СКВАД: ведомость склада душ отвечает на
    // «сколько мир потерял», и дробить это по трупам значило бы заводить
    // отдельную строку на каждую смерть ради того же числа.
    if (souls > 0 && sink) {
        EconFact f{};
        f.kind = EconFact::Kind::SoulsKilled;
        f.amount = souls;
        sink(user, f);
    }
    return killed;
}

// (`dead_rosters_remain` вырезана 2026-09-22: её комментарий утверждал «тик-
// драйверы спрашивают это», а вызовов не было НИ ОДНОГО — ни одного с тех
// пор, как разгрузку пула бандами вырезали 2026-09-21. Дверь выше зовётся
// безусловно, и вопрос «остался ли труп с людьми» миру не задаётся.)

// ── The END of a dead squad's story (CANON S4, canon audit 2026-08-29) ────
// «Убили всех — сквада на карте нет». A tracked death marks the leader
// entity Dead (the subworld reaper, the auto-resolve, the exhaustion bite)
// and the drain above moves his men to the deserter pool — but nothing ever
// DESTROYED the entity: the corpse-row lived in the ECS and rode every save
// (macro_snapshot dead=1) forever. The squad's identity and deeds are already
// the chronicle's, by save-stable ordinal, so the entity itself has nothing
// left to carry.
//
// DEFERRED on purpose — called once per macro tick, at its end (npc_ai.cpp
// tick drivers), never inside a settle: the settle callers still hold the
// entities (the threat step re-reads `self` after settle_auto_battle, the
// pre-battle modal re-checks its foe), and every LONG-lived reference is
// ordinal-based or validity-guarded by construction (MacroOrigin backlinks
// check reg.valid; MacroDebt receipts and possession resolve spawn ordinals
// through scans that simply find nothing; find_creatures fails to a no-op).
//
// The player's squad is never swept: his death is a game-over screen, not a
// disappearance. Returns how many left the map.
//
// ЗДЕСЬ СТОЯЛ АБЗАЦ ПРО «РОСТЕР, КОТОРОМУ ПУЛ ОТКАЗАЛ»: мёртвый сквад стоял,
// пока слив не увёл его людей, потому что пул мог упереться в кап. Слив умер
// 2026-10-06 (M-228): существа ГИБНУТ, приёмника нет, значит и отказа нет —
// область пуста с первого прохода.
//
// ── ГЕЙТ АРЕНДЫ: ДЕВЯТЬ КЛЕТОК ОКНА СВИП НЕ ТРОГАЕТ (M-226) ─────────────
// AGENTS ЗАКОН ШВА: пока игрок внизу, эти клетки ведёт СУБМИР ОДИН, и жать
// их записи наверху значило бы завести второго писателя. Практическая цена
// отсутствия гейта была измерена: со смертью существ (M-228) гейт
// `creatures_empty` перестал держать слот, и запись павшего жилась ровно
// ОДИН макро-тик — подобрать с трупа было нечего.
//
// СРОК ЖИЗНИ ЗАПИСИ ЕСТЬ АРЕНДА, А НЕ КАЛЕНДАРЬ (CANON S10): сезонный
// чистильщик мёртвых анкет НЕ заводится — он был бы вторым законом жизни
// записи (DOD п.6) и сделал бы доступность лута функцией календаря. Конец
// аренды — подъём или ре-центр рамки, оба равноправны — и есть тот момент,
// когда неподобранное сворачивается в казну стоимостью.
//
// ИЗЪЯТИЕ АДРЕСУЕТСЯ КЛЕТКАМИ, НИКОГДА РОДОМ СЦЕНЫ: `cell_is_leased` читает
// ОДНО число мира, а рамку строит `cell_step` по восьми румбам (ЗАКОН
// АДРЕСА). Флага «игрок внизу» у свипа нет и быть не должно.
inline int destroy_dead_macro_squads(MacroStore& st, GameState& gs,
                                     std::int64_t* lootPoolValue = nullptr) {
    // Снос по ординалу (squad_walk.h) — снимок и так был обязателен
    // (destroy под собственным view незаконен), закон порядка достался ему
    // бесплатно. Игрок исключается КОЛОНКАМИ (1е кластер 5): родной сквад —
    // зарезервированным ординалом, носитель флажка — битами GameState;
    // тег-exclude умер вместе с резолвом тегом. Обход — слоты store (6.1);
    // прежний exclude<SubworldTag> был рудиментом (сам тег умер ломтём 6
    // M-150): жильство сцены носили только тела, макро-сквад — никогда.
    const MacroHandle flag = player_flag_handle(gs);
    const bool flagLive = st.valid(flag);
    std::vector<SquadWalkEntry> snapshot;
    collect_squads_by_ordinal(
        st, snapshot,
        [&](std::uint16_t slot) {
            return st.dead[slot] != 0
                && st.playerFlag[slot].home == 0
                && !(flagLive && slot == flag.slot)
                && !cell_is_leased(gs, st.cell[slot].idx);
        });
    int swept = 0;
    for (const SquadWalkEntry& sw : snapshot) {
        const std::uint16_t slot = sw.slot;
        if (!creatures_empty(st.inventory[slot].inv)) continue;
        // THE loot pool (owner 2026-08-30, CANON S5): a squad that died
        // with no victor — exhaustion, drowning — FOLDS its belongings
        // into their catalog worth and pays the world's loot pool (one
        // number; a battle's loser was already emptied by
        // loot_fallen_owner, so what folds here is exactly the victorless
        // remainder). Ruins, dungeons and mob drops are the pool's future
        // contextual outflow: loot is ROLLED from tables with a budget
        // drawn off this value — variety by law, O(1) memory.
        if (lootPoolValue)
            *lootPoolValue += inventory_value(st.inventory[slot].inv);
        // ЛИДЕР — СВОЯ ДУША, И ОН ТОЖЕ БЫЛ ВЗЯТ ИЗ ДОМА (v122: рождение
        // артели списывает `1 + members` голов, npc_ai rotate_worker_squads).
        // Контейнер здесь уже пуст — членов увёл пул, и их дом списал сам, —
        // поэтому остаётся ровно одна душа, и списывается она ровно раз: слот
        // умирает в этой строке и второй раз сюда не придёт.
        if (is_folk_kind(st.kind[slot].type)) {
            shift_home_flock(gs, st, st.runtime[slot].homeSettlementId, -1);
        }
        // 6.3: сквад ЕСТЬ слот store — смерть слота и есть вся смерть.
        store_death(st, handle_at(st, slot));
        ++swept;
    }
    return swept;
}

// ── ПРИКАЗ РУКОЙ — ОДНА ДВЕРЬ ДЛЯ ГРАНИЦЫ (консоль, будущая панель приказов) ─
//
// У ПРИКАЗА ОДИН ДОМ. Маршрут живёт колонкой `orders` store, и читает его мир
// той же дверью `body_state` (`npc_ai.cpp` лестница поведения). Граница НЕ
// СМЕЕТ иметь своего дома для приказа: до 2026-09-26 консоль писала
// entt-компоненту `ecs::SquadOrders`, недостижимую по построению (дверь
// `body_state` отвечала колонкой всякому носителю слота), поэтому команда
// `squad_orders` печатала успех и не меняла мир с флипа 1в — второй ответ на
// один вопрос мира (DOD п.6), проживший сутки ровно потому, что устаревший
// путь не снесли в тот же день (AGENTS §6 «легаси не живёт»).
//
// СНЯТИЕ ПРИКАЗА — ЭТА ЖЕ ДВЕРЬ С ПУСТЫМ МАРШРУТОМ, а не второй вход: наличие
// маршрута И ЕСТЬ приказ (вердикт владельца 2026-09-10, «одна крутилка, не
// две»), значит «отменить» есть запись маршрута нулевой длины. Отказ — вслух у
// звонящего: дверь возвращает false на неизвестный ординал и не трогает мир.
inline bool order_squad_route(MacroStore& st, std::uint32_t ordinal,
                              const ecs::SquadOrders& route) {
    const MacroHandle h = macro_handle_by_spawn_id(st, ordinal);
    if (!st.valid(h)) return false;
    st.orders[h.slot] = route;
    return true;
}

// ── THE SHEET OF A MACRO BODY (owner verdict 2026-09-10, ММОРПГ-модель) ───
//
// A NAMED character (npc.h kNamedKinds) OWNS his sheet: the CharacterSheet
// component born with him in make_npc — «он как игрок»: it levels in place
// and rides the save inside his MacroNpcRecord. A transient crew (rotation
// professions, caravans) derives its GENERIC sheet from its row + level on
// the spot — «они уничтожаются своим ландмарком», there is nothing of
// theirs to store. One door, an honest ontology split — never «игрок/НПЦ».

// Владение листом по колонкам слота — ОДИН предикат на обе двери: именной
// род, анкета стола, РОДНАЯ ЗАПИСЬ ИГРОКА (бит колонки playerFlag.home,
// M-255 — тот же признак, каким её узнают свип, рисовка и резолв загрузки).
inline bool sheet_owned_at(const MacroStore& st, std::uint16_t slot) {
    const auto& kind = st.kind[slot];
    return (kind.type < std::uint16_t(NPCType::Count)
            && npc_named(NPCType(std::uint8_t(kind.type))))
        || st.designTag[slot].ordinal >= 0
        || st.playerFlag[slot].home != 0;
}

// entt-двери листа (owned_sheet/sheet_of/standing_bonuses_of от registry)
// СНЕСЕНЫ (M-150 шаг 0): перепись показала, что на телах сцены никто не
// пишет ни BodyEquipment, ни SpellBook, ни MacroSpawnId — entt-ветки
// отвечали константой, а их include тащил entt всем читателям squad.h.
// Лист тела сцены спрашивается слот-дверью через его макро-запись
// (sub/record.h macro_record_of); тело без записи стоит голым по построению.

// Владеемый лист по хэндлу (1е, каскад слот-нативных дверей): ОДИН предикат
// sheet_owned_at; ординал сквада игрока — колонка, предикат читает её.
inline CharacterSheet* owned_sheet(MacroStore& st, MacroHandle h) {
    return st.valid(h) && sheet_owned_at(st, h.slot)
        ? &st.sheet[h.slot] : nullptr;
}

// Лист по хэндлу — ОДИН ОТВЕТ: КОЛОНКА. Протухший хэндл отвечает дефолтным
// деривативом — читатель обязан был спросить valid() раньше.
//
// ЗДЕСЬ СТОЯЛ ДУАЛИЗМ owned/derived, И ОН БЫЛ ВТОРЫМ ОТВЕТОМ НА «ЧЕЙ ЛИСТ
// ИСТИНА» (DOD п.6, снесён 2026-10-18 вместе с колонкой уровня). Ветка
// спрашивала `sheet_owned_at` и невладеющему скваду ПЕРЕВЫВОДИЛА лист из
// `(строка существа, колонка уровня, сид ординала)` вместо того, чтобы
// прочитать колонку. Умерла она потому, что её предпосылка исчезла ещё
// 2026-09-25: колонка листа есть у ВСЕХ слотов и пишется КАЖДЫМ рождением
// (`npc_spawn.cpp`, `place_birth.h`, `player_entity.cpp`, restore снапшота),
// и `npc_spawn.cpp` об этом прямо говорит — «дуализм owned/derived умер
// здесь». Умер он на одной стороне: писатель перестал деривировать, а
// ЧИТАТЕЛЬ продолжал, ещё три недели.
//
// ЦЕНА ДУАЛИЗМА, НАЗВАННАЯ ЧИСЛОМ. У сквада, рождённого `birth_place`,
// предикат владения ложен по всем трём термам (строка существа нулевая и не
// именная, тег анкеты −1, бит дома 0), поэтому дверь ИГНОРИРОВАЛА его
// анкету и отдавала бросок строки `Peasant`. Город и деревня — ровно два
// непустых листа таблицы `kLandmarkSheets@src/macro/characters.h`, и все их
// 12 заполненных ячеек (Blacksmith/Tailoring/Masonry/Alchemy/Trade + Cha)
// дверь не видела НИ РАЗУ: числа жили только для прямых читателей колонки.
// Веса роли `Peasant` дают этим пяти ремёслам вес 0 (`tables/npc.h`), так
// что бросок не мог их родить даже случайно, а `recipe_known` отказывает
// всем 11 строкам `kRecipes` — то есть через эту дверь мир видел сквад,
// который не умеет НИЧЕГО, при анкете, которая умеет всё.
//
// `sheet_owned_at` ОСТАЁТСЯ, И ЭТО НЕ ОСТАТОК: он отвечает на ДРУГОЙ вопрос
// — «владеет ли сквад своим листом», то есть надо ли писать в него рост.
inline CharacterSheet sheet_of(const MacroStore& st, MacroHandle h) {
    if (!st.valid(h))
        return make_character_sheet(NPCType::Peasant, 1, leader_sheet_seed(0));
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

// ── ФЕОДАЛЬНОЕ РЕБРО — ОДНА ДВЕРЬ НА ОБА КОНЦА (владелец, 2026-09-21;
// переехало из state.h флипом M-90: знание роли живёт в interests ТЕЛА) ───
// CANON S24 требует, чтобы узел знал И сюзерена, И прямых подчинённых.
// Концы ставятся ОДНИМ вызовом и снимаются одним; старого сюзерена дверь
// снимает САМА: у места ровно один сюзерен, и смена его без снятия прежнего
// оставила бы вассала, платящего двоим.
inline int suzerain_of(const MacroStore& st, std::uint16_t slot) {
    const Interests& in = st.interests[slot];
    for (int i = 0; i < kMaxInterests; ++i) {
        const Interest& it = in.slots[i];
        if (it.stance == std::uint8_t(Stance::None)) break;
        if (it.stance == std::uint8_t(Stance::Suzerain)) return it.object;
    }
    return 0;
}

// СРОК И СИЛА — ДЕФОЛТНЫЕ, И ЭТО СТРОЙКА, А НЕ СИРОТА (M-252, 2026-10-12):
// оба сегодня идут нулём у всех прод-звонящих, то есть феод бессрочен и без
// веса. Снести их как «колонки без читателя» пробовали тем же днём и вернули
// — они две из четырёх колонок формы, согласованной с владельцем построчно
// 2026-09-21 (разбор у `Interest@src/macro/interests.h`).
inline void set_suzerain(GameState& gs, MacroStore& st, int vassalId,
                         int suzerainId, int value = 0, int term = 0) {
    const MacroHandle vh = place_handle_by_ordinal(st, std::uint32_t(vassalId));
    if (!st.valid(vh) || vassalId == suzerainId) return;
    Interests& vin = st.interests[vh.slot];
    // Прежний сюзерен теряет этого вассала — с обоих концов; вместе со
    // ЗНАНИЕМ роли умирает и ЛЕТОПИСЬ ДОЛГА (ребро рода 6): непогашенная
    // дань прощается сменой феода, второго носителя долга не существует.
    for (int i = 0; i < kMaxInterests; ++i) {
        Interest& it = vin.slots[i];
        if (it.stance == std::uint8_t(Stance::None)) break;
        if (it.stance != std::uint8_t(Stance::Suzerain)) continue;
        const MacroHandle oh = place_handle_by_ordinal(st,
                                                       std::uint32_t(it.object));
        if (st.valid(oh)) {
            interest_clear(st.interests[oh.slot], vassalId);
            tithe_edge_remove(gs.factions,
                              std::int16_t(st.kind[oh.slot].factionIdx),
                              vassalId);
        }
        interest_clear(vin, it.object);
        break;                     // сюзерен у места ровно один
    }
    if (suzerainId <= 0) return;   // «стал ничьим» — это и есть весь вызов
    const MacroHandle sh = place_handle_by_ordinal(st,
                                                   std::uint32_t(suzerainId));
    if (!st.valid(sh)) return;     // висячего ребра не заводим
    interest_set(vin, suzerainId, Stance::Suzerain, value, term);
    interest_set(st.interests[sh.slot], vassalId, Stance::Vassal,
                 value, term);
    // ОДНА ДВЕРЬ ПИШЕТ ОБА НОСИТЕЛЯ: знание роли — в интересы (род 2),
    // летопись долга — ребром в строку фракции СЮЗЕРЕНА (род 6).
    tithe_edge_add(gs.factions,
                   std::int16_t(st.kind[sh.slot].factionIdx), vassalId,
                   suzerainId);
}

// ФЕОДАЛЬНОЕ РЕБРО ЭТОГО ВАССАЛА (род 6, v121): долг живёт в строке фракции
// СЮЗЕРЕНА — путь к нему идёт через знание роли (suzerain_of, род 2), сами
// носители врозь и отвечают на разные вопросы.
inline TitheEdge* tithe_edge_of(GameState& gs, const MacroStore& st,
                                std::uint16_t vassalSlot) {
    const MacroHandle sh = place_handle_by_ordinal(
        st, std::uint32_t(suzerain_of(st, vassalSlot)));
    return st.valid(sh)
        ? tithe_edge(gs.factions,
                     std::int16_t(st.kind[sh.slot].factionIdx),
                     int(st.spawnId[vassalSlot].index))
        : nullptr;
}
inline const TitheEdge* tithe_edge_of(const GameState& gs,
                                      const MacroStore& st,
                                      std::uint16_t vassalSlot) {
    return tithe_edge_of(const_cast<GameState&>(gs), st, vassalSlot);
}

// ДОЛЖЕН ЛИ ЭТОТ ВАССАЛ ХОТЬ ЧТО-НИБУДЬ. Долг ребра — он же ведомость
// «с кого собрано»: собранный вассал отвечает «нет» по построению, и второго
// признака («посещён в этом сезоне») в мире не заводится (S26).
inline bool owes_tithe(const GameState& gs, const MacroStore& st,
                       std::uint16_t vassalSlot) {
    const TitheEdge* e = tithe_edge_of(gs, st, vassalSlot);
    return e && e->owedValue > 0;
}

// ── STANDING, FOR ANY MACRO PARTICIPANT (CANON S20.1) ─────────────────────
//
// Owner's ruling, 2026-08-27: renown is not a squad's private counter — every
// MACRO entity with an identity carries one. A band, a city, a people. Where
// it lives, by participant kind: a squad's rides its runtime component, a
// landmark's answers through the one landmark door (landmark_renown_slot,
// state.h). A CELL has none and never will — the land has no standing — and
// a FACTION's is the politics track's to add: one field, read through this
// same door. `renown_of` is what makes a deed contextual — killing a legend
// is worth a share of the legend — and `grant_renown` is what makes fame
// spread.
// `GameState& gs` снят 2026-10-09: оба рода субъекта резолвятся ЧЕРЕЗ СТОР
// (`macro_handle_by_spawn_id`), слава места — колонка `runtime.renown` его
// ТЕЛА, и строки места тут не было уже давно.
inline std::uint32_t* renown_slot(MacroStore& st,
                                  std::uint8_t participantKind,
                                  std::uint32_t ordinal) {
    switch (fact_subject_kind(participantKind)) {
        case std::uint8_t(FactSubject::Squad): {
            const MacroHandle h = macro_handle_by_spawn_id(st, ordinal);
            return st.valid(h) ? &st.runtime[h.slot].renown : nullptr;
        }
        case std::uint8_t(FactSubject::Landmark): {
            // Слава места — колонка runtime.renown его ТЕЛА; строки нет,
            // резолв тот же, что у сквада (одно пространство ординалов,
            // M-37), род субъекта остаётся словом ЛЕТОПИСИ.
            const MacroHandle h = macro_handle_by_spawn_id(st, ordinal);
            return st.valid(h) ? &st.runtime[h.slot].renown : nullptr;
        }
        default:
            return nullptr;
    }
}

inline std::uint32_t renown_of(MacroStore& st,
                               std::uint8_t participantKind,
                               std::uint32_t ordinal) {
    const std::uint32_t* slot = renown_slot(st, participantKind, ordinal);
    return slot ? *slot : 0u;
}

// Saturating, though the ceiling is a formality: the greatest single deed
// against a nobody is worth twenty, so reaching it takes two hundred million
// of them. What the clamp really buys is that ADDITION can never be the thing
// that wraps a legend into a nobody.
inline void grant_renown(MacroStore& st,
                         std::uint8_t participantKind, std::uint32_t ordinal,
                         std::uint32_t gain) {
    if (gain == 0u) return;
    std::uint32_t* slot = renown_slot(st, participantKind, ordinal);
    if (!slot) return;
    const std::uint64_t sum = std::uint64_t(*slot) + gain;
    *slot = std::uint32_t(std::min<std::uint64_t>(sum, 0xFFFFFFFFull));
}

// ── THE door into the world's memory: file the fact AND pay the doer ──────
//
// ONE door because the two halves must never come apart (S20.1). A writer
// that filed a deed and forgot the renown would be a world where nobody ever
// becomes somebody; a writer that paid renown without filing would be a
// legend nobody can read. Here neither is expressible: you hand over the
// sentence and the doer, and both happen. It lives on the MACRO layer beside
// its landmark twin (record_landmark_fact, state.h) so the app, the UI and
// the subworld engine all knock on the same door — the one-door law had
// drifted across two layers, and the drift was writers that filed for free.
//
// `subject` is the ENTITY that did it, when the caller holds one: the door
// resolves it to its save-stable ordinal (MacroSpawnId). Figure-ness is
// DERIVED from renown for EVERYONE, and the bit is the participant's standing
// AT THE MOMENT of the deed — marked from PRE-deed renown («с этого дня её
// дела идут в анналы»), subject and object alike. No writer hand-marks it: a
// hand-written `true` was how the player's deals filed him as a figure he had
// not yet become.
// Общий хвост обеих дверей записи дела: figure-ность из славы, летопись,
// плата славой — субъект к этому моменту уже разрешён в ординал.
inline std::uint32_t record_deed_filed(MacroStore& st, GameState& gs,
                                       WorldFact fact) {
    fact.subjectKind = fact_subject(
        FactSubject(fact_subject_kind(fact.subjectKind)),
        renown_is_named(renown_of(st, fact.subjectKind, fact.subject)));
    if (fact_subject_kind(fact.objectKind)
        != std::uint8_t(FactSubject::None)) {
        fact.objectKind = fact_subject(
            FactSubject(fact_subject_kind(fact.objectKind)),
            renown_is_named(renown_of(st, fact.objectKind, fact.object)));
    }
    const std::uint32_t seq = chronicle_record(gs.chronicle, fact);
    if (seq != 0u) {
        // WHAT THE DEED WAS WORTH, asked of the world: the base its row gives
        // for a deed against nobody, plus a share of whatever the OBJECT was
        // worth. That is the whole of "fame is made of fame" — no second rule
        // for famous victims, because their fame is already a number the
        // world keeps about them.
        const std::uint32_t gain = renown_for_deed(
            FactKind(fact.kind),
            renown_of(st, fact.objectKind, fact.object));
        grant_renown(st, fact.subjectKind, fact.subject, gain);
    }
    return seq;
}

// Субъект хэндлом (1г) — ординал из колонки, entt не участвует.
inline std::uint32_t record_deed(MacroStore& st, GameState& gs,
                                 WorldFact fact, MacroHandle subject) {
    if (st.valid(subject) && st.spawnId[subject.slot].index != 0u) {
        fact.subjectKind = std::uint8_t(FactSubject::Squad);
        fact.subject = st.spawnId[subject.slot].index;
    }
    return record_deed_filed(st, gs, fact);
}

// Близнец для МЕСТА (переехал из state.h флипом M-90: слава места — колонка
// его ТЕЛА). Та же одна дверь S20.1: figure-ность из ДО-дельной славы, файл,
// плата — всё внутри record_deed_filed, вторая копия закона умерла с
// переездом.
inline std::uint32_t record_landmark_fact(MacroStore& st, GameState& gs,
                                          FactKind kind, int landmarkId,
                                          int x, int y, int amount,
                                          int objectLandmarkId = 0) {
    WorldFact f{};
    f.day = gs.worldTime.day();
    f.kind = std::uint16_t(kind);
    f.subjectKind = std::uint8_t(FactSubject::Landmark);
    f.subject = std::uint32_t(landmarkId < 0 ? 0 : landmarkId);
    if (objectLandmarkId > 0) {
        f.objectKind = std::uint8_t(FactSubject::Landmark);
        f.object = std::uint32_t(objectLandmarkId);
    }
    f.x = std::int16_t(x);
    f.y = std::int16_t(y);
    f.amount = amount;
    return record_deed_filed(st, gs, f);
}


// ── Auto-battle glue: entity ⇄ the pure resolver ──────────────────────────

// Assemble one resolver side from a live macro squad entity. Everything the
// side needs is read from the entity — the same components every other
// consumer reads — so the resolver and the subworld can never disagree about
// what a squad IS. The leader's sheet seed is derived from his save-stable
// ordinal: the macro layer never stored his birth sheet seed, and both body
// births already re-derive sheets from their own context seeds, so the
// fraction-based wound law is what keeps the layers agreeing (sub/spawn.h).
// A leader whose sheet is AUTHORED rather than rolled — a named character
// with an OWNED component (посадка А/Б) — fights with his own build: the
// component is read right here, so the player and every named lord get it
// through one line and no caller can forget to pass it. A transient leader
// has no component and the row is derived from (type, level, seed) inside
// the resolver, as a generic leader's always was.
//
// (The `storedSheet` parameter this replaced is what killed the twin: the
// app used to assemble the player's side by hand in
// `player_auto_battle_side` — twenty lines restating health-as-a-fraction,
// fatigue-as-sp-over-max and creatures lookup beside the twenty here saying
// the same about everyone else. Посадка Б retired the parameter itself:
// the sheet lives ON the entity now, so the door reads it like every other
// component above.)
inline AutoBattleSide auto_battle_side_of(const MacroStore& st, MacroHandle h) {
    AutoBattleSide s{};
    if (!st.valid(h)) return s;
    const std::uint16_t slot = h.slot;
    if (st.kind[slot].type < std::uint16_t(NPCType::Count)) {
        s.leaderType = NPCType(std::uint8_t(st.kind[slot].type));
    }
    {
        const ecs::Pools& hp = st.pools[slot];
        s.leaderHealthFraction = hp.maxHp > 0
            ? std::clamp(float(hp.hp) / float(hp.maxHp), 0.0f, 1.0f) : 1.0f;
        // sp may be a NEGATIVE debt (exhaustion); the 0.1 floor already
        // says "a squad never fights at literal zero". Same block as the
        // wound now — one read, one component.
        s.fatigue = std::clamp(
            float(hp.sp) / float(std::max<int>(1, hp.maxSp)), 0.1f, 1.0f);
    }
    s.creatures = &st.inventory[slot].inv;   // область существ контейнера (M-71)
    // ПОТОЛОК — ОТ АНКЕТЫ, У КАЖДОГО СКВАДА (M-282, 2026-10-18). Аурой этот
// блок тоже ведал, пока она существовала: `s.bonuses` снесён 2026-10-18
// вместе с `squad_bonuses` — та была константой-нулём.
    // Здесь стоял гейт `if (sheet_owned_at(st, slot))`, и невладеющему листом
    // скваду ОБА терма не ставились ВОВСЕ: потолок HP лидера оставался
    // броском его строки, а аура — нулём. ЗАКОН АНКЕТЫ п.5 запрещает это
    // прямым текстом — «пустой лист есть ЗАКОННОЕ значение… отдельной ветки
    // „а это место“ для них не нужно: пустота — значение, а не исключение», —
    // и п.7 называет ровно эту форму признаком нарушения.
    //
    // ПРЕДПОСЫЛКА ГЕЙТА ИСЧЕЗЛА ДВАЖДЫ. (1) Колонка листа есть у ВСЕХ слотов
    // с 2026-09-25, и каждое рождение её пишет. (2) С M-246а `sheet_of`
    // ВСЕГДА отдаёт колонку — дуализм owned/derived умер, — так что
    // «владение» отвечает теперь ровно на один вопрос: писать ли в лист рост.
    // К тому, КАКИМ листом сквад ДЕРЁТСЯ, оно отношения не имеет: тот же
    // лист, которым его судит очный бой, обязан судить и авторезолв, иначе
    // два исполнения одного закона боя расходятся (CANON S13).
    //
    // `leaderDpsOverride` по-прежнему НЕ ставится: удар оценивает ближняя
    // идентичность субмира (sub/engine.h), а макро — L1 и наверх не тянется.
    // Это говорит звонящий, который знает оба мира.
    //
    // ЛИСТ ЕДЕТ ЦЕЛИКОМ, А НЕ ОДНИМ ПОТОЛКОМ (M-300, 2026-10-21). Здесь
    // стояло `s.leaderHpOverride = body_max_hp(eff, …)` — ОДНО число из
    // листа, — и оно НЕ ЧИТАЛОСЬ НИКОМУ: гейт `squad_power` требовал ОБА
    // оверрайда, а второй (`leaderDpsOverride`) ставит ровно один сайт в
    // игре — игрок. Замер headless-мира назвал дыру числом: 1 088 038
    // постановок за игровой день, попаданий в ветку — 0, то есть
    // эффективный лист считался миллион раз в день и выбрасывался, а
    // авторезолв судил лидера ПЕРЕКАТАННЫМ генерик-листом. Это и была
    // недовезённая половина M-282: его свидетель утверждал, что поле
    // ПОСТАВЛЕНО, и ни одна строка не утверждала, что его кто-то ЧИТАЕТ
    // (§8 п.6). Теперь едет сама анкета, и `leader_power` читает её всегда —
    // «тот же лист, которым его судит очный бой, обязан судить и
    // авторезолв» (CANON S13) стало правдой, а не обещанием.
    //
    // Вместе с потолком умерли `leaderLevel` и `leaderSeed` выше: они
    // существовали РОВНО затем, чтобы резолвер катал лист заново.
    s.leaderSheet = effective_sheet_of(st, h);
    return s;
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
//
// ЗДЕСЬ БЫЛО ДВА ЗАКОНА РОСТА, И ВТОРОЙ СТИРАЛ АНКЕТЫ (слиты 2026-10-18,
// вердикт владельца «одна дверь роста для всех»). Невладеющий лист шёл в
// `award_leader_xp`, которая копила опыт в СВОЕЙ колонке `runtime.xp`, вела
// СВОЙ счёт уровней по колонке `ecs::NpcLevel` и на каждом уровне
// ПЕРЕКАТЫВАЛА анкету из сида — `st.sheet[slot] = make_character_sheet(…)`.
// Для сквада, чья анкета пришла не броском, а ТАБЛИЦЕЙ, это было стирание:
// город и деревня теряли все свои ремёсла и торговлю НАВСЕГДА (веса роли
// `Peasant` дают этим пяти ранга́м вес 0, то есть обратно они не отрастают
// по построению), и мир переставал производить. Достижимо это было одной
// победой: индекс сквадов фильтрует только `dead`, поэтому неподвижный
// сквад — законный противник автобоя и законный победитель.
//
// ЗАКОН АНКЕТЫ п.5 и назвал верную форму заранее: «ПУСТОЙ ЛИСТ ЕСТЬ
// ЗАКОННОЕ ЗНАЧЕНИЕ… Отдельной ветки „а это место“ для них не нужно:
// пустота — значение, а не исключение». Одна дверь, ветки по владению нет.
//
// ЧТО УМЕРЛО ВМЕСТЕ С ВТОРЫМ ЗАКОНОМ: колонка `ecs::NpcLevel` (уровень —
// `levelData.level`, M-246а) и колонка `runtime.xp` (опыт — `levelData.exp`,
// и он уже ехал в сейв внутри листа; её единственные писатель и читатель
// жили ВНУТРИ снесённой функции, то есть она была сиротой по DOD п.9).
//
// БАЛАНСНОЕ СЛЕДСТВИЕ, НАЗВАННОЕ ВСЛУХ (ЗАКОН ПРИОРИТЕТА СТРУКТУРЫ): у
// транзиентных сквадов атрибуты и ранги больше не добираются сами на
// уровне-апе — очки КОПЯТСЯ нетраченными, как у игрока и именных. До
// контента трат такой сквад слабее прежнего; это калибровка, и двигать её
// надо числами, а не возвратом второго закона.
inline int award_kill_xp(MacroStore& st, MacroHandle h, int xp) {
    if (xp <= 0 || !st.valid(h)) return 0;
    const std::uint16_t slot = h.slot;
    CharacterSheet& own = st.sheet[slot];
    const CharacterSheet eff = effective_sheet_of(st, h);
    const int gained = award_exp(
        own.levelData, xp,
        calculate_derived(eff.attributes, eff.skills).expMultPct);
    if (gained > 0) {
        // Потолки и доли следуют за листом через ТУ ЖЕ одну дверь
        // пересборки. Строка существа — только для боевого листа тела;
        // нулевая/неизвестная отвечает `Peasant`, как и всюду.
        const NPCType type =
            st.kind[slot].type < std::uint16_t(NPCType::Count)
                ? NPCType(std::uint8_t(st.kind[slot].type)) : NPCType::Peasant;
        refresh_body_from_sheet(st.pools[slot], &st.runtime[slot],
                                effective_sheet_of(st, h), type);
    }
    return gained;
}

// ── The settling halves — one set of doors for EVERY consumer ──────────────
// An auto-battle's outcome lands in the world through exactly the pieces
// below, whether the caller is the AI threat step (two macro entities) or
// the player's own auto-resolve button (Inc 6). No consumer edits a creatures
// vector or a health bar directly.

// ── The fallen SPEAK (damage-door track Inc 6) ────────────────────────────
// An auto-resolved fight used to be MUTE: no facts, so quest kill-tallies
// never counted it; no kill reputation, so a massacre by auto-resolve cost
// nothing; and the spoils were whatever bag the loser happened to carry —
// the loot registry was never rolled. Below ground the same death paid all
// three. CANON S13 says there is ONE law of battle at both scales, and
// «система обязана объявить, какие факты она эмитит» (work_vector §1); these
// three helpers are that law, spelled once and shared by the AI↔AI settle and
// the player's own auto-resolve.

// Report one death through the envelope's channel (null = nobody listening).
inline void report_death(const MacroWorld& mw, std::uint16_t npcType,
                         MacroHandle victim, MacroHandle killer,
                         std::int32_t detail, int level,
                         const char* factionId) {
    if (!mw.facts) return;
    BattleFact f{};
    f.kind = BattleFact::Kind::Death;
    f.npcType = npcType;
    // Конверт несёт ХЭНДЛ {слот, ID} напрямую (M-220: упаковка в биты
    // умерла); «никого» — дефолтный MacroHandle{}, id == 0. Согласие
    // дефолтов конверта и хэндла — ассерт ниже.
    f.victim = victim;
    f.killer = killer;
    f.detail = detail;
    f.level = level;
    f.factionId = factionId ? factionId : "";
    mw.facts(mw.factsUser, f);
}
// Дефолт конверта обязан называть «никого» законом нуля-ординала.
static_assert(BattleFact{}.victim == MacroHandle{}
                  && BattleFact{}.victim.id == 0
                  && BattleFact{}.killer == MacroHandle{},
              "сентинель BattleFact = пустой MacroHandle (id 0 — «никто»)");

// The faction a macro body wears — its INSTANCE colours (Inc 2), not its row.
inline const char* squad_faction_id(const MacroStore& st, MacroHandle h) {
    return st.valid(h) ? faction_id_for_index(st.kind[h.slot].factionIdx) : "";
}

// СПОЙЛОВ ИЗ ВОЗДУХА БОЛЬШЕ НЕТ (M-139, вердикт владельца 2026-09-26).
// Здесь стояли переходник RNG для реестра лута и `roll_fallen_spoils` —
// «что павший этой РОЛИ был бы должен нести»: бросок хардкод-профиля роли
// плюс печать кошелька. Обе таблицы снесены, и с ними эта дверь. Победителю
// достаётся РОВНО то, что павший нёс: сумку лидера переносит
// `loot_fallen_owner` ниже, а контейнерные записи — это записи, и нести им
// нечего до ПУЛА ЛУТА, который будет раздавать добычу по стоимости и
// контексту (дыра названа в M-139).

// Every death this side suffered, told once: the creature rows by their fallen
// records and the leader by his entity. The casualty coin carries its own
// kind and level (CANON S4) — no creatures scan; the resolver drew these FROM
// the creatures, and a generic record has no id a scan could match anyway.
inline void report_battle_deaths(const MacroWorld& mw, const MacroStore& st,
                                 MacroHandle side,
                                 const std::vector<SoldierRecord>& casualties,
                                 bool leaderFell, MacroHandle killer) {
    if (!mw.facts) return;
    const char* factionId = squad_faction_id(st, side);
    for (const SoldierRecord& r : casualties) {
        if (!valid_npc_kind(r.kind)) continue;
        report_death(mw, r.kind, MacroHandle{}, killer,
                     std::int32_t(r.entityId),
                     normalize_soldier_level(r.level), factionId);
    }
    if (leaderFell) {
        report_death(mw,
                     st.valid(side) ? st.kind[side.slot].type
                                    : std::uint16_t(0),
                     side, killer, -1,
                     normalize_soldier_level(
                         st.valid(side) ? squad_level(st, side.slot) : 1),
                     factionId);
    }
}

// Creatures deaths through the ledger row: a storied soul by its entityId, a
// generic one by {kind, level} (the key's detailKind/detailLevel pair).
inline void settle_squad_casualties(GameState& gs,
                                    MacroStore& st, MacroHandle h,
                                    const std::vector<SoldierRecord>& ids) {
    if (!st.valid(h)) return;
    const ecs::MacroCell cell = st.cell[h.slot];
    MacroWorld mw{.gs = &gs, .store = &st};
    // named, not positional — the envelope grows, positions rot; store
    // ОБЯЗАН ехать в каждом локальном конверте (шрам с.18: без него
    // find_creatures отказывал в no-op и потери авто-боя молча не списывались)
    MacroStockKey key{};
    key.subject = std::int32_t(st.spawnId[h.slot].index);
    key.cellX = std::int16_t(ecs::cell_x(cell, gs.mapW));
    key.cellY = std::int16_t(ecs::cell_y(cell, gs.mapW));
    for (const SoldierRecord& r : ids) {
        key.detail = r.entityId != 0 ? std::int32_t(r.entityId) : -1;
        key.detailKind = r.kind;
        key.detailLevel = r.level;
        macro_stock_apply(mw, MacroStock::Creatures, key, -1);
    }
}

// A leader's post-battle fraction lands in the macro Health; zero is the
// tracked-death shape (hp=0 + Dead), the same mark the subworld reaper
// leaves — so an auto-battle death and a fought death are indistinguishable
// to everything upstream.
inline void settle_leader_fraction(MacroStore& st, MacroHandle h,
                                   float fraction) {
    if (!st.valid(h)) return;
    ecs::Pools& hp = st.pools[h.slot];
    if (fraction <= 0.0f) {
        hp.hp = 0;
        macro_mark_dead(st, h);
        return;
    }
    hp.hp = std::clamp(int(float(hp.maxHp) * fraction), 1, hp.maxHp);
}

// What the fallen of `loser` are worth, through the ONE reward law. Read
// BEFORE the deaths settle — the reward needs the rows, settling removes
// them. Includes the leader's own worth when the outcome killed him.
inline int xp_for_fallen(const MacroStore& st, MacroHandle loser,
                         const std::vector<SoldierRecord>& casualties,
                         bool leaderFell) {
    int xp = 0;
    for (const SoldierRecord& r : casualties) {
        if (!valid_npc_kind(r.kind)) continue;
        xp += npc_xp_reward(NPCType(r.kind),
                            normalize_soldier_level(r.level));
    }
    if (leaderFell && st.valid(loser)
        && st.kind[loser.slot].type < std::uint16_t(NPCType::Count)) {
        xp += npc_xp_reward(
            NPCType(std::uint8_t(st.kind[loser.slot].type)),
            normalize_soldier_level(squad_level(st, loser.slot)));
    }
    return xp;
}

// A fallen owner's bag, stack by stack, into any Inventory — the victor's
// macro bag or the player's own.
inline void loot_fallen_owner(MacroStore& st, MacroHandle fallen,
                              Inventory& into) {
    if (!st.valid(fallen)) return;
    Inventory& from = st.inventory[fallen.slot].inv;
    for (std::size_t i = 0; i < from.slots.size(); ++i) {
        const ItemRef stack = from.slots[i];
        if (stack.empty()) continue;
        // ЛУТ — ТОЛЬКО ПРЕДМЕТНАЯ ОБЛАСТЬ (M-71): выжившие люди павшего —
        // не добыча: живое добычей не бывает, и павшие ГИБНУТ вместе со
        // своим лидером (kill_fallen_squad_creatures ниже, M-228).
        if (!world_row_is_item(stack.def)) continue;
        // Credit first: a stack the victor's bag refuses (full) STAYS on the
        // fallen — a refused pickup leaves the corpse holding it (items.h's
        // conservation ruling), never a cleared slot over an evaporated good.
        // Снятие идёт ДВЕРЬЮ (M-296): здесь стояло `stack = ItemRef{}` —
        // единственная в проде запись в чужой слот, менявшая СОСТАВ, и
        // ровно такой строкой сводка разъехалась бы молча.
        if (into.add_ref(stack)) from.remove_at(int(i), stack.count);
    }
}

// ── СМЕРТЬ ГОВОРИТ В ЛЕТОПИСЬ (CANON S20.1, владелец 2026-09-02) ──────────
// The verdict door is the ONLY place the killer is still known, so the
// world's memory is written here and nowhere else — ONE door for EVERY
// auto-resolve, the AI↔AI settle and the player's own button alike (owner
// 2026-09-02, хвост 2б: «игрок ничем не особенен» — his massacres feed the
// same hatred, danger price and threat field as any lord's). Two records,
// each true from its author's side:
//   · Killed — the winner over the fallen loser. Renown flows by the one
//     deed law (record_deed), and «who kills near this village» becomes
//     a chronicle_near_kind question with no new storage.
//   · Died — each bereaved HOME landmark's own loss, object = the killer
//     (a NAMED band by its ordinal, a nameless one by its faction —
//     owner's ruling): the record hatred, revenge quests and the danger
//     term of the refusal price all READ instead of keeping counters.
inline std::int32_t battle_dead(const std::vector<SoldierRecord>& casualties,
                                float leaderFraction) {
    return std::int32_t(casualties.size())
           + (leaderFraction <= 0.0f ? 1 : 0);
}

inline void record_battle_facts(MacroStore& st, GameState& gs,
                                MacroHandle winner, MacroHandle loser,
                                std::int32_t loserDead,
                                std::int32_t winnerDead) {
    const std::int16_t bx = std::int16_t(
        st.valid(winner) ? ecs::cell_x(st.cell[winner.slot], gs.mapW) : 0);
    const std::int16_t by = std::int16_t(
        st.valid(winner) ? ecs::cell_y(st.cell[winner.slot], gs.mapW) : 0);
    if (loserDead > 0) {
        WorldFact f{};
        f.day = gs.worldTime.day();
        f.kind = std::uint16_t(FactKind::Killed);
        f.objectKind = std::uint8_t(FactSubject::Squad);
        f.object = st.valid(loser) ? st.spawnId[loser.slot].index : 0u;
        f.x = bx;
        f.y = by;
        f.amount = loserDead;
        record_deed(st, gs, f, winner);
    }
    const auto bereave = [&](MacroHandle side, MacroHandle foe,
                             std::int32_t dead) {
        if (dead <= 0 || !st.valid(side)) return;
        const ecs::MacroNpcRuntime& srt = st.runtime[side.slot];
        if (!st.valid(place_handle_by_ordinal(
                st, std::uint32_t(srt.homeSettlementId))))
            return;   // the homeless bereave nobody — Killed already spoke
        WorldFact f{};
        f.day = gs.worldTime.day();
        f.kind = std::uint16_t(FactKind::Died);
        f.subjectKind = std::uint8_t(FactSubject::Landmark);
        f.subject = std::uint32_t(srt.homeSettlementId);
        const std::uint32_t foeOrd =
            st.valid(foe) ? st.spawnId[foe.slot].index : 0u;
        if (foeOrd != 0u
            && renown_is_named(renown_of(
                   st, std::uint8_t(FactSubject::Squad), foeOrd))) {
            f.objectKind = std::uint8_t(FactSubject::Squad);
            f.object = foeOrd;
        } else {
            f.objectKind = std::uint8_t(FactSubject::Faction);
            f.object = st.valid(foe) ? st.kind[foe.slot].factionIdx : 0u;
        }
        f.x = bx;
        f.y = by;
        f.amount = dead;
        record_deed_filed(st, gs, f);
    };
    bereave(loser, winner, loserDead);
    bereave(winner, loser, winnerDead);
}

// Settle a resolved AI↔AI auto-battle — the composition of the halves
// above: deaths by name, leader fractions, spoils to the victor when the
// owner fell (a caravan raid PAYS), survivors of a dead leader into the
// deserter pool (the auto-battle IS the whole fight, so its end is here),
// and the winner's leader paid XP through the one reward law.
inline void settle_auto_battle(const MacroWorld& mw,
                               MacroHandle ha, MacroHandle hb,
                               const AutoBattleOutcome& o) {
    GameState& gs = *mw.gs;
    MacroStore& st = *mw.store;
    const MacroHandle winner = o.winner == 0 ? ha : hb;
    const MacroHandle loser  = o.winner == 0 ? hb : ha;
    const auto& loserCasualties = o.winner == 0 ? o.casualtiesB
                                                : o.casualtiesA;
    const float loserFraction = o.winner == 0 ? o.leaderFractionB
                                              : o.leaderFractionA;

    int xp = xp_for_fallen(st, loser, loserCasualties, loserFraction <= 0.0f);
    report_battle_deaths(mw, st, ha, o.casualtiesA,
                         o.leaderFractionA <= 0.0f, hb);
    report_battle_deaths(mw, st, hb, o.casualtiesB,
                         o.leaderFractionB <= 0.0f, ha);

    settle_squad_casualties(gs, st, ha, o.casualtiesA);
    settle_squad_casualties(gs, st, hb, o.casualtiesB);
    settle_leader_fraction(st, ha, o.leaderFractionA);
    settle_leader_fraction(st, hb, o.leaderFractionB);

    if (macro_dead(st, loser) && st.valid(winner)) {
        loot_fallen_owner(st, loser, st.inventory[winner.slot].inv);
    }

    kill_fallen_squad_creatures(st, gs, mw.econFacts, mw.econFactsUser);
    // ОДНА дверь оплаты (корень 5): именованный победитель растёт как
    // игрок (лист владеем, WIS-дивиденд, очки копятся), транзиент —
    // прежний бросок.
    award_kill_xp(st, winner, xp);

    record_battle_facts(st, gs, winner, loser,
                        battle_dead(loserCasualties, loserFraction),
                        battle_dead(o.winner == 0 ? o.casualtiesA
                                                  : o.casualtiesB,
                                    o.winner == 0 ? o.leaderFractionA
                                                  : o.leaderFractionB));
}

// Settle the PLAYER's auto-resolve against a macro squad (Inc 6 — the M&B
// button). The player is the same shape as any leader — and «the player» is
// the FLAG record (A3, 2026-09-17): the man he is on the map, his own squad
// as himself, the worn lord's while he wears one. Until then the sheet
// priced the fight by the flag while creatures, wound and facts settled into
// the ordinal original — the §45 «два ответа» shape, fought and paid by two
// different men. The enemy half goes through exactly the halves above; the
// player half lands where the flag record's truth lives — army rows removed
// by name, the wound fraction into its ordinary Pools, XP through
// award_exp with the wis dividend. By the resolver's own law his head is
// never diced: he reaches 0 only when his whole army died with him — and a
// zero hp in the store is the same game-over the fought version ends in.
// Returns the XP awarded.
inline int settle_player_auto_battle(const MacroWorld& mw,
                                     MacroHandle enemy,
                                     const AutoBattleOutcome& o,
                                     bool playerIsA) {
    GameState& gs = *mw.gs;
    MacroStore& st = *mw.store;
    const auto& playerCas = playerIsA ? o.casualtiesA : o.casualtiesB;
    const auto& enemyCas  = playerIsA ? o.casualtiesB : o.casualtiesA;
    const float playerFraction =
        playerIsA ? o.leaderFractionA : o.leaderFractionB;
    const float enemyFraction =
        playerIsA ? o.leaderFractionB : o.leaderFractionA;
    const bool playerWon = (o.winner == 0) == playerIsA;
    // «Игрок» — запись ФЛАЖКА (A3): его хэндл — биты GameState (1е кл.5).
    const MacroHandle playerH = player_flag_handle(gs);
    // The player's spoils land in HIS bag — the ordinary NpcInventory on his
    // squad record (macro/player_entity.h), the same container an enemy
    // lord's goods came out of.
    Inventory* playerBag = player_inventory(gs, st);
    Inventory scratch{};
    if (!playerBag) playerBag = &scratch;   // headless fixture: nowhere to put

    int xp = playerWon
        ? xp_for_fallen(st, enemy, enemyCas, enemyFraction <= 0.0f)
        : 0;

    // The player's fallen leave his creatures by the SAME door every squad's do
    // — his squad is an ordinary squad record now, so this is
    // settle_squad_casualties over his own record, ledger and all. The
    // hand-written removal that used to stand here was one of the four
    // player-specific paths.
    settle_squad_casualties(gs, st, playerH, playerCas);
    // His wound settles through THE door every leader's does
    // (settle_leader_fraction), not through a second copy of the same three
    // lines of arithmetic — and it lands in THE store (his squad's Pools),
    // because since landing 4 there is nowhere else for a bar to live. The
    // back-copy onto PlayerState that used to follow this call was the last
    // breath of the two-store era.
    settle_leader_fraction(st, playerH,
                           std::clamp(playerFraction, 0.0f, 1.0f));

    // The enemy's dead are FACTS, and killing them has a PRICE — the same two
    // the fought version pays through the reaper (damage-door Inc 6). The
    // crime is the registry's column, so a bandit costs nothing and a
    // peasant costs the same here as underfoot.
    report_battle_deaths(mw, st, enemy, enemyCas, enemyFraction <= 0.0f,
                         MacroHandle{});
    const char* enemyFaction = squad_faction_id(st, enemy);
    if (!kill_is_no_crime(enemyFaction)) {
        const int fallen = int(enemyCas.size())
            + (enemyFraction <= 0.0f ? 1 : 0);
        for (int i = 0; i < fallen; ++i) {
            add_player_reputation(gs, enemyFaction, kKillRepPenalty);
        }
    }

    // Spoils: РОВНО то, что павший НЁС. Бросок реестра лута по роли ушёл
    // вместе с хардкод-таблицами (M-139): сумку павшего лидера переносит
    // `loot_fallen_owner` ниже — один перенос, ноль печати. Контейнерные
    // мертвецы своих сумок не имеют (они записи, не энтити) и до ПУЛА ЛУТА
    // не роняют ничего.

    settle_squad_casualties(gs, st, enemy, enemyCas);
    settle_leader_fraction(st, enemy, enemyFraction);
    if (playerWon && macro_dead(st, enemy)) {
        loot_fallen_owner(st, enemy, *playerBag);
    }
    kill_fallen_squad_creatures(st, gs, mw.econFacts, mw.econFactsUser);

    // Пара Killed+Died — ТА ЖЕ дверь, что у ИИ↔ИИ (хвост 2б, владелец
    // 2026-09-02): осиротевшие дома жертв игрока получают Died, и
    // ненависть/цена опасности/поле угрозы видят игрока-мясника так же,
    // как любого лорда.
    if (st.valid(playerH)) {
        const MacroHandle pw = playerWon ? playerH : enemy;
        const MacroHandle pl = playerWon ? enemy : playerH;
        record_battle_facts(st, gs, pw, pl,
                            battle_dead(playerWon ? enemyCas : playerCas,
                                        playerWon ? enemyFraction
                                                  : playerFraction),
                            battle_dead(playerWon ? playerCas : enemyCas,
                                        playerWon ? playerFraction
                                                  : enemyFraction));
    }

    if (xp > 0) {
        // ОДНА дверь оплаты (корень 5): игрок — просто лидер своего
        // сквада, WIS-дивиденд и рост листа внутри award_kill_xp.
        award_kill_xp(st, playerH, xp);
    }
    return xp;
}

} // namespace sm

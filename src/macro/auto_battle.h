// AUTO-RESOLVE — the macro law of battle (owner, 2026-08-06, CANON S4/S13 (бывший macrosim.md)):
// two AI squads meeting on the map must produce a winner without a subworld,
// or the macro sim cannot run a war at all. ONE resolver, fed by what the
// containers already say — the same table rows, the same character sheets, the
// same project_combat numbers the subworld births fight with — plus context
// (terrain, fatigue, who ambushed whom). The player's own battles may use it
// too (the M&B "auto-resolve" button): same function, same inputs, no second
// law of combat. Its agreement with the fought version is PINNED BY TEST
// (tests/auto_battle_test.cpp) as a property — same lineup, both paths, same
// winner and compatible loss magnitude — so a player who fights by hand is
// not playing a different game from the one the world plays around him.
//
// Like sub/movement.h this is a PURE law: it computes who won and who fell and
// touches nothing. The CALLER settles the world — creature rows through the
// macro-stock creature row, the leader through the tracked-death path, the
// creatures of a dead leader through kill_fallen_squad_creatures, loot and XP
// through their one registries — so the auto-battle and the fought battle
// pay their debts through the very same doors.
//
// Header-only, macro layer, deterministic in (inputs, rng seed): no ECS, no
// engine, testable everywhere.
#pragma once

#include "core/rng.h"
#include "macro/anketa.h"
#include "tables/npc.h"
#include "macro/world_row.h" // область существ единого контейнера (M-71)

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace sm {

// One side of an auto-battle: the squad as the macro layer holds it — the
// leader (the entity itself) plus the creature rows. The aura is COLLECTED BY
// THE CALLER from the leader's sheet (character_sheet.h squad_bonuses), exactly as the body
// births do, so a Leader-perk lord is stronger here and below by the same
// modifiers. Context multipliers are per-side because terrain and fatigue
// are: a squad on its hill fights the value of its hill.
struct AutoBattleSide {
    // ЛИДЕР ЕСТЬ НОСИТЕЛЬ АНКЕТЫ, И ОНА ЕДЕТ СЮДА ЦЕЛИКОМ (M-300). Строка
    // даёт БАЗУ боевого листа, анкета накладывается на неё (ЗАКОН АНКЕТЫ
    // п.4) — ровно как при воплощении тела. Здесь стояли `leaderLevel` и
    // `leaderSeed`, и они существовали РОВНО затем, чтобы резолвер КАТАЛ
    // лист заново: `make_character_sheet(type, level, seed)`. Для сквада,
    // чья анкета пришла таблицей (неподвижный со строкой стола,
    // дизайн-персонаж) или ВЫРОСЛА победами, перекат давал ДРУГОГО человека
    // того же рода и ранга, то есть авторезолв судил не того, кого судит
    // очный бой, — а CANON S13 требует обратного.
    NPCType        leaderType = NPCType::Peasant;
    CharacterSheet leaderSheet{};
    float          leaderHealthFraction = 1.0f;  // wounds walk in with him
    // ОДИН ЧИСЛО, КОТОРОГО МАКРОМИР НЕ ЗНАЕТ, — ЗАМАХ ИГРОКА. Его оценивает
    // ближняя идентичность субмира (hand_strike_fields над оружием В РУКЕ, в
    // темпе своего восстановления — дверь S14), а макро есть L1 и наверх не
    // тянется; говорит это звонящий, который видит оба мира. < 0 (умолчание)
    // значит «удар выводится из листа», как у всякого НПЦ-лидера.
    //
    // ПАРНЫЙ `leaderHpOverride` УМЕР ВМЕСТЕ С ГЕЙТОМ (M-300): потолок считался
    // из анкеты КАЖДОМУ скваду и не читался НИКОМУ — гейт требовал ОБА
    // оверрайда, а второй ставил ровно один сайт в игре (игрок). Замер
    // headless-мира: 1 088 038 постановок за день, попаданий в ветку — 0.
    // Теперь лист едет полем выше и читается всегда, а оверрайд остался один
    // — на один вопрос, которому вправду нужен нижний мир.
    float          leaderDpsOverride = -1.0f;
    // ЕДИНЫЙ КОНТЕЙНЕР сквада (M-71): бой читает только область существ,
    // предметная область для него не существует.
    const Inventory* creatures = nullptr;   // may be empty/null
    // (Поле `bonuses` умерло с `squad_bonuses` 2026-10-18: аура сквада
    //  была константой-нулём, см. некролог в macro/anketa.h.)
    // Context, composed by the caller from what the cell and the squad say:
    // terrain advantage (defender's hill, forest cover — data rows when the
    // encounter system lands) and fatigue (sp / maxSp). 1 = neutral/fresh.
    float         terrain = 1.0f;
    float         fatigue = 1.0f;
};

// Who struck from ambush: the ambusher opens the exchange with a free volley
// before the lines meet — the same edge a real ambush buys in the subworld.
enum class Ambush : std::uint8_t { None = 0, SideA, SideB };

struct AutoBattleOutcome {
    int winner = 0;   // 0 = side A, 1 = side B
    // The fallen, by the same COIN their deaths are settled under everywhere
    // (macro/army.h remove_one_soldier): a storied soul by its entityId, a
    // generic one by {kind, level, 0} — one of its stack, which is all a
    // generic ever was. The bare id list died with the creatures-as-inventory
    // (CANON S4): a generic death has no id to be listed by.
    std::vector<SoldierRecord> casualtiesA, casualtiesB;
    // The leaders' post-battle health as the FRACTION wounds already travel
    // in (sub/spawn.h): 0 = dead, and the caller routes that through the one
    // tracked-death path. A winner limps out; a loser's leader dies ONLY if
    // his whole creatures died with him — never by dice (owner ruling).
    float leaderFractionA = 1.0f, leaderFractionB = 1.0f;
};

namespace auto_battle_detail {

// The armour a LEADER brings to a macro battle, as ONE number: physical
// blows are what macro armies trade today, so the credit is the mean of the
// TWO physical columns (Pierce and Blunt — Slash folded into Pierce
// 2026-10-01, M-193). When auto-resolve learns attack types, the pairing
// reads the column the OPPONENT's blow names — this helper is the
// approximation, not a second law.
inline int auto_battle_armor(const Defense& d, const Skills& skills) {
    const DefenseSum p = body_defense(d, skills, nullptr, nullptr,
                                      DamageType::Pierce);
    const DefenseSum b = body_defense(d, skills, nullptr, nullptr,
                                      DamageType::Blunt);
    return (p.armor + b.armor) / 2;
}

// Armour enters as EFFECTIVE HP through the PERCENT column of the defence law
// (tables/damage_types.h armor_hp_mult_num/den): a blow keeps
// (kArmorFull - armor) / kArmorFull of itself, so a body of hp H absorbs
// H * kArmorFull / (kArmorFull - armor) worth of raw blows — the same law, read
// as a multiplier, and it falls BELOW 1 for negative armour the way the fought
// path does. У сотни процентов знаменатель упирается в единицу: иммунного
// бойца скаляр выразить не может и потому НЕДООЦЕНИВАЕТ — смещение названо. The FLAT BLOCK column is deliberately uncredited
// here, and it cannot be credited: its worth depends on the size of the
// incoming blow, which a per-fighter scalar does not know. Heavy gear is
// therefore worth slightly MORE in a fought battle than the auto-resolve
// credits — a named bias, not a disagreement of laws. The defence source is
// the door's own for this row: npc_def().defense (macro fighters wear no
// equipment today), assembled by the SAME body_defense the scene calls.
//
// ── ЛИДЕР — НОСИТЕЛЬ АНКЕТЫ, И ЛИСТ ПРИХОДИТ СНАРУЖИ (M-300) ───────────────
// Одна сторона сквада — одно ТЕЛО с личностью, и его стоимость считается из
// ЕГО листа: hp × урон-в-секунду листа, спроецированного на строку. `floor()`
// зеркалит `emplace_body` — оба слоя дерутся целыми.
//
// ЛИСТ ПРИХОДИТ ПАРАМЕТРОМ, А НЕ КАТАЕТСЯ ЗДЕСЬ, И ЭТО ПОЛОВИНА НАРЯДА. Здесь
// стояло `make_character_sheet(type, level, seed)`, то есть резолвер делал
// лидеру НОВОГО человека того же рода и ранга на каждый вопрос о силе. Для
// типового сквада число совпадало тождественно (анкета записана той же
// формулой по тому же сиду), но для сквада, чья анкета пришла ТАБЛИЦЕЙ
// (неподвижный со строкой стола, дизайн-персонаж) или ВЫРОСЛА победами,
// перекат стирал всё, чем он отличался. `auto_battle_side_of` при этом
// считал эффективный лист 1 088 038 раз за игровой день и ВЫБРАСЫВАЛ его:
// гейт требовал ОБА оверрайда, а второй ставил ровно один сайт в игре. Это и
// есть недовезённая половина M-282 — его свидетель проверял, что поле
// ПОСТАВЛЕНО, и ни одна строка не утверждала, что его кто-то ЧИТАЕТ (§8 п.6).
inline float leader_power(const CharacterSheet& sheet, NPCType type,
                          float healthFraction, float dpsOverride) {
    const NpcTypeDef& def = npc_def(type);
    const CombatTemplate pc = project_combat(sheet, def.combat);
    // His armour is his ROW times his TRAINING — the same sentence the fought
    // body reads (sub/damage.cpp defense_of), moved in the same commit for
    // the same reason the strike was: two answers to «сколько держит НПЦ»
    // would be two laws of battle (S13).
    const int armour = auto_battle_armor(def.defense, sheet.skills);
    // ДОЛИ ПРОСТОЯ ЗДЕСЬ НЕТ, И ЭТО НЕ ЗАБЫТО (M-194). Вердикт владельца был
    // «фиксированная доля числом» — но доля применяется к той защите, которая
    // МОЖЕТ уйти в простой, а простой берётся от веса НАДЕТОЙ брони. У
    // макро-бойца надетого нет вовсе: его защита — колонки СТРОКИ, а вросшую
    // шкуру не сбивают (вердикт того же дня: «0 для строки существа»). Значит
    // сегодня дисконтировать нечего, и множитель 1/2 здесь ВРАЛ БЫ: очный бой
    // этой же строке простоя тоже не ставит, и два конца боя разошлись бы в
    // числах — ровно второй закон боя, который S13 запрещает.
    // Доля входит В ТОТ ДЕНЬ, когда макро-боец начнёт носить снаряжение, и
    // входит одной строкой здесь; вывод её записан в CANON S13 (доля в строю =
    // 1/(1 + рековери ÷ период удара), нейтральный случай = 1/2).
    const float hp  = std::max(1.0f, std::floor(pc.hp)) *
                      std::clamp(healthFraction, 0.0f, 1.0f) *
                      (float(armor_hp_mult_num(armour))
                       / float(armor_hp_mult_den(armour)));
    // The swing is the strike's expectation (strike_mean_x2, the same
    // assembly the fought path rolls) — INCLUDING the sheet's typed skill
    // percent (project_combat.multPct, session Е 2026-09-19): the fought
    // body and this resolver moved off the hardcoded 100 in one commit,
    // because two answers to «как бьёт НПЦ» is two laws of battle (S13).
    // Crit expectation is uncredited like the threshold branch — both need
    // the opponent, which a per-fighter scalar lacks.
    const float dps = dpsOverride >= 0.0f
        ? dpsOverride
        : float(strike_mean_x2(pc.dice, pc.flatAdd, pc.multPct))
              * 0.5f / std::max(0.1f, pc.cooldown);
    return hp * dps;
}

// ── ГОЛОВА РОСТЕРА — ОБЪЕКТ, И У ОБЪЕКТА АНКЕТЫ НЕТ ─────────────────────────
// ЗАКОН ДВУХ ТАБЛИЦ (вердикт владельца 2026-10-20, дословно): «АНКЕТВ НЕ МОЖЕТ
// БЫТЬ У ОБЪЕКТОВ … в макромире массовка просто строчки в инвентаре просто
// циферки». Значит сила головы есть КОЛОНКИ ЕЁ СТРОКИ и ничего больше: hp,
// две физические колонки защиты, кости с их добавкой и процентом, период
// удара. Форма названа владельцем 2026-10-21, дословно: «сейчаа минимально
// табилца и по ней всё идёт сразу хп там броня урон и тд то есть без ролевой
// системы (в смысле уровней статов) … меч и магия 6 7 8 стайл апрг то есть хп
// урон (тип урона) броня и тд».
//
// УРОВНЯ ЗДЕСЬ НЕТ, И ЭТО ВЕРДИКТ, А НЕ ПРОПУСК (владелец, тот же день):
// «не мешай цену и уровень — уровень это строка уровня в таблице объектов
// (типа тир) а стоиомтсь это стоимость для экономики (торговли или найма) …
// никаких ветеранов пока (потом если что расширим)». То есть ТИР выражается
// СВОЕЙ СТРОКОЙ (гоблин и гоблин-вождь — две строки, CANON S12 «контекст
// ВЫБИРАЕТ строку и никогда не масштабирует тело»), а `soldier_level_factor`
// есть закон ЭКОНОМИКИ — им множатся найм (`hire_price_for`) и содержание
// (`soldier_upkeep`), и читать его в бою значило бы смешать два вопроса.
// Колонка `level` у головы при этом ЖИВА и читается своими дверями — монетой
// гибели (`SoldierRecord`), ценой и содержанием; просто не боем.
//
// ЧТО ЗДЕСЬ БЫЛО ДО M-300: `make_character_sheet(type, level, member_seed(r))`
// на КАЖДУЮ голову, то есть анкета, выданная циферке. Она считалась без ошибки
// и потому выглядела правдой — ровно как «конь несёт 110 кг» в M-297. Замер
// назвал цену обеими сторонами: (1) 39 201 130 изготовленных листов за ОДИН
// игровой день против 1 088 038 лидерских, то есть 96 % анкет мира выдавались
// объектам; (2) сила головы зависела от АДРЕСА её слота сильнее, чем от
// выслуги — у стражника разброс по адресу на одном уровне 976 против 254 за
// три уровня, — потому что `member_seed` мешал в сид `(slot, index)`. Душа,
// переехавшая в другой слот, меняла боевую силу.
//
// И ОТСЮДА ФОРМА ПРОХОДА: раз сила головы больше не зависит ни от адреса, ни
// от уровня, стак из N одинаковых душ есть РОВНО N одной — значит проход идёт
// ПО СТАКАМ, а не по душам, и зависимость от адреса стала НЕВЫРАЗИМА.
inline float head_power(NPCType type) {
    const NpcTypeDef& def = npc_def(type);
    const CombatTemplate& row = def.combat;
    // Броня — ДВЕ ФИЗИЧЕСКИЕ КОЛОНКИ СТРОКИ, средним (та же пара, что у
    // лидера выше), но БЕЗ выучки: обучение живёт в анкете, а её у объекта
    // нет. Пустой лист здесь был бы той же анкетой, только тихой, и он ВРЁТ
    // — замер: `project_combat(CharacterSheet{}, row)` добавляет +10 hp от
    // END=1 и +1 к урону КАЖДОЙ строке мира (кролик 5 → 15, жаба 3 → 13).
    const int armour = (def.defense.armor_of(DamageType::Pierce)
                        + def.defense.armor_of(DamageType::Blunt)) / 2;
    const float hp = std::max(1.0f, std::floor(row.hp))
                   * (float(armor_hp_mult_num(armour))
                      / float(armor_hp_mult_den(armour)));
    // НОЛЬ — ЗАКОННЫЙ ОТВЕТ: строка с `dice.n == 0` («no roll, total 0») не
    // бьёт вовсе, и кролик со жабой честно не добавляют скваду силы. До M-300
    // им её выдумывала добавка STR сгенерированной анкеты.
    const float dps = float(strike_mean_x2(row.dice, row.flatAdd, row.multPct))
                    * 0.5f / std::max(0.1f, row.cooldown);
    return hp * dps;
}

} // namespace auto_battle_detail

// The squad's strength — THE number the macro AI thinks with. Flee-or-fight
// decisions (Inc 5) and the resolver below read the same function, so a squad
// never runs from a fight the law says it would win: one law of strength.
//
// ДВА ВОПРОСА — ДВЕ ДВЕРИ (M-300): лидер есть носитель АНКЕТЫ и судится
// СВОИМ листом; голова есть ОБЪЕКТ в инвентаре и судится КОЛОНКАМИ своей
// строки. До этого наряда обе стороны шли одной дверью, и та каждой голове
// анкету ИЗГОТАВЛИВАЛА.
inline float squad_power(const AutoBattleSide& s) {
    using namespace auto_battle_detail;
    float power = leader_power(s.leaderSheet, s.leaderType,
                               s.leaderHealthFraction, s.leaderDpsOverride);
    if (s.creatures) {
        // ПО СТАКАМ, А НЕ ПО ДУШАМ (M-300) — тот же проход, что у
        // `creatures_size` ниже. Законно это ровно потому, что `head_power`
        // не спрашивает ни адреса, ни уровня: N одинаковых душ есть N одной.
        for (int i = s.creatures->creature_first(); i < kMaxInventorySlots;
             ++i) {
            const ItemRef& r = s.creatures->slots[std::size_t(i)];
            const std::uint16_t kind =
                std::uint16_t(creature_of_world_row(r.def));
            if (!valid_npc_kind(kind)) continue;
            power += head_power(NPCType(kind)) * float(r.count);
        }
    }
    return power * std::max(0.1f, s.terrain) * std::clamp(s.fatigue, 0.1f, 1.0f);
}

// Resolve one battle. Deterministic in (sides, ambush, rng state): the swing
// the rng contributes is bounded (±10% per side), so a clear advantage cannot
// be rolled away — near-parity fights are the only ones chance may tip,
// which is exactly how the fought version behaves.
inline AutoBattleOutcome resolve_auto_battle(const AutoBattleSide& a,
                                             const AutoBattleSide& b,
                                             Ambush ambush, Rng& rng) {
    using namespace auto_battle_detail;
    AutoBattleOutcome out{};

    float pa = squad_power(a);
    float pb = squad_power(b);
    // The ambusher's free volley: the opening exchange happens before the
    // victim's line answers, worth a flat edge to the side that sprang it.
    constexpr float kAmbushEdge = 1.30f;
    if (ambush == Ambush::SideA) pa *= kAmbushEdge;
    if (ambush == Ambush::SideB) pb *= kAmbushEdge;
    // Bounded fortune: ±10% per side.
    pa *= 0.9f + 0.2f * rng.next_f01();
    pb *= 0.9f + 0.2f * rng.next_f01();

    const bool aWins = pa >= pb;
    out.winner = aWins ? 0 : 1;
    const float pw = aWins ? pa : pb;   // winner's effective power
    const float pl = aWins ? pb : pa;

    // Loss fractions, Lanchester-shaped: the winner pays with the square-law
    // remnant (a 2:1 fight costs the victor ~13%, a near-parity fight most of
    // his line), the loser is broken — most fall, the rest are the survivors
    // the deserter pool or the rout will inherit.
    const float ratio = pl / std::max(1.0f, pw);           // (0, 1]
    const float winnerLoss =
        1.0f - std::sqrt(std::max(0.0f, 1.0f - ratio * ratio));
    const float loserLoss = std::clamp(0.55f + 0.45f * ratio, 0.0f, 1.0f);

    // Distribute a side's losses over its actual creature rows. The FRACTION
    // decides how many fell — that is the law above, and it must hold for a
    // band of three as surely as for a thousand — while the rng decides WHO,
    // because the ledger settles deaths by name (the creature row's `detail`).
    // The leader is not a creature row: he takes the battle as wounds (winner)
    // and only a broken side can lose him outright.
    auto distribute = [&rng](const AutoBattleSide& side, float lossFrac,
                             std::vector<SoldierRecord>& casualties) {
        if (!side.creatures || creatures_empty(*side.creatures)) return;
        std::vector<SoldierRecord> souls;
        souls.reserve(std::size_t(creature_count(*side.creatures)));
        for (const CreatureRef r : creatures_range(*side.creatures)) {
            if (valid_npc_kind(r.kind))
                souls.push_back(SoldierRecord{r.entityId, r.kind,
                                              std::int16_t(r.level)});
        }
        const int n = int(souls.size());
        const int fallen = std::clamp(
            int(std::lround(lossFrac * float(n))), 0, n);
        // Partial Fisher-Yates: the first `fallen` slots are the dead.
        for (int i = 0; i < fallen; ++i) {
            const int j = i + int(rng.next_u32()
                                  % std::uint32_t(n - i));
            std::swap(souls[std::size_t(i)], souls[std::size_t(j)]);
            casualties.push_back(souls[std::size_t(i)]);
        }
    };
    distribute(a, aWins ? winnerLoss : loserLoss, out.casualtiesA);
    distribute(b, aWins ? loserLoss : winnerLoss, out.casualtiesB);

    // Leaders. The winner walks out wounded in proportion to how hard the
    // fight was. The loser's leader dies ONLY when his whole creatures died
    // with him (owner ruling, 2026-08-06): a lord's head is never given to
    // chance, or lords would fall regularly to dice in a war of auto-battles
    // — defeat costs him HP, not his life, while a single man of his still
    // stands. The consequence for a squad of ONE is deliberate: a lone
    // wanderer who loses has nobody left to stand between him and the field.
    // Fractions, because that is how wounds travel between layers.
    const auto creatures_size = [](const AutoBattleSide& s) {
        int n = 0;
        if (s.creatures) {
            for (int i = s.creatures->creature_first();
                 i < kMaxInventorySlots; ++i) {
                const ItemRef& r = s.creatures->slots[std::size_t(i)];
                if (valid_npc_kind(
                        std::uint16_t(creature_of_world_row(r.def)))) {
                    n += r.count;
                }
            }
        }
        return n;
    };
    const AutoBattleSide& loserSide = aWins ? b : a;
    const auto& loserCasualties = aWins ? out.casualtiesB : out.casualtiesA;
    const bool creaturesWipedOut =
        int(loserCasualties.size()) >= creatures_size(loserSide);
    const float winnerFraction = std::clamp(1.0f - winnerLoss * 0.8f,
                                            0.05f, 1.0f);
    const float loserFraction = creaturesWipedOut
        ? 0.0f
        : std::clamp(0.25f * (1.0f - ratio) + 0.05f, 0.05f, 1.0f);
    const float startA = std::clamp(a.leaderHealthFraction, 0.0f, 1.0f);
    const float startB = std::clamp(b.leaderHealthFraction, 0.0f, 1.0f);
    out.leaderFractionA = startA * (aWins ? winnerFraction : loserFraction);
    out.leaderFractionB = startB * (aWins ? loserFraction : winnerFraction);
    return out;
}

} // namespace sm

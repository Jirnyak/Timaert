#include "events/effect_applicator.h"
#include "macro/currency.h"
#include "macro/anketa.h"
#include <cstdio>

namespace sm {

// ЗДЕСЬ СТОЯЛА ПЯТАЯ ПОЛУДВЕРЬ — И ОНА СНЕСЕНА ЦЕЛИКОМ (M-116, 2026-09-28).
// `apply_effect` со своей таблицей `kEffectVerbs` переводила шесть СТРОКОВЫХ
// глаголов ("heal_hp", "restore_hp", "restore_mp", "restore_sp", "drain_sp",
// "grant_xp") в строки реестра бонусов — то есть пересказывала своим словарём
// арифметику, которой реестр уже владеет. Жила она ровно на одной руке
// `EventTag::ApplyEffect`, а этот тег во всём `src/` не рождал НИКТО: перепись
// нашла только её же `case` и конструкции в тестах. Тег снесён, и с ним ушёл
// последний житель-строка внутри аппликатора (ЗАКОН СЛОВАРЯ п.1).
//
// Вместе с ним умерли параметры `pools` и `sheet`: их читала только эта дверь.
// Колонка без читателя не лежит тихо (DOD п.9) — следующий принял бы их за
// закон. Восстановление здоровья и опыт остаются там, где у них есть настоящие
// двери: `apply_instant` над реестром бонусов и `award_exp`.

void apply_events(std::span<const GameEvent> events, GameState& gs,
                  Inventory* bag, SpellBook* book,
                  std::vector<GameEvent>* followups) {
    PlayerState& p = gs.player;
    for (auto& ev : events) {
        switch (ev.tag) {
            case EventTag::SpireDepleted:
                // The orb taught its spell: resolve the registry ordinal the
                // engine emitted (ev.b) against kSpellDefs — the applicator
                // may ask the registry itself now (Rule 13). Learn + journal
                // only on FIRST learn; the SpellLearned announcement below is
                // re-applied idempotently by this same switch.
                if (ev.b < std::uint32_t(kSpellCount)) {
                    const SpellDef& def = kSpellDefs[ev.b];
                    if (book && spellbook_learn(*book, int(ev.b))) {
                        char msg[96];
                        std::snprintf(msg, sizeof(msg),
                                      "You have learned %s!", def.name);
                        session_feed_push(gs.sessionFeed, msg);
                        if (followups) {
                            GameEvent learned{EventTag::SpellLearned};
                            learned.s1 = def.id;
                            followups->push_back(std::move(learned));
                        }
                    }
                }
                break;
            case EventTag::QuestComplete:
                // The engine settles its own quests (b marks that); this arm
                // serves quest events raised by OTHER emitters — an authored
                // chain completing a quest still counts in the tally. The
                // event names the quest by ordinal only, so there is no
                // offer provenance to settle here — and an authored quest
                // has none (bornDay -1).
                if (ev.b != kEventEffectAlreadyApplied) {
                    ++p.completedQuestCount;
                }
                break;
            case EventTag::QuestFail:
                if (ev.s2 == "abandoned") {
                    break;
                }
                if (ev.b == kEventEffectAlreadyApplied) {
                    break;
                }
                ++p.failedQuestCount;
                break;
            case EventTag::SpellLearned:
                // The event still speaks the string id (the bus->chronicle
                // merge will retire it); the book itself is ordinals only.
                if (book) spellbook_learn(*book, spell_ordinal(ev.s1));
                break;
            case EventTag::PlayerGoldChange:
                if (ev.b != kEventEffectAlreadyApplied) {
                    if (!bag) break;
                    // Gold is VALUE, и ОТДАТЬ её можно (по одному закону
                    // плотности), а ВЗЯТЬСЯ ей неоткуда: выдача монет из
                    // воздуха снесена (M-139, вердикт владельца 2026-09-26).
                    // Положительная дельта здесь больше не выплачивается —
                    // платит только тот, у кого стоимость есть (даритель
                    // квеста через `transfer_value_dense`), а процедурную
                    // награду будет раздавать пул лута.
                    if (ev.ix < 0) pay_value_dense(*bag, -ev.ix);
                }
                break;
            case EventTag::ReputationChange:
                if (ev.b != kEventEffectAlreadyApplied) {
                    add_player_reputation(gs, ev.s1.c_str(), ev.ix);
                }
                break;
            default: break;
        }
    }
}

void apply_events(const std::vector<GameEvent>& events, GameState& gs,
                  Inventory* bag, SpellBook* book,
                  std::vector<GameEvent>* followups) {
    apply_events(std::span<const GameEvent>(events.data(), events.size()), gs,
                 bag, book, followups);
}

} // namespace sm

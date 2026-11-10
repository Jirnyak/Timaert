#include "events/effect_applicator.h"
#include "macro/currency.h"
#include "bus/anketa.h"
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

// Параметр `bag` умер с веткой PlayerGoldChange (M-410): он был её
// единственным читателем, а у тега не было эмитента — колонка без
// читателя не лежит тихо (DOD п.9).
void apply_events(std::span<const GameEvent> events, SessionFeed& feed,
                  SpellBook* book,
                  std::vector<GameEvent>* followups) {
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
                        session_feed_push(feed, msg);
                        if (followups) {
                            GameEvent learned{EventTag::SpellLearned};
                            learned.s1 = def.id;
                            followups->push_back(std::move(learned));
                        }
                    }
                }
                break;
            // (Ветки QuestComplete/QuestFail умерли с квест-машиной —
            //  ломоть 3 M-72, вариант А: эмитентов у тегов не осталось.)
            case EventTag::SpellLearned:
                // The event still speaks the string id (the bus->chronicle
                // merge will retire it); the book itself is ordinals only.
                if (book) spellbook_learn(*book, spell_ordinal(ev.s1));
                break;
            // (Ветки PlayerGoldChange/ReputationChange умерли с тегами —
            //  M-410: эмитентов у обоих не было ни одного, теоретический
            //  канал effects пуст у всего авторского контента; золото и
            //  слава мира живут родом 5 — Traded и record_deed.)
            // ПОИМЁННО, НЕ default (ЗАКОН СЛОВАРЯ п.4): у этих тегов
            // применения ЗДЕСЬ нет ПО ПРАВУ — презентационные ведёт свой
            // обработчик (main.cpp, очередь модалок), Custom — дефолт
            // инициализации, не событие, Count — счёт, не тег. Новый тег
            // больше не глотается молча: без default его назовёт -Wswitch.
            case EventTag::ShowDialog:
            case EventTag::ShowStory:
            case EventTag::StoryResult:
            case EventTag::Custom:
            case EventTag::PlayerEnterSettlement:
            case EventTag::Count:
                break;
        }
    }
}

void apply_events(const std::vector<GameEvent>& events, SessionFeed& feed,
                  SpellBook* book,
                  std::vector<GameEvent>* followups) {
    apply_events(std::span<const GameEvent>(events.data(), events.size()),
                 feed, book, followups);
}

} // namespace sm

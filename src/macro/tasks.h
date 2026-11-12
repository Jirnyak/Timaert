// ── КОНСТРУКТОР ЗАДАЧ (ломоть 6б M-72) ────────────────────────────────────
//
// «из системы предикат и декларативной системы можно сделать конструктор
// процедурный квестов чтобы из контекста рандомом и состояния анкеты
// собирались цели и задачи» (владелец, 2026-10-18); «таблично и рандомно
// минимально то есть очень чисто просто элегантно… квесты могут
// комибинроваться случаной из таблцы убить Н мобов / принести предмтов /
// поестить фич / найти сквад и тд (агностично глагольно объектно)»
// (владелец, 2026-11-02).
//
// ЗАДАЧА = ГЛАГОЛ × ОБЪЕКТ × N. Глагол — строка kTaskVerbs; объект НЕ
// сочиняется — он приходит из существующих таблиц мира (товары, ось
// сквадов, фичи). Задание игрока и задача НПЦ — ОДИН объект: колонка цели
// SquadGoal@src/ecs/components.h (род 2); своего носителя у задания нет.
//
// КВЕСТ = ПРЕДИКАТ НАД ФАКТАМИ (вердикт владельца 2026-09-25, ряд M-270):
// взятие пишет QuestTaken, сдача судит летопись (или инвентарь — сдачу
// товаром) В МОМЕНТ ВОПРОСА и пишет QuestDone. Прогресс-состояния нет
// нигде: ни штабеля квестов (его кап стоил 1.45 ГиБ потолка сейва и снесён
// ломтём 3), ни флага «выполнено» (completed выводится — вердикт №6 у
// goal_spend). Офферы доски НЕ хранятся: пересборка на открытие,
// детерминированная от (сид мира, день, место).
#pragma once
#include <cstdint>
#include <iterator>

#include "bus/fact.h"           // FactKind — род судьи (строка рода 5)
#include "core/macro_handle.h"
#include "bus/squad_row.h"   // колонки сквада — род 2 фрейма (ломоть 2б-I)

namespace sm {

struct MacroStore;
struct GameState;
struct ChronicleLinks;
struct Session;
struct SquadIndex;

// Откуда глагол берёт ОБЪЕКТ. Пространства суть оси существующих таблиц:
// новый глагол называет своё пространство строкой, конструктор не меняется.
enum class TaskObjectSpace : std::uint8_t {
    Need = 0,     // товар из НУЖД места (season_demand_for) — что в дефиците
    Hostile = 1,  // сквад, враждебный месту (каркас + матрица фракций)
    Feature = 2,  // фича-достопримечательность рядом (слой фич)
};

// Строка глагола. Судья — РОД ФАКТА (None = сдача товаром через deal):
// выбор применяется данными, не веткой по имени (ЗАКОН СТРОКИ КАТАЛОГА).
struct TaskVerbDef {
    const char* id;              // ключ авторинга (граница мира)
    const char* label;           // что читает доска
    TaskObjectSpace object;      // пространство объекта
    ecs::GoalKind goalKind;      // каким родом задача ложится в цель
    FactKind judge;              // род факта-судьи; None = сдача товаром
    std::uint16_t deadlineDays;  // срок; 0 = бесконечный (ноль — значение)
    std::uint16_t baseXp;        // опыт строки (баланс); fetch берёт
                                 // стоимость товара вместо базы (0)
};

// Глаголы v1 — только те, у кого есть и генератор объекта, и судья
// (мёртвых строк не заводим). Сроки и опыт — баланс, свободно.
//
// ЗДЕСЬ СТОЯЛО «„Убить Н мобов строки“ и „найти сквад“ добавятся строками,
// когда летопись научится помнить строку жертвы (вердикт „вариант а“,
// 2026-11-02)» — ПОСЫЛКА СНЯТА ВЕРДИКТОМ ВЛАДЕЛЬЦА 2026-11-08 (дословно):
// «ИСТОРИЯ делается и менятся только сквадами (никакие обьекты в субмире
// мобы и тд если массовка в этом смысл то естетсвено нет / типа СКВАДЫ В
// МАКРОМИРЕ вот это история и клеточки макромира и поля и состя миар то
// есть 6 пакетов». Значит строки каталога в летописи не будет НИКОГДА, и
// ждать её незачем: «убить Н» выражается БЕЗ неё — целью-СКВАДОМ (ровно
// то, что уже делает строка `kill` ниже) либо убылью ПОЛЯ фауны, то есть
// родом 3, у которого своя ведомость стока. Разбор — CANON S20.1 «КТО
// БЫВАЕТ СУБЪЕКТОМ ИСТОРИИ», ряд M-423.
inline constexpr TaskVerbDef kTaskVerbs[] = {
    {"fetch", "Bring", TaskObjectSpace::Need, ecs::GoalKind::Squad,
     FactKind::None, 15, 0},
    {"kill", "Slay", TaskObjectSpace::Hostile, ecs::GoalKind::Squad,
     FactKind::Killed, 30, 200},
    {"visit", "Visit", TaskObjectSpace::Feature, ecs::GoalKind::Feature,
     FactKind::Explored, 20, 100},
};
// Упаковка контракта держит 3 бита глагола — знаковый бит int32 не трогаем.
static_assert(std::size(kTaskVerbs) <= 8,
              "task_pack carries the verb in 3 bits; bit 31 stays clear");

// Оффер доски — ЛОКАЛЬНЫЙ буфер звонящего: состоянием мира не является и в
// сейв не едет — та же доска пересобирается тем же сидом (ЗАКОН
// СТАБИЛЬНОСТИ: ноль байт на кап, ноль работы в тике).
struct TaskOffer {
    std::uint8_t verb = 0;     // строка kTaskVerbs
    std::uint16_t object = 0;  // Need: строка каталога; Feature: FT-род; иначе 0
    std::uint32_t target = 0;  // ординал цели-сквада (spawnId); 0 = не сквад
    std::uint32_t cell = 0;    // адрес цели — кэш предиката (M-1)
    std::uint16_t amount = 0;  // N
    std::uint32_t xp = 0;      // награда опытом — из чистой стоимости/строки
};

// Контракт живёт В ЛЕТОПИСИ: amount факта QuestTaken/QuestDone несёт
// {глагол 3 бита · объект 12 · счёт 16} — смысл числу даёт строка рода
// (закон рода 5: «what the kind's row says it means»). Объект шире 12 бит
// взятие ОТКАЗЫВАЕТ в точке рождения (§5 п.3), молча не режет.
inline constexpr std::int32_t task_pack(std::uint8_t verb,
                                        std::uint16_t object,
                                        std::uint16_t count) {
    return std::int32_t(((std::uint32_t(verb) & 0x7u) << 28)
                        | ((std::uint32_t(object) & 0xFFFu) << 16)
                        | std::uint32_t(count));
}
inline constexpr std::uint8_t task_verb(std::int32_t amount) {
    return std::uint8_t((std::uint32_t(amount) >> 28) & 0x7u);
}
inline constexpr std::uint16_t task_object(std::int32_t amount) {
    return std::uint16_t((std::uint32_t(amount) >> 16) & 0xFFFu);
}
inline constexpr std::uint16_t task_count(std::int32_t amount) {
    return std::uint16_t(std::uint32_t(amount) & 0xFFFFu);
}

// Сборка офферов доски места: детерминирована от (worldSeed, день, место),
// ничего не пишет и не хранит. Возвращает, сколько положила (≤ cap).
int task_offers_for(const MacroStore& st, const GameState& gs,
                    const Session& ses,
                    const SquadIndex& idx,
                    MacroHandle place, TaskOffer* out, int cap);

// Взятие: цель — игроку (та же колонка, что любому скваду), факт
// QuestTaken — миру. Первый писатель рода за всю историю словаря (M-270).
bool task_take(MacroStore& st, GameState& gs, MacroHandle player,
               MacroHandle place, const TaskOffer& offer);

// Активный контракт игрока у ЭТОГО места — ВЫВОД из летописи и цели, не
// хранимое состояние: новейший QuestTaken против новейшего QuestDone,
// сверка с колонкой цели (перезаписанная цель = брошенный контракт — задача
// есть ОДНА цель, вердикт №2).
struct TaskActive {
    bool valid = false;    // контракт есть, и цель игрока ещё его
    bool expired = false;  // срок вышел (день взятия + срок строки)
    std::uint8_t verb = 0;
    std::uint16_t object = 0;
    std::uint16_t amount = 0;
    std::int32_t takenDay = 0;
};
//
// ЧИТАЕТСЯ ЦЕПОЧКА ПО СУБЪЕКТУ (M-423): все четыре вопроса этого файла суть
// «что сделал ЭТОТ сквад», и длина их обхода — его собственная история, а не
// история места или всего мира. Индекс летописи (`ChronicleLinks`) приходит
// параметром: он выводной, в состоянии мира не лежит и догоняет лог сам.
TaskActive task_active_at(const MacroStore& st, const GameState& gs,
                          ChronicleLinks& links,
                          MacroHandle player, MacroHandle place);

// Суд «выполнено?» — ПО ХОДУ, в момент вопроса (вердикт №3): fetch —
// инвентарь игрока, kill/visit — летопись с дня взятия. Мир сам помнит;
// своего счётчика прогресса у задачи нет.
bool task_judged_done(const MacroStore& st, const GameState& gs,
                      ChronicleLinks& links,
                      MacroHandle player, const TaskActive& t);

// Сдача: судит; fetch переносит товар дверью deal (перенос = сделка с
// пустым встречным лотом); пишет QuestDone — славу платит сама дверь
// летописи (base 5 + десятина славы заказчика); опыт — squad_award_exp
// (единственный вход в прокачку); цель гасится.
bool task_turn_in(MacroStore& st, GameState& gs, ChronicleLinks& links,
                  MacroHandle player, MacroHandle place);

// Писатель визита: игрок пришёл в клетку своей цели-ФИЧИ — мир запоминает
// (Explored). Зовётся с шага игрока (слот MacroWalkReachedFn); дедуп —
// памятью мира, не своим флагом (прецедент SubZone).
void task_note_player_arrival(MacroStore& st, GameState& gs,
                              ChronicleLinks& links,
                              MacroHandle player, int x, int y);

}  // namespace sm

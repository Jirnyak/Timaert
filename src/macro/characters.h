// СТОЛ АНКЕТ — авторские дизайн-персонажи мира (owner verdicts 2026-09-10).
//
// Анкета = ИНДИВИД-субъект, «он как игрок»: одно тело в мире, владеемый
// CharacterSheet-компонент (посадки А/Б), смерть НАВСЕГДА — спавн только в
// генезисе, загрузка генезис не гоняет (SAVE-5), а мир помнит летописью.
// НЕ архетип: архетипы придут позже обобщением ротации ландмарков, и этому
// столу не мешают — рантайм-сторона (тег + компоненты) не различает,
// авторская строка за ординалом или когда-нибудь сгенерённая.
//
// ЗАКОН СТОЛА: данные в строке — решения в функции. Строка говорит, КЕМ
// персонаж рождается (тело, лист, дом, фракция, какая модель думает,
// параметры агенды); модель поведения (npc_ai.cpp, свободная функция одной
// сигнатуры) говорит, ЧТО он делает от состояния мира — она читает контекст
// (TickContext, AgentMemory-факты, летопись) каждый think. Своя ИИ-модель
// персонажа = функция + строка enum AIBehaviour, никогда ветка в диспетче.
//
// Стол живёт в macro/ рядом с kNpcTypeDefs — авторская таблица лежит там,
// где живут её типы (прецедент npc.h): её читают спавн-генезис и лестница
// effective_behaviour, оба L1, и слой content сюда не нужен.
#pragma once
#include <cstdint>
#include "macro/behaviour.h"
#include "macro/character_sheet.h"
#include "macro/npc.h"
#include "macro/state.h"

namespace sm {

// Параметры агенды — маленькая структура с ДЕФОЛТАМИ: новое поле агенды
// аппендится сюда и не трогает старые строки (designated-инициализация).
struct DesignAgenda {
    // Маршрут «дом ↔ ближайший ландмарк этого рода» для Waypoints-моделей:
    // ординал LandmarkType, −1 = маршрута нет. Абсолютные клетки в строке
    // невозможны — мир процедурен, поэтому строка называет РОД цели, а
    // спавн-дверь резолвит её из контекста, как find_valid_spawn.
    std::int8_t routeToNearest = -1;
    // Радиус вылетов от дома в клетках (модель дракона возьмёт).
    std::int16_t radiusCells = 0;
};

// Дом анкеты: ландмарк рода homeType — N-й по порядку (homeIndex ≥ 0,
// заворот по счёту) или СЛУЧАЙНЫЙ сидом мира (homeIndex < 0); ряд можно
// сузить префиксом фракции ландмарка (homeFactionPrefix, nullptr = любой:
// «случайный варварский город» = City + "barbarian"). Когда homeType ==
// LandmarkType::None — прямая клетка cellX/cellY.
struct DesignCharacterDef {
    const char*  id;        // authoring-ключ (летопись, консоль) — не рантайм
    const char*  name;      // авторское имя (текст владельца — ДОСЛОВНО)
    NPCType      body;      // строка реестра как есть: спрайт/комбат/апкип;
                            // контекст выбирает строку, не скейлит (S12)
    std::int16_t level;     // −1 = дефолт строки + бросок
    // Лист: бросок от (body, level, ординальный сид) — как у любого
    // именованного, ИЛИ авторские числа (вердикт «колонка строки»).
    bool           authoredSheet;
    CharacterSheet sheet;   // читается только при authoredSheet
    // Строка ОДНОГО реестра фракций (authoring-ключ); nullptr = фракция
    // ДОМА (царь варварского города — их человек, чей бы город ни выпал).
    const char*  factionId;
    LandmarkType homeType;  // None = клетка ниже
    std::int16_t homeIndex; // N-й ландмарк рода; < 0 = случайный сидом
    const char*  homeFactionPrefix;  // nullptr = род без фильтра фракции
    std::int16_t cellX, cellY;
    AIBehaviour  behaviour; // какая модель думает (лестница effective_behaviour)
    DesignAgenda agenda;
};

// ── СТОЛ ──────────────────────────────────────────────────────────────────
// Добавить персонажа = добавить строку (+ функцию поведения, если модель
// новая). Ординал = индекс строки; сейв несёт его в записи сквада
// (designOrdinal, v92) — каталожный закон: только АППЕНД, строки не
// переставлять и не удалять.
inline constexpr DesignCharacterDef kDesignCharacterDefs[] = {
    // Варнава, проповедник (пример владельца «проповедник город↔деревня»;
    // имя — владельца, 2026-09-10) — проба конвейера НУЛЁМ нового ИИ:
    // behaviour = Waypoints, маршрут кладёт спавн-дверь (дом ↔ ближайшая
    // деревня), думает существующая модель ai_waypoints. Тело Merchant:
    // одиночка без апкипа, именованный род — лист рождается владеемым в
    // make_npc. Фракция Empire of Light — чьё слово он несёт.
    {
        "varnava", "Varnava",
        NPCType::Merchant, /*level*/ 3,
        /*authoredSheet*/ false, CharacterSheet{},
        "empire",
        LandmarkType::City, /*homeIndex*/ 0, /*homeFaction*/ nullptr,
        /*cell*/ 0, 0,
        AIBehaviour::Waypoints,
        DesignAgenda{.routeToNearest =
                         std::int8_t(LandmarkType::Village)},
    },
    // Царь-крестьянин (владелец, 2026-09-10; тестовая анкета — контент
    // доработается ещё много раз): тело Peasant как есть, лист — бросок
    // 70-го уровня, ОДИН (без свиты), дом — СЛУЧАЙНЫЙ варварский город,
    // фракция — ОН САМ (вердикт владельца: своя строка реестра, как у
    // игрока — индивид-субъект со своей строкой матрицы), поводка нет —
    // чистый роамер. Охота: ТОЛЬКО маги Магики (тела Witch/Sorceress
    // фракции magika) — не культисты и НЕ крестьяне магики (слово
    // владельца, важно). Модель — своя функция ai_mage_hunt (npc_ai.cpp),
    // первая проба «своя ИИ-модель = функция + строка».
    {
        "king_peasant", "King-Peasant",
        NPCType::Peasant, /*level*/ 70,
        /*authoredSheet*/ false, CharacterSheet{},
        "king_peasant",
        LandmarkType::City, /*homeIndex: случайный*/ -1,
        /*homeFaction*/ "barbarian",
        /*cell*/ 0, 0,
        AIBehaviour::MageHunt,
        DesignAgenda{},
    },
};

inline constexpr std::int16_t kDesignCharacterCount =
    std::int16_t(sizeof(kDesignCharacterDefs) / sizeof(kDesignCharacterDefs[0]));

// Строка по ординалу тега — nullptr на мусорный ординал (запись из чужого
// будущего сейва каталожный закон объявляет невозможной, но дверь честная).
inline constexpr const DesignCharacterDef* design_character(std::int16_t ord) {
    return ord >= 0 && ord < kDesignCharacterCount
        ? &kDesignCharacterDefs[ord] : nullptr;
}

} // namespace sm

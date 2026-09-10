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

// Дом анкеты: N-й ландмарк рода (индекс заворачивается по счёту рода) или,
// когда homeType == LandmarkType::None, прямая клетка cellX/cellY.
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
    const char*  factionId; // строка ОДНОГО реестра фракций (authoring-ключ)
    LandmarkType homeType;  // None = клетка ниже
    std::int16_t homeIndex; // N-й ландмарк рода
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
        LandmarkType::City, /*homeIndex*/ 0, /*cell*/ 0, 0,
        AIBehaviour::Waypoints,
        DesignAgenda{.routeToNearest =
                         std::int8_t(LandmarkType::Village)},
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

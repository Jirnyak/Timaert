// The macro-ECS snapshot (Session 17) — the save's view of the living map.
//
// The save is a full snapshot of the MACRO world and of nothing else
// (AGENTS.md → Persistence). Until this header the macro ECS was the one part
// of that world the save could not see: every load cleared the registry and
// re-spawned lords from the seed, so a killed squad rose again, a levelled
// leader forgot his campaigns, and a runtime ordinal could be reissued to a
// stranger (problems.md 19.24).
//
// A MacroNpcRecord is ONE macro entity flattened to rows: the POD components
// verbatim, the roster as its SoldierRecord rows, the opt-ins (orders, death,
// death) as explicit flags. «Кем я управляю» с v116 НЕ едет байтом записи:
// это поле мира (GameState::playerFlagBits), на проводе — ординал носителя
// в скалярах мира, резолв — resolve_player_handles_after_load строго после
// restore (SAVE-5 закрыт тем же законом: второй склад «кем управляю» вне
// снимка не существует, генезис на загрузке не гоняется).
#pragma once
#include <cstdint>
#include <vector>

#include "ecs/components.h"
#include "ecs/world.h"
#include "macro/agent_memory.h"
#include "macro/anketa.h"
#include "macro/spell_book_state.h"

namespace sm {

struct GameState;

struct MacroNpcRecord {
    ecs::MacroSpawnId    spawnId{};
    ecs::MacroCell       cell{};
    ecs::MacroVisual       visual{};
    ecs::NPCKind         kind{};
    ecs::Pools           pools{};
    ecs::NpcLevel        level{};
    ecs::MacroNpcRuntime runtime{};
    ecs::NpcTraits       traits{};
    ecs::NpcCharacter    character{};
    // The body's KNOWLEDGE of the spell registry (§41 root 3, v89): two
    // 256-bit planes + the active ordinal — a component like the pools,
    // born all-zero with every squad and ridden verbatim.
    SpellBook            book{};
    ecs::SquadOrders     orders{};          // meaningful iff hasOrders
    AgentMemory          memory{};          // what the leader remembers (v28)
    // The OWNED sheet of a NAMED character (v90, owner 2026-09-10 ММОРПГ-
    // модель: «у каждого персистентного персонажа свой лист и идёт в
    // сейв»). Opt-in like orders: a transient crew derives its generic
    // sheet from its row and writes nothing — a stored copy of a
    // derivable sheet would be a second truth.
    CharacterSheet       sheet{};           // meaningful iff hasSheet
    std::uint8_t         hasSheet = 0;
    std::uint8_t         hasOrders = 0;
    std::uint8_t         dead = 0;
    // Ординал строки стола анкет (v92, macro/characters.h) — −1 у всякого
    // обычного сквада. Едет байтами, восстанавливается тегом: смерть
    // навсегда держится именно этим — генезис на загрузке не гоняется, и
    // погибшая анкета в снапшоте просто отсутствует.
    std::int16_t         designOrdinal = -1;
    Inventory            inventory;         // NpcInventory.inv
    // What this body WEARS (ecs::BodyEquipment, M-183): маска тела + 480
    // ячеек-индексов в `inventory` выше — истина вещи одна, инвентарь.
    Gear                 gear;
    // (Слоты существ ростера уехали в `inventory` слиянием M-71 — область
    // существ единого контейнера едет вместе с предметами одной копией.)
    // СЧЁТ СОДЕРЖАНИЯ РОСТЕРА (v105, CANON S10 «у всякого, кто кормит, есть
    // счёт»): непогашенный харч по лестнице и непогашенная плата. Едут в
    // сейв, потому что это ДОЛГ — состояние мира, а не производное: сквад,
    // сохранённый в середине сезона, обязан проснуться должным ровно
    // столько же, иначе перезагрузка кормит его армию бесплатно.
    std::int32_t rosterNeedDebt[kCommodityCount] = {};
    // ДЫРА ВЫРАВНИВАНИЯ, НАЗВАННАЯ ПОЛЕМ (M-183): needDebt (60 Б) кончается
    // на ≡4 mod 8, а wageDebt хочет 8 — у записи с NSDMI паддинг
    // НЕОПРЕДЕЛЁН, и байтовый свидетель снапшота читал бы мусор. Явное поле
    // зануляется инициализатором и делает раскладку детерминированной; в
    // сейв НЕ пишется (сейв ходит полями, не байтами).
    std::int32_t rosterDebtPad = 0;
    std::int64_t rosterWageDebt = 0;
};

// Flatten every persistent macro NPC (the view is keyed by MacroSpawnId — the
// component only make_npc emplaces) into records, sorted by ordinal so the
// payload bytes are deterministic for one same world state.
std::vector<MacroNpcRecord> snapshot_macro_ecs(ecs::World& w);

// Re-embody the records in an (already cleared of macro NPCs) registry.
// Also self-heals gs.nextMacroSpawnOrdinal to stay ABOVE every restored
// ordinal — the counter must never reissue a living identity.
void restore_macro_ecs(const std::vector<MacroNpcRecord>& records,
                       ecs::World& w, GameState& gs);

} // namespace sm

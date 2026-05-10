// Spellbook runtime behavior. The state struct itself lives in macro/
// because PlayerState owns it; this content layer wires it to spell defs.
#pragma once
#include <cstdint>
#include <string>
#include "macro/attributes.h"
#include "macro/spell_book_state.h"
#include "content/spells/spell_types.h"

namespace sm {

bool spellbook_can_cast(const SpellBook& sb, const CombatStats& combat,
                        const std::string& id);
int  spellbook_start_cast(SpellBook& sb, CombatStats& combat,
                          const std::string& id);
bool spellbook_cast(ecs::World& w, SpellBook& sb, CombatStats& combat,
                    const std::string& id,
                    std::uint32_t playerId, float px, float py, float nx, float ny);
void spellbook_tick(SpellBook& sb, CombatStats& combat, float dt);

} // namespace sm

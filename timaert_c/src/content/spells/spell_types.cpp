#include "content/spells/spell_types.h"

namespace sm {

void SpellRegistry::add(SpellDef d) {
    auto i = spells_.size();
    index_[d.id] = i;
    spells_.push_back(std::move(d));
}
const SpellDef* SpellRegistry::find(const std::string& id) const {
    auto it = index_.find(id);
    return it == index_.end() ? nullptr : &spells_[it->second];
}

SpellRegistry& spell_registry() {
    static SpellRegistry r;
    return r;
}

bool cast_spell(ecs::World& w, const std::string& id,
                std::uint32_t playerId, float px, float py, float nx, float ny) {
    auto* s = spell_registry().find(id);
    if (!s) return false;
    SpellSpawnContext ctx{px, py, nx, ny, s->baseDamage, 320.0f, 6.0f, 0.0f,
                          false, playerId, std::uint32_t(std::hash<std::string>{}(id))};
    if (s->spawn) s->spawn(w, ctx);
    return true;
}

} // namespace sm

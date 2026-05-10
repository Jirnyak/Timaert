// Spell system — registry + simple modular adders. Mirrors spells/spell-types.ts.
// Adding a spell: one register_*() call in content/spells/registry.cpp.
#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>
#include "ecs/components.h"
#include "ecs/world.h"

namespace sm {

enum class SpellTag : std::uint8_t {
    Fire, Ice, Lightning, Dark, Light, Earth, Air, Arcane, Body, Mind,
};

struct SpellSpawnContext {
    float px, py;
    float nx, ny;
    float damage, speed, radius, blastRadius;
    bool  friendlyFire;
    std::uint32_t playerId;
    std::uint32_t spellId;
};

// Spawn callback creates the projectile/effect on the ECS world.
using SpellSpawnFn = std::function<void(ecs::World&, const SpellSpawnContext&)>;

struct SpellDef {
    std::string id;
    std::string name;
    SpellTag    tag;
    int         manaCost;
    float       cooldown;
    float       baseDamage;
    SpellSpawnFn spawn;
};

class SpellRegistry {
public:
    void add(SpellDef d);
    const SpellDef* find(const std::string& id) const;
    std::size_t size() const { return spells_.size(); }
    bool is_consistent() const;
    const std::vector<SpellDef>& all() const { return spells_; }

private:
    std::vector<SpellDef>                      spells_;
    std::unordered_map<std::string, std::size_t> index_;
};

// Singleton.
SpellRegistry& spell_registry();

// Cast a spell — returns false if mana/cooldown insufficient.
bool cast_spell(ecs::World& w, const std::string& id,
                std::uint32_t playerId, float px, float py, float nx, float ny);

} // namespace sm

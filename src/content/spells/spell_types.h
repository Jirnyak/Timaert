// Spell system registry and spell effect descriptors.
// Mirrors the TS spell-types shape closely enough for native casting/UI.
#pragma once

#include <cstddef>
#include <cstdint>
#include <array>
#include <string>
#include <unordered_map>
#include <vector>

#include "ecs/components.h"
#include "ecs/world.h"

namespace sm {

enum class SpellTag : std::uint8_t {
    Fire, Ice, Lightning, Dark, Light, Earth, Air, Arcane, Body, Mind,
};

enum class SpellRarity : std::uint8_t {
    Common, Uncommon, Rare, Epic, Mythic,
};

enum class DeliveryShape : std::uint8_t {
    Projectile, Beam, Nova, Self, Aura, Chain, Summon, Targeted,
};

enum class MacroEffectType : std::uint8_t {
    None,
    TravelSpeed,
    IgnoreTerrain,
    HealParty,
    DamageRegion,
    RevealMap,
    BuffArmy,
};

static constexpr std::size_t kMaxSpellFlavorItems = 5;
using SpellRngFn = float (*)(void*);

struct SpellSpawnContext {
    float px, py;
    float playerRadius;
    float nx, ny;
    float damage;
    float speed;
    float projectileRadius;
    float effectRadius;
    bool  friendlyFire;
    std::uint32_t playerId;
    std::uint32_t spellId;
    SpellRngFn rng01 = nullptr;
    void* rngUser = nullptr;
};

using SpellSpawnFn = void (*)(ecs::World&, const SpellSpawnContext&);

// TS SubworldScreen.svelte creates the player entity with radius 1.5, and
// engine.ts offsets spawned spell visuals by player.radius + 2.
static constexpr float kSpellCasterRadius = 1.5f;

struct SpellDef {
    std::string id;
    std::string name;
    // ASCII fallback for ImGui's default font; sourceIcon keeps the TS icon.
    const char* icon;
    const char* sourceIcon;
    SpellTag    tag;
    SpellTag    secondaryTag;
    std::uint8_t tagCount;
    SpellRarity rarity;
    DeliveryShape shape;
    int         tier;
    int         manaCost;
    float       cooldown;
    float       castTime;
    bool        sustained;
    float       manaDrain;
    bool        hasMicro;
    bool        hasMacro;
    float       baseDamage;
    float       baseHeal;
    float       baseRadius;
    int         chainCount;
    float       chainDecay;
    float       speed;
    float       duration;
    bool        friendlyFire;
    const char* statusEffect;
    float       statusDuration;
    float       scalingPower;
    float       scalingDuration;
    float       scalingRadius;
    float       projectileRadius;
    float       projectileLife;
    float       beamLength;
    SpellSpawnFn spawn;
    const char* description;
    MacroEffectType macroType = MacroEffectType::None;
    float       macroPower = 0.0f;
    float       macroDuration = 0.0f;
    std::array<const char*, kMaxSpellFlavorItems> pros{};
    std::uint8_t prosCount = 0;
    std::array<const char*, kMaxSpellFlavorItems> cons{};
    std::uint8_t consCount = 0;
};

class SpellRegistry {
public:
    void add(SpellDef d);
    const SpellDef* find(const std::string& id) const;
    std::size_t size() const { return spells_.size(); }
    bool is_consistent() const;
    const std::vector<SpellDef>& all() const { return spells_; }

private:
    std::vector<SpellDef> spells_;
    std::unordered_map<std::string, std::size_t> index_;
};

SpellRegistry& spell_registry();

bool cast_spell(ecs::World& w, const std::string& id,
                std::uint32_t playerId, float px, float py, float nx, float ny);
bool cast_spell(ecs::World& w, const SpellDef& spell,
                const SpellSpawnContext& ctx);

std::uint32_t stable_spell_id(const std::string& id);
const char* spell_tag_label(SpellTag tag);
bool spell_has_tag(const SpellDef& spell, SpellTag tag);
const char* spell_rarity_label(SpellRarity rarity);
const char* spell_shape_label(DeliveryShape shape);
const char* spell_macro_label(MacroEffectType macro);

} // namespace sm

#include "content/spells/spell_types.h"

namespace sm {

void SpellRegistry::add(SpellDef d) {
    auto it = index_.find(d.id);
    if (it != index_.end()) {
        spells_[it->second] = std::move(d);
        return;
    }
    auto i = spells_.size();
    index_[d.id] = i;
    spells_.push_back(std::move(d));
}
const SpellDef* SpellRegistry::find(const std::string& id) const {
    auto it = index_.find(id);
    return it == index_.end() ? nullptr : &spells_[it->second];
}

bool SpellRegistry::is_consistent() const {
    if (index_.size() != spells_.size()) return false;
    for (std::size_t i = 0; i < spells_.size(); ++i) {
        const auto it = index_.find(spells_[i].id);
        if (it == index_.end() || it->second != i) return false;
    }
    return true;
}

SpellRegistry& spell_registry() {
    static SpellRegistry r;
    return r;
}

std::uint32_t stable_spell_id(const std::string& id) {
    std::uint32_t h =
        std::uint32_t{2147483647} + std::uint32_t{18652614};
    for (char c : id) {
        h ^= static_cast<std::uint32_t>(static_cast<std::uint8_t>(c));
        h *= std::uint32_t{16777619};
    }
    return h;
}

bool cast_spell(ecs::World& w, const std::string& id,
                std::uint32_t playerId, float px, float py, float nx, float ny) {
    auto* s = spell_registry().find(id);
    if (!s) return false;
    SpellSpawnContext ctx{px, py, nx, ny,
                          s->baseDamage,
                          s->speed > 0.0f ? s->speed : 300.0f,
                          s->projectileRadius,
                          s->baseRadius,
                          s->friendlyFire,
                          playerId,
                          stable_spell_id(s->id)};
    return cast_spell(w, *s, ctx);
}

bool cast_spell(ecs::World& w, const SpellDef& spell,
                const SpellSpawnContext& ctx) {
    if (!spell.spawn) return false;
    spell.spawn(w, ctx);
    return true;
}

const char* spell_tag_label(SpellTag tag) {
    switch (tag) {
        case SpellTag::Fire: return "Fire";
        case SpellTag::Ice: return "Ice";
        case SpellTag::Lightning: return "Lightning";
        case SpellTag::Dark: return "Dark";
        case SpellTag::Light: return "Light";
        case SpellTag::Earth: return "Earth";
        case SpellTag::Air: return "Air";
        case SpellTag::Arcane: return "Arcane";
        case SpellTag::Body: return "Body";
        case SpellTag::Mind: return "Mind";
    }
    return "";
}

bool spell_has_tag(const SpellDef& spell, SpellTag tag) {
    if (spell.tag == tag) return true;
    return spell.tagCount > 1 && spell.secondaryTag == tag;
}

const char* spell_rarity_label(SpellRarity rarity) {
    switch (rarity) {
        case SpellRarity::Common: return "Common";
        case SpellRarity::Uncommon: return "Uncommon";
        case SpellRarity::Rare: return "Rare";
        case SpellRarity::Epic: return "Epic";
        case SpellRarity::Mythic: return "Mythic";
    }
    return "";
}

const char* spell_shape_label(DeliveryShape shape) {
    switch (shape) {
        case DeliveryShape::Projectile: return "Projectile";
        case DeliveryShape::Beam: return "Beam";
        case DeliveryShape::Nova: return "Nova";
        case DeliveryShape::Self: return "Self";
        case DeliveryShape::Aura: return "Aura";
        case DeliveryShape::Chain: return "Chain";
        case DeliveryShape::Summon: return "Summon";
        case DeliveryShape::Targeted: return "Targeted";
    }
    return "";
}

const char* spell_macro_label(MacroEffectType macro) {
    switch (macro) {
        case MacroEffectType::None: return "None";
        case MacroEffectType::TravelSpeed: return "Travel Speed";
        case MacroEffectType::IgnoreTerrain: return "Ignore Terrain";
        case MacroEffectType::HealParty: return "Heal Party";
        case MacroEffectType::DamageRegion: return "Damage Region";
        case MacroEffectType::RevealMap: return "Reveal Map";
        case MacroEffectType::BuffArmy: return "Buff Army";
    }
    return "";
}

} // namespace sm

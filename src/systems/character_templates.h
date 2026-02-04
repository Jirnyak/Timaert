#pragma once

#include <cstdint>
#include <cstdlib>
#include "core/types.h"
#include "systems/attributes.h"

// ============================================================================
// CHARACTER TEMPLATE SYSTEM
// Universal system for generating NPCs with level-based attributes
// Developers can define character templates that automatically generate
// attributes based on level ranges
// ============================================================================

struct CharacterTemplate {
    NPCType type;
    std::int32_t min_level;
    std::int32_t max_level;
    
    // Base HP/MP/SP values (before attribute multipliers)
    std::int32_t base_hp;
    std::int32_t base_mp;
    std::int32_t base_sp;
    
    // Attribute distribution weights (0-100, total = 100 for balanced)
    // These control how randomly distributed attribute points favor certain stats
    std::uint8_t str_weight;
    std::uint8_t end_weight;
    std::uint8_t agi_weight;
    std::uint8_t wil_weight;
    std::uint8_t int_weight;
    std::uint8_t wis_weight;
    std::uint8_t lck_weight;
    std::uint8_t spd_weight;
    std::uint8_t cha_weight;
    
    // Difficulty modifier for EXP calculation (0.5=easy, 1.0=normal, 1.5=elite, 2.0=boss)
    float difficulty_modifier;
    
    // Faction
    FactionID faction;
    
    // Skills granted
    SkillID skills[4];
    std::uint8_t skill_count;
};

// ============================================================================
// CHARACTER TEMPLATE DEFINITIONS
// Add new character types here - the system will automatically handle
// attribute distribution based on level
// ============================================================================

static constexpr CharacterTemplate kCharacterTemplates[] = {
    // Peasant: Low level (1-5), balanced attributes, weak
    {
        .type = NPCType::Peasant,
        .min_level = 1,
        .max_level = 5,
        .base_hp = 50,
        .base_mp = 5,
        .base_sp = 80,
        .str_weight = 10,
        .end_weight = 15,
        .agi_weight = 12,
        .wil_weight = 10,
        .int_weight = 8,
        .wis_weight = 10,
        .lck_weight = 15,
        .spd_weight = 10,
        .cha_weight = 10,
        .difficulty_modifier = 0.5f,
        .faction = FactionID::Faction1,
        .skills = {SkillID::Bodybuilding, SkillID::Travel, SkillID::Fighter, SkillID::Bodybuilding},
        .skill_count = 0
    },
    
    // Woodcutter: Low-mid level (2-7), STR/END focused
    {
        .type = NPCType::Woodcutter,
        .min_level = 2,
        .max_level = 7,
        .base_hp = 60,
        .base_mp = 5,
        .base_sp = 100,
        .str_weight = 25,
        .end_weight = 25,
        .agi_weight = 8,
        .wil_weight = 5,
        .int_weight = 5,
        .wis_weight = 7,
        .lck_weight = 10,
        .spd_weight = 10,
        .cha_weight = 5,
        .difficulty_modifier = 0.5f,
        .faction = FactionID::Faction1,
        .skills = {SkillID::Fighter, SkillID::Bodybuilding, SkillID::Travel, SkillID::Bodybuilding},
        .skill_count = 1
    },
    
    // Merchant: Mid level (5-12), CHA/WIS focused
    {
        .type = NPCType::Merchant,
        .min_level = 5,
        .max_level = 12,
        .base_hp = 70,
        .base_mp = 10,
        .base_sp = 90,
        .str_weight = 5,
        .end_weight = 10,
        .agi_weight = 8,
        .wil_weight = 12,
        .int_weight = 15,
        .wis_weight = 20,
        .lck_weight = 12,
        .spd_weight = 8,
        .cha_weight = 20,
        .difficulty_modifier = 1.0f,
        .faction = FactionID::Faction1,
        .skills = {SkillID::Travel, SkillID::Bodybuilding, SkillID::Fighter, SkillID::Bodybuilding},
        .skill_count = 1
    },
    
    // Caravan: Mid level (8-15), END/STR focused, tanky
    {
        .type = NPCType::Caravan,
        .min_level = 8,
        .max_level = 15,
        .base_hp = 150,
        .base_mp = 8,
        .base_sp = 120,
        .str_weight = 20,
        .end_weight = 25,
        .agi_weight = 5,
        .wil_weight = 10,
        .int_weight = 5,
        .wis_weight = 10,
        .lck_weight = 10,
        .spd_weight = 5,
        .cha_weight = 10,
        .difficulty_modifier = 1.5f,
        .faction = FactionID::Faction1,
        .skills = {SkillID::Travel, SkillID::Bodybuilding, SkillID::Fighter, SkillID::Bodybuilding},
        .skill_count = 2
    },
    
    // Bandit: Low-mid level (3-10), AGI/STR focused, aggressive
    {
        .type = NPCType::Bandit,
        .min_level = 3,
        .max_level = 10,
        .base_hp = 80,
        .base_mp = 5,
        .base_sp = 110,
        .str_weight = 20,
        .end_weight = 12,
        .agi_weight = 20,
        .wil_weight = 8,
        .int_weight = 5,
        .wis_weight = 5,
        .lck_weight = 15,
        .spd_weight = 12,
        .cha_weight = 3,
        .difficulty_modifier = 1.0f,
        .faction = FactionID::Faction2,
        .skills = {SkillID::Fighter, SkillID::Bodybuilding, SkillID::Travel, SkillID::Bodybuilding},
        .skill_count = 2
    },
    
    // Guard: Mid level (6-14), balanced combat, STR/END focused
    {
        .type = NPCType::Guard,
        .min_level = 6,
        .max_level = 14,
        .base_hp = 120,
        .base_mp = 8,
        .base_sp = 110,
        .str_weight = 20,
        .end_weight = 20,
        .agi_weight = 12,
        .wil_weight = 10,
        .int_weight = 8,
        .wis_weight = 8,
        .lck_weight = 10,
        .spd_weight = 10,
        .cha_weight = 2,
        .difficulty_modifier = 1.5f,
        .faction = FactionID::Faction1,
        .skills = {SkillID::Fighter, SkillID::Bodybuilding, SkillID::Travel, SkillID::Bodybuilding},
        .skill_count = 2
    },
    
    // Witch: High level (10-20), INT/WIL focused, magic user
    {
        .type = NPCType::Witch,
        .min_level = 10,
        .max_level = 20,
        .base_hp = 70,
        .base_mp = 100,
        .base_sp = 90,
        .str_weight = 3,
        .end_weight = 8,
        .agi_weight = 12,
        .wil_weight = 25,
        .int_weight = 25,
        .wis_weight = 15,
        .lck_weight = 5,
        .spd_weight = 5,
        .cha_weight = 2,
        .difficulty_modifier = 2.0f,
        .faction = FactionID::Wilderness,
        .skills = {SkillID::Bodybuilding, SkillID::Travel, SkillID::Fighter, SkillID::Bodybuilding},
        .skill_count = 0
    },
    
    // Sorceress: High level (12-25), INT/CHA focused, powerful mage
    {
        .type = NPCType::Sorceress,
        .min_level = 12,
        .max_level = 25,
        .base_hp = 60,
        .base_mp = 120,
        .base_sp = 85,
        .str_weight = 2,
        .end_weight = 5,
        .agi_weight = 10,
        .wil_weight = 22,
        .int_weight = 28,
        .wis_weight = 15,
        .lck_weight = 8,
        .spd_weight = 5,
        .cha_weight = 5,
        .difficulty_modifier = 2.0f,
        .faction = FactionID::Neutral,
        .skills = {SkillID::Bodybuilding, SkillID::Travel, SkillID::Fighter, SkillID::Bodybuilding},
        .skill_count = 0
    }
};

constexpr std::size_t kNumCharacterTemplates =
    sizeof(kCharacterTemplates) / sizeof(CharacterTemplate);

// ============================================================================
// ATTRIBUTE DISTRIBUTION FUNCTION
// Distributes (level + 9) attribute points based on template weights
// ============================================================================

inline void distribute_attributes_from_template(Attributes& out_attrs,
                                               LevelData& out_level,
                                               const CharacterTemplate& tmpl,
                                               rng_t& rng) {
    // Randomly pick level in range
    const std::int32_t level_range = tmpl.max_level - tmpl.min_level + 1;
    const std::int32_t level = tmpl.min_level + static_cast<std::int32_t>(
        random_u32_inclusive(rng, static_cast<std::uint32_t>(level_range - 1)));
    
    // Set level data
    out_level.level = level;
    out_level.exp = 0;
    out_level.exp_to_next = LevelData::calc_exp_for_level(level);
    
    // Calculate total attribute points available
    const std::int32_t total_points = level + 9;
    
    // Start with all attributes at 1
    out_attrs = Attributes{};
    
    // Calculate total weight for normalization
    const std::uint32_t total_weight = 
        tmpl.str_weight + tmpl.end_weight + tmpl.agi_weight +
        tmpl.wil_weight + tmpl.int_weight + tmpl.wis_weight +
        tmpl.lck_weight + tmpl.spd_weight + tmpl.cha_weight;
    
    if (total_weight == 0) {
        // If no weights specified, distribute evenly
        const std::int32_t points_per_attr = total_points / 9;
        const std::int32_t remainder = total_points % 9;
        
        out_attrs.str = 1 + points_per_attr;
        out_attrs.end_ = 1 + points_per_attr;
        out_attrs.agi = 1 + points_per_attr;
        out_attrs.wil = 1 + points_per_attr;
        out_attrs.int_ = 1 + points_per_attr;
        out_attrs.wis = 1 + points_per_attr;
        out_attrs.lck = 1 + points_per_attr;
        out_attrs.spd = 1 + points_per_attr;
        out_attrs.cha = 1 + points_per_attr;
        
        // Distribute remainder randomly
        for (std::int32_t i = 0; i < remainder; ++i) {
            const std::uint32_t attr_idx = random_u32_inclusive(rng, 8);
            switch (attr_idx) {
                case 0: out_attrs.str++; break;
                case 1: out_attrs.end_++; break;
                case 2: out_attrs.agi++; break;
                case 3: out_attrs.wil++; break;
                case 4: out_attrs.int_++; break;
                case 5: out_attrs.wis++; break;
                case 6: out_attrs.lck++; break;
                case 7: out_attrs.spd++; break;
                case 8: out_attrs.cha++; break;
            }
        }
    } else {
        // Distribute points based on weights with randomness
        std::int32_t points_remaining = total_points;
        
        // Allocate base points proportionally to weights
        const auto allocate = [&](std::uint8_t& attr, std::uint8_t weight) {
            if (points_remaining <= 0 || total_weight == 0)
                return;
            // Base allocation (proportional to weight)
            std::int32_t base_alloc = (total_points * weight) / total_weight;
            // Add some randomness (+/- 20%)
            const std::int32_t variance = std::max(1, base_alloc / 5);
            const std::int32_t rand_offset = static_cast<std::int32_t>(
                random_u32_inclusive(rng, static_cast<std::uint32_t>(variance * 2))) - variance;
            std::int32_t final_alloc = std::max(0, base_alloc + rand_offset);
            final_alloc = std::min(final_alloc, points_remaining);
            
            attr = 1 + static_cast<std::uint8_t>(final_alloc);
            points_remaining -= final_alloc;
        };
        
        allocate(out_attrs.str, tmpl.str_weight);
        allocate(out_attrs.end_, tmpl.end_weight);
        allocate(out_attrs.agi, tmpl.agi_weight);
        allocate(out_attrs.wil, tmpl.wil_weight);
        allocate(out_attrs.int_, tmpl.int_weight);
        allocate(out_attrs.wis, tmpl.wis_weight);
        allocate(out_attrs.lck, tmpl.lck_weight);
        allocate(out_attrs.spd, tmpl.spd_weight);
        allocate(out_attrs.cha, tmpl.cha_weight);
        
        // Distribute any remaining points randomly to highest-weight attributes
        while (points_remaining > 0) {
            const std::uint32_t rand_val = random_u32_inclusive(rng, total_weight - 1);
            std::uint32_t accum = 0;
            
            auto try_add = [&](std::uint8_t& attr, std::uint8_t weight) -> bool {
                accum += weight;
                if (rand_val < accum && attr < 255) {
                    attr++;
                    points_remaining--;
                    return true;
                }
                return false;
            };
            
            if (try_add(out_attrs.str, tmpl.str_weight)) continue;
            if (try_add(out_attrs.end_, tmpl.end_weight)) continue;
            if (try_add(out_attrs.agi, tmpl.agi_weight)) continue;
            if (try_add(out_attrs.wil, tmpl.wil_weight)) continue;
            if (try_add(out_attrs.int_, tmpl.int_weight)) continue;
            if (try_add(out_attrs.wis, tmpl.wis_weight)) continue;
            if (try_add(out_attrs.lck, tmpl.lck_weight)) continue;
            if (try_add(out_attrs.spd, tmpl.spd_weight)) continue;
            if (try_add(out_attrs.cha, tmpl.cha_weight)) continue;
            
            // Safety: if we couldn't allocate, break to prevent infinite loop
            break;
        }
    }
}

// ============================================================================
// HELPER: Get template for NPC type
// ============================================================================

inline const CharacterTemplate* get_character_template(NPCType type) {
    for (std::size_t i = 0; i < kNumCharacterTemplates; ++i) {
        if (kCharacterTemplates[i].type == type) {
            return &kCharacterTemplates[i];
        }
    }
    return nullptr;
}

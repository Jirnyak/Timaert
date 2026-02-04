#pragma once

#include <cstdint>
#include <string_view>

// ============================================================================
// SKILLS SYSTEM - Following RPG_MECHANICS.md
// ============================================================================
// Skills provide flat base stat increases before attribute multipliers
// Players earn +1 skill point per level
// Skills affect base values only, not attributes or percentages

// Passive skills from RPG_MECHANICS.md
enum class SkillID : std::uint8_t {
    Bodybuilding = 0,  // +1 base HP per rank
    Travel,            // -1% terrain SP cost per rank  
    Fighter,           // +1 base weapon damage per rank (combat skill)
    
    Count
};

struct SkillInfo {
    std::string_view name;
    std::string_view description;
    
    [[nodiscard]] constexpr SkillInfo(std::string_view n, std::string_view d) noexcept
        : name(n), description(d) {}
};

// Skill database
[[nodiscard]] inline constexpr SkillInfo get_skill_info(SkillID id) noexcept {
    switch (id) {
        case SkillID::Bodybuilding:
            return {"Bodybuilding", "+1 base HP per rank"};
        case SkillID::Travel:
            return {"Travel", "-1% terrain SP cost per rank"};
        case SkillID::Fighter:
            return {"Fighter", "+1 base weapon damage per rank"};
        default:
            return {"Unknown", ""};
    }
}

// ============================================================================
// UNIVERSAL SKILL RANKS - Works for ALL entities (Player, NPCs, Enemies)
// ============================================================================

struct SkillRanks {
    static constexpr std::size_t MAX_SKILLS = 8;  // Reduced from 32 since we have only 3 skills now
    
    struct SkillEntry {
        SkillID id = SkillID::Bodybuilding;
        std::int32_t rank = 0;  // Skill level/rank
    };
    
    SkillEntry skills[MAX_SKILLS]{};
    std::uint8_t count = 0;
    
    // Get rank of a specific skill (0 if not learned)
    [[nodiscard]] std::int32_t get_rank(SkillID id) const noexcept {
        for (std::uint8_t i = 0; i < count; ++i) {
            if (skills[i].id == id)
                return skills[i].rank;
        }
        return 0;
    }
    
    // Add or increase skill rank
    void increase_rank(SkillID id, std::int32_t amount = 1) noexcept {
        // Find existing skill
        for (std::uint8_t i = 0; i < count; ++i) {
            if (skills[i].id == id) {
                skills[i].rank += amount;
                return;
            }
        }
        
        // Add new skill if not found
        if (count < MAX_SKILLS) {
            skills[count].id = id;
            skills[count].rank = amount;
            count++;
        }
    }
    
    // Check if skill is learned (rank > 0)
    [[nodiscard]] bool has_skill(SkillID id) const noexcept {
        return get_rank(id) > 0;
    }
    
    // Get total skill points invested
    [[nodiscard]] std::int32_t total_ranks() const noexcept {
        std::int32_t total = 0;
        for (std::uint8_t i = 0; i < count; ++i) {
            total += skills[i].rank;
        }
        return total;
    }
    
    void clear() noexcept {
        count = 0;
        for (auto& s : skills) {
            s.id = SkillID::Bodybuilding;
            s.rank = 0;
        }
    }
};

// Apply skill bonuses to base stats (called before attribute multipliers)
inline void apply_skill_bonuses(
    std::int32_t& base_hp,
    const SkillRanks& skills
) noexcept {
    base_hp += skills.get_rank(SkillID::Bodybuilding);  // +1 HP per rank
}


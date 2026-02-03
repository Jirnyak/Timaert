#pragma once

#include <algorithm>
#include <cstdint>

// IMPORTANT
// FOR AGENT: MAKE UNIVERSAL - IN SOME CIRCUMSTANCES DIFFERENT ATRRIBUTES COULD AFFECT DIFFERENT
// STATS SO INITIALLY SYSTEM MUST BE ABSTRACT AND UNIVERSAL SO EASY TO ADD PERCS MECHANICS ITEM
// EFFECTS ETC! A = (k_1A_0 + k_2X)*(k_3Y+k_4) - UNIVERSAL FORMULA A - stat, X and Y diferent
// attributes (could be used sum and product of many) UNIVERALITY
//  ============================================================================
//  RPG ATTRIBUTES SYSTEM
//  Following game_design.md specifications
//  ============================================================================

// Primary attributes: STR, INT, CHA, LCK, SPD, AGI, END, WIL, WIS
struct Attributes {
    std::uint8_t str = 1;   // Strength - physical damage +1% per point
    std::uint8_t end_ = 1;  // Endurance - HP regen +1% per point
    std::uint8_t agi = 1;   // Agility - dodge rate +1% per point
    std::uint8_t wil = 1;   // Willpower - mana regen +1% per point
    std::uint8_t int_ = 1;  // Intelligence - spell damage +1%, spell learn requirement
    std::uint8_t wis = 1;   // Wisdom - exp bonus +1% per point
    std::uint8_t lck = 1;   // Luck - better loot, favorable encounters, crit +1%
    std::uint8_t spd = 1;   // Speed - map movement +1%, combat initiative
    std::uint8_t cha = 1;   // Charisma - trade prices +1%, relation bonus 10 per CHA

    // Helper: total points spent
    [[nodiscard]] std::int32_t total() const noexcept {
        return str + end_ + agi + wil + int_ + wis + lck + spd + cha;
    }
};

// ============================================================================
// LEVEL & EXPERIENCE SYSTEM
// ============================================================================

struct LevelData {
    std::int32_t level = 1;
    std::int32_t exp = 0;
    std::int32_t exp_to_next = 1100;  // EXP_next(lvl=1) = 1000 * 1 * (0.1*1 + 1) = 1100

    // Calculate exp required for next level
    // Formula: EXP_next(lvl) = 1000 * lvl * (0.1*lvl + 1)
    [[nodiscard]] static std::int32_t calc_exp_for_level(std::int32_t level) noexcept {
        return 1000 * level * (0.1 * level + 1);
    }

    // Add experience and level up if needed
    // Returns number of levels gained
    std::int32_t add_exp(std::int32_t amount) noexcept {
        exp += amount;
        std::int32_t levels_gained = 0;

        while (exp >= exp_to_next) {
            exp -= exp_to_next;
            level++;
            levels_gained++;
            exp_to_next = calc_exp_for_level(level);
        }

        return levels_gained;
    }

    // Calculate attribute points available at given level
    // Formula: level + 9 (starts with 10 at lvl 1)
    [[nodiscard]] std::int32_t attribute_points_at_level() const noexcept {
        return level + 9;
    }

    // Calculate perk points available at given level
    // Formula: 1 + level/10 (lvl 1 gets 1, lvl 10 gets 2, lvl 20 gets 3, etc.)
    [[nodiscard]] std::int32_t perk_points_at_level() const noexcept {
        return 1 + 0.1 * level;
    }
};

// ============================================================================
// COMBAT STATS (HP, MP) - CALCULATED FROM ATTRIBUTES
// ============================================================================

struct CombatStats {
    std::int32_t max_hp = 100;
    std::int32_t current_hp = 100;
    std::int32_t max_mp = 10;
    std::int32_t current_mp = 10;
    std::int32_t max_sp = 100;
    std::int32_t current_sp = 100;
    std::int32_t base_hp_regen = 1;
    std::int32_t base_mp_regen = 1;
    std::int32_t base_sp_regen = 1;

    // Calculate max HP from attributes and perks
    // Simplified formula from RPG_MECHANICS.md:
    // HP(HP_0, END) = HP_0 * (1 + 0.1 * END)
    // HP_0 = 100 + perk_bonus_hp
    static std::int32_t calc_max_hp(std::int32_t base_hp, const Attributes& attr) noexcept {
        const double multiplier = 1.0 + 0.1 * attr.end_;
        return static_cast<std::int32_t>(base_hp * multiplier);
    }

    // Calculate max MP from attributes and perks
    // Simplified formula from RPG_MECHANICS.md:
    // MP(MP_0, WIL) = MP_0 * (1 + 0.1 * WIL)
    // MP_0 = 10 + perk_bonus_mp
    static std::int32_t calc_max_mp(std::int32_t base_mp, const Attributes& attr) noexcept {
        const double multiplier = 1.0 + 0.1 * attr.wil;
        return static_cast<std::int32_t>(base_mp * multiplier);
    }

    // Calculate max SP from attributes and perks
    // Simplified formula from RPG_MECHANICS.md:
    // SP(SP_0, SPD) = SP_0 * (1 + 0.1 * SPD)
    // SP_0 = 100 + perk_bonus_sp
    static std::int32_t calc_max_sp(std::int32_t base_sp, const Attributes& attr) noexcept {
        const double multiplier = 1.0 + 0.1 * attr.spd;
        return static_cast<std::int32_t>(base_sp * multiplier);
    }

    static std::int32_t calc_regen_hp(std::int32_t base_hp_regen, const Attributes& attr) noexcept {
        const double multiplier = 1.0 + 0.1 * attr.end_;
        return static_cast<std::int32_t>(base_hp_regen * multiplier);
    }

    static std::int32_t calc_regen_mp(std::int32_t base_mp_regen, const Attributes& attr) noexcept {
        const double multiplier = 1.0 + 0.1 * attr.wil;
        return static_cast<std::int32_t>(base_mp_regen * multiplier);
    }

    static std::int32_t calc_regen_sp(std::int32_t base_sp_regen, const Attributes& attr) noexcept {
        const double multiplier = 1.0 + 0.1 * attr.agi;
        return static_cast<std::int32_t>(base_sp_regen * multiplier);
    }

    // Update max values and clamp current values
    void recalculate(std::int32_t base_hp, std::int32_t base_mp, std::int32_t base_sp, const Attributes& attr) noexcept {
        max_hp = calc_max_hp(base_hp, attr);
        max_mp = calc_max_mp(base_mp, attr);
        max_sp = calc_max_sp(base_sp, attr);

        // Clamp current values
        current_hp = std::min(current_hp, max_hp);
        current_mp = std::min(current_mp, max_mp);
        current_sp = std::min(current_sp, max_sp);
    }
};

// ============================================================================
// DERIVED BONUSES FROM ATTRIBUTES
// ============================================================================

struct DerivedBonuses {
    float phys_damage_mult = 1.0f;      // STR: +1% per point
    float carry_weight_mult = 1.0f;     // STR: +1% per point
    float spell_damage_mult = 1.0f;     // INT: +1% per point
    float hp_regen_mult = 1.0f;         // END: +1% per point
    float mp_regen_mult = 1.0f;         // WIL: +1% per point
    float sp_regen_mult = 1.0f;         // AGI: +1% per point
    float exp_mult = 1.0f;              // WIS: +1% per point
    float move_speed_mult = 1.0f;       // SPD: asymptotic formula
    float trade_discount = 0.0f;        // CHA: +1% per point
    std::int32_t relation_bonus = 0;    // CHA: +1 per point

    void recalculate(const Attributes& attr) noexcept {
        phys_damage_mult = 1.0f + attr.str * 0.01f;
        carry_weight_mult = 1.0f + attr.str * 0.01f;
        spell_damage_mult = 1.0f + attr.int_ * 0.01f;
        hp_regen_mult = 1.0f + attr.end_ * 0.01f;
        mp_regen_mult = 1.0f + attr.wil * 0.01f;
        sp_regen_mult = 1.0f + attr.agi * 0.01f;
        exp_mult = 1.0f + attr.wis * 0.01f;
        
        // SPD: movement speed with asymptotic function
        // move_speed_mult = 1.0 + spd / (spd + 50)
        move_speed_mult = 1.0f + static_cast<float>(attr.spd) / (static_cast<float>(attr.spd) + 50.0f);
        
        trade_discount = attr.cha * 0.01f;
        relation_bonus = attr.cha * 1;
    }
};

// ============================================================================
// EXPERIENCE REWARDS
// ============================================================================

// Calculate experience from fight
// EXP_fight(lvl_m, k) = 10 * lvl_m * k
// k = difficulty modifier (boss, elite, etc.)
[[nodiscard]] inline std::int32_t calc_fight_exp(std::int32_t enemy_level,
                                                 float difficulty_mod = 1.0f) noexcept {
    return static_cast<std::int32_t>(10.0f * enemy_level * difficulty_mod);
}

// Calculate experience from quest
// EXP_quest(lvl_q, k) = 100 * lvl_q * k
// k = quest length/difficulty modifier
[[nodiscard]] inline std::int32_t calc_quest_exp(std::int32_t quest_level,
                                                 float difficulty_mod = 1.0f) noexcept {
    return static_cast<std::int32_t>(100.0f * quest_level * difficulty_mod);
}

// ============================================================================
// COMBAT FORMULAS
// ============================================================================

// Calculate dodge chance (defender vs attacker)
// Formula: dodge_chance = agi_defender / (agi_defender + agi_attacker + K)
// K is a balancing constant (default 100 for smoother curve)
[[nodiscard]] inline float calc_dodge_chance(std::int32_t agi_defender, 
                                             std::int32_t agi_attacker,
                                             float K = 100.0f) noexcept {
    const float agi_def = static_cast<float>(agi_defender);
    const float agi_atk = static_cast<float>(agi_attacker);
    return agi_def / (agi_def + agi_atk + K);
}

// Calculate critical hit chance (attacker vs defender)
// Formula: crit_chance = lck_attacker / (lck_attacker + lck_defender + K)
// K is a balancing constant (default 100 for smoother curve)
[[nodiscard]] inline float calc_crit_chance(std::int32_t lck_attacker,
                                            std::int32_t lck_defender,
                                            float K = 100.0f) noexcept {
    const float lck_atk = static_cast<float>(lck_attacker);
    const float lck_def = static_cast<float>(lck_defender);
    return lck_atk / (lck_atk + lck_def + K);
}

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
    std::int32_t base_hp_regen = 1;
    std::int32_t base_mp_regen = 1;

    // Calculate max HP from attributes and perks
    // HP = HP_0 * (0.1*END + 0.05*STR + 0.03*AGI + 0.001*(END*STR + END*AGI + STR*AGI))
    // HP_0 = 100 + perk_bonus_hp
    static std::int32_t calc_max_hp(std::int32_t base_hp, const Attributes& attr) noexcept {
        const double e = attr.end_;
        const double s = attr.str;
        const double a = attr.agi;

        const double multiplier =
            1 + 0.1 * e + 0.05 * s + 0.03 * a + 0.001 * (e * s + e * a + s * a);

        return static_cast<std::int32_t>(base_hp * multiplier);
    }

    // Calculate max MP from attributes and perks
    // MP = MP_0 * (0.1*WIL + 0.05*INT + 0.03*WIS + 0.001*(WIS*INT + WIS*WIL + WIL*INT))
    // MP_0 = 10 + perk_bonus_mp
    static std::int32_t calc_max_mp(std::int32_t base_mp, const Attributes& attr) noexcept {
        const double wil = attr.wil;
        const double i = attr.int_;
        const double wis = attr.wis;

        const double multiplier =
            1 + 0.1 * wil + 0.05 * i + 0.03 * wis + 0.001 * (wil * i + wil * wis + wis * i);

        return static_cast<std::int32_t>(base_mp * multiplier);
    }

    static std::int32_t calc_regen_hp(std::int32_t base_hp_regen, const Attributes& attr) noexcept {
        const double multiplier = 1 + 0.1 * attr.end_;

        return static_cast<std::int32_t>(base_hp_regen * multiplier);
    }

    static std::int32_t calc_regen_mp(std::int32_t base_mp_regen, const Attributes& attr) noexcept {
        const double multiplier = 1 + 0.1 * attr.wil;

        return static_cast<std::int32_t>(base_mp_regen * multiplier);
    }

    // Update max values and clamp current values
    void recalculate(std::int32_t base_hp, std::int32_t base_mp, const Attributes& attr) noexcept {
        max_hp = calc_max_hp(base_hp, attr);
        max_mp = calc_max_mp(base_mp, attr);

        // Clamp current values
        current_hp = std::min(current_hp, max_hp);
        current_mp = std::min(current_mp, max_mp);
    }
};

// ============================================================================
// DERIVED BONUSES FROM ATTRIBUTES
// ============================================================================

struct DerivedBonuses {
    float phys_damage_mult = 1.0f;     // STR: +1% to base dmg per point
    float spell_damage_mult = 1.0f;    // INT: +1% to base dmg per point
    float hp_regen_mult = 1.0f;        // END: +1% to base regen per point
    float mp_regen_mult = 1.0f;        // WIL: +1% to base regen per point
    float dodge_rate = 1.0f;           // AGI: +1% to base dodge per point (capped or asymptotic)
    float exp_mult = 1.0f;             // WIS: +1% to exp gained per point
    float crit_rate = 1.0f;            // LCK: +1% to crit chance per point
    float move_speed_mult = 1.0f;      // SPD: +1% to base move speed per point (asymptotic)
    float trade_discount = 1.0f;       // CHA: +/-1% to base markup per point
    std::int32_t relation_bonus = 10;  // CHA: +10 to any relation per point

    void recalculate(const Attributes& attr) noexcept {
        phys_damage_mult = 1.0f + attr.str * 0.01f;
        spell_damage_mult = 1.0f + attr.int_ * 0.01f;
        hp_regen_mult = 1.0f + attr.end_ * 0.01f;
        mp_regen_mult = 1.0f + attr.wil * 0.01f;

        // AGI: dodge rate with soft cap using asymptotic function
        // dodge = agi / (agi + 100) gives 0.5 (50%) at agi=100, 0.9 (90%) at agi=900
        dodge_rate = static_cast<float>(attr.agi) / (static_cast<float>(attr.agi) + 100.0f);

        exp_mult = 1.0f + attr.wis * 0.01f;
        crit_rate = attr.lck * 0.01f;

        // SPD: movement speed with asymptotic function
        // speed = spd / (spd + 50) gives 0.5 (50%) at spd=50, 0.9 (90%) at spd=450
        move_speed_mult =
            1.0f + static_cast<float>(attr.spd) / (static_cast<float>(attr.spd) + 50.0f);

        trade_discount = attr.cha * 0.01f;
        relation_bonus = attr.cha * 10;
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

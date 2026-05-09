// Faithful port of `src/game/army.ts`.
//
// Universal `CombatTemplate` shared by army units AND NPC type defs.
// Rock-paper-scissors damage matrix. Hire/fire/upkeep/garrison/desertion
// helpers — values match TS exactly (HIRE_COST 10/15/12/25, UPKEEP 1/1/1/2).
#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>

namespace sm {

enum class UnitType : std::uint8_t {
    Swordsman = 0, Archer = 1, Spearman = 2, Horseman = 3,
};
constexpr int kUnitTypeCount = 4;

inline constexpr UnitType kAllUnitTypes[kUnitTypeCount] = {
    UnitType::Swordsman, UnitType::Archer, UnitType::Spearman, UnitType::Horseman,
};

// ── Universal combat stat block (also used by NPC type defs) ───

struct CombatTemplate {
    enum AttackKind : std::uint8_t { Melee = 0, Missile = 1 };
    float       hp;
    float       damage;
    float       speed;        // grid units / s
    float       attackRange;  // grid units
    float       cooldown;     // seconds between attacks
    const char* label;
    AttackKind  attackKind = Melee;
    float       missileSpeed = 0;  // px/s when attackKind == Missile
    float       missileBlast = 0;  // 0 = single target
    std::uint32_t missileColorRGBA = 0xFFFFFFFFu;
};

// Values verbatim from UNIT_STATS. Archer is the only ranged unit.
inline constexpr CombatTemplate kUnitStats[kUnitTypeCount] = {
    /*Swordsman*/ {100, 15, 40,  3, 1.0f, "Swd", CombatTemplate::Melee,   0,   0, 0xFFFFFFFFu},
    /*Archer   */ { 50, 12, 35, 30, 1.5f, "Arc", CombatTemplate::Missile, 220, 0, 0xFFE8D070u},
    /*Spearman */ { 80, 12, 35,  4, 1.2f, "Spr", CombatTemplate::Melee,   0,   0, 0xFFFFFFFFu},
    /*Horseman */ { 90, 18, 80,  3, 0.8f, "Hrs", CombatTemplate::Melee,   0,   0, 0xFFFFFFFFu},
};

// ── Composition ────────────────────────────────────────────────

struct ArmyComposition {
    std::array<int, kUnitTypeCount> counts{};
    int  get(UnitType t) const { return counts[std::size_t(t)]; }
    void set(UnitType t, int v) { counts[std::size_t(t)] = v; }
    void add(UnitType t, int v) { counts[std::size_t(t)] += v; }
};

inline ArmyComposition default_army() { return {}; }

inline int total_units(const ArmyComposition& a) {
    int s = 0;
    for (int c : a.counts) s += c;
    return s;
}

// ── RPS damage matrix ──────────────────────────────────────────
//
//   Sword  → 1.5× Archer        (closes gap, shield blocks arrows)
//   Archer → 1.5× Spearman      (kite at range)
//   Spear  → 1.8× Horseman      (brace against charge)
//   Horse  → 1.4× Swordsman     (charge & mobility)
inline float damage_multiplier(UnitType atk, UnitType def) {
    switch (atk) {
        case UnitType::Swordsman: return def == UnitType::Archer    ? 1.5f : 1.0f;
        case UnitType::Archer:    return def == UnitType::Spearman  ? 1.5f : 1.0f;
        case UnitType::Spearman:  return def == UnitType::Horseman  ? 1.8f : 1.0f;
        case UnitType::Horseman:  return def == UnitType::Swordsman ? 1.4f : 1.0f;
    }
    return 1.0f;
}

// ── Garrison / hire / upkeep (values exact from TS) ────────────

inline constexpr int kHireCost  [kUnitTypeCount] = {10, 15, 12, 25};
inline constexpr int kUpkeepCost[kUnitTypeCount] = { 1,  1,  1,  2};

inline int hire_cost  (UnitType t) { return kHireCost  [std::size_t(t)]; }
inline int upkeep_cost(UnitType t) { return kUpkeepCost[std::size_t(t)]; }

// Each CHA point = -1% upkeep linearly.
inline int calculate_army_upkeep(const ArmyComposition& a, int charisma = 0) {
    int base = 0;
    for (auto t : kAllUnitTypes) base += a.get(t) * upkeep_cost(t);
    return int(float(base) * (1.0f - float(charisma) * 0.01f));
}

// Generate garrison from settlement population.
// Returns garrison + popCost (subtracted from population by caller).
struct GarrisonResult { ArmyComposition garrison; int popCost; };

inline GarrisonResult generate_garrison(int population,
                                        const std::function<float()>& rng) {
    GarrisonResult r{};
    if (population < 20) return r;
    int budget = int(std::sqrt(float(population)) * 0.3f);
    if (budget > 10) budget = 10;
    if (budget <= 0) return r;
    for (int i = 0; i < budget; ++i) {
        const float roll = rng();
        UnitType ut;
        if      (roll < 0.45f) ut = UnitType::Swordsman;
        else if (roll < 0.70f) ut = UnitType::Archer;
        else if (roll < 0.90f) ut = UnitType::Spearman;
        else                   ut = UnitType::Horseman;
        r.garrison.add(ut, 1);
        r.popCost += 1;
    }
    return r;
}

// Ensure NPC has at least a minimal army derived from level.
inline ArmyComposition ensure_army(const ArmyComposition& existing, int level) {
    if (total_units(existing) > 0) return existing;
    ArmyComposition a;
    int sw = int(float(level) * 0.5f); if (sw < 1) sw = 1;
    int ar = int(float(level) * 0.3f); if (ar < 0) ar = 0;
    int sp = int(float(level) * 0.2f); if (sp < 0) sp = 0;
    a.set(UnitType::Swordsman, sw);
    a.set(UnitType::Archer,    ar);
    a.set(UnitType::Spearman,  sp);
    return a;
}

inline void add_army(ArmyComposition& target, const ArmyComposition& src) {
    for (auto t : kAllUnitTypes) target.add(t, src.get(t));
}

// Hire one unit from a settlement garrison. Returns gold cost or 0.
// Population is NOT changed here — already paid when garrison was generated.
inline int hire_unit(ArmyComposition& playerArmy, ArmyComposition& garrison,
                     UnitType ut, int playerGold) {
    const int cost = hire_cost(ut);
    if (garrison.get(ut) <= 0 || playerGold < cost) return 0;
    garrison.add(ut, -1);
    playerArmy.add(ut,  1);
    return cost;
}

// Fire one unit into the deserter pool. Returns true if fired.
inline bool fire_unit(ArmyComposition& playerArmy, ArmyComposition& deserterPool,
                      UnitType ut) {
    if (playerArmy.get(ut) <= 0) return false;
    playerArmy.add  (ut, -1);
    deserterPool.add(ut,  1);
    return true;
}

// Drain the deserter pool, returning total count and resetting pool to 0.
inline int drain_deserter_pool(ArmyComposition& pool) {
    int total = 0;
    for (auto t : kAllUnitTypes) {
        total += pool.get(t);
        pool.set(t, 0);
    }
    return total;
}

} // namespace sm

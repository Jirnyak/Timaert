#pragma once

#include <cstdint>

enum class GameMode : std::uint8_t
{
    Gen,
    Exit,
    Game,
    Menu,
    Stat,
    Map,
    Load,
    Labyrinth,
    Event,
    Fight,
    Pause,
    Settings
};

enum class TerrainType : std::uint8_t
{
    Nothing,
    Sand,
    Grass,
    Dirt,
    Mount,
    Water,
    Snow,
    Jungle,
    Swamp,
    Tundra,
    Count
};

enum class Direction : std::int8_t
{
    Up = 0,
    Left = 1,
    Down = 2,
    Right = 3
};

enum class Gender : std::uint8_t
{
    Male = 0,
    Female = 1,
    Futanari = 2,
    Count
};

enum class Race : std::uint8_t
{
    Human = 0,
    Elf = 1,
    Orc = 2,
    Goblin = 3,
    Slime = 4,
    Demon = 5,
    Count
};

enum class FactionID : std::uint8_t
{
    Neutral = 0,
    Faction1,
    Faction2,
    Faction3,
    Faction4,
    Faction5,
    Faction6,
    Faction7,
    Wilderness,
    Count
};

enum class NPCType : std::uint8_t
{
    None = 0,
    Peasant,
    Woodcutter,
    Merchant,
    Caravan,
    Bandit,
    Guard,
    Witch,
    Count
};

[[nodiscard]] inline const char* npc_type_name(NPCType type) noexcept {
    switch (type) {
        case NPCType::Peasant: return "Peasant";
        case NPCType::Woodcutter: return "Woodcutter";
        case NPCType::Merchant: return "Merchant";
        case NPCType::Caravan: return "Caravan";
        case NPCType::Bandit: return "Bandit";
        case NPCType::Guard: return "Guard";
        case NPCType::Witch: return "Witch";
        default: return "NPC";
    }
}

enum class NPCState : std::uint8_t
{
    Idle,
    Wandering,
    Traveling,
    Trading,
    Returning,
    Fleeing,
    Raiding,
    Cutting,
    Dead
};

enum class EntityState : std::uint8_t
{
    Default = 0
};

enum class ObjectType : std::uint8_t
{
    City = 0,
    Tree = 1,
    Band = 2,
    Village = 3,
    Town = 4,
    Player = 5,
    Peasant = 6,
    Woodcutter = 7,
    Merchant = 8,
    Caravan = 9,
    Bandit = 10,
    Guard = 11,
    Door = 12,
    Witch = 13,
    Count
};

enum class PlayerState : std::uint8_t
{
    Normal = 0,
    InSettlement,
    Trading,
    InCombat,
    Dead
};

enum class ResourceType : std::uint8_t
{
    None = 0,
    Grain,
    Wood,
    Stone,
    Iron,
    Cloth,
    Salt,
    Wine,
    Spices,
    Count
};

enum class ItemType : std::uint8_t
{
    Coins = 0,
    IronSword,
    WoodenShield,
    LeatherArmor,
    ClothDress,
    BanditMask,
    Count
};

enum class SkillType : std::uint8_t
{
    Physical = 0,
    Magic,
    Lust,
    Heal,
    Support,
    Utility
};

[[nodiscard]] constexpr bool is_hostile(FactionID a, FactionID b) noexcept
{
    return (a == FactionID::Faction1 && b == FactionID::Faction2) ||
           (a == FactionID::Faction2 && b == FactionID::Faction1);
}

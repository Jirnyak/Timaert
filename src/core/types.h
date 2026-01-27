#pragma once

#include <cstdint>

enum class GameMode : std::uint8_t {
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
    Settings,
    Interaction
};

enum class TerrainType : std::uint8_t {
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

enum class Direction : std::int8_t {
    Up,
    Left,
    Down,
    Right
};

enum class Gender : std::uint8_t {
    Male,
    Female,
    Futanari,
    Count
};

enum class Race : std::uint8_t {
    Human,
    Elf,
    Orc,
    Goblin,
    Slime,
    Demon,
    Count
};

enum class FactionID : std::uint8_t {
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

enum class NPCType : std::uint8_t {
    None = 0,
    Peasant = 1,
    Woodcutter = 2,
    Merchant = 3,
    Caravan = 4,
    Bandit = 5,
    Guard = 6,
    Witch = 7,
};

[[nodiscard]] inline const char* npc_type_name(NPCType type) noexcept {
    switch (type) {
        case NPCType::Peasant:
            return "Peasant";
        case NPCType::Woodcutter:
            return "Woodcutter";
        case NPCType::Merchant:
            return "Merchant";
        case NPCType::Caravan:
            return "Caravan";
        case NPCType::Bandit:
            return "Bandit";
        case NPCType::Guard:
            return "Guard";
        case NPCType::Witch:
            return "Witch";
        default:
            return "NPC";
    }
}

enum class NPCState : std::uint8_t {
    Idle = 0,
    Wandering = 1,
    Traveling = 2,
    Trading = 3,
    Returning = 4,
    Fleeing = 5,
    Raiding = 6,
    Cutting = 7,
    Dead = 8
};

enum class EntityState : std::uint8_t { Default = 0 };

enum class ObjectType : std::uint8_t {
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
    Count = 14,
};

enum class PlayerState : std::uint8_t { Normal = 0, InSettlement, Trading, InCombat, Dead };

enum class ResourceType : std::uint8_t {
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

enum class ItemType : std::uint8_t {
    Coins = 0,

    // --- WEAPONS: One-Handed ---
    RustyDagger,
    IronDagger,
    SteelDagger,
    AssassinBlade,
    ShortSword,
    IronSword,
    SteelSword,
    Falchion,
    Rapier,
    HandAxe,
    BattleAxe,
    Mace,
    Flail,
    Warhammer,

    // --- WEAPONS: Two-Handed ---
    GreatSword,
    Claymore,
    Zweihander,
    ExecutionerSword,
    DoubleAxe,
    GreatHammer,
    Maul,
    Spear,
    Pike,
    Halberd,
    Trident,

    // --- WEAPONS: Ranged & Magic ---
    ShortBow,
    LongBow,
    CompoundBow,
    Crossbow,
    HeavyCrossbow,
    MagicWand,
    ApprenticeStaff,
    ArchmageStaff,
    RitualKnife,

    // --- SHIELDS ---
    Buckler,
    WoodenShield,
    IronShield,
    KiteShield,
    TowerShield,

    // --- ARMOR: Head ---
    ClothHood,
    LeatherCap,
    IronHelmet,
    KnightHelmet,
    FullPlateHelm,
    Circlet,
    WizardHat,
    BanditMask,

    // --- ARMOR: Body ---
    Rags,
    PeasantClothes,
    TravelerCloak,
    NobleDress,
    ClothTunic,
    QuiltedArmor,
    LeatherArmor,
    HardenedLeather,
    StuddedLeather,
    Chainmail,
    ScaleArmor,
    Breastplate,
    FullPlateArmor,

    // --- ARMOR: Legs & Feet ---
    Sandals,
    LeatherBoots,
    ArmoredBoots,
    Greaves,
    ClothTrousers,
    LeatherPants,

    // --- ACCESSORIES ---
    CopperRing,
    SilverRing,
    GoldRing,
    AmuletOfWill,
    AmuletOfLust,
    LeatherBelt,
    SilkScarf,

    // --- CONSUMABLES: Potions & Food ---
    HealthPotionSmall,
    HealthPotionBig,
    ManaPotionSmall,
    ManaPotionBig,
    LustPotion,
    Aphrodisiac,
    Bread,
    DriedMeat,
    RoastedChicken,
    Apple,
    Berry,
    WineBottle,
    BeerMug,
    ClearWater,

    // --- MATERIALS ---
    IronOre,
    SteelIngot,
    GoldNugget,
    WoodLogs,
    Planks,
    LeatherScraps,
    ThickHide,
    ClothRoll,
    SilkFabric,
    MagicDust,
    AncientRune,

    // --- EROTIC & SPECIAL (Theater/18+) ---
    SilkStockings,
    LacePanties,
    TransparentDress,
    LeatherCollar,
    SlaveChain,
    SteelCuffs,
    SmallGag,
    BallGag,
    Blindfold,
    SmallWhip,
    RidingCrop,
    LustOil,
    SecretVibratingStone,
    MaidOutfit,
    BunnyEars,

    // --- MISC ---
    Lockpick,
    Torch,
    OldMap,
    MysteryBox,
    BrokenKey,

    Count
};

enum class SkillType : std::uint8_t {
    Physical = 0,
    Magic,
    Lust,
    Heal,
    Support,
    Utility
};

[[nodiscard]] constexpr bool is_hostile(FactionID a, FactionID b) noexcept {
    return (a == FactionID::Faction1 && b == FactionID::Faction2)
           || (a == FactionID::Faction2 && b == FactionID::Faction1);
}

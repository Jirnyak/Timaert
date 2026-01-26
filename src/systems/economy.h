#pragma once

#include <cstdint>
#include <array>
#include "core/game_context.h"

inline constexpr std::size_t RESOURCE_COUNT = static_cast<std::size_t>(ResourceType::Count);

struct ResourceInfo {
    const char* name;
    std::int32_t base_price;
    std::int32_t weight;
};

inline constexpr std::array<ResourceInfo, RESOURCE_COUNT> RESOURCE_DATA = {{{"Nothing", 0, 0},
                                                                            {"Grain", 5, 1},
                                                                            {"Wood", 10, 3},
                                                                            {"Stone", 15, 5},
                                                                            {"Iron", 50, 4},
                                                                            {"Cloth", 30, 1},
                                                                            {"Salt", 25, 2},
                                                                            {"Wine", 40, 2},
                                                                            {"Spices", 100, 1}}};

[[nodiscard]] inline const char* resource_name(ResourceType r) noexcept {
    return RESOURCE_DATA[static_cast<std::size_t>(r)].name;
}

[[nodiscard]] inline std::int32_t resource_base_price(ResourceType r) noexcept {
    return RESOURCE_DATA[static_cast<std::size_t>(r)].base_price;
}

[[nodiscard]] inline std::int32_t resource_weight(ResourceType r) noexcept {
    return RESOURCE_DATA[static_cast<std::size_t>(r)].weight;
}
struct ItemMetadata {
    const char* name;
    std::int32_t base_price;
    RaIcon icon;
};

inline constexpr std::array<ItemMetadata, static_cast<size_t>(ItemType::Count)> ITEM_DATABASE = {
    {{"Gold Coins", 1, RaIcon::GoldBar},

     {"Rusty Dagger", 15, RaIcon::PlainDagger},
     {"Iron Dagger", 40, RaIcon::PlainDagger},
     {"Steel Dagger", 90, RaIcon::PlainDagger},
     {"Assassin Blade", 250, RaIcon::DrippingBlade},
     {"Short Sword", 70, RaIcon::Sword},
     {"Iron Sword", 150, RaIcon::Sword},
     {"Steel Sword", 350, RaIcon::Sword},
     {"Falchion", 400, RaIcon::Sword},
     {"Rapier", 450, RaIcon::RelicBlade},
     {"Hand Axe", 60, RaIcon::Axe},
     {"Battle Axe", 180, RaIcon::Axe},
     {"Mace", 140, RaIcon::Hammer},
     {"Flail", 220, RaIcon::SpikedMace},
     {"Warhammer", 320, RaIcon::LargeHammer},

     {"Great Sword", 550, RaIcon::CrossedSwords},
     {"Claymore", 700, RaIcon::CrossedSwords},
     {"Zweihander", 950, RaIcon::CrossedSwords},
     {"Executioner Sword", 1200, RaIcon::SkullTrophy},
     {"Double Axe", 650, RaIcon::CrossedAxes},
     {"Great Hammer", 600, RaIcon::LargeHammer},
     {"Maul", 800, RaIcon::LargeHammer},
     {"Spear", 100, RaIcon::Trident},
     {"Pike", 150, RaIcon::Trident},
     {"Halberd", 300, RaIcon::Halberd},
     {"Trident", 450, RaIcon::Trident},

     {"Short Bow", 80, RaIcon::ArcheryTarget},
     {"Long Bow", 200, RaIcon::ArcheryTarget},
     {"Compound Bow", 500, RaIcon::ArcheryTarget},
     {"Crossbow", 350, RaIcon::Crossbow},
     {"Heavy Crossbow", 650, RaIcon::Crossbow},
     {"Magic Wand", 150, RaIcon::CrystalWand},
     {"Apprentice Staff", 250, RaIcon::CrystalWand},
     {"Archmage Staff", 1500, RaIcon::FairyWand},
     {"Ritual Knife", 400, RaIcon::DrippingKnife},

     {"Buckler", 50, RaIcon::CircularShield},
     {"Wooden Shield", 80, RaIcon::ZebraShield},
     {"Iron Shield", 200, RaIcon::RoundShield},
     {"Kite Shield", 450, RaIcon::Shield},
     {"Tower Shield", 800, RaIcon::HeavyShield},

     {"Cloth Hood", 20, RaIcon::Hood},
     {"Leather Cap", 45, RaIcon::Helmet},
     {"Iron Helmet", 120, RaIcon::Helmet},
     {"Knight Helmet", 300, RaIcon::KnightHelmet},
     {"Full Plate Helm", 650, RaIcon::KnightHelmet},
     {"Circlet", 500, RaIcon::Crown},
     {"Wizard Hat", 150, RaIcon::Hood},
     {"Bandit Mask", 25, RaIcon::ArcaneMask},

     {"Rags", 2, RaIcon::VineWhip},
     {"Peasant Clothes", 10, RaIcon::Flower},
     {"Traveler Cloak", 50, RaIcon::Hood},
     {"Noble Dress", 800, RaIcon::GemPendant},
     {"Cloth Tunic", 30, RaIcon::Flower},
     {"Quilted Armor", 90, RaIcon::Vest},
     {"Leather Armor", 200, RaIcon::Vest},
     {"Hardened Leather", 350, RaIcon::Vest},
     {"Studded Leather", 500, RaIcon::Vest},
     {"Chainmail", 850, RaIcon::ZebraShield},
     {"Scale Armor", 1200, RaIcon::ZebraShield},
     {"Breastplate", 2000, RaIcon::Vest},
     {"Full Plate Armor", 5000, RaIcon::HeavyShield},

     {"Sandals", 5, RaIcon::ShoePrints},
     {"Leather Boots", 40, RaIcon::ShoePrints},
     {"Armored Boots", 200, RaIcon::BootStomp},
     {"Greaves", 400, RaIcon::BootStomp},
     {"Cloth Trousers", 15, RaIcon::Flower},
     {"Leather Pants", 80, RaIcon::Vest},

     {"Copper Ring", 50, RaIcon::Cycle},
     {"Silver Ring", 150, RaIcon::Cycle},
     {"Gold Ring", 500, RaIcon::Cycle},
     {"Amulet of Will", 1000, RaIcon::GemPendant},
     {"Amulet of Lust", 1000, RaIcon::HeartBottle},
     {"Leather Belt", 30, RaIcon::VineWhip},
     {"Silk Scarf", 120, RaIcon::Flower},

     {"Health Potion (S)", 50, RaIcon::HealthIncrease},
     {"Health Potion (B)", 200, RaIcon::HealthIncrease},
     {"Mana Potion (S)", 50, RaIcon::BottledBolt},
     {"Mana Potion (B)", 200, RaIcon::BottledBolt},
     {"Lust Potion", 150, RaIcon::BubblingPotion},
     {"Aphrodisiac", 500, RaIcon::HeartBottle},
     {"Bread", 5, RaIcon::Cheese},
     {"Dried Meat", 15, RaIcon::Meat},
     {"Roasted Chicken", 40, RaIcon::RoastChicken},
     {"Apple", 2, RaIcon::Apple},
     {"Berry", 1, RaIcon::Acorn},
     {"Wine Bottle", 35, RaIcon::BrandyBottle},
     {"Beer Mug", 10, RaIcon::Beer},
     {"Clear Water", 1, RaIcon::Droplet},

     {"Iron Ore", 20, RaIcon::Cog},
     {"Steel Ingot", 60, RaIcon::CogWheel},
     {"Gold Nugget", 250, RaIcon::GoldBar},
     {"Wood Logs", 10, RaIcon::Acorn},
     {"Planks", 25, RaIcon::Hammer},
     {"Leather Scraps", 15, RaIcon::VineWhip},
     {"Thick Hide", 40, RaIcon::AlligatorClip},
     {"Cloth Roll", 20, RaIcon::Flower},
     {"Silk Fabric", 150, RaIcon::Flower},
     {"Magic Dust", 300, RaIcon::FairyWand},
     {"Ancient Rune", 1000, RaIcon::RuneStone},

     {"Silk Stockings", 150, RaIcon::Hearts},
     {"Lace Panties", 120, RaIcon::Hearts},
     {"Transparent Dress", 450, RaIcon::Hearts},
     {"Leather Collar", 100, RaIcon::VineWhip},
     {"Slave Chain", 200, RaIcon::Chain},
     {"Steel Cuffs", 150, RaIcon::Chain},
     {"Small Gag", 80, RaIcon::PlayerPain},
     {"Ball Gag", 150, RaIcon::PlayerPain},
     {"Blindfold", 60, RaIcon::Aware},
     {"Small Whip", 120, RaIcon::VineWhip},
     {"Riding Crop", 200, RaIcon::VineWhip},
     {"Lust Oil", 180, RaIcon::Droplets},
     {"Vibrating Stone", 400, RaIcon::CrystalCluster},
     {"Maid Outfit", 600, RaIcon::Flower},
     {"Bunny Ears", 100, RaIcon::Fox},

     {"Lockpick", 20, RaIcon::KeyBasic},
     {"Torch", 10, RaIcon::Torch},
     {"Old Map", 50, RaIcon::ScrollUnfurled},
     {"Mystery Box", 100, RaIcon::Cubes},
     {"Broken Key", 5, RaIcon::Key}}};
struct Inventory {
    static constexpr std::size_t CAPACITY = 256 * 2;  // 512 slots in 16x16 grid
    static constexpr std::size_t COINS_SLOT = 0;      // Gold/coins always in slot 0
    std::array<std::uint16_t, CAPACITY> items{};
    std::array<ItemType, CAPACITY> item_types{};  // Track item type for each slot

    constexpr Inventory() noexcept {
        items.fill(0);
        item_types.fill(ItemType::Coins);  // Default to coins
    }

    // Capital is now derived from coins in slot 0
    [[nodiscard]] double get_capital() const noexcept {
        return static_cast<double>(items[COINS_SLOT]);
    }

    void set_capital(double value) noexcept {
        items[COINS_SLOT] =
            static_cast<std::uint16_t>(value > 65535 ? 65535 : (value < 0 ? 0 : value));
        item_types[COINS_SLOT] = ItemType::Coins;
    }

    void add_capital(double value) noexcept {
        double new_val = get_capital() + value;
        set_capital(new_val);
    }

    void remove_capital(double value) noexcept {
        double new_val = get_capital() - value;
        set_capital(new_val < 0 ? 0 : new_val);
    }

    [[nodiscard]] std::uint16_t get_at(std::size_t index) const noexcept {
        return index < CAPACITY ? items[index] : 0;
    }

    [[nodiscard]] ItemType get_item_type_at(std::size_t index) const noexcept {
        return index < CAPACITY ? item_types[index] : ItemType::Coins;
    }

    void set_at(std::size_t index, std::uint16_t value, ItemType type = ItemType::Coins) noexcept {
        if (index < CAPACITY) {
            items[index] = value;
            item_types[index] = type;
        }
    }

    // Resource-based compatibility methods for existing code
    [[nodiscard]] std::int32_t get(ResourceType r) const noexcept {
        return static_cast<std::int32_t>(get_at(static_cast<std::size_t>(r)));
    }

    void set(ResourceType r, std::int32_t amount) noexcept {
        set_at(static_cast<std::size_t>(r), static_cast<std::uint16_t>(amount & 0xFFFF));
    }

    bool add(ResourceType r, std::int32_t amount) noexcept {
        const std::size_t idx = static_cast<std::size_t>(r);
        const std::uint32_t new_val =
            static_cast<std::uint32_t>(items[idx]) + static_cast<std::uint32_t>(amount);
        if (new_val > 65535)
            return false;  // Overflow check for uint16
        items[idx] = static_cast<std::uint16_t>(new_val);
        return true;
    }

    bool add(ItemType type, std::int32_t amount) noexcept {
        // Ищем пустой слот или слот с таким же предметом
        for (std::size_t i = 1; i < CAPACITY; ++i) {  // Начинаем с 1, чтобы не трогать золото
            if (item_types[i] == type) {
                const std::uint32_t new_val =
                    static_cast<std::uint32_t>(items[i]) + static_cast<std::uint32_t>(amount);
                if (new_val <= 65535) {
                    items[i] = static_cast<std::uint16_t>(new_val);
                    return true;
                }
            }
        }
        // Ищем пустой слот
        for (std::size_t i = 1; i < CAPACITY; ++i) {
            if (items[i] == 0) {
                item_types[i] = type;
                items[i] = static_cast<std::uint16_t>(amount);
                return true;
            }
        }
        return false;  // Нет места
    }

    bool remove(ResourceType r, std::int32_t amount) noexcept {
        const std::size_t idx = static_cast<std::size_t>(r);
        if (static_cast<std::int32_t>(items[idx]) < amount)
            return false;
        items[idx] -= static_cast<std::uint16_t>(amount);
        return true;
    }

    bool can_add(ResourceType r, std::int32_t amount) const noexcept {
        const std::size_t idx = static_cast<std::size_t>(r);
        return static_cast<std::uint32_t>(items[idx]) + static_cast<std::uint32_t>(amount) <= 65535;
    }

    [[nodiscard]] std::int32_t total_items() const noexcept {
        std::int32_t total = 0;
        for (std::size_t i = 0; i < CAPACITY; ++i) {
            total += items[i];
        }
        return total;
    }

    void clear() noexcept {
        items.fill(0);
    }
};

struct MarketPrices {
    std::array<double, RESOURCE_COUNT> prices{};
    std::array<double, RESOURCE_COUNT> supply{};
    std::array<double, RESOURCE_COUNT> demand{};

    constexpr MarketPrices() noexcept {
        for (std::size_t i = 0; i < RESOURCE_COUNT; ++i) {
            prices[i] = static_cast<double>(RESOURCE_DATA[i].base_price);
            supply[i] = 100.0;
            demand[i] = 100.0;
        }
    }

    void update_prices() noexcept {
        for (std::size_t i = 1; i < RESOURCE_COUNT; ++i) {
            const double base = static_cast<double>(RESOURCE_DATA[i].base_price);
            const double ratio = (demand[i] + 1.0) / (supply[i] + 1.0);
            prices[i] = base * ratio;

            if (prices[i] < base * 0.25)
                prices[i] = base * 0.25;
            if (prices[i] > base * 4.0)
                prices[i] = base * 4.0;
        }
    }

    [[nodiscard]] double buy_price(ResourceType r) const noexcept {
        return prices[static_cast<std::size_t>(r)] * 1.1;
    }

    [[nodiscard]] double sell_price(ResourceType r) const noexcept {
        return prices[static_cast<std::size_t>(r)] * 0.9;
    }

    void record_sale(ResourceType r, std::int32_t amount) noexcept {
        supply[static_cast<std::size_t>(r)] += amount;
    }

    void record_purchase(ResourceType r, std::int32_t amount) noexcept {
        demand[static_cast<std::size_t>(r)] += amount;
        supply[static_cast<std::size_t>(r)] -= amount * 0.5;
        if (supply[static_cast<std::size_t>(r)] < 1.0)
            supply[static_cast<std::size_t>(r)] = 1.0;
    }

    void decay() noexcept {
        for (std::size_t i = 1; i < RESOURCE_COUNT; ++i) {
            supply[i] = supply[i] * 0.99 + 50.0 * 0.01;
            demand[i] = demand[i] * 0.99 + 50.0 * 0.01;
        }
    }
};

[[nodiscard]] inline ResourceType get_local_resource(TilePosition pos, rng_t& rng) {
    const std::uint32_t roll = random_u32_inclusive(rng, 100);

    const int x = static_cast<int>(pos.x);
    const int y = static_cast<int>(pos.y);
    const std::uint32_t region_hash = static_cast<std::uint32_t>((x / 64) * 17 + (y / 64) * 31);

    const std::uint32_t combined = (roll + region_hash) % 100;

    if (combined < 30)
        return ResourceType::Grain;
    if (combined < 50)
        return ResourceType::Wood;
    if (combined < 65)
        return ResourceType::Stone;
    if (combined < 75)
        return ResourceType::Iron;
    if (combined < 85)
        return ResourceType::Cloth;
    if (combined < 92)
        return ResourceType::Salt;
    if (combined < 97)
        return ResourceType::Wine;
    return ResourceType::Spices;
}

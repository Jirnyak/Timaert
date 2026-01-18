#pragma once

#include <cstdint>
#include <array>
#include "game_context.h"

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

inline constexpr std::size_t RESOURCE_COUNT = static_cast<std::size_t>(ResourceType::Count);

struct ResourceInfo
{
    const char* name;
    std::int32_t base_price;
    std::int32_t weight;
};

inline constexpr std::array<ResourceInfo, RESOURCE_COUNT> RESOURCE_DATA = {{
    {"Nothing", 0, 0},
    {"Grain", 5, 1},
    {"Wood", 10, 3},
    {"Stone", 15, 5},
    {"Iron", 50, 4},
    {"Cloth", 30, 1},
    {"Salt", 25, 2},
    {"Wine", 40, 2},
    {"Spices", 100, 1}
}};

[[nodiscard]] inline const char* resource_name(ResourceType r) noexcept
{
    return RESOURCE_DATA[static_cast<std::size_t>(r)].name;
}

[[nodiscard]] inline std::int32_t resource_base_price(ResourceType r) noexcept
{
    return RESOURCE_DATA[static_cast<std::size_t>(r)].base_price;
}

[[nodiscard]] inline std::int32_t resource_weight(ResourceType r) noexcept
{
    return RESOURCE_DATA[static_cast<std::size_t>(r)].weight;
}

struct Inventory
{
    std::array<std::int32_t, RESOURCE_COUNT> stock{};
    double capital = 0.0;
    std::int32_t max_capacity = 100;
    
    constexpr Inventory() noexcept
    {
        stock.fill(0);
    }
    
    [[nodiscard]] std::int32_t total_weight() const noexcept
    {
        std::int32_t total = 0;
        for (std::size_t i = 0; i < RESOURCE_COUNT; ++i)
        {
            total += stock[i] * RESOURCE_DATA[i].weight;
        }
        return total;
    }
    
    [[nodiscard]] std::int32_t available_capacity() const noexcept
    {
        return max_capacity - total_weight();
    }
    
    [[nodiscard]] bool can_add(ResourceType r, std::int32_t amount) const noexcept
    {
        const std::int32_t weight = resource_weight(r) * amount;
        return weight <= available_capacity();
    }
    
    bool add(ResourceType r, std::int32_t amount) noexcept
    {
        if (!can_add(r, amount)) return false;
        stock[static_cast<std::size_t>(r)] += amount;
        return true;
    }
    
    bool remove(ResourceType r, std::int32_t amount) noexcept
    {
        const std::size_t idx = static_cast<std::size_t>(r);
        if (stock[idx] < amount) return false;
        stock[idx] -= amount;
        return true;
    }
    
    [[nodiscard]] std::int32_t get(ResourceType r) const noexcept
    {
        return stock[static_cast<std::size_t>(r)];
    }
    
    void set(ResourceType r, std::int32_t amount) noexcept
    {
        stock[static_cast<std::size_t>(r)] = amount;
    }
    
    [[nodiscard]] std::int32_t total_value() const noexcept
    {
        std::int32_t total = 0;
        for (std::size_t i = 1; i < RESOURCE_COUNT; ++i)
        {
            total += stock[i] * RESOURCE_DATA[i].base_price;
        }
        return total;
    }
    
    [[nodiscard]] std::int32_t total_items() const noexcept
    {
        std::int32_t total = 0;
        for (std::size_t i = 1; i < RESOURCE_COUNT; ++i)
        {
            total += stock[i];
        }
        return total;
    }
    
    void clear() noexcept
    {
        stock.fill(0);
    }
};

struct MarketPrices
{
    std::array<double, RESOURCE_COUNT> prices{};
    std::array<double, RESOURCE_COUNT> supply{};
    std::array<double, RESOURCE_COUNT> demand{};
    
    constexpr MarketPrices() noexcept
    {
        for (std::size_t i = 0; i < RESOURCE_COUNT; ++i)
        {
            prices[i] = static_cast<double>(RESOURCE_DATA[i].base_price);
            supply[i] = 100.0;
            demand[i] = 100.0;
        }
    }
    
    void update_prices() noexcept
    {
        for (std::size_t i = 1; i < RESOURCE_COUNT; ++i)
        {
            const double base = static_cast<double>(RESOURCE_DATA[i].base_price);
            const double ratio = (demand[i] + 1.0) / (supply[i] + 1.0);
            prices[i] = base * ratio;
            
            if (prices[i] < base * 0.25) prices[i] = base * 0.25;
            if (prices[i] > base * 4.0) prices[i] = base * 4.0;
        }
    }
    
    [[nodiscard]] double buy_price(ResourceType r) const noexcept
    {
        return prices[static_cast<std::size_t>(r)] * 1.1;
    }
    
    [[nodiscard]] double sell_price(ResourceType r) const noexcept
    {
        return prices[static_cast<std::size_t>(r)] * 0.9;
    }
    
    void record_sale(ResourceType r, std::int32_t amount) noexcept
    {
        supply[static_cast<std::size_t>(r)] += amount;
    }
    
    void record_purchase(ResourceType r, std::int32_t amount) noexcept
    {
        demand[static_cast<std::size_t>(r)] += amount;
        supply[static_cast<std::size_t>(r)] -= amount * 0.5;
        if (supply[static_cast<std::size_t>(r)] < 1.0)
            supply[static_cast<std::size_t>(r)] = 1.0;
    }
    
    void decay() noexcept
    {
        for (std::size_t i = 1; i < RESOURCE_COUNT; ++i)
        {
            supply[i] = supply[i] * 0.99 + 50.0 * 0.01;
            demand[i] = demand[i] * 0.99 + 50.0 * 0.01;
        }
    }
};

[[nodiscard]] inline ResourceType get_local_resource(int pos, rng_t& rng)
{
    const std::uint32_t roll = random_u32_inclusive(rng, 100);
    
    const int x = pos / WORLD_WIDTH;
    const int y = pos % WORLD_WIDTH;
    const std::uint32_t region_hash = static_cast<std::uint32_t>((x / 64) * 17 + (y / 64) * 31);
    
    const std::uint32_t combined = (roll + region_hash) % 100;
    
    if (combined < 30) return ResourceType::Grain;
    if (combined < 50) return ResourceType::Wood;
    if (combined < 65) return ResourceType::Stone;
    if (combined < 75) return ResourceType::Iron;
    if (combined < 85) return ResourceType::Cloth;
    if (combined < 92) return ResourceType::Salt;
    if (combined < 97) return ResourceType::Wine;
    return ResourceType::Spices;
}

#pragma once

#include <cstddef>
#include <cstdint>
#include <istream>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include "core/game_context.h"
#include "core/tile_map.h"
#include "core/types.h"
#include "systems/economy.h"

enum class SettlementType : std::uint8_t
{
    None = 0,
    Village,
    Town,
    City,
    Count
};

struct Settlement
{
    std::int32_t id = -1;
    TilePosition pos = INVALID_POS;
    SettlementType type = SettlementType::None;
    FactionID faction = FactionID::Neutral;
    
    std::string name;
    double population = 0.0;
    double capital = 0.0;
    double growth_rate = 0.001;
    MarketPrices market;
    
    std::int32_t spawn_count = 0;
    std::int32_t max_spawn = 10;
    
    static constexpr double BASE_POPULATION_VILLAGE = 50.0;
    static constexpr double BASE_POPULATION_TOWN = 500.0;
    static constexpr double BASE_POPULATION_CITY = 5000.0;
    
    static constexpr double BASE_CAPITAL_VILLAGE = 1000.0;
    static constexpr double BASE_CAPITAL_TOWN = 10000.0;
    static constexpr double BASE_CAPITAL_CITY = 100000.0;
    
    void init_by_type(SettlementType t, rng_t& rng)
    {
        type = t;
        switch (t)
        {
            case SettlementType::Village:
                population = BASE_POPULATION_VILLAGE + random_u32_inclusive(rng, 50);
                capital = BASE_CAPITAL_VILLAGE + random_u32_inclusive(rng, 1000);
                max_spawn = 3;
                growth_rate = 0.0005;
                break;
            case SettlementType::Town:
                population = BASE_POPULATION_TOWN + random_u32_inclusive(rng, 500);
                capital = BASE_CAPITAL_TOWN + random_u32_inclusive(rng, 10000);
                max_spawn = 10;
                growth_rate = 0.001;
                break;
            case SettlementType::City:
                population = BASE_POPULATION_CITY + random_u32_inclusive(rng, 5000);
                capital = BASE_CAPITAL_CITY + random_u32_inclusive(rng, 100000);
                max_spawn = 30;
                growth_rate = 0.002;
                break;
            default:
                break;
        }
    }
    
    void update()
    {
        population += population * growth_rate;
        if (population > 1.0)
        {
            capital += population * 0.01;
        }

        // Рыночная симуляция: спрос и предложение выравниваются, цены обновляются
        market.decay();
        market.update_prices();
    }
};

class LandmarkSystem
{
public:
    using DistanceType = std::uint16_t;
    static constexpr DistanceType INVALID_DISTANCE = std::numeric_limits<DistanceType>::max();
    static constexpr std::size_t MAX_LANDMARKS = 256;
    
private:
    static constexpr std::size_t MAX_DISTANCE_CACHE_SIZE = 64;
    static constexpr std::size_t INVALID_CACHE_INDEX = std::numeric_limits<std::size_t>::max();

    struct DistanceCacheEntry
    {
        std::size_t settlement_idx = INVALID_CACHE_INDEX;
        std::unique_ptr<WorldMap<DistanceType>> field;
        std::uint64_t last_used = 0;
    };

    std::vector<Settlement> settlements_;
    WorldMap<std::int32_t> nearest_landmark_;
    std::unique_ptr<DistanceType[]> distance_matrix_;
    mutable std::vector<DistanceCacheEntry> distance_cache_{};
    mutable std::vector<TilePosition> bfs_queue_;
    std::int32_t next_id_ = 0;
    std::size_t distance_matrix_stride_ = 0;
    mutable std::uint64_t cache_tick_ = 0;
    const WorldMap<TerrainType>* relief_ = nullptr;
    
public:
    LandmarkSystem() = default;
    
    void init();
    void save(std::ostream& out) const;
    void load(std::istream& in, const WorldMap<TerrainType>* relief);

private:
    void ensure_distance_storage();
    WorldMap<DistanceType>* ensure_distance_field(std::size_t landmark_idx) const;

public:
    [[nodiscard]] Settlement* add_settlement(TilePosition pos, SettlementType type, rng_t& rng);
    void propagate_all_fields(const WorldMap<TerrainType>& relief);
    void compute_nearest_landmarks();
    void compute_distance_matrix();
    [[nodiscard]] std::optional<Direction> get_direction_toward_landmark(TilePosition current_pos, std::size_t landmark_idx) const;
    [[nodiscard]] std::int32_t get_distance_to_landmark(TilePosition pos, std::size_t landmark_idx) const;
    [[nodiscard]] std::int32_t get_distance_between_landmarks(std::size_t from, std::size_t to) const;
    
    [[nodiscard]] std::int32_t get_nearest_landmark_at(TilePosition pos) const
    {
        if (!is_valid(pos)) return -1;
        return nearest_landmark_[pos];
    }
    
    [[nodiscard]] Settlement* find_settlement_at(TilePosition pos);
    [[nodiscard]] const Settlement* find_settlement_at(TilePosition pos) const;
    [[nodiscard]] Settlement* get_settlement(std::size_t idx);
    [[nodiscard]] const Settlement* get_settlement(std::size_t idx) const;
    [[nodiscard]] std::size_t settlement_count() const noexcept { return settlements_.size(); }
    [[nodiscard]] std::vector<Settlement>& settlements() noexcept { return settlements_; }
    [[nodiscard]] const std::vector<Settlement>& settlements() const noexcept { return settlements_; }
    void update_all();
    [[nodiscard]] std::size_t find_nearest_other_settlement(std::size_t from_idx) const;
    [[nodiscard]] std::size_t find_random_destination(std::size_t from_idx, rng_t& rng) const;
};

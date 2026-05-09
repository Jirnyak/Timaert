#pragma once

#include <vector>
#include <cstdint>
#include <algorithm>
#include <limits>
#include <string>
#include <optional>
#include <memory>
#include <istream>
#include <ostream>
#include "core/binary_io.h"
#include "core/game_context.h"
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
    
    void init()
    {
        settlements_.clear();
        settlements_.reserve(MAX_LANDMARKS);
        
        nearest_landmark_.fill(-1);
        
        distance_matrix_.reset();
        distance_matrix_stride_ = 0;
        cache_tick_ = 0;
        relief_ = nullptr;
        distance_cache_.clear();

        bfs_queue_.clear();
        bfs_queue_.reserve(WORLD_SIZE);
        
        next_id_ = 0;
    }

    void save(std::ostream& out) const
    {
        BinaryWriter writer(out);
        const std::uint32_t settlement_count = static_cast<std::uint32_t>(settlements_.size());
        writer.write(settlement_count);
        writer.write(next_id_);

        for (const auto& settlement : settlements_)
        {
            writer.write(settlement.id);
            writer.write(settlement.pos.x);
            writer.write(settlement.pos.y);
            writer.write(settlement.type);
            writer.write(settlement.faction);

            writer.write_string(settlement.name);

            writer.write(settlement.population);
            writer.write(settlement.capital);
            writer.write(settlement.growth_rate);
            writer.write_bytes(&settlement.market, sizeof(MarketPrices));
            writer.write(settlement.spawn_count);
            writer.write(settlement.max_spawn);
        }
    }

    void load(std::istream& in, const WorldMap<TerrainType>* relief)
    {
        init();

        BinaryReader reader(in);
        std::uint32_t settlement_count = reader.read<std::uint32_t>();
        reader.read(next_id_);

        settlements_.reserve(std::min<std::size_t>(settlement_count, MAX_LANDMARKS));
        for (std::uint32_t i = 0; i < settlement_count && settlements_.size() < MAX_LANDMARKS; ++i)
        {
            Settlement settlement;
            reader.read(settlement.id);
            settlement.pos.x = reader.read<std::uint16_t>();
            settlement.pos.y = reader.read<std::uint16_t>();
            reader.read(settlement.type);
            reader.read(settlement.faction);

            reader.read_string(settlement.name);

            reader.read(settlement.population);
            reader.read(settlement.capital);
            reader.read(settlement.growth_rate);
            reader.read_bytes(&settlement.market, sizeof(MarketPrices));
            reader.read(settlement.spawn_count);
            reader.read(settlement.max_spawn);

            settlements_.push_back(std::move(settlement));
        }

        if (relief)
        {
            propagate_all_fields(*relief);
        }
    }

private:
    void ensure_distance_storage()
    {
        const std::size_t settlement_count = settlements_.size();
        if (settlement_count == 0)
        {
            distance_matrix_.reset();
            distance_matrix_stride_ = 0;
            distance_cache_.clear();
            return;
        }

        if (!distance_matrix_ || distance_matrix_stride_ != settlement_count)
        {
            distance_matrix_ = std::make_unique<DistanceType[]>(settlement_count * settlement_count);
            distance_matrix_stride_ = settlement_count;
        }

        std::fill_n(distance_matrix_.get(), settlement_count * settlement_count, INVALID_DISTANCE);

        const std::size_t cache_size = std::min(settlement_count, MAX_DISTANCE_CACHE_SIZE);
        if (distance_cache_.size() != cache_size)
        {
            distance_cache_.clear();
            distance_cache_.resize(cache_size);
        }
        for (auto& entry : distance_cache_)
        {
            entry.settlement_idx = INVALID_CACHE_INDEX;
            entry.field.reset();
            entry.last_used = 0;
        }
    }

    WorldMap<DistanceType>* ensure_distance_field(std::size_t landmark_idx) const
    {
        if (!relief_) return nullptr;
        if (distance_cache_.empty()) return nullptr;

        ++cache_tick_;

        for (auto& entry : distance_cache_)
        {
            if (entry.settlement_idx == landmark_idx && entry.field)
            {
                entry.last_used = cache_tick_;
                return entry.field.get();
            }
        }

        DistanceCacheEntry* selected = &distance_cache_[0];
        for (auto& entry : distance_cache_)
        {
            if (entry.settlement_idx == INVALID_CACHE_INDEX)
            {
                selected = &entry;
                break;
            }
            if (entry.last_used < selected->last_used)
            {
                selected = &entry;
            }
        }

        if (!selected->field)
        {
            selected->field = std::make_unique<WorldMap<DistanceType>>();
        }

        WorldMap<DistanceType>& field = *selected->field;
        field.fill(INVALID_DISTANCE);

        if (landmark_idx >= settlements_.size())
        {
            selected->settlement_idx = landmark_idx;
            selected->last_used = cache_tick_;
            return &field;
        }

        const TilePosition start_pos = settlements_[landmark_idx].pos;
        if (!is_valid(start_pos))
        {
            selected->settlement_idx = landmark_idx;
            selected->last_used = cache_tick_;
            return &field;
        }

        bfs_queue_.clear();
        bfs_queue_.reserve(WORLD_SIZE);
        field[start_pos] = 0;
        bfs_queue_.push_back(start_pos);

        std::size_t head = 0;
        while (head < bfs_queue_.size())
        {
            const TilePosition current_pos = bfs_queue_[head++];
            const DistanceType current_dist = field[current_pos];

            for (int d = 0; d < 4; ++d)
            {
                const Direction dir = static_cast<Direction>(d);
                const TilePosition neighbor_tile = neighbor_from_pos(current_pos, dir);
                if (!is_valid(neighbor_tile)) continue;

                if ((*relief_)[neighbor_tile] == TerrainType::Water ||
                    (*relief_)[neighbor_tile] == TerrainType::Mount)
                {
                    continue;
                }

                const DistanceType new_dist = static_cast<DistanceType>(current_dist + 1);
                if (new_dist < field[neighbor_tile])
                {
                    field[neighbor_tile] = new_dist;
                    bfs_queue_.push_back(neighbor_tile);
                }
            }
        }

        selected->settlement_idx = landmark_idx;
        selected->last_used = cache_tick_;
        return &field;
    }

public:
    
    [[nodiscard]] Settlement* add_settlement(TilePosition pos, SettlementType type, rng_t& rng)
    {
        if (settlements_.size() >= MAX_LANDMARKS) return nullptr;
        if (!is_valid(pos)) return nullptr;
        
        Settlement s;
        s.id = next_id_++;
        s.pos = pos;
        s.init_by_type(type, rng);
        
        settlements_.push_back(std::move(s));
        return &settlements_.back();
    }
    
    void propagate_all_fields(const WorldMap<TerrainType>& relief)
    {
        relief_ = &relief;
        ensure_distance_storage();
        compute_nearest_landmarks();
        compute_distance_matrix();
    }
    
    void compute_nearest_landmarks()
    {
        if (!relief_) return;
        if (settlements_.empty()) return;

        WorldMap<DistanceType> min_dist;
        min_dist.fill(INVALID_DISTANCE);
        nearest_landmark_.fill(-1);

        for (std::size_t i = 0; i < settlements_.size(); ++i)
        {
            const WorldMap<DistanceType>* field = ensure_distance_field(i);
            if (!field) return;

            for (int y = 0; y < WORLD_WIDTH; ++y)
            {
                for (int x = 0; x < WORLD_WIDTH; ++x)
                {
                    const TilePosition pos{static_cast<std::uint16_t>(x), static_cast<std::uint16_t>(y)};
                    const DistanceType dist = (*field)[pos];
                    if (dist < min_dist[pos])
                    {
                        min_dist[pos] = dist;
                        nearest_landmark_[pos] = static_cast<std::int32_t>(i);
                    }
                }
            }
        }
    }
    
    void compute_distance_matrix()
    {
        const std::size_t n = settlements_.size();
        if (n == 0 || distance_matrix_stride_ != n) return;
        if (!relief_) return;
        
        for (std::size_t i = 0; i < n; ++i)
        {
            const WorldMap<DistanceType>* field = ensure_distance_field(i);
            if (!field) return;

            for (std::size_t j = 0; j < n; ++j)
            {
                if (i == j)
                {
                    distance_matrix_[i * distance_matrix_stride_ + j] = 0;
                }
                else
                {
                    const TilePosition pos_j = settlements_[j].pos;
                    if (is_valid(pos_j))
                    {
                        distance_matrix_[i * distance_matrix_stride_ + j] = (*field)[pos_j];
                    }
                }
            }
        }
    }
    
    [[nodiscard]] std::optional<Direction> get_direction_toward_landmark(TilePosition current_pos, std::size_t landmark_idx) const
    {
        if (landmark_idx >= settlements_.size()) return std::nullopt;
        if (!is_valid(current_pos)) return std::nullopt;
        
        const WorldMap<DistanceType>* field = ensure_distance_field(landmark_idx);
        if (!field) return std::nullopt;
        
        DistanceType current_dist = (*field)[current_pos];
        if (current_dist == INVALID_DISTANCE) return std::nullopt;
        if (current_dist == 0) return std::nullopt;
        
        std::optional<Direction> best_dir;
        DistanceType best_dist = current_dist;
        
        for (int d = 0; d < 4; ++d)
        {
            const Direction dir = static_cast<Direction>(d);
            const TilePosition neighbor_pos = neighbor_from_pos(current_pos, dir);
            if (!is_valid(neighbor_pos)) continue;
            
            const DistanceType neighbor_dist = (*field)[neighbor_pos];
            if (neighbor_dist < best_dist)
            {
                best_dist = neighbor_dist;
                best_dir = dir;
            }
        }
        
        return best_dir;
    }
    
    [[nodiscard]] std::int32_t get_distance_to_landmark(TilePosition pos, std::size_t landmark_idx) const
    {
        if (landmark_idx >= settlements_.size()) return INVALID_DISTANCE;
        if (!is_valid(pos)) return INVALID_DISTANCE;
        
        const WorldMap<DistanceType>* field = ensure_distance_field(landmark_idx);
        if (!field) return INVALID_DISTANCE;
        return static_cast<std::int32_t>((*field)[pos]);
    }
    
    [[nodiscard]] std::int32_t get_distance_between_landmarks(std::size_t from, std::size_t to) const
    {
        if (from >= settlements_.size() || to >= settlements_.size()) return INVALID_DISTANCE;
        if (distance_matrix_stride_ == 0) return INVALID_DISTANCE;
        return static_cast<std::int32_t>(distance_matrix_[from * distance_matrix_stride_ + to]);
    }
    
    [[nodiscard]] std::int32_t get_nearest_landmark_at(TilePosition pos) const
    {
        if (!is_valid(pos)) return -1;
        return nearest_landmark_[pos];
    }
    
    [[nodiscard]] Settlement* find_settlement_at(TilePosition pos)
    {
        for (auto& s : settlements_)
        {
            if (s.pos == pos) return &s;
        }
        return nullptr;
    }
    
    [[nodiscard]] const Settlement* find_settlement_at(TilePosition pos) const
    {
        for (const auto& s : settlements_)
        {
            if (s.pos == pos) return &s;
        }
        return nullptr;
    }
    
    [[nodiscard]] Settlement* get_settlement(std::size_t idx)
    {
        if (idx >= settlements_.size()) return nullptr;
        return &settlements_[idx];
    }
    
    [[nodiscard]] const Settlement* get_settlement(std::size_t idx) const
    {
        if (idx >= settlements_.size()) return nullptr;
        return &settlements_[idx];
    }
    
    [[nodiscard]] std::size_t settlement_count() const noexcept { return settlements_.size(); }
    
    [[nodiscard]] std::vector<Settlement>& settlements() noexcept { return settlements_; }
    [[nodiscard]] const std::vector<Settlement>& settlements() const noexcept { return settlements_; }
    
    void update_all()
    {
        for (auto& s : settlements_)
        {
            s.update();
        }
    }
    
    [[nodiscard]] std::size_t find_nearest_other_settlement(std::size_t from_idx) const
    {
        if (from_idx >= settlements_.size()) return from_idx;
        
        std::int32_t min_dist = INVALID_DISTANCE;
        std::size_t nearest = from_idx;
        
        for (std::size_t i = 0; i < settlements_.size(); ++i)
        {
            if (i == from_idx) continue;
            
            const std::int32_t dist = distance_matrix_[from_idx * distance_matrix_stride_ + i];
            if (dist < min_dist && dist > 0)
            {
                min_dist = dist;
                nearest = i;
            }
        }
        
        return nearest;
    }
    
    [[nodiscard]] std::size_t find_random_destination(std::size_t from_idx, rng_t& rng) const
    {
        if (settlements_.size() <= 1) return from_idx;
        
        std::vector<std::size_t> reachable;
        reachable.reserve(settlements_.size());
        
        for (std::size_t i = 0; i < settlements_.size(); ++i)
        {
            if (i == from_idx) continue;
            if (distance_matrix_[from_idx * distance_matrix_stride_ + i] < INVALID_DISTANCE)
            {
                reachable.push_back(i);
            }
        }
        
        if (reachable.empty()) return from_idx;
        
        const std::size_t idx = random_u32_inclusive(rng, static_cast<std::uint32_t>(reachable.size() - 1));
        return reachable[idx];
    }
};

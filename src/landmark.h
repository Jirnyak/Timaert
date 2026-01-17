#pragma once

#include <vector>
#include <cstdint>
#include <algorithm>
#include <limits>
#include <string>
#include "game_context.h"

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
    std::int32_t pos = -1;
    SettlementType type = SettlementType::None;
    std::uint8_t owner = 0;
    
    std::string name;
    double population = 0.0;
    double capital = 0.0;
    double growth_rate = 0.001;
    
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
                population = BASE_POPULATION_VILLAGE + randomer(rng, 50);
                capital = BASE_CAPITAL_VILLAGE + randomer(rng, 1000);
                max_spawn = 3;
                growth_rate = 0.0005;
                break;
            case SettlementType::Town:
                population = BASE_POPULATION_TOWN + randomer(rng, 500);
                capital = BASE_CAPITAL_TOWN + randomer(rng, 10000);
                max_spawn = 10;
                growth_rate = 0.001;
                break;
            case SettlementType::City:
                population = BASE_POPULATION_CITY + randomer(rng, 5000);
                capital = BASE_CAPITAL_CITY + randomer(rng, 100000);
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
    }
};

class LandmarkSystem
{
public:
    static constexpr std::int32_t INVALID_DISTANCE = std::numeric_limits<std::int32_t>::max();
    static constexpr std::size_t MAX_LANDMARKS = 256;
    
private:
    std::vector<Settlement> settlements_;
    std::unique_ptr<std::int32_t[]> distance_fields_;
    std::unique_ptr<std::int32_t[]> nearest_landmark_;
    std::unique_ptr<std::int32_t[]> distance_matrix_;
    std::vector<int> bfs_queue_;
    std::int32_t next_id_ = 0;
    
public:
    LandmarkSystem() = default;
    
    void init()
    {
        settlements_.clear();
        settlements_.reserve(MAX_LANDMARKS);
        
        const std::size_t world_size = static_cast<std::size_t>(WORLD_WIDTH) * WORLD_WIDTH;
        distance_fields_ = std::make_unique<std::int32_t[]>(world_size * MAX_LANDMARKS);
        std::fill_n(distance_fields_.get(), world_size * MAX_LANDMARKS, INVALID_DISTANCE);
        
        nearest_landmark_ = std::make_unique<std::int32_t[]>(world_size);
        std::fill_n(nearest_landmark_.get(), world_size, -1);
        
        distance_matrix_ = std::make_unique<std::int32_t[]>(MAX_LANDMARKS * MAX_LANDMARKS);
        std::fill_n(distance_matrix_.get(), MAX_LANDMARKS * MAX_LANDMARKS, INVALID_DISTANCE);

        bfs_queue_.clear();
        bfs_queue_.reserve(world_size);
        
        next_id_ = 0;
    }
    
    [[nodiscard]] Settlement* add_settlement(int pos, SettlementType type, rng_t& rng)
    {
        if (settlements_.size() >= MAX_LANDMARKS) return nullptr;
        if (pos < 0 || pos >= WORLD_WIDTH * WORLD_WIDTH) return nullptr;
        
        Settlement s;
        s.id = next_id_++;
        s.pos = pos;
        s.init_by_type(type, rng);
        
        settlements_.push_back(std::move(s));
        return &settlements_.back();
    }
    
    void propagate_distance_field(std::size_t landmark_idx, const TerrainType* relief, const std::vector<Cell>& world)
    {
        if (landmark_idx >= settlements_.size()) return;
        
        const std::size_t world_size = static_cast<std::size_t>(WORLD_WIDTH) * WORLD_WIDTH;
        std::int32_t* field = distance_fields_.get() + landmark_idx * world_size;
        
        std::fill_n(field, world_size, INVALID_DISTANCE);
        
        const int start_pos = settlements_[landmark_idx].pos;
        if (start_pos < 0) return;
        
        bfs_queue_.clear();
        bfs_queue_.reserve(world_size);
        field[start_pos] = 0;
        bfs_queue_.push_back(start_pos);

        std::size_t head = 0;
        while (head < bfs_queue_.size())
        {
            const int current_pos = bfs_queue_[head++];
            const std::int32_t current_dist = field[current_pos];

            for (int dir = 0; dir < 4; ++dir)
            {
                const int neighbor = world[current_pos].side(dir);
                if (neighbor < 0 || neighbor >= static_cast<int>(world_size)) continue;

                if (relief[neighbor] == TerrainType::Water ||
                    relief[neighbor] == TerrainType::Mount)
                {
                    continue;
                }

                const std::int32_t new_dist = current_dist + 1;
                if (new_dist < field[neighbor])
                {
                    field[neighbor] = new_dist;
                    bfs_queue_.push_back(neighbor);
                }
            }
        }
    }
    
    void propagate_all_fields(const TerrainType* relief, const std::vector<Cell>& world)
    {
        for (std::size_t i = 0; i < settlements_.size(); ++i)
        {
            propagate_distance_field(i, relief, world);
        }
        
        compute_nearest_landmarks();
        compute_distance_matrix();
    }
    
    void compute_nearest_landmarks()
    {
        const std::size_t world_size = static_cast<std::size_t>(WORLD_WIDTH) * WORLD_WIDTH;
        
        for (std::size_t pos = 0; pos < world_size; ++pos)
        {
            std::int32_t min_dist = INVALID_DISTANCE;
            std::int32_t nearest = -1;
            
            for (std::size_t i = 0; i < settlements_.size(); ++i)
            {
                const std::int32_t dist = distance_fields_[i * world_size + pos];
                if (dist < min_dist)
                {
                    min_dist = dist;
                    nearest = static_cast<std::int32_t>(i);
                }
            }
            
            nearest_landmark_[pos] = nearest;
        }
    }
    
    void compute_distance_matrix()
    {
        const std::size_t world_size = static_cast<std::size_t>(WORLD_WIDTH) * WORLD_WIDTH;
        const std::size_t n = settlements_.size();
        
        for (std::size_t i = 0; i < n; ++i)
        {
            for (std::size_t j = 0; j < n; ++j)
            {
                if (i == j)
                {
                    distance_matrix_[i * MAX_LANDMARKS + j] = 0;
                }
                else
                {
                    const int pos_j = settlements_[j].pos;
                    if (pos_j >= 0 && pos_j < static_cast<int>(world_size))
                    {
                        distance_matrix_[i * MAX_LANDMARKS + j] = distance_fields_[i * world_size + pos_j];
                    }
                }
            }
        }
    }
    
    [[nodiscard]] int get_direction_toward_landmark(int current_pos, std::size_t landmark_idx, 
                                                     const std::vector<Cell>& world) const
    {
        if (landmark_idx >= settlements_.size()) return -1;
        if (current_pos < 0 || current_pos >= WORLD_WIDTH * WORLD_WIDTH) return -1;
        
        const std::size_t world_size = static_cast<std::size_t>(WORLD_WIDTH) * WORLD_WIDTH;
        const std::int32_t* field = distance_fields_.get() + landmark_idx * world_size;
        
        std::int32_t current_dist = field[current_pos];
        if (current_dist == INVALID_DISTANCE) return -1;
        if (current_dist == 0) return -1;
        
        int best_dir = -1;
        std::int32_t best_dist = current_dist;
        
        for (int dir = 0; dir < 4; ++dir)
        {
            const int neighbor = world[current_pos].side(dir);
            if (neighbor < 0 || neighbor >= static_cast<int>(world_size)) continue;
            
            const std::int32_t neighbor_dist = field[neighbor];
            if (neighbor_dist < best_dist)
            {
                best_dist = neighbor_dist;
                best_dir = dir;
            }
        }
        
        return best_dir;
    }
    
    [[nodiscard]] std::int32_t get_distance_to_landmark(int pos, std::size_t landmark_idx) const
    {
        if (landmark_idx >= settlements_.size()) return INVALID_DISTANCE;
        if (pos < 0 || pos >= WORLD_WIDTH * WORLD_WIDTH) return INVALID_DISTANCE;
        
        const std::size_t world_size = static_cast<std::size_t>(WORLD_WIDTH) * WORLD_WIDTH;
        return distance_fields_[landmark_idx * world_size + pos];
    }
    
    [[nodiscard]] std::int32_t get_distance_between_landmarks(std::size_t from, std::size_t to) const
    {
        if (from >= settlements_.size() || to >= settlements_.size()) return INVALID_DISTANCE;
        return distance_matrix_[from * MAX_LANDMARKS + to];
    }
    
    [[nodiscard]] std::int32_t get_nearest_landmark_at(int pos) const
    {
        if (pos < 0 || pos >= WORLD_WIDTH * WORLD_WIDTH) return -1;
        return nearest_landmark_[pos];
    }
    
    [[nodiscard]] Settlement* find_settlement_at(int pos)
    {
        for (auto& s : settlements_)
        {
            if (s.pos == pos) return &s;
        }
        return nullptr;
    }
    
    [[nodiscard]] const Settlement* find_settlement_at(int pos) const
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
            
            const std::int32_t dist = distance_matrix_[from_idx * MAX_LANDMARKS + i];
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
            if (distance_matrix_[from_idx * MAX_LANDMARKS + i] < INVALID_DISTANCE)
            {
                reachable.push_back(i);
            }
        }
        
        if (reachable.empty()) return from_idx;
        
        const std::size_t idx = randomer(rng, static_cast<std::uint32_t>(reachable.size() - 1));
        return reachable[idx];
    }
};

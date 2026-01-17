#pragma once

#include <cstdint>
#include <algorithm>
#include <vector>
#include "game_context.h"
#include "economy.h"
#include "landmark.h"

enum class PlayerState : std::uint8_t
{
    Normal,
    InSettlement,
    Trading,
    InCombat
};

struct Player
{
    std::int32_t pos = -1;
    std::int32_t prev_pos = -1;
    std::int32_t aim_pos = -1;
    
    Inventory inventory;
    PlayerState state = PlayerState::Normal;
    
    double speed = 1.0;
    double move_progress = 0.0;
    std::int32_t life = 100;
    std::int32_t max_life = 100;
    
    bool active = false;
    
    void init(int start_pos, rng_t& /*rng*/)
    {
        pos = start_pos;
        prev_pos = start_pos;
        aim_pos = -1;
        inventory = Inventory{};
        inventory.max_capacity = 50;
        inventory.capital = 1000.0;
        state = PlayerState::Normal;
        speed = 25.0;
        move_progress = 0.0;
        life = 100;
        max_life = 100;
        active = true;
    }
    
    void set_aim(int target_pos)
    {
        aim_pos = target_pos;
    }
    
    void clear_aim()
    {
        aim_pos = -1;
    }
    
    [[nodiscard]] bool has_aim() const noexcept
    {
        return aim_pos >= 0;
    }
    
    [[nodiscard]] bool is_at_aim() const noexcept
    {
        return aim_pos >= 0 && pos == aim_pos;
    }
};

class PlayerController
{
private:
    Player player_;
    std::int32_t current_settlement_idx_ = -1;
    std::vector<int> path_{};
    std::size_t path_index_ = 0;
    
public:
    PlayerController() = default;
    
    void init(int start_pos, rng_t& rng)
    {
        player_.init(start_pos, rng);
        current_settlement_idx_ = -1;
    }
    
    void update(GameContext& ctx, LandmarkSystem& landmarks,
                const TerrainType* relief, const std::vector<Cell>& world)
    {
        if (!player_.active) return;
        
        const Settlement* settlement = landmarks.find_settlement_at(player_.pos);
        if (settlement)
        {
            player_.state = PlayerState::InSettlement;
            current_settlement_idx_ = settlement->id;
        }
        else
        {
            player_.state = PlayerState::Normal;
            current_settlement_idx_ = -1;
        }
        
        if (path_index_ < path_.size())
        {
            if (player_.is_at_aim())
            {
                clear_path();
                return;
            }

            player_.move_progress += player_.speed;
            if (player_.move_progress < 100.0) return;
            player_.move_progress = 0.0;

            const int next_pos = path_[path_index_];
            if (can_move_to(next_pos, relief))
            {
                player_.prev_pos = player_.pos;
                player_.pos = next_pos;
                ++path_index_;
                if (path_index_ >= path_.size())
                {
                    clear_path();
                }
            }
            else
            {
                clear_path();
            }
            return;
        }

        if (!player_.has_aim()) return;
        if (player_.is_at_aim())
        {
            player_.clear_aim();
            return;
        }

        player_.move_progress += player_.speed;
        if (player_.move_progress < 100.0) return;
        player_.move_progress = 0.0;

        move_toward_direct(ctx, relief, world);
    }
    
    void move_toward_direct(GameContext& /*ctx*/, const TerrainType* relief, 
                            const std::vector<Cell>& world)
    {
        if (!player_.has_aim()) return;
        
        const int aim_row = player_.aim_pos / WORLD_WIDTH;
        const int aim_col = player_.aim_pos % WORLD_WIDTH;
        const int cur_row = player_.pos / WORLD_WIDTH;
        const int cur_col = player_.pos % WORLD_WIDTH;
        
        int d_row = aim_row - cur_row;
        int d_col = aim_col - cur_col;
        
        if (std::abs(d_row) > WORLD_WIDTH / 2)
        {
            d_row = (d_row > 0) ? d_row - WORLD_WIDTH : d_row + WORLD_WIDTH;
        }
        if (std::abs(d_col) > WORLD_WIDTH / 2)
        {
            d_col = (d_col > 0) ? d_col - WORLD_WIDTH : d_col + WORLD_WIDTH;
        }
        
        int best_dir = -1;
        if (std::abs(d_row) >= std::abs(d_col))
        {
            best_dir = (d_row < 0) ? 0 : 2;
        }
        else
        {
            best_dir = (d_col < 0) ? 1 : 3;
        }
        
        int next_pos = world[player_.pos].side(best_dir);
        if (can_move_to(next_pos, relief))
        {
            player_.prev_pos = player_.pos;
            player_.pos = next_pos;
            return;
        }
        
        for (int d = 0; d < 4; ++d)
        {
            if (d == best_dir) continue;
            next_pos = world[player_.pos].side(d);
            if (can_move_to(next_pos, relief))
            {
                player_.prev_pos = player_.pos;
                player_.pos = next_pos;
                return;
            }
        }
        
    }
    
    [[nodiscard]] bool can_move_to(int pos, const TerrainType* relief) const noexcept
    {
        if (pos < 0 || pos >= WORLD_WIDTH * WORLD_WIDTH) return false;
        return relief[pos] != TerrainType::Water && relief[pos] != TerrainType::Mount;
    }
    
    void move_direction(int dir, const TerrainType* relief, const std::vector<Cell>& world)
    {
        if (!player_.active) return;
        if (dir < 0 || dir > 3) return;
        
        const int next_pos = world[player_.pos].side(dir);
        if (can_move_to(next_pos, relief))
        {
            player_.prev_pos = player_.pos;
            player_.pos = next_pos;
        }
    }

    [[nodiscard]] bool set_path_to(int target_pos, const TerrainType* relief,
                                   const std::vector<Cell>& world)
    {
        if (!player_.active) return false;
        if (target_pos < 0 || target_pos >= WORLD_WIDTH * WORLD_WIDTH) return false;
        if (!can_move_to(target_pos, relief)) return false;
        if (target_pos == player_.pos) return false;

        const int world_size = WORLD_WIDTH * WORLD_WIDTH;
        std::vector<int> prev(static_cast<std::size_t>(world_size), -1);
        std::vector<int> queue;
        queue.reserve(static_cast<std::size_t>(world_size));

        prev[player_.pos] = player_.pos;
        queue.push_back(player_.pos);

        std::size_t head = 0;
        while (head < queue.size())
        {
            const int current = queue[head++];
            if (current == target_pos) break;

            for (int dir = 0; dir < 4; ++dir)
            {
                const int neighbor = world[current].side(dir);
                if (neighbor < 0 || neighbor >= world_size) continue;
                if (prev[neighbor] != -1) continue;
                if (!can_move_to(neighbor, relief)) continue;

                prev[neighbor] = current;
                queue.push_back(neighbor);
            }
        }

        if (prev[target_pos] == -1) return false;

        std::vector<int> new_path;
        for (int pos = target_pos; pos != player_.pos; pos = prev[pos])
        {
            new_path.push_back(pos);
        }
        std::reverse(new_path.begin(), new_path.end());

        path_ = std::move(new_path);
        path_index_ = 0;
        player_.set_aim(target_pos);
        return true;
    }

    void clear_path()
    {
        path_.clear();
        path_index_ = 0;
        player_.clear_aim();
    }

    void clear_aim()
    {
        clear_path();
    }
    
    [[nodiscard]] bool try_buy(ResourceType res, std::int32_t amount, Settlement& settlement)
    {
        if (player_.state != PlayerState::InSettlement) return false;
        if (settlement.id != current_settlement_idx_) return false;
        
        const double price = resource_base_price(res) * 1.1 * amount;
        if (player_.inventory.capital < price) return false;
        if (!player_.inventory.can_add(res, amount)) return false;
        
        player_.inventory.capital -= price;
        player_.inventory.add(res, amount);
        settlement.capital += price;
        
        return true;
    }
    
    [[nodiscard]] bool try_sell(ResourceType res, std::int32_t amount, Settlement& settlement)
    {
        if (player_.state != PlayerState::InSettlement) return false;
        if (settlement.id != current_settlement_idx_) return false;
        
        if (player_.inventory.get(res) < amount) return false;
        
        const double price = resource_base_price(res) * 0.9 * amount;
        if (settlement.capital < price) return false;
        
        player_.inventory.remove(res, amount);
        player_.inventory.capital += price;
        settlement.capital -= price;
        
        return true;
    }
    
    [[nodiscard]] Player& player() noexcept { return player_; }
    [[nodiscard]] const Player& player() const noexcept { return player_; }
    
    [[nodiscard]] std::int32_t current_settlement() const noexcept { return current_settlement_idx_; }
    
    [[nodiscard]] bool is_in_settlement() const noexcept
    {
        return player_.state == PlayerState::InSettlement;
    }
};

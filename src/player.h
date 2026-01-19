#pragma once

#include <cstdint>
#include <algorithm>
#include <vector>
#include "game_context.h"
#include "economy.h"
#include "landmark.h"
#include "skills.h"
#include "npc.h" // Нужно знать про NPC для коллизий

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
    float visual_x = 0.0f;
    float visual_y = 0.0f;
    std::int32_t life = 100;
    std::int32_t max_life = 100;
    
    // --- Ролевые характеристики ---
    Gender gender = Gender::Male;
    Race race = Race::Human;
    
    std::int32_t lust = 0;
    std::int32_t max_lust = 100;
    std::int32_t will = 100;
    std::int32_t max_will = 100;
    
    // Навыки (фиксированный массив для бинарной совместимости сохранения)
    static constexpr std::size_t MAX_PLAYER_SKILLS = 32;
    SkillID skills[MAX_PLAYER_SKILLS];
    std::uint8_t skill_count = 0;
    // ------------------------------
    
    bool active = false;
    
    void init(int start_pos, rng_t& rng)
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
        visual_x = static_cast<float>(start_pos / WORLD_WIDTH);
        visual_y = static_cast<float>(start_pos % WORLD_WIDTH);
        life = 100;
        max_life = 100;
        
        gender = static_cast<Gender>(random_u32_inclusive(rng, static_cast<std::uint32_t>(Gender::Count) - 1));
        race = Race::Human;
        
        lust = 0;
        max_lust = 100;
        will = 100;
        max_will = 100;
        
        // Инициализация навыков
        skill_count = 0;
        for(auto& s : skills) s = SkillID::Wait;

        learn_skill(SkillID::Punch);
        learn_skill(SkillID::Wait);
        learn_skill(SkillID::Tease);
        
        active = true;
    }

    void learn_skill(SkillID skill)
    {
        // Проверяем дубликаты
        for (std::size_t i = 0; i < skill_count; ++i) {
            if (skills[i] == skill) return;
        }
        
        // Добавляем, если есть место
        if (skill_count < MAX_PLAYER_SKILLS) {
            skills[skill_count++] = skill;
        }
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

    // --- Логика коллизий ---
    // Проверка: можно ли шагнуть в клетку, или там враг?
    bool check_collision_and_trigger(int target_pos, NPCManager& npcs, GameContext& ctx)
    {
        // Ищем NPC в целевой клетке
        NPC* npc = npcs.find_at(target_pos);
        if (npc && npc->state != NPCState::Dead)
        {
            // Если нашли - инициируем бой
            ctx.battle_target_id = npc->id;
            ctx.game_mod = GameMode::Fight;
            
            // Останавливаем движение игрока
            player_.clear_aim();
            path_.clear();
            path_index_ = 0;
            return true; // Коллизия произошла
        }
        return false; // Пусто
    }
    // -----------------------
    
public:
    PlayerController() = default;
    
    void init(int start_pos, rng_t& rng)
    {
        player_.init(start_pos, rng);
        current_settlement_idx_ = -1;
    }
    
    // Метод обновлен: принимает NPCManager
    void update(GameContext& ctx, LandmarkSystem& landmarks,
                const TerrainType* relief, NPCManager& npcs)
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
            
            // Проверка на врага перед шагом
            if (check_collision_and_trigger(next_pos, npcs, ctx)) return;

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

        move_toward_direct(ctx, relief, npcs);
    }
    
    // Метод обновлен: принимает NPCManager
    void move_toward_direct(GameContext& ctx, const TerrainType* relief, NPCManager& npcs)
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
        
        Direction best_dir = Direction::Up;
        if (std::abs(d_row) >= std::abs(d_col))
        {
            best_dir = (d_row < 0) ? Direction::Up : Direction::Down;
        }
        else
        {
            best_dir = (d_col < 0) ? Direction::Left : Direction::Right;
        }
        
        int next_pos = neighbor_from_pos(player_.pos, best_dir);

        // Проверка коллизии
        if (check_collision_and_trigger(next_pos, npcs, ctx)) return;

        if (can_move_to(next_pos, relief))
        {
            player_.prev_pos = player_.pos;
            player_.pos = next_pos;
            return;
        }
        
        for (int d = 0; d < 4; ++d)
        {
            const Direction dir = static_cast<Direction>(d);
            if (dir == best_dir) continue;
            next_pos = neighbor_from_pos(player_.pos, dir);

            // Проверка коллизии для альтернативного пути
            if (check_collision_and_trigger(next_pos, npcs, ctx)) return;

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

    struct TerrainEffect {
        float speed_mult = 1.0f;
        int will_drain = 0;
    };

    [[nodiscard]] TerrainEffect get_terrain_effect(TerrainType type) const noexcept
    {
        switch (type) {
            case TerrainType::Swamp:  return { 0.4f, 1 }; // Очень медленно + изнурение
            case TerrainType::Snow:   return { 0.6f, 1 }; // Снег замедляет
            case TerrainType::Jungle: return { 0.6f, 1 }; // Густые заросли
            case TerrainType::Sand:   return { 0.8f, 0 }; // Песок немного замедляет
            case TerrainType::Tundra: return { 0.8f, 0 };
            case TerrainType::Grass:  return { 1.0f, 0 };
            case TerrainType::Dirt:   return { 1.0f, 0 };
            default: return { 1.0f, 0 };
        }
    }
    
    // Метод обновлен: принимает NPCManager
    void move_direction(Direction dir, const TerrainType* relief, NPCManager& npcs, GameContext& ctx)
    {
        if (!player_.active) return;
        
        const int next_pos = neighbor_from_pos(player_.pos, dir);
        
        // Проверка коллизии
        if (check_collision_and_trigger(next_pos, npcs, ctx)) return;

        if (can_move_to(next_pos, relief))
        {
            player_.prev_pos = player_.pos;
            player_.pos = next_pos;
        }
    }

    [[nodiscard]] bool set_path_to(GameContext& ctx, int target_pos, const TerrainType* relief)
    {
        if (!player_.active) return false;
        if (target_pos < 0 || target_pos >= WORLD_WIDTH * WORLD_WIDTH) return false;
        if (!can_move_to(target_pos, relief)) return false;
        if (target_pos == player_.pos) return false;

        const int world_size = WORLD_WIDTH * WORLD_WIDTH;
        auto& prev = ctx.path_prev;
        auto& queue = ctx.path_queue;
        if (prev.size() != static_cast<std::size_t>(world_size))
        {
            prev.assign(static_cast<std::size_t>(world_size), -1);
        }
        else
        {
            std::fill(prev.begin(), prev.end(), -1);
        }
        queue.clear();
        if (queue.capacity() < static_cast<std::size_t>(world_size))
        {
            queue.reserve(static_cast<std::size_t>(world_size));
        }

        prev[player_.pos] = player_.pos;
        queue.push_back(player_.pos);

        std::size_t head = 0;
        while (head < queue.size())
        {
            const int current = queue[head++];
            if (current == target_pos) break;

            for (int d = 0; d < 4; ++d)
            {
                const Direction dir = static_cast<Direction>(d);
                const int neighbor = neighbor_from_pos(current, dir);
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

    void set_current_settlement(std::int32_t settlement_idx) noexcept
    {
        current_settlement_idx_ = settlement_idx;
    }
    
    [[nodiscard]] bool is_in_settlement() const noexcept
    {
        return player_.state == PlayerState::InSettlement;
    }
};

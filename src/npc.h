#pragma once

#include <cstdint>
#include <vector>
#include "game_context.h"
#include "economy.h"
#include "landmark.h"

enum class NPCType : std::uint8_t
{
    None = 0,
    Peasant,
    Merchant,
    Caravan,
    Bandit,
    Guard,
    Count
};

enum class NPCState : std::uint8_t
{
    Idle,
    Wandering,
    Traveling,
    Trading,
    Returning,
    Fleeing,
    Dead
};

struct NPC
{
    std::int32_t id = -1;
    std::int32_t pos = -1;
    std::int32_t prev_pos = -1;
    NPCType type = NPCType::None;
    NPCState state = NPCState::Idle;
    std::uint8_t owner = 0;
    
    std::int32_t home_settlement = -1;
    std::int32_t target_settlement = -1;
    
    Inventory inventory;
    
    double speed = 1.0;
    double move_progress = 0.0;
    std::int32_t life = 100;
    std::int32_t max_life = 100;
    
    std::int32_t idle_timer = 0;
    std::int32_t trade_timer = 0;
    
    bool active = false;
    
    void reset() noexcept
    {
        id = -1;
        pos = -1;
        prev_pos = -1;
        type = NPCType::None;
        state = NPCState::Idle;
        owner = 0;
        home_settlement = -1;
        target_settlement = -1;
        inventory = Inventory{};
        speed = 1.0;
        move_progress = 0.0;
        life = 100;
        max_life = 100;
        idle_timer = 0;
        trade_timer = 0;
        active = false;
    }
    
    void init_by_type(NPCType t, rng_t& rng)
    {
        type = t;
        switch (t)
        {
            case NPCType::Peasant:
                speed = 0.5 + static_cast<double>(randomer(rng, 50)) / 100.0;
                inventory.max_capacity = 20;
                life = max_life = 50 + static_cast<std::int32_t>(randomer(rng, 50));
                break;
            case NPCType::Merchant:
                speed = 0.8 + static_cast<double>(randomer(rng, 40)) / 100.0;
                inventory.max_capacity = 100;
                inventory.capital = 500.0 + randomer(rng, 500);
                life = max_life = 80 + static_cast<std::int32_t>(randomer(rng, 40));
                break;
            case NPCType::Caravan:
                speed = 0.6 + static_cast<double>(randomer(rng, 30)) / 100.0;
                inventory.max_capacity = 500;
                inventory.capital = 2000.0 + randomer(rng, 3000);
                life = max_life = 200 + static_cast<std::int32_t>(randomer(rng, 100));
                break;
            case NPCType::Bandit:
                speed = 1.0 + static_cast<double>(randomer(rng, 50)) / 100.0;
                inventory.max_capacity = 50;
                life = max_life = 100 + static_cast<std::int32_t>(randomer(rng, 50));
                break;
            case NPCType::Guard:
                speed = 0.7;
                inventory.max_capacity = 30;
                life = max_life = 150 + static_cast<std::int32_t>(randomer(rng, 50));
                break;
            default:
                break;
        }
    }
};

class NPCManager
{
public:
    static constexpr std::size_t MAX_NPCS = 4096;
    
private:
    std::unique_ptr<NPC[]> npcs_;
    std::vector<std::size_t> free_ids_;
    std::int32_t next_id_ = 0;
    
public:
    NPCManager()
        : npcs_(std::make_unique<NPC[]>(MAX_NPCS))
    {
        free_ids_.reserve(MAX_NPCS);
    }
    
    void init()
    {
        free_ids_.clear();
        for (std::size_t i = 0; i < MAX_NPCS; ++i)
        {
            npcs_[i].reset();
            free_ids_.push_back(i);
        }
        next_id_ = 0;
    }
    
    [[nodiscard]] NPC* spawn(NPCType type, int pos, int home_settlement, rng_t& rng)
    {
        if (free_ids_.empty()) return nullptr;
        
        const std::size_t slot = free_ids_.back();
        free_ids_.pop_back();
        
        NPC& npc = npcs_[slot];
        npc.reset();
        npc.id = next_id_++;
        npc.pos = pos;
        npc.prev_pos = pos;
        npc.home_settlement = home_settlement;
        npc.active = true;
        npc.init_by_type(type, rng);
        
        return &npc;
    }
    
    void despawn(NPC* npc)
    {
        if (!npc || !npc->active) return;
        
        for (std::size_t i = 0; i < MAX_NPCS; ++i)
        {
            if (&npcs_[i] == npc)
            {
                npc->reset();
                free_ids_.push_back(i);
                return;
            }
        }
    }
    
    [[nodiscard]] std::size_t active_count() const noexcept
    {
        return MAX_NPCS - free_ids_.size();
    }
    
    void despawn_by_id(std::int32_t id)
    {
        for (std::size_t i = 0; i < MAX_NPCS; ++i)
        {
            if (npcs_[i].active && npcs_[i].id == id)
            {
                npcs_[i].reset();
                free_ids_.push_back(i);
                return;
            }
        }
    }
    
    void update_all(GameContext& ctx, LandmarkSystem& landmarks, 
                    const TerrainType* relief, const std::vector<Cell>& world)
    {
        for (std::size_t i = 0; i < MAX_NPCS; ++i)
        {
            NPC& npc = npcs_[i];
            if (!npc.active) continue;
            
            update_npc(npc, ctx, landmarks, relief, world);
        }
    }
    
    void update_npc(NPC& npc, GameContext& ctx, LandmarkSystem& landmarks,
                    const TerrainType* relief, const std::vector<Cell>& world)
    {
        if (npc.state == NPCState::Dead) return;
        
        if (npc.life <= 0)
        {
            npc.state = NPCState::Dead;
            return;
        }
        
        switch (npc.type)
        {
            case NPCType::Peasant:
                update_peasant(npc, ctx, landmarks, relief, world);
                break;
            case NPCType::Merchant:
            case NPCType::Caravan:
                update_trader(npc, ctx, landmarks, relief, world);
                break;
            case NPCType::Bandit:
                update_bandit(npc, ctx, relief, world);
                break;
            default:
                break;
        }
    }
    
    void update_peasant(NPC& npc, GameContext& ctx, LandmarkSystem& /*landmarks*/,
                        const TerrainType* relief, const std::vector<Cell>& world)
    {
        npc.move_progress += npc.speed;
        
        if (npc.move_progress < 100.0) return;
        npc.move_progress = 0.0;
        
        npc.life -= 1;
        
        const int current_pos = npc.pos;
        const ResourceType local_res = get_local_resource(current_pos, ctx.rng);
        if (local_res != ResourceType::None && npc.inventory.can_add(local_res, 1))
        {
            npc.inventory.add(local_res, 1);
        }
        
        const int dir = randomer(ctx.rng, 3);
        const int next_pos = world[current_pos].side(dir);
        
        if (next_pos >= 0 && next_pos < WORLD_WIDTH * WORLD_WIDTH)
        {
            if (relief[next_pos] != TerrainType::Water && 
                relief[next_pos] != TerrainType::Mount)
            {
                npc.prev_pos = npc.pos;
                npc.pos = next_pos;
            }
        }
    }
    
    void update_trader(NPC& npc, GameContext& ctx, LandmarkSystem& landmarks,
                       const TerrainType* relief, const std::vector<Cell>& world)
    {
        npc.move_progress += npc.speed;
        
        if (npc.state == NPCState::Idle)
        {
            npc.idle_timer++;
            if (npc.idle_timer > 50)
            {
                npc.idle_timer = 0;
                
                if (npc.home_settlement >= 0 && 
                    static_cast<std::size_t>(npc.home_settlement) < landmarks.settlement_count())
                {
                    npc.target_settlement = static_cast<std::int32_t>(
                        landmarks.find_random_destination(
                            static_cast<std::size_t>(npc.home_settlement), ctx.rng));
                    
                    if (npc.target_settlement != npc.home_settlement)
                    {
                        npc.state = NPCState::Traveling;
                        
                        for (std::size_t r = 1; r < RESOURCE_COUNT; ++r)
                        {
                            const auto res = static_cast<ResourceType>(r);
                            const std::int32_t amount = randomer(ctx.rng, 10) + 1;
                            if (npc.inventory.can_add(res, amount))
                            {
                                npc.inventory.add(res, amount);
                            }
                        }
                    }
                }
            }
            return;
        }
        
        if (npc.state == NPCState::Trading)
        {
            npc.trade_timer++;
            if (npc.trade_timer > 30)
            {
                npc.trade_timer = 0;
                npc.state = NPCState::Returning;
                npc.target_settlement = npc.home_settlement;
            }
            return;
        }
        
        if (npc.move_progress < 100.0) return;
        npc.move_progress = 0.0;
        
        const int target_idx = (npc.state == NPCState::Returning) 
            ? npc.home_settlement 
            : npc.target_settlement;
        
        if (target_idx < 0 || static_cast<std::size_t>(target_idx) >= landmarks.settlement_count())
        {
            npc.state = NPCState::Idle;
            return;
        }
        
        const Settlement* target = landmarks.get_settlement(static_cast<std::size_t>(target_idx));
        if (!target)
        {
            npc.state = NPCState::Idle;
            return;
        }
        
        if (npc.pos == target->pos)
        {
            if (npc.state == NPCState::Traveling)
            {
                npc.state = NPCState::Trading;
                npc.trade_timer = 0;
                
                const double profit = npc.inventory.total_value() * 0.2;
                npc.inventory.capital += profit;
                npc.inventory.clear();
            }
            else if (npc.state == NPCState::Returning)
            {
                npc.state = NPCState::Idle;
                npc.idle_timer = 0;
            }
            return;
        }
        
        const int dir = landmarks.get_direction_toward_landmark(
            npc.pos, static_cast<std::size_t>(target_idx), world);
        
        if (dir < 0)
        {
            const int random_dir = randomer(ctx.rng, 3);
            const int next_pos = world[npc.pos].side(random_dir);
            if (next_pos >= 0 && next_pos < WORLD_WIDTH * WORLD_WIDTH &&
                relief[next_pos] != TerrainType::Water &&
                relief[next_pos] != TerrainType::Mount)
            {
                npc.prev_pos = npc.pos;
                npc.pos = next_pos;
            }
            return;
        }
        
        const int next_pos = world[npc.pos].side(dir);
        if (next_pos >= 0 && next_pos < WORLD_WIDTH * WORLD_WIDTH &&
            relief[next_pos] != TerrainType::Water &&
            relief[next_pos] != TerrainType::Mount)
        {
            npc.prev_pos = npc.pos;
            npc.pos = next_pos;
        }
    }
    
    void update_bandit(NPC& npc, GameContext& ctx, 
                       const TerrainType* relief, const std::vector<Cell>& world)
    {
        npc.move_progress += npc.speed;
        
        if (npc.move_progress < 100.0) return;
        npc.move_progress = 0.0;
        
        const int dir = randomer(ctx.rng, 3);
        const int next_pos = world[npc.pos].side(dir);
        
        if (next_pos >= 0 && next_pos < WORLD_WIDTH * WORLD_WIDTH)
        {
            if (relief[next_pos] != TerrainType::Water && 
                relief[next_pos] != TerrainType::Mount)
            {
                npc.prev_pos = npc.pos;
                npc.pos = next_pos;
            }
        }
    }
    
    void rebuild_pos_map(std::unordered_map<int, std::vector<int>>& pos_map) const
    {
        for (std::size_t i = 0; i < MAX_NPCS; ++i)
        {
            const NPC& npc = npcs_[i];
            if (!npc.active || npc.state == NPCState::Dead) continue;
            pos_map[npc.pos].push_back(npc.id);
        }
    }
    
    [[nodiscard]] NPC* find_at(int pos)
    {
        for (std::size_t i = 0; i < MAX_NPCS; ++i)
        {
            if (npcs_[i].active && npcs_[i].pos == pos)
            {
                return &npcs_[i];
            }
        }
        return nullptr;
    }
    
    [[nodiscard]] std::vector<NPC*> find_all_at(int pos)
    {
        std::vector<NPC*> result;
        for (std::size_t i = 0; i < MAX_NPCS; ++i)
        {
            if (npcs_[i].active && npcs_[i].pos == pos)
            {
                result.push_back(&npcs_[i]);
            }
        }
        return result;
    }
    
    [[nodiscard]] NPC& operator[](std::size_t idx) noexcept { return npcs_[idx]; }
    [[nodiscard]] const NPC& operator[](std::size_t idx) const noexcept { return npcs_[idx]; }
    
    template<typename Func>
    void for_each_active(Func&& func)
    {
        NPC* const data = npcs_.get();
        for (std::size_t i = 0; i < MAX_NPCS; ++i)
        {
            if (data[i].active)
            {
                func(data[i]);
            }
        }
    }
    
    template<typename Func>
    void for_each_active(Func&& func) const
    {
        const NPC* const data = npcs_.get();
        for (std::size_t i = 0; i < MAX_NPCS; ++i)
        {
            if (data[i].active)
            {
                func(data[i]);
            }
        }
    }
};

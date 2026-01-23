#pragma once

#include "core/game_context.h"
#include "systems/landmark.h"
#include "systems/npc.h"
#include "systems/player.h"
#include "states/event_state.h"
#include <limits>
#include <istream>
#include <ostream>
#include "core/binary_io.h"

// ECS integration
#include "ecs/world.h"
#include "ecs/systems/combat_system.h"
#include "ecs/systems/ai_system.h"
#include "ecs/systems/movement_system.h"
#include "ecs/systems/spawn_system.h"

class WorldManager
{
public:
    LandmarkSystem landmarks;
    NPCManager npcs;
    PlayerController player_ctrl;
    
    // ECS spatial hash for combat resolution
    ecs::SpatialHash ecs_spatial_hash;
    
    static constexpr int NUM_CITIES = 5;
    static constexpr int NUM_TOWNS = 15;
    static constexpr int NUM_VILLAGES = 30;
    static constexpr int MIN_SETTLEMENT_DISTANCE = 50;
    
    WorldManager() = default;

    void save(std::ostream& out) const
    {
        BinaryWriter writer(out);
        landmarks.save(out);
        npcs.save(out);

        const Player& player = player_ctrl.player();
        writer.write(player);

        const std::int32_t current_settlement = player_ctrl.current_settlement();
        writer.write(current_settlement);
    }

    void load(std::istream& in, GameContext& ctx)
    {
        BinaryReader reader(in);
        landmarks.load(in, &ctx.relief);
        npcs.load(in);

        Player loaded_player = reader.read<Player>();
        player_ctrl.player() = loaded_player;

        std::int32_t current_settlement = reader.read<std::int32_t>();
        player_ctrl.set_current_settlement(current_settlement);
    }
    
    void init()
    {
        landmarks.init();
        npcs.init();
    }
    
    
    void generate_settlements(GameContext& ctx)
    {
        place_settlements(ctx, SettlementType::City, NUM_CITIES);
        place_settlements(ctx, SettlementType::Town, NUM_TOWNS);
        place_settlements(ctx, SettlementType::Village, NUM_VILLAGES);
        
        landmarks.propagate_all_fields(ctx.relief);
    }
    
    void place_settlements(GameContext& ctx, SettlementType type, int count)
    {
        int placed = 0;
        int attempts = 0;
        const int max_attempts = count * 100;
        
        while (placed < count && attempts < max_attempts)
        {
            attempts++;
            
            const auto x = static_cast<std::uint16_t>(random_u32_inclusive(ctx.rng, static_cast<std::uint32_t>(WORLD_WIDTH - 1)));
            const auto y = static_cast<std::uint16_t>(random_u32_inclusive(ctx.rng, static_cast<std::uint32_t>(WORLD_WIDTH - 1)));
            const TilePosition pos_tile{x, y};
            
            if (ctx.relief[pos_tile] != TerrainType::Grass && ctx.relief[pos_tile] != TerrainType::Dirt)
            {
                continue;
            }
            
            if (landmarks.find_settlement_at(pos_tile) != nullptr)
            {
                continue;
            }
            
            bool too_close = false;
            for (const auto& s : landmarks.settlements())
            {
                const double dist = toroidal_distance(pos_tile, s.pos);
                if (dist < static_cast<double>(MIN_SETTLEMENT_DISTANCE))
                {
                    too_close = true;
                    break;
                }
            }
            
            if (too_close) continue;
            
            Settlement* s = landmarks.add_settlement(pos_tile, type, ctx.rng);
            if (s)
            {
                s->name = generate_settlement_name(ctx.rng, type);
                s->faction = FactionID::Kingdom;
                
                ctx.world_map[pos_tile] = get_settlement_color(type);
                
                placed++;
            }
        }
    }
    
    [[nodiscard]] static MapPixel get_settlement_color(SettlementType type) noexcept
    {
        switch (type)
        {
            case SettlementType::City:
                return {255, 255, 255};
            case SettlementType::Town:
                return {200, 200, 200};
            case SettlementType::Village:
                return {150, 150, 150};
            default:
                return {100, 100, 100};
        }
    }
    
    [[nodiscard]] static std::string generate_settlement_name(rng_t& rng, SettlementType type)
    {
        static const char* prefixes[] = {
            "Novo", "Staro", "Veliko", "Malo", "Belo", "Cherno", 
            "Kras", "Dubro", "Zele", "Sini", "Zoloto"
        };
        static const char* roots[] = {
            "grad", "gorod", "pole", "more", "les", "gora", 
            "reka", "dol", "bor", "lug", "stan"
        };
        static const char* suffixes_city[] = {"sk", "burg", "polis", ""};
        static const char* suffixes_town[] = {"ovo", "ino", "ichi", "ki"};
        static const char* suffixes_village[] = {"ka", "tsy", "iki", "ovka"};
        
        const std::size_t prefix_idx = random_u32_inclusive(rng, 10);
        const std::size_t root_idx = random_u32_inclusive(rng, 10);
        
        std::string name = std::string(prefixes[prefix_idx]) + roots[root_idx];
        
        switch (type)
        {
            case SettlementType::City:
                name += suffixes_city[random_u32_inclusive(rng, 3)];
                break;
            case SettlementType::Town:
                name += suffixes_town[random_u32_inclusive(rng, 3)];
                break;
            case SettlementType::Village:
                name += suffixes_village[random_u32_inclusive(rng, 3)];
                break;
            default:
                break;
        }
        
        return name;
    }
    
    // Spawn NPCs directly to ECS (pure ECS approach)
    void spawn_initial_npcs(GameContext& ctx)
    {
        if (!ctx.ecs_world) return;
        
        for (std::size_t i = 0; i < landmarks.settlement_count(); ++i)
        {
            const Settlement* s = landmarks.get_settlement(i);
            if (!s) continue;
            
            int peasant_count = 0;
            int woodcutter_count = 0;
            int caravan_count = 0;
            int merchant_count = 0;
            int guard_count = 0;
            
            switch (s->type)
            {
                case SettlementType::City:
                    peasant_count = 5;
                    woodcutter_count = 2;
                    caravan_count = 3;
                    merchant_count = 2;
                    guard_count = 2;
                    break;
                case SettlementType::Town:
                    peasant_count = 3;
                    woodcutter_count = 1;
                    caravan_count = 2;
                    merchant_count = 1;
                    guard_count = 1;
                    break;
                case SettlementType::Village:
                    peasant_count = 2;
                    woodcutter_count = 1;
                    caravan_count = 1;
                    merchant_count = 0;
                    guard_count = 0;
                    break;
                default:
                    break;
            }
            
            const TilePosition spawn_tile = s->pos;
            const std::int32_t home_idx = static_cast<std::int32_t>(i);
            
            for (int j = 0; j < peasant_count; ++j) {
                ecs::spawn_npc(*ctx.ecs_world, NPCType::Peasant, spawn_tile, home_idx, ctx.rng);
            }
            for (int j = 0; j < woodcutter_count; ++j) {
                ecs::spawn_npc(*ctx.ecs_world, NPCType::Woodcutter, spawn_tile, home_idx, ctx.rng);
            }
            for (int j = 0; j < caravan_count; ++j) {
                ecs::spawn_npc(*ctx.ecs_world, NPCType::Caravan, spawn_tile, home_idx, ctx.rng);
            }
            for (int j = 0; j < merchant_count; ++j) {
                ecs::spawn_npc(*ctx.ecs_world, NPCType::Merchant, spawn_tile, home_idx, ctx.rng);
            }
            for (int j = 0; j < guard_count; ++j) {
                ecs::spawn_npc(*ctx.ecs_world, NPCType::Guard, spawn_tile, home_idx, ctx.rng);
            }
        }
        
        // Spawn bandits near settlements (within 15-30 tiles)
        for (std::size_t i = 0; i < landmarks.settlement_count(); ++i) {
            const Settlement* s = landmarks.get_settlement(i);
            if (!s) continue;
            
            int bandit_count = (s->type == SettlementType::City) ? 4 : 
                               (s->type == SettlementType::Town) ? 3 : 2;
            
            for (int j = 0; j < bandit_count; ++j) {
                // Spawn 15-30 tiles away from settlement
                int offset_x = static_cast<int>(random_u32_inclusive(ctx.rng, 30)) - 15;
                int offset_y = static_cast<int>(random_u32_inclusive(ctx.rng, 30)) - 15;
                if (std::abs(offset_x) < 15) offset_x = (offset_x >= 0) ? 15 : -15;
                if (std::abs(offset_y) < 15) offset_y = (offset_y >= 0) ? 15 : -15;
                
                auto x = static_cast<std::uint16_t>((s->pos.x + offset_x + WORLD_WIDTH) % WORLD_WIDTH);
                auto y = static_cast<std::uint16_t>((s->pos.y + offset_y + WORLD_WIDTH) % WORLD_WIDTH);
                TilePosition pos{x, y};
                
                if (ctx.relief[pos] != TerrainType::Water && ctx.relief[pos] != TerrainType::Mount) {
                    ecs::spawn_npc(*ctx.ecs_world, NPCType::Bandit, pos, -1, ctx.rng);
                }
            }
        }
        
        // Spawn witches near settlements (within 10-20 tiles)
        for (std::size_t i = 0; i < landmarks.settlement_count(); ++i) {
            const Settlement* s = landmarks.get_settlement(i);
            if (!s) continue;
            if (s->type == SettlementType::Village && random_u32_inclusive(ctx.rng, 2) != 0) continue;
            
            int offset_x = static_cast<int>(random_u32_inclusive(ctx.rng, 20)) - 10;
            int offset_y = static_cast<int>(random_u32_inclusive(ctx.rng, 20)) - 10;
            if (std::abs(offset_x) < 10) offset_x = (offset_x >= 0) ? 10 : -10;
            if (std::abs(offset_y) < 10) offset_y = (offset_y >= 0) ? 10 : -10;
            
            auto x = static_cast<std::uint16_t>((s->pos.x + offset_x + WORLD_WIDTH) % WORLD_WIDTH);
            auto y = static_cast<std::uint16_t>((s->pos.y + offset_y + WORLD_WIDTH) % WORLD_WIDTH);
            TilePosition pos{x, y};
            
            if (ctx.relief[pos] != TerrainType::Water && ctx.relief[pos] != TerrainType::Mount) {
                ecs::spawn_npc(*ctx.ecs_world, NPCType::Witch, pos, static_cast<std::int32_t>(i), ctx.rng);
            }
        }
    }
    
    void init_player(GameContext& ctx)
    {
        if (landmarks.settlement_count() > 0)
        {
            const Settlement* start = landmarks.get_settlement(0);
            if (start)
            {
                player_ctrl.init(start->pos, ctx.rng);
                ctx.pos_cam = start->pos;
            }
        }
        else
        {
            const auto x = static_cast<std::uint16_t>(random_u32_inclusive(ctx.rng, static_cast<std::uint32_t>(WORLD_WIDTH - 1)));
            const auto y = static_cast<std::uint16_t>(random_u32_inclusive(ctx.rng, static_cast<std::uint32_t>(WORLD_WIDTH - 1)));
            const TilePosition pos_tile{x, y};
            player_ctrl.init(pos_tile, ctx.rng);
            ctx.pos_cam = pos_tile;
        }
    }
    
    void update(GameContext& ctx)
    {
        landmarks.update_all();
        
        // 1. NPC AI updates via ECS (rendering reads directly from ECS now)
        if (ctx.ecs_world) {
            ecs::update_all_npc_ai(*ctx.ecs_world, ctx.relief, ctx.flora, landmarks, ctx.rng);
        }
        
        // 2. Player movement
        const TilePosition old_pos = player_ctrl.player().pos;
        player_ctrl.update(ctx, landmarks, npcs);
        const TilePosition new_pos = player_ctrl.player().pos;

        // 3. Rebuild position map for collision detection
        if (ctx.ecs_world) {
            ecs::rebuild_pos_map(*ctx.ecs_world, ctx.pos_map);
        }

        // 4. Combat resolution via ECS spatial hash
        if (ctx.ecs_world) {
            ecs::build_spatial_hash(*ctx.ecs_world, ecs_spatial_hash);
            ecs::resolve_combat(*ctx.ecs_world, ecs_spatial_hash, ctx.rng);
        }

        if (old_pos != new_pos && current_game_mode(ctx) == GameMode::Game)
        {
            if (random_u32_inclusive(ctx.rng, 1000) < 5)
            {
                push_state(ctx, std::make_unique<EventState>(EventState::kRandomEvent));
            }
        }
        
        spawn_from_settlements(ctx);
        
        // Cleanup dead entities
        cleanup_dead_npcs();
        if (ctx.ecs_world) {
            ctx.ecs_world->cleanup_dead();
        }
    }
    
    void update_visual_interpolation(GameContext& ctx, float delta_time) {
        if (ctx.ecs_world) {
            ecs::update_visual_interpolation(*ctx.ecs_world, delta_time);
        }
    }
    
    // Spawn NPCs directly to ECS during gameplay
    void spawn_from_settlements(GameContext& ctx)
    {
        if (!ctx.ecs_world) return;
        
        for (auto& s : landmarks.settlements())
        {
            if (s.spawn_count >= s.max_spawn) continue;
            
            const std::uint32_t spawn_chance = random_u32_inclusive(ctx.rng, 1000);
            if (spawn_chance > 5) continue;
            
            NPCType type_to_spawn = NPCType::Peasant;
            const std::uint32_t type_roll = random_u32_inclusive(ctx.rng, 10);
            
            if (type_roll < 2) {
                type_to_spawn = NPCType::Caravan;
            } else if (type_roll < 4) {
                type_to_spawn = NPCType::Merchant;
            } else if (type_roll < 6) {
                type_to_spawn = NPCType::Woodcutter;
            }
            
            ecs::spawn_npc(*ctx.ecs_world, type_to_spawn, s.pos, s.id, ctx.rng);
            s.spawn_count++;
        }
    }
    
    void cleanup_dead_npcs()
    {
        npcs.for_each_active([this](NPC& npc) {
            if (npc.state == NPCState::Dead || npc.life <= 0)
            {
                if (npc.home_settlement_idx >= 0)
                {
                    Settlement* home = landmarks.get_settlement(static_cast<std::size_t>(npc.home_settlement_idx));
                    if (home && home->spawn_count > 0)
                    {
                        home->spawn_count--;
                    }
                }
                npcs.despawn(&npc);
            }
        });
    }
    
    void rebuild_pos_map(WorldMap<std::uint16_t>& pos_map)
    {
        pos_map.fill(0);
        
        for (const auto& s : landmarks.settlements())
        {
            if (!is_valid(s.pos)) continue;
            if (pos_map[s.pos] < std::numeric_limits<std::uint16_t>::max())
            {
                pos_map[s.pos] += 1;
            }
        }
        
        npcs.rebuild_pos_map(pos_map);
        
        if (player_ctrl.player().active)
        {
            const TilePosition player_tile = player_ctrl.player().pos;
            if (is_valid(player_tile) &&
                pos_map[player_tile] < std::numeric_limits<std::uint16_t>::max())
            {
                pos_map[player_tile] += 1;
            }
        }
    }
    
    [[nodiscard]] bool is_settlement_at(TilePosition pos) const
    {
        return landmarks.find_settlement_at(pos) != nullptr;
    }
    
    [[nodiscard]] const Settlement* get_settlement_at(TilePosition pos) const
    {
        return landmarks.find_settlement_at(pos);
    }
    
    [[nodiscard]] Settlement* get_settlement_at(TilePosition pos)
    {
        return landmarks.find_settlement_at(pos);
    }
};

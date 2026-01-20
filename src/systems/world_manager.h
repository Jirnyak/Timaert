#pragma once

#include "core/game_context.h"
#include "systems/landmark.h"
#include "systems/npc.h"
#include "systems/player.h"
#include "systems/entity_manager.h"
#include "systems/economy.h"
#include <algorithm>
#include <limits>
#include <istream>
#include <ostream>
#include "core/binary_io.h"

class WorldManager
{
public:
    LandmarkSystem landmarks;
    NPCManager npcs;
    PlayerController player_ctrl;
    
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
        landmarks.load(in, ctx.relief.get());
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
        
        landmarks.propagate_all_fields(ctx.relief.get());
    }
    
    void place_settlements(GameContext& ctx, SettlementType type, int count)
    {
        int placed = 0;
        int attempts = 0;
        const int max_attempts = count * 100;
        
        while (placed < count && attempts < max_attempts)
        {
            attempts++;
            
            const auto pos = static_cast<int>(random_u32_inclusive(ctx.rng, static_cast<std::uint32_t>(WORLD_SIZE - 1)));
            
            if (ctx.relief[pos] != TerrainType::Grass && ctx.relief[pos] != TerrainType::Dirt)
            {
                continue;
            }
            
            if (landmarks.find_settlement_at(pos) != nullptr)
            {
                continue;
            }
            
            bool too_close = false;
            for (const auto& s : landmarks.settlements())
            {
                const int dist = toroidal_distance(
                    pos / WORLD_WIDTH, pos % WORLD_WIDTH,
                    s.pos / WORLD_WIDTH, s.pos % WORLD_WIDTH
                );
                if (dist < MIN_SETTLEMENT_DISTANCE)
                {
                    too_close = true;
                    break;
                }
            }
            
            if (too_close) continue;
            
            Settlement* s = landmarks.add_settlement(pos, type, ctx.rng);
            if (s)
            {
                s->name = generate_settlement_name(ctx.rng, type);
                s->faction = FactionID::Kingdom;
                
                ctx.world_map[pos] = get_settlement_color(type);
                
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
    
    void spawn_initial_npcs(GameContext& ctx)
    {
        for (std::size_t i = 0; i < landmarks.settlement_count(); ++i)
        {
            const Settlement* s = landmarks.get_settlement(i);
            if (!s) continue;
            
            int peasant_count = 0;
            int woodcutter_count = 0;
            int caravan_count = 0;
            
            switch (s->type)
            {
                case SettlementType::City:
                    peasant_count = 5;
                    woodcutter_count = 2;
                    caravan_count = 3;
                    break;
                case SettlementType::Town:
                    peasant_count = 3;
                    woodcutter_count = 1;
                    caravan_count = 2;
                    break;
                case SettlementType::Village:
                    peasant_count = 2;
                    woodcutter_count = 1;
                    caravan_count = 1;
                    break;
                default:
                    break;
            }
            
            for (int j = 0; j < peasant_count; ++j)
            {
                [[maybe_unused]] NPC* peasant = npcs.spawn(NPCType::Peasant, s->pos, static_cast<int>(i), ctx.rng);
            }

            for (int j = 0; j < woodcutter_count; ++j)
            {
                [[maybe_unused]] NPC* woodcutter = npcs.spawn(NPCType::Woodcutter, s->pos, static_cast<int>(i), ctx.rng);
            }
            
            for (int j = 0; j < caravan_count; ++j)
            {
                NPC* caravan = npcs.spawn(NPCType::Caravan, s->pos, static_cast<int>(i), ctx.rng);
                if (caravan)
                {
                    for (std::size_t r = 1; r < RESOURCE_COUNT; ++r)
                    {
                        const auto res = static_cast<ResourceType>(r);
                        const std::int32_t amount = random_u32_inclusive(ctx.rng, 20) + 5;
                        [[maybe_unused]] bool added = caravan->inventory.add(res, amount);
                    }
                }
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
            const auto pos = static_cast<int>(random_u32_inclusive(ctx.rng, static_cast<std::uint32_t>(WORLD_SIZE - 1)));
            player_ctrl.init(pos, ctx.rng);
            ctx.pos_cam = pos;
        }
    }
    
    void update(GameContext& ctx, EntityManager& entities)
    {
        landmarks.update_all();
        
        // 1. Движение NPC
        npcs.update_all(ctx, landmarks, ctx.relief.get(), entities, player_ctrl.player());
        
        // 2. Движение игрока
        const int old_pos = player_ctrl.player().pos;
        player_ctrl.update(ctx, landmarks, ctx.relief.get(), npcs);
        const int new_pos = player_ctrl.player().pos;

        // 3. Обновляем карту позиций для расчета столкновений
        rebuild_pos_map(ctx.pos_map);

        // 4. Расчет сражений NPC vs NPC
        npcs.resolve_npc_combat(ctx);

       if (old_pos != new_pos && ctx.game_mod == GameMode::Game)
        {
            // Шанс события: 5 из 1000 (0.5%) на каждый шаг
            if (random_u32_inclusive(ctx.rng, 1000) < 5)
            {
                // Устанавливаем -2 как сигнал для EventState выбрать случайное событие
                ctx.active_event_id = -2;
                ctx.game_mod = GameMode::Event;
            }
        }
        
        spawn_from_settlements(ctx);
        
        cleanup_dead_npcs();
    }
    
    void spawn_from_settlements(GameContext& ctx)
    {
        for (auto& s : landmarks.settlements())
        {
            if (s.spawn_count >= s.max_spawn) continue;
            
            const std::uint32_t spawn_chance = random_u32_inclusive(ctx.rng, 1000);
            if (spawn_chance > 5) continue;
            
            NPCType type_to_spawn = NPCType::Peasant;
            const std::uint32_t type_roll = random_u32_inclusive(ctx.rng, 10);
            
            if (type_roll < 2)
            {
                type_to_spawn = NPCType::Caravan;
            }
            else if (type_roll < 4)
            {
                type_to_spawn = NPCType::Merchant;
            }
            else if (type_roll < 6)
            {
                type_to_spawn = NPCType::Woodcutter;
            }
            
            NPC* npc = npcs.spawn(type_to_spawn, s.pos, s.id, ctx.rng);
            if (npc)
            {
                s.spawn_count++;
                
                if (type_to_spawn == NPCType::Caravan || type_to_spawn == NPCType::Merchant)
                {
                    for (std::size_t r = 1; r < RESOURCE_COUNT; ++r)
                    {
                        const auto res = static_cast<ResourceType>(r);
                        const std::int32_t amount = random_u32_inclusive(ctx.rng, 10) + 1;
                        if (npc->inventory.can_add(res, amount))
                        {
                            [[maybe_unused]] bool added = npc->inventory.add(res, amount);
                        }
                    }
                }
            }
        }
    }
    
    void cleanup_dead_npcs()
    {
        npcs.for_each_active([this](NPC& npc) {
            if (npc.state == NPCState::Dead || npc.life <= 0)
            {
                if (npc.home_settlement >= 0)
                {
                    Settlement* home = landmarks.get_settlement(static_cast<std::size_t>(npc.home_settlement));
                    if (home && home->spawn_count > 0)
                    {
                        home->spawn_count--;
                    }
                }
                npcs.despawn(&npc);
            }
        });
    }
    
    void rebuild_pos_map(std::vector<std::uint16_t>& pos_map)
    {
        std::fill(pos_map.begin(), pos_map.end(), 0);
        
        for (const auto& s : landmarks.settlements())
        {
            if (s.pos < 0 || static_cast<std::size_t>(s.pos) >= pos_map.size()) continue;
            if (pos_map[s.pos] < std::numeric_limits<std::uint16_t>::max())
            {
                pos_map[s.pos] += 1;
            }
        }
        
        npcs.rebuild_pos_map(pos_map);
        
        if (player_ctrl.player().active)
        {
            const int pos = player_ctrl.player().pos;
            if (pos >= 0 && static_cast<std::size_t>(pos) < pos_map.size() &&
                pos_map[pos] < std::numeric_limits<std::uint16_t>::max())
            {
                pos_map[pos] += 1;
            }
        }
    }
    
    [[nodiscard]] bool is_settlement_at(int pos) const
    {
        return landmarks.find_settlement_at(pos) != nullptr;
    }
    
    [[nodiscard]] const Settlement* get_settlement_at(int pos) const
    {
        return landmarks.find_settlement_at(pos);
    }
    
    [[nodiscard]] Settlement* get_settlement_at(int pos)
    {
        return landmarks.find_settlement_at(pos);
    }
};

#pragma once

#include "ecs/world.h"
#include "ecs/components/core.h"
#include "ecs/components/npc.h"
#include "ecs/components/player.h"
#include "ecs/components/entity.h"
#include "ecs/systems/movement_system.h"
#include "systems/landmark.h"

namespace ecs {

// Helper: get direction toward target position
[[nodiscard]] inline Direction get_direction_toward(TilePosition from, TilePosition to) {
    int dx = static_cast<int>(to.x) - static_cast<int>(from.x);
    int dy = static_cast<int>(to.y) - static_cast<int>(from.y);
    
    // Handle toroidal wrap
    if (dx > WORLD_WIDTH / 2) dx -= WORLD_WIDTH;
    if (dx < -WORLD_WIDTH / 2) dx += WORLD_WIDTH;
    if (dy > WORLD_WIDTH / 2) dy -= WORLD_WIDTH;
    if (dy < -WORLD_WIDTH / 2) dy += WORLD_WIDTH;
    
    if (std::abs(dx) > std::abs(dy)) {
        return dx > 0 ? Direction::Right : Direction::Left;
    } else {
        return dy > 0 ? Direction::Down : Direction::Up;
    }
}

inline void update_peasant_ai(World& world, const WorldMap<TerrainType>& relief, rng_t& rng) {
    auto view = world.registry.view<Position, PreviousPosition, Speed, AIBehavior, 
                                     PeasantTag, Active>(entt::exclude<Dead>);
    
    for (auto entity : view) {
        auto& pos = view.get<Position>(entity);
        auto& prev = view.get<PreviousPosition>(entity);
        auto& speed = view.get<Speed>(entity);
        
        speed.progress += speed.base;
        if (speed.progress < 100.0) continue;
        speed.progress = 0.0;
        
        if (world.registry.all_of<InventoryComponent>(entity)) {
            auto& inv = world.registry.get<InventoryComponent>(entity);
            if (random_u32_inclusive(rng, 100) < 10) {
                if (relief[pos.tile] == TerrainType::Grass) {
                    inv.data.add(ResourceType::Grain, 1);
                }
            }
        }
        
        Direction dir = static_cast<Direction>(random_u32_inclusive(rng, 3));
        try_move(pos, prev, dir, relief);
    }
}

[[nodiscard]] inline TilePosition find_nearest_tree(World& world, TilePosition from, 
                                                     entt::entity exclude_woodcutter) {
    TilePosition best = INVALID_POS;
    double best_dist = 9999.0;
    
    auto tree_view = world.registry.view<Position, ObjectSprite, Active>();
    auto woodcutter_view = world.registry.view<WoodcutterWork, WoodcutterTag, Active>();
    
    for (auto tree_entity : tree_view) {
        const auto& tree_pos = tree_view.get<Position>(tree_entity);
        const auto& sprite = tree_view.get<ObjectSprite>(tree_entity);
        if (sprite.type != ObjectType::Tree) continue;
        if (!is_valid(tree_pos.tile)) continue;
        
        double dist = toroidal_distance(from, tree_pos.tile);
        if (dist >= best_dist || dist > 50.0) continue;
        
        bool claimed = false;
        for (auto wc_entity : woodcutter_view) {
            const auto& wc_work = woodcutter_view.get<WoodcutterWork>(wc_entity);
            if (wc_entity == exclude_woodcutter) continue;
            if (wc_work.target_tree == tree_pos.tile) {
                claimed = true;
                break;
            }
        }
        
        if (!claimed) {
            best = tree_pos.tile;
            best_dist = dist;
        }
    }
    
    return best;
}

[[nodiscard]] inline bool is_tree_at(World& world, TilePosition pos) {
    auto view = world.registry.view<Position, ObjectSprite, Active>();
    for (auto entity : view) {
        const auto& tree_pos = view.get<Position>(entity);
        const auto& sprite = view.get<ObjectSprite>(entity);
        if (sprite.type == ObjectType::Tree && tree_pos.tile == pos) {
            return true;
        }
    }
    return false;
}

inline void remove_tree_at(World& world, TilePosition pos, WorldMap<std::uint8_t>& flora) {
    auto view = world.registry.view<Position, ObjectSprite, Active>();
    for (auto entity : view) {
        const auto& tree_pos = view.get<Position>(entity);
        const auto& sprite = view.get<ObjectSprite>(entity);
        if (sprite.type == ObjectType::Tree && tree_pos.tile == pos) {
            world.mark_dead(entity);
            flora[pos] = 0;
            return;
        }
    }
}

inline void update_woodcutter_ai(World& world, const WorldMap<TerrainType>& relief,
                                  WorldMap<std::uint8_t>& flora,
                                  LandmarkSystem& landmarks, rng_t& rng) {
    auto view = world.registry.view<Position, PreviousPosition, Speed, AIBehavior, 
                                     WoodcutterWork, SettlementLink, WoodcutterTag, Active>(entt::exclude<Dead>);
    
    for (auto entity : view) {
        auto& pos = view.get<Position>(entity);
        auto& prev = view.get<PreviousPosition>(entity);
        auto& speed = view.get<Speed>(entity);
        auto& ai = view.get<AIBehavior>(entity);
        auto& work = view.get<WoodcutterWork>(entity);
        auto& link = view.get<SettlementLink>(entity);
        
        speed.progress += speed.base;
        if (speed.progress < 100.0) continue;
        speed.progress = 0.0;
        
        auto move_random = [&]() {
            Direction dir = static_cast<Direction>(random_u32_inclusive(rng, 3));
            try_move(pos, prev, dir, relief);
        };
        
        if (is_valid(work.target_tree) && !is_tree_at(world, work.target_tree)) {
            work.target_tree = INVALID_POS;
            ai.action_timer = 0;
            ai.state = NPCState::Idle;
        }
        
        if (!is_valid(work.target_tree) && ai.state != NPCState::Returning) {
            work.target_tree = find_nearest_tree(world, pos.tile, entity);
            ai.action_timer = 0;
        }
        
        if (!is_valid(work.target_tree) && ai.state != NPCState::Returning) {
            move_random();
            continue;
        }
        
        if (pos.tile == work.target_tree) {
            ai.state = NPCState::Cutting;
            ai.action_timer++;
            
            if (ai.action_timer >= 40) {
                ai.action_timer = 0;
                
                remove_tree_at(world, work.target_tree, flora);
                
                if (world.registry.all_of<InventoryComponent>(entity)) {
                    auto& inv = world.registry.get<InventoryComponent>(entity);
                    inv.data.add(ResourceType::Wood, 1);
                }
                
                work.target_tree = INVALID_POS;
                ai.state = NPCState::Returning;
            }
            continue;
        }
        
        if (ai.state == NPCState::Returning) {
            if (link.has_home() && 
                static_cast<std::size_t>(link.home_idx) < landmarks.settlement_count()) {
                auto opt_dir = landmarks.get_direction_toward_landmark(pos.tile, static_cast<std::size_t>(link.home_idx));
                if (opt_dir) {
                    try_move(pos, prev, *opt_dir, relief);
                }
                
                const Settlement* home = landmarks.get_settlement(static_cast<std::size_t>(link.home_idx));
                if (home && toroidal_distance(pos.tile, home->pos) < 3.0) {
                    ai.state = NPCState::Idle;
                    
                    if (world.registry.all_of<InventoryComponent>(entity)) {
                        auto& inv = world.registry.get<InventoryComponent>(entity);
                        inv.data.remove(ResourceType::Wood, inv.data.get(ResourceType::Wood));
                    }
                }
            } else {
                ai.state = NPCState::Idle;
            }
            continue;
        }
        
        ai.state = NPCState::Traveling;
        Direction dir = get_direction_toward(pos.tile, work.target_tree);
        try_move(pos, prev, dir, relief);
    }
}

inline void update_bandit_ai(World& world, const WorldMap<TerrainType>& relief, 
                              TilePosition player_pos, rng_t& rng) {
    auto view = world.registry.view<Position, PreviousPosition, Speed, AIBehavior, 
                                     BanditTag, Active>(entt::exclude<Dead>);
    
    for (auto entity : view) {
        auto& pos = view.get<Position>(entity);
        auto& prev = view.get<PreviousPosition>(entity);
        auto& speed = view.get<Speed>(entity);
        
        speed.progress += speed.base;
        if (speed.progress < 100.0) continue;
        speed.progress = 0.0;
        
        bool target_found = false;
        
        if (is_valid(player_pos)) {
            double dist = toroidal_distance(pos.tile, player_pos);
            if (dist < 10.0) {
                Direction dir = get_direction_toward(pos.tile, player_pos);
                try_move(pos, prev, dir, relief);
                target_found = true;
            }
        }
        
        if (!target_found) {
            Direction dir = static_cast<Direction>(random_u32_inclusive(rng, 3));
            try_move(pos, prev, dir, relief);
        }
    }
}

inline void update_guard_ai(World& world, const WorldMap<TerrainType>& relief,
                             LandmarkSystem& landmarks, rng_t& rng) {
    auto view = world.registry.view<Position, PreviousPosition, Speed, AIBehavior, 
                                     SettlementLink, GuardTag, Active>(entt::exclude<Dead>);
    
    for (auto entity : view) {
        auto& pos = view.get<Position>(entity);
        auto& prev = view.get<PreviousPosition>(entity);
        auto& speed = view.get<Speed>(entity);
        auto& link = view.get<SettlementLink>(entity);
        
        speed.progress += speed.base;
        if (speed.progress < 100.0) continue;
        speed.progress = 0.0;
        
        if (link.has_home() && 
            static_cast<std::size_t>(link.home_idx) < landmarks.settlement_count()) {
            const Settlement* home = landmarks.get_settlement(static_cast<std::size_t>(link.home_idx));
            if (!home) continue;
            double dist = toroidal_distance(pos.tile, home->pos);
            
            if (dist > 8.0) {
                auto opt_dir = landmarks.get_direction_toward_landmark(pos.tile, static_cast<std::size_t>(link.home_idx));
                if (opt_dir) {
                    try_move(pos, prev, *opt_dir, relief);
                }
            } else {
                Direction dir = static_cast<Direction>(random_u32_inclusive(rng, 3));
                try_move(pos, prev, dir, relief);
            }
        } else {
            Direction dir = static_cast<Direction>(random_u32_inclusive(rng, 3));
            try_move(pos, prev, dir, relief);
        }
    }
}

inline void update_caravan_ai(World& world, const WorldMap<TerrainType>& relief,
                               LandmarkSystem& landmarks, rng_t& rng) {
    auto view = world.registry.view<Position, PreviousPosition, Speed, AIBehavior, 
                                     SettlementLink, CaravanTag, Active>(entt::exclude<Dead>);
    
    for (auto entity : view) {
        auto& pos = view.get<Position>(entity);
        auto& prev = view.get<PreviousPosition>(entity);
        auto& speed = view.get<Speed>(entity);
        auto& ai = view.get<AIBehavior>(entity);
        auto& link = view.get<SettlementLink>(entity);
        
        speed.progress += speed.base;
        if (speed.progress < 100.0) continue;
        speed.progress = 0.0;
        
        if (ai.state == NPCState::Idle) {
            // Caravans should immediately start traveling
            if (landmarks.settlement_count() > 1) {
                std::int32_t target;
                do {
                    target = static_cast<std::int32_t>(
                        random_u32_inclusive(rng, 
                            static_cast<std::uint32_t>(landmarks.settlement_count() - 1)));
                } while (target == link.home_idx && landmarks.settlement_count() > 1);
                
                link.target_idx = target;
                ai.state = NPCState::Traveling;
            }
        } else if (ai.state == NPCState::Traveling) {
            if (link.has_target() && 
                static_cast<std::size_t>(link.target_idx) < landmarks.settlement_count()) {
                auto opt_dir = landmarks.get_direction_toward_landmark(pos.tile, static_cast<std::size_t>(link.target_idx));
                if (opt_dir) {
                    try_move(pos, prev, *opt_dir, relief);
                }
                
                const Settlement* target = landmarks.get_settlement(static_cast<std::size_t>(link.target_idx));
                if (target && toroidal_distance(pos.tile, target->pos) < 3.0) {
                    ai.state = NPCState::Trading;
                    ai.action_timer = 0;
                }
            } else {
                ai.state = NPCState::Idle;
            }
        } else if (ai.state == NPCState::Trading) {
            ai.action_timer++;
            if (ai.action_timer > 30) {
                ai.action_timer = 0;
                link.home_idx = link.target_idx;
                
                if (landmarks.settlement_count() > 1) {
                    std::int32_t new_target;
                    do {
                        new_target = static_cast<std::int32_t>(
                            random_u32_inclusive(rng, 
                                static_cast<std::uint32_t>(landmarks.settlement_count() - 1)));
                    } while (new_target == link.home_idx && landmarks.settlement_count() > 1);
                    
                    link.target_idx = new_target;
                    ai.state = NPCState::Traveling;
                } else {
                    ai.state = NPCState::Idle;
                }
            }
        } else {
            ai.state = NPCState::Idle;
        }
    }
}

inline void update_merchant_ai(World& world, const WorldMap<TerrainType>& relief,
                                LandmarkSystem& landmarks, rng_t& rng) {
    auto view = world.registry.view<Position, PreviousPosition, Speed, AIBehavior, 
                                     SettlementLink, MerchantTag, Active>(entt::exclude<Dead>);
    
    for (auto entity : view) {
        auto& pos = view.get<Position>(entity);
        auto& prev = view.get<PreviousPosition>(entity);
        auto& speed = view.get<Speed>(entity);
        auto& ai = view.get<AIBehavior>(entity);
        auto& link = view.get<SettlementLink>(entity);
        
        speed.progress += speed.base;
        if (speed.progress < 100.0) continue;
        speed.progress = 0.0;
        
        if (ai.state == NPCState::Idle) {
            // Merchants should start traveling immediately
            if (landmarks.settlement_count() > 1) {
                std::int32_t target;
                do {
                    target = static_cast<std::int32_t>(
                        random_u32_inclusive(rng, 
                            static_cast<std::uint32_t>(landmarks.settlement_count() - 1)));
                } while (target == link.home_idx && landmarks.settlement_count() > 1);
                
                link.target_idx = target;
                ai.state = NPCState::Traveling;
            }
        } else if (ai.state == NPCState::Traveling) {
            if (link.has_target() && 
                static_cast<std::size_t>(link.target_idx) < landmarks.settlement_count()) {
                auto opt_dir = landmarks.get_direction_toward_landmark(pos.tile, static_cast<std::size_t>(link.target_idx));
                if (opt_dir) {
                    try_move(pos, prev, *opt_dir, relief);
                }
                
                const Settlement* target = landmarks.get_settlement(static_cast<std::size_t>(link.target_idx));
                if (target && toroidal_distance(pos.tile, target->pos) < 3.0) {
                    ai.state = NPCState::Trading;
                    ai.action_timer = 0;
                }
            } else {
                ai.state = NPCState::Idle;
            }
        } else if (ai.state == NPCState::Trading) {
            ai.action_timer++;
            if (ai.action_timer > 20) {
                ai.action_timer = 0;
                ai.state = NPCState::Returning;
            }
        } else if (ai.state == NPCState::Returning) {
            if (link.has_home() && 
                static_cast<std::size_t>(link.home_idx) < landmarks.settlement_count()) {
                auto opt_dir = landmarks.get_direction_toward_landmark(pos.tile, static_cast<std::size_t>(link.home_idx));
                if (opt_dir) {
                    try_move(pos, prev, *opt_dir, relief);
                }
                
                const Settlement* home = landmarks.get_settlement(static_cast<std::size_t>(link.home_idx));
                if (home && toroidal_distance(pos.tile, home->pos) < 3.0) {
                    ai.state = NPCState::Idle;
                    link.target_idx = -1;
                }
            } else {
                ai.state = NPCState::Idle;
            }
        } else {
            ai.state = NPCState::Idle;
        }
    }
}

inline TilePosition get_player_position(World& world) {
    auto view = world.registry.view<Position, PlayerTag, Active>();
    for (auto entity : view) {
        const auto& pos = view.get<Position>(entity);
        return pos.tile;
    }
    return INVALID_POS;
}

inline void update_witch_ai(World& world, const WorldMap<TerrainType>& relief, rng_t& rng) {
    auto view = world.registry.view<Position, PreviousPosition, Speed, AIBehavior, 
                                     WitchTag, Active>(entt::exclude<Dead>);
    
    for (auto entity : view) {
        auto& pos = view.get<Position>(entity);
        auto& prev = view.get<PreviousPosition>(entity);
        auto& speed = view.get<Speed>(entity);
        
        speed.progress += speed.base;
        if (speed.progress < 100.0) continue;
        speed.progress = 0.0;
        
        // Witches wander randomly in their area
        Direction dir = static_cast<Direction>(random_u32_inclusive(rng, 3));
        try_move(pos, prev, dir, relief);
    }
}

inline void update_all_npc_ai(World& world, const WorldMap<TerrainType>& relief,
                               WorldMap<std::uint8_t>& flora,
                               LandmarkSystem& landmarks, rng_t& rng) {
    TilePosition player_pos = get_player_position(world);
    
    update_peasant_ai(world, relief, rng);
    update_woodcutter_ai(world, relief, flora, landmarks, rng);
    update_caravan_ai(world, relief, landmarks, rng);
    update_merchant_ai(world, relief, landmarks, rng);
    update_guard_ai(world, relief, landmarks, rng);
    update_bandit_ai(world, relief, player_pos, rng);
    update_witch_ai(world, relief, rng);
}

} // namespace ecs

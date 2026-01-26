#include "systems/world_manager.h"
#include "systems/landmark.h"
#include "core/game_state.h"
#include "core/types.h"
#include <SDL_log.h>
#include <vector>
#include "core/binary_io.h"
#include "states/event_state.h"
#include "ecs/world.h"
#include "ecs/systems/ai_system.h"
#include "ecs/systems/spawn_system.h"
#include <cstdlib>
#include <limits>
#include <memory>

void WorldManager::save(std::ostream& out) const {
    BinaryWriter writer(out);
    landmarks.save(out);

    politics.save(out);

    const Player& player = player_ctrl.player();
    writer.write(player);

    const std::int32_t current_settlement = player_ctrl.current_settlement();
    writer.write(current_settlement);
}

void WorldManager::load(std::istream& in, GameContext& ctx) {
    BinaryReader reader(in);
    landmarks.load(in, &ctx.relief);

    politics.load(in);

    Player loaded_player = reader.read<Player>();
    player_ctrl.player() = loaded_player;

    std::int32_t current_settlement = reader.read<std::int32_t>();
    player_ctrl.set_current_settlement(current_settlement);
}

void WorldManager::init() {
    landmarks.init();
}

void WorldManager::generate_settlements(GameContext& ctx, politics::PoliticsSystem* politics_sys) {
    if (politics_sys) {
        place_faction_capitals(ctx, politics_sys);
    }

    place_faction_settlements(ctx, SettlementType::City, NUM_CITIES - 8);
    place_faction_settlements(ctx, SettlementType::Town, NUM_TOWNS);
    place_faction_settlements(ctx, SettlementType::Village, NUM_VILLAGES);

    landmarks.propagate_all_fields(ctx.relief);
}

MapPixel WorldManager::get_settlement_color(SettlementType type) noexcept {
    switch (type) {
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

void WorldManager::place_faction_capitals(GameContext& ctx,
                                          politics::PoliticsSystem* politics_sys) noexcept {
    static constexpr int NUM_FACTIONS = 8;

    for (int faction_idx = 0; faction_idx < NUM_FACTIONS; ++faction_idx) {
        const FactionID faction_id = static_cast<FactionID>(faction_idx + 1);
        if (faction_id >= FactionID::Wilderness)
            break;

        int attempts = 0;
        const int max_attempts = 100;

        while (attempts < max_attempts) {
            attempts++;

            const auto x = static_cast<std::uint16_t>(
                random_u32_inclusive(ctx.rng, static_cast<std::uint32_t>(WORLD_WIDTH - 1)));
            const auto y = static_cast<std::uint16_t>(
                random_u32_inclusive(ctx.rng, static_cast<std::uint32_t>(WORLD_WIDTH - 1)));
            const TilePosition pos_tile{x, y};

            if (ctx.relief[pos_tile] != TerrainType::Grass
                && ctx.relief[pos_tile] != TerrainType::Dirt) {
                continue;
            }

            if (landmarks.find_settlement_at(pos_tile) != nullptr) {
                continue;
            }

            bool too_close = false;
            for (const auto& s : landmarks.settlements()) {
                const double dist = toroidal_distance(pos_tile, s.pos);
                if (dist < static_cast<double>(MIN_SETTLEMENT_DISTANCE)) {
                    too_close = true;
                    break;
                }
            }

            if (too_close)
                continue;

            Settlement* s = landmarks.add_settlement(pos_tile, SettlementType::City, ctx.rng);
            if (s) {
                s->name = generate_settlement_name(ctx.rng, SettlementType::City);
                s->faction = faction_id;
                ctx.world_map[pos_tile] = get_settlement_color(SettlementType::City);

                if (politics_sys) {
                    politics::Faction* f = politics_sys->get_faction(faction_id);
                    if (f) {
                        f->capital_pos = pos_tile;
                    }
                }
                break;
            }
        }
    }
}

void WorldManager::place_faction_settlements(GameContext& ctx, SettlementType type, int count) {
    int placed = 0;
    int attempts = 0;
    const int max_attempts = count * 100;

    while (placed < count && attempts < max_attempts) {
        attempts++;

        const auto x = static_cast<std::uint16_t>(
            random_u32_inclusive(ctx.rng, static_cast<std::uint32_t>(WORLD_WIDTH - 1)));
        const auto y = static_cast<std::uint16_t>(
            random_u32_inclusive(ctx.rng, static_cast<std::uint32_t>(WORLD_WIDTH - 1)));
        const TilePosition pos_tile{x, y};

        if (ctx.relief[pos_tile] != TerrainType::Grass
            && ctx.relief[pos_tile] != TerrainType::Dirt) {
            continue;
        }

        if (landmarks.find_settlement_at(pos_tile) != nullptr) {
            continue;
        }

        bool too_close = false;
        for (const auto& s : landmarks.settlements()) {
            const double dist = toroidal_distance(pos_tile, s.pos);
            if (dist < static_cast<double>(MIN_SETTLEMENT_DISTANCE)) {
                too_close = true;
                break;
            }
        }

        if (too_close)
            continue;

        Settlement* s = landmarks.add_settlement(pos_tile, type, ctx.rng);
        if (s) {
            s->name = generate_settlement_name(ctx.rng, type);

            const FactionID owner = static_cast<FactionID>(ctx.owner[pos_tile]);
            if (owner > FactionID::Neutral && owner < FactionID::Wilderness) {
                s->faction = owner;
            } else {
                s->faction = FactionID::Faction1;
            }

            ctx.world_map[pos_tile] = get_settlement_color(type);

            placed++;
        }
    }
}

std::string WorldManager::generate_settlement_name(rng_t& rng, SettlementType type) {
    static const char* prefixes[] = {"Novo",
                                     "Staro",
                                     "Veliko",
                                     "Malo",
                                     "Belo",
                                     "Cherno",
                                     "Kras",
                                     "Dubro",
                                     "Zele",
                                     "Sini",
                                     "Zoloto"};
    static const char* roots[] =
        {"grad", "gorod", "pole", "more", "les", "gora", "reka", "dol", "bor", "lug", "stan"};
    static const char* suffixes_city[] = {"sk", "burg", "polis", ""};
    static const char* suffixes_town[] = {"ovo", "ino", "ichi", "ki"};
    static const char* suffixes_village[] = {"ka", "tsy", "iki", "ovka"};

    const std::size_t prefix_idx = random_u32_inclusive(rng, 10);
    const std::size_t root_idx = random_u32_inclusive(rng, 10);

    std::string name = std::string(prefixes[prefix_idx]) + roots[root_idx];

    switch (type) {
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

void WorldManager::spawn_initial_npcs(GameContext& ctx) {
    if (!ctx.ecs_world)
        return;

    for (std::size_t i = 0; i < landmarks.settlement_count(); ++i) {
        const Settlement* s = landmarks.get_settlement(i);
        if (!s)
            continue;

        int peasant_count = 0;
        int woodcutter_count = 0;
        int caravan_count = 0;
        int merchant_count = 0;
        int guard_count = 0;

        switch (s->type) {
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

    for (std::size_t i = 0; i < landmarks.settlement_count(); ++i) {
        const Settlement* s = landmarks.get_settlement(i);
        if (!s)
            continue;

        int bandit_count = (s->type == SettlementType::City)   ? 4
                           : (s->type == SettlementType::Town) ? 3
                                                               : 2;

        for (int j = 0; j < bandit_count; ++j) {
            int offset_x = static_cast<int>(random_u32_inclusive(ctx.rng, 30)) - 15;
            int offset_y = static_cast<int>(random_u32_inclusive(ctx.rng, 30)) - 15;
            if (std::abs(offset_x) < 15)
                offset_x = (offset_x >= 0) ? 15 : -15;
            if (std::abs(offset_y) < 15)
                offset_y = (offset_y >= 0) ? 15 : -15;

            auto x = static_cast<std::uint16_t>((s->pos.x + offset_x + WORLD_WIDTH) % WORLD_WIDTH);
            auto y = static_cast<std::uint16_t>((s->pos.y + offset_y + WORLD_WIDTH) % WORLD_WIDTH);
            TilePosition pos{x, y};

            if (ctx.relief[pos] != TerrainType::Water && ctx.relief[pos] != TerrainType::Mount) {
                ecs::spawn_npc(*ctx.ecs_world, NPCType::Bandit, pos, -1, ctx.rng);
            }
        }
    }

    for (std::size_t i = 0; i < landmarks.settlement_count(); ++i) {
        const Settlement* s = landmarks.get_settlement(i);
        if (!s)
            continue;
        if (s->type == SettlementType::Village && random_u32_inclusive(ctx.rng, 2) != 0)
            continue;

        int offset_x = static_cast<int>(random_u32_inclusive(ctx.rng, 20)) - 10;
        int offset_y = static_cast<int>(random_u32_inclusive(ctx.rng, 20)) - 10;
        if (std::abs(offset_x) < 10)
            offset_x = (offset_x >= 0) ? 10 : -10;
        if (std::abs(offset_y) < 10)
            offset_y = (offset_y >= 0) ? 10 : -10;

        auto x = static_cast<std::uint16_t>((s->pos.x + offset_x + WORLD_WIDTH) % WORLD_WIDTH);
        auto y = static_cast<std::uint16_t>((s->pos.y + offset_y + WORLD_WIDTH) % WORLD_WIDTH);
        TilePosition pos{x, y};

        if (ctx.relief[pos] != TerrainType::Water && ctx.relief[pos] != TerrainType::Mount) {
            ecs::spawn_npc(*ctx.ecs_world,
                           NPCType::Witch,
                           pos,
                           static_cast<std::int32_t>(i),
                           ctx.rng);
        }
    }
}

void WorldManager::init_player(GameContext& ctx) {
    if (landmarks.settlement_count() > 0) {
        const Settlement* start = landmarks.get_settlement(0);
        if (start) {
            player_ctrl.init(start->pos, ctx.rng);
            ctx.pos_cam = start->pos;
        }
    } else {
        const auto x = static_cast<std::uint16_t>(
            random_u32_inclusive(ctx.rng, static_cast<std::uint32_t>(WORLD_WIDTH - 1)));
        const auto y = static_cast<std::uint16_t>(
            random_u32_inclusive(ctx.rng, static_cast<std::uint32_t>(WORLD_WIDTH - 1)));
        const TilePosition pos_tile{x, y};
        player_ctrl.init(pos_tile, ctx.rng);
        ctx.pos_cam = pos_tile;
    }
}

void WorldManager::update(GameContext& ctx) {
    landmarks.update_all();

    if (ctx.ticks() > 0 && ctx.ticks() % (TICKS_PER_DAY * 30) == 0) {
        politics.update_monthly(ctx);
        SDL_Log("ECONOMY: Monthly taxes collected and population grew.");
    }

    if (ctx.ecs_world) {
        ecs::update_all_npc_ai(*ctx.ecs_world, ctx.relief, ctx.flora, landmarks, ctx.rng);
    }

    const TilePosition old_pos = player_ctrl.player().pos;
    player_ctrl.update(ctx, landmarks);
    const TilePosition new_pos = player_ctrl.player().pos;

    if (ctx.ecs_world) {
        ecs::rebuild_pos_map(*ctx.ecs_world, ctx.pos_map);
    }

    if (ctx.ecs_world) {
        ecs::build_spatial_hash(*ctx.ecs_world, ecs_spatial_hash);
        ecs::resolve_combat(*ctx.ecs_world, ecs_spatial_hash, ctx.rng);
    }

    if (old_pos != new_pos && current_game_mode(ctx) == GameMode::Game) {
        if (random_u32_inclusive(ctx.rng, 1000) < 5) {
            push_state(ctx, std::make_unique<EventState>(EventState::kRandomEvent));
        }
    }

    spawn_from_settlements(ctx);

    if (ctx.ecs_world) {
        ctx.ecs_world->cleanup_dead();
    }
}

void WorldManager::spawn_from_settlements(GameContext& ctx) {
    if (!ctx.ecs_world)
        return;

    for (auto& s : landmarks.settlements()) {
        if (s.spawn_count >= s.max_spawn)
            continue;

        const std::uint32_t spawn_chance = random_u32_inclusive(ctx.rng, 1000);
        if (spawn_chance > 5)
            continue;

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

void WorldManager::rebuild_pos_map(WorldMap<std::uint16_t>& pos_map) {
    pos_map.fill(0);

    for (const auto& s : landmarks.settlements()) {
        if (!is_valid(s.pos))
            continue;
        if (pos_map[s.pos] < std::numeric_limits<std::uint16_t>::max()) {
            pos_map[s.pos] += 1;
        }
    }

    if (player_ctrl.player().active) {
        const TilePosition player_tile = player_ctrl.player().pos;
        if (is_valid(player_tile)
            && pos_map[player_tile] < std::numeric_limits<std::uint16_t>::max()) {
            pos_map[player_tile] += 1;
        }
    }
}

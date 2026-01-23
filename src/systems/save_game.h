#pragma once

#include <cstdint>
#include <fstream>
#include <string_view>
#include <algorithm>
#include <vector>
#include "core/binary_io.h"

#include "systems/entity_manager.h"
#include "core/game_context.h"
#include "core/game_state.h"
#include "systems/world_manager.h"

namespace save_game
{
struct SaveHeader {
    std::uint32_t magic = 0;
    std::uint32_t version = 0;
};

struct ViewState {
    float zoom = 1.0f;
    float target_zoom = 1.0f;
    float map_offset_x = 0.0f;
    float map_offset_y = 0.0f;
    std::uint16_t pos_cam_x = 0;
    std::uint16_t pos_cam_y = 0;
    std::uint64_t hour = 0;
};

constexpr std::uint32_t kSaveMagic = 0x53415645; // 'SAVE'
// ВЕРСИЯ 9: Entity.pos и ViewState.pos_cam используют TilePosition (uint16_t x/y) вместо int index
constexpr std::uint32_t kSaveVersion = 9; 

[[nodiscard]] inline bool write_save(const GameContext& ctx,
                                     const EntityManager& entities,
                                     const WorldManager& world_manager)
{
    std::ofstream out(resolve_path(ctx, "save.dat"), std::ios::binary | std::ios::trunc);
    if (!out) return false;

    BinaryWriter writer(out);
    const SaveHeader header{kSaveMagic, kSaveVersion};
    writer.write(header);

    writer.write_bytes(ctx.field.data(), sizeof(float) * WORLD_SIZE);
    writer.write_bytes(ctx.temperature.data(), sizeof(float) * WORLD_SIZE);
    writer.write_bytes(ctx.humidity.data(), sizeof(float) * WORLD_SIZE);
    writer.write_bytes(ctx.continent_map.data(), sizeof(float) * WORLD_SIZE);
    writer.write_bytes(ctx.flora.data(), sizeof(std::uint8_t) * WORLD_SIZE);

    const ViewState view_state{ctx.zoom, ctx.target_zoom, ctx.map_offset_x, ctx.map_offset_y, ctx.pos_cam.x, ctx.pos_cam.y, ctx.hour};
    writer.write(view_state);

    std::size_t stack_size_raw = ctx.state_stack.size();
    while (stack_size_raw > 0 && ctx.state_stack[stack_size_raw - 1]->mode() == GameMode::Pause) {
        --stack_size_raw;
    }
    if (stack_size_raw == 0) {
        writer.write(static_cast<std::uint8_t>(1));
        writer.write(static_cast<std::uint8_t>(GameMode::Game));
    } else {
        const std::uint8_t stack_size = static_cast<std::uint8_t>(std::min<std::size_t>(stack_size_raw, 255u));
        writer.write(stack_size);
        for (std::size_t i = 0; i < stack_size; ++i) {
            const auto mode = static_cast<std::uint8_t>(ctx.state_stack[i]->mode());
            writer.write(mode);
        }
    }

    writer.write(ctx.active_event_id);
    writer.write(ctx.active_battle_id);

    entities.save(out);
    world_manager.save(out);

    return static_cast<bool>(out);
}

[[nodiscard]] inline bool read_save(GameContext& ctx,
                                    EntityManager& entities,
                                    WorldManager& world_manager)
{
    std::ifstream in(resolve_path(ctx, "save.dat"), std::ios::binary);
    if (!in) return false;

    BinaryReader reader(in);
    SaveHeader header = reader.read<SaveHeader>();
    
    // Проверка версии: разрешаем загрузку только если версия совпадает
    // Это предотвращает краш при изменении структур данных
    if (header.magic != kSaveMagic || header.version != kSaveVersion) {
        SDL_Log("GEN: Save version mismatch: expected %u, got %u", kSaveVersion, header.version);
        return false;
    }

    reader.read_bytes(ctx.field.data(), sizeof(float) * WORLD_SIZE);
    reader.read_bytes(ctx.temperature.data(), sizeof(float) * WORLD_SIZE);
    reader.read_bytes(ctx.humidity.data(), sizeof(float) * WORLD_SIZE);
    reader.read_bytes(ctx.continent_map.data(), sizeof(float) * WORLD_SIZE);
    reader.read_bytes(ctx.flora.data(), sizeof(std::uint8_t) * WORLD_SIZE);

    build_terrain_map(ctx);

    // Чтение состояния камеры
    ViewState view_state = reader.read<ViewState>();
    ctx.zoom = std::clamp(view_state.zoom, ctx.min_zoom, ctx.max_zoom);
    ctx.target_zoom = std::clamp(view_state.target_zoom, ctx.min_zoom, ctx.max_zoom);
    ctx.map_offset_x = view_state.map_offset_x;
    ctx.map_offset_y = view_state.map_offset_y;
    ctx.pos_cam = TilePosition{view_state.pos_cam_x, view_state.pos_cam_y};
    ctx.hour = view_state.hour;

    const auto stack_size = reader.read<std::uint8_t>();
    std::vector<GameMode> stack;
    stack.reserve(stack_size);
    for (std::uint8_t i = 0; i < stack_size; ++i) {
        const auto raw_mode = reader.read<std::uint8_t>();
        if (raw_mode > static_cast<std::uint8_t>(GameMode::Pause)) {
            continue;
        }
        const auto mode = static_cast<GameMode>(raw_mode);
        if (mode == GameMode::Menu || mode == GameMode::Pause) {
            continue;
        }
        stack.push_back(mode);
    }
    if (stack.empty()) {
        stack.push_back(GameMode::Game);
    }
    ctx.state_stack.clear();
    for (GameMode mode : stack) {
        auto state = StateRegistry::instance().create(mode);
        if (state) ctx.state_stack.push_back(std::move(state));
    }

    const std::int32_t saved_event_id = reader.read<std::int32_t>();
    const std::int32_t saved_battle_id = reader.read<std::int32_t>();

    const bool has_event = std::any_of(stack.begin(), stack.end(), [](GameMode mode) {
        return mode == GameMode::Event;
    });
    const bool has_fight = std::any_of(stack.begin(), stack.end(), [](GameMode mode) {
        return mode == GameMode::Fight;
    });

    ctx.active_event_id = has_event ? saved_event_id : -1;
    ctx.active_battle_id = -1;
    ctx.battle_target_id = has_fight ? saved_battle_id : -1;

    entities.load(in);
    world_manager.load(in, ctx);

    return static_cast<bool>(in);
}
} // namespace save_game

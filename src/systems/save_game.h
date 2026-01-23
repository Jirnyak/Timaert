#pragma once

#include <cstdint>
#include <fstream>
#include <string_view>
#include <algorithm>
#include <vector>
#include "core/binary_io.h"

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
// VERSION 10: NPCs are now fully ECS-managed, old saves incompatible
constexpr std::uint32_t kSaveVersion = 10; 

[[nodiscard]] inline bool write_save(const GameContext& ctx,
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

    world_manager.save(out);

    const bool success = static_cast<bool>(out);
    
#ifdef __EMSCRIPTEN__
    if (success) {
        out.close();
        em_sync_persistent_fs();
    }
#endif
    
    return success;
}

[[nodiscard]] inline bool read_save(GameContext& ctx,
                                    WorldManager& world_manager)
{
    const std::string save_path = resolve_path(ctx, "save.dat");
    SDL_Log("SAVE: Attempting to load from: %s", save_path.c_str());
    
    std::ifstream in(save_path, std::ios::binary);
    if (!in) {
        SDL_Log("SAVE: Failed to open save file (file may not exist)");
        return false;
    }
    
    SDL_Log("SAVE: File opened successfully, reading header...");
    BinaryReader reader(in);
    SaveHeader header = reader.read<SaveHeader>();
    
    // Version check: only allow loading if version matches
    // This prevents crashes when data structures change
    if (header.magic != kSaveMagic) {
        SDL_Log("SAVE: Invalid save file magic: expected 0x%08X, got 0x%08X", kSaveMagic, header.magic);
        return false;
    }
    if (header.version != kSaveVersion) {
        SDL_Log("SAVE: Version mismatch: expected %u, got %u", kSaveVersion, header.version);
        return false;
    }
    
    SDL_Log("SAVE: Header valid, loading world data...");

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

    const bool has_event = std::any_of(stack.begin(), stack.end(), [](GameMode mode) {
        return mode == GameMode::Event;
    });

    ctx.active_event_id = has_event ? saved_event_id : -1;

    world_manager.load(in, ctx);

    return static_cast<bool>(in);
}
} // namespace save_game

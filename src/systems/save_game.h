#pragma once

#include <cstdint>
#include <fstream>
#include <string_view>
#include <algorithm>
#include <vector>
#include "core/binary_io.h"

#include "systems/entity_manager.h"
#include "core/game_context.h"
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
    std::int32_t pos_cam = 0;
    std::uint64_t hour = 0;
};

constexpr std::uint32_t kSaveMagic = 0x53415645; // 'SAVE'
// ВЕРСИЯ 7: Добавлено сохранение игрового времени
constexpr std::uint32_t kSaveVersion = 7; 

[[nodiscard]] inline bool write_save(const GameContext& ctx,
                                     const EntityManager& entities,
                                     const WorldManager& world_manager)
{
    std::ofstream out(resolve_path(ctx, "save.dat"), std::ios::binary | std::ios::trunc);
    if (!out) return false;

    BinaryWriter writer(out);
    const SaveHeader header{kSaveMagic, kSaveVersion};
    writer.write(header);

    writer.write_bytes(ctx.field.get(), sizeof(float) * WORLD_SIZE);

    const ViewState view_state{ctx.zoom, ctx.target_zoom, ctx.map_offset_x, ctx.map_offset_y, ctx.pos_cam, ctx.hour};
    writer.write(view_state);

    std::size_t stack_size_raw = ctx.state_stack.size();
    while (stack_size_raw > 0 && ctx.state_stack[stack_size_raw - 1] == GameMode::Pause) {
        --stack_size_raw;
    }
    if (stack_size_raw == 0) {
        writer.write(static_cast<std::uint8_t>(1));
        writer.write(static_cast<std::uint8_t>(GameMode::Game));
    } else {
        const std::uint8_t stack_size = static_cast<std::uint8_t>(std::min<std::size_t>(stack_size_raw, 255u));
        writer.write(stack_size);
        for (std::size_t i = 0; i < stack_size; ++i) {
            const auto mode = static_cast<std::uint8_t>(ctx.state_stack[i]);
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

    reader.read_bytes(ctx.field.get(), sizeof(float) * WORLD_SIZE);

    build_terrain_map(ctx);

    // Чтение состояния камеры
    ViewState view_state = reader.read<ViewState>();
    ctx.zoom = std::clamp(view_state.zoom, ctx.min_zoom, ctx.max_zoom);
    ctx.target_zoom = std::clamp(view_state.target_zoom, ctx.min_zoom, ctx.max_zoom);
    ctx.map_offset_x = view_state.map_offset_x;
    ctx.map_offset_y = view_state.map_offset_y;
    if (view_state.pos_cam >= 0 && view_state.pos_cam < static_cast<std::int32_t>(WORLD_SIZE)) {
        ctx.pos_cam = view_state.pos_cam;
    }
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
    set_state_stack(ctx, stack, false);

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

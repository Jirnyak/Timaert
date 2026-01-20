#pragma once

#include <cstdint>
#include <fstream>
#include <string_view>
#include <algorithm>
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
};

constexpr std::uint32_t kSaveMagic = 0x53415645; // 'SAVE'
// ВЕРСИЯ 5: Добавлены фракции и система репутации игрока
constexpr std::uint32_t kSaveVersion = 5; 

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

    const ViewState view_state{ctx.zoom, ctx.target_zoom, ctx.map_offset_x, ctx.map_offset_y, ctx.pos_cam};
    writer.write(view_state);

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
    if (header.magic != kSaveMagic || header.version != kSaveVersion) return false;

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

    entities.load(in);
    world_manager.load(in, ctx);

    return static_cast<bool>(in);
}
} // namespace save_game

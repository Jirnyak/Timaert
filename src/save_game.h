#pragma once

#include <cstdint>
#include <fstream>
#include <string_view>
#include <algorithm>

#include "entity_manager.h"
#include "game_context.h"
#include "world_manager.h"

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
constexpr std::uint32_t kSaveVersion = 2;

[[nodiscard]] inline bool write_save(const GameContext& ctx,
                                     const EntityManager& entities,
                                     const WorldManager& world_manager)
{
    std::ofstream out(resolve_path(ctx, "save.dat"), std::ios::binary | std::ios::trunc);
    if (!out) return false;

    const SaveHeader header{kSaveMagic, kSaveVersion};
    out.write(reinterpret_cast<const char*>(&header), static_cast<std::streamsize>(sizeof(header)));

    out.write(reinterpret_cast<const char*>(ctx.field.get()),
              static_cast<std::streamsize>(sizeof(float) * WORLD_SIZE));

    const ViewState view_state{ctx.zoom, ctx.target_zoom, ctx.map_offset_x, ctx.map_offset_y, ctx.pos_cam};
    out.write(reinterpret_cast<const char*>(&view_state), static_cast<std::streamsize>(sizeof(view_state)));

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

    SaveHeader header{};
    in.read(reinterpret_cast<char*>(&header), static_cast<std::streamsize>(sizeof(header)));
    if (header.magic != kSaveMagic || (header.version != 1 && header.version != kSaveVersion)) return false;

    in.read(reinterpret_cast<char*>(ctx.field.get()),
            static_cast<std::streamsize>(sizeof(float) * WORLD_SIZE));

    build_terrain_map(ctx);

    if (header.version >= 2) {
        ViewState view_state{};
        in.read(reinterpret_cast<char*>(&view_state), static_cast<std::streamsize>(sizeof(view_state)));
        ctx.zoom = std::clamp(view_state.zoom, ctx.min_zoom, ctx.max_zoom);
        ctx.target_zoom = std::clamp(view_state.target_zoom, ctx.min_zoom, ctx.max_zoom);
        ctx.map_offset_x = view_state.map_offset_x;
        ctx.map_offset_y = view_state.map_offset_y;
        if (view_state.pos_cam >= 0 && view_state.pos_cam < static_cast<std::int32_t>(WORLD_SIZE)) {
            ctx.pos_cam = view_state.pos_cam;
        }
    }

    entities.load(in);
    world_manager.load(in, ctx);

    return static_cast<bool>(in);
}
} // namespace save_game

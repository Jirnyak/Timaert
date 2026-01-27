#pragma once

#include <cstdint>
#include <fstream>
#include <print>
#include <sstream>
#include <string_view>
#include <algorithm>
#include <vector>
#include "core/binary_io.h"

#include "core/game_context.h"
#include "core/game_state.h"
#include "systems/world_manager.h"

namespace save_game {

[[nodiscard]] inline bool save_exists(const GameContext& ctx) {
    const std::string save_path = resolve_path(ctx, "save.dat");
    std::ifstream in(save_path, std::ios::binary);
    return in.good();
}

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

constexpr std::uint32_t kSaveMagic = 0x53415645;  // 'SAVE'
// VERSION 13: InteractionState added - NPCs can be talked to, traded with, or fought
constexpr std::uint32_t kSaveVersion = 13;

[[nodiscard]] inline bool write_save(const GameContext& ctx, const WorldManager& world_manager) {
    std::ofstream out(resolve_path(ctx, "save.dat"), std::ios::binary | std::ios::trunc);
    if (!out)
        return false;

    BinaryWriter writer(out);
    const SaveHeader header{kSaveMagic, kSaveVersion};
    writer.write(header);

    writer.write_bytes(ctx.field.data(), sizeof(float) * WORLD_SIZE);
    writer.write_bytes(ctx.temperature.data(), sizeof(float) * WORLD_SIZE);
    writer.write_bytes(ctx.humidity.data(), sizeof(float) * WORLD_SIZE);
    writer.write_bytes(ctx.continent_map.data(), sizeof(float) * WORLD_SIZE);
    writer.write_bytes(ctx.flora.data(), sizeof(std::uint8_t) * WORLD_SIZE);

    const ViewState view_state{ctx.zoom,
                               ctx.target_zoom,
                               ctx.map_offset_x,
                               ctx.map_offset_y,
                               ctx.pos_cam.x,
                               ctx.pos_cam.y,
                               ctx.ticks()};
    writer.write(view_state);

    // Build list of saveable states, replacing non-saveable with fallbacks
    std::vector<std::pair<GameMode, GameState*>> saveable_states;
    for (const auto& state_ptr : ctx.state_stack) {
        if (!state_ptr)
            continue;
        if (state_ptr->mode() == GameMode::Pause || state_ptr->mode() == GameMode::Load)
            continue;

        if (state_ptr->can_save()) {
            saveable_states.emplace_back(state_ptr->mode(), state_ptr.get());
        } else {
            // Replace non-saveable state with its fallback
            GameMode const fallback = state_ptr->fallback_mode();
            if (fallback != GameMode::Menu && fallback != GameMode::Pause) {
                saveable_states.emplace_back(fallback, nullptr);
            }
        }
    }

    // Remove duplicates (keep last occurrence of each mode)
    for (auto it = saveable_states.begin(); it != saveable_states.end();) {
        auto next = std::find_if(it + 1, saveable_states.end(), [&](const auto& p) {
            return p.first == it->first;
        });
        if (next != saveable_states.end()) {
            it = saveable_states.erase(it);
        } else {
            ++it;
        }
    }

    if (saveable_states.empty()) {
        saveable_states.emplace_back(GameMode::Game, nullptr);
    }

    const std::uint8_t stack_size =
        static_cast<std::uint8_t>(std::min(saveable_states.size(), std::size_t{255}));
    writer.write(stack_size);

    for (const auto& [mode, state_ptr] : saveable_states) {
        writer.write(static_cast<std::uint8_t>(mode));

        // Write state data with size prefix for forward compatibility
        if (state_ptr) {
            // Serialize to temp buffer to get size
            std::ostringstream temp_stream(std::ios::binary);
            BinaryWriter temp_writer(temp_stream);
            state_ptr->save_state(temp_writer);
            std::string data = temp_stream.str();

            writer.write(static_cast<std::uint16_t>(data.size()));
            if (!data.empty()) {
                writer.write_bytes(data.data(), data.size());
            }
        } else {
            writer.write(static_cast<std::uint16_t>(0));
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

[[nodiscard]] inline bool read_save(GameContext& ctx, WorldManager& world_manager) {
    const std::string save_path = resolve_path(ctx, "save.dat");
    std::println("[SAVE] Attempting to load from: {}", save_path);

    std::ifstream in(save_path, std::ios::binary);
    if (!in) {
        std::println("[SAVE] Failed to open save file (file may not exist)");
        return false;
    }

    std::println("[SAVE] File opened successfully, reading header...");
    BinaryReader reader(in);
    SaveHeader header = reader.read<SaveHeader>();

    // Version check: only allow loading if version matches
    // This prevents crashes when data structures change
    if (header.magic != kSaveMagic) {
        std::println("[SAVE] Invalid save file magic: expected 0x{:08X}, got 0x{:08X}",
                kSaveMagic,
                header.magic);
        return false;
    }
    if (header.version != kSaveVersion) {
        std::println("[SAVE] Version mismatch: expected {}, got {}", kSaveVersion, header.version);
        return false;
    }

    std::println("[SAVE] Header valid, loading world data...");

    reader.read_bytes(ctx.field.data(), sizeof(float) * WORLD_SIZE);
    reader.read_bytes(ctx.temperature.data(), sizeof(float) * WORLD_SIZE);
    reader.read_bytes(ctx.humidity.data(), sizeof(float) * WORLD_SIZE);
    reader.read_bytes(ctx.continent_map.data(), sizeof(float) * WORLD_SIZE);
    reader.read_bytes(ctx.flora.data(), sizeof(std::uint8_t) * WORLD_SIZE);

    build_terrain_map(ctx);

    // Чтение состояния камеры
    ViewState const view_state = reader.read<ViewState>();
    ctx.zoom = std::clamp(view_state.zoom, ctx.min_zoom, ctx.max_zoom);
    ctx.target_zoom = std::clamp(view_state.target_zoom, ctx.min_zoom, ctx.max_zoom);
    ctx.map_offset_x = view_state.map_offset_x;
    ctx.map_offset_y = view_state.map_offset_y;
    ctx.pos_cam = TilePosition{view_state.pos_cam_x, view_state.pos_cam_y};
    ctx.set_ticks(view_state.hour);

    // Read state stack with per-state data
    const auto stack_size = reader.read<std::uint8_t>();
    ctx.state_stack.clear();

    for (std::uint8_t i = 0; i < stack_size; ++i) {
        const auto raw_mode = reader.read<std::uint8_t>();
        const auto data_size = reader.read<std::uint16_t>();

        if (raw_mode > static_cast<std::uint8_t>(GameMode::Interaction)) {
            // Skip unknown mode data
            if (data_size > 0) {
                in.seekg(data_size, std::ios::cur);
            }
            continue;
        }

        const auto mode = static_cast<GameMode>(raw_mode);
        if (mode == GameMode::Menu || mode == GameMode::Pause || mode == GameMode::Load) {
            // Skip non-saveable states
            if (data_size > 0) {
                in.seekg(data_size, std::ios::cur);
            }
            continue;
        }

        auto state = StateRegistry::instance().create(mode);
        if (state) {
            // Load state-specific data
            if (data_size > 0) {
                state->load_state(reader);
            }
            ctx.state_stack.push_back(std::move(state));
        } else if (data_size > 0) {
            in.seekg(data_size, std::ios::cur);
        }
    }

    // Ensure at least Game state exists
    if (ctx.state_stack.empty()) {
        auto game_state = StateRegistry::instance().create(GameMode::Game);
        if (game_state)
            ctx.state_stack.push_back(std::move(game_state));
    }

    const std::int32_t saved_event_id = reader.read<std::int32_t>();

    // Check if any loaded state is an Event state
    const bool has_event =
        std::any_of(ctx.state_stack.begin(), ctx.state_stack.end(), [](const auto& state) {
            return state && state->mode() == GameMode::Event;
        });

    ctx.active_event_id = has_event ? saved_event_id : -1;

    world_manager.load(in, ctx);

    return static_cast<bool>(in);
}
}  // namespace save_game

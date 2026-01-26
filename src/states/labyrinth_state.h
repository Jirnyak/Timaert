#pragma once

#include "core/game_state.h"
#include "core/binary_io.h"
#include "rendering/tile_view.h"
#include "ui/ui.h"
#include "ui/ui_events.h"

#include <cstdint>
#include <vector>

class TextureManager;

class LabyrinthState : public GameState {
public:
    [[nodiscard]] GameMode mode() const noexcept override {
        return GameMode::Labyrinth;
    }

    // Labyrinth state is saveable - save generated maze and player progress
    void save_state(BinaryWriter& writer) const override {
        // Save initialization flag
        writer.write(static_cast<std::uint8_t>(initialized_ ? 1 : 0));

        if (initialized_) {
            // Save player and camera positions
            writer.write(player_pos_.x);
            writer.write(player_pos_.y);
            writer.write(cam_pos_.x);
            writer.write(cam_pos_.y);
            writer.write(static_cast<std::uint8_t>(freecam_ ? 1 : 0));

            // Save cells and seen maps (compressed: RLE for cells, raw for seen)
            writer.write_bytes(cells_.data(), sizeof(CellType) * WORLD_SIZE);
            writer.write_bytes(seen_.data(), sizeof(std::uint8_t) * WORLD_SIZE);
        }
    }

    void load_state(BinaryReader& reader) override {
        initialized_ = reader.read<std::uint8_t>() != 0;

        if (initialized_) {
            // Load player and camera positions
            player_pos_.x = reader.read<std::uint16_t>();
            player_pos_.y = reader.read<std::uint16_t>();
            cam_pos_.x = reader.read<std::uint16_t>();
            cam_pos_.y = reader.read<std::uint16_t>();
            freecam_ = reader.read<std::uint8_t>() != 0;

            // Load cells and seen maps
            reader.read_bytes(cells_.data(), sizeof(CellType) * WORLD_SIZE);
            reader.read_bytes(seen_.data(), sizeof(std::uint8_t) * WORLD_SIZE);
        }
    }

private:
    enum class CellType : std::uint8_t { Nothing = 0, Wall, Door, Source, Test };

    WorldMap<CellType> cells_;
    WorldMap<std::uint8_t> seen_;
    TilePosition player_pos_ = INVALID_POS;
    TilePosition cam_pos_ = INVALID_POS;
    bool initialized_ = false;
    bool freecam_ = false;
    std::vector<TilePosition> path_;
    WorldMap<TilePosition> path_prev_;
    std::vector<TilePosition> path_queue_;
    std::size_t path_index_ = 0;
    std::uint32_t last_move_ticks_ = 0;
    UIButtonGroup speed_buttons_;
    UIButtonGroup move_buttons_;
    bool buttons_initialized_ = false;
    bool click_blocked_ = false;
    TilePosition hover_pos_ = INVALID_POS;
    InputManager input_manager_;
    Direction pending_move_dir_ = Direction::Up;
    bool move_pending_ = false;
    bool center_pending_ = false;

    void request_move(Direction dir) {
        pending_move_dir_ = dir;
        move_pending_ = true;
    }
    void request_center() {
        center_pending_ = true;
    }

    static constexpr std::uint32_t kMoveDelayMs = 80;

    static constexpr int kWallSpacing = 4;
    static constexpr int kRevealRadius = 6;

    [[nodiscard]] int to_pos(int x, int y) const noexcept {
        x = wrap_coord(x);
        y = wrap_coord(y);
        return x * WORLD_WIDTH + y;
    }

    void handle_click_move(GameContext& ctx, int screen_x, int screen_y);

    [[nodiscard]] bool is_wall(TilePosition pos) const noexcept {
        return cells_[pos] == CellType::Wall;
    }

    [[nodiscard]] TilePosition screen_to_world_pos(const GameContext& ctx,
                                                   int screen_x,
                                                   int screen_y,
                                                   const TileView& view) const {
        auto neighbor = [](TilePosition pos, Direction dir) { return neighbor_from_pos(pos, dir); };
        return ::screen_to_world_pos(ctx, screen_x, screen_y, cam_pos_, view, neighbor);
    }

    void reveal_from_player() noexcept;
    void generate_labyrinth(GameContext& ctx);
    void ensure_generated(GameContext& ctx);
    void ensure_player_exit(GameContext& ctx);
    void move_player(Direction dir, GameContext& ctx);
    void init_buttons(GameContext& ctx);

public:
    void handle_event(SDL_Event& event, GameContext& ctx, TextureManager& textures) override;
    void update(GameContext& ctx, TextureManager& textures) override;
    void render(GameContext& ctx, TextureManager& textures) override;
};

inline StateRegistrar<LabyrinthState> register_labyrinth_state_{GameMode::Labyrinth};

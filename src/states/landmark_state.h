#pragma once

#include <string>

#include "core/game_state.h"
#include "core/binary_io.h"
#include "ui/ui.h"
#include "systems/world_manager.h"
#include "ui/ui_events.h"
#include "ecs/components/core.h"
#include "ecs/entity_ref.h"
#include "core/game_context.h"
#include "core/types.h"
#include "entt/entt.hpp"
#include "systems/player.h"
#include "systems/landmark.h"

class TextureManager;

namespace ecs {
struct Dead;
}  // namespace ecs
struct Inventory;

class LandmarkState : public GameState {
public:
    explicit LandmarkState() = default;

    [[nodiscard]] GameMode mode() const noexcept override {
        return GameMode::Landmark;
    }

    void save_state(BinaryWriter& writer) const override {
        writer.write(settlement_id_);
        writer.write(static_cast<std::uint8_t>(landmark_type_));
        writer.write_string(settlement_name_);
    }

    void load_state(BinaryReader& reader) override {
        settlement_id_ = reader.read<std::int32_t>();
        landmark_type_ = static_cast<LandmarkType>(reader.read<std::uint8_t>());
        reader.read_string(settlement_name_);
        loaded_from_save_ = true;
    }

private:
    std::int32_t settlement_id_ = -1;
    bool loaded_from_save_ = false;
    bool interaction_started_ = false;

    // Cached settlement data for rendering
    LandmarkType landmark_type_ = LandmarkType::None;
    std::string settlement_name_ = "Unknown";

    [[nodiscard]] bool has_settlement() const {
        return settlement_id_ >= 0 || loaded_from_save_;
    }

    // UI state
    std::string dialogue_message_;
    bool showing_enter_msg_ = false;
    bool showing_tavern_msg_ = false;

    // UI
    MenuButtonList landmark_menu_;
    UIButtonGroup pause_buttons_;
    bool ui_initialized_ = false;
    bool pause_buttons_initialized_ = false;
    int last_buttons_width_ = -1;
    int last_buttons_height_ = -1;

    enum class LandmarkAction : std::uint8_t { None, Enter, Trade, Tavern, Leave };
    LandmarkAction pending_action_ = LandmarkAction::None;

    InputManager input_manager_;

    void init_pause_buttons(GameContext& ctx);
    void init_ui(GameContext& ctx);
    void process_pending_action(GameContext& ctx);
    void handle_enter(GameContext& ctx);
    void handle_trade(GameContext& ctx);
    void handle_tavern(GameContext& ctx);
    void start_interaction_with_settlement(std::int32_t settlement_id, GameContext& ctx);

public:
    void update(GameContext& ctx, TextureManager& textures) override;
    void render(GameContext& ctx, TextureManager& textures) override;
};

inline StateRegistrar<LandmarkState> register_landmark_state_{GameMode::Landmark};

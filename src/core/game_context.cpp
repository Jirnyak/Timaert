#include "core/game_context.h"
#include "core/game_state.h"

GameContext::GameContext() : rng(std::random_device{}())
{
    pos_cam = TilePosition{static_cast<std::uint16_t>(cam_x), static_cast<std::uint16_t>(cam_y)};
}

GameContext::~GameContext() = default;

GameState* current_state(const GameContext& ctx) noexcept
{
    return ctx.state_stack.empty() ? nullptr : ctx.state_stack.back().get();
}

std::unique_ptr<GameState> StateRegistry::create(GameMode mode) const
{
    auto fn = creators[static_cast<std::size_t>(mode)];
    return fn ? fn() : nullptr;
}

void push_state(GameContext& ctx, std::unique_ptr<GameState> state, bool reset_pick)
{
    if (state) ctx.state_stack.push_back(std::move(state));
    if (reset_pick) ctx.picked = false;
}

void replace_state(GameContext& ctx, std::unique_ptr<GameState> state, bool reset_pick)
{
    if (!state) return;
    if (ctx.state_stack.empty()) {
        ctx.state_stack.push_back(std::move(state));
    } else {
        ctx.state_stack.back() = std::move(state);
    }
    if (reset_pick) ctx.picked = false;
}

bool pop_state(GameContext& ctx, bool reset_pick)
{
    if (!ctx.state_stack.empty()) {
        ctx.state_stack.pop_back();
        if (reset_pick) ctx.picked = false;
        return true;
    }
    return false;
}

void clear_states(GameContext& ctx, bool reset_pick)
{
    ctx.state_stack.clear();
    if (reset_pick) ctx.picked = false;
}

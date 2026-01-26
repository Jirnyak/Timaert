#include "core/game_context.h"
#include "core/game_state.h"
#include "ecs/world.h"

GameContext::GameContext() : rng(std::random_device{}()), ecs_world(std::make_unique<ecs::World>())
{
    pos_cam = TilePosition{static_cast<std::uint16_t>(cam_x), static_cast<std::uint16_t>(cam_y)};
    ecs_world->init();
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

// ECS singleton accessor implementations

std::uint64_t GameContext::ticks() const noexcept {
    if (ecs_world) return ecs_world->time().ticks;
    return hour;
}

void GameContext::set_ticks(std::uint64_t t) noexcept {
    hour = t;
    if (ecs_world) ecs_world->time().ticks = t;
}

int GameContext::speed() const noexcept {
    if (ecs_world) return ecs_world->time().game_speed;
    return game_speed;
}

void GameContext::set_speed(int s) noexcept {
    game_speed = s;
    if (ecs_world) ecs_world->time().game_speed = s;
}

bool GameContext::is_paused() const noexcept {
    if (ecs_world) return ecs_world->time().paused;
    return paused;
}

void GameContext::set_paused(bool p) noexcept {
    paused = p;
    if (ecs_world) ecs_world->time().paused = p;
}

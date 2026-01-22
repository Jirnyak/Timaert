#include "systems/player.h"

#include "core/game_state.h"

#include <algorithm>
#include <cmath>
#include <utility>

[[maybe_unused]] constexpr std::size_t kGameStateSize = sizeof(GameState);

void Player::init(int start_pos, rng_t& rng)
{
    pos = start_pos;
    prev_pos = start_pos;
    aim_pos = -1;
    inventory = Inventory{};
    inventory.max_capacity = 50;
    inventory.capital = 1000.0;
    state = PlayerState::Normal;
    speed = 25.0;
    move_progress = 0.0;
    visual_x = static_cast<float>(start_pos) / static_cast<float>(WORLD_WIDTH);
    visual_y = static_cast<float>(start_pos % WORLD_WIDTH);
    life = 100;
    max_life = 100;

    gender = static_cast<Gender>(random_u32_inclusive(rng, static_cast<std::uint32_t>(Gender::Count) - 1));
    race = Race::Human;

    lust = 0;
    max_lust = 100;
    will = 100;
    max_will = 100;

    // Начальная репутация
    for (auto& r : reputation) r = 0;
    reputation[static_cast<std::size_t>(FactionID::Kingdom)] = 10;   // Чуть-чуть любят
    reputation[static_cast<std::size_t>(FactionID::Outlaws)] = -20; // Бандиты недолюбливают

    // Инициализация навыков
    skill_count = 0;
    for (auto& s : skills) s = SkillID::Wait;

    learn_skill(SkillID::Punch);
    learn_skill(SkillID::Wait);
    learn_skill(SkillID::Tease);

    active = true;
}

void Player::learn_skill(SkillID skill)
{
    // Проверяем дубликаты
    for (std::size_t i = 0; i < skill_count; ++i) {
        if (skills[i] == skill) return;
    }

    // Добавляем, если есть место
    if (skill_count < MAX_PLAYER_SKILLS) {
        skills[skill_count++] = skill;
    }
}

void Player::set_aim(int target_pos)
{
    aim_pos = target_pos;
}

void Player::clear_aim()
{
    aim_pos = -1;
}

bool Player::has_aim() const noexcept
{
    return aim_pos >= 0;
}

bool Player::is_at_aim() const noexcept
{
    return aim_pos >= 0 && pos == aim_pos;
}

bool PlayerController::check_collision_and_trigger(int target_pos, NPCManager& npcs, GameContext& ctx)
{
    NPC* npc = npcs.find_at(target_pos);
    if (npc && npc->active && npc->state != NPCState::Dead)
    {
        // ИСПРАВЛЕНИЕ: Убрана проверка репутации (hostile).
        // Теперь любое столкновение с живым NPC запускает боевой режим для тестов.

        // Use StateRegistry - BattleState will get target from ctx.battle_target_id
        ctx.battle_target_id = npc->id;
        push_state(ctx, StateRegistry::instance().create(GameMode::Fight));

        player_.clear_aim();
        path_.clear();
        path_index_ = 0;
        return true;
    }
    return false;
}

void PlayerController::init(int start_pos, rng_t& rng)
{
    player_.init(start_pos, rng);
    current_settlement_idx_ = -1;
}

void PlayerController::update(GameContext& ctx, LandmarkSystem& landmarks,
                              const TerrainType* relief, NPCManager& npcs)
{
    if (!player_.active) return;

    const Settlement* settlement = landmarks.find_settlement_at(player_.pos);
    if (settlement)
    {
        player_.state = PlayerState::InSettlement;
        current_settlement_idx_ = settlement->id;
    }
    else
    {
        player_.state = PlayerState::Normal;
        current_settlement_idx_ = -1;
    }

    if (path_index_ < path_.size())
    {
        if (player_.is_at_aim())
        {
            clear_path();
            return;
        }

        const auto effect = get_terrain_effect(relief[player_.pos]);
        player_.move_progress += player_.speed * effect.speed_mult;
        if (player_.move_progress < 100.0) return;
        player_.move_progress = 0.0;
        player_.will = std::max(0, player_.will - effect.will_drain);

        const int next_pos = path_[path_index_];

        // Проверка на врага перед шагом
        if (check_collision_and_trigger(next_pos, npcs, ctx)) return;

        if (can_move_to(next_pos, relief))
        {
            player_.prev_pos = player_.pos;
            player_.pos = next_pos;
            ++path_index_;
            if (path_index_ >= path_.size())
            {
                clear_path();
            }
        }
        else
        {
            clear_path();
        }
        return;
    }

    if (!player_.has_aim()) return;
    if (player_.is_at_aim())
    {
        player_.clear_aim();
        return;
    }

    const auto effect = get_terrain_effect(relief[player_.pos]);
    player_.move_progress += player_.speed * effect.speed_mult;
    if (player_.move_progress < 100.0) return;
    player_.move_progress = 0.0;
    player_.will = std::max(0, player_.will - effect.will_drain);

    move_toward_direct(ctx, relief, npcs);
}

void PlayerController::move_toward_direct(GameContext& ctx, const TerrainType* relief, NPCManager& npcs)
{
    if (!player_.has_aim()) return;

    const int aim_row = player_.aim_pos / WORLD_WIDTH;
    const int aim_col = player_.aim_pos % WORLD_WIDTH;
    const int cur_row = player_.pos / WORLD_WIDTH;
    const int cur_col = player_.pos % WORLD_WIDTH;

    int d_row = aim_row - cur_row;
    int d_col = aim_col - cur_col;

    if (std::abs(d_row) > WORLD_WIDTH / 2)
    {
        d_row = (d_row > 0) ? d_row - WORLD_WIDTH : d_row + WORLD_WIDTH;
    }
    if (std::abs(d_col) > WORLD_WIDTH / 2)
    {
        d_col = (d_col > 0) ? d_col - WORLD_WIDTH : d_col + WORLD_WIDTH;
    }

    Direction best_dir = Direction::Up;
    if (std::abs(d_row) >= std::abs(d_col))
    {
        best_dir = (d_row < 0) ? Direction::Up : Direction::Down;
    }
    else
    {
        best_dir = (d_col < 0) ? Direction::Left : Direction::Right;
    }

    int next_pos = neighbor_from_pos(player_.pos, best_dir);

    // Проверка коллизии
    if (check_collision_and_trigger(next_pos, npcs, ctx)) return;

    if (can_move_to(next_pos, relief))
    {
        player_.prev_pos = player_.pos;
        player_.pos = next_pos;
        return;
    }

    for (int d = 0; d < 4; ++d)
    {
        const Direction dir = static_cast<Direction>(d);
        if (dir == best_dir) continue;
        next_pos = neighbor_from_pos(player_.pos, dir);

        // Проверка коллизии для альтернативного пути
        if (check_collision_and_trigger(next_pos, npcs, ctx)) return;

        if (can_move_to(next_pos, relief))
        {
            player_.prev_pos = player_.pos;
            player_.pos = next_pos;
            return;
        }
    }
}

bool PlayerController::can_move_to(int pos, const TerrainType* relief) const noexcept
{
    if (pos < 0 || pos >= WORLD_WIDTH * WORLD_WIDTH) return false;
    return relief[pos] != TerrainType::Water && relief[pos] != TerrainType::Mount;
}

PlayerController::TerrainEffect PlayerController::get_terrain_effect(TerrainType type) const noexcept
{
    switch (type) {
        case TerrainType::Swamp:  return { 0.4f, 1 }; // Очень медленно + изнурение
        case TerrainType::Snow:   return { 0.6f, 1 }; // Снег замедляет
        case TerrainType::Jungle: return { 0.6f, 1 }; // Густые заросли
        case TerrainType::Sand:   return { 0.8f, 0 }; // Песок немного замедляет
        case TerrainType::Tundra: return { 0.8f, 0 };
        case TerrainType::Grass:  return { 1.0f, 0 };
        case TerrainType::Dirt:   return { 1.0f, 0 };
        default: return { 1.0f, 0 };
    }
}

void PlayerController::move_direction(Direction dir, const TerrainType* relief, NPCManager& npcs, GameContext& ctx)
{
    if (!player_.active) return;

    const int next_pos = neighbor_from_pos(player_.pos, dir);

    // Проверка коллизии
    if (check_collision_and_trigger(next_pos, npcs, ctx)) return;

    if (can_move_to(next_pos, relief))
    {
        player_.prev_pos = player_.pos;
        player_.pos = next_pos;
    }
}

bool PlayerController::set_path_to(GameContext& ctx, int target_pos, const TerrainType* relief)
{
    if (!player_.active) return false;
    if (target_pos < 0 || target_pos >= WORLD_WIDTH * WORLD_WIDTH) return false;
    if (!can_move_to(target_pos, relief)) return false;
    if (target_pos == player_.pos) return false;

    const int world_size = WORLD_WIDTH * WORLD_WIDTH;
    auto& prev = ctx.path_prev;
    auto& queue = ctx.path_queue;
    if (prev.size() != static_cast<std::size_t>(world_size))
    {
        prev.assign(static_cast<std::size_t>(world_size), -1);
    }
    else
    {
        std::fill(prev.begin(), prev.end(), -1);
    }
    queue.clear();
    if (queue.capacity() < static_cast<std::size_t>(world_size))
    {
        queue.reserve(static_cast<std::size_t>(world_size));
    }

    prev[player_.pos] = player_.pos;
    queue.push_back(player_.pos);

    std::size_t head = 0;
    while (head < queue.size())
    {
        const int current = queue[head++];
        if (current == target_pos) break;

        for (int d = 0; d < 4; ++d)
        {
            const Direction dir = static_cast<Direction>(d);
            const int neighbor = neighbor_from_pos(current, dir);
            if (neighbor < 0 || neighbor >= world_size) continue;
            if (prev[neighbor] != -1) continue;
            if (!can_move_to(neighbor, relief)) continue;

            prev[neighbor] = current;
            queue.push_back(neighbor);
        }
    }

    if (prev[target_pos] == -1) return false;

    std::vector<int> new_path;
    for (int pos = target_pos; pos != player_.pos; pos = prev[pos])
    {
        new_path.push_back(pos);
    }
    std::reverse(new_path.begin(), new_path.end());

    path_ = std::move(new_path);
    path_index_ = 0;
    player_.set_aim(target_pos);
    return true;
}

void PlayerController::clear_path()
{
    path_.clear();
    path_index_ = 0;
    player_.clear_aim();
}

void PlayerController::clear_aim()
{
    clear_path();
}

bool PlayerController::try_buy(ResourceType res, std::int32_t amount, Settlement& settlement)
{
    if (player_.state != PlayerState::InSettlement) return false;
    if (settlement.id != current_settlement_idx_) return false;

    const double price = settlement.market.buy_price(res) * static_cast<double>(amount);
    if (player_.inventory.capital < price) return false;
    if (!player_.inventory.can_add(res, amount)) return false;

    player_.inventory.capital -= price;
    player_.inventory.add(res, amount);
    settlement.capital += price;

    settlement.market.record_purchase(res, amount);
    settlement.market.update_prices();

    return true;
}

bool PlayerController::try_sell(ResourceType res, std::int32_t amount, Settlement& settlement)
{
    if (player_.state != PlayerState::InSettlement) return false;
    if (settlement.id != current_settlement_idx_) return false;

    if (player_.inventory.get(res) < amount) return false;

    const double price = settlement.market.sell_price(res) * static_cast<double>(amount);
    if (settlement.capital < price) return false;

    player_.inventory.remove(res, amount);
    player_.inventory.capital += price;
    settlement.capital -= price;

    settlement.market.record_sale(res, amount);
    settlement.market.update_prices();

    return true;
}

Player& PlayerController::player() noexcept
{
    return player_;
}

const Player& PlayerController::player() const noexcept
{
    return player_;
}

std::int32_t PlayerController::current_settlement() const noexcept
{
    return current_settlement_idx_;
}

void PlayerController::set_current_settlement(std::int32_t settlement_idx) noexcept
{
    current_settlement_idx_ = settlement_idx;
}

bool PlayerController::is_in_settlement() const noexcept
{
    return player_.state == PlayerState::InSettlement;
}

#pragma once

#include <cstdint>
#include <vector>
#include <limits>
#include <cstring>
#include <memory>
#include <istream>
#include <ostream>
#include "core/game_context.h"
#include "systems/economy.h"
#include "systems/landmark.h"
#include "core/binary_io.h"
#include "systems/skills.h"
#include "systems/entity_manager.h"
#include "rendering/texture_manager.h"
struct Player;
enum class NPCType : std::uint8_t
{
    None = 0,
    Peasant,
    Woodcutter,
    Merchant,
    Caravan,
    Bandit,
    Guard,
    Witch,
    Count
};

enum class NPCState : std::uint8_t
{
    Idle,
    Wandering,
    Traveling,
    Trading,
    Returning,
    Fleeing,
    Raiding,
    Cutting,
    Dead
};

struct NPC
{
    std::int32_t id = -1;
    std::int32_t pos = -1;
    std::int32_t prev_pos = -1;
    
    char name[32]{0};      // Имя персонажа
    char personality[32]{0}; // Характер (Brave, Cowardly, Lustful, etc.)
    NPCType type = NPCType::None;
    NPCState state = NPCState::Idle;
    FactionID faction = FactionID::Neutral;
    
    std::int32_t home_settlement = -1;
    std::int32_t target_settlement = -1;
    
    Inventory inventory;
    
    double speed = 1.0;
    double move_progress = 0.0;
    std::int32_t life = 100;
    std::int32_t max_life = 100;
    
    // --- Ролевые характеристики ---
    Gender gender = Gender::Male;
    Race race = Race::Human;
    
    std::int32_t lust = 0;
    std::int32_t max_lust = 100;
    std::int32_t will = 100;
    std::int32_t max_will = 100;
    
    // Флаг для "Театра": true = включает режим VN/Hentai, false = быстрый бой
    bool is_special = false;
    
    // Навыки (фиксированный массив для бинарной совместимости сохранения)
    static constexpr std::size_t MAX_NPC_SKILLS = 8;
    SkillID skills[MAX_NPC_SKILLS];
    std::uint8_t skill_count = 0;
    // ------------------------------
    
    std::int32_t idle_timer = 0;
    std::int32_t trade_timer = 0;

    // Visual coordinates for smooth movement
    float visual_x = 0.0f;
    float visual_y = 0.0f;
    
    bool active = false;
    
    void reset() noexcept
    {
        id = -1;
        pos = -1;
        prev_pos = -1;
        std::memset(name, 0, sizeof(name));
        std::memset(personality, 0, sizeof(personality));
        type = NPCType::None;
        state = NPCState::Idle;
        faction = FactionID::Neutral;
        home_settlement = -1;
        target_settlement = -1;
        inventory = Inventory{};
        speed = 1.0;
        move_progress = 0.0;
        life = 100;
        max_life = 100;
        
        gender = Gender::Male;
        race = Race::Human;
        lust = 0;
        max_lust = 100;
        will = 100;
        max_will = 100;
        is_special = false;
        
        skill_count = 0;
        for (auto& s : skills) s = SkillID::Wait;
        
        idle_timer = 0;
        trade_timer = 0;
        visual_x = 0.0f;
        visual_y = 0.0f;
        active = false;
    }

    void add_skill(SkillID id)
    {
        if (skill_count < MAX_NPC_SKILLS)
        {
            skills[skill_count++] = id;
        }
    }
    
    void init_by_type(NPCType t, rng_t& rng)
    {
        type = t;

        // Генерация имени (упрощенная)
        static const char* syl1[] = {"Bel", "Gar", "Mar", "Kael", "Jor", "Zan", "Thor", "Ray"};
        static const char* syl2[] = {"dor", "van", "ius", "eth", "lin", "morn", "tor", "gan"};
        
        std::string n = std::string(syl1[random_u32_inclusive(rng, 7)]) + syl2[random_u32_inclusive(rng, 7)];
        std::strncpy(name, n.c_str(), sizeof(name) - 1);

        // Генерация характера
        static const char* traits[] = {"Aggressive", "Calm", "Arrogant", "Fearful", "Merciless", "Flirty"};
        std::strncpy(personality, traits[random_u32_inclusive(rng, 5)], sizeof(personality) - 1);

        // Назначение фракции
        switch (t) {
            case NPCType::Bandit:   faction = FactionID::Outlaws; break;
            case NPCType::Peasant:
            case NPCType::Woodcutter:
            case NPCType::Merchant:
            case NPCType::Caravan:
            case NPCType::Guard:    faction = FactionID::Kingdom; break;
            default:                faction = FactionID::Neutral; break;
        }        
        // Генерация пола и расы
        gender = static_cast<Gender>(random_u32_inclusive(rng, static_cast<std::uint32_t>(Gender::Count) - 1));
        race = Race::Human;
        
        // Шанс 5% стать "особенным" (триггерит хентай-сцену вместо обычного боя)
        is_special = (random_u32_inclusive(rng, 100) > 95); 

        skill_count = 0;

        switch (t)
        {
            case NPCType::Peasant:
                speed = 0.5 + static_cast<double>(random_u32_inclusive(rng, 50)) / 100.0;
                life = max_life = 50 + static_cast<std::int32_t>(random_u32_inclusive(rng, 50));
                
                add_skill(SkillID::Punch);
                add_skill(SkillID::Struggle);
                add_skill(SkillID::Wait);
                break;

            case NPCType::Woodcutter:
                speed = 0.5 + static_cast<double>(random_u32_inclusive(rng, 40)) / 100.0;
                life = max_life = 60 + static_cast<std::int32_t>(random_u32_inclusive(rng, 40));

                add_skill(SkillID::Punch);
                add_skill(SkillID::Struggle);
                add_skill(SkillID::Wait);
                break;

            case NPCType::Merchant:
                speed = 0.8 + static_cast<double>(random_u32_inclusive(rng, 40)) / 100.0;
                inventory.set_capital(500.0 + random_u32_inclusive(rng, 500));
                life = max_life = 80 + static_cast<std::int32_t>(random_u32_inclusive(rng, 40));
                
                add_skill(SkillID::Slap);
                add_skill(SkillID::Wait);
                break;

            case NPCType::Caravan:
                speed = 0.6 + static_cast<double>(random_u32_inclusive(rng, 30)) / 100.0;
                inventory.set_capital(2000.0 + random_u32_inclusive(rng, 3000));
                life = max_life = 200 + static_cast<std::int32_t>(random_u32_inclusive(rng, 100));
                
                add_skill(SkillID::Wait);
                break;

            case NPCType::Bandit:
                speed = 1.0 + static_cast<double>(random_u32_inclusive(rng, 50)) / 100.0;
                life = max_life = 100 + static_cast<std::int32_t>(random_u32_inclusive(rng, 50));
                
                // Бандиты чаще бывают агрессивными
                if (random_u32_inclusive(rng, 100) > 80) is_special = true;

                add_skill(SkillID::Punch);
                add_skill(SkillID::Kick);
                add_skill(SkillID::DirtyBlow);
                
                if (is_special) {
                    add_skill(SkillID::Grope);
                    add_skill(SkillID::Insult);
                    add_skill(SkillID::Tease);
                }
                break;

            case NPCType::Guard:
                speed = 0.7;
                life = max_life = 150 + static_cast<std::int32_t>(random_u32_inclusive(rng, 50));
                
                add_skill(SkillID::Bash);
                add_skill(SkillID::ShieldBash);
                break;

            default:
                break;
        }
    }
};

class NPCManager
{
public:
    static constexpr std::size_t MAX_NPCS = 4096;
    static constexpr int VISION_RANGE = 6;
    
private:
    std::unique_ptr<NPC[]> npcs_;
    std::vector<std::size_t> free_ids_;
    std::int32_t next_id_ = 0;

    void rebuild_free_ids_()
    {
        free_ids_.clear();
        free_ids_.reserve(MAX_NPCS);
        for (std::size_t i = 0; i < MAX_NPCS; ++i)
        {
            if (!npcs_[i].active)
            {
                free_ids_.push_back(i);
            }
        }
    }
    
public:
    NPCManager()
        : npcs_(std::make_unique<NPC[]>(MAX_NPCS))
    {
        free_ids_.reserve(MAX_NPCS);
    }
    
    void init()
    {
        free_ids_.clear();
        for (std::size_t i = 0; i < MAX_NPCS; ++i)
        {
            npcs_[i].reset();
            free_ids_.push_back(i);
        }
        next_id_ = 0;
    }
    
    [[nodiscard]] NPC* spawn(NPCType type, int pos, int home_settlement, rng_t& rng)
    {
        if (free_ids_.empty()) return nullptr;
        
        const std::size_t slot = free_ids_.back();
        free_ids_.pop_back();
        
        NPC& npc = npcs_[slot];
        npc.reset();
        npc.id = next_id_++;
        npc.pos = pos;
        npc.prev_pos = pos;
        
        // Инициализируем визуальные координаты сразу в целевую клетку
        if (pos >= 0) {
            const int grid_x = pos / WORLD_WIDTH;
            const int grid_y = pos % WORLD_WIDTH;
            npc.visual_x = static_cast<float>(grid_x);
            npc.visual_y = static_cast<float>(grid_y);
        }

        npc.home_settlement = home_settlement;
        npc.active = true;
        npc.init_by_type(type, rng);
        
        return &npc;
    }
    
    void despawn(NPC* npc)
    {
        if (!npc || !npc->active) return;
        
        for (std::size_t i = 0; i < MAX_NPCS; ++i)
        {
            if (&npcs_[i] == npc)
            {
                npc->reset();
                free_ids_.push_back(i);
                return;
            }
        }
    }
    
    [[nodiscard]] std::size_t active_count() const noexcept
    {
        return MAX_NPCS - free_ids_.size();
    }

    void save(std::ostream& out) const
    {
        BinaryWriter writer(out);
        writer.write(next_id_);
        writer.write_bytes(npcs_.get(), sizeof(NPC) * MAX_NPCS);
    }

    void load(std::istream& in)
    {
        BinaryReader reader(in);
        reader.read(next_id_);
        reader.read_bytes(npcs_.get(), sizeof(NPC) * MAX_NPCS);
        rebuild_free_ids_();
    }
    
    void despawn_by_id(std::int32_t id)
    {
        for (std::size_t i = 0; i < MAX_NPCS; ++i)
        {
            if (npcs_[i].active && npcs_[i].id == id)
            {
                npcs_[i].reset();
                free_ids_.push_back(i);
                return;
            }
        }
    }

    // Метод для поиска NPC по ID (нужен для BattleState)
    [[nodiscard]] NPC* get_by_id(std::int32_t id)
    {
        for (std::size_t i = 0; i < MAX_NPCS; ++i)
        {
            if (npcs_[i].active && npcs_[i].id == id)
            {
                return &npcs_[i];
            }
        }
        return nullptr;
    }

    // Обработка сражений между NPC в одной клетке
    void resolve_npc_combat(const GameContext& ctx)
    {
        for (std::size_t i = 0; i < MAX_NPCS; ++i)
        {
            NPC& npc = npcs_[i];
            if (!npc.active || npc.state == NPCState::Dead) continue;

            // Оптимизация: проверяем только если в этой клетке больше 1 существа
            if (ctx.pos_map[npc.pos] <= 1) continue;

            // Ищем противника в этой же клетке
            for (std::size_t j = i + 1; j < MAX_NPCS; ++j)
            {
                NPC& other = npcs_[j];
                if (!other.active || other.pos != npc.pos || other.state == NPCState::Dead) continue;

                // Проверка вражды фракций
                bool is_enemy = (npc.faction == FactionID::Outlaws && other.faction == FactionID::Kingdom) ||
                                (npc.faction == FactionID::Kingdom && other.faction == FactionID::Outlaws);

                if (is_enemy)
                {
                    // Простейший расчет урона на основе типа
                    int dmg_to_other = 5 + (rand() % 10);
                    int dmg_to_self = 5 + (rand() % 10);

                    // Бонусы ролей
                    if (npc.type == NPCType::Guard || npc.type == NPCType::Bandit) dmg_to_other += 10;
                    if (other.type == NPCType::Guard || other.type == NPCType::Bandit) dmg_to_self += 10;
                    if (npc.type == NPCType::Caravan) dmg_to_self += 5; // Караваны беззащитны

                    other.life -= dmg_to_other;
                    npc.life -= dmg_to_self;
                    
                    // Если кто-то умер, он помечается в cleanup_dead_npcs позже
                }
            }
        }
    }

    // Поиск ближайшего враждебного NPC или игрока
    template <typename P>
    [[nodiscard]] int find_hostile_near(const NPC& npc, const P& player)
    {
        // 1. Проверяем игрока
        double dist_to_player = toroidal_distance(
            npc.pos / WORLD_WIDTH, npc.pos % WORLD_WIDTH,
            player.pos / WORLD_WIDTH, player.pos % WORLD_WIDTH
        );
        
        if (dist_to_player <= static_cast<double>(VISION_RANGE) && player.active) {
            // Проверяем репутацию игрока для этого NPC
            int rep = player.reputation[static_cast<size_t>(npc.faction)];
            if ((npc.faction == FactionID::Outlaws && rep < 20) || 
                (npc.faction == FactionID::Kingdom && rep < -30)) {
                return player.pos;
            }
        }

        // 2. Проверяем других NPC
        int closest_pos = -1;
        double min_dist = static_cast<double>(VISION_RANGE) + 1.0;

        for (size_t i = 0; i < MAX_NPCS; ++i) {
            NPC& other = npcs_[i];
            if (!other.active || other.id == npc.id || other.state == NPCState::Dead) continue;

            // Вражда между фракциями: Бандиты против Королевства
            bool is_enemy = (npc.faction == FactionID::Outlaws && other.faction == FactionID::Kingdom) ||
                            (npc.faction == FactionID::Kingdom && other.faction == FactionID::Outlaws);

            if (is_enemy) {
                double d = toroidal_distance(
                    npc.pos / WORLD_WIDTH, npc.pos % WORLD_WIDTH,
                    other.pos / WORLD_WIDTH, other.pos % WORLD_WIDTH
                );
                if (d < min_dist) {
                    min_dist = d;
                    closest_pos = other.pos;
                }
            }
        }
        return closest_pos;
    }
    
    template <typename P>
    void update_all(GameContext& ctx, LandmarkSystem& landmarks,
                    const TerrainType* relief, EntityManager& entities, const P& player)
    {
        for (std::size_t i = 0; i < MAX_NPCS; ++i)
        {
            NPC& npc = npcs_[i];
            if (!npc.active) continue;
            
            update_npc(npc, ctx, landmarks, relief, entities, player);
        }
    }
    
    template <typename P>
    void update_npc(NPC& npc, GameContext& ctx, LandmarkSystem& landmarks,
                    const TerrainType* relief, EntityManager& entities, const P& player)
    {
        if (npc.state == NPCState::Dead) return;
        
        if (npc.life <= 0)
        {
            npc.state = NPCState::Dead;
            return;
        }
        
        switch (npc.type)
        {
            case NPCType::Peasant:
                update_peasant(npc, ctx, landmarks, relief);
                break;
            case NPCType::Woodcutter:
                update_woodcutter(npc, ctx, landmarks, relief, entities);
                break;
            case NPCType::Merchant:
            case NPCType::Caravan:
                update_trader(npc, ctx, landmarks, relief);
                break;
            case NPCType::Bandit:
                update_bandit(npc, ctx, relief, player);
                break;
            case NPCType::Guard:
                update_guard(npc, ctx, landmarks, relief, player);
                break;
            default:
                break;
        }
    }
    
    void update_peasant(NPC& npc, GameContext& ctx, LandmarkSystem& /*landmarks*/,
                        const TerrainType* relief)
    {
        npc.move_progress += npc.speed;
        
        if (npc.move_progress < 100.0) return;
        npc.move_progress = 0.0;
        
        const int current_pos = npc.pos;
        const ResourceType local_res = get_local_resource(current_pos, ctx.rng);
        if (local_res != ResourceType::None && npc.inventory.can_add(local_res, 1))
        {
            npc.inventory.add(local_res, 1);
        }
        
        const Direction dir = static_cast<Direction>(random_u32_inclusive(ctx.rng, 3));
        const int next_pos = neighbor_from_pos(current_pos, dir);
        
        if (next_pos >= 0 && next_pos < WORLD_WIDTH * WORLD_WIDTH)
        {
            if (relief[next_pos] != TerrainType::Water && 
                relief[next_pos] != TerrainType::Mount)
            {
                npc.prev_pos = npc.pos;
                npc.pos = next_pos;
            }
        }
    }
    
    void update_trader(NPC& npc, GameContext& ctx, LandmarkSystem& landmarks,
                       const TerrainType* relief)
    {
        npc.move_progress += npc.speed;
        
        if (npc.state == NPCState::Idle)
        {
            npc.idle_timer++;
            if (npc.idle_timer > 50)
            {
                npc.idle_timer = 0;
                
                if (npc.home_settlement >= 0 && 
                    static_cast<std::size_t>(npc.home_settlement) < landmarks.settlement_count())
                {
                    npc.target_settlement = static_cast<std::int32_t>(
                        landmarks.find_random_destination(
                            static_cast<std::size_t>(npc.home_settlement), ctx.rng));
                    
                    if (npc.target_settlement != npc.home_settlement)
                    {
                        npc.state = NPCState::Traveling;
                        
                        for (std::size_t r = 1; r < RESOURCE_COUNT; ++r)
                        {
                            const auto res = static_cast<ResourceType>(r);
                            const std::int32_t amount = random_u32_inclusive(ctx.rng, 10) + 1;
                            if (npc.inventory.can_add(res, amount))
                            {
                                npc.inventory.add(res, amount);
                            }
                        }
                    }
                }
            }
            return;
        }
        
        if (npc.state == NPCState::Trading)
        {
            npc.trade_timer++;
            if (npc.trade_timer > 30)
            {
                npc.trade_timer = 0;
                npc.state = NPCState::Returning;
                npc.target_settlement = npc.home_settlement;
            }
            return;
        }
        
        if (npc.move_progress < 100.0) return;
        npc.move_progress = 0.0;
        
        const int target_idx = (npc.state == NPCState::Returning) 
            ? npc.home_settlement 
            : npc.target_settlement;
        
        if (target_idx < 0 || static_cast<std::size_t>(target_idx) >= landmarks.settlement_count())
        {
            npc.state = NPCState::Idle;
            return;
        }
        
        Settlement* target = landmarks.get_settlement(static_cast<std::size_t>(target_idx));
        if (!target)
        {
            npc.state = NPCState::Idle;
            return;
        }
        
        if (npc.pos == target->pos)
        {
            if (npc.state == NPCState::Traveling)
            {
                npc.state = NPCState::Trading;
                npc.trade_timer = 0;
                
                for (std::size_t r = 1; r < RESOURCE_COUNT; ++r)
                {
                    const auto res = static_cast<ResourceType>(r);
                    const std::int32_t amount = npc.inventory.get(res);
                    if (amount > 0)
                    {
                        const double price = target->market.sell_price(res) * static_cast<double>(amount);
                        npc.inventory.add_capital(price);
                        target->capital -= price;
                        target->market.record_sale(res, amount);
                    }
                }
                target->market.update_prices();
                npc.inventory.clear();
            }
            else if (npc.state == NPCState::Returning)
            {
                npc.state = NPCState::Idle;
                npc.idle_timer = 0;
            }
            return;
        }
        
        const auto dir = landmarks.get_direction_toward_landmark(
            npc.pos, static_cast<std::size_t>(target_idx));
        
        if (!dir)
        {
            const Direction random_dir = static_cast<Direction>(random_u32_inclusive(ctx.rng, 3));
            const int next_pos = neighbor_from_pos(npc.pos, random_dir);
            if (next_pos >= 0 && next_pos < WORLD_WIDTH * WORLD_WIDTH &&
                relief[next_pos] != TerrainType::Water &&
                relief[next_pos] != TerrainType::Mount)
            {
                npc.prev_pos = npc.pos;
                npc.pos = next_pos;
            }
            return;
        }
        
        const int next_pos = neighbor_from_pos(npc.pos, *dir);
        if (next_pos >= 0 && next_pos < WORLD_WIDTH * WORLD_WIDTH &&
            relief[next_pos] != TerrainType::Water &&
            relief[next_pos] != TerrainType::Mount)
        {
            npc.prev_pos = npc.pos;
            npc.pos = next_pos;
        }
    }

    [[nodiscard]] bool is_tree_claimed_by_other(int pos, std::int32_t self_id) const
    {
        for (std::size_t i = 0; i < MAX_NPCS; ++i)
        {
            const NPC& other = npcs_[i];
            if (!other.active || other.id == self_id || other.type != NPCType::Woodcutter) continue;
            if (other.state == NPCState::Dead) continue;
            if (other.target_settlement == pos &&
                (other.state == NPCState::Traveling || other.state == NPCState::Cutting))
            {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] bool is_tree_being_cut_by_other(int pos, std::int32_t self_id) const
    {
        for (std::size_t i = 0; i < MAX_NPCS; ++i)
        {
            const NPC& other = npcs_[i];
            if (!other.active || other.id == self_id || other.type != NPCType::Woodcutter) continue;
            if (other.state == NPCState::Dead) continue;
            if (other.state == NPCState::Cutting && other.target_settlement == pos)
            {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] int find_nearest_tree_pos(int from_pos, const EntityManager& entities,
                                            const TerrainType* relief, std::int32_t self_id) const
    {
        if (from_pos < 0 || from_pos >= WORLD_WIDTH * WORLD_WIDTH) return -1;
        if (!relief) return -1;

        const int world_size = WORLD_WIDTH * WORLD_WIDTH;
        const auto tree_type = static_cast<std::uint8_t>(ObjectType::Tree);
        std::vector<std::uint8_t> tree_map(static_cast<std::size_t>(world_size), 0);

        for (const auto& entity : entities.entities())
        {
            if (!entity.active || entity.type != tree_type) continue;
            if (entity.pos < 0 || entity.pos >= world_size) continue;
            tree_map[static_cast<std::size_t>(entity.pos)] = 1;
        }

        if (tree_map[static_cast<std::size_t>(from_pos)] &&
            !is_tree_claimed_by_other(from_pos, self_id))
        {
            return from_pos;
        }

        std::vector<int> dist(static_cast<std::size_t>(world_size), -1);
        std::vector<int> queue;
        queue.reserve(static_cast<std::size_t>(world_size));

        dist[static_cast<std::size_t>(from_pos)] = 0;
        queue.push_back(from_pos);

        int best_pos = -1;
        int best_dist = std::numeric_limits<int>::max();
        std::size_t head = 0;

        while (head < queue.size())
        {
            const int current = queue[head++];
            const int current_dist = dist[static_cast<std::size_t>(current)];
            if (current_dist > best_dist) break;

            if (tree_map[static_cast<std::size_t>(current)] &&
                !is_tree_claimed_by_other(current, self_id))
            {
                best_pos = current;
                best_dist = current_dist;
                continue;
            }

            for (int d = 0; d < 4; ++d)
            {
                const Direction dir = static_cast<Direction>(d);
                const int neighbor = neighbor_from_pos(current, dir);
                if (neighbor < 0 || neighbor >= world_size) continue;
                if (dist[static_cast<std::size_t>(neighbor)] != -1) continue;
                if (relief[neighbor] == TerrainType::Water || relief[neighbor] == TerrainType::Mount) continue;

                dist[static_cast<std::size_t>(neighbor)] = current_dist + 1;
                queue.push_back(neighbor);
            }
        }

        return best_pos;
    }

    [[nodiscard]] bool is_tree_at_pos(int pos, const EntityManager& entities) const
    {
        const auto tree_type = static_cast<std::uint8_t>(ObjectType::Tree);
        for (const auto& entity : entities.entities())
        {
            if (entity.active && entity.type == tree_type && entity.pos == pos)
            {
                return true;
            }
        }
        return false;
    }

    bool remove_tree_at_pos(int pos, EntityManager& entities) const
    {
        const auto tree_type = static_cast<std::uint8_t>(ObjectType::Tree);
        for (auto& entity : entities.entities())
        {
            if (entity.active && entity.type == tree_type && entity.pos == pos)
            {
                entities.destroy_entity(&entity);
                return true;
            }
        }
        return false;
    }

    void update_woodcutter(NPC& npc, GameContext& ctx, LandmarkSystem& landmarks,
                           const TerrainType* relief, EntityManager& entities)
    {
        npc.move_progress += npc.speed;
        if (npc.move_progress < 100.0) return;
        npc.move_progress = 0.0;

        auto move_random = [&]() {
            const Direction dir = static_cast<Direction>(random_u32_inclusive(ctx.rng, 3));
            const int next_pos = neighbor_from_pos(npc.pos, dir);
            if (next_pos >= 0 && next_pos < WORLD_WIDTH * WORLD_WIDTH &&
                relief[next_pos] != TerrainType::Water && relief[next_pos] != TerrainType::Mount)
            {
                npc.prev_pos = npc.pos;
                npc.pos = next_pos;
            }
        };

        const auto wood_amount = npc.inventory.get(ResourceType::Wood);
        if (wood_amount > 0)
        {
            if (npc.home_settlement < 0 ||
                static_cast<std::size_t>(npc.home_settlement) >= landmarks.settlement_count())
            {
                move_random();
                return;
            }

            Settlement* home = landmarks.get_settlement(static_cast<std::size_t>(npc.home_settlement));
            if (!home)
            {
                move_random();
                return;
            }

            if (npc.pos == home->pos)
            {
                const std::int32_t amount = npc.inventory.get(ResourceType::Wood);
                if (amount > 0)
                {
                    const double price = home->market.sell_price(ResourceType::Wood) * static_cast<double>(amount);
                    npc.inventory.remove(ResourceType::Wood, amount);
                    npc.inventory.capital += price;
                    home->capital -= price;
                    home->market.record_sale(ResourceType::Wood, amount);
                    home->market.update_prices();
                }
                npc.target_settlement = -1;
                npc.trade_timer = 0;
                npc.state = NPCState::Idle;
                return;
            }

            const auto dir = landmarks.get_direction_toward_landmark(
                npc.pos, static_cast<std::size_t>(npc.home_settlement));
            if (!dir)
            {
                move_random();
                return;
            }

            const int next_pos = neighbor_from_pos(npc.pos, *dir);
            if (next_pos >= 0 && next_pos < WORLD_WIDTH * WORLD_WIDTH &&
                relief[next_pos] != TerrainType::Water && relief[next_pos] != TerrainType::Mount)
            {
                npc.prev_pos = npc.pos;
                npc.pos = next_pos;
            }
            return;
        }

        if (npc.target_settlement >= 0 && !is_tree_at_pos(npc.target_settlement, entities))
        {
            npc.target_settlement = -1;
            npc.trade_timer = 0;
            npc.state = NPCState::Idle;
        }

        if (npc.target_settlement < 0)
        {
            npc.target_settlement = find_nearest_tree_pos(npc.pos, entities, relief, npc.id);
            npc.trade_timer = 0;
        }

        if (npc.target_settlement < 0)
        {
            move_random();
            return;
        }

        if (npc.pos == npc.target_settlement)
        {
            if (is_tree_being_cut_by_other(npc.target_settlement, npc.id))
            {
                npc.target_settlement = -1;
                npc.trade_timer = 0;
                npc.state = NPCState::Idle;
                move_random();
                return;
            }
            npc.state = NPCState::Cutting;
            npc.trade_timer++;
            if (npc.trade_timer >= 40)
            {
                npc.trade_timer = 0;
                if (remove_tree_at_pos(npc.target_settlement, entities))
                {
                    if (npc.inventory.can_add(ResourceType::Wood, 1))
                    {
                        npc.inventory.add(ResourceType::Wood, 1);
                    }
                    if (ctx.flora)
                    {
                        ctx.flora[npc.target_settlement] = 0;
                    }
                }
                npc.target_settlement = -1;
                npc.state = NPCState::Idle;
            }
            return;
        }

        npc.state = NPCState::Traveling;
        const Direction move_dir = get_dir_to(npc.pos, npc.target_settlement);
        const int next_pos = neighbor_from_pos(npc.pos, move_dir);
        if (next_pos >= 0 && next_pos < WORLD_WIDTH * WORLD_WIDTH &&
            relief[next_pos] != TerrainType::Water && relief[next_pos] != TerrainType::Mount)
        {
            npc.prev_pos = npc.pos;
            npc.pos = next_pos;
        }
        else
        {
            move_random();
        }
    }
    template <typename P>
    void update_bandit(NPC& npc, GameContext& ctx,
                       const TerrainType* relief, const P& player)
    {
        npc.move_progress += npc.speed;
        
        if (npc.move_progress < 100.0) return;
        npc.move_progress = 0.0;

        int target_pos = find_hostile_near(npc, player);
        Direction move_dir;

        if (target_pos != -1) {
            // Охота: определяем направление к цели
            int tx = target_pos / WORLD_WIDTH;
            int ty = target_pos % WORLD_WIDTH;
            int nx = npc.pos / WORLD_WIDTH;
            int ny = npc.pos % WORLD_WIDTH;

            int dx = tx - nx;
            int dy = ty - ny;

            // Учет тороидальности мира для кратчайшего пути
            if (std::abs(dx) > WORLD_WIDTH / 2) dx = (dx > 0) ? dx - WORLD_WIDTH : dx + WORLD_WIDTH;
            if (std::abs(dy) > WORLD_WIDTH / 2) dy = (dy > 0) ? dy - WORLD_WIDTH : dy + WORLD_WIDTH;

            if (std::abs(dx) > std::abs(dy)) {
                move_dir = (dx > 0) ? Direction::Right : Direction::Left;
            } else {
                move_dir = (dy > 1) ? Direction::Down : Direction::Up;
            }
        } else {
            // Мирное блуждание
            move_dir = static_cast<Direction>(random_u32_inclusive(ctx.rng, 3));
        }
        
        const int next_pos = neighbor_from_pos(npc.pos, move_dir);
        
        if (next_pos >= 0 && next_pos < WORLD_WIDTH * WORLD_WIDTH)
        {
            if (relief[next_pos] != TerrainType::Water && 
                relief[next_pos] != TerrainType::Mount)
            {
                npc.prev_pos = npc.pos;
                npc.pos = next_pos;
            }
        }
    }
    
    template <typename P>
    void update_guard(NPC& npc, GameContext& ctx, LandmarkSystem& landmarks,
                      const TerrainType* relief, const P& player)
    {
        npc.move_progress += npc.speed;
        if (npc.move_progress < 100.0) return;
        npc.move_progress = 0.0;

        // 1. Поиск врагов поблизости (Бандиты или злой игрок)
        int hostile_pos = find_hostile_near(npc, player);
        Direction move_dir;

        if (hostile_pos != -1) {
            // Агрессия: преследуем врага
            move_dir = get_dir_to(npc.pos, hostile_pos);
        } else {
            // 2. Если врагов нет, проверяем дистанцию до дома
            if (npc.home_settlement >= 0) {
                const auto* home = landmarks.get_settlement(static_cast<size_t>(npc.home_settlement));
                if (home) {
                    double dist = toroidal_distance(
                        npc.pos / WORLD_WIDTH, npc.pos % WORLD_WIDTH,
                        home->pos / WORLD_WIDTH, home->pos % WORLD_WIDTH
                    );

                    if (dist > 12.0) {
                        // Слишком далеко: возвращаемся к городу
                        auto dir_to_home = landmarks.get_direction_toward_landmark(npc.pos, static_cast<size_t>(npc.home_settlement));
                        move_dir = dir_to_home.has_value() ? *dir_to_home : static_cast<Direction>(rand() % 4);
                    } else {
                        // Патрулирование: случайное движение рядом с городом
                        move_dir = static_cast<Direction>(random_u32_inclusive(ctx.rng, 3));
                    }
                } else {
                    move_dir = static_cast<Direction>(random_u32_inclusive(ctx.rng, 3));
                }
            } else {
                move_dir = static_cast<Direction>(random_u32_inclusive(ctx.rng, 3));
            }
        }

        const int next_pos = neighbor_from_pos(npc.pos, move_dir);
        if (next_pos >= 0 && next_pos < WORLD_WIDTH * WORLD_WIDTH) {
            if (relief[next_pos] != TerrainType::Water && relief[next_pos] != TerrainType::Mount) {
                npc.prev_pos = npc.pos;
                npc.pos = next_pos;
            }
        }
    }

    // Вспомогательный метод для определения направления (тороидальный)
    [[nodiscard]] Direction get_dir_to(int from_pos, int to_pos)
    {
        int tx = to_pos / WORLD_WIDTH;
        int ty = to_pos % WORLD_WIDTH;
        int nx = from_pos / WORLD_WIDTH;
        int ny = from_pos % WORLD_WIDTH;
        int dx = tx - nx;
        int dy = ty - ny;
        if (std::abs(dx) > WORLD_WIDTH / 2) dx = (dx > 0) ? dx - WORLD_WIDTH : dx + WORLD_WIDTH;
        if (std::abs(dy) > WORLD_WIDTH / 2) dy = (dy > 0) ? dy - WORLD_WIDTH : dy + WORLD_WIDTH;
        if (std::abs(dx) > std::abs(dy)) return (dx > 0) ? Direction::Right : Direction::Left;
        return (dy > 0) ? Direction::Down : Direction::Up;
    }

    void rebuild_pos_map(std::vector<std::uint16_t>& pos_map) const
    {
        for (std::size_t i = 0; i < MAX_NPCS; ++i)
        {
            const NPC& npc = npcs_[i];
            if (!npc.active || npc.state == NPCState::Dead) continue;
            if (npc.pos < 0 || static_cast<std::size_t>(npc.pos) >= pos_map.size()) continue;
            if (pos_map[npc.pos] < std::numeric_limits<std::uint16_t>::max())
            {
                pos_map[npc.pos] += 1;
            }
        }
    }
    
    [[nodiscard]] NPC* find_at(int pos)
    {
        for (std::size_t i = 0; i < MAX_NPCS; ++i)
        {
            if (npcs_[i].active && npcs_[i].pos == pos)
            {
                return &npcs_[i];
            }
        }
        return nullptr;
    }
    
    [[nodiscard]] std::vector<NPC*> find_all_at(int pos)
    {
        std::vector<NPC*> result;
        for (std::size_t i = 0; i < MAX_NPCS; ++i)
        {
            if (npcs_[i].active && npcs_[i].pos == pos)
            {
                result.push_back(&npcs_[i]);
            }
        }
        return result;
    }
    
    [[nodiscard]] NPC& operator[](std::size_t idx) noexcept { return npcs_[idx]; }
    [[nodiscard]] const NPC& operator[](std::size_t idx) const noexcept { return npcs_[idx]; }
    
    template<typename Func>
    void for_each_active(Func&& func)
    {
        NPC* const data = npcs_.get();
        for (std::size_t i = 0; i < MAX_NPCS; ++i)
        {
            if (data[i].active)
            {
                func(data[i]);
            }
        }
    }
    
    template<typename Func>
    void for_each_active(Func&& func) const
    {
        const NPC* const data = npcs_.get();
        for (std::size_t i = 0; i < MAX_NPCS; ++i)
        {
            if (data[i].active)
            {
                func(data[i]);
            }
        }
    }
};

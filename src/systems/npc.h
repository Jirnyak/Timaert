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
#include "core/binary_io.h"
#include "systems/skills.h"

struct Player;

// NPCType and NPCState are defined in core/types.h (included via game_context.h)

struct NPC
{
    std::int32_t id = -1;
    TilePosition pos = INVALID_POS;
    TilePosition prev_pos = INVALID_POS;
    
    char name[32]{0};      // Имя персонажа
    char personality[32]{0}; // Характер (Brave, Cowardly, Lustful, etc.)
    NPCType type = NPCType::None;
    NPCState state = NPCState::Idle;
    FactionID faction = FactionID::Neutral;
    
    std::int32_t home_settlement_idx = -1;
    std::int32_t target_settlement_idx = -1;
    TilePosition target_tree_pos = INVALID_POS;  // For woodcutters
    
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
        pos = INVALID_POS;
        prev_pos = INVALID_POS;
        std::memset(name, 0, sizeof(name));
        std::memset(personality, 0, sizeof(personality));
        type = NPCType::None;
        state = NPCState::Idle;
        faction = FactionID::Neutral;
        home_settlement_idx = -1;
        target_settlement_idx = -1;
        target_tree_pos = INVALID_POS;
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
    
    [[nodiscard]] NPC* spawn(NPCType type, TilePosition spawn_pos, int home_settlement_idx_id, rng_t& rng)
    {
        if (free_ids_.empty()) return nullptr;
        
        const std::size_t slot = free_ids_.back();
        free_ids_.pop_back();
        
        NPC& npc = npcs_[slot];
        npc.reset();
        npc.id = next_id_++;
        npc.pos = spawn_pos;
        npc.prev_pos = spawn_pos;
        
        // Инициализируем визуальные координаты сразу в целевую клетку
        if (is_valid(spawn_pos)) {
            npc.visual_x = static_cast<float>(spawn_pos.x);
            npc.visual_y = static_cast<float>(spawn_pos.y);
        }

        npc.home_settlement_idx = home_settlement_idx_id;
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
    [[nodiscard]] TilePosition find_hostile_near(const NPC& npc, const P& player)
    {
        // 1. Проверяем игрока
        double dist_to_player = toroidal_distance(npc.pos, player.pos);
        
        if (dist_to_player <= static_cast<double>(VISION_RANGE) && player.active) {
            // Проверяем репутацию игрока для этого NPC
            int rep = player.reputation[static_cast<size_t>(npc.faction)];
            if ((npc.faction == FactionID::Outlaws && rep < 20) || 
                (npc.faction == FactionID::Kingdom && rep < -30)) {
                return player.pos;
            }
        }

        // 2. Проверяем других NPC
        TilePosition closest_pos = INVALID_POS;
        double min_dist = static_cast<double>(VISION_RANGE) + 1.0;

        for (size_t i = 0; i < MAX_NPCS; ++i) {
            NPC& other = npcs_[i];
            if (!other.active || other.id == npc.id || other.state == NPCState::Dead) continue;

            // Вражда между фракциями: Бандиты против Королевства
            bool is_enemy = (npc.faction == FactionID::Outlaws && other.faction == FactionID::Kingdom) ||
                            (npc.faction == FactionID::Kingdom && other.faction == FactionID::Outlaws);

            if (is_enemy) {
                double d = toroidal_distance(npc.pos, other.pos);
                if (d < min_dist) {
                    min_dist = d;
                    closest_pos = other.pos;
                }
            }
        }
        return closest_pos;
    }
    
    // NOTE: Legacy update methods removed - ECS handles all NPC AI via ecs::update_all_npc_ai()
    // See src/ecs/systems/ai_system.h for the active implementation

    void rebuild_pos_map(WorldMap<std::uint16_t>& pos_map) const
    {
        for (std::size_t i = 0; i < MAX_NPCS; ++i)
        {
            const NPC& npc = npcs_[i];
            if (!npc.active || npc.state == NPCState::Dead) continue;
            if (!is_valid(npc.pos)) continue;
            if (pos_map[npc.pos] < std::numeric_limits<std::uint16_t>::max())
            {
                pos_map[npc.pos] += 1;
            }
        }
    }
    
    [[nodiscard]] NPC* find_at(TilePosition pos)
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
    
    [[nodiscard]] std::vector<NPC*> find_all_at(TilePosition pos)
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
    [[nodiscard]] static constexpr std::size_t max_npcs() noexcept { return MAX_NPCS; }
    
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

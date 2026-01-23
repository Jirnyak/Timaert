#pragma once

#include <cstdint>
#include <vector>
#include "core/game_context.h"
#include "systems/economy.h"
#include "systems/landmark.h"
#include "systems/skills.h"
#include "systems/npc.h"

struct Player
{
    TilePosition pos = INVALID_POS;
    TilePosition prev_pos = INVALID_POS;
    TilePosition aim_pos = INVALID_POS;
    
    Inventory inventory;
    PlayerState state = PlayerState::Normal;
    
    double speed = 1.0;
    double move_progress = 0.0;
    float visual_x = 0.0f;
    float visual_y = 0.0f;
    std::int32_t life = 100;
    std::int32_t max_life = 100;
    
    // --- Ролевые характеристики ---
    Gender gender = Gender::Male;
    Race race = Race::Human;
    
    std::int32_t lust = 0;
    std::int32_t max_lust = 100;
    std::int32_t will = 100;
    std::int32_t max_will = 100;

    // Репутация: индекс соответствует FactionID
    std::int32_t reputation[static_cast<std::size_t>(FactionID::Count)];
    
    // Навыки (фиксированный массив для бинарной совместимости сохранения)
    static constexpr std::size_t MAX_PLAYER_SKILLS = 32;
    SkillID skills[MAX_PLAYER_SKILLS];
    std::uint8_t skill_count = 0;
    
    bool active = false;
    
    void init(TilePosition start_pos, rng_t& rng);
    void learn_skill(SkillID skill);
    void set_aim(TilePosition target_pos);
    void clear_aim();
    [[nodiscard]] bool has_aim() const noexcept;
    [[nodiscard]] bool is_at_aim() const noexcept;
};

class PlayerController
{
private:
    Player player_;
    std::int32_t current_settlement_idx_ = -1;
    std::vector<TilePosition> path_{};
    std::size_t path_index_ = 0;

    // --- Логика коллизий ---
    // Проверка: можно ли шагнуть в клетку, или там враг?
    bool check_collision_and_trigger(TilePosition target_pos, NPCManager& npcs, GameContext& ctx);
       // NPC* npc = npcs.find_at(target_pos);
       // if (npc && npc->active && npc->state != NPCState::Dead)
     //   {
            // Проверка репутации игрока с фракцией NPC
          //  const int rep = player_.reputation[static_cast<std::size_t>(npc->faction)];
         //   bool hostile = false;

            // Логика агрессивности фракций:
           // if (npc->faction == FactionID::Outlaws) {
                // Разбойники нападают на всех, кого не считают "своими" (нужна репутация > 20)
              //  if (rep < 20) hostile = true;
          //  } 
          //  else if (npc->faction == FactionID::Wilderness) {
                // Дикие существа атакуют почти всегда
             //   if (rep < 50) hostile = true;
          //  } 
            //else {
                // Мирные фракции (Kingdom) атакуют только явных врагов (репутация ниже -30)
          //      if (rep < -30) hostile = true;
         //   }

         //   if (hostile)
       //     {
          //      ctx.battle_target_id = npc->id;
           //     ctx.game_mod = GameMode::Fight;
                
              //  player_.clear_aim();
             //   path_.clear();
           //     path_index_ = 0;
           //     return true; 
          //  }
      //  }
      //  return false; 
  // }
    // -----------------------
    
public:
    PlayerController() = default;
    
    void init(TilePosition start_pos, rng_t& rng);
    
    void update(GameContext& ctx, LandmarkSystem& landmarks, NPCManager& npcs);
    
    void move_toward_direct(GameContext& ctx, NPCManager& npcs);
    
    [[nodiscard]] static bool can_move_to(TilePosition pos, const WorldMap<TerrainType>& relief) noexcept;

    struct TerrainEffect {
        float speed_mult = 1.0f;
        int will_drain = 0;
    };

    [[nodiscard]] TerrainEffect get_terrain_effect(TerrainType type) const noexcept;
    
    void move_direction(Direction dir, NPCManager& npcs, GameContext& ctx);

    [[nodiscard]] bool set_path_to(GameContext& ctx, TilePosition target_pos);

    void clear_path();

    void clear_aim();
    
    [[nodiscard]] bool try_buy(ResourceType res, std::int32_t amount, Settlement& settlement);
    
    [[nodiscard]] bool try_sell(ResourceType res, std::int32_t amount, Settlement& settlement);
    
    [[nodiscard]] Player& player() noexcept;
    [[nodiscard]] const Player& player() const noexcept;
    [[nodiscard]] std::int32_t current_settlement() const noexcept;
    void set_current_settlement(std::int32_t settlement_idx) noexcept;
    [[nodiscard]] bool is_in_settlement() const noexcept;
};

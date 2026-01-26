#pragma once

#include "ecs/world.h"
#include "ecs/components/core.h"
#include "rendering/texture_manager.h"
#include <SDL_rect.h>
#include <SDL_render.h>

namespace ecs {

struct RenderContext {
    SDL_Renderer* renderer;
    TextureManager* textures;
    float cam_x;
    float cam_y;
    int center_x;
    int center_y;
    int scaled_tile_size;
    int window_width;
    int window_height;
};

inline void render_all_npcs_ecs(World& world, const RenderContext& rc) {
    auto view = world.registry.view<Position, VisualPos, NPCTag, AIBehavior, Active>(entt::exclude<Dead>);
    
    for (auto entity : view) {
        const auto& pos = view.get<Position>(entity);
        const auto& visual = view.get<VisualPos>(entity);
        const auto& npc_tag = view.get<NPCTag>(entity);
        const auto& ai = view.get<AIBehavior>(entity);
        if (!is_valid(pos.tile)) continue;
        
        float dx = visual.x - rc.cam_x;
        if (dx > WORLD_WIDTH / 2.0f) dx -= WORLD_WIDTH;
        if (dx < -WORLD_WIDTH / 2.0f) dx += WORLD_WIDTH;
        
        float dy = visual.y - rc.cam_y;
        if (dy > WORLD_WIDTH / 2.0f) dy -= WORLD_WIDTH;
        if (dy < -WORLD_WIDTH / 2.0f) dy += WORLD_WIDTH;
        
        SDL_Rect draw_tile;
        draw_tile.w = rc.scaled_tile_size;
        draw_tile.h = rc.scaled_tile_size;
        draw_tile.x = rc.center_x + static_cast<int>(dx * static_cast<float>(rc.scaled_tile_size)) - rc.scaled_tile_size / 2;
        draw_tile.y = rc.center_y + static_cast<int>(dy * static_cast<float>(rc.scaled_tile_size)) - rc.scaled_tile_size / 2;
        
        if (draw_tile.x + rc.scaled_tile_size <= 0 || draw_tile.x >= rc.window_width ||
            draw_tile.y + rc.scaled_tile_size <= 0 || draw_tile.y >= rc.window_height) continue;
        
        ObjectType obj_type = ObjectType::Peasant;
        if (npc_tag.type == NPCType::Peasant) obj_type = ObjectType::Peasant;
        else if (npc_tag.type == NPCType::Woodcutter) obj_type = ObjectType::Woodcutter;
        else if (npc_tag.type == NPCType::Merchant) obj_type = ObjectType::Merchant;
        else if (npc_tag.type == NPCType::Caravan) obj_type = ObjectType::Caravan;
        else if (npc_tag.type == NPCType::Bandit) obj_type = ObjectType::Bandit;
        else if (npc_tag.type == NPCType::Guard) obj_type = ObjectType::Guard;
        
        SDL_RenderCopy(rc.renderer, rc.textures->sprite(static_cast<std::size_t>(obj_type)), nullptr, &draw_tile);
        
        if (npc_tag.type == NPCType::Woodcutter && ai.state == NPCState::Cutting) {
            constexpr float kCutDuration = 40.0f;
            const float progress = std::min(1.0f, static_cast<float>(ai.action_timer) / kCutDuration);
            const int bar_height = std::max(2, rc.scaled_tile_size / 6);
            const int bar_width = rc.scaled_tile_size;
            const int bar_x = draw_tile.x;
            const int bar_y = draw_tile.y - bar_height - 2;
            
            SDL_Rect bar_bg{bar_x, bar_y, bar_width, bar_height};
            SDL_Rect bar_fg{bar_x + 1, bar_y + 1,
                            std::max(0, static_cast<int>((bar_width - 2) * progress)),
                            std::max(0, bar_height - 2)};
            
            SDL_SetRenderDrawBlendMode(rc.renderer, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(rc.renderer, 27, 27, 27, 204);
            SDL_RenderFillRect(rc.renderer, &bar_bg);
            SDL_SetRenderDrawColor(rc.renderer, 123, 210, 71, 255);
            SDL_RenderFillRect(rc.renderer, &bar_fg);
        }
    }
}

inline void update_npc_visuals_ecs(World& world, float delta_time) {
    auto view = world.registry.view<Position, VisualPos, Active>();
    
    for (auto entity : view) {
        auto& pos = view.get<Position>(entity);
        auto& visual = view.get<VisualPos>(entity);
        if (!is_valid(pos.tile)) continue;
        
        float target_x = static_cast<float>(pos.tile.x);
        float target_y = static_cast<float>(pos.tile.y);
        
        // Handle toroidal wrapping for interpolation
        float dx = target_x - visual.x;
        if (dx > WORLD_WIDTH / 2.0f) dx -= WORLD_WIDTH;
        if (dx < -WORLD_WIDTH / 2.0f) dx += WORLD_WIDTH;
        
        float dy = target_y - visual.y;
        if (dy > WORLD_WIDTH / 2.0f) dy -= WORLD_WIDTH;
        if (dy < -WORLD_WIDTH / 2.0f) dy += WORLD_WIDTH;
        
        // Smooth interpolation
        float step_x = dx * 0.15f * delta_time;
        float step_y = dy * 0.15f * delta_time;
        
        if (std::abs(dx) > 0.01f || std::abs(dy) > 0.01f) {
            visual.x += step_x;
            visual.y += step_y;
        }
        
        // Keep in valid range
        if (visual.x < 0) visual.x += WORLD_WIDTH;
        if (visual.x >= WORLD_WIDTH) visual.x -= WORLD_WIDTH;
        if (visual.y < 0) visual.y += WORLD_WIDTH;
        if (visual.y >= WORLD_WIDTH) visual.y -= WORLD_WIDTH;
    }
}

} // namespace ecs

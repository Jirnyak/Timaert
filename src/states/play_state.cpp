#include "states/play_state.h"
#include "systems/world_manager.h"
#include "systems/economy.h"
#include "rendering/lod_system.h"
#include "rendering/ra_icon.h"
#include "rendering/texture_manager.h"
#include "ecs/systems/render_system.h"
#include "ecs/systems/spawn_system.h"
#include "ecs/components/core.h"
#include "ecs/components/entity.h"
#include "ecs/components/npc.h"
#include "ecs/world.h"
#include <SDL_blendmode.h>
#include <SDL_keycode.h>
#include <SDL_pixels.h>
#include <SDL_render.h>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <span>
#include <vector>
#include <array>
#include <entt/entt.hpp>

void PlayState::render_all_npcs(GameContext& ctx,
                                TextureManager& textures,
                                int scaled_tile_size,
                                int /*visible_epoch*/) {
    if (!ctx.ecs_world)
        return;

    ecs::RenderContext rc{ctx.renderer,
                          &textures,
                          static_cast<float>(ctx.pos_cam.x),
                          static_cast<float>(ctx.pos_cam.y),
                          ctx.window_width / 2 + static_cast<int>(ctx.map_offset_x * ctx.zoom),
                          ctx.window_height / 2 + static_cast<int>(ctx.map_offset_y * ctx.zoom),
                          scaled_tile_size,
                          ctx.window_width,
                          ctx.window_height};

    ecs::render_all_npcs_ecs(*ctx.ecs_world, rc);
}

void PlayState::render_entities(GameContext& ctx,
                                TextureManager& textures,
                                int scaled_tile_size,
                                int visible_epoch) {
    if (!ctx.ecs_world)
        return;

    auto view = ctx.ecs_world->registry.view<ecs::Position, ecs::ObjectSprite, ecs::Active>();
    for (auto entity : view) {
        const auto& pos = view.get<ecs::Position>(entity);
        const auto& sprite = view.get<ecs::ObjectSprite>(entity);
        if (!is_valid(pos.tile))
            continue;
        if (visible_epoch_[pos.tile] != visible_epoch)
            continue;

        SDL_Rect draw_tile;
        const SDL_Point& pt = visible_points_[pos.tile];
        draw_tile.x = pt.x;
        draw_tile.y = pt.y;
        draw_tile.w = scaled_tile_size;
        draw_tile.h = scaled_tile_size;
        SDL_RenderCopy(ctx.renderer,
                       textures.sprite(static_cast<int>(sprite.type)),
                       nullptr,
                       &draw_tile);
    }
}

void PlayState::render_settlements(GameContext& ctx,
                                   TextureManager& textures,
                                   int scaled_tile_size,
                                   int visible_epoch) {
    if (!ctx.world_manager)
        return;

    for (const auto& settlement : ctx.world_manager->landmarks.settlements()) {
        if (!is_valid(settlement.pos))
            continue;
        if (visible_epoch_[settlement.pos] != visible_epoch)
            continue;

        ObjectType obj_type = ObjectType::Village;
        if (settlement.type == SettlementType::City)
            obj_type = ObjectType::City;
        else if (settlement.type == SettlementType::Town)
            obj_type = ObjectType::Town;

        SDL_Rect draw_tile;
        const SDL_Point& pt = visible_points_[settlement.pos];
        draw_tile.x = pt.x;
        draw_tile.y = pt.y;
        draw_tile.w = scaled_tile_size;
        draw_tile.h = scaled_tile_size;
        SDL_RenderCopy(ctx.renderer,
                       textures.sprite(static_cast<std::size_t>(obj_type)),
                       nullptr,
                       &draw_tile);
    }
}

void PlayState::render_player(GameContext& ctx,
                              TextureManager& textures,
                              int scaled_tile_size,
                              int /*visible_epoch*/) {
    if (!ctx.world_manager)
        return;

    const Player& p = ctx.world_manager->player_ctrl.player();
    if (!p.active)
        return;

    const float cam_x = static_cast<float>(ctx.pos_cam.x);
    const float cam_y = static_cast<float>(ctx.pos_cam.y);
    int center_x = ctx.window_width / 2 + static_cast<int>(ctx.map_offset_x * ctx.zoom);
    int center_y = ctx.window_height / 2 + static_cast<int>(ctx.map_offset_y * ctx.zoom);

    float dx = p.visual_x - cam_x;
    if (dx > WORLD_WIDTH / 2.0f)
        dx -= WORLD_WIDTH;
    if (dx < -WORLD_WIDTH / 2.0f)
        dx += WORLD_WIDTH;

    float dy = p.visual_y - cam_y;
    if (dy > WORLD_WIDTH / 2.0f)
        dy -= WORLD_WIDTH;
    if (dy < -WORLD_WIDTH / 2.0f)
        dy += WORLD_WIDTH;

    SDL_Rect draw_tile;
    draw_tile.w = scaled_tile_size;
    draw_tile.h = scaled_tile_size;
    draw_tile.x = center_x + static_cast<int>(dx * static_cast<float>(scaled_tile_size))
                  - scaled_tile_size / 2;
    draw_tile.y = center_y + static_cast<int>(dy * static_cast<float>(scaled_tile_size))
                  - scaled_tile_size / 2;

    SDL_RenderCopy(ctx.renderer,
                   textures.sprite(static_cast<std::size_t>(ObjectType::Player)),
                   nullptr,
                   &draw_tile);
}

void PlayState::init_buttons(GameContext& ctx) {
    buttons_.clear();

    const UiButtonLayout layout = ui_default_button_layout(ctx);
    const int btn_size = layout.btn_size;
    const int margin = layout.margin;

    ui_init_speed_buttons(buttons_, ctx, 4);

    buttons_.add(UIButton{{margin, layout.speed_y, btn_size, btn_size},
                          "",
                          [this]() { request_pause(); },
                          nullptr,
                          RaIcon::Cog});

    ui_init_move_buttons(
        move_buttons_,
        ctx,
        [this]() { pending_move_dir_ = Direction::Up; },
        [this]() { pending_move_dir_ = Direction::Left; },
        [this]() { request_center(); },
        [this]() { pending_move_dir_ = Direction::Right; },
        [this]() { pending_move_dir_ = Direction::Down; });

    action_buttons_.clear();
    const int action_x = ctx.window_width - btn_size - margin;
    action_buttons_.add(UIButton{{action_x, layout.move_start_y, btn_size, btn_size},
                                 "$",
                                 [this]() { show_trade_ui_ = !show_trade_ui_; },
                                 [this]() { return show_trade_ui_; }});

    action_buttons_.add(
        UIButton{{action_x, layout.move_start_y + btn_size + margin, btn_size, btn_size},
                 "?",
                 [this]() { request_stat(); },
                 nullptr});

    buttons_initialized_ = true;
}

void PlayState::move_player_direction(Direction dir, GameContext& ctx) {
    if (!ctx.world_manager)
        return;

    ctx.world_manager->player_ctrl.move_direction(dir, ctx);

    ctx.world_manager->player_ctrl.clear_aim();
    player_destination_ = INVALID_POS;
}

void PlayState::center_on_player(GameContext& ctx) {
    if (!ctx.world_manager)
        return;
    const Player& p = ctx.world_manager->player_ctrl.player();
    if (p.active) {
        ctx.pos_cam = p.pos;
        reset_map_view(ctx);
    }
}

void PlayState::handle_tap_to_move(GameContext& ctx, int screen_x, int screen_y) {
    if (!ctx.world_manager)
        return;
    Player& p = ctx.world_manager->player_ctrl.player();
    if (!p.active)
        return;

    const int tile_size = scaled_tile_size(TILE_SIZE, ctx.zoom);
    const int pixel_offset_x = static_cast<int>(ctx.map_offset_x * ctx.zoom);
    const int pixel_offset_y = static_cast<int>(ctx.map_offset_y * ctx.zoom);
    const TileView view = make_tile_view(ctx, tile_size, pixel_offset_x, pixel_offset_y);
    const TilePosition target_tile = screen_to_world_pos(ctx, screen_x, screen_y, view);
    if (!is_valid(target_tile))
        return;

    if (ctx.world_manager->player_ctrl.set_path_to(ctx, target_tile)) {
        player_destination_ = target_tile;
    }
}

void PlayState::handle_event(SDL_Event& event, GameContext& ctx, TextureManager& /*textures*/) {
    if (!buttons_initialized_)
        init_buttons(ctx);

    InputEvent evt;
    if (input_manager_.process_event(event, ctx, evt)) {
        auto trade_panel_contains = [this, &ctx](int x, int y) {
            return ui_point_in_rect(x, y, trade_panel_rect(ctx));
        };

        switch (evt.action) {
            case InputAction::Press: {
                if (show_trade_ui_) {
                    if (!trade_panel_contains(evt.x, evt.y)) {
                        show_trade_ui_ = false;
                    }
                    end_map_drag(ctx);
                    ui_click_consumed_ = true;
                    return;
                }

                if (buttons_.handle_press(evt.x, evt.y)) {
                    ui_click_consumed_ = true;
                    return;
                }
                if (move_buttons_.handle_press(evt.x, evt.y)) {
                    ui_click_consumed_ = true;
                    return;
                }
                if (action_buttons_.handle_press(evt.x, evt.y)) {
                    ui_click_consumed_ = true;
                    return;
                }
                if (ctx.ui_hit_test.contains(evt.x, evt.y)) {
                    ui_click_consumed_ = true;
                    end_map_drag(ctx);
                    return;
                }

                begin_map_drag(ctx);
                ui_click_consumed_ = false;
                break;
            }

            case InputAction::Drag: {
                apply_map_drag(ctx,
                               static_cast<float>(evt.dx),
                               static_cast<float>(evt.dy),
                               1.0f / ctx.zoom);
                break;
            }

            case InputAction::Click: {
                buttons_.reset_pressed();
                move_buttons_.reset_pressed();
                action_buttons_.reset_pressed();
                if (show_trade_ui_ || ui_click_consumed_
                    || ctx.ui_hit_test.contains(evt.x, evt.y)) {
                    ui_click_consumed_ = false;
                    end_map_drag(ctx);
                    break;
                }

                handle_tap_to_move(ctx, evt.x, evt.y);

                end_map_drag(ctx);
                break;
            }

            case InputAction::Release: {
                buttons_.reset_pressed();
                move_buttons_.reset_pressed();
                action_buttons_.reset_pressed();
                end_map_drag(ctx);
                ui_click_consumed_ = false;
                break;
            }

            case InputAction::Zoom: {
                ctx.target_zoom *= evt.zoom;
                if (ctx.target_zoom < ctx.min_zoom)
                    ctx.target_zoom = ctx.min_zoom;
                if (ctx.target_zoom > ctx.max_zoom)
                    ctx.target_zoom = ctx.max_zoom;
                break;
            }

            default:
                break;
        }
    }

    if (event.type == SDL_KEYDOWN) {
        switch (event.key.keysym.sym) {
            case SDLK_ESCAPE:
                if (current_game_mode(ctx) != GameMode::Pause)
                    push_state(ctx, StateRegistry::instance().create(GameMode::Pause));
                break;
            case SDLK_0:
                handle_fullscreen_key(ctx, event.key.keysym.sym);
                break;
            case SDLK_SPACE:
                ctx.paused = !ctx.paused;
                break;
            case SDLK_k:
                trigger_screenshot(ctx);
                break;
            case SDLK_c: {
                [[maybe_unused]] const bool input_result = inputbox(ctx,
                                                                    ctx.window_width / 2,
                                                                    ctx.window_height / 2,
                                                                    200,
                                                                    100,
                                                                    ctx.input,
                                                                    0);
                break;
            }
            case SDLK_p:
                ctx.freecam = !ctx.freecam;
                break;
            case SDLK_m:
                push_state(ctx, StateRegistry::instance().create(GameMode::Map));
                break;
            case SDLK_TAB:
            case SDLK_i:
                push_state(ctx, StateRegistry::instance().create(GameMode::Stat));
                break;
            case SDLK_UP:
                move_player_direction(Direction::Up, ctx);
                break;
            case SDLK_LEFT:
                move_player_direction(Direction::Left, ctx);
                break;
            case SDLK_DOWN:
                move_player_direction(Direction::Down, ctx);
                break;
            case SDLK_RIGHT:
                move_player_direction(Direction::Right, ctx);
                break;
            default:
                break;
        }
    }
}

void PlayState::update(GameContext& ctx, TextureManager& /*textures*/) {
    bool needs_redraw = false;
    if (ctx.window_width != last_buttons_width_ || ctx.window_height != last_buttons_height_) {
        buttons_initialized_ = false;
        init_buttons(ctx);
        last_buttons_width_ = ctx.window_width;
        last_buttons_height_ = ctx.window_height;
        ctx.window_dirty = false;
        needs_redraw = true;
    }

    if (pause_pending_) {
        pause_pending_ = false;
        if (current_game_mode(ctx) != GameMode::Pause)
            push_state(ctx, StateRegistry::instance().create(GameMode::Pause));
    }
    if (stat_pending_) {
        stat_pending_ = false;
        push_state(ctx, StateRegistry::instance().create(GameMode::Stat));
    }

    const float prev_zoom = ctx.zoom;
    const float prev_offset_x = ctx.map_offset_x;
    const float prev_offset_y = ctx.map_offset_y;
    const TilePosition prev_cam = ctx.pos_cam;
    float delta_time = calc_frame_delta_time(ctx);

    if (pending_move_dir_) {
        move_player_direction(*pending_move_dir_, ctx);
        pending_move_dir_.reset();
        needs_redraw = true;
    }

    if (center_pending_) {
        center_on_player(ctx);
        center_pending_ = false;
        needs_redraw = true;
    }

    ctx.zoom += (ctx.target_zoom - ctx.zoom) * ctx.zoom_speed * delta_time;

    update_map_inertia(ctx, delta_time);
    const int tile_size = TILE_SIZE;
    while (ctx.map_offset_x >= tile_size) {
        ctx.pos_cam = ctx.get_neighbor(ctx.pos_cam, Direction::Left);
        ctx.map_offset_x -= tile_size;
    }
    while (ctx.map_offset_x <= -tile_size) {
        ctx.pos_cam = ctx.get_neighbor(ctx.pos_cam, Direction::Right);
        ctx.map_offset_x += tile_size;
    }
    while (ctx.map_offset_y <= -tile_size) {
        ctx.pos_cam = ctx.get_neighbor(ctx.pos_cam, Direction::Down);
        ctx.map_offset_y += tile_size;
    }
    while (ctx.map_offset_y >= tile_size) {
        ctx.pos_cam = ctx.get_neighbor(ctx.pos_cam, Direction::Up);
        ctx.map_offset_y -= tile_size;
    }

    if (!ctx.paused) {
        needs_redraw = true;
    }
    if (ctx.map_dragging || ctx.velocity_x != 0.0f || ctx.velocity_y != 0.0f) {
        needs_redraw = true;
    }
    if (std::abs(ctx.zoom - prev_zoom) > 0.0001f) {
        needs_redraw = true;
    }
    if (ctx.map_offset_x != prev_offset_x || ctx.map_offset_y != prev_offset_y) {
        needs_redraw = true;
    }
    if (ctx.pos_cam != prev_cam) {
        needs_redraw = true;
    }

    if (ctx.world_manager) {
        auto update_visuals = [&](float& v_x, float& v_y, TilePosition target_pos, float dt) {
            float target_x = static_cast<float>(target_pos.x);
            float target_y = static_cast<float>(target_pos.y);

            auto interpolate_wrapped = [](float& current, float target, int size, float delta) {
                float diff = target - current;
                if (diff > size / 2.0f)
                    diff -= size;
                if (diff < -size / 2.0f)
                    diff += size;

                float step = diff * 0.15f * delta;
                current += step;

                if (current < 0)
                    current += size;
                if (current >= size)
                    current -= size;

                return std::abs(diff) > 0.01f;
            };

            bool moving_x = interpolate_wrapped(v_x, target_x, WORLD_WIDTH, dt);
            bool moving_y = interpolate_wrapped(v_y, target_y, WORLD_WIDTH, dt);
            return moving_x || moving_y;
        };

        Player& p = ctx.world_manager->player_ctrl.player();
        if (p.active) {
            if (update_visuals(p.visual_x, p.visual_y, p.pos, delta_time)) {
                needs_redraw = true;
            }
        }
    }

    if (ctx.ecs_world) {
        ecs::update_npc_visuals_ecs(*ctx.ecs_world, delta_time);
        needs_redraw = true;
    }

    ctx.redraw_requested = ctx.redraw_requested || needs_redraw;

    if (!ctx.paused) {
        for (int tick = 0; tick < ctx.speed(); ++tick) {
            ctx.set_ticks(ctx.ticks() + 1);

            if (ctx.world_manager) {
                ctx.world_manager->update(ctx);
            }

            ctx.pos_map.fill(0);
            if (ctx.world_manager) {
                ctx.world_manager->rebuild_pos_map(ctx.pos_map);
            }
            if (ctx.ecs_world) {
                auto tree_view =
                    ctx.ecs_world->registry.view<ecs::Position, ecs::ObjectSprite, ecs::Active>();
                int checked = 0;
                constexpr int kSpawnSamplesPerTick = 64;
                for (auto entity : tree_view) {
                    const auto& pos = tree_view.get<ecs::Position>(entity);
                    const auto& sprite = tree_view.get<ecs::ObjectSprite>(entity);
                    if (sprite.type != ObjectType::Tree)
                        continue;
                    if (++checked > kSpawnSamplesPerTick)
                        break;

                    const int drop = random_u32_inclusive(ctx.rng, WORLD_WIDTH);
                    if (drop != 0)
                        continue;

                    const Direction drop_dir =
                        static_cast<Direction>(random_u32_inclusive(ctx.rng, 3));
                    const TilePosition side_tile = ctx.get_neighbor(pos.tile, drop_dir);
                    if (is_valid(side_tile)
                        && (ctx.relief[side_tile] == TerrainType::Grass
                            || ctx.relief[side_tile] == TerrainType::Dirt)
                        && ctx.pos_map[side_tile] == 0) {
                        ecs::spawn_tree(*ctx.ecs_world, side_tile);
                    }
                }
            }
        }
    }
}

void PlayState::render(GameContext& ctx, TextureManager& textures) {
    ui_clear_black(ctx.renderer);

    const int scaled_size = scaled_tile_size(TILE_SIZE, ctx.zoom);
    const int pixel_offset_x = static_cast<int>(ctx.map_offset_x * ctx.zoom);
    const int pixel_offset_y = static_cast<int>(ctx.map_offset_y * ctx.zoom);
    const TileView view = make_tile_view(ctx, scaled_size, pixel_offset_x, pixel_offset_y);
    auto neighbor = [&ctx](TilePosition pos, Direction dir) { return ctx.get_neighbor(pos, dir); };

    if (visible_epoch_counter_ == 0) {
        visible_epoch_.fill(0);
        visible_points_.fill(SDL_Point{0, 0});
    }
    if (++visible_epoch_counter_ == std::numeric_limits<int>::max()) {
        visible_epoch_.fill(0);
        visible_epoch_counter_ = 1;
    }
    const int visible_epoch = visible_epoch_counter_;

    const LODConfig lod_cfg = calculate_lod_config(scaled_size);

    if (lod_cfg.use_grouped_render && lod_cfg.group_size > 1) {
        for_each_grouped_tile(
            ctx.pos_cam,
            lod_cfg.group_size,
            view,
            neighbor,
            [&](TilePosition group_start, const SDL_Rect& draw_tile, int group_size) {
                if (draw_tile.x + scaled_size > 0 && draw_tile.x < ctx.window_width
                    && draw_tile.y + scaled_size > 0 && draw_tile.y < ctx.window_height) {
                    const TerrainType dominant =
                        get_dominant_terrain(group_start, group_size, ctx.relief, neighbor);

                    SDL_RenderCopy(ctx.renderer, textures.tile(dominant), nullptr, &draw_tile);

                    if (group_has_flora(group_start, group_size, ctx.flora, 100, neighbor)) {
                        SDL_RenderCopy(ctx.renderer,
                                       textures.sprite(static_cast<size_t>(ObjectType::Tree)),
                                       nullptr,
                                       &draw_tile);
                    }

                    TilePosition center_pos = group_start;
                    for (int i = 0; i < group_size / 2; ++i) {
                        center_pos = neighbor(center_pos, Direction::Right);
                    }
                    for (int i = 0; i < group_size / 2; ++i) {
                        center_pos = neighbor(center_pos, Direction::Down);
                    }
                    if (is_valid(center_pos)) {
                        visible_epoch_[center_pos] = visible_epoch;
                        visible_points_[center_pos] = SDL_Point{draw_tile.x, draw_tile.y};
                    }
                }
            });
    } else if (scaled_size < 8) {
        const int map_render_size = WORLD_WIDTH * scaled_size;
        const int start_x = ctx.window_width / 2 + pixel_offset_x - (ctx.pos_cam.x * scaled_size);
        const int start_y = ctx.window_height / 2 + pixel_offset_y - (ctx.pos_cam.y * scaled_size);

        for (int ox = -1; ox <= 1; ++ox) {
            for (int oy = -1; oy <= 1; ++oy) {
                SDL_Rect world_rect = {start_x + ox * map_render_size,
                                       start_y + oy * map_render_size,
                                       map_render_size,
                                       map_render_size};
                if (world_rect.x + map_render_size > 0 && world_rect.x < ctx.window_width
                    && world_rect.y + map_render_size > 0 && world_rect.y < ctx.window_height) {
                    SDL_RenderCopy(ctx.renderer, ctx.world_image.get(), nullptr, &world_rect);
                }
            }
        }
    } else {
        for_each_visible_tile(
            ctx.pos_cam,
            view,
            neighbor,
            [&](TilePosition tile_pos, const SDL_Rect& draw_tile) {
                if (draw_tile.x + scaled_size > 0 && draw_tile.x < ctx.window_width
                    && draw_tile.y + scaled_size > 0 && draw_tile.y < ctx.window_height) {
                    SDL_RenderCopy(ctx.renderer,
                                   textures.tile(ctx.relief[tile_pos]),
                                   nullptr,
                                   &draw_tile);

                    if (ctx.flora[tile_pos] > 100) {
                        SDL_RenderCopy(ctx.renderer,
                                       textures.sprite((size_t)ObjectType::Tree),
                                       nullptr,
                                       &draw_tile);
                    }

                    visible_epoch_[tile_pos] = visible_epoch;
                    visible_points_[tile_pos] = SDL_Point{draw_tile.x, draw_tile.y};
                }
            });
    }

    render_entities(ctx, textures, scaled_size, visible_epoch);
    render_settlements(ctx, textures, scaled_size, visible_epoch);
    render_player(ctx, textures, scaled_size, visible_epoch);

    render_all_npcs(ctx, textures, scaled_size, visible_epoch);

    hovered_npc_text_.clear();
    if (!ctx.ui_hit_test.contains(ctx.curs_x, ctx.curs_y)) {
        const TilePosition hover_tile = screen_to_world_pos(ctx, ctx.curs_x, ctx.curs_y, view);
        if (is_valid(hover_tile)) {
            if (visible_epoch_[hover_tile] == visible_epoch) {
                const SDL_Point& hover_pt = visible_points_[hover_tile];
                SDL_Rect hover_rect{hover_pt.x, hover_pt.y, scaled_size, scaled_size};
                ui_fill_rect(ctx.renderer, hover_rect, ui_color("#FFFFFF28"));
                ui_draw_rect(ctx.renderer, hover_rect, ui_color("#FFFFFF8C"));

                if (ctx.ecs_world) {
                    auto npc_view =
                        ctx.ecs_world->registry.view<ecs::Position, ecs::NPCTag, ecs::Active>(
                            entt::exclude<ecs::Dead>);
                    for (auto entity : npc_view) {
                        const auto& epos = npc_view.get<ecs::Position>(entity);
                        const auto& npc_tag = npc_view.get<ecs::NPCTag>(entity);
                        if (epos.tile == hover_tile) {
                            const char* type_text = npc_type_name(npc_tag.type);
                            hovered_npc_text_ = std::string("NPC: ") + type_text;
                            break;
                        }
                    }
                }
            }
        }
    }

    SDL_Color ambient = get_ambient_color(ctx.ticks());
    if (ambient.a > 0) {
        SDL_Rect screen_rect = {0, 0, ctx.window_width, ctx.window_height};
        ui_fill_rect(ctx.renderer, screen_rect, ambient, SDL_BLENDMODE_BLEND);
    }

    hud_.set_hover_npc_text(hovered_npc_text_);
    hud_.render(ctx, ctx.world_manager);

    if (ctx.paused) {
        const std::string text = "PAUSED";
        render_text(ctx, text, ctx.window_width / 2 - 50, 10, 100, 20, {255, 0, 0, 255});
    }

    if (buttons_initialized_) {
        buttons_.render(ctx);
        move_buttons_.render(ctx);
        action_buttons_.render(ctx);
    }

    if (show_trade_ui_ && ctx.world_manager) {
        render_trade_ui(ctx);
    }
}

void PlayState::render_trade_ui(GameContext& ctx) {
    if (!ctx.world_manager)
        return;
    const Player& p = ctx.world_manager->player_ctrl.player();
    if (!p.active)
        return;

    const Settlement* at_settlement = ctx.world_manager->get_settlement_at(p.pos);

    const SDL_Rect panel = trade_panel_rect(ctx);
    ctx.ui_hit_test.add(panel);
    const int panel_x = panel.x;
    const int panel_y = panel.y;
    const int panel_w = panel.w;
    const int panel_h = panel.h;
    ui_draw_panel(ctx.renderer, panel, ui_color("#28283CE6"), ui_color("#64648C"));

    int y = panel_y + 10;

    if (at_settlement) {
        const std::uint64_t day_tick = ctx.ticks() % TICKS_PER_DAY;
        const int game_hour = static_cast<int>(day_tick / 1000);
        const bool is_night = (game_hour >= 22 || game_hour < 6);

        std::string title = "Trade at " + at_settlement->name;
        render_text(ctx, title, panel_x + 10, y, panel_w - 20, 20, {255, 255, 255, 255});
        y += 30;

        if (is_night) {
            render_text(ctx,
                        "[ CLOSED FOR NIGHT ]",
                        panel_x + 10,
                        y + 20,
                        panel_w - 20,
                        20,
                        {255, 50, 50, 255});
            render_text(ctx,
                        "Opens at 06:00",
                        panel_x + 10,
                        y + 50,
                        panel_w - 20,
                        16,
                        {150, 150, 150, 255});
        } else {
            render_text(ctx, "Your Inventory:", panel_x + 10, y, 150, 16, {200, 200, 200, 255});
            y += 20;

            for (std::size_t i = 1; i < RESOURCE_COUNT; ++i) {
                const auto res = static_cast<ResourceType>(i);
                const std::int32_t amount = p.inventory.get(res);
                if (amount > 0) {
                    std::string line =
                        std::string(RESOURCE_DATA[i].name) + ": " + std::to_string(amount);
                    render_text(ctx, line, panel_x + 20, y, 200, 14, {180, 180, 180, 255});
                    y += 16;
                }
            }
        }
    } else {
        render_text(ctx,
                    "Not at a settlement",
                    panel_x + 10,
                    y,
                    panel_w - 20,
                    20,
                    {255, 100, 100, 255});
        y += 30;
        render_text(ctx,
                    "Travel to a city, town,",
                    panel_x + 10,
                    y,
                    panel_w - 20,
                    16,
                    {180, 180, 180, 255});
        y += 20;
        render_text(ctx,
                    "or village to trade.",
                    panel_x + 10,
                    y,
                    panel_w - 20,
                    16,
                    {180, 180, 180, 255});
    }

    render_text(ctx,
                "Tap outside to close",
                panel_x + 10,
                panel_y + panel_h - 25,
                panel_w - 20,
                14,
                {150, 150, 150, 255});
}

// Application entry — SDL2 + OpenGL 3.2 Core + ImGui. Owns the screen
// state machine (Title / Playing / Paused / Dead), boots the macroworld
// on demand, drives the macro renderer + subworld, and routes input.
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <SDL.h>
#include "gl/gl.h"
#include "core/rng.h"
#include "ecs/world.h"
#include "events/event_bus.h"
#include "events/logic_nodes.h"
#include "events/node_registry.h"
#include "events/quests/quest_engine.h"
#include "macro/state.h"
#include "macro/map_generator.h"
#include "macro/spawners.h"
#include "macro/zones.h"
#include "macro/politik.h"
#include "macro/macro_renderer.h"
#include "macro/biomes.h"
#include "macro/world_tick.h"
#include "macro/npc_ai.h"
#include "macro/npc_spawn.h"
#include "macro/pathfinding.h"
#include "macro/save.h"
#include "content/spells/registry.h"
#include "content/plot/encounters.h"
#include "sub/engine.h"
#include "ui/overlays.h"
#include "ui/screens.h"
#include "ui/macro_overlay.h"

#include "imgui.h"
#include "backends/imgui_impl_sdl2.h"
#include "backends/imgui_impl_opengl3.h"

namespace {

constexpr const char* kSavePath = "save.bin";

struct App {
    SDL_Window*   window  = nullptr;
    SDL_GLContext gl      = nullptr;
    int           width   = 1280;
    int           height  = 800;
    bool          running = true;

    sm::ui::AppState state = sm::ui::AppState::Title;
    bool worldLoaded = false;

    sm::GameState        gs;
    sm::TerrainData      terrain{};
    sm::FeatureLayer     features;
    sm::ZoneLayer        zones;
    sm::MacroRenderer    macro;
    sm::ecs::World       ecs;
    sm::EventBus         bus;
    sm::LogicNodeEngine  logic;
    sm::QuestEngine      quests;
    std::vector<sm::Quest> activeQuests;
    sm::sub::SubworldEngine subworld;

    // Macro tree list (used for biome features and woodcutter NPC AI).
    std::vector<sm::TreePoint> trees;
    sm::TreeGrid               treeGrid;

    // Camera (macro view): follows player, offset-able via middle-/right-drag.
    float camX = 0, camY = 0;             // current
    float camTargetX = 0, camTargetY = 0; // smoothed target
    float camPanX = 0, camPanY = 0;       // user pan offset (cells)
    float zoom = 32.0f;
    bool  panning = false;
    int   panLastMouseX = 0, panLastMouseY = 0;

    bool  showDebug = false;
    sm::ui::Toggles ui;
    sm::ui::MacroCursor cursor;
    sm::PathCostData pathCost;
    sm::ui::CustomGameParams customParams; // remembered across visits to the menu
    GLuint   customPreviewTex   = 0;       // biome-coloured world preview
    int      customPreviewSide  = 0;       // 0 = no preview built yet
    bool     customWorldReady   = false;   // true after a regen succeeds
};

// ── Boot ──────────────────────────────────────────────────────

bool boot_window(App& app) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return false;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
                        SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    app.window = SDL_CreateWindow("Samosbor / Timaert",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        app.width, app.height,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!app.window) { std::fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError()); return false; }
    app.gl = SDL_GL_CreateContext(app.window);
    if (!app.gl) { std::fprintf(stderr, "SDL_GL_CreateContext: %s\n", SDL_GetError()); return false; }
    SDL_GL_MakeCurrent(app.window, app.gl);
    SDL_GL_SetSwapInterval(1);
    SDL_GL_GetDrawableSize(app.window, &app.width, &app.height);
    return true;
}

void boot_imgui(App& app) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    ImGui::StyleColorsDark();
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding = 4.0f;
    s.FrameRounding  = 3.0f;
    s.GrabRounding   = 3.0f;
    ImGui_ImplSDL2_InitForOpenGL(app.window, app.gl);
    ImGui_ImplOpenGL3_Init("#version 150");
}

void shutdown_imgui() {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
}

// ── World life-cycle ──────────────────────────────────────────

void destroy_world(App& app) {
    if (!app.worldLoaded) return;
    if (app.subworld.active()) app.subworld.leave();
    sm::destroy_terrain(app.terrain);
    app.terrain = {};
    app.gs = sm::GameState{};
    app.activeQuests.clear();
    app.ecs.reg.clear();
    app.worldLoaded = false;
}

void boot_world(App& app, std::uint32_t seed,
                int mapW = 1024, int mapH = 1024,
                const sm::LayerParameters* lpOverride = nullptr,
                int targetTotalCities = 0) {
    destroy_world(app);

    sm::register_builtin_spells();
    sm::register_builtin_nodes(app.bus, app.gs);

    app.gs = sm::default_game_state(seed, mapW, mapH);
    sm::LayerParameters lp = lpOverride ? *lpOverride : sm::LayerParameters{};
    lp.seed = float(app.gs.worldSeed % 100000u);
    app.terrain = sm::generate_terrain(app.gs.mapW, app.gs.mapH, lp);

    app.gs.politik = sm::generate_politik(app.gs.worldSeed, app.gs.mapW, app.gs.mapH,
                                          &app.terrain, std::uint8_t(lp.seaLevel * 255.0f),
                                          targetTotalCities);
    sm::snap_cities_to_land(app.gs.politik, app.terrain, std::uint8_t(lp.seaLevel * 255.0f));
    sm::finalize_politik(app.gs.politik, app.terrain, std::uint8_t(lp.seaLevel * 255.0f));
    sm::populate_landmarks_from_politik(app.gs, app.terrain,
                                        std::uint8_t(lp.seaLevel * 255.0f));

    app.trees = sm::spawn_trees(app.terrain, app.gs.worldSeed, 0.18f);
    auto roads = sm::trace_roads(app.terrain, app.gs.politik);
    const auto& citiesFlat = app.gs.politik.cities;
    std::vector<int> vx, vy;
    for (const auto& v : app.gs.villages) { vx.push_back(v.x); vy.push_back(v.y); }
    auto dirts = sm::trace_dirt_roads(app.gs.mapW, app.gs.mapH, roads, vx, vy,
                                       app.terrain.rgba.data());
    app.features = sm::build_feature_layer(app.terrain, app.trees, 0.78f, roads, &dirts);
    sm::build_tree_grid(app.treeGrid, app.trees, app.gs.mapW, app.gs.mapH);

    std::vector<sm::ZoneSeed> zsCities, zsVills;
    for (auto& c : citiesFlat) zsCities.push_back({c.x, c.y});
    for (auto& v : app.gs.villages) zsVills.push_back({v.x, v.y});
    app.zones = sm::generate_zones(app.gs.mapW, app.gs.mapH, app.gs.worldSeed,
                                   zsCities, zsVills, app.features);

    app.macro.init();
    app.macro.upload_features(app.features);
    app.macro.upload_zones(app.zones);
    app.macro.rebuild_landmarks(app.gs);

    app.pathCost = sm::build_cost_grid(app.terrain, &app.features, 0.40f);
    app.cursor = sm::ui::MacroCursor{};

    sm::spawn_macro_npcs(app.gs, app.ecs, app.terrain, app.gs.worldSeed);

    if (!citiesFlat.empty()) {
        app.gs.player.x = float(citiesFlat[0].x);
        app.gs.player.y = float(citiesFlat[0].y);
    } else {
        app.gs.player.x = float(app.gs.mapW / 2);
        app.gs.player.y = float(app.gs.mapH / 2);
    }
    // Anchor camera at the player's CELL CENTRE (cell N spans [N..N+1]
    // in world units, so its centre sits at N+0.5). The per-sprite
    // +0.5 in macro_overlay.cpp lines up with this so the player +
    // every NPC render at their cell centre, never at the cell
    // crossing.
    app.camX = app.camTargetX = app.gs.player.x + 0.5f;
    app.camY = app.camTargetY = app.gs.player.y + 0.5f;
    app.camPanX = app.camPanY = 0;

    app.subworld.init();
    app.worldLoaded = true;
    app.state = sm::ui::AppState::Playing;
}

bool boot_world_from_save(App& app) {
    sm::GameState fresh;
    if (!sm::load_game(fresh, kSavePath)) return false;
    boot_world(app, fresh.worldSeed);
    app.gs.worldTime              = fresh.worldTime;
    app.gs.saveName               = fresh.saveName;
    app.gs.player.name            = fresh.player.name;
    app.gs.player.ageDays         = fresh.player.ageDays;
    app.gs.player.x               = fresh.player.x;
    app.gs.player.y               = fresh.player.y;
    app.gs.player.gold            = fresh.player.gold;
    app.gs.player.attributes      = fresh.player.attributes;
    app.gs.player.combatStats     = fresh.player.combatStats;
    app.gs.player.levelData       = fresh.player.levelData;
    app.gs.player.spellBookSpellIds = std::move(fresh.player.spellBookSpellIds);
    app.gs.player.completedQuestIds = std::move(fresh.player.completedQuestIds);
    app.gs.player.reputation        = std::move(fresh.player.reputation);
    app.camX = app.camTargetX = app.gs.player.x + 0.5f;
    app.camY = app.camTargetY = app.gs.player.y + 0.5f;
    return true;
}

// ── Input ─────────────────────────────────────────────────────

void handle_event_playing(App& app, const SDL_Event& e) {
    switch (e.type) {
        case SDL_KEYDOWN:
            switch (e.key.keysym.sym) {
                case SDLK_ESCAPE: app.state = sm::ui::AppState::Paused; break;
                case SDLK_F3:     app.showDebug = !app.showDebug; break;
                case SDLK_k:      app.ui.diplomacy  = !app.ui.diplomacy; break;
                case SDLK_t:      app.ui.settlement = !app.ui.settlement; break;
                case SDLK_q:      app.ui.quest      = !app.ui.quest; break;
                case SDLK_c:      app.ui.codex      = !app.ui.codex; break;
                case SDLK_m:      app.ui.map        = !app.ui.map; break;
                case SDLK_f:      app.subworld.toggle_3d(); break;
                case SDLK_F5:     sm::save_game(app.gs, kSavePath); break;
                case SDLK_F9:     boot_world_from_save(app); break;
                case SDLK_RETURN:
                    if (!app.subworld.active())
                        app.subworld.enter(app.gs, app.terrain, app.features, app.ecs, app.bus);
                    else
                        app.subworld.leave();
                    break;
                default: break;
            }
            break;
        case SDL_MOUSEWHEEL:
            if (e.wheel.y > 0) app.zoom *= 1.15f;
            if (e.wheel.y < 0) app.zoom /= 1.15f;
            if (app.zoom < 4.0f)  app.zoom = 4.0f;
            if (app.zoom > 96.0f) app.zoom = 96.0f;
            break;
        case SDL_MOUSEBUTTONDOWN:
            if (e.button.button == SDL_BUTTON_MIDDLE ||
                e.button.button == SDL_BUTTON_RIGHT) {
                app.panning = true;
                app.panLastMouseX = e.button.x;
                app.panLastMouseY = e.button.y;
            }
            break;
        case SDL_MOUSEBUTTONUP:
            if (e.button.button == SDL_BUTTON_MIDDLE ||
                e.button.button == SDL_BUTTON_RIGHT) {
                app.panning = false;
            }
            break;
        case SDL_MOUSEMOTION:
            // 3D subworld: any mouse motion drives camera look (yaw + pitch).
            // Captured via SDL relative-mouse mode; gated on subworld 3D
            // being the active view.
            if (app.subworld.active() && app.subworld.is_3d()) {
                const float kSensYaw   = 0.0035f;
                const float kSensPitch = 0.0030f;
                app.subworld.rotate_camera(float(e.motion.xrel) * kSensYaw,
                                           -float(e.motion.yrel) * kSensPitch);
            } else if (app.panning && !app.subworld.active()) {
                int dx = e.motion.x - app.panLastMouseX;
                int dy = e.motion.y - app.panLastMouseY;
                app.panLastMouseX = e.motion.x;
                app.panLastMouseY = e.motion.y;
                const float pxPerCell = 16.0f * app.zoom;
                // Joystick-style: camera follows the cursor direction.
                // World +Y is screen-UP, so cursor-DOWN → camera-DOWN means
                // world Y decreases.
                app.camPanX += float(dx) / pxPerCell;
                app.camPanY -= float(dy) / pxPerCell;
            }
            break;
        default: break;
    }
}

void handle_event(App& app, const SDL_Event& e) {
    ImGui_ImplSDL2_ProcessEvent(&e);
    switch (e.type) {
        case SDL_QUIT: app.running = false; return;
        case SDL_WINDOWEVENT:
            if (e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED ||
                e.window.event == SDL_WINDOWEVENT_RESIZED) {
                SDL_GL_GetDrawableSize(app.window, &app.width, &app.height);
            }
            return;
        default: break;
    }
    const ImGuiIO& io = ImGui::GetIO();
    if (io.WantCaptureKeyboard && e.type == SDL_KEYDOWN) return;
    if (io.WantCaptureMouse && (e.type == SDL_MOUSEBUTTONDOWN ||
                                e.type == SDL_MOUSEBUTTONUP ||
                                e.type == SDL_MOUSEWHEEL ||
                                e.type == SDL_MOUSEMOTION)) return;
    if (app.state == sm::ui::AppState::Playing) handle_event_playing(app, e);
    else if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE &&
             app.state == sm::ui::AppState::Paused) {
        app.state = sm::ui::AppState::Playing;
    }
}

void poll_movement(App& app, float dt) {
    if (app.state != sm::ui::AppState::Playing) return;
    if (ImGui::GetIO().WantCaptureKeyboard) return;
    const Uint8* keys = SDL_GetKeyboardState(nullptr);

    // SDL relative mouse mode (mouselook) is enabled only while the 3D
    // subworld view is active; toggle here so the cursor returns to
    // normal in 2D / macro / paused states.
    static bool s_relMouse = false;
    bool wantRel = app.subworld.active() && app.subworld.is_3d();
    if (wantRel != s_relMouse) {
        SDL_SetRelativeMouseMode(wantRel ? SDL_TRUE : SDL_FALSE);
        s_relMouse = wantRel;
    }

    if (app.subworld.active()) {
        // Subworld: ARROW keys move the player; mouse rotates the camera.
        // WASD also accepted as alias for keyboards without arrows.
        // Y axis: UP = forward (+y in world tile space).
        float dx = 0, dy = 0;
        if (keys[SDL_SCANCODE_UP]    || keys[SDL_SCANCODE_W]) dy += 1;
        if (keys[SDL_SCANCODE_DOWN]  || keys[SDL_SCANCODE_S]) dy -= 1;
        if (keys[SDL_SCANCODE_LEFT]  || keys[SDL_SCANCODE_A]) dx -= 1;
        if (keys[SDL_SCANCODE_RIGHT] || keys[SDL_SCANCODE_D]) dx += 1;
        app.subworld.move_player(dx * 96.0f * dt, dy * 96.0f * dt);
        return;
    }

    // Macroworld: keyboard does NOT move the player. The player only
    // moves by clicking a destination cell (handled in the overlay
    // cursor → find_path → step_macro_walk pipeline). Keyboard pans the
    // free camera around the world map (Mount & Blade-style overworld
    // camera). Y axis matches the shader convention: world +Y = screen UP.
    float panDx = 0, panDy = 0;
    if (keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_UP])    panDy += 1;
    if (keys[SDL_SCANCODE_S] || keys[SDL_SCANCODE_DOWN])  panDy -= 1;
    if (keys[SDL_SCANCODE_A] || keys[SDL_SCANCODE_LEFT])  panDx -= 1;
    if (keys[SDL_SCANCODE_D] || keys[SDL_SCANCODE_RIGHT]) panDx += 1;

    if (panDx != 0 || panDy != 0) {
        const float kCamPanCellsPerSec = 30.0f;
        app.camPanX += panDx * kCamPanCellsPerSec * dt;
        app.camPanY += panDy * kCamPanCellsPerSec * dt;
        // Keep panning flag set so the soft pan-decay in update_camera
        // doesn't fight the keyboard. Released keys → decay slowly
        // re-anchors the camera to the player (Mount & Blade overworld).
        app.panning = true;
    }

    // Auto-walk along a clicked path (independent of camera input).
    if (!app.cursor.path.empty()) {
        const float kAutoCellsPerSec = 6.0f;
        const float prevX = app.gs.player.x;
        const float prevY = app.gs.player.y;
        sm::ui::step_macro_walk(app.gs, app.cursor, dt, kAutoCellsPerSec);

        // Random encounter trigger — accumulate distance walked along
        // the auto-path, roll on threshold (mirrors TS enc_random).
        float ddx = app.gs.player.x - prevX;
        float ddy = app.gs.player.y - prevY;
        // Account for torus wrap so a seam crossing isn't a giant jump.
        if (ddx >  app.gs.mapW * 0.5f) ddx -= float(app.gs.mapW);
        if (ddx < -app.gs.mapW * 0.5f) ddx += float(app.gs.mapW);
        if (ddy >  app.gs.mapH * 0.5f) ddy -= float(app.gs.mapH);
        if (ddy < -app.gs.mapH * 0.5f) ddy += float(app.gs.mapH);
        const float dist = std::sqrt(ddx * ddx + ddy * ddy);
        static float s_distAcc = 0.0f;
        static std::uint32_t s_rng = 0xC0FFEEu;
        s_distAcc += dist;
        const float kEncounterStep = 60.0f;
        if (s_distAcc > kEncounterStep
            && app.gs.subState.kind == sm::GameSubStateKind::Exploring) {
            s_distAcc = 0.0f;
            s_rng = s_rng * 1664525u + 1013904223u;
            if ((s_rng >> 24) < 64) {
                const auto& tbl = sm::content::encounters();
                if (!tbl.empty()) {
                    s_rng = s_rng * 1664525u + 1013904223u;
                    int idx = int((s_rng >> 8) % tbl.size());
                    app.gs.subState.kind = sm::GameSubStateKind::Event;
                    app.gs.subState.pendingEncounterIdx = idx;
                }
            }
        }
    }
}

void update_camera(App& app, float dt) {
    if (app.subworld.active()) return;
    if (!app.panning) {
        const float decay = std::exp(-dt * 1.5f);
        app.camPanX *= decay;
        app.camPanY *= decay;
    }
    app.camTargetX = app.gs.player.x + 0.5f + app.camPanX;
    app.camTargetY = app.gs.player.y + 0.5f + app.camPanY;
    const float a = 1.0f - std::exp(-dt * 8.0f);
    app.camX += (app.camTargetX - app.camX) * a;
    app.camY += (app.camTargetY - app.camY) * a;
}

// ── HUD / Debug ───────────────────────────────────────────────

void draw_debug_ui(App& app) {
    if (!app.showDebug) return;
    ImGui::SetNextWindowPos(ImVec2(float(app.width) - 8, 8),
                            ImGuiCond_Always, ImVec2(1, 0));
    ImGui::SetNextWindowBgAlpha(0.55f);
    ImGui::Begin("##debug", nullptr,
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoTitleBar);
    ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
    ImGui::Text("Zoom %.2f  Cam %.1f,%.1f", app.zoom, app.camX, app.camY);
    ImGui::Text("Kingdoms %zu  Cities %zu  Villages %zu",
                app.gs.politik.kingdoms.size(),
                app.gs.politik.cities.size(),
                app.gs.villages.size());
    ImGui::Text("Subworld: %s", app.subworld.active() ? "ACTIVE" : "off");
    ImGui::End();
}

// ── Frame ─────────────────────────────────────────────────────

// Build a small biome-coloured RGBA preview of the currently-loaded
// terrain + politik and upload it to `app.customPreviewTex`. Cheap CPU
// loop — same biome rule as the macro shader (`Water` if h<sea, else
// 3×3 climate matrix). Cities drawn as 3×3 yellow stamps.
void build_world_preview(App& app, int side = 384) {
    if (!app.worldLoaded || app.terrain.rgba.empty()) return;
    const auto& td = app.terrain;
    const std::uint8_t sea8 = std::uint8_t(app.customParams.layer.seaLevel * 255.0f);
    std::vector<std::uint8_t> img(std::size_t(side) * side * 4);
    for (int y = 0; y < side; ++y) {
        const int sy = y * td.height / side;
        for (int x = 0; x < side; ++x) {
            const int sx = x * td.width / side;
            const std::size_t s = std::size_t(sy * td.width + sx) * 4;
            const std::uint8_t h = td.rgba[s + 0];
            const std::uint8_t m = td.rgba[s + 1];
            const std::uint8_t t = td.rgba[s + 2];
            sm::Biome b = (h < sea8)
                ? sm::Water
                : sm::biome_from_climate(float(t) / 255.0f, float(m) / 255.0f);
            const auto& bd = sm::kBiomes[b];
            // Soft height shading so land has some relief.
            const float lift = (h < sea8) ? 0.0f
                                          : 0.85f + 0.30f * (float(h - sea8) / 255.0f);
            const std::size_t o = std::size_t(y * side + x) * 4;
            auto clamp8 = [](float v) -> std::uint8_t {
                if (v <   0.0f) v =   0.0f;
                if (v > 255.0f) v = 255.0f;
                return std::uint8_t(v);
            };
            img[o + 0] = clamp8(bd.r * 255.0f * lift);
            img[o + 1] = clamp8(bd.g * 255.0f * lift);
            img[o + 2] = clamp8(bd.b * 255.0f * lift);
            img[o + 3] = 255;
        }
    }
    // Stamp cities (yellow) and capitals (brighter yellow).
    auto stamp = [&](int cx, int cy, int rad, std::uint8_t cr,
                     std::uint8_t cg, std::uint8_t cb) {
        for (int dy = -rad; dy <= rad; ++dy)
            for (int dx = -rad; dx <= rad; ++dx) {
                int x = cx + dx, y = cy + dy;
                if (x < 0 || y < 0 || x >= side || y >= side) continue;
                const std::size_t o = std::size_t(y * side + x) * 4;
                img[o + 0] = cr; img[o + 1] = cg; img[o + 2] = cb; img[o + 3] = 255;
            }
    };
    for (int i = 0; i < int(app.gs.politik.cities.size()); ++i) {
        const auto& c = app.gs.politik.cities[std::size_t(i)];
        const int px = c.x * side / td.width;
        const int py = c.y * side / td.height;
        stamp(px, py, 1, 240, 200, 60);
    }
    for (const auto& kg : app.gs.politik.kingdoms) {
        if (kg.capitalCityIdx < 0
            || kg.capitalCityIdx >= int(app.gs.politik.cities.size())) continue;
        const auto& c = app.gs.politik.cities[std::size_t(kg.capitalCityIdx)];
        const int px = c.x * side / td.width;
        const int py = c.y * side / td.height;
        stamp(px, py, 2, 255, 240, 120);
    }

    if (!app.customPreviewTex) glGenTextures(1, &app.customPreviewTex);
    glBindTexture(GL_TEXTURE_2D, app.customPreviewTex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, side, side, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, img.data());
    app.customPreviewSide = side;
}

void apply_shell_actions(App& app, const sm::ui::ShellResult& r) {
    if (r.startNewGame)        boot_world(app, std::uint32_t(SDL_GetTicks()) ^ 0xC0FFEEu);
    if (r.openCustomNewGame) {
        app.state = sm::ui::AppState::CustomNewGame;
        app.customWorldReady = false;   // force a fresh regen
    }
    if (r.cancelCustomNewGame) {
        app.state = sm::ui::AppState::Title;
        app.customWorldReady = false;
        if (app.worldLoaded) destroy_world(app);  // drop preview-built world
    }
    if (r.regenerateCustom) {
        const int side = 1 << app.customParams.mapSizeLog2;
        const std::uint32_t seed = app.customParams.seed != 0
            ? app.customParams.seed
            : (std::uint32_t(SDL_GetTicks()) ^ 0xC0FFEEu);
        boot_world(app, seed, side, side, &app.customParams.layer,
                   app.customParams.cityCountTarget);
        app.state = sm::ui::AppState::CustomNewGame;  // stay in menu
        build_world_preview(app);
        app.customWorldReady = true;
    }
    if (r.startCustomNewGame) {
        if (!app.customWorldReady) {
            const int side = 1 << app.customParams.mapSizeLog2;
            const std::uint32_t seed = app.customParams.seed != 0
                ? app.customParams.seed
                : (std::uint32_t(SDL_GetTicks()) ^ 0xC0FFEEu);
            boot_world(app, seed, side, side, &app.customParams.layer,
                       app.customParams.cityCountTarget);
        }
        app.state = sm::ui::AppState::Playing;
        app.customWorldReady = false;
    }
    if (r.loadGame) {
        if (!boot_world_from_save(app))
            std::fprintf(stderr, "load_game: no save at %s\n", kSavePath);
    }
    if (r.saveGame)      sm::save_game(app.gs, kSavePath);
    if (r.resume)        app.state = sm::ui::AppState::Playing;
    if (r.returnToTitle) { destroy_world(app); app.state = sm::ui::AppState::Title; }
    if (r.quit)          app.running = false;
}

void frame(App& app, float dt) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) handle_event(app, e);

    if (app.state == sm::ui::AppState::Playing && app.worldLoaded) {
        if (app.subworld.active()) {
            // While in the subworld the macro simulation is FROZEN — no
            // settlement / village / economy daily ticks, no macro NPC AI.
            // The player's macro cell is only re-synced on `leave()`. This
            // keeps a long subworld session from silently fast-forwarding
            // the macro economy or moving NPCs out from under the player.
            // The world CLOCK still advances (`tick_world_time_only`) so
            // the sun arc / sky tint keep moving while exploring.
            //
            // poll_movement() must still run — it owns the subworld
            // arrow/WASD movement branch AND the SDL relative-mouse-mode
            // toggle that captures the cursor for 3D camera look. Skipping
            // it broke both player movement and mouse capture in subworld.
            poll_movement(app, dt);
            sm::tick_world_time_only(app.gs, dt);
            app.subworld.tick(dt);
            app.bus.flush(app.gs.worldTime.day, app.gs.worldTime.hour);
            app.quests.tick(app.activeQuests, app.bus, app.gs.player, app.gs.worldTime);
        } else {
            poll_movement(app, dt);
            update_camera(app, dt);
            sm::tick_world(app.gs, dt);
            sm::tick_macro_npc_ai(app.gs, app.ecs, &app.treeGrid, dt);
            app.bus.flush(app.gs.worldTime.day, app.gs.worldTime.hour);
            app.quests.tick(app.activeQuests, app.bus, app.gs.player, app.gs.worldTime);
        }
        if (app.gs.player.combatStats.currentHp <= 0)
            app.state = sm::ui::AppState::Dead;
    }

    glViewport(0, 0, app.width, app.height);
    glClearColor(0.02f, 0.02f, 0.04f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    if (app.worldLoaded) {
        if (app.subworld.active()) app.subworld.render(app.width, app.height);
        else app.macro.draw(app.terrain, app.camX, app.camY, app.zoom,
                            app.width, app.height, app.gs.worldTime);
    }

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();
if (app.worldLoaded && !app.subworld.active()
        && app.state == sm::ui::AppState::Playing) {
        // The macro shader runs in drawable pixels (`app.width/height`,
        // `app.zoom` = drawable-px/cell). ImGui works in logical points.
        // Feed the overlay logical sizes + a logical zoom so the hover
        // rect, markers and walk path stay 1:1 with the basemap on
        // HiDPI displays (no parallax, no doubled cell size).
        int logicalW = app.width, logicalH = app.height;
        SDL_GetWindowSize(app.window, &logicalW, &logicalH);
        const float dpr = (logicalW > 0)
                            ? float(app.width) / float(logicalW)
                            : 1.0f;
        const float zoomLogical = app.zoom / dpr;
        sm::ui::draw_macro_overlay(app.gs, app.ecs,
                                   app.terrain, app.features,
                                   app.cursor,
                                   app.camX, app.camY, zoomLogical,
                                   logicalW, logicalH,
                                   app.gs.mapW, app.gs.mapH);
        // Resolve a click → pathfind here so the overlay stays purely visual.
        if (app.cursor.requestPath) {
            app.cursor.requestPath = false;
            int sx = int(std::floor(app.gs.player.x));
            int sy = int(std::floor(app.gs.player.y));
            sm::PathResult r = sm::find_path(app.pathCost, sx, sy,
                                             app.cursor.requestX,
                                             app.cursor.requestY);
            if (r.found && r.path.size() > 1) {
                app.cursor.path    = std::move(r.path);
                app.cursor.pathIdx = 1; // skip current cell
            } else {
                app.cursor.path.clear();
                app.cursor.pathIdx = 0;
            }
        }
    }

    
    sm::ui::ShellResult shell{};
    switch (app.state) {
        case sm::ui::AppState::Title:
            shell = sm::ui::draw_title_menu(app.width, app.height);
            break;
        case sm::ui::AppState::CustomNewGame:
            shell = sm::ui::draw_custom_new_game(app.customParams,
                                                 app.customPreviewTex,
                                                 app.customPreviewSide,
                                                 app.customPreviewSide,
                                                 app.customWorldReady,
                                                 app.width, app.height);
            break;
        case sm::ui::AppState::Playing:
            sm::ui::draw_player_hud(app.gs);
            {
                auto tb = sm::ui::draw_bottom_toolbar(app.gs, app.subworld.active());
                if (tb.pause)         app.state          = sm::ui::AppState::Paused;
                if (tb.diplomacy)     app.ui.diplomacy   = !app.ui.diplomacy;
                if (tb.build)         app.ui.settlement  = !app.ui.settlement;
                if (tb.quests)        app.ui.quest       = !app.ui.quest;
                if (tb.codex)         app.ui.codex       = !app.ui.codex;
                if (tb.map)           app.ui.map         = !app.ui.map;
                if (tb.zoomIn)  { app.zoom *= 1.15f; if (app.zoom > 96.0f) app.zoom = 96.0f; }
                if (tb.zoomOut) { app.zoom /= 1.15f; if (app.zoom <  4.0f) app.zoom =  4.0f; }
                if (tb.toggleSubworld) {
                    if (!app.subworld.active())
                        app.subworld.enter(app.gs, app.terrain, app.features, app.ecs, app.bus, &app.zones);
                    else
                        app.subworld.leave();
                }
            }
            sm::ui::draw_hint_bar(app.state, app.subworld.active(), app.width, app.height);
            draw_debug_ui(app);
            sm::ui::draw_diplomacy(app.gs, &app.ui.diplomacy);
            sm::ui::draw_settlement(app.gs, app.ui.settlementId, &app.ui.settlement);
            sm::ui::draw_quest_log(app.gs, app.activeQuests, &app.ui.quest);
            sm::ui::draw_codex(app.gs, &app.ui.codex);
            if (app.subworld.active()) {
                // ImGui foreground draw list works in logical points; the
                // minimap must be anchored to window size, not the HiDPI
                // drawable size (otherwise the circle goes off-screen on
                // Retina displays and looks like it's missing).
                int logicalW = app.width, logicalH = app.height;
                SDL_GetWindowSize(app.window, &logicalW, &logicalH);
                sm::ui::draw_subworld_minimap_hud(app.subworld.mgr(),
                    app.subworld.player_x(), app.subworld.player_y(),
                    app.subworld.cam_yaw(), logicalW, logicalH);
                sm::ui::draw_subworld_map_overlay(app.subworld.mgr(),
                    app.subworld.player_x(), app.subworld.player_y(),
                    app.subworld.cam_yaw(),
                    &app.ui.map);
            } else {
                sm::ui::draw_map_overlay(app.gs, app.terrain, &app.ui.map);
            }
            sm::ui::draw_encounter_modal(app.gs, app.bus);
            // Right-edge nearby-NPC stack (mirrors NpcProximityPanel.svelte).
            // Macro view only, suppressed inside subworld.
            if (!app.subworld.active()) {
                int logicalW = app.width, logicalH = app.height;
                SDL_GetWindowSize(app.window, &logicalW, &logicalH);
                sm::ui::draw_npc_proximity_panel(app.gs, app.ecs,
                                                 logicalW, logicalH);
            }
            break;
        case sm::ui::AppState::Paused:
            sm::ui::draw_player_hud(app.gs);
            shell = sm::ui::draw_pause_menu(app.width, app.height);
            break;
        case sm::ui::AppState::Dead:
            shell = sm::ui::draw_death_screen(app.gs, app.width, app.height);
            break;
    }
    apply_shell_actions(app, shell);

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    SDL_GL_SwapWindow(app.window);
}

} // namespace

int main(int /*argc*/, char* /*argv*/[]) {
    App app;
    if (!boot_window(app)) return 1;
    boot_imgui(app);

    Uint64 prev = SDL_GetPerformanceCounter();
    const double freq = double(SDL_GetPerformanceFrequency());
    while (app.running) {
        Uint64 now = SDL_GetPerformanceCounter();
        float dt = float(double(now - prev) / freq);
        if (dt > 0.1f) dt = 0.1f;
        prev = now;
        frame(app, dt);
    }

    destroy_world(app);
    app.subworld.destroy();
    app.macro.destroy();
    shutdown_imgui();
    SDL_GL_DeleteContext(app.gl);
    SDL_DestroyWindow(app.window);
    SDL_Quit();
    return 0;
}

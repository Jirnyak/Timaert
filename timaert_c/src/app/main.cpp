// Application entry — SDL2 + OpenGL 3.2 Core + ImGui. Owns the screen
// state machine (Title / Playing / Paused / Dead), boots the macroworld
// on demand, drives the macro renderer + subworld, and routes input.
#include <cassert>
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <span>
#include <string>
#include <string_view>
#include <SDL.h>
#include "gl/gl.h"
#include "core/rng.h"
#include "core/torus.h"
#include "ecs/world.h"
#include "events/event_bus.h"
#include "events/effect_applicator.h"
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
#include "content/spells/spell_types.h"
#include "content/plot/encounters.h"
#include "content/quests/procedural.h"
#include "sub/engine.h"
#include "sub/map_factory.h"
#include "ui/overlays.h"
#include "ui/screens.h"
#include "ui/macro_overlay.h"

#include "imgui.h"
#include "backends/imgui_impl_sdl2.h"
#include "backends/imgui_impl_opengl3.h"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {

constexpr const char* kSaveFileName = "save.bin";
constexpr const char* kSaveOrgName = "Timaert";
constexpr const char* kSaveAppName = "timaert_c";
constexpr int kSubworldDailyTicksPerFrame = 1;
constexpr int kSubworldMacroNpcTicksPerFrame = 64;
constexpr const char* kSmokeScriptEnv = "TIMAERT_SMOKE_SCRIPT";
constexpr const char* kSmokeSeedEnv = "TIMAERT_SMOKE_SEED";
constexpr int kSubworldSmokeFrames = 1000;
constexpr float kSubworldSmokeDt = 0.1f;

#if defined(_WIN32)
LONG WINAPI crash_filter(EXCEPTION_POINTERS* info) {
    const auto code = info && info->ExceptionRecord
        ? info->ExceptionRecord->ExceptionCode
        : 0ul;
    const auto addr = info && info->ExceptionRecord
        ? info->ExceptionRecord->ExceptionAddress
        : nullptr;
    std::fprintf(stderr, "[crash] unhandled exception code=0x%08lx addr=%p\n",
                 code, addr);
    std::fflush(stderr);
    return EXCEPTION_EXECUTE_HANDLER;
}
#endif

bool boot_trace_enabled() {
#ifndef NDEBUG
    return true;
#else
    static const bool enabled = [] {
        const char* env = std::getenv("TIMAERT_BOOT_TRACE");
        return env && env[0] != '\0' && env[0] != '0';
    }();
    return enabled;
#endif
}

void boot_trace(const char* step) {
    if (!boot_trace_enabled()) return;
    std::fprintf(stderr, "[boot] %s\n", step);
    std::fflush(stderr);
}

void boot_trace_time(const char* step, const sm::WorldTime& time) {
    if (!boot_trace_enabled()) return;
    std::fprintf(stderr, "[time] %s day=%d %02d:%02d\n",
                 step, time.day, time.hour, time.minute);
    std::fflush(stderr);
}

enum class SmokeAction : std::uint8_t {
    NewGame,
    SaveGame,
    OpenLoad,
    LoadGame,
    WaitBootDone,
    SubworldTime,
    WaitVisible,
    OpenQuests,
    TriggerLevelDialog,
    ReturnTitle,
    Quit,
};

struct SmokeScript {
    static constexpr int kMaxActions = 16;
    std::array<SmokeAction, kMaxActions> actions{};
    int count = 0;
    int cursor = 0;
    int bootsObserved = 0;
    int visibleChecks = 0;
    bool enabled = false;
    bool failed = false;
    bool verifyDestroyAfterShell = false;
    bool pendingLoadBoot = false;
};

struct App {
    SDL_Window*   window  = nullptr;
    SDL_GLContext gl      = nullptr;
    int           width   = 1280;
    int           height  = 800;
    bool          running = true;
    std::string   savePath = kSaveFileName;

    sm::ui::AppState state = sm::ui::AppState::Title;
    sm::ui::AppState loadReturnState = sm::ui::AppState::Title;
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
    std::vector<sm::Quest> availableSettlementQuests;
    int                  availableQuestSettlementId = -1;
    int                  availableQuestDay = -1;
    std::size_t          appliedEventCount = 0;
    float                encounterDistAcc = 0.0f;
    sm::Rng              encounterRng{0xC0FFEEu};
    sm::WorldTickRuntime worldTick;
    sm::MacroNpcAiRuntime npcAi;
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
    bool  showDialogOpen = false;
    sm::GameEvent showDialogEvent{};
    std::uint32_t showDialogCapturedTick = std::uint32_t(-1);
    sm::ui::Toggles ui;
    sm::ui::MacroCursor cursor;
    sm::SaveSummary saveSummary;
    sm::PathCostData pathCost;
    sm::ui::CustomGameParams customParams; // remembered across visits to the menu
    GLuint   customPreviewTex   = 0;       // biome-coloured world preview
    int      customPreviewSide  = 0;       // 0 = no preview built yet
    bool     customWorldReady   = false;   // true after a regen succeeds
    SmokeScript smoke;
};

bool smoke_token_equals(std::string_view token, const char* lit) {
    std::size_t n = 0;
    while (lit[n] != '\0') ++n;
    return token.size() == n && token.compare(lit) == 0;
}

bool smoke_action_from_token(std::string_view token, SmokeAction& out) {
    if (smoke_token_equals(token, "new_game")) {
        out = SmokeAction::NewGame;
        return true;
    }
    if (smoke_token_equals(token, "save_game")) {
        out = SmokeAction::SaveGame;
        return true;
    }
    if (smoke_token_equals(token, "open_load")) {
        out = SmokeAction::OpenLoad;
        return true;
    }
    if (smoke_token_equals(token, "load_game")) {
        out = SmokeAction::LoadGame;
        return true;
    }
    if (smoke_token_equals(token, "wait_boot_done")) {
        out = SmokeAction::WaitBootDone;
        return true;
    }
    if (smoke_token_equals(token, "subworld_time")) {
        out = SmokeAction::SubworldTime;
        return true;
    }
    if (smoke_token_equals(token, "wait_visible")) {
        out = SmokeAction::WaitVisible;
        return true;
    }
    if (smoke_token_equals(token, "open_quests")) {
        out = SmokeAction::OpenQuests;
        return true;
    }
    if (smoke_token_equals(token, "trigger_level_dialog")) {
        out = SmokeAction::TriggerLevelDialog;
        return true;
    }
    if (smoke_token_equals(token, "return_title")) {
        out = SmokeAction::ReturnTitle;
        return true;
    }
    if (smoke_token_equals(token, "quit")) {
        out = SmokeAction::Quit;
        return true;
    }
    return false;
}

bool smoke_is_separator(char c) {
    return c == ',' || c == ';' || c == ' ' || c == '\t'
        || c == '\r' || c == '\n';
}

bool parse_u32(const char* text, std::uint32_t& out) {
    if (!text || text[0] == '\0') return false;
    std::uint64_t value = 0;
    for (const char* p = text; *p != '\0'; ++p) {
        if (*p < '0' || *p > '9') return false;
        value = value * 10u + std::uint64_t(*p - '0');
        if (value > 0xffffffffull) return false;
    }
    out = std::uint32_t(value);
    return true;
}

bool parse_smoke_script(const char* script, SmokeScript& out) {
    out = SmokeScript{};
    if (!script || script[0] == '\0') return true;
    out.enabled = true;

    const char* p = script;
    while (*p != '\0') {
        while (*p != '\0' && smoke_is_separator(*p)) ++p;
        const char* begin = p;
        while (*p != '\0' && !smoke_is_separator(*p)) ++p;
        if (begin == p) continue;

        if (out.count >= SmokeScript::kMaxActions) {
            std::fprintf(stderr, "[smoke] too many actions in %s\n", kSmokeScriptEnv);
            out.failed = true;
            return false;
        }
        SmokeAction action = SmokeAction::Quit;
        if (!smoke_action_from_token(std::string_view(begin, std::size_t(p - begin)),
                                     action)) {
            std::fprintf(stderr, "[smoke] unknown action '%.*s'\n",
                         int(p - begin), begin);
            out.failed = true;
            return false;
        }
        out.actions[std::size_t(out.count++)] = action;
    }

    if (out.count == 0) {
        std::fprintf(stderr, "[smoke] empty %s\n", kSmokeScriptEnv);
        out.failed = true;
        return false;
    }
    return true;
}

void smoke_print_counts(const App& app, const char* label) {
    if (!app.smoke.enabled) return;
    std::fprintf(stderr,
                 "[smoke] %s spells=%zu bus=%zu logic=%zu active=%zu world=%d state=%d\n",
                 label,
                 sm::spell_registry().size(),
                 app.bus.subscription_count(),
                 app.logic.node_count(),
                 app.logic.active_count(),
                 app.worldLoaded ? 1 : 0,
                 int(app.state));
    std::fflush(stderr);
}

bool smoke_boot_invariants_hold(const App& app) {
    return app.worldLoaded
        && app.state == sm::ui::AppState::Playing
        && sm::spell_registry().is_consistent()
        && sm::spell_registry().size() > 0
        && app.bus.subscription_count() == 0
        && app.logic.is_consistent()
        && app.logic.node_count() > 0
        && app.logic.active_count() <= app.logic.node_count();
}

bool smoke_destroy_invariants_hold(const App& app) {
    return !app.worldLoaded
        && app.state == sm::ui::AppState::Title
        && sm::spell_registry().is_consistent()
        && app.bus.subscription_count() == 0
        && app.logic.is_consistent()
        && app.logic.node_count() == 0
        && app.logic.active_count() == 0;
}

void smoke_fail(App& app, const char* reason) {
    std::fprintf(stderr, "[smoke] FAIL %s\n", reason);
    std::fflush(stderr);
    app.smoke.failed = true;
    app.running = false;
}

// ── Boot ──────────────────────────────────────────────────────

std::uint32_t choose_new_game_seed(const App& app) {
    std::uint32_t seed = 0;
    if (app.smoke.enabled && parse_u32(std::getenv(kSmokeSeedEnv), seed)) {
        return seed;
    }
    return std::uint32_t(SDL_GetTicks()) ^ 0xC0FFEEu;
}

std::string resolve_legacy_save_path() {
    char* base = SDL_GetBasePath();
    if (!base || base[0] == '\0') {
        if (base) SDL_free(base);
        return kSaveFileName;
    }
    std::string path(base);
    SDL_free(base);
    path += kSaveFileName;
    return path;
}

bool app_file_exists(const std::string& path) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::fclose(f);
    return true;
}

bool copy_file_bytes(const std::string& from, const std::string& to) {
    std::FILE* in = std::fopen(from.c_str(), "rb");
    if (!in) return false;
    std::FILE* out = std::fopen(to.c_str(), "wb");
    if (!out) {
        std::fclose(in);
        return false;
    }

    bool ok = true;
    std::array<std::uint8_t, 64 * 1024> buffer{};
    while (ok) {
        const std::size_t got = std::fread(buffer.data(), 1, buffer.size(), in);
        if (got > 0) {
            ok = std::fwrite(buffer.data(), 1, got, out) == got;
        }
        if (got < buffer.size()) {
            if (std::ferror(in) != 0) ok = false;
            break;
        }
    }

    ok = ok && std::fflush(out) == 0;
    ok = ok && std::ferror(out) == 0;
    const bool closeOutOk = std::fclose(out) == 0;
    const bool closeInOk = std::fclose(in) == 0;
    if (!(ok && closeOutOk && closeInOk)) {
        std::remove(to.c_str());
        return false;
    }
    return true;
}

std::string resolve_save_path() {
    const std::string legacy = resolve_legacy_save_path();
    char* pref = SDL_GetPrefPath(kSaveOrgName, kSaveAppName);
    if (!pref || pref[0] == '\0') {
        if (pref) SDL_free(pref);
        return legacy;
    }

    std::string path(pref);
    SDL_free(pref);
    path += kSaveFileName;

    if (!app_file_exists(path) && app_file_exists(legacy)) {
        if (!copy_file_bytes(legacy, path)) {
            return legacy;
        }
    }
    return path;
}

long file_size_bytes(const std::string& path) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return -1;
    if (std::fseek(f, 0, SEEK_END) != 0) {
        std::fclose(f);
        return -1;
    }
    const long len = std::ftell(f);
    std::fclose(f);
    return len;
}

bool smoke_framebuffer_has_world_pixels(const App& app, int& samplesHit) {
    samplesHit = 0;
    if (!app.worldLoaded || app.state != sm::ui::AppState::Playing) return false;
    if (app.width <= 0 || app.height <= 0) return false;

    const int xs[3] = {app.width / 4, app.width / 2, (app.width * 3) / 4};
    const int ys[3] = {app.height / 4, app.height / 2, (app.height * 3) / 4};
    std::array<std::uint8_t, 4> px{};
    for (int y : ys) {
        for (int x : xs) {
            glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
            const int rgb = int(px[0]) + int(px[1]) + int(px[2]);
            if (px[3] > 0 && rgb > 36) ++samplesHit;
        }
    }
    return samplesHit > 0;
}

const sm::Settlement* settlement_by_id(const sm::GameState& gs, int id) {
    for (const auto& s : gs.settlements) {
        if (s.id == id) return &s;
    }
    return nullptr;
}

int settlement_at_player(const sm::GameState& gs, float radius = 3.0f) {
    const float r2 = radius * radius;
    for (const auto& s : gs.settlements) {
        if (sm::torus_dist_sq(gs.player.x, gs.player.y,
                              float(s.x), float(s.y),
                              float(gs.mapW), float(gs.mapH)) <= r2) {
            return s.id;
        }
    }
    return -1;
}

void refresh_player_settlement(App& app) {
    const int id = settlement_at_player(app.gs);
    if (id == app.gs.subState.settlementId) return;
    app.gs.subState.settlementId = id;
    if (app.ui.settlement) app.ui.settlementId = id;
    if (id < 0) return;
    const sm::Settlement* s = settlement_by_id(app.gs, id);
    sm::GameEvent ev{sm::EventTag::SettlementVisit};
    ev.a = std::uint32_t(id);
    if (s) ev.s1 = s->name;
    app.bus.emit(ev);
}

void refresh_available_settlement_quests(App& app) {
    const int id = app.ui.settlement ? app.ui.settlementId
                                     : app.gs.subState.settlementId;
    if (id < 0) {
        app.availableSettlementQuests.clear();
        app.availableQuestSettlementId = -1;
        app.availableQuestDay = -1;
        return;
    }
    if (app.availableQuestSettlementId == id
        && app.availableQuestDay == app.gs.worldTime.day) {
        return;
    }
    const sm::Settlement* s = settlement_by_id(app.gs, id);
    if (!s) {
        app.availableSettlementQuests.clear();
        app.availableQuestSettlementId = -1;
        app.availableQuestDay = -1;
        return;
    }
    app.availableSettlementQuests =
        sm::generate_quests_for_settlement(*s, app.gs, app.gs.worldSeed);
    app.availableQuestSettlementId = id;
    app.availableQuestDay = app.gs.worldTime.day;
}

void toggle_settlement_panel(App& app) {
    refresh_player_settlement(app);
    if (app.cursor.hoverSettlementId >= 0) {
        app.ui.settlementId = app.cursor.hoverSettlementId;
    } else if (app.gs.subState.settlementId >= 0) {
        app.ui.settlementId = app.gs.subState.settlementId;
    }
    refresh_available_settlement_quests(app);
    app.ui.settlement = !app.ui.settlement;
}

void open_settlement_panel(App& app, sm::ui::SettlementPanelTab tab) {
    refresh_player_settlement(app);
    if (app.cursor.hoverSettlementId >= 0) {
        app.ui.settlementId = app.cursor.hoverSettlementId;
    } else if (app.gs.subState.settlementId >= 0) {
        app.ui.settlementId = app.gs.subState.settlementId;
    }
    app.ui.settlementTab = tab;
    refresh_available_settlement_quests(app);
    app.ui.settlement = true;
}

void emit_player_move(App& app, float prevX, float prevY, float dist) {
    if (dist <= 0.0f) return;
    sm::GameEvent ev{sm::EventTag::PlayerMove};
    ev.fx = prevX;
    ev.fy = prevY;
    ev.ix = sm::wrapi(int(std::floor(app.gs.player.x)), app.gs.mapW);
    ev.iy = sm::wrapi(int(std::floor(app.gs.player.y)), app.gs.mapH);
    ev.a = std::uint32_t(std::max(0.0f, dist) * 1000.0f);
    app.bus.emit(ev);
    refresh_player_settlement(app);
}

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
#if defined(_WIN32) && !defined(__EMSCRIPTEN__)
    if (!sm::gl_load_functions()) {
        std::fprintf(stderr, "OpenGL 3.x function load failed\n");
        return false;
    }
#endif
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
    if (app.worldLoaded && app.subworld.active()) app.subworld.leave();
    sm::sub::clear_saved_subworlds();
    app.bus.reset();
    app.logic.reset();
#ifndef NDEBUG
    assert(app.bus.subscription_count() == 0);
    assert(app.logic.node_count() == 0);
    assert(app.logic.active_count() == 0);
#endif
    app.appliedEventCount = 0;
    app.encounterDistAcc = 0.0f;
    app.showDialogOpen = false;
    app.showDialogEvent = sm::GameEvent{};
    app.showDialogCapturedTick = std::uint32_t(-1);
    app.availableSettlementQuests.clear();
    app.availableQuestSettlementId = -1;
    app.availableQuestDay = -1;
    if (!app.worldLoaded) return;
    sm::destroy_terrain(app.terrain);
    app.terrain = {};
    app.gs = sm::GameState{};
    app.activeQuests.clear();
    app.ecs.reg.clear();
    app.worldLoaded = false;
}

void refresh_save_summary(App& app) {
    app.saveSummary = sm::inspect_save(app.savePath);
}

void open_load_screen(App& app) {
    app.loadReturnState = app.state;
    refresh_save_summary(app);
    app.state = sm::ui::AppState::Load;
}

void boot_world(App& app, std::uint32_t seed,
                int mapW = 1024, int mapH = 1024,
                const sm::LayerParameters* lpOverride = nullptr,
                int targetTotalCities = 0) {
    boot_trace("start");
    if (boot_trace_enabled()) {
        std::fprintf(stderr, "[boot] params seed=%u map=%dx%d targetCities=%d\n",
                     seed, mapW, mapH, targetTotalCities);
        std::fflush(stderr);
    }
    destroy_world(app);
    boot_trace("destroyed previous world");

    sm::register_builtin_spells();
#ifndef NDEBUG
    assert(sm::spell_registry().is_consistent());
    assert(sm::spell_registry().size() > 0);
#endif
    sm::LayerParameters lp = lpOverride ? *lpOverride : sm::LayerParameters{};
    lp.seed = float(seed % 100000u);
    app.gs = sm::default_game_state(seed, mapW, mapH, lp, targetTotalCities);
    boot_trace("default game state");
    sm::reset_world_tick_runtime(app.worldTick, seed);
    sm::reset_macro_npc_ai_runtime(app.npcAi, seed);
    app.appliedEventCount = 0;
    app.encounterDistAcc = 0.0f;
    app.encounterRng = sm::Rng(seed ^ 0x9E3779B9u);
    app.ui.settlementId = -1;
    app.availableSettlementQuests.clear();
    app.availableQuestSettlementId = -1;
    app.availableQuestDay = -1;
    sm::register_builtin_nodes(app.logic);
#ifndef NDEBUG
    assert(app.bus.subscription_count() == 0);
    assert(app.logic.is_consistent());
    assert(app.logic.node_count() > 0);
    assert(app.logic.active_count() <= app.logic.node_count());
#endif

    app.terrain = sm::generate_terrain(app.gs.mapW, app.gs.mapH, lp);
    boot_trace("terrain generated");

    app.gs.politik = sm::generate_politik(app.gs.worldSeed, app.gs.mapW, app.gs.mapH,
                                          &app.terrain, std::uint8_t(lp.seaLevel * 255.0f),
                                          targetTotalCities);
    boot_trace("politik generated");
    sm::snap_cities_to_land(app.gs.politik, app.terrain, std::uint8_t(lp.seaLevel * 255.0f));
    sm::finalize_politik(app.gs.politik, app.terrain, std::uint8_t(lp.seaLevel * 255.0f));
    sm::populate_landmarks_from_politik(app.gs, app.terrain,
                                        std::uint8_t(lp.seaLevel * 255.0f));
    boot_trace("landmarks populated");

    app.trees = sm::spawn_trees(app.terrain, app.gs.worldSeed, 0.18f);
    boot_trace("trees spawned");
    sm::RoadTraceStats roadStats;
    auto roads = sm::trace_roads(app.terrain, app.gs.politik, &roadStats);
    if (boot_trace_enabled()) {
        std::fprintf(stderr,
                     "[roads] cities=%d attempted=%d kept=%d pruned=%d "
                     "bounded=%d fallback=%d expansions=%d edgeCapHits=%d wholeCapHits=%d\n",
                     roadStats.cityCount,
                     roadStats.attemptedEdges,
                     roadStats.keptEdges,
                     roadStats.prunedEdges,
                     roadStats.boundedEdges,
                     roadStats.fallbackEdges,
                     roadStats.expansions,
                     roadStats.edgeExpansionCapHits,
                     roadStats.wholeExpansionCapHits);
        std::fflush(stderr);
    }
    boot_trace("roads traced");
    const auto& citiesFlat = app.gs.politik.cities;
    std::vector<int> vx, vy;
    for (const auto& v : app.gs.villages) { vx.push_back(v.x); vy.push_back(v.y); }
    auto dirts = sm::trace_dirt_roads(app.gs.mapW, app.gs.mapH, roads, vx, vy,
                                       app.terrain.rgba.data());
    boot_trace("dirt roads traced");
    app.features = sm::build_feature_layer(app.terrain, app.trees, 0.78f, roads, &dirts);
    sm::build_tree_grid(app.treeGrid, app.trees, app.gs.mapW, app.gs.mapH);
    boot_trace("features and tree grid built");

    std::vector<sm::ZoneSeed> zsCities, zsVills;
    for (auto& c : citiesFlat) zsCities.push_back({c.x, c.y});
    for (auto& v : app.gs.villages) zsVills.push_back({v.x, v.y});
    app.zones = sm::generate_zones(app.gs.mapW, app.gs.mapH, app.gs.worldSeed,
                                   zsCities, zsVills, app.features);
    boot_trace("zones generated");

    if (!app.macro.init()) {
        boot_trace("macro renderer init failed");
    } else {
        boot_trace("macro renderer initialized");
    }
    app.macro.upload_features(app.features);
    boot_trace("features uploaded");
    app.macro.upload_zones(app.zones);
    boot_trace("zones uploaded");
    app.macro.rebuild_landmarks(app.gs);
    boot_trace("landmarks uploaded");

    app.pathCost = sm::build_cost_grid(app.terrain, &app.features, 0.40f);
    app.cursor = sm::ui::MacroCursor{};
    boot_trace("path cost built");

    sm::spawn_macro_npcs(app.gs, app.ecs, app.terrain, app.gs.worldSeed);
    boot_trace("macro npcs spawned");

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
    if (app.gs.subState.kind == sm::GameSubStateKind::Exploring
        && app.gs.subState.settlementId < 0) {
        app.gs.subState.settlementId = settlement_at_player(app.gs);
    }
    app.ui.settlementId = app.gs.subState.settlementId;

    app.subworld.init();
    app.worldLoaded = true;
    app.state = sm::ui::AppState::Playing;
    boot_trace("done");
}

bool boot_world_from_save(App& app) {
    sm::GameState fresh;
    std::vector<sm::Quest> loadedQuests;
    if (!sm::load_game(fresh, loadedQuests, app.savePath)) return false;
    boot_world(app, fresh.worldSeed, fresh.mapW, fresh.mapH,
               &fresh.mapParams, fresh.cityCountTarget);

    app.gs.version           = fresh.version;
    app.gs.saveName          = std::move(fresh.saveName);
    app.gs.savedAt           = std::move(fresh.savedAt);
    app.gs.mapParams         = fresh.mapParams;
    app.gs.cityCountTarget   = fresh.cityCountTarget;
    app.gs.worldTime         = fresh.worldTime;
    app.gs.player            = std::move(fresh.player);
    app.gs.settlements       = std::move(fresh.settlements);
    app.gs.villages          = std::move(fresh.villages);
    app.gs.spires            = std::move(fresh.spires);
    app.gs.markers           = std::move(fresh.markers);
    app.gs.factions          = std::move(fresh.factions);
    app.gs.subState          = std::move(fresh.subState);
    app.gs.deserterPool      = fresh.deserterPool;
    app.gs.activeTradeRoutes = std::move(fresh.activeTradeRoutes);
    app.gs.cityLastTradeDay  = std::move(fresh.cityLastTradeDay);
    app.activeQuests         = std::move(loadedQuests);

    app.macro.rebuild_landmarks(app.gs);
    app.camX = app.camTargetX = app.gs.player.x + 0.5f;
    app.camY = app.camTargetY = app.gs.player.y + 0.5f;
    app.gs.subState.settlementId = settlement_at_player(app.gs);
    app.ui.settlementId = app.gs.subState.settlementId;
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
                case SDLK_t:      toggle_settlement_panel(app); break;
                case SDLK_q:      app.ui.quest      = !app.ui.quest; break;
                case SDLK_i:
                case SDLK_TAB:
                    app.ui.character = !app.ui.character;
                    app.ui.characterTab = sm::ui::CharacterPanelTab::Inventory;
                    break;
                case SDLK_p:
                    app.ui.character = true;
                    app.ui.characterTab = sm::ui::CharacterPanelTab::Army;
                    break;
                case SDLK_e:
                    app.ui.character = true;
                    app.ui.characterTab = sm::ui::CharacterPanelTab::Equipment;
                    break;
                case SDLK_c:      app.ui.codex      = !app.ui.codex; break;
                case SDLK_m:      app.ui.map        = !app.ui.map; break;
                case SDLK_f:      app.subworld.toggle_3d(); break;
                case SDLK_F5:
                    sm::save_game(app.gs, app.activeQuests, app.savePath);
                    refresh_save_summary(app);
                    break;
                case SDLK_F9:     open_load_screen(app); break;
                case SDLK_RETURN:
                    if (!app.subworld.active()) {
                        app.subworld.enter(app.gs, app.terrain, app.features, app.ecs, app.bus);
                        boot_trace_time("subworld enter", app.gs.worldTime);
                    } else {
                        app.subworld.leave();
                        boot_trace_time("subworld leave", app.gs.worldTime);
                    }
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
    if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) {
        if (app.state == sm::ui::AppState::Load) {
            app.state = app.loadReturnState;
            return;
        }
        if (app.state == sm::ui::AppState::Paused) {
            app.state = sm::ui::AppState::Playing;
            return;
        }
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
    } else if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE &&
             app.state == sm::ui::AppState::Load) {
        app.state = app.loadReturnState;
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
        emit_player_move(app, prevX, prevY, dist);
        app.encounterDistAcc += dist;
        const float kEncounterStep = 60.0f;
        if (app.encounterDistAcc > kEncounterStep
            && app.gs.subState.kind == sm::GameSubStateKind::Exploring) {
            app.encounterDistAcc = 0.0f;
            if ((app.encounterRng.next_u32() >> 24) < 64) {
                const auto& tbl = sm::content::encounters();
                if (!tbl.empty()) {
                    int idx = int(app.encounterRng.next_u32() % std::uint32_t(tbl.size()));
                    sm::GameEvent ev{sm::EventTag::Encounter};
                    ev.a = std::uint32_t(idx);
                    ev.s1 = tbl[std::size_t(idx)].title;
                    app.bus.emit(ev);
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

void apply_pending_event_effects(App& app) {
    while (true) {
        const auto& events = app.bus.tick_events();
        if (app.appliedEventCount >= events.size()) return;

        const std::size_t begin = app.appliedEventCount;
        const std::size_t end = events.size();
        const sm::LevelData beforeLevel = app.gs.player.levelData;
        std::span<const sm::GameEvent> pending(events.data() + begin, end - begin);
        sm::apply_events(pending, app.gs.player);
        const sm::LevelData afterLevel = app.gs.player.levelData;
        app.appliedEventCount = end;
        sm::queue_player_level_up_if_needed(app.bus, pending, beforeLevel, afterLevel);
    }
}

bool has_active_wait_objective(const std::vector<sm::Quest>& quests) {
    for (const auto& q : quests) {
        for (const auto& o : q.objectives) {
            if (!o.completed && o.kind == sm::ObjectiveKind::WaitAt) return true;
        }
    }
    return false;
}

void emit_time_advance_if_needed(App& app, const sm::WorldTickResult& tick) {
    if (tick.hoursAdvanced <= 0) return;
    if (!app.bus.has_subscribers(sm::EventTag::TimeAdvance)
        && !has_active_wait_objective(app.activeQuests)) {
        return;
    }

    sm::GameEvent ev{sm::EventTag::TimeAdvance};
    ev.a = std::uint32_t(app.gs.worldTime.day);
    ev.ix = tick.hoursAdvanced;
    ev.iy = app.gs.worldTime.hour;
    app.bus.emit(ev);
}

void capture_show_dialog(App& app) {
    if (app.showDialogOpen) return;
    const std::uint32_t tick = app.bus.tick();
    if (app.showDialogCapturedTick == tick) return;

    for (const auto& ev : app.bus.tick_events()) {
        if (ev.tag == sm::EventTag::ShowDialog) {
            app.showDialogEvent = ev;
            app.showDialogOpen = true;
            app.showDialogCapturedTick = tick;
            return;
        }
    }
}

void process_world_events(App& app) {
    apply_pending_event_effects(app);
    app.bus.flush(app.gs.worldTime.day, app.gs.worldTime.hour);
    app.appliedEventCount = 0;
    app.logic.tick(app.bus, app.gs.player);
    app.quests.tick(app.activeQuests, app.bus, app.gs);
    apply_pending_event_effects(app);
    capture_show_dialog(app);
}

struct RuntimeFrameStats {
    sm::WorldTickResult timeTick{};
    sm::MacroNpcAiSliceResult macroNpcAi{};
    bool ticked = false;
    bool subworldActive = false;
};

RuntimeFrameStats tick_playing_runtime(App& app, float dt, bool allowInput) {
    RuntimeFrameStats stats{};
    if (app.state != sm::ui::AppState::Playing || !app.worldLoaded) return stats;

    stats.ticked = true;
    apply_pending_event_effects(app);
    if (app.subworld.active()) {
        stats.subworldActive = true;
        if (allowInput) poll_movement(app, dt);
        stats.timeTick = sm::tick_world_time_only(app.gs, app.worldTick, dt);
        stats.timeTick.dailyTicksProcessed =
            sm::process_world_daily_ticks(app.gs, app.worldTick,
                                          kSubworldDailyTicksPerFrame);
        stats.timeTick.dailyBudgetExhausted =
            app.worldTick.pendingDailyTicks > 0;
        app.subworld.tick(dt);
        stats.macroNpcAi =
            sm::tick_macro_npc_ai_budgeted(app.gs, app.ecs, &app.treeGrid,
                                           app.npcAi, dt,
                                           kSubworldMacroNpcTicksPerFrame);
        emit_time_advance_if_needed(app, stats.timeTick);
        process_world_events(app);
    } else {
        if (allowInput) poll_movement(app, dt);
        update_camera(app, dt);
        stats.timeTick = sm::tick_world(app.gs, app.worldTick, dt);
        sm::tick_macro_npc_ai(app.gs, app.ecs, &app.treeGrid, app.npcAi, dt);
        app.npcAi.sweepAccum = 0.0f;
        app.npcAi.pendingSweeps = 0;
        app.npcAi.sweepCursor = 0;
        emit_time_advance_if_needed(app, stats.timeTick);
        process_world_events(app);
    }
    if (app.gs.player.combatStats.currentHp <= 0) {
        app.state = sm::ui::AppState::Dead;
    }
    return stats;
}

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

void merge_shell_result(sm::ui::ShellResult& dst, const sm::ui::ShellResult& src) {
    dst.startNewGame        = dst.startNewGame        || src.startNewGame;
    dst.openCustomNewGame   = dst.openCustomNewGame   || src.openCustomNewGame;
    dst.startCustomNewGame  = dst.startCustomNewGame  || src.startCustomNewGame;
    dst.cancelCustomNewGame = dst.cancelCustomNewGame || src.cancelCustomNewGame;
    dst.regenerateCustom    = dst.regenerateCustom    || src.regenerateCustom;
    dst.loadGame            = dst.loadGame            || src.loadGame;
    dst.cancelLoad          = dst.cancelLoad          || src.cancelLoad;
    dst.saveGame            = dst.saveGame            || src.saveGame;
    dst.resume              = dst.resume              || src.resume;
    dst.returnToTitle       = dst.returnToTitle       || src.returnToTitle;
    dst.quit                = dst.quit                || src.quit;
}

int smoke_total_minutes(const sm::WorldTime& t) {
    return ((t.day - 1) * 24 + t.hour) * 60 + t.minute;
}

bool run_subworld_time_smoke(App& app) {
    if (!smoke_boot_invariants_hold(app)) {
        smoke_print_counts(app, "subworld_time_boot_failed");
        smoke_fail(app, "subworld_time boot invariants");
        return false;
    }
    if (app.subworld.active()) {
        smoke_fail(app, "subworld_time already active");
        return false;
    }

    const sm::WorldTime before = app.gs.worldTime;
    const int beforeMinutes = smoke_total_minutes(before);
    const float playerBeforeX = app.gs.player.x;
    const float playerBeforeY = app.gs.player.y;
    const int expectedPlayerX =
        sm::wrapi(int(std::floor(playerBeforeX)), app.gs.mapW);
    const int expectedPlayerY =
        sm::wrapi(int(std::floor(playerBeforeY)), app.gs.mapH);
    const int dailyPendingStart = app.worldTick.pendingDailyTicks;
    const int sweepsPendingStart = app.npcAi.pendingSweeps;

    std::fprintf(stderr,
                 "[smoke] subworld_time before day=%d %02d:%02d "
                 "player=%.1f,%.1f pendingDaily=%d pendingSweeps=%d\n",
                 before.day, before.hour, before.minute,
                 playerBeforeX, playerBeforeY,
                 dailyPendingStart, sweepsPendingStart);
    std::fflush(stderr);

    app.subworld.enter(app.gs, app.terrain, app.features, app.ecs,
                       app.bus, &app.zones);
    if (!app.subworld.active()) {
        smoke_fail(app, "subworld_time enter failed");
        return false;
    }

    int minutesAdvanced = 0;
    int daysAdvanced = 0;
    int dailyProcessed = 0;
    int npcProcessed = 0;
    int sweepsCompleted = 0;
    int backlogFrames = 0;
    int maxPendingDaily = app.worldTick.pendingDailyTicks;
    int maxPendingSweeps = app.npcAi.pendingSweeps;
    std::size_t maxSweepCursor = app.npcAi.sweepCursor;

    for (int i = 0; i < kSubworldSmokeFrames; ++i) {
        RuntimeFrameStats frameStats =
            tick_playing_runtime(app, kSubworldSmokeDt, false);
        if (!frameStats.ticked || !frameStats.subworldActive) {
            smoke_fail(app, "subworld_time runtime tick inactive");
            return false;
        }
        minutesAdvanced += frameStats.timeTick.minutesAdvanced;
        daysAdvanced += frameStats.timeTick.daysAdvanced;
        dailyProcessed += frameStats.timeTick.dailyTicksProcessed;
        npcProcessed += frameStats.macroNpcAi.npcsProcessed;
        sweepsCompleted += frameStats.macroNpcAi.sweepsCompleted;
        if (frameStats.macroNpcAi.backlog) ++backlogFrames;
        maxPendingDaily = std::max(maxPendingDaily, app.worldTick.pendingDailyTicks);
        maxPendingSweeps = std::max(maxPendingSweeps, app.npcAi.pendingSweeps);
        maxSweepCursor = std::max(maxSweepCursor, app.npcAi.sweepCursor);
    }

    const sm::WorldTime beforeLeave = app.gs.worldTime;
    const int pendingDailyBeforeLeave = app.worldTick.pendingDailyTicks;
    const int pendingSweepsBeforeLeave = app.npcAi.pendingSweeps;
    const std::size_t sweepCursorBeforeLeave = app.npcAi.sweepCursor;
    const bool activeBeforeLeave = app.subworld.active();
    app.subworld.leave();
    const bool activeAfterLeave = app.subworld.active();

    const sm::WorldTime after = app.gs.worldTime;
    const int afterMinutes = smoke_total_minutes(after);
    const int playerAfterX = int(app.gs.player.x);
    const int playerAfterY = int(app.gs.player.y);
    const bool timeAdvanced = afterMinutes > beforeMinutes
        && minutesAdvanced == afterMinutes - beforeMinutes;
    const bool dailyCaughtUp = daysAdvanced == dailyProcessed
        && pendingDailyBeforeLeave == 0
        && app.worldTick.pendingDailyTicks == pendingDailyBeforeLeave;
    const bool npcBounded = maxPendingSweeps <= 4
        && app.npcAi.pendingSweeps <= 4
        && app.npcAi.sweepAccum < sm::kAiTickSec;
    const bool playerSynced = playerAfterX == expectedPlayerX
        && playerAfterY == expectedPlayerY;
    const bool leaveOk = activeBeforeLeave && !activeAfterLeave;

    std::fprintf(stderr,
                 "[smoke] subworld_time after day=%d %02d:%02d "
                 "preLeave=%d %02d:%02d player=%d,%d expected=%d,%d\n",
                 after.day, after.hour, after.minute,
                 beforeLeave.day, beforeLeave.hour, beforeLeave.minute,
                 playerAfterX, playerAfterY, expectedPlayerX, expectedPlayerY);
    std::fprintf(stderr,
                 "[smoke] subworld_time frames=%d dt=%.3f minutes=%d days=%d "
                 "dailyProcessed=%d pendingDailyStart=%d pendingDailyBeforeLeave=%d "
                 "pendingDailyAfterLeave=%d maxPendingDaily=%d\n",
                 kSubworldSmokeFrames, kSubworldSmokeDt, minutesAdvanced,
                 daysAdvanced, dailyProcessed, dailyPendingStart,
                 pendingDailyBeforeLeave, app.worldTick.pendingDailyTicks,
                 maxPendingDaily);
    std::fprintf(stderr,
                 "[smoke] subworld_time npcProcessed=%d sweepsCompleted=%d "
                 "backlogFrames=%d pendingSweepsStart=%d pendingSweepsBeforeLeave=%d "
                 "pendingSweepsAfterLeave=%d maxPendingSweeps=%d "
                 "sweepCursorBeforeLeave=%zu maxSweepCursor=%zu sweepAccum=%.3f\n",
                 npcProcessed, sweepsCompleted, backlogFrames,
                 sweepsPendingStart, pendingSweepsBeforeLeave,
                 app.npcAi.pendingSweeps, maxPendingSweeps,
                 sweepCursorBeforeLeave, maxSweepCursor, app.npcAi.sweepAccum);
    std::fflush(stderr);

    if (!timeAdvanced) {
        smoke_fail(app, "subworld_time did not advance exactly");
        return false;
    }
    if (!dailyCaughtUp) {
        smoke_fail(app, "subworld_time daily budget invariant");
        return false;
    }
    if (!npcBounded) {
        smoke_fail(app, "subworld_time npc budget invariant");
        return false;
    }
    if (!playerSynced || !leaveOk) {
        smoke_fail(app, "subworld_time leave/player invariant");
        return false;
    }

    std::fprintf(stderr, "[smoke] subworld_time OK\n");
    std::fflush(stderr);
    return true;
}

sm::ui::ShellResult tick_smoke_script(App& app) {
    sm::ui::ShellResult shell{};
    if (!app.smoke.enabled || app.smoke.failed) return shell;
    if (app.smoke.cursor >= app.smoke.count) {
        std::fprintf(stderr, "[smoke] PASS\n");
        std::fflush(stderr);
        app.running = false;
        return shell;
    }

    const SmokeAction action = app.smoke.actions[std::size_t(app.smoke.cursor)];
    switch (action) {
        case SmokeAction::NewGame:
            std::fprintf(stderr, "[smoke] action=new_game\n");
            std::fflush(stderr);
            shell.startNewGame = true;
            ++app.smoke.cursor;
            break;
        case SmokeAction::SaveGame: {
            std::fprintf(stderr, "[smoke] action=save_game\n");
            std::fflush(stderr);
            if (!app.worldLoaded) {
                smoke_fail(app, "save without world");
                break;
            }
            if (!sm::save_game(app.gs, app.activeQuests, app.savePath)) {
                smoke_fail(app, "save_game returned false");
                break;
            }
            refresh_save_summary(app);
            if (app.saveSummary.status != sm::SaveInspectStatus::Ready) {
                smoke_fail(app, "saved slot not inspectable");
                break;
            }
            const long bytes = file_size_bytes(app.savePath);
            if (bytes <= 0) {
                smoke_fail(app, "saved slot empty");
                break;
            }
            std::fprintf(stderr, "[smoke] saved path=%s bytes=%ld\n",
                         app.savePath.c_str(), bytes);
            std::fflush(stderr);
            ++app.smoke.cursor;
            break;
        }
        case SmokeAction::OpenLoad:
            std::fprintf(stderr, "[smoke] action=open_load\n");
            std::fflush(stderr);
            shell.loadGame = true;
            ++app.smoke.cursor;
            break;
        case SmokeAction::LoadGame:
            std::fprintf(stderr, "[smoke] action=load_game\n");
            std::fflush(stderr);
            if (app.state != sm::ui::AppState::Load) {
                smoke_fail(app, "load_game action outside load screen");
                break;
            }
            shell.loadGame = true;
            app.smoke.pendingLoadBoot = true;
            ++app.smoke.cursor;
            break;
        case SmokeAction::WaitBootDone:
            if (!smoke_boot_invariants_hold(app)) {
                smoke_print_counts(app, "boot_wait_failed");
                smoke_fail(app, "boot invariants");
                break;
            }
            ++app.smoke.bootsObserved;
            if (app.smoke.pendingLoadBoot) {
                smoke_print_counts(app, "load_boot");
                app.smoke.pendingLoadBoot = false;
            } else {
                smoke_print_counts(app,
                    app.smoke.bootsObserved == 1 ? "boot#1" : "boot#2");
            }
            ++app.smoke.cursor;
            break;
        case SmokeAction::SubworldTime:
            std::fprintf(stderr, "[smoke] action=subworld_time\n");
            std::fflush(stderr);
            if (run_subworld_time_smoke(app)) ++app.smoke.cursor;
            break;
        case SmokeAction::WaitVisible: {
            if (!smoke_boot_invariants_hold(app)) {
                smoke_print_counts(app, "visible_wait_failed");
                smoke_fail(app, "visible invariants");
                break;
            }
            int samplesHit = 0;
            if (!smoke_framebuffer_has_world_pixels(app, samplesHit)) {
                smoke_fail(app, "world framebuffer not visible");
                break;
            }
            ++app.smoke.visibleChecks;
            std::fprintf(stderr, "[smoke] visible samples=%d check=%d\n",
                         samplesHit, app.smoke.visibleChecks);
            std::fflush(stderr);
            ++app.smoke.cursor;
            break;
        }
        case SmokeAction::OpenQuests:
            std::fprintf(stderr, "[smoke] action=open_quests\n");
            std::fflush(stderr);
            if (!app.worldLoaded) {
                smoke_fail(app, "open_quests without world");
                break;
            }
            app.ui.quest = true;
            app.ui.questSelection = 0;
            std::fprintf(stderr,
                         "[smoke] quest_journal open active=%zu done=%zu first=%s\n",
                         app.activeQuests.size(),
                         app.gs.player.completedQuestIds.size(),
                         app.activeQuests.empty()
                             ? "(none)" : app.activeQuests.front().id.c_str());
            std::fflush(stderr);
            ++app.smoke.cursor;
            break;
        case SmokeAction::TriggerLevelDialog: {
            std::fprintf(stderr, "[smoke] action=trigger_level_dialog\n");
            std::fflush(stderr);
            if (!app.worldLoaded) {
                smoke_fail(app, "trigger_level_dialog without world");
                break;
            }
            sm::GameEvent xp{sm::EventTag::ApplyEffect};
            xp.s1 = "grant_xp";
            xp.ix = std::max(1, app.gs.player.levelData.expToNext);
            app.bus.emit(xp);
            process_world_events(app);
            if (!app.showDialogOpen
                || app.showDialogEvent.tag != sm::EventTag::ShowDialog) {
                smoke_fail(app, "ShowDialog was not captured");
                break;
            }
            std::fprintf(stderr,
                         "[smoke] show_dialog title=\"%s\" body=\"%s\" choices=%d\n",
                         app.showDialogEvent.s1.c_str(),
                         app.showDialogEvent.s2.c_str(),
                         app.showDialogEvent.ix);
            std::fflush(stderr);
            ++app.smoke.cursor;
            break;
        }
        case SmokeAction::ReturnTitle:
            std::fprintf(stderr, "[smoke] action=return_title\n");
            std::fflush(stderr);
            shell.returnToTitle = true;
            app.smoke.verifyDestroyAfterShell = true;
            ++app.smoke.cursor;
            break;
        case SmokeAction::Quit:
            std::fprintf(stderr, "[smoke] action=quit\n[smoke] PASS\n");
            std::fflush(stderr);
            shell.quit = true;
            ++app.smoke.cursor;
            break;
    }
    return shell;
}

void smoke_after_shell_actions(App& app) {
    if (!app.smoke.enabled || app.smoke.failed) return;
    if (!app.smoke.verifyDestroyAfterShell) return;
    app.smoke.verifyDestroyAfterShell = false;
    if (!smoke_destroy_invariants_hold(app)) {
        smoke_print_counts(app, "destroy_failed");
        smoke_fail(app, "destroy invariants");
        return;
    }
    smoke_print_counts(app, "destroy");
}

void apply_shell_actions(App& app, const sm::ui::ShellResult& r) {
    if (r.startNewGame)        boot_world(app, choose_new_game_seed(app));
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
        if (app.state == sm::ui::AppState::Load) {
            if (!boot_world_from_save(app)) {
                std::fprintf(stderr, "load_game: no save at %s\n", app.savePath.c_str());
                refresh_save_summary(app);
            }
        } else {
            open_load_screen(app);
        }
    }
    if (r.cancelLoad) {
        app.state = app.loadReturnState;
    }
    if (r.saveGame) {
        sm::save_game(app.gs, app.activeQuests, app.savePath);
        refresh_save_summary(app);
    }
    if (r.resume)        app.state = sm::ui::AppState::Playing;
    if (r.returnToTitle) { destroy_world(app); app.state = sm::ui::AppState::Title; }
    if (r.quit)          app.running = false;
}

void frame(App& app, float dt) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) handle_event(app, e);

    tick_playing_runtime(app, dt, true);

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
        if (app.cursor.hoverSettlementId >= 0) {
            app.ui.settlementId = app.cursor.hoverSettlementId;
        }
        if (app.cursor.clickedSettlementId >= 0) {
            app.ui.settlementId = app.cursor.clickedSettlementId;
            app.availableQuestSettlementId = -1;
        }
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
        case sm::ui::AppState::Load:
            shell = sm::ui::draw_load_screen(app.saveSummary, app.width, app.height);
            break;
        case sm::ui::AppState::Playing:
            sm::ui::draw_player_hud(app.gs);
            {
                auto tb = sm::ui::draw_bottom_toolbar(app.gs, app.subworld.active());
                if (tb.pause)         app.state          = sm::ui::AppState::Paused;
                if (tb.diplomacy)     app.ui.diplomacy   = !app.ui.diplomacy;
                if (tb.build)         open_settlement_panel(app, sm::ui::SettlementPanelTab::Info);
                if (tb.quests)        app.ui.quest       = !app.ui.quest;
                if (tb.codex)         app.ui.codex       = !app.ui.codex;
                if (tb.map)           app.ui.map         = !app.ui.map;
                if (tb.inventory) {
                    app.ui.character = true;
                    app.ui.characterTab = sm::ui::CharacterPanelTab::Inventory;
                }
                if (tb.party) {
                    app.ui.character = true;
                    app.ui.characterTab = sm::ui::CharacterPanelTab::Army;
                }
                if (tb.equipment) {
                    app.ui.character = true;
                    app.ui.characterTab = sm::ui::CharacterPanelTab::Equipment;
                }
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
            sm::ui::draw_character_panel(app.gs, &app.ui.character, &app.ui.characterTab);
            if (app.ui.settlement) refresh_available_settlement_quests(app);
            sm::ui::draw_settlement(app.gs,
                                    app.ui.settlementId,
                                    app.availableSettlementQuests,
                                    app.activeQuests,
                                    app.quests,
                                    app.bus,
                                    &app.ui.settlementTab,
                                    &app.ui.settlement);
            sm::ui::draw_quest_log(app.gs,
                                   app.activeQuests,
                                   app.quests,
                                   app.bus,
                                   &app.ui.questSelection,
                                   &app.ui.quest);
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
            sm::ui::draw_show_dialog(app.showDialogEvent, &app.showDialogOpen);
            break;
        case sm::ui::AppState::Paused:
            sm::ui::draw_player_hud(app.gs);
            shell = sm::ui::draw_pause_menu(app.width, app.height);
            break;
        case sm::ui::AppState::Dead:
            shell = sm::ui::draw_death_screen(app.gs, app.width, app.height);
            break;
    }
    merge_shell_result(shell, tick_smoke_script(app));
    apply_shell_actions(app, shell);
    smoke_after_shell_actions(app);

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    SDL_GL_SwapWindow(app.window);
}

} // namespace

int main(int /*argc*/, char* /*argv*/[]) {
#if defined(_WIN32)
    SetUnhandledExceptionFilter(crash_filter);
#endif
    App app;
    if (!parse_smoke_script(std::getenv(kSmokeScriptEnv), app.smoke)) return 2;
    if (!boot_window(app)) return 1;
    app.savePath = resolve_save_path();
    if (boot_trace_enabled() || app.smoke.enabled) {
        std::fprintf(stderr, "[save] path=%s\n", app.savePath.c_str());
        std::fflush(stderr);
    }
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

    const int exitCode = app.smoke.failed ? 2 : 0;
    destroy_world(app);
    app.subworld.destroy();
    app.macro.destroy();
    shutdown_imgui();
    SDL_GL_DeleteContext(app.gl);
    SDL_DestroyWindow(app.window);
    SDL_Quit();
    return exitCode;
}

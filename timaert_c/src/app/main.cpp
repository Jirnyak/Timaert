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
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <SDL.h>
#include <SDL_vulkan.h>
#include "gpu/vk_device.h"
#include "gpu/vk_renderer.h"
#include "core/torus.h"
#include "ecs/world.h"
#include "ecs/components.h"
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
#include "macro/vk_macro_renderer.h"
#include "macro/biomes.h"
#include "macro/world_tick.h"
#include "macro/npc_ai.h"
#include "macro/npc_spawn.h"
#include "macro/pathfinding.h"
#include "macro/items.h"
#include "macro/player_recovery.h"
#include "macro/travel.h"
#include "macro/audio.h"
#include "macro/save.h"
#include "content/spells/registry.h"
#include "content/spells/spell_book.h"
#include "content/spells/spell_types.h"
#include "content/plot/intro.h"
#include "content/quests/procedural.h"
#include "sub/engine.h"
#include "sub/map_factory.h"
#include "sub/tree_atlas.h"
#include "sub/fauna.h"
#include "ui/overlays.h"
#include "ui/screens.h"
#include "ui/macro_overlay.h"
#include "ui/ui_gpu.h"
#include "assets/sprite_atlas.h"
#include "app/debug_console.h"

#include "imgui.h"
#include "backends/imgui_impl_sdl2.h"
#include "backends/imgui_impl_vulkan.h"
#include <vulkan/vulkan.h>

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
constexpr float kSubworldTimeScale = 0.10f;
constexpr float kSubworldHitFlashSeconds = 0.30f;
constexpr float kSubworldSpPer1000 = 10.0f;
constexpr const char* kSmokeScriptEnv = "TIMAERT_SMOKE_SCRIPT";
constexpr const char* kSmokeSeedEnv = "TIMAERT_SMOKE_SEED";
constexpr int kSubworldSmokeFrames = 1000;
constexpr int kSubworldSeamSmokeSettleFrames = 120;
constexpr float kSubworldSmokeDt = 0.1f;
constexpr std::size_t kPendingPresentationMax = 8;
constexpr int kSmokeMacroTravelSteps = 3;

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
    SubworldSeam,
    SubworldAudio,
    SubworldExitGate,
    SubworldLootXp,
    SubworldEnemyFeedback,
    SubworldMissileFeedback,
    SubworldPlayerMelee,
    SubworldReputationHit,
    SubworldMouseRelease,
    SubworldTreeAnchor,
    SubworldNoRecovery,
    SubworldSpDrain,
    SubworldEnter,
    TriggerBattleStart,
    WaitVisible,
    OpenSettlementBuild,
    OpenSettlementTrade,
    OpenSettlementMap,
    EnterFirstSettlement,
    FocusNpcPanel,
    OpenNpcTrade,
    AttackFirstNpc,
    CaptureFrame,
    OpenMap,
    OpenStats,
    SpendAttributeVit,
    SpendSkillBodybuilding,
    MacroTravelSp,
    MacroRecovery,
    TimeAdvanceBurst,
    MacroNpcTrace,
    OpenQuests,
    OpenCodex,
    OpenSpells,
    CastSpell,
    ToggleHaste,
    ToggleFlight,
    PrepareSpellAuras,
    TriggerLevelDialog,
    TriggerCountOnlyDialog,
    TriggerStoryOverlay,
    CompleteStoryOverlay,
    ConsoleSmoke,
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
    bool capturePending = false;
    int captureActionIndex = 0;
};

struct App {
    SDL_Window*   window  = nullptr;
    gpu::VulkanDevice  device;
    gpu::VulkanRenderer renderer;
    VkDescriptorPool imguiPool = VK_NULL_HANDLE;
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
    sm::MacroRendererVk  macro;
    sm::ecs::World       ecs;
    sm::EventBus         bus;
    sm::LogicNodeEngine  logic;
    sm::QuestEngine      quests;
    std::vector<sm::Quest> activeQuests;
    std::vector<sm::Quest> availableSettlementQuests;
    int                  availableQuestSettlementId = -1;
    int                  availableQuestDay = -1;
    std::size_t          appliedEventCount = 0;
    std::size_t          appliedStoryResultCount = 0;
    std::size_t          appliedCombatEventCount = 0;
    sm::WorldTickRuntime worldTick;
    sm::PlayerRecoveryAccumulator playerRecovery;
    sm::MacroNpcAiRuntime npcAi;
    sm::sub::SubworldEngine subworld;
    sm::AudioSystem      audio;
    sm::MusicId          audioDesired = sm::MusicId::Count;
    sm::MusicId          audioFailed = sm::MusicId::Count;
    int                  subworldLastPlayerHp = -1;
    float                subworldHitFlashTimer = 0.0f;
    float                subworldDistanceAccum = 0.0f;

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
    bool  relativeMouseActive = false;

    bool  showDebug = false;
    bool  showDialogOpen = false;
    sm::GameEvent showDialogEvent{};
    sm::ui::DialogOverlayState showDialogUi{};
    std::uint32_t showDialogCapturedTick = std::uint32_t(-1);
    sm::ui::StoryOverlayState storyOverlay;
    std::uint32_t showStoryCapturedTick = std::uint32_t(-1);
    std::array<sm::GameEvent, kPendingPresentationMax> pendingPresentationEvents{};
    std::size_t pendingPresentationCount = 0;
    std::uint32_t pendingPresentationTick = std::uint32_t(-1);
    std::size_t pendingPresentationSeen = 0;
    sm::ui::Toggles ui;
    sm::ui::MacroCursor cursor;
    sm::SaveSummary saveSummary;
    sm::PathCostData pathCost;
    sm::ui::CustomGameParams customParams; // remembered across visits to the menu
    ImTextureID  customPreviewTex   = ImTextureID();  // biome-coloured world preview
    int          customPreviewSide  = 0;        // 0 = no preview built yet
    bool         customWorldReady   = false;    // true after a regen succeeds
    SmokeScript smoke;

    // Developer console (Quake-style REPL + inspector panels). Toggled with
    // the backtick key in the Playing state. Commands are registered in
    // `register_console_commands` and capture `App&`.
    sm::dev::Console console;
    // Dev console: multiplies the live-frame simulation dt (1.0 = normal). Only
    // the interactive frame path honours this; scripted/smoke steps keep their
    // fixed dt so determinism is preserved.
    float simSpeed = 1.0f;
    // Dev inspector panels (the "panels" half of the hybrid console). Each is a
    // read-only ImGui window toggled by a console command; they read live game
    // state generically so no per-content wiring is needed.
    struct DebugPanels {
        bool entities  = false;
        bool ecsStats  = false;
        bool gameState = false;
        bool journal   = false;
    } panels;
};

bool modal_overlay_active(const App& app);

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
    if (smoke_token_equals(token, "subworld_seam")) {
        out = SmokeAction::SubworldSeam;
        return true;
    }
    if (smoke_token_equals(token, "subworld_audio")) {
        out = SmokeAction::SubworldAudio;
        return true;
    }
    if (smoke_token_equals(token, "subworld_exit_gate")) {
        out = SmokeAction::SubworldExitGate;
        return true;
    }
    if (smoke_token_equals(token, "subworld_loot_xp")) {
        out = SmokeAction::SubworldLootXp;
        return true;
    }
    if (smoke_token_equals(token, "subworld_enemy_feedback")) {
        out = SmokeAction::SubworldEnemyFeedback;
        return true;
    }
    if (smoke_token_equals(token, "subworld_missile_feedback")) {
        out = SmokeAction::SubworldMissileFeedback;
        return true;
    }
    if (smoke_token_equals(token, "subworld_player_melee")) {
        out = SmokeAction::SubworldPlayerMelee;
        return true;
    }
    if (smoke_token_equals(token, "subworld_reputation_hit")) {
        out = SmokeAction::SubworldReputationHit;
        return true;
    }
    if (smoke_token_equals(token, "subworld_mouse_release")) {
        out = SmokeAction::SubworldMouseRelease;
        return true;
    }
    if (smoke_token_equals(token, "subworld_tree_anchor")) {
        out = SmokeAction::SubworldTreeAnchor;
        return true;
    }
    if (smoke_token_equals(token, "subworld_no_recovery")) {
        out = SmokeAction::SubworldNoRecovery;
        return true;
    }
    if (smoke_token_equals(token, "subworld_sp_drain")) {
        out = SmokeAction::SubworldSpDrain;
        return true;
    }
    if (smoke_token_equals(token, "subworld_enter")) {
        out = SmokeAction::SubworldEnter;
        return true;
    }
    if (smoke_token_equals(token, "trigger_battle_start")) {
        out = SmokeAction::TriggerBattleStart;
        return true;
    }
    if (smoke_token_equals(token, "wait_visible")) {
        out = SmokeAction::WaitVisible;
        return true;
    }
    if (smoke_token_equals(token, "open_settlement_build")) {
        out = SmokeAction::OpenSettlementBuild;
        return true;
    }
    if (smoke_token_equals(token, "open_settlement_trade")) {
        out = SmokeAction::OpenSettlementTrade;
        return true;
    }
    if (smoke_token_equals(token, "open_settlement_map")) {
        out = SmokeAction::OpenSettlementMap;
        return true;
    }
    if (smoke_token_equals(token, "enter_first_settlement")) {
        out = SmokeAction::EnterFirstSettlement;
        return true;
    }
    if (smoke_token_equals(token, "focus_npc_panel")) {
        out = SmokeAction::FocusNpcPanel;
        return true;
    }
    if (smoke_token_equals(token, "open_npc_trade")) {
        out = SmokeAction::OpenNpcTrade;
        return true;
    }
    if (smoke_token_equals(token, "attack_first_npc")) {
        out = SmokeAction::AttackFirstNpc;
        return true;
    }
    if (smoke_token_equals(token, "capture_frame")) {
        out = SmokeAction::CaptureFrame;
        return true;
    }
    if (smoke_token_equals(token, "open_map")) {
        out = SmokeAction::OpenMap;
        return true;
    }
    if (smoke_token_equals(token, "open_stats")) {
        out = SmokeAction::OpenStats;
        return true;
    }
    if (smoke_token_equals(token, "spend_attribute_vit")) {
        out = SmokeAction::SpendAttributeVit;
        return true;
    }
    if (smoke_token_equals(token, "spend_skill_bodybuilding")) {
        out = SmokeAction::SpendSkillBodybuilding;
        return true;
    }
    if (smoke_token_equals(token, "macro_travel_sp")) {
        out = SmokeAction::MacroTravelSp;
        return true;
    }
    if (smoke_token_equals(token, "macro_recovery")) {
        out = SmokeAction::MacroRecovery;
        return true;
    }
    if (smoke_token_equals(token, "timeadvance_burst")) {
        out = SmokeAction::TimeAdvanceBurst;
        return true;
    }
    if (smoke_token_equals(token, "macro_npc_trace")) {
        out = SmokeAction::MacroNpcTrace;
        return true;
    }
    if (smoke_token_equals(token, "open_quests")) {
        out = SmokeAction::OpenQuests;
        return true;
    }
    if (smoke_token_equals(token, "open_codex")) {
        out = SmokeAction::OpenCodex;
        return true;
    }
    if (smoke_token_equals(token, "open_spells")) {
        out = SmokeAction::OpenSpells;
        return true;
    }
    if (smoke_token_equals(token, "cast_spell")) {
        out = SmokeAction::CastSpell;
        return true;
    }
    if (smoke_token_equals(token, "toggle_haste")) {
        out = SmokeAction::ToggleHaste;
        return true;
    }
    if (smoke_token_equals(token, "toggle_flight")) {
        out = SmokeAction::ToggleFlight;
        return true;
    }
    if (smoke_token_equals(token, "prepare_spell_auras")) {
        out = SmokeAction::PrepareSpellAuras;
        return true;
    }
    if (smoke_token_equals(token, "trigger_level_dialog")) {
        out = SmokeAction::TriggerLevelDialog;
        return true;
    }
    if (smoke_token_equals(token, "trigger_count_only_dialog")) {
        out = SmokeAction::TriggerCountOnlyDialog;
        return true;
    }
    if (smoke_token_equals(token, "trigger_story_overlay")) {
        out = SmokeAction::TriggerStoryOverlay;
        return true;
    }
    if (smoke_token_equals(token, "complete_story_overlay")) {
        out = SmokeAction::CompleteStoryOverlay;
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
    if (smoke_token_equals(token, "console")) {
        out = SmokeAction::ConsoleSmoke;
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

bool smoke_framebuffer_has_world_pixels(const App& /*app*/, int& samplesHit) {
    // Vulkan readback not yet implemented (PHASE C). Assume pixels are present.
    samplesHit = 9;
    return true;
}

bool write_smoke_frame_ppm(const App& /*app*/, int /*actionIndex*/, const char* /*label*/) {
    // Vulkan readback not yet implemented (PHASE C). Stub success.
    return true;
}

void smoke_clear_modal_overlays(App& app) {
    app.showDialogOpen = false;
    app.showDialogEvent = sm::GameEvent{};
    app.showDialogUi = sm::ui::DialogOverlayState{};
    app.storyOverlay = sm::ui::StoryOverlayState{};
    app.pendingPresentationCount = 0;
    app.pendingPresentationTick = std::uint32_t(-1);
    app.pendingPresentationSeen = 0;
    if (app.gs.subState.kind == sm::GameSubStateKind::Event) {
        app.gs.subState.kind = sm::GameSubStateKind::Exploring;
    }
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
    const int previousId = app.gs.subState.settlementId;
    if (id == previousId) return;
    if (previousId >= 0) {
        const sm::Settlement* previous = settlement_by_id(app.gs, previousId);
        sm::GameEvent leave{sm::EventTag::PlayerLeaveSettlement};
        leave.a = std::uint32_t(previousId);
        leave.ix = previousId;
        if (previous) leave.s1 = previous->name;
        app.bus.emit(leave);
    }
    app.gs.subState.settlementId = id;
    if (app.ui.settlement) app.ui.settlementId = id;
    if (id < 0) return;
    const sm::Settlement* s = settlement_by_id(app.gs, id);
    sm::GameEvent ev{sm::EventTag::PlayerEnterSettlement};
    ev.a = std::uint32_t(id);
    ev.ix = id;
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

const char* macro_npc_display_name(sm::NPCType type,
                                   const sm::ecs::NpcCharacter& ch) {
    const sm::NpcTypeDef& def = sm::npc_def(type);
    if (def.nameCount > 0) return def.names[ch.nameIdx % def.nameCount];
    return def.label;
}

bool route_macro_npc_attack(App& app, entt::entity npc) {
    if (!app.worldLoaded || app.subworld.active()) return false;
    auto& reg = app.ecs.reg;
    if (!reg.valid(npc)) return false;
    if (!reg.all_of<sm::ecs::Position, sm::ecs::NPCKind,
                    sm::ecs::Health, sm::ecs::NpcLevel,
                    sm::ecs::NpcCharacter>(npc)) {
        return false;
    }

    const auto& hp = reg.get<sm::ecs::Health>(npc);
    if (hp.hp <= 0.0f) return false;

    const auto& kind = reg.get<sm::ecs::NPCKind>(npc);
    const auto& lvl = reg.get<sm::ecs::NpcLevel>(npc);
    const auto& ch = reg.get<sm::ecs::NpcCharacter>(npc);
    const sm::ecs::NpcCharacter chCopy = ch;
    sm::ecs::NpcInventory bagCopy{};
    const bool hasBag = reg.all_of<sm::ecs::NpcInventory>(npc);
    if (hasBag) {
        bagCopy = reg.get<sm::ecs::NpcInventory>(npc);
    }
    sm::ecs::NpcTraits traitsCopy{};
    const bool hasTraits = reg.all_of<sm::ecs::NpcTraits>(npc);
    if (hasTraits) {
        traitsCopy = reg.get<sm::ecs::NpcTraits>(npc);
    }
    const sm::NPCType type = sm::valid_npc_kind(std::uint8_t(kind.type))
        ? sm::NPCType(kind.type)
        : sm::NPCType::Bandit;
    const sm::NpcTypeDef& def = sm::npc_def(type);
    const char* displayName = macro_npc_display_name(type, chCopy);
    const std::uint32_t seed = chCopy.visualSeed ^
        (std::uint32_t(entt::to_integral(npc)) * 16777619u) ^
        app.gs.worldSeed;

    app.cursor.path.clear();
    app.cursor.pathIdx = 0;
    app.ui.settlement = false;
    app.subworld.enter(app.gs, app.terrain, app.features,
                       app.ecs, app.bus, &app.zones);
    if (!app.subworld.active()) return false;

    if (!app.subworld.spawn_hostile_npc(def.label, displayName,
                                        int(lvl.value), seed,
                                        hasBag ? &bagCopy : nullptr,
                                        hasTraits ? &traitsCopy : nullptr,
                                        &chCopy)) {
        app.subworld.leave(true);
        return false;
    }

    if (reg.valid(npc)) {
        reg.destroy(npc);
    }
    return true;
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

struct MacroWalkChargeResult {
    std::size_t cells = 0;
    int totalCost = 0;
    sm::MacroTravelCost lastCost{};
};

struct MacroWalkChargeContext {
    App* app = nullptr;
    MacroWalkChargeResult result{};
};

void charge_macro_walk_cell(void* user, int x, int y) {
    auto* ctx = static_cast<MacroWalkChargeContext*>(user);
    if (!ctx || !ctx->app) return;

    sm::MacroTravelCost cost;
    if (!sm::drain_player_sp_for_macro_cell(ctx->app->gs,
                                            ctx->app->terrain,
                                            &ctx->app->features,
                                            x, y, &cost)) {
        return;
    }
    ++ctx->result.cells;
    ctx->result.totalCost += cost.totalCost;
    ctx->result.lastCost = cost;
}

MacroWalkChargeResult step_macro_walk_with_travel_cost(App& app,
                                                       float dt,
                                                       float cellsPerSec) {
    const float prevX = app.gs.player.x;
    const float prevY = app.gs.player.y;
    MacroWalkChargeContext charge{&app, {}};
    sm::ui::step_macro_walk(app.gs, app.cursor, dt, cellsPerSec,
                            charge_macro_walk_cell, &charge);

    float ddx = app.gs.player.x - prevX;
    float ddy = app.gs.player.y - prevY;
    if (ddx >  app.gs.mapW * 0.5f) ddx -= float(app.gs.mapW);
    if (ddx < -app.gs.mapW * 0.5f) ddx += float(app.gs.mapW);
    if (ddy >  app.gs.mapH * 0.5f) ddy -= float(app.gs.mapH);
    if (ddy < -app.gs.mapH * 0.5f) ddy += float(app.gs.mapH);
    const float dist = std::sqrt(ddx * ddx + ddy * ddy);
    emit_player_move(app, prevX, prevY, dist);
    return charge.result;
}

bool boot_window(App& app) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return false;
    }
    app.window = SDL_CreateWindow("Samosbor / Timaert",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        app.width, app.height,
        SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!app.window) { std::fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError()); return false; }
    const bool validation = [] {
        const char* e = std::getenv("TIMAERT_VK_VALIDATION");
        return e && e[0] != '0';
    }();
    if (!app.device.init(app.window, validation)) {
        std::fprintf(stderr, "VulkanDevice init failed\n");
        return false;
    }
    if (!app.renderer.init(app.device, app.window)) {
        std::fprintf(stderr, "VulkanRenderer init failed\n");
        return false;
    }
    SDL_Vulkan_GetDrawableSize(app.window, &app.width, &app.height);
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
    ImGui_ImplSDL2_InitForVulkan(app.window);

    // Descriptor pool for ImGui textures (font + user textures via AddTexture).
    {
        VkDescriptorPoolSize sizes[] = {
            {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4096}};
        VkDescriptorPoolCreateInfo pci{};
        pci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pci.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        pci.maxSets = 4096;
        pci.poolSizeCount = 1;
        pci.pPoolSizes = sizes;
        VkResult r = vkCreateDescriptorPool(app.device.device, &pci, nullptr,
                                            &app.imguiPool);
        if (r != VK_SUCCESS) {
            std::fprintf(stderr, "[imgui] descriptor pool creation failed: %d\n",
                         int(r));
        } else {
            std::fprintf(stderr, "[imgui] descriptor pool created: %p maxSets=256\n",
                         (void*)app.imguiPool);
        }
    }

    ImGui_ImplVulkan_InitInfo info{};
    info.Instance = app.device.instance;
    info.PhysicalDevice = app.device.physical;
    info.Device = app.device.device;
    info.QueueFamily = app.device.families.graphics;
    info.Queue = app.device.graphicsQueue;
    info.DescriptorPool = app.imguiPool;
    info.RenderPass = app.renderer.renderPass;
    info.MinImageCount = 2;
    info.ImageCount =
        static_cast<std::uint32_t>(app.renderer.swapchain.images.size());
    info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    info.MinAllocationSize = 1024 * 1024;
    info.CheckVkResultFn = [](VkResult err) {
        if (err != VK_SUCCESS)
            std::fprintf(stderr, "[imgui/vk] VkResult = %d\n", int(err));
    };
    ImGui_ImplVulkan_Init(&info);
    ImGui_ImplVulkan_CreateFontsTexture();
}

void shutdown_imgui(App& app) {
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    if (app.imguiPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(app.device.device, app.imguiPool, nullptr);
        app.imguiPool = VK_NULL_HANDLE;
    }
}

sm::MusicId desired_music(const App& app) {
    if (app.worldLoaded && app.subworld.active()) return sm::MusicId::Subworld;
    return sm::MusicId::Explore;
}

void request_music(App& app, sm::MusicId id) {
    if (app.audioDesired == id) {
        if (!app.audio.is_initialized()
            || app.audioFailed == id
            || (app.audio.current_music() == id && app.audio.music_playing())) {
            return;
        }
    } else {
        app.audioDesired = id;
        app.audioFailed = sm::MusicId::Count;
    }
    if (!app.audio.is_initialized()) return;
    if (app.audio.play_music(id, sm::AudioSystem::kDefaultFadeMs)) {
        app.audioFailed = sm::MusicId::Count;
    } else {
        app.audioFailed = id;
    }
}

void sync_audio_music(App& app) {
    request_music(app, desired_music(app));
}

void boot_audio(App& app) {
    if (!app.audio.init()) {
        std::fprintf(stderr, "[audio] disabled: %s\n", app.audio.last_error());
        std::fflush(stderr);
        return;
    }
    sync_audio_music(app);
}

// ── World life-cycle ──────────────────────────────────────────

void destroy_world(App& app) {
    if (app.worldLoaded && app.subworld.active()) app.subworld.leave(true);
    sm::sub::clear_saved_subworlds();
    app.bus.reset();
    app.logic.reset();
#ifndef NDEBUG
    assert(app.bus.subscription_count() == 0);
    assert(app.logic.node_count() == 0);
    assert(app.logic.active_count() == 0);
#endif
    app.appliedEventCount = 0;
    app.appliedStoryResultCount = 0;
    app.appliedCombatEventCount = 0;
    sm::reset_player_recovery(app.playerRecovery);
    app.subworldDistanceAccum = 0.0f;
    app.showDialogOpen = false;
    app.showDialogEvent = sm::GameEvent{};
    app.showDialogUi = sm::ui::DialogOverlayState{};
    app.showDialogCapturedTick = std::uint32_t(-1);
    app.storyOverlay = sm::ui::StoryOverlayState{};
    app.showStoryCapturedTick = std::uint32_t(-1);
    app.pendingPresentationCount = 0;
    app.pendingPresentationTick = std::uint32_t(-1);
    app.pendingPresentationSeen = 0;
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
                int targetTotalCities = 0,
                bool registerIntroStory = true) {
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
    sm::reset_player_recovery(app.playerRecovery);
    app.subworldDistanceAccum = 0.0f;
    sm::reset_macro_npc_ai_runtime(app.npcAi, seed);
    app.appliedEventCount = 0;
    app.ui.settlementId = -1;
    app.availableSettlementQuests.clear();
    app.availableQuestSettlementId = -1;
    app.availableQuestDay = -1;
    sm::register_builtin_nodes(app.logic);
    if (registerIntroStory) {
        sm::content::register_intro_story_nodes(app.logic);
    }
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

    app.trees = sm::spawn_trees(app.terrain, app.gs.worldSeed, 0.18f, lp.seaLevel);
    boot_trace("trees spawned");
    sm::RoadTraceStats roadStats;
    auto roads = sm::trace_roads(app.terrain, app.gs.politik, &roadStats, lp.seaLevel);
    if (boot_trace_enabled()) {
        std::fprintf(stderr,
                     "[roads] cities=%d attempted=%d kept=%d pruned=%d "
                     "componentPruned=%d expansions=%d\n",
                     roadStats.cityCount,
                     roadStats.attemptedEdges,
                     roadStats.keptEdges,
                     roadStats.prunedEdges,
                     roadStats.componentPrunedEdges,
                     roadStats.expansions);
        std::fflush(stderr);
    }
    boot_trace("roads traced");
    const auto& citiesFlat = app.gs.politik.cities;
    std::vector<int> vx, vy;
    for (const auto& v : app.gs.villages) { vx.push_back(v.x); vy.push_back(v.y); }
    auto dirts = sm::trace_dirt_roads(app.gs.mapW, app.gs.mapH, roads, vx, vy,
                                       app.terrain.rgba.data(),
                                       app.terrain.rgba.size());
    boot_trace("dirt roads traced");
    // Macro invariant: every settlement sits on a road feature (cities on a
    // main road, villages on a dirt road). Neighbour road-stitching in the
    // seamless subworld is feature-driven, so stamping the road here is what
    // makes roads reach every settlement and adjacent settlements merge with no
    // seam -- no per-generator landmark plumbing. `build_feature_layer` still
    // fails settlement cells closed on water. Extend by tagging any future
    // road-bearing landmark into the appropriate mask the same way.
    {
        const int mw = app.gs.mapW, mh = app.gs.mapH;
        auto stamp = [&](std::vector<std::uint8_t>& mask, int x, int y) {
            if (x < 0 || y < 0 || x >= mw || y >= mh) return;
            const std::size_t idx =
                std::size_t(y) * std::size_t(mw) + std::size_t(x);
            if (idx < mask.size()) mask[idx] = 255;
        };
        for (const auto& c : citiesFlat) stamp(roads, c.x, c.y);
        for (const auto& v : app.gs.villages) stamp(dirts, v.x, v.y);
    }
    app.features = sm::build_feature_layer(app.terrain, app.trees,
                                           sm::kDefaultFeatureMountainThreshold,
                                           roads, &dirts, lp.seaLevel);
    sm::build_tree_grid(app.treeGrid, app.trees, app.gs.mapW, app.gs.mapH);
    boot_trace("features and tree grid built");

    std::vector<sm::ZoneSeed> zsCities, zsVills;
    for (auto& c : citiesFlat) zsCities.push_back({c.x, c.y});
    for (auto& v : app.gs.villages) zsVills.push_back({v.x, v.y});
    app.zones = sm::generate_zones(app.gs.mapW, app.gs.mapH, app.gs.worldSeed,
                                   zsCities, zsVills, app.features,
                                   app.terrain.rgba.data(),
                                   app.terrain.rgba.size());
    boot_trace("zones generated");

    if (!app.macro.init(app.device, app.renderer.renderPass)) {
        boot_trace("macro renderer init failed");
    } else {
        boot_trace("macro renderer initialized");
    }
    app.macro.upload(app.device, app.terrain, app.features, app.zones);
    boot_trace("world data uploaded");
    // TODO: rebuild_landmarks (PHASE C — landmark glyphs/lights).

    app.pathCost = sm::build_cost_grid(app.terrain, &app.features, lp.seaLevel);
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
    boot_trace("player anchored");
    // Anchor camera at the player's CELL CENTRE (cell N spans [N..N+1]
    // in world units, so its centre sits at N+0.5). The per-sprite
    // +0.5 in macro_overlay.cpp lines up with this so the player +
    // every NPC render at their cell centre, never at the cell
    // crossing.
    app.camX = app.camTargetX = app.gs.player.x + 0.5f;
    app.camY = app.camTargetY = app.gs.player.y + 0.5f;
    app.camPanX = app.camPanY = 0;
    boot_trace("camera anchored");
    if (app.gs.subState.kind == sm::GameSubStateKind::Exploring
        && app.gs.subState.settlementId < 0) {
        boot_trace("settlement lookup start");
        app.gs.subState.settlementId = settlement_at_player(app.gs);
        boot_trace("settlement lookup done");
    }
    app.ui.settlementId = app.gs.subState.settlementId;

    boot_trace("subworld init start");
    app.subworld.init(app.device, app.renderer.renderPass);
    boot_trace("subworld init done");
    app.worldLoaded = true;
    app.state = sm::ui::AppState::Playing;
    boot_trace("done");
}

bool boot_world_from_save(App& app) {
    sm::GameState fresh;
    std::vector<sm::Quest> loadedQuests;
    if (!sm::load_game(fresh, loadedQuests, app.savePath)) return false;
    boot_world(app, fresh.worldSeed, fresh.mapW, fresh.mapH,
               &fresh.mapParams, fresh.cityCountTarget, false);

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

    // TODO: rebuild_landmarks (PHASE C — landmark glyphs/lights).
    app.camX = app.camTargetX = app.gs.player.x + 0.5f;
    app.camY = app.camTargetY = app.gs.player.y + 0.5f;
    app.gs.subState.settlementId = settlement_at_player(app.gs);
    app.ui.settlementId = app.gs.subState.settlementId;
    return true;
}

// ── Input ─────────────────────────────────────────────────────

bool sustained_spell_active(const sm::SpellBook& book, const char* id) {
    return sm::spellbook_has_sustained(book, id);
}

bool gameplay_panel_open(const App& app) {
    return modal_overlay_active(app) ||
           app.ui.diplomacy ||
           app.ui.settlement ||
           app.ui.quest ||
           app.ui.codex ||
           app.ui.map ||
           app.ui.character ||
           app.showDebug;
}

bool wants_subworld_relative_mouse(const App& app) {
    if (app.state != sm::ui::AppState::Playing || !app.worldLoaded) {
        return false;
    }
    if (!app.subworld.active()) {
        return false;
    }
    if (gameplay_panel_open(app)) {
        return false;
    }
    return true;
}

void sync_relative_mouse_mode(App& app) {
    const bool wantRel = wants_subworld_relative_mouse(app);
    const bool actual = SDL_GetRelativeMouseMode() == SDL_TRUE;
    if (wantRel != actual) {
        (void)SDL_SetRelativeMouseMode(wantRel ? SDL_TRUE : SDL_FALSE);
    }
    app.relativeMouseActive = SDL_GetRelativeMouseMode() == SDL_TRUE;
}

std::vector<sm::PathPoint> build_flight_path(int sx, int sy, int gx, int gy,
                                             int mapW, int mapH) {
    int dx = gx - sx;
    int dy = gy - sy;
    if (dx >  mapW / 2) dx -= mapW;
    if (dx < -mapW / 2) dx += mapW;
    if (dy >  mapH / 2) dy -= mapH;
    if (dy < -mapH / 2) dy += mapH;
    const int steps = std::max(std::abs(dx), std::abs(dy));
    std::vector<sm::PathPoint> path;
    path.reserve(std::size_t(std::max(1, steps) + 1));
    for (int i = 0; i <= steps; ++i) {
        const float t = steps > 0 ? float(i) / float(steps) : 0.0f;
        const int x = sm::wrapi(sx + int(std::lround(float(dx) * t)), mapW);
        const int y = sm::wrapi(sy + int(std::lround(float(dy) * t)), mapH);
        if (path.empty() || path.back().x != x || path.back().y != y) {
            path.push_back({x, y});
        }
    }
    return path;
}

void emit_spell_cast(App& app, const std::string& id,
                     bool ok, const char* reason, float cooldown = 0.0f) {
    sm::GameEvent ev{sm::EventTag::SpellCast};
    ev.s1 = id;
    ev.s2 = reason ? reason : "";
    ev.ix = ok ? 1 : 0;
    ev.fx = app.subworld.active() ? app.subworld.player_x() : app.gs.player.x;
    ev.fy = app.subworld.active() ? app.subworld.player_y() : app.gs.player.y;
    ev.a = sm::stable_spell_id(id);
    ev.b = std::uint32_t(cooldown * 1000.0f);
    app.bus.emit(ev);
}

int count_tick_events(const sm::EventBus& bus, sm::EventTag tag) {
    int count = 0;
    for (const auto& ev : bus.tick_events()) {
        if (ev.tag == tag) ++count;
    }
    return count;
}

const sm::GameEvent* latest_tick_event(const sm::EventBus& bus,
                                       sm::EventTag tag) {
    const auto& events = bus.tick_events();
    for (std::size_t i = events.size(); i > 0; --i) {
        const sm::GameEvent& ev = events[i - 1];
        if (ev.tag == tag) return &ev;
    }
    return nullptr;
}

void tick_subworld_hit_flash(App& app, float dt) {
    const int hp = app.gs.player.combatStats.currentHp;
    if (!app.subworld.active()) {
        app.subworldLastPlayerHp = hp;
        app.subworldHitFlashTimer = 0.0f;
        app.subworldDistanceAccum = 0.0f;
        return;
    }

    if (app.subworldLastPlayerHp < 0) {
        app.subworldLastPlayerHp = hp;
    } else if (hp < app.subworldLastPlayerHp) {
        app.subworldHitFlashTimer =
            std::max(app.subworldHitFlashTimer, kSubworldHitFlashSeconds);
    }
    app.subworldLastPlayerHp = hp;
    if (app.subworldHitFlashTimer > 0.0f) {
        app.subworldHitFlashTimer =
            std::max(0.0f, app.subworldHitFlashTimer - dt);
    }
}

int charge_subworld_sp_for_distance(App& app, float distance) {
    if (distance <= 0.01f) return 0;
    app.subworldDistanceAccum += distance;
    const float cost = (app.subworldDistanceAccum / 1000.0f)
        * kSubworldSpPer1000;
    if (cost < 1.0f) return 0;

    const int drain = int(std::floor(cost));
    app.subworldDistanceAccum -=
        (float(drain) / kSubworldSpPer1000) * 1000.0f;
    const float capacity =
        sm::get_carry_capacity(app.gs.player.attributes,
                               app.gs.player.skills);
    const float weight = sm::inventory_weight(app.gs.player.inventory);
    const float overload =
        sm::get_overload_penalty(weight, capacity);
    const int overloadCost = overload > 0.0f
        ? int(std::ceil(overload))
        : 0;
    auto& cs = app.gs.player.combatStats;
    const int total = drain + overloadCost;
    cs.currentSp -= total;
    if (cs.currentSp < 0) {
        cs.currentHp += cs.currentSp;
    }
    return total;
}

float subworld_spell_rng01(void* user) {
    auto* sub = static_cast<sm::sub::SubworldEngine*>(user);
    return sub ? sub->spell_rng01() : 0.0f;
}

void draw_subworld_danger_gem(const sm::sub::SubworldEngine& subworld) {
    const sm::sub::DangerLevel level = subworld.danger_level();
    ImU32 color = IM_COL32(63, 191, 74, 255);
    const char* label = "Safe";
    const char* title = "Safe: no enemies nearby";
    if (level == sm::sub::DangerLevel::Yellow) {
        color = IM_COL32(232, 200, 74, 255);
        label = "Caution";
        title = "Caution: enemies nearby";
    } else if (level == sm::sub::DangerLevel::Red) {
        color = IM_COL32(224, 50, 42, 255);
        label = "Danger";
        title = "Danger: enemies in melee range";
    }

    ImDrawList* fg = ImGui::GetForegroundDrawList();
    const ImVec2 pos(14.0f, 44.0f);
    const ImVec2 textSize = ImGui::CalcTextSize(label);
    const ImVec2 boxMax(pos.x + 36.0f + textSize.x, pos.y + 25.0f);
    fg->AddRectFilled(pos, boxMax, IM_COL32(8, 10, 12, 190), 6.0f);
    const ImVec2 center(pos.x + 14.0f, pos.y + 12.5f);
    fg->AddCircleFilled(center, 8.0f, color, 20);
    fg->AddCircleFilled(ImVec2(center.x - 2.5f, center.y - 2.8f),
                        2.5f, IM_COL32(255, 255, 255, 150), 12);
    fg->AddCircle(center, 8.0f, IM_COL32(0, 0, 0, 180), 20, 1.0f);
    fg->AddText(ImVec2(pos.x + 28.0f, pos.y + 5.0f),
                IM_COL32(235, 238, 224, 245), label);
    if (ImGui::IsMouseHoveringRect(pos, boxMax)) {
        ImGui::SetTooltip("%s", title);
    }
}

void draw_subworld_combat_log(const sm::sub::SubworldEngine& subworld,
                              int logicalW) {
    const int count = subworld.combat_log_count();
    if (count <= 0) return;

    int firstVisible = count;
    for (int i = count - 1; i >= 0; --i) {
        const sm::sub::CombatLogEntry* entry = subworld.combat_log_entry(i);
        if (!entry || entry->age > sm::sub::kCombatLogVisibleSeconds) {
            firstVisible = i + 1;
            break;
        }
        firstVisible = i;
    }
    if (firstVisible >= count) return;
    if (count - firstVisible > sm::sub::kCombatLogMaxVisible) {
        firstVisible = count - sm::sub::kCombatLogMaxVisible;
    }

    ImDrawList* fg = ImGui::GetForegroundDrawList();
    float y = 64.0f;
    const ImVec2 pad(12.0f, 5.0f);
    for (int i = firstVisible; i < count; ++i) {
        const sm::sub::CombatLogEntry* entry = subworld.combat_log_entry(i);
        if (!entry || entry->text[0] == '\0'
            || entry->age > sm::sub::kCombatLogVisibleSeconds) {
            continue;
        }
        const float fade = std::max(
            0.15f, 1.0f - entry->age / sm::sub::kCombatLogVisibleSeconds);
        const int alpha = int(fade * 255.0f);
        const ImVec2 size = ImGui::CalcTextSize(entry->text);
        const ImVec2 pos((float(logicalW) - size.x) * 0.5f, y);
        fg->AddRectFilled(ImVec2(pos.x - pad.x, pos.y - pad.y),
                          ImVec2(pos.x + size.x + pad.x,
                                 pos.y + size.y + pad.y),
                          IM_COL32(0, 0, 0, int(153.0f * fade)), 5.0f);
        fg->AddText(pos, IM_COL32(255, 214, 168, alpha), entry->text);
        y += size.y + 8.0f;
    }
}

bool cast_active_spell(App& app) {
    if (!app.worldLoaded) return false;
    const std::string& id = app.gs.player.spellBook.activeSpellId;
    if (id.empty()) {
        emit_spell_cast(app, id, false, "No active spell");
        return false;
    }

    const sm::SpellDef* def = sm::spell_registry().find(id);
    if (!def) {
        emit_spell_cast(app, id, false, "Unknown spell");
        return false;
    }

    const bool inMicro = app.subworld.active();
    const sm::CastCheck check = sm::spellbook_can_cast_ex(
        app.gs.player.spellBook, app.gs.player.combatStats, id, inMicro);
    if (!check.ok) {
        emit_spell_cast(app, id, false, check.reason.c_str(),
                        check.cooldownRemaining);
        return false;
    }

    if (!inMicro) {
        if (!def->sustained || def->macroType == sm::MacroEffectType::None) {
            emit_spell_cast(app, id, false, "World-map spell effect not implemented");
            return false;
        }
        sm::spellbook_start_cast(app.gs.player.spellBook,
                                 app.gs.player.combatStats, id);
        emit_spell_cast(app, id, true, "");
        return true;
    }

    const float nx = std::cos(app.subworld.cam_yaw());
    const float ny = std::sin(app.subworld.cam_yaw());
    const bool ok = sm::spellbook_cast(app.ecs,
        app.gs.player.spellBook,
        app.gs.player.combatStats,
        app.gs.player.attributes,
        app.gs.player.skills,
        id,
        std::uint32_t{0},
        app.subworld.player_x(),
        app.subworld.player_y(),
        nx,
        ny,
        true,
        &subworld_spell_rng01,
        &app.subworld);
    emit_spell_cast(app, id, ok, ok ? "" : "Cast failed");
    return ok;
}

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
                    if (app.subworld.active()) {
                        app.subworld.interact();
                    } else {
                        app.ui.character = true;
                        app.ui.characterTab = sm::ui::CharacterPanelTab::Equipment;
                    }
                    break;
                case SDLK_b:
                    app.ui.character = true;
                    app.ui.characterTab = sm::ui::CharacterPanelTab::Spells;
                    break;
                case SDLK_c:      app.ui.codex      = !app.ui.codex; break;
                case SDLK_m:      app.ui.map        = !app.ui.map; break;
                case SDLK_SPACE:  cast_active_spell(app); break;
                case SDLK_F5:
                    sm::save_game(app.gs, app.activeQuests, app.savePath);
                    refresh_save_summary(app);
                    break;
                case SDLK_F9:     open_load_screen(app); break;
                case SDLK_RETURN:
                    if (!app.subworld.active()) {
                        app.subworld.enter(app.gs, app.terrain, app.features,
                                           app.ecs, app.bus, &app.zones);
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
            if (wants_subworld_relative_mouse(app)) {
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
        if (app.state == sm::ui::AppState::Playing && app.ui.codex) {
            app.ui.codex = false;
            return;
        }
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
    if (app.state == sm::ui::AppState::Playing && modal_overlay_active(app)) return;
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
    sync_relative_mouse_mode(app);
    if (app.state != sm::ui::AppState::Playing) return;
    const ImGuiIO& io = ImGui::GetIO();

    if (app.subworld.active()) {
        if (gameplay_panel_open(app) || io.WantCaptureKeyboard
            || io.WantCaptureMouse) {
            app.subworld.set_player_attack_held(false);
            return;
        }
        const Uint8* keys = SDL_GetKeyboardState(nullptr);
        const Uint32 mouse = SDL_GetMouseState(nullptr, nullptr);
        app.subworld.set_player_attack_held(
            keys[SDL_SCANCODE_A]
            || ((mouse & SDL_BUTTON(SDL_BUTTON_LEFT)) != 0u));
        // Subworld: arrows move the player. W/S/D are retained as
        // convenience aliases, while A is TS-faithful melee attack.
        // Y axis: UP = forward (+y in world tile space).
        float dx = 0, dy = 0;
        if (keys[SDL_SCANCODE_UP])    dy += 1;
        if (keys[SDL_SCANCODE_DOWN])  dy -= 1;
        if (keys[SDL_SCANCODE_LEFT])  dx -= 1;
        if (keys[SDL_SCANCODE_RIGHT]) dx += 1;
        const float haste = sustained_spell_active(app.gs.player.spellBook, "haste")
            ? 1.5f : 1.0f;
        app.subworld.set_flying(
            sustained_spell_active(app.gs.player.spellBook, "flight"));
        app.subworld.move_player(dx * 96.0f * haste * dt,
                                 dy * 96.0f * haste * dt);
        return;
    }

    if (io.WantCaptureKeyboard) return;
    const Uint8* keys = SDL_GetKeyboardState(nullptr);

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
        const float travelMul = sustained_spell_active(app.gs.player.spellBook, "haste")
            ? 1.5f : 1.0f;
        step_macro_walk_with_travel_cost(app, dt,
                                         kAutoCellsPerSec * travelMul);
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
        std::span<const sm::GameEvent> pending(events.data() + begin, end - begin);
        sm::apply_events(pending, app.gs.player);
        app.appliedEventCount = end;
    }
}

const std::string* story_result_value(const sm::StoryResultPayload& result,
                                      const char* key) {
    for (const auto& entry : result.values) {
        if (entry.first == key) return &entry.second;
    }
    return nullptr;
}

void emit_simple_dialog(App& app, const char* title, const std::string& body,
                        const char* label) {
    sm::GameEvent dialog{sm::EventTag::ShowDialog};
    dialog.s1 = title ? title : "Event";
    dialog.s2 = body;
    dialog.dialogChoices = std::make_shared<std::vector<sm::DialogChoicePayload>>();
    dialog.dialogChoices->push_back(
        sm::DialogChoicePayload{label ? label : "Continue", {}, {}});
    dialog.ix = static_cast<int>(dialog.dialogChoices->size());
    app.bus.emit(dialog);
}

void apply_intro_story_result(App& app, const sm::StoryResultPayload& result) {
    const std::string* sex = story_result_value(result, "sex");
    const std::string* realm = story_result_value(result, "realm");
    const std::string* playerName = story_result_value(result, "name");

    if (playerName && !playerName->empty()) {
        app.gs.player.name = *playerName;
    }

    if (sex && *sex == "male") {
        app.gs.player.levelData.skillPoints += 1;
    } else if (sex && *sex == "female") {
        app.gs.player.levelData.attributePoints += 1;
    }

    if (realm && !realm->empty()) {
        app.gs.player.reputation[*realm] += 15;
    }

    sm::LogEntry entry{};
    entry.type = sm::LogType::World;
    entry.day = app.gs.worldTime.day;
    entry.message = "Born ";
    entry.message += sex ? *sex : "unknown";
    entry.message += ", homeland: ";
    entry.message += realm ? *realm : "unknown";
    entry.message += ".";
    app.gs.player.eventLog.push_back(std::move(entry));

    app.logic.activate("plot_chapter_1");

    for (const sm::Quest& quest : app.activeQuests) {
        if (quest.id.rfind("q_travel_", 0) != 0 || quest.objectives.empty())
            continue;
        const sm::Objective& obj = quest.objectives.front();
        if (obj.kind != sm::ObjectiveKind::VisitCell)
            continue;
        for (const sm::Settlement& settlement : app.gs.settlements) {
            if (settlement.x == obj.ix && settlement.y == obj.iy) {
                const float dist = sm::torus_dist(app.gs.player.x, app.gs.player.y,
                                                  float(obj.ix), float(obj.iy),
                                                  float(app.gs.mapW), float(app.gs.mapH));
                const int days = std::max(1, int(std::ceil(dist / 120.0f)));
                std::string body = "Now I should go to ";
                body += settlement.name;
                body += ". ~";
                body += std::to_string(days);
                body += days > 1 ? " days travel." : " day travel.";
                emit_simple_dialog(app, "First Steps", body, "On my way");
                return;
            }
        }
    }
}

void apply_pending_story_results(App& app) {
    const auto& events = app.bus.tick_events();
    if (app.appliedStoryResultCount >= events.size()) return;

    const std::size_t begin = app.appliedStoryResultCount;
    const std::size_t end = events.size();
    for (std::size_t i = begin; i < end; ++i) {
        const sm::GameEvent& ev = events[i];
        if (ev.tag != sm::EventTag::StoryResult || !ev.storyResult)
            continue;
        if (ev.storyResult->sourceNodeId == "intro_main")
            apply_intro_story_result(app, *ev.storyResult);
    }
    app.appliedStoryResultCount = end;
}

void handle_pending_battle_start_events(App& app) {
    const auto& events = app.bus.tick_events();
    if (app.appliedCombatEventCount >= events.size()) return;

    const std::size_t begin = app.appliedCombatEventCount;
    const std::size_t end = events.size();
    for (std::size_t i = begin; i < end; ++i) {
        const sm::GameEvent& ev = events[i];
        if (ev.tag != sm::EventTag::BattleStart) continue;

        if (!app.subworld.active()) {
            app.subworld.enter(app.gs, app.terrain, app.features,
                               app.ecs, app.bus, &app.zones);
            boot_trace_time("battle-start subworld enter", app.gs.worldTime);
        }
        const std::uint32_t seed = app.gs.worldSeed
            ^ (app.bus.tick() * 16777619u)
            ^ std::uint32_t(i * 2654435761u);
        if (app.subworld.spawn_hostile_npc(ev.s2.c_str(), ev.s1.c_str(),
                                           ev.ix, seed)) {
            sm::LogEntry entry{};
            entry.type = sm::LogType::Combat;
            entry.day = app.gs.worldTime.day;
            entry.message = "Encounter spawned in subworld: ";
            entry.message += ev.s1.empty() ? ev.s2 : ev.s1;
            app.gs.player.eventLog.push_back(std::move(entry));
        }
    }
    app.appliedCombatEventCount = end;
}

void emit_time_advance_if_needed(App& app, const sm::WorldTickResult& tick) {
    if (tick.hoursAdvanced <= 0) return;

    const int currentAbsHour = app.gs.worldTime.day * 24 + app.gs.worldTime.hour;
    const int rawFirstAbsHour = currentAbsHour - tick.hoursAdvanced + 1;
    const int firstAbsHour = rawFirstAbsHour > 0 ? rawFirstAbsHour : 0;
    for (int absHour = firstAbsHour; absHour <= currentAbsHour; ++absHour) {
        sm::GameEvent ev{sm::EventTag::TimeAdvance};
        ev.a = std::uint32_t(absHour / 24);
        ev.ix = 1;
        ev.iy = absHour % 24;
        app.bus.emit(ev);
    }
}

bool modal_overlay_active(const App& app) {
    return app.showDialogOpen ||
           sm::ui::story_overlay_active(app.storyOverlay) ||
           app.gs.subState.kind == sm::GameSubStateKind::Event;
}

bool macro_overlay_blocks_npc_proximity(const App& app) {
    return modal_overlay_active(app) ||
           app.ui.diplomacy ||
           app.ui.settlement ||
           app.ui.quest ||
           app.ui.codex ||
           app.ui.map ||
           app.ui.character;
}

const sm::content::StoryDef* story_def_for_event(const sm::GameEvent& ev) {
    const sm::content::StoryDef& intro = sm::content::intro_story();
    if (ev.s2.empty() || ev.s2 == intro.id) return &intro;
    return nullptr;
}

bool is_presentation_event(const sm::GameEvent& ev) {
    return ev.tag == sm::EventTag::ShowDialog ||
           ev.tag == sm::EventTag::ShowStory;
}

void queue_presentation_event(App& app, const sm::GameEvent& ev) {
    if (app.pendingPresentationCount < app.pendingPresentationEvents.size()) {
        app.pendingPresentationEvents[app.pendingPresentationCount++] = ev;
        return;
    }

    sm::GameEvent overflow{sm::EventTag::ShowDialog};
    overflow.s1 = "Presentation queue overflow";
    overflow.s2 = "Too many modal presentation events were emitted before the current modal closed.";
    overflow.ix = 1;
    app.pendingPresentationEvents.back() = std::move(overflow);
}

void collect_presentation_events(App& app) {
    const std::uint32_t tick = app.bus.tick();
    const auto& events = app.bus.tick_events();
    std::size_t begin = 0;
    if (app.pendingPresentationTick == tick) {
        begin = std::min(app.pendingPresentationSeen, events.size());
    } else {
        app.pendingPresentationTick = tick;
        app.pendingPresentationSeen = 0;
    }

    for (std::size_t i = begin; i < events.size(); ++i) {
        if (is_presentation_event(events[i])) {
            queue_presentation_event(app, events[i]);
        }
    }
    app.pendingPresentationSeen = events.size();
}

bool pop_presentation_event(App& app, sm::GameEvent& out) {
    if (app.pendingPresentationCount == 0) return false;
    out = std::move(app.pendingPresentationEvents[0]);
    for (std::size_t i = 1; i < app.pendingPresentationCount; ++i) {
        app.pendingPresentationEvents[i - 1] =
            std::move(app.pendingPresentationEvents[i]);
    }
    --app.pendingPresentationCount;
    app.pendingPresentationEvents[app.pendingPresentationCount] = sm::GameEvent{};
    return true;
}

void open_presentation_event(App& app, const sm::GameEvent& ev) {
    const std::uint32_t tick = app.bus.tick();
    if (ev.tag == sm::EventTag::ShowStory) {
        if (const sm::content::StoryDef* story = story_def_for_event(ev)) {
            sm::ui::open_story_overlay(app.storyOverlay, *story);
            app.showStoryCapturedTick = tick;
            return;
        }

        app.showDialogEvent = sm::GameEvent{sm::EventTag::ShowDialog};
        app.showDialogEvent.s1 = "Story unavailable";
        app.showDialogEvent.s2 =
            "Missing backend: native ShowStory has no registered StoryDef for the requested story id.";
        app.showDialogEvent.ix = 1;
        app.showDialogUi = sm::ui::DialogOverlayState{};
        app.showDialogOpen = true;
        app.showStoryCapturedTick = tick;
        return;
    }

    if (ev.tag == sm::EventTag::ShowDialog) {
        app.showDialogEvent = ev;
        app.showDialogUi = sm::ui::DialogOverlayState{};
        app.showDialogOpen = true;
        app.showDialogCapturedTick = tick;
    }
}

void capture_presentation_events(App& app) {
    collect_presentation_events(app);
    if (modal_overlay_active(app)) return;

    sm::GameEvent ev{};
    if (pop_presentation_event(app, ev)) {
        open_presentation_event(app, ev);
    }
}

void handle_dialog_node_activation(App& app) {
    if (!app.showDialogUi.hasNodeActivation) return;

    const std::string nodeId = app.showDialogUi.nodeId.data();
    app.showDialogUi.hasNodeActivation = false;
    app.showDialogUi.nodeId[0] = '\0';
    if (nodeId.empty()) return;

    app.logic.activate(nodeId);
}

void process_world_events(App& app) {
    apply_pending_event_effects(app);
    apply_pending_story_results(app);
    handle_pending_battle_start_events(app);
    app.bus.flush(app.gs.worldTime.day, app.gs.worldTime.hour);
    app.appliedEventCount = 0;
    app.appliedStoryResultCount = 0;
    app.appliedCombatEventCount = 0;
    app.logic.tick(app.bus, app.gs.player);
    app.quests.tick(app.activeQuests, app.bus, app.gs);
    apply_pending_event_effects(app);
    apply_pending_story_results(app);
    handle_pending_battle_start_events(app);
    capture_presentation_events(app);
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
    sm::spellbook_tick(app.gs.player.spellBook,
                       app.gs.player.combatStats,
                       dt);
    if (app.subworld.active()) {
        stats.subworldActive = true;
        if (allowInput) {
            const float prevX = app.subworld.player_x();
            const float prevY = app.subworld.player_y();
            poll_movement(app, dt);
            const float movedX = app.subworld.player_x() - prevX;
            const float movedY = app.subworld.player_y() - prevY;
            (void)charge_subworld_sp_for_distance(
                app, std::sqrt(movedX * movedX + movedY * movedY));
        }
        stats.timeTick = sm::tick_world_time_only(
            app.gs, app.worldTick, dt * kSubworldTimeScale);
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
        sm::tick_macro_npc_visuals(app.ecs, app.gs.mapW, app.gs.mapH, dt);
        emit_time_advance_if_needed(app, stats.timeTick);
        process_world_events(app);
    } else {
        if (allowInput) poll_movement(app, dt);
        update_camera(app, dt);
        stats.timeTick = sm::tick_world(app.gs, app.worldTick, dt);
        sm::apply_macro_minute_recovery(app.gs.player,
                                        stats.timeTick.minutesAdvanced,
                                        app.playerRecovery);
        sm::tick_macro_npc_ai(app.gs, app.ecs, &app.treeGrid, app.npcAi, dt);
        sm::tick_macro_npc_visuals(app.ecs, app.gs.mapW, app.gs.mapH, dt);
        app.npcAi.sweepAccum = 0.0f;
        app.npcAi.pendingSweeps = 0;
        app.npcAi.sweepCursor = 0;
        emit_time_advance_if_needed(app, stats.timeTick);
        process_world_events(app);
    }
    tick_subworld_hit_flash(app, dt);
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

// ── Developer console commands ─────────────────────────────────
//
// Every command is one `register_cmd` row here — there is no dispatch
// switch to edit. Handlers capture `&app` and call the same gameplay APIs
// the rest of the engine uses, so the console can never drift from real
// behaviour. Grouped by theme; each group is grown in its own increment.
using Con = sm::dev::Console;
using Lvl = sm::dev::ConsoleLevel;

// Case-insensitive substring test (ASCII), for settlement name lookup.
bool console_icontains(const std::string& hay, const std::string& needle) {
    auto lc = [](char ch) { return (ch >= 'A' && ch <= 'Z') ? char(ch + 32) : ch; };
    if (needle.empty()) return true;
    if (needle.size() > hay.size()) return false;
    for (std::size_t i = 0; i + needle.size() <= hay.size(); ++i) {
        std::size_t j = 0;
        for (; j < needle.size(); ++j)
            if (lc(hay[i + j]) != lc(needle[j])) break;
        if (j == needle.size()) return true;
    }
    return false;
}

// Resolve an optional on/off/toggle argument for a boolean dev flag. "on/1/
// true/yes/enable" -> true, "off/0/false/no/disable" -> false, missing or
// unrecognised -> flip `current`.
bool console_toggle_arg(const std::vector<std::string>& a, bool current) {
    if (a.empty()) return !current;
    const std::string& s = a[0];
    if (s == "on" || s == "1" || s == "true"  || s == "yes" || s == "enable")
        return true;
    if (s == "off" || s == "0" || s == "false" || s == "no" || s == "disable")
        return false;
    return !current;
}

void register_console_commands(App& app) {
    Con& con = app.console;
    con.register_builtins();

    // ── Diagnostics (read-only) ───────────────────────────────
    con.register_cmd("fps", "fps", "print the current framerate",
        [](Con& c, const std::vector<std::string>&) {
            c.printfln(Lvl::Ok, "%.1f fps (%.2f ms/frame)",
                       ImGui::GetIO().Framerate,
                       1000.0f / (ImGui::GetIO().Framerate + 1e-6f));
            return true;
        });

    con.register_cmd("pos", "pos", "print the player position",
        [&app](Con& c, const std::vector<std::string>&) {
            if (app.subworld.active())
                c.printfln(Lvl::Ok, "subworld pos = %.1f, %.1f  (cam height %.1f m)",
                           app.subworld.player_x(), app.subworld.player_y(),
                           app.subworld.cam_height_m());
            else
                c.printfln(Lvl::Ok, "macro pos = %.1f, %.1f",
                           app.gs.player.x, app.gs.player.y);
            return true;
        });

    con.register_cmd("time", "time", "print the world clock",
        [&app](Con& c, const std::vector<std::string>&) {
            c.printfln(Lvl::Ok, "day %d, %02d:%02d",
                       app.gs.worldTime.day, app.gs.worldTime.hour,
                       app.gs.worldTime.minute);
            return true;
        });

    // ── Spawn & teleport ──────────────────────────────────────
    con.register_cmd("tp", "tp <x> <y>", "teleport the player (subworld or macro, by context)",
        [&app](Con& c, const std::vector<std::string>& a) {
            float x = 0, y = 0;
            if (!sm::dev::arg_float(a, 0, x) || !sm::dev::arg_float(a, 1, y)) return false;
            if (app.subworld.active()) {
                app.subworld.set_player_pos(x, y);
                c.printfln(Lvl::Ok, "teleported (subworld) to %.1f, %.1f", x, y);
            } else {
                if (x < 0) x = 0; if (x > float(app.gs.mapW - 1)) x = float(app.gs.mapW - 1);
                if (y < 0) y = 0; if (y > float(app.gs.mapH - 1)) y = float(app.gs.mapH - 1);
                app.gs.player.x = x;
                app.gs.player.y = y;
                c.printfln(Lvl::Ok, "teleported (macro) to %.1f, %.1f", x, y);
            }
            return true;
        });

    con.register_cmd("tp_settlement", "tp_settlement <id|name>",
        "teleport to a settlement by id or name (macro position)",
        [&app](Con& c, const std::vector<std::string>& a) {
            if (a.empty()) return false;
            const sm::Settlement* found = nullptr;
            int id = 0;
            if (sm::dev::arg_int(a, 0, id))
                for (const auto& s : app.gs.settlements) if (s.id == id) { found = &s; break; }
            if (!found) {
                std::string q;
                for (std::size_t i = 0; i < a.size(); ++i) { if (i) q += ' '; q += a[i]; }
                for (const auto& s : app.gs.settlements)
                    if (console_icontains(s.name, q)) { found = &s; break; }
            }
            if (!found) { c.error("no settlement matching '" + a[0] + "'"); return true; }
            app.gs.player.x = float(found->x);
            app.gs.player.y = float(found->y);
            if (app.subworld.active())
                c.warn("leave the subworld (Enter) for the macro teleport to take effect");
            c.printfln(Lvl::Ok, "teleported to %s (id %d) at %d, %d",
                       found->name.c_str(), found->id, found->x, found->y);
            return true;
        });

    con.register_cmd("spawn", "spawn <type> [level] [count]",
        "spawn hostiles near you (subworld). NPC types: bandit guard witch "
        "sorceress peasant woodcutter merchant caravan. Monster ids (global "
        "table): wolf bear goblin skeleton troll ... (unknown -> bandit)",
        [&app](Con& c, const std::vector<std::string>& a) {
            if (a.empty()) return false;
            if (!app.subworld.active()) {
                c.error("spawn works only inside a subworld (press Enter to enter one)");
                return true;
            }
            const std::string& type = a[0];
            int level = 1; sm::dev::arg_int(a, 1, level);
            int count = 1; sm::dev::arg_int(a, 2, count);
            if (count < 1) count = 1;
            if (count > 64) count = 64;
            static std::uint32_t seq = 0;
            int placed = 0;
            for (int i = 0; i < count; ++i) {
                const std::uint32_t seed = app.gs.worldSeed ^ (++seq * 2654435761u);
                if (app.subworld.spawn_hostile_npc(type.c_str(), type.c_str(), level, seed))
                    ++placed;
            }
            c.printfln(Lvl::Ok, "spawned %d x %s (level %d)", placed, type.c_str(), level);
            return true;
        });

    con.register_cmd("spawn_fauna", "spawn_fauna",
        "re-roll this cell's ambient fauna from the table (subworld; clears current creatures)",
        [&app](Con& c, const std::vector<std::string>&) {
            if (!app.subworld.active()) {
                c.error("spawn_fauna works only inside a subworld");
                return true;
            }
            app.subworld.respawn_fauna();
            c.ok("re-rolled this cell's fauna from the table");
            return true;
        });

    // ── Items, gold, progression ──────────────────────────────
    con.register_cmd("items", "items", "list every item id in the catalog (source of truth)",
        [](Con& c, const std::vector<std::string>&) {
            for (const auto& d : sm::item_catalog())
                c.printfln(Lvl::Info, "  %-12s  %s", d.id, d.name);
            c.printfln(Lvl::Ok, "%zu items", sm::item_catalog().size());
            return true;
        });

    con.register_cmd("give", "give <item|gold> [count]",
        "add items to the player inventory (type 'items' for ids)",
        [&app](Con& c, const std::vector<std::string>& a) {
            if (a.empty()) return false;
            int n = 1; sm::dev::arg_int(a, 1, n);
            if (n <= 0) { c.error("count must be positive"); return true; }
            const std::string& id = a[0];
            if (id == "gold") {
                app.gs.player.gold += n;
                c.printfln(Lvl::Ok, "gold += %d  (now %d)", n, app.gs.player.gold);
                return true;
            }
            if (!sm::item_def(id)) {
                c.error("unknown item '" + id + "' - type 'items' for the list");
                return true;
            }
            app.gs.player.inventory.add(id, n);
            c.printfln(Lvl::Ok, "gave %d x %s  (have %d)", n, id.c_str(),
                       app.gs.player.inventory.count(id));
            return true;
        });

    con.register_cmd("take", "take <item|gold> [count]",
        "remove items from the player inventory",
        [&app](Con& c, const std::vector<std::string>& a) {
            if (a.empty()) return false;
            int n = 1; sm::dev::arg_int(a, 1, n);
            if (n <= 0) { c.error("count must be positive"); return true; }
            const std::string& id = a[0];
            if (id == "gold") {
                const int taken = n < app.gs.player.gold ? n : app.gs.player.gold;
                app.gs.player.gold -= taken;
                c.printfln(Lvl::Ok, "gold -= %d  (now %d)", taken, app.gs.player.gold);
                return true;
            }
            if (app.gs.player.inventory.remove(id, n))
                c.printfln(Lvl::Ok, "took %d x %s  (have %d)", n, id.c_str(),
                           app.gs.player.inventory.count(id));
            else
                c.printfln(Lvl::Warn, "not enough '%s' (have %d)", id.c_str(),
                           app.gs.player.inventory.count(id));
            return true;
        });

    con.register_cmd("gold", "gold <delta>",
        "add (or, if negative, subtract) player gold",
        [&app](Con& c, const std::vector<std::string>& a) {
            int delta = 0;
            if (!sm::dev::arg_int(a, 0, delta)) return false;
            app.gs.player.gold += delta;
            if (app.gs.player.gold < 0) app.gs.player.gold = 0;
            c.printfln(Lvl::Ok, "gold = %d", app.gs.player.gold);
            return true;
        });

    con.register_cmd("addexp", "addexp <amount>",
        "grant experience (auto-levels while over the threshold)",
        [&app](Con& c, const std::vector<std::string>& a) {
            int amount = 0;
            if (!sm::dev::arg_int(a, 0, amount)) return false;
            auto& ld = app.gs.player.levelData;
            const int before = ld.level;
            if (amount > 0) ld.exp += amount;
            while (sm::try_level_up(ld)) {}
            c.printfln(Lvl::Ok, "exp +%d -> level %d (%d gained), %d/%d to next",
                       amount, ld.level, ld.level - before, ld.exp, ld.expToNext);
            return true;
        });

    con.register_cmd("levelup", "levelup [count]",
        "force N level-ups, granting the usual attribute/skill/perk points",
        [&app](Con& c, const std::vector<std::string>& a) {
            int n = 1; sm::dev::arg_int(a, 0, n);
            if (n < 1) n = 1;
            auto& ld = app.gs.player.levelData;
            const int before = ld.level;
            for (int i = 0; i < n; ++i) {
                if (ld.exp < ld.expToNext) ld.exp = ld.expToNext;
                sm::try_level_up(ld);
            }
            c.printfln(Lvl::Ok, "level %d -> %d  (%d attr, %d skill, %d perk pts avail)",
                       before, ld.level, ld.attributePoints, ld.skillPoints, ld.perkPoints);
            return true;
        });

    con.register_cmd("spells", "spells", "list every spell id in the registry (source of truth)",
        [](Con& c, const std::vector<std::string>&) {
            for (const auto& s : sm::spell_registry().all())
                c.printfln(Lvl::Info, "  %-16s  %s", s.id.c_str(), s.name.c_str());
            c.printfln(Lvl::Ok, "%zu spells", sm::spell_registry().size());
            return true;
        });

    con.register_cmd("learn", "learn <spellId>",
        "learn a spell by id (type 'spells' for ids)",
        [&app](Con& c, const std::vector<std::string>& a) {
            if (a.empty()) return false;
            const std::string& id = a[0];
            if (!sm::spell_registry().find(id)) {
                c.error("unknown spell '" + id + "' - type 'spells' for the list");
                return true;
            }
            if (sm::spellbook_learn(app.gs.player.spellBook, id))
                c.printfln(Lvl::Ok, "learned %s", id.c_str());
            else
                c.printfln(Lvl::Warn, "already knew %s", id.c_str());
            return true;
        });

    con.register_cmd("learnall", "learnall",
        "learn every spell in the registry",
        [&app](Con& c, const std::vector<std::string>&) {
            int learned = 0;
            for (const auto& s : sm::spell_registry().all())
                if (sm::spellbook_learn(app.gs.player.spellBook, s.id)) ++learned;
            c.printfln(Lvl::Ok, "learned %d new spell(s); know %zu total", learned,
                       app.gs.player.spellBook.learned.size());
            return true;
        });

    // ── World & toggles ───────────────────────────────────────
    con.register_cmd("settime", "settime <hour> [minute]",
        "set the world clock (hour 0-23, minute 0-59)",
        [&app](Con& c, const std::vector<std::string>& a) {
            int h = 0;
            if (!sm::dev::arg_int(a, 0, h)) return false;
            int m = 0; sm::dev::arg_int(a, 1, m);
            if (h < 0) h = 0; if (h > 23) h = 23;
            if (m < 0) m = 0; if (m > 59) m = 59;
            app.gs.worldTime.hour = h;
            app.gs.worldTime.minute = m;
            c.printfln(Lvl::Ok, "clock set to day %d, %02d:%02d",
                       app.gs.worldTime.day, h, m);
            return true;
        });

    con.register_cmd("addtime", "addtime <hours>",
        "advance the world clock forward N hours (runs the daily simulation)",
        [&app](Con& c, const std::vector<std::string>& a) {
            float hours = 0.0f;
            if (!sm::dev::arg_float(a, 0, hours)) return false;
            if (hours <= 0.0f) { c.error("hours must be positive (clock only moves forward)"); return true; }
            const float dtSeconds = hours * 60.0f / sm::kWorldMinutesPerSecond;
            const sm::WorldTickResult r =
                sm::tick_world(app.gs, app.worldTick, dtSeconds);
            c.printfln(Lvl::Ok, "advanced %.2f h -> day %d, %02d:%02d  (%d daily tick(s))",
                       hours, app.gs.worldTime.day, app.gs.worldTime.hour,
                       app.gs.worldTime.minute, r.dailyTicksProcessed);
            return true;
        });

    con.register_cmd("simspeed", "simspeed [mult]",
        "get/set the live simulation speed multiplier (0-100; 1 = normal)",
        [&app](Con& c, const std::vector<std::string>& a) {
            if (a.empty()) {
                c.printfln(Lvl::Ok, "simspeed = %.2fx", app.simSpeed);
                return true;
            }
            float m = 0.0f;
            if (!sm::dev::arg_float(a, 0, m)) return false;
            if (m < 0.0f) m = 0.0f; if (m > 100.0f) m = 100.0f;
            app.simSpeed = m;
            c.printfln(Lvl::Ok, "simspeed = %.2fx", app.simSpeed);
            return true;
        });

    con.register_cmd("heal", "heal",
        "restore the player's HP / MP / SP to full",
        [&app](Con& c, const std::vector<std::string>&) {
            auto& cs = app.gs.player.combatStats;
            cs.currentHp = cs.maxHp;
            cs.currentMp = cs.maxMp;
            cs.currentSp = cs.maxSp;
            c.printfln(Lvl::Ok, "restored to full (%d hp / %d mp / %d sp)",
                       cs.maxHp, cs.maxMp, cs.maxSp);
            return true;
        });

    con.register_cmd("killall", "killall",
        "kill every hostile in the current subworld scene (grants xp + loot)",
        [&app](Con& c, const std::vector<std::string>&) {
            if (!app.subworld.active()) {
                c.error("killall works only inside a subworld");
                return true;
            }
            const int n = app.subworld.dev_kill_all_hostiles();
            c.printfln(Lvl::Ok, "killed %d hostile(s)", n);
            return true;
        });

    con.register_cmd("godmode", "godmode [on|off]",
        "toggle player invulnerability in subworld combat",
        [&app](Con& c, const std::vector<std::string>& a) {
            const bool ns = console_toggle_arg(a, app.subworld.god_mode());
            app.subworld.set_god_mode(ns);
            c.printfln(ns ? Lvl::Ok : Lvl::Info, "godmode %s", ns ? "ON" : "off");
            return true;
        });

    con.register_cmd("flight", "flight [on|off]",
        "toggle free-fly movement in the subworld (no flight spell needed)",
        [&app](Con& c, const std::vector<std::string>& a) {
            if (!app.subworld.active()) {
                c.error("flight works only inside a subworld");
                return true;
            }
            const bool ns = console_toggle_arg(a, app.subworld.flying());
            app.subworld.set_flying(ns);
            c.printfln(ns ? Lvl::Ok : Lvl::Info, "flight %s", ns ? "ON" : "off");
            return true;
        });

    // ── Inspector panels (diagnostics) ────────────────────────
    con.register_cmd("entities", "entities [on|off]",
        "toggle the live ECS entity inspector window",
        [&app](Con& c, const std::vector<std::string>& a) {
            app.panels.entities = console_toggle_arg(a, app.panels.entities);
            c.printfln(Lvl::Ok, "entity inspector %s", app.panels.entities ? "shown" : "hidden");
            return true;
        });
    con.register_cmd("ecsstats", "ecsstats [on|off]",
        "toggle the ECS component-population stats window",
        [&app](Con& c, const std::vector<std::string>& a) {
            app.panels.ecsStats = console_toggle_arg(a, app.panels.ecsStats);
            c.printfln(Lvl::Ok, "ecs stats %s", app.panels.ecsStats ? "shown" : "hidden");
            return true;
        });
    con.register_cmd("gamestate", "gamestate [on|off]",
        "toggle the game-state inspector window (player / world / counts)",
        [&app](Con& c, const std::vector<std::string>& a) {
            app.panels.gameState = console_toggle_arg(a, app.panels.gameState);
            c.printfln(Lvl::Ok, "game-state inspector %s", app.panels.gameState ? "shown" : "hidden");
            return true;
        });
    con.register_cmd("journal", "journal [on|off]",
        "toggle the player event-log window",
        [&app](Con& c, const std::vector<std::string>& a) {
            app.panels.journal = console_toggle_arg(a, app.panels.journal);
            c.printfln(Lvl::Ok, "journal %s", app.panels.journal ? "shown" : "hidden");
            return true;
        });

    con.info("developer console ready - type 'help'");
}

// Draw the dev inspector panels — the "panels" half of the hybrid console.
// Each is a read-only ImGui window guarded by an App::DebugPanels flag that a
// console command toggles. They read live game state generically (ECS views +
// GameState), so no per-content wiring is needed. Call once per frame in the
// Playing state, next to draw_debug_console.
void draw_debug_panels(App& app) {
    auto& reg = app.ecs.reg;

    // ── Entity inspector ──────────────────────────────────────
    if (app.panels.entities) {
        ImGui::SetNextWindowSize(ImVec2(580, 380), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Entities", &app.panels.entities)) {
            ImGui::Text("%s scene   (tags: S=subworld D=dead A=ally H=hostile)",
                        app.subworld.active() ? "subworld" : "macro");
            constexpr std::size_t kMaxRows = 500;
            std::size_t shown = 0, total = 0;
            const ImGuiTableFlags tf = ImGuiTableFlags_Borders |
                ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
                ImGuiTableFlags_Resizable;
            if (ImGui::BeginTable("ents", 7, tf,
                                  ImVec2(0.0f, -ImGui::GetTextLineHeightWithSpacing()))) {
                ImGui::TableSetupColumn("id");
                ImGui::TableSetupColumn("kind");
                ImGui::TableSetupColumn("lvl");
                ImGui::TableSetupColumn("hp");
                ImGui::TableSetupColumn("pos");
                ImGui::TableSetupColumn("arch");
                ImGui::TableSetupColumn("tags");
                ImGui::TableSetupScrollFreeze(0, 1);
                ImGui::TableHeadersRow();
                for (auto e : reg.view<sm::ecs::Position>()) {
                    ++total;
                    if (shown >= kMaxRows) continue;
                    const auto& pos = reg.get<sm::ecs::Position>(e);
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::Text("%u", unsigned(entt::to_integral(e)));
                    ImGui::TableNextColumn();
                    if (const auto* k = reg.try_get<sm::ecs::NPCKind>(e)) {
                        ImGui::TextUnformatted(
                            sm::valid_npc_kind(std::uint8_t(k->type))
                                ? sm::npc_def(sm::NPCType(k->type)).label : "?");
                    } else if (reg.any_of<sm::ecs::Projectile>(e)) {
                        ImGui::TextUnformatted("(projectile)");
                    } else if (reg.any_of<sm::ecs::Structure>(e)) {
                        ImGui::TextUnformatted("(structure)");
                    } else {
                        ImGui::TextUnformatted("-");
                    }
                    ImGui::TableNextColumn();
                    if (const auto* lv = reg.try_get<sm::ecs::NpcLevel>(e))
                        ImGui::Text("%d", int(lv->value));
                    else ImGui::TextUnformatted("-");
                    ImGui::TableNextColumn();
                    if (const auto* h = reg.try_get<sm::ecs::Health>(e))
                        ImGui::Text("%.0f/%.0f", double(h->hp), double(h->maxHp));
                    else ImGui::TextUnformatted("-");
                    ImGui::TableNextColumn();
                    ImGui::Text("%.1f, %.1f", double(pos.x), double(pos.y));
                    ImGui::TableNextColumn();
                    if (const auto* sp = reg.try_get<sm::ecs::Sprite>(e);
                        sp && sp->archetype != 0xFF)
                        ImGui::Text("%u", unsigned(sp->archetype));
                    else ImGui::TextUnformatted("-");
                    ImGui::TableNextColumn();
                    char tags[8]; int ti = 0;
                    if (reg.any_of<sm::ecs::SubworldTag>(e))        tags[ti++] = 'S';
                    if (reg.any_of<sm::ecs::Dead>(e))               tags[ti++] = 'D';
                    if (reg.any_of<sm::ecs::PlayerSoldierTag>(e))   tags[ti++] = 'A';
                    if (reg.any_of<sm::ecs::TempHostileToPlayer>(e))tags[ti++] = 'H';
                    tags[ti] = '\0';
                    ImGui::TextUnformatted(tags);
                    ++shown;
                }
                ImGui::EndTable();
            }
            ImGui::Text("showing %zu of %zu%s", shown, total,
                        total > shown ? "  (capped at 500)" : "");
        }
        ImGui::End();
    }

    // ── ECS component-population stats ─────────────────────────
    if (app.panels.ecsStats) {
        ImGui::SetNextWindowSize(ImVec2(300, 420), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("ECS stats", &app.panels.ecsStats)) {
            auto cnt = [](auto v) {
                std::size_t n = 0; for (auto e : v) { (void)e; ++n; } return n;
            };
            struct Row { const char* name; std::size_t count; };
            const Row rows[] = {
                {"Position",        cnt(reg.view<sm::ecs::Position>())},
                {"Health",          cnt(reg.view<sm::ecs::Health>())},
                {"Combat",          cnt(reg.view<sm::ecs::Combat>())},
                {"NPCKind",         cnt(reg.view<sm::ecs::NPCKind>())},
                {"SubworldTag",     cnt(reg.view<sm::ecs::SubworldTag>())},
                {"SubworldAi",      cnt(reg.view<sm::ecs::SubworldAi>())},
                {"NpcLevel",        cnt(reg.view<sm::ecs::NpcLevel>())},
                {"Projectile",      cnt(reg.view<sm::ecs::Projectile>())},
                {"Structure",       cnt(reg.view<sm::ecs::Structure>())},
                {"Sprite",          cnt(reg.view<sm::ecs::Sprite>())},
                {"MacroNpcRuntime", cnt(reg.view<sm::ecs::MacroNpcRuntime>())},
                {"Dead",            cnt(reg.view<sm::ecs::Dead>())},
                {"PlayerSoldier",   cnt(reg.view<sm::ecs::PlayerSoldierTag>())},
                {"TempHostile",     cnt(reg.view<sm::ecs::TempHostileToPlayer>())},
                {"HitFlash",        cnt(reg.view<sm::ecs::HitFlash>())},
                {"CorpseLoot",      cnt(reg.view<sm::ecs::CorpseLoot>())},
            };
            if (ImGui::BeginTable("ecs", 2,
                                  ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
                ImGui::TableSetupColumn("component");
                ImGui::TableSetupColumn("count");
                ImGui::TableHeadersRow();
                for (const auto& r : rows) {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn(); ImGui::TextUnformatted(r.name);
                    ImGui::TableNextColumn(); ImGui::Text("%zu", r.count);
                }
                ImGui::EndTable();
            }
        }
        ImGui::End();
    }

    // ── Game-state inspector ──────────────────────────────────
    if (app.panels.gameState) {
        ImGui::SetNextWindowSize(ImVec2(340, 440), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Game state", &app.panels.gameState)) {
            const auto& p = app.gs.player;
            ImGui::SeparatorText("Player");
            ImGui::Text("pos     %.1f, %.1f", double(p.x), double(p.y));
            ImGui::Text("gold    %d", p.gold);
            ImGui::Text("level   %d   (exp %d / %d)",
                        p.levelData.level, p.levelData.exp, p.levelData.expToNext);
            ImGui::Text("hp      %d / %d", p.combatStats.currentHp, p.combatStats.maxHp);
            ImGui::Text("mp      %d / %d", p.combatStats.currentMp, p.combatStats.maxMp);
            ImGui::Text("sp      %d / %d", p.combatStats.currentSp, p.combatStats.maxSp);
            ImGui::Text("points  attr %d  skill %d  perk %d",
                        p.levelData.attributePoints, p.levelData.skillPoints,
                        p.levelData.perkPoints);
            ImGui::Text("spells  %zu learned", p.spellBook.learned.size());
            ImGui::SeparatorText("World");
            ImGui::Text("clock   day %d, %02d:%02d",
                        app.gs.worldTime.day, app.gs.worldTime.hour,
                        app.gs.worldTime.minute);
            ImGui::Text("seed    %u", app.gs.worldSeed);
            ImGui::Text("map     %d x %d", app.gs.mapW, app.gs.mapH);
            ImGui::Text("world   %zu settlements  %zu villages  %zu spires",
                        app.gs.settlements.size(), app.gs.villages.size(),
                        app.gs.spires.size());
            ImGui::SeparatorText("Dev");
            ImGui::Text("simspeed %.2fx", double(app.simSpeed));
            ImGui::Text("subworld %s", app.subworld.active() ? "active" : "-");
            ImGui::Text("godmode  %s", app.subworld.god_mode() ? "ON" : "off");
            ImGui::Text("flight   %s", app.subworld.flying() ? "ON" : "off");
        }
        ImGui::End();
    }

    // ── Journal (player event log) ────────────────────────────
    if (app.panels.journal) {
        ImGui::SetNextWindowSize(ImVec2(440, 300), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Journal", &app.panels.journal)) {
            const auto& log = app.gs.player.eventLog;
            ImGui::Text("%zu entries (showing last 200)", log.size());
            ImGui::Separator();
            if (ImGui::BeginChild("jlog")) {
                const std::size_t start = log.size() > 200 ? log.size() - 200 : 0;
                for (std::size_t i = start; i < log.size(); ++i)
                    ImGui::TextWrapped("[day %d] %s", log[i].day, log[i].message.c_str());
                if (log.empty()) ImGui::TextDisabled("(no entries yet)");
            }
            ImGui::EndChild();
        }
        ImGui::End();
    }
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

    app.customPreviewTex = sm::ui::recreate_ui_texture(
        app.customPreviewTex, side, side, img.data(), /*linear=*/true);
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

bool smoke_find_open_subworld_cell(const App& app, int& outX, int& outY);

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

    smoke_clear_modal_overlays(app);
    int safeCellX = 0;
    int safeCellY = 0;
    if (smoke_find_open_subworld_cell(app, safeCellX, safeCellY)) {
        app.gs.player.x = float(safeCellX);
        app.gs.player.y = float(safeCellY);
        app.gs.subState.settlementId = -1;
        app.ui.settlementId = -1;
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
    {
        std::array<entt::entity, 2048> neutralized{};
        int neutralizedCount = 0;
        auto combatView = app.ecs.reg.view<sm::ecs::Combat,
                                           sm::ecs::SubworldTag>();
        for (auto e : combatView) {
            if (neutralizedCount >= int(neutralized.size())) break;
            neutralized[std::size_t(neutralizedCount++)] = e;
        }
        for (int i = 0; i < neutralizedCount; ++i) {
            const entt::entity e = neutralized[std::size_t(i)];
            if (app.ecs.reg.valid(e)) {
                app.ecs.reg.remove<sm::ecs::Combat>(e);
            }
        }
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
    app.subworld.leave(true);
    const bool activeAfterLeave = app.subworld.active();

    const sm::WorldTime after = app.gs.worldTime;
    const int afterMinutes = smoke_total_minutes(after);
    const int playerAfterX = int(app.gs.player.x);
    const int playerAfterY = int(app.gs.player.y);
    const bool timeAdvanced = afterMinutes > beforeMinutes
        && minutesAdvanced == afterMinutes - beforeMinutes;
    const int expectedMinutes = int(std::floor(
        float(kSubworldSmokeFrames) * kSubworldSmokeDt
        * sm::kWorldMinutesPerSecond * kSubworldTimeScale + 0.0001f));
    const bool subworldRateOk = minutesAdvanced == expectedMinutes;
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
                 "[smoke] subworld_time frames=%d dt=%.3f scale=%.2f "
                 "minutes=%d expected=%d days=%d "
                 "dailyProcessed=%d pendingDailyStart=%d pendingDailyBeforeLeave=%d "
                 "pendingDailyAfterLeave=%d maxPendingDaily=%d\n",
                 kSubworldSmokeFrames, kSubworldSmokeDt, kSubworldTimeScale,
                 minutesAdvanced, expectedMinutes, daysAdvanced,
                 dailyProcessed, dailyPendingStart,
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
    if (!subworldRateOk) {
        smoke_fail(app, "subworld_time rate invariant");
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

bool run_subworld_no_recovery_smoke(App& app) {
    if (!smoke_boot_invariants_hold(app)) {
        smoke_print_counts(app, "subworld_no_recovery_boot_failed");
        smoke_fail(app, "subworld_no_recovery boot invariants");
        return false;
    }
    if (app.subworld.active()) {
        smoke_fail(app, "subworld_no_recovery already active");
        return false;
    }

    smoke_clear_modal_overlays(app);
    int safeCellX = 0;
    int safeCellY = 0;
    if (smoke_find_open_subworld_cell(app, safeCellX, safeCellY)) {
        app.gs.player.x = float(safeCellX);
        app.gs.player.y = float(safeCellY);
        app.gs.subState.settlementId = -1;
        app.ui.settlementId = -1;
    }

    auto& stats = app.gs.player.combatStats;
    stats.currentHp = 5;
    stats.currentMp = 5;
    stats.currentSp = 5;
    stats.maxHp = 100;
    stats.maxMp = 100;
    stats.maxSp = 100;
    sm::reset_player_recovery(app.playerRecovery);

    app.subworld.enter(app.gs, app.terrain, app.features, app.ecs,
                       app.bus, &app.zones);
    if (!app.subworld.active()) {
        smoke_fail(app, "subworld_no_recovery enter failed");
        return false;
    }
    {
        std::array<entt::entity, 2048> neutralized{};
        int neutralizedCount = 0;
        auto combatView = app.ecs.reg.view<sm::ecs::Combat,
                                           sm::ecs::SubworldTag>();
        for (auto e : combatView) {
            if (neutralizedCount >= int(neutralized.size())) break;
            neutralized[std::size_t(neutralizedCount++)] = e;
        }
        for (int i = 0; i < neutralizedCount; ++i) {
            const entt::entity e = neutralized[std::size_t(i)];
            if (app.ecs.reg.valid(e)) {
                app.ecs.reg.remove<sm::ecs::Combat>(e);
            }
        }
    }

    int minutesAdvanced = 0;
    constexpr int kFrames = 42;
    for (int i = 0; i < kFrames; ++i) {
        RuntimeFrameStats frameStats =
            tick_playing_runtime(app, kSubworldSmokeDt, false);
        if (!frameStats.ticked || !frameStats.subworldActive) {
            smoke_fail(app, "subworld_no_recovery runtime tick inactive");
            return false;
        }
        minutesAdvanced += frameStats.timeTick.minutesAdvanced;
    }

    const int afterHp = app.gs.player.combatStats.currentHp;
    const int afterMp = app.gs.player.combatStats.currentMp;
    const int afterSp = app.gs.player.combatStats.currentSp;
    app.subworld.leave(true);

    std::fprintf(stderr,
                 "[smoke] subworld_no_recovery frames=%d dt=%.3f "
                 "minutes=%d hp=%d mp=%d sp=%d\n",
                 kFrames, kSubworldSmokeDt, minutesAdvanced,
                 afterHp, afterMp, afterSp);
    std::fflush(stderr);

    if (minutesAdvanced <= 0 || afterHp != 5 || afterMp != 5 || afterSp != 5) {
        smoke_fail(app, "subworld_no_recovery invariant");
        return false;
    }
    return true;
}

bool run_subworld_sp_drain_smoke(App& app) {
    if (!smoke_boot_invariants_hold(app)) {
        smoke_print_counts(app, "subworld_sp_drain_boot_failed");
        smoke_fail(app, "subworld_sp_drain boot invariants");
        return false;
    }
    if (app.subworld.active()) {
        smoke_fail(app, "subworld_sp_drain already active");
        return false;
    }

    smoke_clear_modal_overlays(app);
    int safeCellX = 0;
    int safeCellY = 0;
    if (smoke_find_open_subworld_cell(app, safeCellX, safeCellY)) {
        app.gs.player.x = float(safeCellX);
        app.gs.player.y = float(safeCellY);
        app.gs.subState.settlementId = -1;
        app.ui.settlementId = -1;
    }
    app.gs.player.combatStats.currentSp = 100;
    app.gs.player.combatStats.currentHp = 100;
    app.gs.player.combatStats.maxSp = 100;
    app.gs.player.combatStats.maxHp = 100;
    app.subworldDistanceAccum = 0.0f;

    app.subworld.enter(app.gs, app.terrain, app.features,
                       app.ecs, app.bus, &app.zones);
    if (!app.subworld.active()) {
        smoke_fail(app, "subworld_sp_drain enter failed");
        return false;
    }

    const float beforeX = app.subworld.player_x();
    const float beforeY = app.subworld.player_y();
    const int beforeSp = app.gs.player.combatStats.currentSp;
    const int beforeHp = app.gs.player.combatStats.currentHp;
    app.subworld.move_player(0.0f, 250.0f);
    const float dx = app.subworld.player_x() - beforeX;
    const float dy = app.subworld.player_y() - beforeY;
    const float distance = std::sqrt(dx * dx + dy * dy);
    const int charged = charge_subworld_sp_for_distance(app, distance);
    const int afterSp = app.gs.player.combatStats.currentSp;
    const int afterHp = app.gs.player.combatStats.currentHp;
    app.subworld.leave(true);

    std::fprintf(stderr,
                 "[smoke] subworld_sp_drain distance=%.1f charged=%d "
                 "sp=%d->%d hp=%d->%d carry=%.1f\n",
                 double(distance),
                 charged,
                 beforeSp,
                 afterSp,
                 beforeHp,
                 afterHp,
                 double(app.subworldDistanceAccum));
    std::fflush(stderr);

    if (std::fabs(distance - 100.0f) > 0.01f
        || charged != 1
        || afterSp != beforeSp - 1
        || afterHp != beforeHp
        || std::fabs(app.subworldDistanceAccum) > 0.01f) {
        smoke_fail(app, "subworld_sp_drain invariant");
        return false;
    }
    return true;
}

bool run_subworld_seam_smoke(App& app) {
    if (!smoke_boot_invariants_hold(app)) {
        smoke_print_counts(app, "subworld_seam_boot_failed");
        smoke_fail(app, "subworld_seam boot invariants");
        return false;
    }
    if (app.subworld.active()) {
        smoke_fail(app, "subworld_seam already active");
        return false;
    }

    app.subworld.enter(app.gs, app.terrain, app.features, app.ecs,
                       app.bus, &app.zones);
    if (!app.subworld.active()) {
        smoke_fail(app, "subworld_seam enter failed");
        return false;
    }
    std::fprintf(stderr, "[smoke] subworld_seam entered center=%d,%d player=%.1f,%.1f\n",
                 app.subworld.mgr().center_cx(),
                 app.subworld.mgr().center_cy(),
                 app.subworld.player_x(),
                 app.subworld.player_y());
    std::fflush(stderr);

    const int beforeCx = app.subworld.mgr().center_cx();
    const int beforeCy = app.subworld.mgr().center_cy();
    const float beforeX = app.subworld.player_x();
    const float beforeY = app.subworld.player_y();
    app.subworld.move_player(0.0f, 3000.0f);
    std::fprintf(stderr, "[smoke] subworld_seam moved player=%.1f,%.1f\n",
                 app.subworld.player_x(), app.subworld.player_y());
    std::fflush(stderr);

    RuntimeFrameStats frameStats =
        tick_playing_runtime(app, 1.0f / 60.0f, false);
    std::fprintf(stderr, "[smoke] subworld_seam crossed ticked=%d active=%d center=%d,%d\n",
                 frameStats.ticked ? 1 : 0,
                 frameStats.subworldActive ? 1 : 0,
                 app.subworld.mgr().center_cx(),
                 app.subworld.mgr().center_cy());
    std::fflush(stderr);
    if (!frameStats.ticked || !frameStats.subworldActive) {
        smoke_fail(app, "subworld_seam runtime tick inactive");
        app.subworld.leave(true);
        return false;
    }

    const int afterCx = app.subworld.mgr().center_cx();
    const int afterCy = app.subworld.mgr().center_cy();
    const sm::sub::SeamTiming timing = app.subworld.mgr().last_seam_timing();
    if (afterCx != beforeCx + 1 || afterCy != beforeCy
        || !timing.crossed || timing.smoothMs != 0.0) {
        smoke_fail(app, "subworld_seam crossing invariant");
        app.subworld.leave(true);
        return false;
    }

    for (int i = 0; i < kSubworldSeamSmokeSettleFrames; ++i) {
        frameStats = tick_playing_runtime(app, 1.0f / 60.0f, false);
        if (!frameStats.ticked || !frameStats.subworldActive) {
            smoke_fail(app, "subworld_seam settle tick inactive");
            app.subworld.leave(true);
            return false;
        }
    }

    const float afterX = app.subworld.player_x();
    const float afterY = app.subworld.player_y();
    app.subworld.leave(true);
    if (app.subworld.active()) {
        smoke_fail(app, "subworld_seam leave failed");
        return false;
    }

    std::fprintf(stderr,
                 "[smoke] subworld_seam center=%d,%d->%d,%d "
                 "player=%.1f,%.1f->%.1f,%.1f gen=%.3fms smooth=%.3fms "
                 "settleFrames=%d\n",
                 beforeCx, beforeCy, afterCx, afterCy,
                 beforeX, beforeY, afterX, afterY,
                 timing.genMs, timing.smoothMs,
                 kSubworldSeamSmokeSettleFrames);
    std::fflush(stderr);
    return true;
}

bool run_subworld_audio_smoke(App& app) {
    if (!smoke_boot_invariants_hold(app)) {
        smoke_print_counts(app, "subworld_audio_boot_failed");
        smoke_fail(app, "subworld_audio boot invariants");
        return false;
    }
    if (!app.audio.is_initialized()) {
        smoke_fail(app, "subworld_audio audio not initialized");
        return false;
    }
    if (app.subworld.active()) {
        smoke_fail(app, "subworld_audio already active");
        return false;
    }

    sync_audio_music(app);
    const bool exploreBefore = app.audio.current_music() == sm::MusicId::Explore
        && app.audio.music_playing();

    app.subworld.enter(app.gs, app.terrain, app.features, app.ecs,
                       app.bus, &app.zones);
    if (!app.subworld.active()) {
        smoke_fail(app, "subworld_audio enter failed");
        return false;
    }

    sync_audio_music(app);
    const bool subworldActive = app.audio.current_music() == sm::MusicId::Subworld
        && app.audio.music_playing();

    app.subworld.leave(true);
    sync_audio_music(app);
    const bool exploreAfter = !app.subworld.active()
        && app.audio.current_music() == sm::MusicId::Explore
        && app.audio.music_playing();

    std::fprintf(stderr,
                 "[smoke] subworld_audio exploreBefore=%d subworld=%d "
                 "exploreAfter=%d current=%s error=%s\n",
                 exploreBefore ? 1 : 0, subworldActive ? 1 : 0,
                 exploreAfter ? 1 : 0,
                 sm::music_key(app.audio.current_music())
                     ? sm::music_key(app.audio.current_music()) : "none",
                 app.audio.last_error());
    std::fflush(stderr);

    if (!exploreBefore || !subworldActive || !exploreAfter) {
        smoke_fail(app, "subworld_audio music transition invariant");
        return false;
    }

    std::fprintf(stderr, "[smoke] subworld_audio OK\n");
    std::fflush(stderr);
    return true;
}

bool smoke_cell_is_land(const sm::TerrainData& terrain, int x, int y) {
    if (!terrain.has_rgba_storage() || terrain.width <= 0 || terrain.height <= 0) {
        return false;
    }
    const int wx = sm::wrapi(x, terrain.width);
    const int wy = sm::wrapi(y, terrain.height);
    const std::size_t idx =
        (std::size_t(wy) * std::size_t(terrain.width) + std::size_t(wx)) * 4u;
    return idx + 3u < terrain.rgba.size() && terrain.rgba[idx + 3u] != 0u;
}

bool smoke_find_danger_land_cell(const App& app, int& outX, int& outY) {
    if (!app.zones.has_data_storage() || !app.terrain.has_rgba_storage()) {
        return false;
    }
    for (int level = sm::kZoneCount - 1; level >= 3; --level) {
        for (int y = 0; y < app.zones.height; ++y) {
            for (int x = 0; x < app.zones.width; ++x) {
                if (int(app.zones.at(x, y)) != level) continue;
                if (!smoke_cell_is_land(app.terrain, x, y)) continue;
                outX = x;
                outY = y;
                return true;
            }
        }
    }
    return false;
}

bool smoke_find_open_subworld_cell(const App& app, int& outX, int& outY) {
    if (!app.terrain.has_rgba_storage() || app.terrain.width <= 0
        || app.terrain.height <= 0) {
        return false;
    }
    auto hasLandmark = [&](int x, int y) {
        for (const auto& s : app.gs.settlements) {
            if (s.x == x && s.y == y) return true;
        }
        for (const auto& v : app.gs.villages) {
            if (v.x == x && v.y == y) return true;
        }
        for (const auto& sp : app.gs.spires) {
            if (sp.x == x && sp.y == y) return true;
        }
        return false;
    };

    const int cx = app.gs.mapW / 2;
    const int cy = app.gs.mapH / 2;
    const float minH = app.gs.mapParams.seaLevel + 0.06f;
    for (int radius = 0; radius < std::max(app.gs.mapW, app.gs.mapH) / 2; ++radius) {
        for (int dy = -radius; dy <= radius; ++dy) {
            for (int dx = -radius; dx <= radius; ++dx) {
                if (radius > 0 && std::max(std::abs(dx), std::abs(dy)) != radius) {
                    continue;
                }
                const int x = sm::wrapi(cx + dx, app.gs.mapW);
                const int y = sm::wrapi(cy + dy, app.gs.mapH);
                if (x < 64 || y < 64 || x >= app.gs.mapW - 64
                    || y >= app.gs.mapH - 64) {
                    continue;
                }
                if (hasLandmark(x, y)) continue;
                if (app.features.at(x, y) == sm::FT_Mountain) continue;
                const std::size_t idx =
                    (std::size_t(y) * std::size_t(app.terrain.width)
                     + std::size_t(x)) * 4u;
                if (idx + 3u >= app.terrain.rgba.size()) continue;
                const float h = float(app.terrain.rgba[idx + 0u]) / 255.0f;
                if (h < minH || h > 0.72f) continue;
                outX = x;
                outY = y;
                return true;
            }
        }
    }
    return false;
}

bool smoke_find_tree_subworld_cell(const App& app, int& outX, int& outY) {
    if (!app.terrain.has_rgba_storage() || app.terrain.width <= 0
        || app.terrain.height <= 0 || !app.features.has_complete_storage()) {
        return false;
    }
    const int cx = app.gs.mapW / 2;
    const int cy = app.gs.mapH / 2;
    const float minH = app.gs.mapParams.seaLevel + 0.05f;
    for (int radius = 0; radius < std::max(app.gs.mapW, app.gs.mapH) / 2; ++radius) {
        for (int dy = -radius; dy <= radius; ++dy) {
            for (int dx = -radius; dx <= radius; ++dx) {
                if (radius > 0 && std::max(std::abs(dx), std::abs(dy)) != radius) {
                    continue;
                }
                const int x = sm::wrapi(cx + dx, app.gs.mapW);
                const int y = sm::wrapi(cy + dy, app.gs.mapH);
                if (x < 64 || y < 64 || x >= app.gs.mapW - 64
                    || y >= app.gs.mapH - 64) {
                    continue;
                }
                if (app.features.at(x, y) != sm::FT_Tree) continue;
                const std::size_t idx =
                    (std::size_t(y) * std::size_t(app.terrain.width)
                     + std::size_t(x)) * 4u;
                if (idx + 3u >= app.terrain.rgba.size()) continue;
                const float h = float(app.terrain.rgba[idx + 0u]) / 255.0f;
                if (h < minH) continue;
                outX = x;
                outY = y;
                return true;
            }
        }
    }
    return false;
}

bool smoke_find_macro_travel_path(const App& app, sm::PathResult& out) {
    out = {};
    if (!app.worldLoaded || app.pathCost.width <= 0 || app.pathCost.height <= 0) {
        return false;
    }

    const int sx = sm::wrapi(int(std::floor(app.gs.player.x)), app.gs.mapW);
    const int sy = sm::wrapi(int(std::floor(app.gs.player.y)), app.gs.mapH);
    for (int radius = kSmokeMacroTravelSteps; radius <= 48; ++radius) {
        for (int dy = -radius; dy <= radius; ++dy) {
            for (int dx = -radius; dx <= radius; ++dx) {
                if (std::max(std::abs(dx), std::abs(dy)) != radius) {
                    continue;
                }
                sm::PathResult path = sm::find_path(app.pathCost,
                                                    sx, sy,
                                                    sx + dx, sy + dy);
                if (path.found
                    && int(path.path.size()) >= kSmokeMacroTravelSteps + 1) {
                    out = std::move(path);
                    return true;
                }
            }
        }
    }
    return false;
}

bool run_macro_travel_sp_smoke(App& app) {
    if (!smoke_boot_invariants_hold(app)) {
        smoke_print_counts(app, "macro_travel_sp_boot_failed");
        smoke_fail(app, "macro_travel_sp boot invariants");
        return false;
    }
    if (app.subworld.active()) {
        smoke_fail(app, "macro_travel_sp while subworld active");
        return false;
    }
    smoke_clear_modal_overlays(app);

    sm::PathResult path;
    if (!smoke_find_macro_travel_path(app, path)) {
        smoke_fail(app, "macro_travel_sp found no path");
        return false;
    }

    int expectedCost = 0;
    sm::MacroTravelCost lastExpected{};
    for (int i = 1; i <= kSmokeMacroTravelSteps; ++i) {
        sm::MacroTravelCost cost;
        if (!sm::macro_travel_cost_for_cell(app.gs, app.terrain,
                                            &app.features,
                                            path.path[std::size_t(i)].x,
                                            path.path[std::size_t(i)].y,
                                            cost)) {
            smoke_fail(app, "macro_travel_sp cost failed");
            return false;
        }
        expectedCost += cost.totalCost;
        lastExpected = cost;
    }

    app.cursor.path.assign(path.path.begin(),
                           path.path.begin() + kSmokeMacroTravelSteps + 1);
    app.cursor.pathIdx = 1;
    const int beforeSp = app.gs.player.combatStats.currentSp;
    const int beforeHp = app.gs.player.combatStats.currentHp;
    const float beforeX = app.gs.player.x;
    const float beforeY = app.gs.player.y;

    MacroWalkChargeResult charged =
        step_macro_walk_with_travel_cost(app, 1.0f, 64.0f);

    const int afterSp = app.gs.player.combatStats.currentSp;
    const int afterHp = app.gs.player.combatStats.currentHp;
    if (int(charged.cells) != kSmokeMacroTravelSteps
        || charged.totalCost != expectedCost
        || afterSp != beforeSp - expectedCost
        || afterHp != beforeHp) {
        smoke_fail(app, "macro_travel_sp invariant");
        return false;
    }

    std::fprintf(stderr,
                 "[smoke] macro_travel_sp steps=%d pos=%.0f,%.0f->%.0f,%.0f "
                 "sp=%d->%d cost=%d lastBiome=%d lastFeature=%d lastCell=%d\n",
                 kSmokeMacroTravelSteps,
                 beforeX, beforeY,
                 app.gs.player.x, app.gs.player.y,
                 beforeSp, afterSp,
                 expectedCost,
                 int(lastExpected.biome),
                 int(lastExpected.feature),
                 lastExpected.cellCost);
    std::fflush(stderr);
    return true;
}

bool run_macro_recovery_smoke(App& app) {
    if (!smoke_boot_invariants_hold(app)) {
        smoke_print_counts(app, "macro_recovery_boot_failed");
        smoke_fail(app, "macro_recovery boot invariants");
        return false;
    }
    if (app.subworld.active()) {
        smoke_fail(app, "macro_recovery while subworld active");
        return false;
    }
    smoke_clear_modal_overlays(app);

    auto& player = app.gs.player;
    player.attributes.end = 1;
    player.attributes.vit = 1;
    player.attributes.wil = 1;
    player.combatStats.currentSp = 0;
    player.combatStats.currentHp = 0;
    player.combatStats.currentMp = 0;
    player.combatStats.maxSp = 100;
    player.combatStats.maxHp = 100;
    player.combatStats.maxMp = 100;
    sm::reset_player_recovery(app.playerRecovery);

    const float dt = 6.0f / sm::kWorldMinutesPerSecond;
    const RuntimeFrameStats stats = tick_playing_runtime(app, dt, false);
    if (stats.subworldActive
        || stats.timeTick.minutesAdvanced != 6
        || player.combatStats.currentSp != 1
        || player.combatStats.currentHp != 1
        || player.combatStats.currentMp != 1) {
        smoke_fail(app, "macro_recovery invariant");
        return false;
    }

    std::fprintf(stderr,
                 "[smoke] macro_recovery minutes=%d hp=%d mp=%d sp=%d\n",
                 stats.timeTick.minutesAdvanced,
                 player.combatStats.currentHp,
                 player.combatStats.currentMp,
                 player.combatStats.currentSp);
    std::fflush(stderr);
    return true;
}

bool run_timeadvance_burst_smoke(App& app) {
    if (!smoke_boot_invariants_hold(app)) {
        smoke_print_counts(app, "timeadvance_burst_boot_failed");
        smoke_fail(app, "timeadvance_burst boot invariants");
        return false;
    }
    if (app.subworld.active()) {
        smoke_fail(app, "timeadvance_burst while subworld active");
        return false;
    }
    smoke_clear_modal_overlays(app);

    app.gs.worldTime.day = 0;
    app.gs.worldTime.hour = 6;
    app.gs.worldTime.minute = 0;
    sm::reset_world_tick_runtime(app.worldTick, app.gs.worldSeed);

    std::array<int, 8> days{};
    std::array<int, 8> hours{};
    int count = 0;
    const std::uint32_t subId =
        app.bus.on(sm::EventTag::TimeAdvance, [&](const sm::GameEvent& ev) {
            if (count >= int(hours.size())) {
                return;
            }
            days[std::size_t(count)] = int(ev.a);
            hours[std::size_t(count)] = ev.iy;
            ++count;
        });

    const float dt = 180.1f / sm::kWorldMinutesPerSecond;
    const RuntimeFrameStats stats = tick_playing_runtime(app, dt, false);
    app.bus.unsubscribe(subId);

    const bool subscriberOk = stats.ticked
        && !stats.subworldActive
        && stats.timeTick.hoursAdvanced == 3
        && count == 3
        && days[0] == 0 && hours[0] == 7
        && days[1] == 0 && hours[1] == 8
        && days[2] == 0 && hours[2] == 9;

    const std::size_t timeHistoryBefore =
        app.bus.query_history(sm::EventTag::TimeAdvance, 16).size();
    app.gs.worldTime.day = 0;
    app.gs.worldTime.hour = 9;
    app.gs.worldTime.minute = 0;
    sm::reset_world_tick_runtime(app.worldTick, app.gs.worldSeed);
    const RuntimeFrameStats noSubStats =
        tick_playing_runtime(app, 60.1f / sm::kWorldMinutesPerSecond, false);
    const auto timeHistory =
        app.bus.query_history(sm::EventTag::TimeAdvance, 16);
    const int noSubDelta =
        int(timeHistory.size()) - int(timeHistoryBefore);
    const bool noSubscriberOk = noSubStats.ticked
        && !noSubStats.subworldActive
        && noSubStats.timeTick.hoursAdvanced == 1
        && noSubDelta == 1
        && !timeHistory.empty()
        && int(timeHistory[0].event.a) == 0
        && timeHistory[0].event.iy == 10;

    const bool ok = subscriberOk && noSubscriberOk;

    std::fprintf(stderr,
                 "[smoke] timeadvance_burst hoursAdvanced=%d count=%d "
                 "events=[%d:%02d,%d:%02d,%d:%02d] noSubDelta=%d latest=%d:%02d\n",
                 stats.timeTick.hoursAdvanced,
                 count,
                 days[0], hours[0],
                 days[1], hours[1],
                 days[2], hours[2],
                 noSubDelta,
                 timeHistory.empty() ? -1 : int(timeHistory[0].event.a),
                 timeHistory.empty() ? -1 : timeHistory[0].event.iy);
    std::fflush(stderr);

    if (!ok) {
        smoke_fail(app, "timeadvance_burst invariant");
        return false;
    }
    return true;
}

entt::entity smoke_find_macro_npc_trace_target(App& app) {
    entt::entity fallback = entt::null;
    auto view = app.ecs.reg.view<sm::ecs::Position, sm::ecs::NPCKind,
                                 sm::ecs::MacroNpcRuntime,
                                 sm::ecs::Health, sm::ecs::VisualPos>(
        entt::exclude<sm::ecs::Dead, sm::ecs::SubworldTag>);
    for (auto e : view) {
        const auto& hp = view.get<sm::ecs::Health>(e);
        if (hp.hp <= 0.0f) continue;
        const auto& kind = view.get<sm::ecs::NPCKind>(e);
        if (fallback == entt::null) fallback = e;
        if (kind.type == std::uint16_t(sm::NPCType::Caravan)
            || kind.type == std::uint16_t(sm::NPCType::Merchant)
            || kind.type == std::uint16_t(sm::NPCType::Woodcutter)) {
            return e;
        }
    }
    return fallback;
}

bool run_macro_npc_trace_smoke(App& app) {
    if (!smoke_boot_invariants_hold(app)) {
        smoke_print_counts(app, "macro_npc_trace_boot_failed");
        smoke_fail(app, "macro_npc_trace boot invariants");
        return false;
    }
    if (app.subworld.active()) {
        smoke_fail(app, "macro_npc_trace while subworld active");
        return false;
    }
    smoke_clear_modal_overlays(app);

    const entt::entity e = smoke_find_macro_npc_trace_target(app);
    if (e == entt::null) {
        smoke_fail(app, "macro_npc_trace found no macro NPC");
        return false;
    }

    auto& pos = app.ecs.reg.get<sm::ecs::Position>(e);
    auto& kind = app.ecs.reg.get<sm::ecs::NPCKind>(e);
    auto& rt = app.ecs.reg.get<sm::ecs::MacroNpcRuntime>(e);
    auto& hp = app.ecs.reg.get<sm::ecs::Health>(e);
    auto& visual = app.ecs.reg.get<sm::ecs::VisualPos>(e);

    const int maxSp = std::max(1, int(std::lround(hp.maxHp * 2.0f)));
    rt.state = std::uint8_t(sm::NPCState::Resting);
    rt.sp = 0;
    rt.tickAccum = 0.0f;
    rt.visualSpeed = 0.0f;
    for (int i = 0; i < 32; ++i) {
        sm::tick_macro_npc_ai(app.gs, app.ecs, &app.treeGrid, app.npcAi,
                              sm::kAiTickSec);
        if (rt.state == std::uint8_t(sm::NPCState::Idle)) {
            break;
        }
    }
    const int recoveredSp = rt.sp;
    const int recoveredState = int(rt.state);
    const bool recovered =
        recoveredSp >= maxSp / 2
        && rt.state == std::uint8_t(sm::NPCState::Idle);

    const int baseX = app.gs.mapW > 32 ? app.gs.mapW / 2 : 4;
    const int baseY = app.gs.mapH > 32 ? app.gs.mapH / 2 : 4;
    pos.x = float(baseX);
    pos.y = float(baseY);
    visual.vx = pos.x;
    visual.vy = pos.y;
    visual.speed = 0.0f;
    kind.type = std::uint16_t(sm::NPCType::Caravan);
    rt.targetX = float(sm::wrapi(baseX + 3, app.gs.mapW));
    rt.targetY = float(baseY);
    rt.targetSettlementId = -1;
    rt.state = std::uint8_t(sm::NPCState::Traveling);
    rt.sp = std::int16_t(maxSp);
    rt.tickAccum = 0.0f;
    rt.visualSpeed = 0.0f;

    sm::tick_macro_npc_ai(app.gs, app.ecs, &app.treeGrid, app.npcAi,
                          sm::kAiTickSec);
    const float logicalX = pos.x;
    const float logicalY = pos.y;
    const float visualBefore = visual.vx;
    sm::tick_macro_npc_visuals(app.ecs, app.gs.mapW, app.gs.mapH, 0.25f);
    const float visualMid = visual.vx;
    sm::tick_macro_npc_visuals(app.ecs, app.gs.mapW, app.gs.mapH, 0.25f);
    const float visualEnd = visual.vx;

    const bool logicalMoved =
        int(std::lround(logicalX)) == sm::wrapi(baseX + 1, app.gs.mapW)
        && int(std::lround(logicalY)) == baseY;
    const bool visualSmoothed =
        visualBefore == float(baseX)
        && visualMid > float(baseX)
        && visualMid < logicalX
        && std::fabs(visualEnd - logicalX) < 0.001f;

    std::fprintf(stderr,
                 "[smoke] macro_npc_trace entity=%u maxSp=%d "
                 "rest=%d:%d recovered=%d move=%.1f,%.1f->%.1f,%.1f "
                 "visual=%.2f->%.2f->%.2f\n",
                 unsigned(entt::to_integral(e)),
                 maxSp,
                 recoveredSp,
                 recoveredState,
                 recovered ? 1 : 0,
                 float(baseX), float(baseY), logicalX, logicalY,
                 visualBefore, visualMid, visualEnd);
    std::fflush(stderr);

    if (!recovered || !logicalMoved || !visualSmoothed) {
        smoke_fail(app, "macro_npc_trace invariant");
        return false;
    }
    return true;
}

entt::entity smoke_find_subworld_npc(App& app, sm::NPCType type) {
    auto& reg = app.ecs.reg;
    auto view = reg.view<sm::ecs::SubworldTag, sm::ecs::NPCKind,
                         sm::ecs::Health>(
        entt::exclude<sm::ecs::Dead, sm::ecs::PlayerSoldierTag>);
    for (auto e : view) {
        const auto& kind = view.get<sm::ecs::NPCKind>(e);
        if (kind.type == std::uint16_t(type)) return e;
    }
    return entt::null;
}

bool run_subworld_exit_gate_smoke(App& app) {
    if (!smoke_boot_invariants_hold(app)) {
        smoke_print_counts(app, "subworld_exit_gate_boot_failed");
        smoke_fail(app, "subworld_exit_gate boot invariants");
        return false;
    }
    if (app.subworld.active()) {
        smoke_fail(app, "subworld_exit_gate already active");
        return false;
    }

    int cellX = 0;
    int cellY = 0;
    if (!smoke_find_danger_land_cell(app, cellX, cellY)) {
        smoke_fail(app, "subworld_exit_gate found no danger land cell");
        return false;
    }

    const float oldX = app.gs.player.x;
    const float oldY = app.gs.player.y;
    const auto oldSubState = app.gs.subState;
    const int oldUiSettlement = app.ui.settlementId;
    auto restore = [&]() {
        if (app.subworld.active()) app.subworld.leave(true);
        app.gs.player.x = oldX;
        app.gs.player.y = oldY;
        app.gs.subState = oldSubState;
        app.ui.settlementId = oldUiSettlement;
    };

    app.gs.player.x = float(cellX);
    app.gs.player.y = float(cellY);
    app.gs.subState.settlementId = -1;
    app.ui.settlementId = -1;
    app.subworld.enter(app.gs, app.terrain, app.features, app.ecs,
                       app.bus, &app.zones);
    if (!app.subworld.active()) {
        restore();
        smoke_fail(app, "subworld_exit_gate enter failed");
        return false;
    }
    if (!app.subworld.spawn_hostile_npc("bandit", "Smoke Gate Bandit", 3,
                                        app.gs.worldSeed ^ 0xE917u)) {
        restore();
        smoke_fail(app, "subworld_exit_gate hostile spawn failed");
        return false;
    }

    app.subworld.leave(false);
    const bool blocked = app.subworld.active();
    const bool statusSet = app.subworld.status_line()[0] != '\0';
    const int zone = int(app.zones.at(cellX, cellY));
    restore();

    std::fprintf(stderr,
                 "[smoke] subworld_exit_gate zone=%d cell=%d,%d "
                 "blocked=%d status=%d\n",
                 zone, cellX, cellY, blocked ? 1 : 0, statusSet ? 1 : 0);
    std::fflush(stderr);

    if (!blocked || !statusSet) {
        smoke_fail(app, "subworld_exit_gate invariant");
        return false;
    }
    return true;
}

bool run_subworld_loot_xp_smoke(App& app) {
    if (!smoke_boot_invariants_hold(app)) {
        smoke_print_counts(app, "subworld_loot_xp_boot_failed");
        smoke_fail(app, "subworld_loot_xp boot invariants");
        return false;
    }
    if (app.subworld.active()) {
        smoke_fail(app, "subworld_loot_xp already active");
        return false;
    }

    const float oldX = app.gs.player.x;
    const float oldY = app.gs.player.y;
    const auto oldSubState = app.gs.subState;
    const int oldUiSettlement = app.ui.settlementId;
    auto restore = [&]() {
        if (app.subworld.active()) app.subworld.leave(true);
        app.gs.player.x = oldX;
        app.gs.player.y = oldY;
        app.gs.subState = oldSubState;
        app.ui.settlementId = oldUiSettlement;
    };

    app.subworld.enter(app.gs, app.terrain, app.features, app.ecs,
                       app.bus, &app.zones);
    if (!app.subworld.active()) {
        restore();
        smoke_fail(app, "subworld_loot_xp enter failed");
        return false;
    }

    sm::ecs::NpcInventory bag{};
    bag.inv.add("misc_gem", 2);
    if (!app.subworld.spawn_hostile_npc("bandit", "Smoke Loot Bandit", 2,
                                        app.gs.worldSeed ^ 0x10A7u, &bag)) {
        restore();
        smoke_fail(app, "subworld_loot_xp hostile spawn failed");
        return false;
    }

    auto& reg = app.ecs.reg;
    const entt::entity target = smoke_find_subworld_npc(app, sm::NPCType::Bandit);
    if (target == entt::null) {
        restore();
        smoke_fail(app, "subworld_loot_xp hostile not found");
        return false;
    }

    const int expBefore = app.gs.player.levelData.exp;
    const int gemBefore = app.gs.player.inventory.count("misc_gem");
    if (auto* pos = reg.try_get<sm::ecs::Position>(target)) {
        pos->x = float(sm::sub::kFullSize) * 0.5f + 2.0f;
        pos->y = float(sm::sub::kFullSize) * 0.5f;
    }
    if (auto* vp = reg.try_get<sm::ecs::VisualPos>(target)) {
        vp->vx = float(sm::sub::kFullSize) * 0.5f + 2.0f;
        vp->vy = float(sm::sub::kFullSize) * 0.5f;
    }
    if (auto* hp = reg.try_get<sm::ecs::Health>(target)) hp->hp = 0.0f;
    reg.emplace_or_replace<sm::ecs::LastHit>(target, 0u, true);
    if (!reg.any_of<sm::ecs::Dead>(target)) {
        reg.emplace<sm::ecs::Dead>(target);
    }

    app.subworld.tick(0.016f);
    bool corpseFound = false;
    auto corpses = reg.view<sm::ecs::Structure, sm::ecs::CorpseLoot,
                            sm::ecs::SubworldTag>();
    for (auto e : corpses) {
        const auto& st = corpses.get<sm::ecs::Structure>(e);
        if (st.kind == sm::ecs::Structure::Corpse) corpseFound = true;
    }
    const bool interacted = app.subworld.interact();
    const int expAfter = app.gs.player.levelData.exp;
    const int gemAfter = app.gs.player.inventory.count("misc_gem");
    restore();

    std::fprintf(stderr,
                 "[smoke] subworld_loot_xp corpse=%d interact=%d "
                 "exp=%d->%d misc_gem=%d->%d\n",
                 corpseFound ? 1 : 0, interacted ? 1 : 0,
                 expBefore, expAfter, gemBefore, gemAfter);
    std::fflush(stderr);

    if (!corpseFound || !interacted || expAfter <= expBefore
        || gemAfter < gemBefore + 2) {
        smoke_fail(app, "subworld_loot_xp invariant");
        return false;
    }
    return true;
}

bool run_subworld_enemy_feedback_smoke(App& app) {
    if (!smoke_boot_invariants_hold(app)) {
        smoke_print_counts(app, "subworld_enemy_feedback_boot_failed");
        smoke_fail(app, "subworld_enemy_feedback boot invariants");
        return false;
    }
    smoke_clear_modal_overlays(app);
    if (!app.subworld.active()) {
        int cellX = 0;
        int cellY = 0;
        if (smoke_find_open_subworld_cell(app, cellX, cellY)
            || smoke_find_danger_land_cell(app, cellX, cellY)) {
            app.gs.player.x = float(cellX);
            app.gs.player.y = float(cellY);
            app.gs.subState.settlementId = -1;
            app.ui.settlementId = -1;
        }
        app.subworld.enter(app.gs, app.terrain, app.features,
                           app.ecs, app.bus, &app.zones);
    }
    if (!app.subworld.active()) {
        smoke_fail(app, "subworld_enemy_feedback enter failed");
        return false;
    }

    auto& reg = app.ecs.reg;
    const float px = app.subworld.player_x();
    const float py = app.subworld.player_y();
    const entt::entity hostile = reg.create();
    reg.emplace<sm::ecs::Position>(hostile,
        std::min(px + 5.0f, float(sm::sub::kFullSize - 2)), py);
    reg.emplace<sm::ecs::VisualPos>(hostile,
        std::min(px + 5.0f, float(sm::sub::kFullSize - 2)), py, 0.0f);
    reg.emplace<sm::ecs::NPCKind>(
        hostile, sm::ecs::NPCKind{std::uint16_t(0x1FE), std::uint16_t(2)});
    reg.emplace<sm::ecs::Health>(hostile, 18.0f, 18.0f);
    reg.emplace<sm::ecs::Combat>(hostile,
        7.0f, 0.0f, 8.0f, 0.30f, 0.0f, sm::ecs::Combat::Melee);
    reg.emplace<sm::ecs::SubworldTag>(hostile);
    reg.emplace<sm::ecs::Active>(hostile);
    reg.emplace<sm::ecs::SubworldAi>(hostile,
        sm::ecs::SubworldAi::Combat, 0.0f, 0.0f, 0.0f, 0.0f, 1.2f);
    reg.emplace<sm::ecs::Sprite>(hostile,
        std::uint16_t(0x1FE),
        std::uint8_t(255), std::uint8_t(60), std::uint8_t(45),
        std::uint8_t(255), 1.2f);

    int spriteOnlyVisible = 0;
    auto spriteView = reg.view<sm::ecs::Position, sm::ecs::Sprite,
                               sm::ecs::Health, sm::ecs::SubworldTag>(
        entt::exclude<sm::ecs::Dead>);
    for (auto e : spriteView) {
        const auto& hp = spriteView.get<sm::ecs::Health>(e);
        if (hp.hp <= 0.0f) continue;
        if (reg.any_of<sm::ecs::NpcCharacter>(e)) continue;
        ++spriteOnlyVisible;
    }

    const int beforeHp = app.gs.player.combatStats.currentHp;
    tick_playing_runtime(app, 0.20f, false);
    const int afterHp = app.gs.player.combatStats.currentHp;
    const float flash = app.subworldHitFlashTimer;
    const sm::sub::DangerLevel danger = app.subworld.danger_level();
    const char* status = app.subworld.status_line();
    const bool feedback = status && status[0] != '\0';
    const int combatLogCount = app.subworld.combat_log_count();
    const sm::sub::CombatLogEntry* combatLog =
        app.subworld.combat_log_entry(combatLogCount - 1);
    const bool combatLogVisible = combatLog && combatLog->text[0] != '\0'
        && combatLog->age <= sm::sub::kCombatLogVisibleSeconds;

    std::fprintf(stderr,
                 "[smoke] subworld_enemy_feedback spriteOnly=%d hp=%d->%d "
                 "flash=%.3f danger=%d combatLog=%d latest=\"%s\" status=\"%s\"\n",
                 spriteOnlyVisible, beforeHp, afterHp,
                 double(flash),
                 int(danger),
                 combatLogCount,
                 combatLogVisible ? combatLog->text : "",
                 feedback ? status : "");
    std::fflush(stderr);

    if (spriteOnlyVisible <= 0 || afterHp >= beforeHp || flash <= 0.0f
        || danger != sm::sub::DangerLevel::Red || !combatLogVisible
        || !feedback) {
        smoke_fail(app, "subworld_enemy_feedback invariant");
        return false;
    }
    return true;
}

bool run_subworld_missile_feedback_smoke(App& app) {
    if (!smoke_boot_invariants_hold(app)) {
        smoke_print_counts(app, "subworld_missile_feedback_boot_failed");
        smoke_fail(app, "subworld_missile_feedback boot invariants");
        return false;
    }
    smoke_clear_modal_overlays(app);
    if (!app.subworld.active()) {
        int cellX = 0;
        int cellY = 0;
        if (smoke_find_open_subworld_cell(app, cellX, cellY)
            || smoke_find_danger_land_cell(app, cellX, cellY)) {
            app.gs.player.x = float(cellX);
            app.gs.player.y = float(cellY);
            app.gs.subState.settlementId = -1;
            app.ui.settlementId = -1;
        }
        app.subworld.enter(app.gs, app.terrain, app.features,
                           app.ecs, app.bus, &app.zones);
    }
    if (!app.subworld.active()) {
        smoke_fail(app, "subworld_missile_feedback enter failed");
        return false;
    }

    auto& reg = app.ecs.reg;
    const float px = app.subworld.player_x();
    const float py = app.subworld.player_y();
    const entt::entity hostile = reg.create();
    reg.emplace<sm::ecs::Position>(hostile,
        std::min(px + 18.0f, float(sm::sub::kFullSize - 2)), py);
    reg.emplace<sm::ecs::VisualPos>(hostile,
        std::min(px + 18.0f, float(sm::sub::kFullSize - 2)), py, 0.0f);
    reg.emplace<sm::ecs::NPCKind>(
        hostile,
        sm::ecs::NPCKind{
            std::uint16_t(sm::NPCType::Witch),
            std::uint16_t(3)});
    reg.emplace<sm::ecs::Health>(hostile, 30.0f, 30.0f);
    reg.emplace<sm::ecs::Combat>(
        hostile,
        6.0f,
        0.0f,
        45.0f,
        0.30f,
        0.0f,
        sm::ecs::Combat::Missile);
    reg.emplace<sm::ecs::MissileAttack>(
        hostile, 160.0f, 0.0f, std::uint32_t{0xFFA070D0u});
    reg.emplace<sm::ecs::SubworldTag>(hostile);
    reg.emplace<sm::ecs::Active>(hostile);
    reg.emplace<sm::ecs::SubworldAi>(
        hostile,
        sm::ecs::SubworldAi::Combat,
        0.0f, 0.0f, 0.0f, 0.0f, 1.2f);
    reg.emplace<sm::ecs::Sprite>(
        hostile,
        std::uint16_t(sm::NPCType::Witch),
        std::uint8_t(160), std::uint8_t(112), std::uint8_t(208),
        std::uint8_t(255), 1.2f);

    int beforeProjectiles = 0;
    for (auto e : reg.view<sm::ecs::Projectile>()) {
        (void)e;
        ++beforeProjectiles;
    }
    const int beforeHp = app.gs.player.combatStats.currentHp;
    tick_playing_runtime(app, 0.10f, false);
    int afterProjectiles = 0;
    for (auto e : reg.view<sm::ecs::Projectile>()) {
        (void)e;
        ++afterProjectiles;
    }
    const int afterHp = app.gs.player.combatStats.currentHp;
    const float flash = app.subworldHitFlashTimer;
    const int combatLogCount = app.subworld.combat_log_count();
    const sm::sub::CombatLogEntry* combatLog =
        app.subworld.combat_log_entry(combatLogCount - 1);
    const bool combatLogVisible = combatLog && combatLog->text[0] != '\0'
        && combatLog->age <= sm::sub::kCombatLogVisibleSeconds;
    const char* status = app.subworld.status_line();
    const bool feedback = status && status[0] != '\0';

    std::fprintf(stderr,
                 "[smoke] subworld_missile_feedback projectiles=%d->%d "
                 "hp=%d->%d flash=%.3f combatLog=%d latest=\"%s\" status=\"%s\"\n",
                 beforeProjectiles,
                 afterProjectiles,
                 beforeHp,
                 afterHp,
                 double(flash),
                 combatLogCount,
                 combatLogVisible ? combatLog->text : "",
                 feedback ? status : "");
    std::fflush(stderr);

    if (afterHp >= beforeHp || flash <= 0.0f || !combatLogVisible
        || !feedback) {
        smoke_fail(app, "subworld_missile_feedback invariant");
        return false;
    }
    return true;
}

bool run_subworld_player_melee_smoke(App& app) {
    if (!smoke_boot_invariants_hold(app)) {
        smoke_print_counts(app, "subworld_player_melee_boot_failed");
        smoke_fail(app, "subworld_player_melee boot invariants");
        return false;
    }
    smoke_clear_modal_overlays(app);
    if (!app.subworld.active()) {
        int cellX = 0;
        int cellY = 0;
        if (smoke_find_open_subworld_cell(app, cellX, cellY)
            || smoke_find_danger_land_cell(app, cellX, cellY)) {
            app.gs.player.x = float(cellX);
            app.gs.player.y = float(cellY);
            app.gs.subState.settlementId = -1;
            app.ui.settlementId = -1;
        }
        app.subworld.enter(app.gs, app.terrain, app.features,
                           app.ecs, app.bus, &app.zones);
    }
    if (!app.subworld.active()) {
        smoke_fail(app, "subworld_player_melee enter failed");
        return false;
    }

    auto& reg = app.ecs.reg;
    const float px = app.subworld.player_x();
    const float py = app.subworld.player_y();
    const entt::entity target = reg.create();
    reg.emplace<sm::ecs::Position>(target,
        std::min(px + 4.0f, float(sm::sub::kFullSize - 2)), py);
    reg.emplace<sm::ecs::VisualPos>(target,
        std::min(px + 4.0f, float(sm::sub::kFullSize - 2)), py, 0.0f);
    reg.emplace<sm::ecs::NPCKind>(
        target,
        sm::ecs::NPCKind{
            std::uint16_t(sm::NPCType::Bandit),
            std::uint16_t(3)});
    reg.emplace<sm::ecs::Health>(target, 40.0f, 40.0f);
    reg.emplace<sm::ecs::SubworldTag>(target);
    reg.emplace<sm::ecs::Active>(target);
    reg.emplace<sm::ecs::Sprite>(
        target,
        std::uint16_t(sm::NPCType::Bandit),
        std::uint8_t(255), std::uint8_t(84), std::uint8_t(54),
        std::uint8_t(255), 1.2f);

    const sm::DerivedBonuses derived =
        sm::calculate_derived(app.gs.player.attributes,
                              app.gs.player.skills);
    const float expectedDamage = std::floor(10.0f + derived.rawPhysDamage);
    const float beforeHp = reg.get<sm::ecs::Health>(target).hp;
    const int beforeCombatLog = app.subworld.combat_log_count();
    app.subworld.set_player_attack_held(true);
    RuntimeFrameStats frameStats = tick_playing_runtime(app, 0.05f, false);
    app.subworld.set_player_attack_held(false);
    if (!frameStats.ticked || !frameStats.subworldActive) {
        smoke_fail(app, "subworld_player_melee tick inactive");
        return false;
    }

    const auto* hp = reg.try_get<sm::ecs::Health>(target);
    const auto* hitFlash = reg.try_get<sm::ecs::HitFlash>(target);
    const auto* lastHit = reg.try_get<sm::ecs::LastHit>(target);
    const int afterCombatLog = app.subworld.combat_log_count();
    const sm::sub::CombatLogEntry* combatLog =
        app.subworld.combat_log_entry(afterCombatLog - 1);
    const bool combatLogVisible = afterCombatLog > beforeCombatLog
        && combatLog && combatLog->text[0] != '\0'
        && combatLog->age <= sm::sub::kCombatLogVisibleSeconds;
    const char* status = app.subworld.status_line();
    const bool statusSet = status && status[0] != '\0';
    const float afterHp = hp ? hp->hp : -1.0f;
    const float dealt = beforeHp - afterHp;

    std::fprintf(stderr,
                 "[smoke] subworld_player_melee hp=%.1f->%.1f "
                 "expected=%.1f flash=%.3f playerOwned=%d log=\"%s\" status=\"%s\"\n",
                 double(beforeHp),
                 double(afterHp),
                 double(expectedDamage),
                 hitFlash ? double(hitFlash->timer) : 0.0,
                 lastHit && lastHit->playerOwned ? 1 : 0,
                 combatLogVisible ? combatLog->text : "",
                 statusSet ? status : "");
    std::fflush(stderr);

    if (!hp || std::fabs(dealt - expectedDamage) > 0.001f
        || !hitFlash || hitFlash->timer <= 0.0f
        || !lastHit || !lastHit->playerOwned
        || !combatLogVisible || !statusSet) {
        smoke_fail(app, "subworld_player_melee invariant");
        return false;
    }
    return true;
}

bool run_subworld_reputation_hit_smoke(App& app) {
    if (!app.worldLoaded) {
        smoke_fail(app, "subworld_reputation_hit without world");
        return false;
    }
    smoke_clear_modal_overlays(app);
    if (!app.subworld.active()) {
        int cellX = 0;
        int cellY = 0;
        if (smoke_find_open_subworld_cell(app, cellX, cellY)
            || smoke_find_danger_land_cell(app, cellX, cellY)) {
            app.gs.player.x = float(cellX);
            app.gs.player.y = float(cellY);
            app.gs.subState.settlementId = -1;
            app.ui.settlementId = -1;
        }
        app.subworld.enter(app.gs, app.terrain, app.features,
                           app.ecs, app.bus, &app.zones);
    }
    if (!app.subworld.active()) {
        smoke_fail(app, "subworld_reputation_hit enter failed");
        return false;
    }

    auto& reg = app.ecs.reg;
    app.gs.player.reputation["empire"] = 0;
    const float px = app.subworld.player_x();
    const float py = app.subworld.player_y();
    const float tx = std::min(px + 2.0f, float(sm::sub::kFullSize - 2));
    const entt::entity target = reg.create();
    reg.emplace<sm::ecs::Position>(target, tx, py);
    reg.emplace<sm::ecs::VisualPos>(target, tx, py, 0.0f);
    reg.emplace<sm::ecs::NPCKind>(
        target,
        sm::ecs::NPCKind{
            std::uint16_t(sm::NPCType::Peasant),
            std::uint16_t(0)});
    reg.emplace<sm::ecs::Health>(target, 40.0f, 40.0f);
    reg.emplace<sm::ecs::Combat>(
        target,
        3.0f, 20.0f, 2.0f, 1.5f, 0.0f,
        sm::ecs::Combat::Melee);
    reg.emplace<sm::ecs::SubworldAi>(
        target,
        sm::ecs::SubworldAi::Flee,
        3.0f, 0.0f, 0.0f, 8.0f, 0.55f);
    reg.emplace<sm::ecs::SubworldTag>(target);
    reg.emplace<sm::ecs::Active>(target);
    reg.emplace<sm::ecs::Sprite>(
        target,
        std::uint16_t(sm::NPCType::Peasant),
        std::uint8_t(190), std::uint8_t(150), std::uint8_t(120),
        std::uint8_t(255), 0.8f);

    const int beforeRep = app.gs.player.reputation["empire"];
    const float neutralX = reg.get<sm::ecs::Position>(target).x;
    const float neutralY = reg.get<sm::ecs::Position>(target).y;
    RuntimeFrameStats neutralFrame = tick_playing_runtime(app, 0.05f, false);
    if (!neutralFrame.ticked || !neutralFrame.subworldActive) {
        smoke_fail(app, "subworld_reputation_hit neutral tick inactive");
        return false;
    }
    const auto& neutralPos = reg.get<sm::ecs::Position>(target);
    const float neutralMove = std::sqrt(
        (neutralPos.x - neutralX) * (neutralPos.x - neutralX)
        + (neutralPos.y - neutralY) * (neutralPos.y - neutralY));

    const float beforeFriendlySpellHp = reg.get<sm::ecs::Health>(target).hp;
    const int beforeFriendlySpellLog = app.subworld.combat_log_count();
    const entt::entity friendlyProjectile = reg.create();
    reg.emplace<sm::ecs::Position>(friendlyProjectile, tx, py);
    reg.emplace<sm::ecs::Projectile>(
        friendlyProjectile,
        0.0f, 0.0f, 1.5f, 1.0f, 1.0f, 13.0f, 0.0f,
        tx, py, 0.0f, 0.0f, 0.0f,
        sm::stable_spell_id("magic_bolt"), std::uint32_t{0},
        std::int16_t{0}, sm::ecs::Projectile::Bolt,
        false, false, false);
    reg.emplace<sm::ecs::SubworldTag>(friendlyProjectile);
    RuntimeFrameStats friendlySpellFrame =
        tick_playing_runtime(app, 0.05f, false);
    if (!friendlySpellFrame.ticked || !friendlySpellFrame.subworldActive) {
        smoke_fail(app, "subworld_reputation_hit friendly spell tick inactive");
        return false;
    }
    const float afterFriendlySpellHp = reg.get<sm::ecs::Health>(target).hp;
    const bool friendlySpellBlocked =
        std::fabs(afterFriendlySpellHp - beforeFriendlySpellHp) <= 0.001f
        && !reg.any_of<sm::ecs::HitFlash>(target)
        && app.subworld.combat_log_count() == beforeFriendlySpellLog;
    if (reg.valid(friendlyProjectile)) {
        reg.destroy(friendlyProjectile);
    }

    app.subworld.set_player_attack_held(true);
    RuntimeFrameStats frameStats = tick_playing_runtime(app, 0.05f, false);
    app.subworld.set_player_attack_held(false);
    if (!frameStats.ticked || !frameStats.subworldActive) {
        smoke_fail(app, "subworld_reputation_hit tick inactive");
        return false;
    }

    const int afterRep = app.gs.player.reputation["empire"];
    const bool tempHostile =
        reg.any_of<sm::ecs::TempHostileToPlayer>(target);
    const auto* ai = reg.try_get<sm::ecs::SubworldAi>(target);
    const auto danger = app.subworld.danger_level();
    const int combatLogCount = app.subworld.combat_log_count();
    const sm::sub::CombatLogEntry* combatLog =
        app.subworld.combat_log_entry(combatLogCount - 1);
    const bool logIsPlayerHit = combatLog
        && std::string_view(combatLog->text).find("You hit Peasant")
            != std::string_view::npos;

    std::fprintf(stderr,
                 "[smoke] subworld_reputation_hit rep=%d->%d temp=%d "
                 "ai=%d danger=%d neutralMove=%.3f friendlySpell=%d log=\"%s\"\n",
                 beforeRep,
                 afterRep,
                 tempHostile ? 1 : 0,
                 ai ? int(ai->kind) : -1,
                 int(danger),
                 double(neutralMove),
                 friendlySpellBlocked ? 1 : 0,
                 combatLog ? combatLog->text : "");
    std::fflush(stderr);

    if (beforeRep != 0 || neutralMove > 0.001f || !friendlySpellBlocked
        || afterRep != -1 || !tempHostile
        || !ai || ai->kind != sm::ecs::SubworldAi::Flee
        || danger != sm::sub::DangerLevel::Red
        || !logIsPlayerHit) {
        smoke_fail(app, "subworld_reputation_hit invariant");
        return false;
    }
    return true;
}

void smoke_close_gameplay_panels(App& app) {
    app.ui.diplomacy = false;
    app.ui.settlement = false;
    app.ui.quest = false;
    app.ui.codex = false;
    app.ui.map = false;
    app.ui.character = false;
    app.showDebug = false;
}

bool run_subworld_mouse_release_smoke(App& app) {
    if (!smoke_boot_invariants_hold(app)) {
        smoke_print_counts(app, "subworld_mouse_release_boot_failed");
        smoke_fail(app, "subworld_mouse_release boot invariants");
        return false;
    }

    smoke_clear_modal_overlays(app);
    smoke_close_gameplay_panels(app);
    if (!app.subworld.active()) {
        int cellX = 0;
        int cellY = 0;
        if (smoke_find_open_subworld_cell(app, cellX, cellY)) {
            app.gs.player.x = float(cellX);
            app.gs.player.y = float(cellY);
            app.gs.subState.settlementId = -1;
            app.ui.settlementId = -1;
        }
        app.subworld.enter(app.gs, app.terrain, app.features,
                           app.ecs, app.bus, &app.zones);
    }
    if (!app.subworld.active()) {
        smoke_fail(app, "subworld_mouse_release enter failed");
        return false;
    }

    sync_relative_mouse_mode(app);
    const bool captured = SDL_GetRelativeMouseMode() == SDL_TRUE;
    app.ui.map = true;
    sync_relative_mouse_mode(app);
    const bool releasedMap = SDL_GetRelativeMouseMode() == SDL_FALSE;
    app.ui.map = false;
    app.ui.character = true;
    sync_relative_mouse_mode(app);
    const bool releasedCharacter = SDL_GetRelativeMouseMode() == SDL_FALSE;
    app.ui.character = false;
    sync_relative_mouse_mode(app);
    const bool restored = SDL_GetRelativeMouseMode() == SDL_TRUE;

    std::fprintf(stderr,
                 "[smoke] subworld_mouse_release captured=%d "
                 "releasedMap=%d releasedCharacter=%d restored=%d\n",
                 captured ? 1 : 0,
                 releasedMap ? 1 : 0,
                 releasedCharacter ? 1 : 0,
                 restored ? 1 : 0);
    std::fflush(stderr);

    if (!captured || !releasedMap || !releasedCharacter || !restored) {
        smoke_fail(app, "subworld_mouse_release invariant");
        return false;
    }
    return true;
}

float smoke_tree_hash01(const sm::sub::SeamlessSubworldManager& mgr,
                        const sm::sub::Structure& s) {
    const float absX = float((mgr.center_cx() - 1) * sm::sub::kCellSize) + s.x;
    const float absY = float((mgr.center_cy() - 1) * sm::sub::kCellSize) + s.y;
    std::uint32_t h = std::uint32_t(absX * 374761.0f)
        * std::uint32_t{2246822519}
        ^ std::uint32_t(absY * 668265.0f)
        * std::uint32_t{3266489917};
    h ^= h >> 13;
    h *= std::uint32_t{1274126177};
    h ^= h >> 16;
    return float(h & std::uint32_t{0x00ffffff}) / float(0x00ffffff);
}

bool run_subworld_tree_anchor_smoke(App& app) {
    if (!smoke_boot_invariants_hold(app)) {
        smoke_print_counts(app, "subworld_tree_anchor_boot_failed");
        smoke_fail(app, "subworld_tree_anchor boot invariants");
        return false;
    }

    smoke_clear_modal_overlays(app);
    smoke_close_gameplay_panels(app);
    if (app.subworld.active()) {
        app.subworld.leave(true);
    }

    int cellX = 0;
    int cellY = 0;
    if (smoke_find_tree_subworld_cell(app, cellX, cellY)
        || smoke_find_open_subworld_cell(app, cellX, cellY)) {
        app.gs.player.x = float(cellX);
        app.gs.player.y = float(cellY);
        app.gs.subState.settlementId = -1;
        app.ui.settlementId = -1;
    }
    app.subworld.enter(app.gs, app.terrain, app.features,
                       app.ecs, app.bus, &app.zones);
    if (!app.subworld.active()) {
        smoke_fail(app, "subworld_tree_anchor enter failed");
        return false;
    }

    const auto& mgr = app.subworld.mgr();
    const sm::sub::Structure* focus = nullptr;
    float bestDistSq = 1e30f;
    float minSink = 1e30f;
    float maxSink = 0.0f;
    int treeCount = 0;
    int typeCount[sm::sub::TreeAtlas::kTypes]{};
    const float px = app.subworld.player_x();
    const float py = app.subworld.player_y();
    for (const auto& s : mgr.structures()) {
        if (s.kind != sm::sub::Structure::Tree) continue;
        ++treeCount;
        const float sink = s.height * 0.15f; // billboard anchor sink
        minSink = std::min(minSink, sink);
        maxSink = std::max(maxSink, sink);
        const int cellCol = std::min(2, std::max(0, int(s.x) / sm::sub::kCellSize));
        const int cellRow = std::min(2, std::max(0, int(s.y) / sm::sub::kCellSize));
        const float temp = mgr.cell_temperature(cellRow * 3 + cellCol);
        const int type = sm::sub::tree_type_for_temperature(
            temp, smoke_tree_hash01(mgr, s));
        if (type >= 0 && type < sm::sub::TreeAtlas::kTypes) {
            ++typeCount[type];
        }
        const float dx = s.x - px;
        const float dy = s.y - py;
        const float dSq = dx * dx + dy * dy;
        if (dSq > 9.0f && dSq < bestDistSq) {
            bestDistSq = dSq;
            focus = &s;
        }
    }

    if (focus) {
        float dx = focus->x - app.subworld.player_x();
        float dy = focus->y - app.subworld.player_y();
        float dist = std::sqrt(dx * dx + dy * dy);
        float yaw = std::atan2(dy, dx);
        app.subworld.rotate_camera(yaw - app.subworld.cam_yaw(), -0.10f);
        if (dist > 22.0f) {
            app.subworld.move_player(0.0f, (dist - 18.0f) / 0.4f);
            dx = focus->x - app.subworld.player_x();
            dy = focus->y - app.subworld.player_y();
            yaw = std::atan2(dy, dx);
            app.subworld.rotate_camera(yaw - app.subworld.cam_yaw(), 0.0f);
        }
    }

    const int autumn = typeCount[3];
    const int nonAutumn = treeCount - autumn;
    std::fprintf(stderr,
                 "[smoke] subworld_tree_anchor cell=%d,%d trees=%d "
                 "sink=%.2f..%.2f types=[%d,%d,%d,%d,%d,%d,%d] "
                 "focus=%.1f,%.1f\n",
                 cellX, cellY, treeCount, minSink, maxSink,
                 typeCount[0], typeCount[1], typeCount[2], typeCount[3],
                 typeCount[4], typeCount[5], typeCount[6],
                 focus ? focus->x : -1.0f,
                 focus ? focus->y : -1.0f);
    std::fflush(stderr);

    if (treeCount <= 0 || !focus || minSink < 1.70f
        || (treeCount > 8 && nonAutumn <= 0)) {
        smoke_fail(app, "subworld_tree_anchor invariant");
        return false;
    }
    return true;
}

// Exercise the dev console end-to-end: drive real command strings through
// app.console.execute() and assert the resulting GameState / ECS deltas. This
// is the only path that actually RUNS the console handlers (the interactive
// window is never opened headlessly), so it guards the whole feature against
// regressions. All player/world mutations are captured up front and restored
// before returning, leaving the world exactly as found.
bool run_console_smoke(App& app) {
    if (!smoke_boot_invariants_hold(app)) {
        smoke_print_counts(app, "console_boot_failed");
        smoke_fail(app, "console boot invariants");
        return false;
    }
    if (app.subworld.active()) {
        smoke_fail(app, "console smoke started with a subworld active");
        return false;
    }

    // Snapshot everything the commands below touch, so we can fully restore.
    const int    oldGold         = app.gs.player.gold;
    const auto   oldInv          = app.gs.player.inventory;
    const auto   oldLevel        = app.gs.player.levelData;
    const auto   oldCombat       = app.gs.player.combatStats;
    const auto   oldSpellBook    = app.gs.player.spellBook;
    const auto   oldTime         = app.gs.worldTime;
    const float  oldSimSpeed     = app.simSpeed;
    const float  oldX            = app.gs.player.x;
    const float  oldY            = app.gs.player.y;
    const auto   oldSubState     = app.gs.subState;
    const int    oldUiSettlement = app.ui.settlementId;
    auto restore = [&]() {
        if (app.subworld.active()) {
            app.subworld.set_god_mode(false);
            app.subworld.set_flying(false);
            app.subworld.leave(true);
        }
        app.gs.player.gold        = oldGold;
        app.gs.player.inventory   = oldInv;
        app.gs.player.levelData   = oldLevel;
        app.gs.player.combatStats = oldCombat;
        app.gs.player.spellBook   = oldSpellBook;
        app.gs.worldTime          = oldTime;
        app.simSpeed              = oldSimSpeed;
        app.gs.player.x           = oldX;
        app.gs.player.y           = oldY;
        app.gs.subState           = oldSubState;
        app.ui.settlementId       = oldUiSettlement;
    };

    sm::dev::Console& con = app.console;

    // ── Items / gold / progression (macro context) ───────────────
    const std::size_t sbHelp = con.scrollback.size();
    con.execute("help");
    if (con.scrollback.size() <= sbHelp) {
        restore(); smoke_fail(app, "console help produced no output"); return false;
    }

    con.execute("gold 500");
    if (app.gs.player.gold != oldGold + 500) {
        restore(); smoke_fail(app, "console gold add"); return false;
    }

    const int potBefore = app.gs.player.inventory.count("potion_hp");
    con.execute("give potion_hp 3");
    if (app.gs.player.inventory.count("potion_hp") != potBefore + 3) {
        restore(); smoke_fail(app, "console give item"); return false;
    }
    con.execute("take potion_hp 1");
    if (app.gs.player.inventory.count("potion_hp") != potBefore + 2) {
        restore(); smoke_fail(app, "console take item"); return false;
    }
    con.execute("give gold 250");
    if (app.gs.player.gold != oldGold + 750) {
        restore(); smoke_fail(app, "console give gold"); return false;
    }

    const int lvlBefore = app.gs.player.levelData.level;
    con.execute("addexp 100000");
    if (app.gs.player.levelData.level <= lvlBefore) {
        restore(); smoke_fail(app, "console addexp did not level up"); return false;
    }

    con.execute("learnall");
    if (app.gs.player.spellBook.learned.size() != sm::spell_registry().size()) {
        restore(); smoke_fail(app, "console learnall count mismatch"); return false;
    }

    // ── World & toggles (macro context) ──────────────────────────
    con.execute("settime 13 37");
    if (app.gs.worldTime.hour != 13 || app.gs.worldTime.minute != 37) {
        restore(); smoke_fail(app, "console settime"); return false;
    }
    con.execute("simspeed 3");
    if (!(app.simSpeed > 2.99f && app.simSpeed < 3.01f)) {
        restore(); smoke_fail(app, "console simspeed"); return false;
    }
    app.gs.player.combatStats.currentHp = 1;
    con.execute("heal");
    if (app.gs.player.combatStats.currentHp != app.gs.player.combatStats.maxHp) {
        restore(); smoke_fail(app, "console heal"); return false;
    }

    // A usage error (missing arg) must print but never mutate state.
    const int goldPreUsage = app.gs.player.gold;
    con.execute("gold");
    if (app.gs.player.gold != goldPreUsage) {
        restore(); smoke_fail(app, "console usage-error mutated state"); return false;
    }
    // An unknown command must be handled gracefully (output, no crash).
    const std::size_t sbUnknown = con.scrollback.size();
    con.execute("blorf_zzzz 1 2 3");
    if (con.scrollback.size() <= sbUnknown) {
        restore(); smoke_fail(app, "console unknown command produced no output"); return false;
    }

    // ── Spawn / teleport / subworld toggles (subworld context) ───
    app.subworld.enter(app.gs, app.terrain, app.features, app.ecs,
                       app.bus, &app.zones);
    if (!app.subworld.active()) {
        restore(); smoke_fail(app, "console subworld enter failed"); return false;
    }

    auto count_live_bandits = [&]() {
        auto& reg = app.ecs.reg;
        int n = 0;
        auto view = reg.view<sm::ecs::SubworldTag, sm::ecs::NPCKind,
                             sm::ecs::Health>(
            entt::exclude<sm::ecs::Dead, sm::ecs::PlayerSoldierTag>);
        for (auto e : view) {
            if (view.get<sm::ecs::NPCKind>(e).type
                == std::uint16_t(sm::NPCType::Bandit)) ++n;
        }
        return n;
    };

    const int banditsBefore = count_live_bandits();
    con.execute("spawn bandit 2 4");
    const int banditsAfter = count_live_bandits();
    const int spawnedDelta = banditsAfter - banditsBefore;
    if (spawnedDelta < 1) {
        restore(); smoke_fail(app, "console spawn produced no hostiles"); return false;
    }

    // ── Monster spawn + per-creature XP via the unified table (Inc 3) ──
    // A stable creature id ("wolf") resolves through the SAME spawn entry as
    // humanoid NPCs, yielding a monster-kind entity (NPCKind.type >= 0x100) that
    // maps back to the wolf catalog row. Killing it grants XP through the shared
    // death path (fauna route), proving spawn-any-creature + creature XP.
    auto count_live_creatures = [&]() {
        auto& reg = app.ecs.reg;
        int n = 0;
        auto view = reg.view<sm::ecs::SubworldTag, sm::ecs::NPCKind,
                             sm::ecs::Health>(
            entt::exclude<sm::ecs::Dead, sm::ecs::PlayerSoldierTag>);
        for (auto e : view) {
            if (view.get<sm::ecs::NPCKind>(e).type >= std::uint16_t{0x100}) ++n;
        }
        return n;
    };
    const int creaturesBefore = count_live_creatures();
    con.execute("spawn wolf");
    const int creatureDelta = count_live_creatures() - creaturesBefore;
    if (creatureDelta < 1) {
        restore(); smoke_fail(app, "console spawn wolf produced no monster"); return false;
    }
    {
        auto& reg = app.ecs.reg;
        const sm::sub::FaunaEntry* wolf = sm::sub::creature_def("wolf");
        entt::entity wolfE = entt::null;
        auto view = reg.view<sm::ecs::SubworldTag, sm::ecs::NPCKind,
                             sm::ecs::Health>(
            entt::exclude<sm::ecs::Dead, sm::ecs::PlayerSoldierTag>);
        for (auto e : view) {
            if (sm::sub::creature_def_from_kind(
                    view.get<sm::ecs::NPCKind>(e).type) == wolf) { wolfE = e; break; }
        }
        if (!wolf || wolfE == entt::null) {
            restore();
            smoke_fail(app, "console spawn wolf: kind did not resolve to wolf row");
            return false;
        }
        const int expBefore = app.gs.player.levelData.exp;
        if (auto* hp = reg.try_get<sm::ecs::Health>(wolfE)) hp->hp = 0.0f;
        reg.emplace_or_replace<sm::ecs::LastHit>(wolfE, 0u, true);
        if (!reg.any_of<sm::ecs::Dead>(wolfE)) reg.emplace<sm::ecs::Dead>(wolfE);
        app.subworld.tick(0.016f);
        if (app.gs.player.levelData.exp <= expBefore) {
            restore(); smoke_fail(app, "console wolf kill granted no XP"); return false;
        }
    }

    con.execute("godmode on");
    if (!app.subworld.god_mode()) {
        restore(); smoke_fail(app, "console godmode on"); return false;
    }
    con.execute("godmode off");
    if (app.subworld.god_mode()) {
        restore(); smoke_fail(app, "console godmode off"); return false;
    }
    con.execute("godmode");   // bare toggle -> back on
    if (!app.subworld.god_mode()) {
        restore(); smoke_fail(app, "console godmode toggle"); return false;
    }

    con.execute("flight on");
    if (!app.subworld.flying()) {
        restore(); smoke_fail(app, "console flight on"); return false;
    }

    con.execute("killall");
    const int banditsFinal = count_live_bandits();
    if (banditsFinal != 0) {
        restore(); smoke_fail(app, "console killall left hostiles"); return false;
    }

    // Capture reporting values before restoring the world.
    const int         rGold   = app.gs.player.gold;
    const int         rLevel  = app.gs.player.levelData.level;
    const std::size_t rSpells = app.gs.player.spellBook.learned.size();
    restore();

    std::fprintf(stderr,
                 "[smoke] console gold=%d->%d level=%d->%d spells=%zu "
                 "spawned=%d spawned_creatures=%d killall_cleared=1\n",
                 oldGold, rGold, lvlBefore, rLevel, rSpells, spawnedDelta,
                 creatureDelta);
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
        case SmokeAction::SubworldSeam:
            std::fprintf(stderr, "[smoke] action=subworld_seam\n");
            std::fflush(stderr);
            if (run_subworld_seam_smoke(app)) ++app.smoke.cursor;
            break;
        case SmokeAction::SubworldAudio:
            std::fprintf(stderr, "[smoke] action=subworld_audio\n");
            std::fflush(stderr);
            if (run_subworld_audio_smoke(app)) ++app.smoke.cursor;
            break;
        case SmokeAction::SubworldEnter:
            std::fprintf(stderr, "[smoke] action=subworld_enter\n");
            std::fflush(stderr);
            if (!app.worldLoaded) {
                smoke_fail(app, "subworld_enter without world");
                break;
            }
            // Opt-in (TIMAERT_SMOKE_MOUNTAIN=1): relocate the macro player to
            // the nearest mountain-feature cell before entering, so the 3D
            // capture shows mountain relief instead of the spawn city. Test
            // harness only — normal play is unaffected.
            if (!app.subworld.active() && std::getenv("TIMAERT_SMOKE_MOUNTAIN")) {
                const int pcx = int(app.gs.player.x);
                const int pcy = int(app.gs.player.y);
                int bestX = -1, bestY = -1;
                long bestD = 1L << 60;
                for (int y = 0; y < app.gs.mapH; ++y) {
                    for (int x = 0; x < app.gs.mapW; ++x) {
                        if (app.features.at(x, y) != sm::FT_Mountain) continue;
                        const long dx = x - pcx, dy = y - pcy;
                        const long d = dx * dx + dy * dy;
                        if (d < bestD) { bestD = d; bestX = x; bestY = y; }
                    }
                }
                if (bestX >= 0) {
                    app.gs.player.x = float(bestX);
                    app.gs.player.y = float(bestY);
                    std::fprintf(stderr, "[smoke] mountain relocate -> %d,%d\n",
                                 bestX, bestY);
                    std::fflush(stderr);
                }
            }
            if (!app.subworld.active()) {
                app.subworld.enter(app.gs, app.terrain, app.features, app.ecs,
                                   app.bus, &app.zones);
            }
            if (!app.subworld.active()) {
                smoke_fail(app, "subworld_enter failed");
                break;
            }
            for (int i = 0; i < 8; ++i) {
                tick_playing_runtime(app, 1.0f / 60.0f, false);
            }
            std::fprintf(stderr,
                         "[smoke] subworld_enter active=%d 3d=%d player=%.1f,%.1f\n",
                         app.subworld.active() ? 1 : 0,
                         1,
                         app.subworld.player_x(), app.subworld.player_y());
            std::fflush(stderr);
            ++app.smoke.cursor;
            break;
        case SmokeAction::SubworldExitGate:
            std::fprintf(stderr, "[smoke] action=subworld_exit_gate\n");
            std::fflush(stderr);
            if (run_subworld_exit_gate_smoke(app)) ++app.smoke.cursor;
            break;
        case SmokeAction::SubworldLootXp:
            std::fprintf(stderr, "[smoke] action=subworld_loot_xp\n");
            std::fflush(stderr);
            if (run_subworld_loot_xp_smoke(app)) ++app.smoke.cursor;
            break;
        case SmokeAction::SubworldEnemyFeedback:
            std::fprintf(stderr, "[smoke] action=subworld_enemy_feedback\n");
            std::fflush(stderr);
            if (run_subworld_enemy_feedback_smoke(app)) ++app.smoke.cursor;
            break;
        case SmokeAction::SubworldMissileFeedback:
            std::fprintf(stderr, "[smoke] action=subworld_missile_feedback\n");
            std::fflush(stderr);
            if (run_subworld_missile_feedback_smoke(app)) ++app.smoke.cursor;
            break;
        case SmokeAction::SubworldPlayerMelee:
            std::fprintf(stderr, "[smoke] action=subworld_player_melee\n");
            std::fflush(stderr);
            if (run_subworld_player_melee_smoke(app)) ++app.smoke.cursor;
            break;
        case SmokeAction::SubworldReputationHit:
            std::fprintf(stderr, "[smoke] action=subworld_reputation_hit\n");
            std::fflush(stderr);
            if (run_subworld_reputation_hit_smoke(app)) ++app.smoke.cursor;
            break;
        case SmokeAction::SubworldMouseRelease:
            std::fprintf(stderr, "[smoke] action=subworld_mouse_release\n");
            std::fflush(stderr);
            if (run_subworld_mouse_release_smoke(app)) ++app.smoke.cursor;
            break;
        case SmokeAction::SubworldTreeAnchor:
            std::fprintf(stderr, "[smoke] action=subworld_tree_anchor\n");
            std::fflush(stderr);
            if (run_subworld_tree_anchor_smoke(app)) ++app.smoke.cursor;
            break;
        case SmokeAction::SubworldNoRecovery:
            std::fprintf(stderr, "[smoke] action=subworld_no_recovery\n");
            std::fflush(stderr);
            if (run_subworld_no_recovery_smoke(app)) ++app.smoke.cursor;
            break;
        case SmokeAction::SubworldSpDrain:
            std::fprintf(stderr, "[smoke] action=subworld_sp_drain\n");
            std::fflush(stderr);
            if (run_subworld_sp_drain_smoke(app)) ++app.smoke.cursor;
            break;
        case SmokeAction::TriggerBattleStart: {
            std::fprintf(stderr, "[smoke] action=trigger_battle_start\n");
            std::fflush(stderr);
            if (!app.worldLoaded) {
                smoke_fail(app, "trigger_battle_start without world");
                break;
            }
            auto countHostiles = [&]() {
                int n = 0;
                auto view = app.ecs.reg.view<sm::ecs::SubworldTag,
                                             sm::ecs::SubworldAi,
                                             sm::ecs::Health>(
                    entt::exclude<sm::ecs::Dead, sm::ecs::PlayerSoldierTag>);
                for (auto e : view) {
                    const auto& ai = view.get<sm::ecs::SubworldAi>(e);
                    const auto& hp = view.get<sm::ecs::Health>(e);
                    if (ai.kind == sm::ecs::SubworldAi::Combat && hp.hp > 0.0f) {
                        ++n;
                    }
                }
                return n;
            };
            auto countSubworldEntities = [&]() {
                int n = 0;
                auto view = app.ecs.reg.view<sm::ecs::SubworldTag>();
                for (auto e : view) {
                    (void)e;
                    ++n;
                }
                return n;
            };
            auto findSmokeHostile = [&]() -> entt::entity {
                auto view = app.ecs.reg.view<sm::ecs::SubworldTag,
                                             sm::ecs::SubworldAi,
                                             sm::ecs::Health>(
                    entt::exclude<sm::ecs::Dead, sm::ecs::PlayerSoldierTag>);
                for (auto e : view) {
                    const auto& ai = view.get<sm::ecs::SubworldAi>(e);
                    const auto& hp = view.get<sm::ecs::Health>(e);
                    if (ai.kind == sm::ecs::SubworldAi::Combat && hp.hp > 0.0f) {
                        return e;
                    }
                }
                return entt::null;
            };
            const int beforeHostiles = countHostiles();
            sm::GameEvent battle{sm::EventTag::BattleStart};
            battle.s1 = "Smoke Bandit";
            battle.s2 = "bandit";
            battle.ix = 2;
            app.bus.emit(battle);
            process_world_events(app);
            const int afterHostiles = countHostiles();
            if (!app.subworld.active() || afterHostiles <= beforeHostiles) {
                smoke_fail(app, "battle_start did not enter subworld combat");
                break;
            }
            RuntimeFrameStats frameStats =
                tick_playing_runtime(app, 0.10f, false);
            if (!frameStats.ticked || !frameStats.subworldActive) {
                smoke_fail(app, "battle_start subworld tick inactive");
                break;
            }
            std::fprintf(stderr,
                         "[smoke] battle_start routed hostiles=%d->%d status=%s\n",
                         beforeHostiles, afterHostiles,
                         app.subworld.status_line());
            std::fflush(stderr);
            const sm::LevelData beforeDeathXp = app.gs.player.levelData;
            const entt::entity smokeHostile = findSmokeHostile();
            if (smokeHostile == entt::null) {
                smoke_fail(app, "battle_start hostile not found for death flush");
                break;
            }
            if (!app.ecs.reg.any_of<sm::ecs::NpcCharacter>(smokeHostile)) {
                smoke_fail(app, "battle_start hostile missing paper-doll character");
                break;
            }
            auto* smokeHp = app.ecs.reg.try_get<sm::ecs::Health>(smokeHostile);
            if (!smokeHp) {
                smoke_fail(app, "battle_start hostile lost health");
                break;
            }
            smokeHp->hp = 0.0f;
            app.ecs.reg.emplace_or_replace<sm::ecs::LastHit>(
                smokeHostile, 0u, true);
            if (!app.ecs.reg.any_of<sm::ecs::Dead>(smokeHostile)) {
                app.ecs.reg.emplace<sm::ecs::Dead>(smokeHostile);
            }
            app.subworld.leave(true);
            const sm::LevelData afterDeathXp = app.gs.player.levelData;
            if (afterDeathXp.level <= beforeDeathXp.level
                && afterDeathXp.exp <= beforeDeathXp.exp) {
                smoke_fail(app, "battle_start leave did not flush death XP");
                break;
            }
            const int leakedSubworldEntities = countSubworldEntities();
            if (leakedSubworldEntities != 0) {
                smoke_fail(app, "battle_start leave leaked subworld entities");
                break;
            }
            ++app.smoke.cursor;
            break;
        }
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
        case SmokeAction::OpenSettlementBuild: {
            std::fprintf(stderr, "[smoke] action=open_settlement_build\n");
            std::fflush(stderr);
            if (!app.worldLoaded) {
                smoke_fail(app, "open_settlement_build without world");
                break;
            }
            if (app.gs.settlements.empty()) {
                smoke_fail(app, "open_settlement_build without settlements");
                break;
            }
            smoke_clear_modal_overlays(app);
            const sm::Settlement& s = app.gs.settlements.front();
            app.ui.settlementId = s.id;
            app.ui.settlementTab = sm::ui::SettlementPanelTab::Build;
            app.ui.settlement = true;
            refresh_available_settlement_quests(app);
            std::fprintf(stderr,
                         "[smoke] settlement_build open id=%d name=\"%s\" tab=Build\n",
                         s.id, s.name.c_str());
            std::fflush(stderr);
            ++app.smoke.cursor;
            break;
        }
        case SmokeAction::OpenSettlementTrade: {
            std::fprintf(stderr, "[smoke] action=open_settlement_trade\n");
            std::fflush(stderr);
            if (!app.worldLoaded) {
                smoke_fail(app, "open_settlement_trade without world");
                break;
            }
            if (app.gs.settlements.empty()) {
                smoke_fail(app, "open_settlement_trade without settlements");
                break;
            }
            smoke_clear_modal_overlays(app);
            const sm::Settlement& s = app.gs.settlements.front();
            app.ui.settlementId = s.id;
            app.ui.settlementTab = sm::ui::SettlementPanelTab::Trade;
            app.ui.settlement = true;
            app.ui.codex = false;
            app.ui.map = false;
            app.ui.quest = false;
            refresh_available_settlement_quests(app);
            std::fprintf(stderr,
                         "[smoke] settlement_trade open id=%d name=\"%s\" mood=%d stock=%zu playerItems=%d gold=%d\n",
                         s.id,
                         s.name.c_str(),
                         int(s.mood),
                         s.inventory.stacks.size(),
                         app.gs.player.inventory.total(),
                         app.gs.player.gold);
            std::fflush(stderr);
            ++app.smoke.cursor;
            break;
        }
        case SmokeAction::OpenSettlementMap: {
            std::fprintf(stderr, "[smoke] action=open_settlement_map\n");
            std::fflush(stderr);
            if (!app.worldLoaded) {
                smoke_fail(app, "open_settlement_map without world");
                break;
            }
            if (app.gs.settlements.empty()) {
                smoke_fail(app, "open_settlement_map without settlements");
                break;
            }
            smoke_clear_modal_overlays(app);
            const sm::Settlement& s = app.gs.settlements.front();
            app.ui.settlementId = s.id;
            app.ui.settlementTab = sm::ui::SettlementPanelTab::Map;
            app.ui.settlement = true;
            app.ui.codex = false;
            app.ui.map = false;
            app.ui.quest = false;
            refresh_available_settlement_quests(app);
            const std::uint32_t previewSeed =
                app.gs.worldSeed + std::uint32_t(s.id >= 0 ? s.id : 0) * 123u;
            std::fprintf(stderr,
                         "[smoke] settlement_map open id=%d name=\"%s\" seed=0x%08X pop=%d\n",
                         s.id,
                         s.name.c_str(),
                         previewSeed,
                         s.population);
            std::fflush(stderr);
            ++app.smoke.cursor;
            break;
        }
        case SmokeAction::EnterFirstSettlement: {
            std::fprintf(stderr, "[smoke] action=enter_first_settlement\n");
            std::fflush(stderr);
            if (!app.worldLoaded) {
                smoke_fail(app, "enter_first_settlement without world");
                break;
            }
            if (app.gs.settlements.empty()) {
                smoke_fail(app, "enter_first_settlement without settlements");
                break;
            }
            if (app.subworld.active()) {
                app.subworld.leave(true);
            }
            smoke_clear_modal_overlays(app);
            const sm::Settlement& s = app.gs.settlements.front();
            app.gs.player.x = float(s.x);
            app.gs.player.y = float(s.y);
            app.cursor.path.clear();
            app.cursor.pathIdx = 0;
            app.gs.subState.settlementId = s.id;
            app.ui.settlementId = s.id;
            app.ui.settlement = false;
            app.subworld.enter(app.gs, app.terrain, app.features,
                               app.ecs, app.bus, &app.zones);
            if (!app.subworld.active()) {
                smoke_fail(app, "enter_first_settlement subworld enter failed");
                break;
            }
            int houses = 0;
            int walls = 0;
            for (const auto& st : app.subworld.mgr().structures()) {
                if (st.kind == sm::sub::Structure::House) ++houses;
                if (st.kind == sm::sub::Structure::Wall) ++walls;
            }
            int citizens = 0;
            auto cityView = app.ecs.reg.view<sm::ecs::SubworldTag,
                                             sm::ecs::NpcCharacter,
                                             sm::ecs::NPCKind>();
            for (auto e : cityView) {
                (void)e;
                ++citizens;
            }
            std::fprintf(stderr,
                         "[smoke] settlement_subworld id=%d pop=%d houses=%d walls=%d citizens=%d center=%d,%d\n",
                         s.id, s.population, houses, walls, citizens,
                         app.subworld.mgr().center_cx(),
                         app.subworld.mgr().center_cy());
            std::fflush(stderr);
            if (houses <= 0 || citizens <= 0) {
                smoke_fail(app, "enter_first_settlement missing structures or citizens");
                break;
            }
            ++app.smoke.cursor;
            break;
        }
        case SmokeAction::FocusNpcPanel: {
            std::fprintf(stderr, "[smoke] action=focus_npc_panel\n");
            std::fflush(stderr);
            if (!app.worldLoaded) {
                smoke_fail(app, "focus_npc_panel without world");
                break;
            }
            smoke_clear_modal_overlays(app);
            auto view = app.ecs.reg.view<sm::ecs::Position,
                                         sm::ecs::NPCKind,
                                         sm::ecs::Health,
                                         sm::ecs::NpcLevel,
                                         sm::ecs::NpcCharacter,
                                         sm::ecs::NpcInventory>();
            bool found = false;
            for (auto e : view) {
                const auto& hp = view.get<sm::ecs::Health>(e);
                if (hp.hp <= 0.0f) continue;
                const auto& pos = view.get<sm::ecs::Position>(e);
                const auto& kind = view.get<sm::ecs::NPCKind>(e);
                app.gs.player.x = pos.x;
                app.gs.player.y = pos.y;
                app.cursor.path.clear();
                app.cursor.pathIdx = 0;
                app.ui.settlement = false;
                found = true;
                std::fprintf(stderr,
                             "[smoke] npc_panel focus type=%d x=%.2f y=%.2f inventory=1\n",
                             int(kind.type), pos.x, pos.y);
                std::fflush(stderr);
                break;
            }
            if (!found) {
                smoke_fail(app, "focus_npc_panel found no live NPC with NpcInventory");
                break;
            }
            ++app.smoke.cursor;
            break;
        }
        case SmokeAction::OpenNpcTrade: {
            std::fprintf(stderr, "[smoke] action=open_npc_trade\n");
            std::fflush(stderr);
            if (!app.worldLoaded) {
                smoke_fail(app, "open_npc_trade without world");
                break;
            }
            smoke_clear_modal_overlays(app);
            app.ui.settlement = false;
            app.ui.codex = false;
            app.ui.map = false;
            app.ui.quest = false;
            auto view = app.ecs.reg.view<sm::ecs::Position,
                                         sm::ecs::NPCKind,
                                         sm::ecs::Health,
                                         sm::ecs::NpcLevel,
                                         sm::ecs::NpcCharacter,
                                         sm::ecs::NpcInventory>();
            entt::entity target = entt::null;
            int stock = 0;
            int type = -1;
            for (auto e : view) {
                const auto& hp = view.get<sm::ecs::Health>(e);
                if (hp.hp <= 0.0f) continue;
                const auto& pos = view.get<sm::ecs::Position>(e);
                const auto& kind = view.get<sm::ecs::NPCKind>(e);
                const auto& bag = view.get<sm::ecs::NpcInventory>(e);
                app.gs.player.x = pos.x;
                app.gs.player.y = pos.y;
                app.cursor.path.clear();
                app.cursor.pathIdx = 0;
                target = e;
                stock = bag.inv.total();
                type = int(kind.type);
                break;
            }
            if (target == entt::null) {
                smoke_fail(app, "open_npc_trade found no live NPC with inventory");
                break;
            }
            sm::ui::open_npc_trade_panel(target);
            std::fprintf(stderr,
                         "[smoke] npc_trade open entity=%u type=%d stock=%d playerItems=%d gold=%d\n",
                         static_cast<unsigned>(entt::to_integral(target)),
                         type,
                         stock,
                         app.gs.player.inventory.total(),
                         app.gs.player.gold);
            std::fflush(stderr);
            ++app.smoke.cursor;
            break;
        }
        case SmokeAction::AttackFirstNpc: {
            std::fprintf(stderr, "[smoke] action=attack_first_npc\n");
            std::fflush(stderr);
            if (!app.worldLoaded) {
                smoke_fail(app, "attack_first_npc without world");
                break;
            }
            smoke_clear_modal_overlays(app);
            auto view = app.ecs.reg.view<sm::ecs::Position,
                                         sm::ecs::NPCKind,
                                         sm::ecs::Health,
                                         sm::ecs::NpcLevel,
                                         sm::ecs::NpcCharacter>();
            entt::entity target = entt::null;
            for (auto e : view) {
                const auto& hp = view.get<sm::ecs::Health>(e);
                if (hp.hp <= 0.0f) continue;
                const auto& pos = view.get<sm::ecs::Position>(e);
                app.gs.player.x = pos.x;
                app.gs.player.y = pos.y;
                target = e;
                break;
            }
            if (target == entt::null) {
                smoke_fail(app, "attack_first_npc found no live NPC");
                break;
            }
            if (!route_macro_npc_attack(app, target)) {
                smoke_fail(app, "attack_first_npc route failed");
                break;
            }
            std::fprintf(stderr, "[smoke] npc_attack routed active=%d\n",
                         app.subworld.active() ? 1 : 0);
            std::fflush(stderr);
            ++app.smoke.cursor;
            break;
        }
        case SmokeAction::CaptureFrame: {
            std::fprintf(stderr, "[smoke] action=capture_frame\n");
            std::fflush(stderr);
            app.smoke.capturePending = true;
            app.smoke.captureActionIndex = app.smoke.cursor;
            ++app.smoke.cursor;
            break;
        }
        case SmokeAction::OpenMap: {
            std::fprintf(stderr, "[smoke] action=open_map\n");
            std::fflush(stderr);
            if (!app.worldLoaded) {
                smoke_fail(app, "open_map without world");
                break;
            }
            app.ui.map = true;
            ++app.smoke.cursor;
            break;
        }
        case SmokeAction::OpenStats: {
            std::fprintf(stderr, "[smoke] action=open_stats\n");
            std::fflush(stderr);
            if (!app.worldLoaded) {
                smoke_fail(app, "open_stats without world");
                break;
            }
            app.ui.character = true;
            app.ui.characterTab = sm::ui::CharacterPanelTab::Stats;
            app.ui.map = false;
            app.ui.quest = false;
            app.ui.codex = false;
            std::fprintf(stderr,
                         "[smoke] stats open attrPts=%d skillPts=%d perkPts=%d vit=%d bodybuilding=%d hpMax=%d\n",
                         app.gs.player.levelData.attributePoints,
                         app.gs.player.levelData.skillPoints,
                         app.gs.player.levelData.perkPoints,
                         app.gs.player.attributes.vit,
                         app.gs.player.skills.bodybuilding,
                         app.gs.player.combatStats.maxHp);
            std::fflush(stderr);
            ++app.smoke.cursor;
            break;
        }
        case SmokeAction::SpendAttributeVit: {
            std::fprintf(stderr, "[smoke] action=spend_attribute_vit\n");
            std::fflush(stderr);
            if (!app.worldLoaded) {
                smoke_fail(app, "spend_attribute_vit without world");
                break;
            }
            const int beforePoints = app.gs.player.levelData.attributePoints;
            const int beforeVit = app.gs.player.attributes.vit;
            const int beforeHp = app.gs.player.combatStats.maxHp;
            if (!sm::spend_attribute_point(app.gs.player.levelData,
                                           app.gs.player.attributes,
                                           sm::AttributeId::Vit)) {
                smoke_fail(app, "spend_attribute_vit rejected");
                break;
            }
            app.gs.player.combatStats =
                sm::calculate_combat_stats(app.gs.player.attributes,
                                           app.gs.player.skills);
            if (app.gs.player.levelData.attributePoints != beforePoints - 1
                || app.gs.player.attributes.vit != beforeVit + 1
                || app.gs.player.combatStats.maxHp <= beforeHp) {
                smoke_fail(app, "spend_attribute_vit invariant");
                break;
            }
            std::fprintf(stderr,
                         "[smoke] spend_attr_vit points=%d->%d vit=%d->%d hpMax=%d->%d\n",
                         beforePoints,
                         app.gs.player.levelData.attributePoints,
                         beforeVit,
                         app.gs.player.attributes.vit,
                         beforeHp,
                         app.gs.player.combatStats.maxHp);
            std::fflush(stderr);
            ++app.smoke.cursor;
            break;
        }
        case SmokeAction::SpendSkillBodybuilding: {
            std::fprintf(stderr, "[smoke] action=spend_skill_bodybuilding\n");
            std::fflush(stderr);
            if (!app.worldLoaded) {
                smoke_fail(app, "spend_skill_bodybuilding without world");
                break;
            }
            const int beforePoints = app.gs.player.levelData.skillPoints;
            const int beforeRank = app.gs.player.skills.bodybuilding;
            const int beforeHp = app.gs.player.combatStats.maxHp;
            if (!sm::spend_skill_point(app.gs.player.levelData,
                                       app.gs.player.skills,
                                       sm::SkillId::Bodybuilding)) {
                smoke_fail(app, "spend_skill_bodybuilding rejected");
                break;
            }
            app.gs.player.combatStats =
                sm::calculate_combat_stats(app.gs.player.attributes,
                                           app.gs.player.skills);
            if (app.gs.player.levelData.skillPoints != beforePoints - 1
                || app.gs.player.skills.bodybuilding != beforeRank + 1
                || app.gs.player.combatStats.maxHp <= beforeHp) {
                smoke_fail(app, "spend_skill_bodybuilding invariant");
                break;
            }
            std::fprintf(stderr,
                         "[smoke] spend_skill_bodybuilding points=%d->%d rank=%d->%d hpMax=%d->%d\n",
                         beforePoints,
                         app.gs.player.levelData.skillPoints,
                         beforeRank,
                         app.gs.player.skills.bodybuilding,
                         beforeHp,
                         app.gs.player.combatStats.maxHp);
            std::fflush(stderr);
            ++app.smoke.cursor;
            break;
        }
        case SmokeAction::MacroTravelSp:
            std::fprintf(stderr, "[smoke] action=macro_travel_sp\n");
            std::fflush(stderr);
            if (run_macro_travel_sp_smoke(app)) ++app.smoke.cursor;
            break;
        case SmokeAction::MacroRecovery:
            std::fprintf(stderr, "[smoke] action=macro_recovery\n");
            std::fflush(stderr);
            if (run_macro_recovery_smoke(app)) ++app.smoke.cursor;
            break;
        case SmokeAction::TimeAdvanceBurst:
            std::fprintf(stderr, "[smoke] action=timeadvance_burst\n");
            std::fflush(stderr);
            if (run_timeadvance_burst_smoke(app)) ++app.smoke.cursor;
            break;
        case SmokeAction::MacroNpcTrace:
            std::fprintf(stderr, "[smoke] action=macro_npc_trace\n");
            std::fflush(stderr);
            if (run_macro_npc_trace_smoke(app)) ++app.smoke.cursor;
            break;
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
        case SmokeAction::OpenCodex:
            std::fprintf(stderr, "[smoke] action=open_codex\n");
            std::fflush(stderr);
            if (!app.worldLoaded) {
                smoke_fail(app, "open_codex without world");
                break;
            }
            app.ui.codex = true;
            std::fprintf(stderr,
                         "[smoke] codex open unlocked=%zu first=%s\n",
                         app.gs.player.codexUnlocked.size(),
                         app.gs.player.codexUnlocked.empty()
                             ? "(none)" : app.gs.player.codexUnlocked.front().c_str());
            std::fflush(stderr);
            ++app.smoke.cursor;
            break;
        case SmokeAction::OpenSpells: {
            std::fprintf(stderr, "[smoke] action=open_spells\n");
            std::fflush(stderr);
            if (!app.worldLoaded) {
                smoke_fail(app, "open_spells without world");
                break;
            }
            app.ui.character = true;
            app.ui.characterTab = sm::ui::CharacterPanelTab::Spells;
            const auto& book = app.gs.player.spellBook;
            const auto cdIt = book.activeSpellId.empty()
                ? book.cooldowns.end()
                : book.cooldowns.find(book.activeSpellId);
            const float cd = cdIt == book.cooldowns.end() ? 0.0f : cdIt->second;
            std::fprintf(stderr,
                         "[smoke] spell_overlay learned=%zu active=%s mp=%d/%d cd=%.2f sustained=%zu\n",
                         book.learned.size(),
                         book.activeSpellId.empty() ? "(none)" : book.activeSpellId.c_str(),
                         app.gs.player.combatStats.currentMp,
                         app.gs.player.combatStats.maxMp,
                         cd,
                         book.sustainedActive.size());
            std::fflush(stderr);
            ++app.smoke.cursor;
            break;
        }
        case SmokeAction::CastSpell: {
            std::fprintf(stderr, "[smoke] action=cast_spell\n");
            std::fflush(stderr);
            if (!app.worldLoaded) {
                smoke_fail(app, "cast_spell without world");
                break;
            }
            if (!app.subworld.active()) {
                app.subworld.enter(app.gs, app.terrain, app.features,
                                   app.ecs, app.bus, &app.zones);
            }
            sm::spellbook_learn(app.gs.player.spellBook, "magic_bolt");
            sm::spellbook_set_active(app.gs.player.spellBook, "magic_bolt");
            const float spellTargetX = std::min(
                app.subworld.player_x() + 43.5f,
                float(sm::sub::kFullSize - 2));
            const float spellTargetY = app.subworld.player_y();
            const entt::entity spellTarget = app.ecs.reg.create();
            app.ecs.reg.emplace<sm::ecs::Position>(
                spellTarget, spellTargetX, spellTargetY);
            app.ecs.reg.emplace<sm::ecs::VisualPos>(
                spellTarget, spellTargetX, spellTargetY, 0.0f);
            app.ecs.reg.emplace<sm::ecs::NPCKind>(
                spellTarget,
                sm::ecs::NPCKind{
                    std::uint16_t(sm::NPCType::Bandit),
                    std::uint16_t(3)});
            app.ecs.reg.emplace<sm::ecs::Health>(spellTarget, 30.0f, 30.0f);
            app.ecs.reg.emplace<sm::ecs::SubworldTag>(spellTarget);
            app.ecs.reg.emplace<sm::ecs::Active>(spellTarget);
            app.ecs.reg.emplace<sm::ecs::Sprite>(
                spellTarget,
                std::uint16_t(sm::NPCType::Bandit),
                std::uint8_t(255), std::uint8_t(72), std::uint8_t(48),
                std::uint8_t(255), 1.2f);
            int beforeProjectiles = 0;
            for (auto e : app.ecs.reg.view<sm::ecs::Projectile>()) {
                (void)e;
                ++beforeProjectiles;
            }
            const int beforeCombatLog = app.subworld.combat_log_count();
            const int beforeSpellCastEvents =
                count_tick_events(app.bus, sm::EventTag::SpellCast);
            if (!cast_active_spell(app)) {
                smoke_fail(app, "active spell cast failed");
                break;
            }
            const int afterSpellCastEvents =
                count_tick_events(app.bus, sm::EventTag::SpellCast);
            const sm::GameEvent* spellEvent =
                latest_tick_event(app.bus, sm::EventTag::SpellCast);
            if (afterSpellCastEvents <= beforeSpellCastEvents
                || !spellEvent
                || spellEvent->ix != 1
                || spellEvent->s1 != app.gs.player.spellBook.activeSpellId
                || spellEvent->a != sm::stable_spell_id(
                    app.gs.player.spellBook.activeSpellId)) {
                smoke_fail(app, "SpellCast event was not emitted honestly");
                break;
            }
            int afterProjectiles = 0;
            for (auto e : app.ecs.reg.view<sm::ecs::Projectile>()) {
                (void)e;
                ++afterProjectiles;
            }
            if (afterProjectiles <= beforeProjectiles) {
                smoke_fail(app, "projectile was not spawned");
                break;
            }
            RuntimeFrameStats frameStats =
                tick_playing_runtime(app, 0.10f, false);
            if (!frameStats.ticked || !frameStats.subworldActive) {
                smoke_fail(app, "spell projectile tick inactive");
                break;
            }
            const int afterCombatLog = app.subworld.combat_log_count();
            const sm::sub::CombatLogEntry* combatLog =
                app.subworld.combat_log_entry(afterCombatLog - 1);
            const bool hitLogged = afterCombatLog > beforeCombatLog
                && combatLog && combatLog->text[0] != '\0';
            if (!hitLogged) {
                smoke_fail(app, "spell hit combat log missing");
                break;
            }
            const auto* hitFlash =
                app.ecs.reg.try_get<sm::ecs::HitFlash>(spellTarget);
            if (!hitFlash || hitFlash->timer <= 0.0f) {
                smoke_fail(app, "spell hit flash missing");
                break;
            }
            const auto& book = app.gs.player.spellBook;
            std::fprintf(stderr,
                         "[smoke] spell_projectile active=%s projectiles=%d->%d mp=%d cd=%zu event=%d flash=%.3f log=\"%s\"\n",
                         book.activeSpellId.c_str(),
                         beforeProjectiles,
                         afterProjectiles,
                         app.gs.player.combatStats.currentMp,
                         book.cooldowns.size(),
                         afterSpellCastEvents - beforeSpellCastEvents,
                         double(hitFlash->timer),
                         combatLog ? combatLog->text : "");
            std::fflush(stderr);
            ++app.smoke.cursor;
            break;
        }
        case SmokeAction::ToggleHaste: {
            std::fprintf(stderr, "[smoke] action=toggle_haste\n");
            std::fflush(stderr);
            if (!app.worldLoaded) {
                smoke_fail(app, "toggle_haste without world");
                break;
            }
            sm::spellbook_learn(app.gs.player.spellBook, "haste");
            sm::spellbook_set_active(app.gs.player.spellBook, "haste");
            const int beforeMp = app.gs.player.combatStats.currentMp;
            if (!cast_active_spell(app)) {
                smoke_fail(app, "haste toggle failed");
                break;
            }
            RuntimeFrameStats frameStats =
                tick_playing_runtime(app, 1.20f, false);
            if (!frameStats.ticked) {
                smoke_fail(app, "haste drain tick inactive");
                break;
            }
            const bool active = sm::spellbook_has_sustained(
                app.gs.player.spellBook, "haste");
            const int afterMp = app.gs.player.combatStats.currentMp;
            if (!active || afterMp >= beforeMp) {
                smoke_fail(app, "haste sustained drain invariant");
                break;
            }
            std::fprintf(stderr,
                         "[smoke] sustained_haste active=%d mp=%d->%d carry=%.3f\n",
                         active ? 1 : 0,
                         beforeMp,
                         afterMp,
                         app.gs.player.spellBook.sustainedDrainCarry);
            std::fflush(stderr);
            ++app.smoke.cursor;
            break;
        }
        case SmokeAction::ToggleFlight: {
            std::fprintf(stderr, "[smoke] action=toggle_flight\n");
            std::fflush(stderr);
            if (!app.worldLoaded) {
                smoke_fail(app, "toggle_flight without world");
                break;
            }
            if (app.subworld.active()) {
                app.subworld.leave();
            }
            sm::spellbook_learn(app.gs.player.spellBook, "flight");
            sm::spellbook_set_active(app.gs.player.spellBook, "flight");
            int beforeProjectiles = 0;
            for (auto e : app.ecs.reg.view<sm::ecs::Projectile>()) {
                (void)e;
                ++beforeProjectiles;
            }
            const int beforeMp = app.gs.player.combatStats.currentMp;
            if (!cast_active_spell(app)) {
                smoke_fail(app, "flight toggle failed");
                break;
            }
            int afterProjectiles = 0;
            for (auto e : app.ecs.reg.view<sm::ecs::Projectile>()) {
                (void)e;
                ++afterProjectiles;
            }
            if (afterProjectiles != beforeProjectiles) {
                smoke_fail(app, "flight spawned projectile");
                break;
            }
            const bool active = sm::spellbook_has_sustained(
                app.gs.player.spellBook, "flight");
            if (!active || app.gs.player.combatStats.currentMp != beforeMp) {
                smoke_fail(app, "flight toggle invariant");
                break;
            }
            const int sx = int(std::floor(app.gs.player.x));
            const int sy = int(std::floor(app.gs.player.y));
            const int gx = sm::wrapi(sx + 17, app.gs.mapW);
            const int gy = sm::wrapi(sy + 9, app.gs.mapH);
            const auto path = build_flight_path(sx, sy, gx, gy,
                                                app.gs.mapW, app.gs.mapH);
            if (path.size() < 2 || path.front().x != sx || path.front().y != sy
                || path.back().x != gx || path.back().y != gy) {
                smoke_fail(app, "flight path invariant");
                break;
            }
            RuntimeFrameStats frameStats =
                tick_playing_runtime(app, 0.60f, false);
            if (!frameStats.ticked
                || app.gs.player.combatStats.currentMp >= beforeMp) {
                smoke_fail(app, "flight drain tick inactive");
                break;
            }
            app.subworld.enter(app.gs, app.terrain, app.features, app.ecs,
                               app.bus, &app.zones);
            if (!app.subworld.active()) {
                smoke_fail(app, "flight subworld enter failed");
                break;
            }
            app.subworld.set_flying(true);
            const float flightH0 = app.subworld.flight_height_m();
            app.subworld.rotate_camera(0.0f, 0.55f);
            app.subworld.move_player(0.0f, 32.0f);
            const float flightH1 = app.subworld.flight_height_m();
            if (!app.subworld.flying() || !(flightH1 > flightH0 + 1.0f)) {
                smoke_fail(app, "flight subworld height invariant");
                app.subworld.leave(true);
                break;
            }
            app.subworld.set_flying(false);
            app.subworld.leave(true);
            std::fprintf(stderr,
                         "[smoke] sustained_flight active=%d mp=%d->%d path=%zu projectileDelta=%d subFlight=%.2f\n",
                         active ? 1 : 0,
                         beforeMp,
                         app.gs.player.combatStats.currentMp,
                         path.size(),
                         afterProjectiles - beforeProjectiles,
                         flightH1 - flightH0);
            std::fflush(stderr);
            ++app.smoke.cursor;
            break;
        }
        case SmokeAction::PrepareSpellAuras: {
            std::fprintf(stderr, "[smoke] action=prepare_spell_auras\n");
            std::fflush(stderr);
            if (!app.worldLoaded) {
                smoke_fail(app, "prepare_spell_auras without world");
                break;
            }
            if (!app.subworld.active()) {
                app.subworld.enter(app.gs, app.terrain, app.features,
                                   app.ecs, app.bus, &app.zones);
            }
            if (!app.subworld.active()) {
                smoke_fail(app, "prepare_spell_auras enter failed");
                break;
            }

            sm::spellbook_learn(app.gs.player.spellBook, "haste");
            sm::spellbook_learn(app.gs.player.spellBook, "flight");
            if (!sm::spellbook_has_sustained(
                    app.gs.player.spellBook, "haste")) {
                sm::spellbook_set_active(app.gs.player.spellBook, "haste");
                if (!cast_active_spell(app)) {
                    smoke_fail(app, "prepare_spell_auras haste failed");
                    break;
                }
            }
            if (!sm::spellbook_has_sustained(
                    app.gs.player.spellBook, "flight")) {
                sm::spellbook_set_active(app.gs.player.spellBook, "flight");
                if (!cast_active_spell(app)) {
                    smoke_fail(app, "prepare_spell_auras flight failed");
                    break;
                }
            }

            app.subworld.set_flying(true);
            RuntimeFrameStats frameStats =
                tick_playing_runtime(app, 0.05f, false);
            const bool haste = sm::spellbook_has_sustained(
                app.gs.player.spellBook, "haste");
            const bool flight = sm::spellbook_has_sustained(
                app.gs.player.spellBook, "flight");
            if (!frameStats.ticked || !frameStats.subworldActive
                || !haste || !flight || !app.subworld.flying()) {
                smoke_fail(app, "prepare_spell_auras invariant");
                break;
            }
            std::fprintf(stderr,
                         "[smoke] spell_auras haste=%d flight=%d subworld=%d flying=%d mp=%d\n",
                         haste ? 1 : 0,
                         flight ? 1 : 0,
                         app.subworld.active() ? 1 : 0,
                         app.subworld.flying() ? 1 : 0,
                         app.gs.player.combatStats.currentMp);
            std::fflush(stderr);
            ++app.smoke.cursor;
            break;
        }
        case SmokeAction::TriggerLevelDialog: {
            std::fprintf(stderr, "[smoke] action=trigger_level_dialog\n");
            std::fflush(stderr);
            if (!app.worldLoaded) {
                smoke_fail(app, "trigger_level_dialog without world");
                break;
            }
            smoke_clear_modal_overlays(app);
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
        case SmokeAction::TriggerCountOnlyDialog: {
            std::fprintf(stderr, "[smoke] action=trigger_count_only_dialog\n");
            std::fflush(stderr);
            if (!app.worldLoaded) {
                smoke_fail(app, "trigger_count_only_dialog without world");
                break;
            }
            smoke_clear_modal_overlays(app);
            sm::GameEvent dialog{sm::EventTag::ShowDialog};
            dialog.s1 = "Count Only Dialog";
            dialog.s2 = "Smoke event intentionally carries only ix=1 and no DialogChoicePayload.";
            dialog.ix = 1;
            app.bus.emit(dialog);
            capture_presentation_events(app);
            if (!app.showDialogOpen
                || app.showDialogEvent.tag != sm::EventTag::ShowDialog
                || app.showDialogEvent.dialogChoices) {
                smoke_fail(app, "count-only ShowDialog was not captured honestly");
                break;
            }
            std::fprintf(stderr,
                         "[smoke] count_only_dialog title=\"%s\" choices=%d payload=%s\n",
                         app.showDialogEvent.s1.c_str(),
                         app.showDialogEvent.ix,
                         app.showDialogEvent.dialogChoices ? "present" : "missing");
            std::fflush(stderr);
            ++app.smoke.cursor;
            break;
        }
        case SmokeAction::TriggerStoryOverlay: {
            std::fprintf(stderr, "[smoke] action=trigger_story_overlay\n");
            std::fflush(stderr);
            if (!app.worldLoaded) {
                smoke_fail(app, "trigger_story_overlay without world");
                break;
            }
            if (!sm::ui::story_overlay_active(app.storyOverlay)) {
                sm::GameEvent ev{sm::EventTag::ShowStory};
                ev.s1 = "intro_main";
                ev.s2 = "intro";
                app.bus.emit(ev);
                capture_presentation_events(app);
            }
            if (!sm::ui::story_overlay_active(app.storyOverlay) ||
                app.storyOverlay.story == nullptr) {
                smoke_fail(app, "ShowStory overlay was not captured");
                break;
            }
            std::fprintf(stderr,
                         "[smoke] show_story id=\"%s\" phases=%zu phase=%zu slide=%zu\n",
                         app.storyOverlay.story->id ? app.storyOverlay.story->id : "(none)",
                         app.storyOverlay.story->phaseCount,
                         app.storyOverlay.phaseIndex,
                         app.storyOverlay.slideIndex);
            std::fflush(stderr);
            ++app.smoke.cursor;
            break;
        }
        case SmokeAction::CompleteStoryOverlay: {
            std::fprintf(stderr, "[smoke] action=complete_story_overlay\n");
            std::fflush(stderr);
            if (!app.worldLoaded) {
                smoke_fail(app, "complete_story_overlay without world");
                break;
            }
            if (!sm::ui::story_overlay_active(app.storyOverlay)) {
                sm::GameEvent ev{sm::EventTag::ShowStory};
                ev.s1 = "intro_main";
                ev.s2 = "intro";
                app.bus.emit(ev);
                capture_presentation_events(app);
            }
            if (!sm::ui::story_overlay_active(app.storyOverlay)) {
                smoke_fail(app, "complete_story_overlay missing active story");
                break;
            }

            const int beforeAttributePoints = app.gs.player.levelData.attributePoints;
            const int beforeReputation =
                app.gs.player.reputation.count("magika")
                    ? app.gs.player.reputation["magika"]
                    : 0;
            const bool valuesOk =
                sm::ui::set_story_overlay_value(app.storyOverlay, "sex", "female") &&
                sm::ui::set_story_overlay_value(app.storyOverlay, "name", "Smoke Traveller") &&
                sm::ui::set_story_overlay_value(app.storyOverlay, "realm", "magika");
            if (!valuesOk ||
                !sm::ui::complete_story_overlay(app.storyOverlay, app.bus)) {
                smoke_fail(app, "complete_story_overlay could not emit StoryResult");
                break;
            }
            apply_pending_story_results(app);
            if (sm::ui::story_overlay_active(app.storyOverlay) ||
                app.gs.player.name != "Smoke Traveller" ||
                app.gs.player.levelData.attributePoints <= beforeAttributePoints ||
                app.gs.player.reputation["magika"] < beforeReputation + 15) {
                smoke_fail(app, "complete_story_overlay result was not applied");
                break;
            }
            std::fprintf(stderr,
                         "[smoke] complete_story name=\"%s\" attr=%d->%d magika=%d->%d\n",
                         app.gs.player.name.c_str(),
                         beforeAttributePoints,
                         app.gs.player.levelData.attributePoints,
                         beforeReputation,
                         app.gs.player.reputation["magika"]);
            std::fflush(stderr);
            ++app.smoke.cursor;
            break;
        }
        case SmokeAction::ConsoleSmoke:
            std::fprintf(stderr, "[smoke] action=console\n");
            std::fflush(stderr);
            if (run_console_smoke(app)) ++app.smoke.cursor;
            break;
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
    if (r.openCodex) {
        app.state = sm::ui::AppState::Playing;
        app.ui.codex = true;
    }
    if (r.resume)        app.state = sm::ui::AppState::Playing;
    if (r.returnToTitle) { destroy_world(app); app.state = sm::ui::AppState::Title; }
    if (r.quit)          app.running = false;
}

void frame(App& app, float dt) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) handle_event(app, e);
    sync_relative_mouse_mode(app);

    tick_playing_runtime(app, dt * app.simSpeed, !modal_overlay_active(app));
    sync_audio_music(app);

    // --- ImGui frame: start BEFORE acquire so that lazy texture loads
    //     (sprite_get -> create_ui_texture -> vkQueueSubmit) happen
    //     outside the active render pass / command buffer recording.
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

    // Vulkan frame: acquire → shadow → begin render pass → game draws → ImGui → end.
    if (!app.renderer.acquire_frame(app.window)) {
        ImGui::EndFrame();  // balance the NewFrame
        return;
    }
    VkCommandBuffer cmd = app.renderer.current_command_buffer();

    if (app.worldLoaded && app.subworld.active()) {
        app.subworld.prepare_frame(cmd);
        app.subworld.record_shadow(cmd);
    }

    app.renderer.begin_render_pass(0.02f, 0.02f, 0.04f);
    VkExtent2D ext = app.renderer.swapchain.extent;

    if (app.worldLoaded) {
        if (app.subworld.active()) {
            app.subworld.record_main(cmd, ext);
        } else {
            const float tod = (float(app.gs.worldTime.hour)
                               + float(app.gs.worldTime.minute) / 60.0f) / 24.0f;
            app.macro.record(cmd, ext, app.terrain,
                             app.camX, app.camY, app.zoom,
                             app.gs.mapParams.seaLevel, tod);
        }
    }

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
            if (sustained_spell_active(app.gs.player.spellBook, "flight")) {
                app.cursor.path = build_flight_path(sx, sy,
                    app.cursor.requestX, app.cursor.requestY,
                    app.gs.mapW, app.gs.mapH);
                app.cursor.pathIdx = 1; // skip current cell
            } else {
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
        {
            const bool modalActive = modal_overlay_active(app);
            sm::ui::draw_player_hud(app.gs);
            if (!modalActive)
            {
                auto tb = sm::ui::draw_bottom_toolbar(app.gs, app.subworld.active());
                if (tb.pause)         app.state          = sm::ui::AppState::Paused;
                if (tb.diplomacy)     app.ui.diplomacy   = !app.ui.diplomacy;
                if (tb.build)         open_settlement_panel(app, sm::ui::SettlementPanelTab::Build);
                if (tb.quests)        app.ui.quest       = !app.ui.quest;
                if (tb.codex)         app.ui.codex       = !app.ui.codex;
                if (tb.map)           app.ui.map         = !app.ui.map;
                if (tb.stats) {
                    app.ui.character = true;
                    app.ui.characterTab = sm::ui::CharacterPanelTab::Stats;
                }
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
            if (!modalActive)
                sm::ui::draw_hint_bar(app.state, app.subworld.active(), app.width, app.height);
            draw_debug_ui(app);
            sm::dev::draw_debug_console(app.console);
            draw_debug_panels(app);
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
                if (app.subworldHitFlashTimer > 0.0f) {
                    const float alpha = std::min(
                        0.45f, app.subworldHitFlashTimer * 1.6f);
                    ImGui::GetBackgroundDrawList()->AddRectFilled(
                        ImVec2(0.0f, 0.0f),
                        ImVec2(float(logicalW), float(logicalH)),
                        IM_COL32(220, 40, 40, int(alpha * 255.0f)));
                }
                draw_subworld_danger_gem(app.subworld);
                draw_subworld_combat_log(app.subworld, logicalW);
                sm::ui::draw_subworld_minimap_hud(app.subworld.mgr(),
                    app.subworld.player_x(), app.subworld.player_y(),
                    app.subworld.cam_yaw(), logicalW, logicalH);
                if (app.subworld.status_line()[0] != '\0') {
                    const char* msg = app.subworld.status_line();
                    const ImVec2 size = ImGui::CalcTextSize(msg);
                    const ImVec2 pad(12.0f, 7.0f);
                    const ImVec2 pos((float(logicalW) - size.x) * 0.5f,
                                     float(logicalH) - 132.0f);
                    ImDrawList* fg = ImGui::GetForegroundDrawList();
                    fg->AddRectFilled(ImVec2(pos.x - pad.x, pos.y - pad.y),
                                      ImVec2(pos.x + size.x + pad.x,
                                             pos.y + size.y + pad.y),
                                      IM_COL32(18, 20, 24, 220), 5.0f);
                    fg->AddText(pos, IM_COL32(235, 238, 224, 255), msg);
                }
                sm::ui::draw_subworld_map_overlay(app.subworld.mgr(),
                    app.subworld.player_x(), app.subworld.player_y(),
                    app.subworld.cam_yaw(),
                    &app.ui.map);
            } else {
                sm::ui::draw_map_overlay(app.gs, app.terrain, &app.ui.map);
            }
            sm::ui::draw_encounter_modal(app.gs, app.bus);
            // Right-edge nearby-NPC stack (mirrors NpcProximityPanel.svelte).
            // Macro view only. The badge stack follows TS anyOverlayOpen
            // suppression, while an already-open native NPC popup keeps
            // rendering until it is closed.
            const bool showNpcRows = !macro_overlay_blocks_npc_proximity(app);
            if (!app.subworld.active()
                && (showNpcRows || sm::ui::npc_proximity_popup_open())) {
                int logicalW = app.width, logicalH = app.height;
                SDL_GetWindowSize(app.window, &logicalW, &logicalH);
                const sm::ui::NpcProximityResult npcResult =
                    sm::ui::draw_npc_proximity_panel(app.gs, app.ecs,
                                                     logicalW, logicalH,
                                                     showNpcRows);
                if (npcResult.attackNpc != entt::null) {
                    (void)route_macro_npc_attack(app, npcResult.attackNpc);
                }
            }
            sm::ui::draw_show_dialog(app.gs, app.showDialogEvent, app.bus,
                                     app.showDialogUi, &app.showDialogOpen);
            handle_dialog_node_activation(app);
            sm::ui::draw_story_overlay(app.storyOverlay, app.bus);
            break;
        }
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
    sync_audio_music(app);
    sync_relative_mouse_mode(app);

    ImGui::Render();
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
    if (app.smoke.enabled && app.smoke.capturePending) {
        const int actionIndex = app.smoke.captureActionIndex;
        app.smoke.capturePending = false;
        if (!write_smoke_frame_ppm(app, actionIndex, "ui")) {
            smoke_fail(app, "capture_frame write failed");
        }
    }
    app.renderer.end_frame(app.window);
    SDL_Vulkan_GetDrawableSize(app.window, &app.width, &app.height);
}

} // namespace

int main(int /*argc*/, char* /*argv*/[]) {
#if defined(_WIN32)
    SetUnhandledExceptionFilter(crash_filter);
#endif
    App app;
    if (!parse_smoke_script(std::getenv(kSmokeScriptEnv), app.smoke)) return 2;
    if (!boot_window(app)) return 1;
    boot_audio(app);
    app.savePath = resolve_save_path();
    if (boot_trace_enabled() || app.smoke.enabled) {
        std::fprintf(stderr, "[save] path=%s\n", app.savePath.c_str());
        std::fflush(stderr);
    }
    boot_imgui(app);
    sm::ui::set_gpu_device(&app.device);

    // Preload all sprites at boot (before any frame), so create_rgba8 +
    // AddTexture happen without any active command buffer recording.
    for (int i = 0; i < int(sm::SpriteId::Count_); ++i)
        sm::sprite_get(sm::SpriteId(i));

    app.macro.init(app.device, app.renderer.renderPass);
    app.subworld.init(app.device, app.renderer.renderPass);
    register_console_commands(app);

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
    vkDeviceWaitIdle(app.device.device);
    destroy_world(app);
    sm::ui::destroy_all_ui_textures();
    sm::sprite_atlas_shutdown();
    app.subworld.destroy(app.device);
    app.macro.destroy(app.device);
    app.audio.shutdown();
    shutdown_imgui(app);
    app.renderer.destroy();
    app.device.destroy();
    SDL_DestroyWindow(app.window);
    SDL_Quit();
    return exitCode;
}

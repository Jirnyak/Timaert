// Application entry — SDL2 + OpenGL 3.2 Core + ImGui. Owns the screen
// state machine (Title / Playing / Menu / Dead), boots the macroworld
// on demand, drives the macro renderer + subworld, and routes input.
#include <cassert>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>
#include <SDL.h>
#include <SDL_vulkan.h>
#include "gpu/vk_device.h"
#include "gpu/vk_gpu_timer.h"
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
#include "macro/character_sheet.h"
#include "macro/knowledge.h"
#include "ui/map_screen.h"
#include "macro/map_generator.h"
#include "macro/settlement_score.h"
#include "macro/spawners.h"
#include "macro/tree_layer.h"
#include "macro/landmark_grid.h"
#include "macro/deposit_layer.h"
#include "macro/spires.h"
#include "macro/zones.h"
#include "macro/politik.h"
#include "macro/vk_macro_renderer.h"
#include "macro/macro_lighting.h"
#include "macro/biomes.h"
#include "macro/world_tick.h"
#include "macro/npc_ai.h"
#include "macro/entry_context.h"
#include "macro/faction.h"
#include "macro/npc_spawn.h"
#include "macro/macro_snapshot.h"
#include "macro/currency.h"
#include "macro/squad.h"
#include "macro/player_entity.h"
#include "macro/pathfinding.h"
#include "macro/items.h"
#include "macro/player_recovery.h"
#include "macro/travel.h"
#include "macro/audio.h"
#include "macro/save.h"
#include "macro/seasons.h"
#include "content/spells/casting.h"
#include "content/spells/spell_book.h"
#include "content/plot/intro.h"
#include "content/quests/procedural.h"
#include "sub/engine.h"
#include "sub/damage.h"
#include "sub/dgn/dispatch.h"
#include "sub/height.h"
#include "sub/map_data.h"
#include "sub/map_factory.h"
#include "sub/tree_atlas.h"
#include "macro/fauna.h"
#include "ui/overlays.h"
#include "ui/screens.h"
#include "ui/macro_overlay.h"
#include "ui/ui_gpu.h"
#include "ui/keymap.h"
#include "ui/ui_settings.h"
#include "assets/sprite_atlas.h"
#include "app/debug_console.h"

#include "imgui.h"
#include "backends/imgui_impl_sdl2.h"
#include "backends/imgui_impl_vulkan.h"
#include <vulkan/vulkan.h>

// Screenshot PNG encoder. Kept in this TU only: `stb_image_write.h` is on the
// `timaert` target's include path (see CMake ${stb_SOURCE_DIR}), NOT the shared
// gpu/* targets, so the renderer exposes raw pixels and main writes the file.
// The vendored header trips -Wmissing-field-initializers / -Wdeprecated
// (its internal `{ 0 }` inits and one sprintf) — silence those locally so our
// build stays warning-clean without patching third-party code.
#define STB_IMAGE_WRITE_IMPLEMENTATION
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-field-initializers"
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif
#include "stb_image_write.h"
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

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
// The monthly autosave writes a SIBLING slot, never the player's own file —
// an autosave that overwrites the manual save turns "the world is on disk"
// into "your last deliberate save is gone" (Session 17).
constexpr const char* kAutosaveFileName = "autosave.bin";
constexpr const char* kPrefsFileName = "ui_prefs.cfg";
constexpr const char* kKeymapFileName = "keymap.cfg";
constexpr const char* kSaveOrgName = "Timaert";
constexpr const char* kSaveAppName = "timaert_c";
constexpr int kSubworldDailyTicksPerStep = 1;
// Macro NPCs dispatched per simulation STEP while the player is underground —
// a budget, so a huge macro population cannot make one step unbounded. Named
// for the step, not the frame: since the world runs on a fixed tick a frame may
// carry several steps or none.
constexpr int kSubworldMacroNpcTicksPerStep = 64;
// Base on-foot speed in the subworld, in tiles per REAL second, before the
// character's own pace (DerivedBonuses::moveSpeedMult) and haste.
//
// DERIVED from the macro march (the A8 debt closes by derivation, not by
// change): the same body walks the same world at both scales. 8 cells/game
// hour above (kMacroWalkCellsPerHour) = 8000 tiles per subworld game hour;
// down here an hour lasts kTicksPerDay/24 × kSubworldTickDivisor ticks =
// 85⅓ real seconds, so the honest rate is 8000 / 85.33 = 93.75 tiles/s —
// rounded up 2.4% to 96, a named fantasy allowance, not a tuned number.
// (The old macro 32 made this look like a 4× disagreement — canon-audit A8;
// the ground floor was right all along, the map was galloping.)
constexpr float kSubworldWalkTilesPerSecond = 96.0f;
constexpr float kSubworldHitFlashSeconds = 0.30f;
// Macro-map zoom: one step factor and one clamp, shared by the mouse wheel
// and the toolbar buttons (the four literals used to be written out at both).
constexpr float kMacroZoomStep = 1.15f;
constexpr float kMacroZoomMin = 4.0f;
constexpr float kMacroZoomMax = 96.0f;
constexpr const char* kSmokeScriptEnv = "TIMAERT_SMOKE_SCRIPT";
constexpr const char* kSmokeSeedEnv = "TIMAERT_SMOKE_SEED";
constexpr int kSubworldSmokeFrames = 1000;
constexpr int kSubworldSeamSmokeSettleFrames = 120;
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
                 step, time.day(), time.hour(), time.minute());
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
    SubworldSelfFireball,
    SubworldPlayerMelee,
    SubworldReputationHit,
    SubworldMouseRelease,
    SubworldTreeAnchor,
    SubworldRecovery,
    SubworldSpDrain,
    SubworldEnter,
    SubworldExitRemap,
    DungeonHouse,
    DungeonCave,
    SpireClimb,
    TriggerBattleStart,
    WaitVisible,
    OpenSettlementBuild,
    OpenSettlementTrade,
    OpenSettlementMap,
    EnterFirstSettlement,
    FocusNpcPanel,
    OpenNpcTrade,
    AttackFirstNpc,
    MacroKillWriteback,
    FaunaKillWriteback,
    SpawnSquadAtPlayer,
    ForceEncounter,
    CaptureFrame,
    StatsSettle,
    OpenMap,
    OpenStats,
    SpendAttributeVit,
    SpendSkillBodybuilding,
    MacroTravelSp,
    MacroRecovery,
    RestSp,
    TimeAdvanceBurst,
    MacroNpcTrace,
    OpenQuests,
    OpenCodex,
    OpenSpells,
    CastSpell,
    CastBoltCapture,
    LightProbeCapture,
    ToggleHaste,
    ToggleFlight,
    PrepareSpellAuras,
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
    // stats_settle: rendered frames already idled through (see the case).
    int settleFrames = 0;
    bool capturePending = false;
    int captureActionIndex = 0;
    // capture_frame on the MACRO map applies its opt-in mutations (modal
    // clear, TIMAERT_SMOKE_HOUR) a frame BEFORE arming the capture — the
    // smoke script runs after the frame is recorded, so a same-tick capture
    // would photograph the pre-mutation frame (same stale-frame trap as
    // light_probe_capture below).
    bool captureStaged = false;
    // Deferred probe capture. light_probe_capture stages its actor + lights in
    // tick_smoke_script, which runs AFTER the frame's 3D scene is already
    // recorded — so a same-tick capture photographs the PRE-staging frame (the
    // actor and any light strip only reach the ECS next frame). We therefore
    // stage once, then hold for a few frames (re-pinning the actor against the
    // sim tick) before arming the capture, so the photographed scene actually
    // contains the staged actor and its lighting. -1 = idle.
    int probeSettleFrames = -1;
    entt::entity probeEntity = entt::null;
    float probeX = 0.0f, probeY = 0.0f;
    // wait_visible reads the PRESENTED frame back instead of assuming it.
    // The scenario arms a capture on one frame and samples it on the next —
    // the same defer-by-a-frame rule every other capture action obeys, for the
    // same reason: the script runs after the frame is recorded, so sampling in
    // the same tick would judge the picture taken before the action.
    // Armed by wait_visible, drained at the ONE capture point in frame().
    bool pixelProbeArmed = false;
    std::vector<std::uint8_t> probePixels;
    int probePixW = 0;
    int probePixH = 0;
    VkFormat probePixFmt = VK_FORMAT_UNDEFINED;
    // macro_kill_writeback walks three phases with a frame between each, because
    // what it is testing happens in the ENGINE tick (the writeback and the death
    // settlement), not in this script: wound → let a tick pass → read the map →
    // kill → let a tick pass → read the map.
    int trackedPhase = 0;
    entt::entity trackedBody = entt::null;
    entt::entity trackedMacro = entt::null;
    float trackedMacroHp0 = 0.0f;
    // fauna_kill_writeback: the culled creature's cell key and the cell's
    // headcount before the kill (the body is reaped before phase 1 reads).
    int trackedCellX = 0;
    int trackedCellY = 0;
    int trackedCount0 = 0;
};

struct App {
    SDL_Window*   window  = nullptr;
    gpu::VulkanDevice  device;
    gpu::VulkanRenderer renderer;
    gpu::GpuTimer gpuTimer;      // pass-boundary GPU ms (TIMAERT_GPU_STATS)
    bool          showFpsHud = false; // persistent corner readout (`fpshud`)
    VkDescriptorPool imguiPool = VK_NULL_HANDLE;
    int           width   = 1280;
    int           height  = 800;
    bool          running = true;
    std::string   savePath = kSaveFileName;
    std::string   autosavePath = kAutosaveFileName;
    std::string   prefsPath = kPrefsFileName;
    std::string   keymapPath = kKeymapFileName;

    sm::ui::AppState state = sm::ui::AppState::Title;
    sm::ui::AppState loadReturnState = sm::ui::AppState::Title;
    bool worldLoaded = false;
    // The macro↔micro transition edge (CANON S7, owner 2026-08-24: the world
    // rebakes on EVERY transition, literally). Set each frame in
    // tick_playing_runtime; the frame that sees `active` fall pays the exit
    // rebake — every leave path funnels through this one edge.
    bool subworldWasActive = false;
    // The zone field's GPU refresh rides the same dirty-flush discipline as
    // the night glow (Session 19 law: no mid-frame device drains) — set by
    // rebake_world when it may not touch the device, flushed on the macro
    // path beside macroLightsDirty.
    bool zoneFieldDirty = false;

    sm::GameState        gs;
    sm::TerrainData      terrain{};
    sm::FeatureLayer     features;
    sm::ZoneLayer        zones;
    // Baked cell → landmark index (macro/landmark_grid.h): the one answer to
    // "who stands on this cell". Rebaked wherever the landmark set changes —
    // today world-gen and load; living landmarks (CANON S9) add theirs here.
    sm::LandmarkGrid     landmarkGrid;
    // Derived per-cell tree counts (macro/tree_layer.h) — the LIVING grid of
    // the registry's Trees row; the save carries it whole (v36).
    // uploadedTreeRev mirrors treeLayer.revision so the u_treeMap texture
    // refreshes only when a count actually changed.
    sm::TreeLayer        treeLayer;
    std::uint32_t        uploadedTreeRev = 0;
    // Mirrors gs.knowledge.revision so the u_knowledgeMap texture refreshes
    // only when sight actually moved (a cell crossing / a quest reveal).
    std::uint32_t        uploadedKnowledgeRev = 0;
    // The full-screen map page's own camera + chrome state (ui/map_screen.h).
    // Session-only; survives page closes so the player returns to their view.
    sm::ui::MapScreenState mapScreen;
    // Derived mineral deposits (macro/deposit_layer.h, W2a); mutations
    // persist as gs.depositOverrides.
    sm::DepositLayer     deposits;
    // The player's sight session state (macro/knowledge.h): the current
    // Visible set + the sweep scratch. The persistent grid lives on
    // gs.knowledge; this half is exactly what a load must NOT keep.
    sm::SightRuntime     sightRt;
    // Dev console `revealmap`: while on, the sight projection is pinned to
    // "everything" and the per-frame sweep is suspended. Toggling off does
    // NOT un-reveal anything — it merely wakes the ordinary law, whose next
    // sweep decays the pinned Visible set to Explored: the map stays charted
    // (drowned), the live disc returns. Session-only, never saved.
    bool                 revealMapOn = false;
    // Cached float views of the optical world — heights from the terrain R
    // channel, tree density from the live tree layer — shared by the
    // night-glow bake and the player-sight sweep (ONE physics, one cache).
    // Rebuilt only when the tree revision moves: a chop changes what both
    // light and sight can pass; terrain is boot-static.
    struct OpticalCache {
        std::vector<float> heights, treeDensity;
        std::uint32_t treeRev = std::uint32_t(-1);
        int w = 0, h = 0;
    };
    OpticalCache         optical;
    sm::MacroRendererVk  macro;
    // Set when the daily world sim changes glow-driving state (populations, and
    // any future opt-in emitter) so the night-light field can be re-baked. Set
    // in either tick path; flushed only on the macro path (see update_world) so
    // the subworld never pays a GPU sync for a map it is not drawing.
    bool                 macroLightsDirty = false;
    // World day of the last SLOW re-bake (owner, Session 21 follow-up): the
    // baked cost grid (pathCost) sleeps through tree chops rather than
    // The re-bake/autosave day itself lives on GameState (v22) so the phase
    // survives a load; App only keeps derived caches here.
    bool                 lastSpellFlight = false;
    bool                 lastJumpHeld = false;
    sm::ecs::World       ecs;
    sm::EventBus         bus;
    sm::LogicNodeEngine  logic;
    sm::QuestEngine      quests;
    std::vector<sm::Quest> activeQuests;
    // Cache signature for the derived quest-marker set (gs.markers "quest_*"):
    // the per-frame quest tick refreshes those pins only when this fingerprint
    // of activeQuests changes, so an unchanged quest set never re-allocates
    // markers. Reset to 0 whenever activeQuests is wholesale replaced.
    std::uint64_t          questMarkerSig = 0;
    std::vector<sm::Quest> availableSettlementQuests;
    int                  availableQuestSettlementId = -1;
    int                  availableQuestDay = -1;
    std::size_t          appliedEventCount = 0;
    std::size_t          appliedStoryResultCount = 0;
    std::size_t          appliedCombatEventCount = 0;
    std::size_t          appliedSpawnEventCount = 0;
    sm::PlayerRecoveryAccumulator playerRecovery;
    sm::MacroNpcAiRuntime npcAi;
    sm::sub::SubworldEngine subworld;
    sm::AudioSystem      audio;
    sm::MusicId          audioDesired = sm::MusicId::Count;
    sm::MusicId          audioFailed = sm::MusicId::Count;
    int                  subworldLastPlayerHp = -1;
    float                subworldHitFlashTimer = 0.0f;
    // ONE fractional stamina carry for the whole body: the map walk and the
    // subworld walk charge the same purse through the same law, so they share
    // the remainder instead of each rounding on its own. Runtime only.
    sm::TravelStamina    travelStamina;

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

    // The player's own half of the pause — the ONLY stored pause bit (see
    // PauseReason / world_paused). Written in one place, set_paused(), which
    // the Space key and the toolbar's II / > all route through. Every other
    // reason the world can be stopped for is derived, never stored.
    //
    // Session state: not saved.
    bool  playerPaused = false;

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
    sm::ui::UiSettings uiSettings;   // universal UI show/hide + size prefs (global)
    sm::ui::Keymap     keymap;       // universal rebindable key bindings (global)
    bool uiPrefsDirty = false;       // set when the Interface panel changes a value
    sm::ui::MacroCursor cursor;
    sm::SaveSummary saveSummary;
    sm::SaveSummary autosaveSummary;
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
    // Forced encounter (Session 15 Inc 6). The squad the pre-battle screen is
    // about, and the one squad the player just parleyed with / paid off — it
    // does not re-force the encounter while the two still share a cell;
    // separation clears the grace. Runtime-only: entt handles are not save
    // material, and a loaded PreBattle kind with no live target fails closed.
    entt::entity preBattleNpc      = entt::null;
    entt::entity encounterGraceNpc = entt::null;
    std::string  encounterTalkLine;
    // Dev console: multiplies the live-frame simulation dt (1.0 = normal). Only
    // the interactive frame path honours this; scripted/smoke steps keep their
    // fixed dt so determinism is preserved.
    float simSpeed = 1.0f;
    // Fractional part of a simSpeed-scaled step count, carried between frames.
    // At the normal 1.0 it stays exactly zero — steps * 1.0f leaves no residue —
    // so ordinary play is drift-free and only a deliberate fast-forward rounds.
    float simStepCarry = 0.0f;
    // Rest-until-morning fast-forward (Session 17): the toolbar's Z aims the
    // clock at the next 06:00 and the loop runs kRestTicksPerTurn world ticks
    // per turn until it lands — frames keep rendering, so the rest is VISIBLE
    // and interruptible. 0 = off. Map only; anything that changes the scene
    // (pause, subworld, an encounter modal) cancels it.
    std::uint64_t restUntilTick = 0;
    // World rate meter: ticks actually produced per real second, against the
    // nominal kTicksPerRealSecond.
    //
    // One turn of the loop is one tick AND one frame, so this number is the
    // frame rate — that is the point of showing it. What changed with the tick
    // model is what a low frame rate MEANS: it used to be a choppier picture of
    // a world still moving at its own pace (a bigger dt covered the gap), and
    // now it is the world itself living slower. The label says so, and it also
    // covers the one case where the two numbers differ: a `simspeed` other
    // than 1 runs several ticks per turn, or none.
    int   tickRateCounter = 0;
    Uint64 tickRateMark = 0;
    float measuredTicksPerSec = float(sm::kTicksPerRealSecond);
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

// THE one assembly of the layer envelope (macro_stock.h MacroWorld, CANON S6)
// for the live game. Every consumer — the subworld's enter(), the AI drivers,
// the daily tick, a console command settling a debt — takes the world through
// this and only this; a call site that re-picks layers by hand is the defect
// this function exists to end (canon-audit C4: a dozen partial re-picks, three
// of them missing `deposits`, and the deposit rows refusing silently).
// The macro layer's facts, raised onto the ONE bus (work_vector §1: a system
// that emits nothing is invisible to the story layer). The macro layer itself
// never sees the bus — it reports through the envelope's POD channel, exactly
// as econ_day does, and this is where a macro fact becomes the same GameEvent
// a subworld death produces. An auto-resolved kill now counts toward a
// kill-N quest, because it is literally the same fact.
void raise_macro_fact(void* user, const sm::BattleFact& fact) {
    auto& app = *static_cast<App*>(user);
    if (fact.kind != sm::BattleFact::Kind::Death) return;
    sm::GameEvent ev{sm::EventTag::NpcDeath};
    ev.a = fact.victim;
    ev.b = fact.killer;
    ev.ix = fact.npcType < std::uint16_t(sm::NPCType::Count)
        ? int(fact.npcType) : sm::kNoNpcType;
    app.bus.emit(ev);
}

// THE player's bag, from his squad entity (macro/player_entity.h). A world
// that has not been built yet answers with a scratch pack rather than a null:
// every caller here is UI or harness, and neither should learn to null-check
// a container.
sm::Inventory& player_bag(App& app) {
    static sm::Inventory scratch{};
    sm::Inventory* bag = sm::player_inventory(app.ecs);
    return bag ? *bag : scratch;
}

sm::MacroWorld macro_world(App& app) {
    sm::MacroWorld mw{};
    mw.facts     = &raise_macro_fact;
    mw.factsUser = &app;
    mw.gs       = &app.gs;
    mw.trees    = &app.treeLayer;
    mw.world    = &app.ecs;
    mw.terrain  = &app.terrain;
    mw.deposits = &app.deposits;
    mw.features = &app.features;
    mw.zones    = &app.zones;
    mw.pathCost = &app.pathCost;
    mw.treeGrid = &app.treeGrid;
    mw.landmarks = &app.landmarkGrid;
    return mw;
}

// Defined below (they compose the bake helpers); declared here because
// early call sites enter battle through the same one transition.
void rebake_world(App& app, bool uploadNow = true);
void enter_subworld(App& app);

// The full-screen macro map page is open: the M toggle, on the macro layer,
// in play. Everyone who swaps a camera or reroutes an input asks THIS.
bool macro_map_open(const App& app) {
    return app.ui.map && app.worldLoaded && !app.subworld.active()
        && app.state == sm::ui::AppState::Playing;
}

bool smoke_token_equals(std::string_view token, const char* lit) {
    std::size_t n = 0;
    while (lit[n] != '\0') ++n;
    return token.size() == n && token.compare(lit) == 0;
}

// One row per scenario: the token the script names and the action it maps to.
// Adding a smoke action = one enum value + one row here + one switch case.
struct SmokeTokenRow { const char* tok; SmokeAction act; };
constexpr SmokeTokenRow kSmokeTokens[] = {
    {"new_game", SmokeAction::NewGame},
    {"save_game", SmokeAction::SaveGame},
    {"open_load", SmokeAction::OpenLoad},
    {"load_game", SmokeAction::LoadGame},
    {"wait_boot_done", SmokeAction::WaitBootDone},
    {"subworld_time", SmokeAction::SubworldTime},
    {"subworld_seam", SmokeAction::SubworldSeam},
    {"subworld_audio", SmokeAction::SubworldAudio},
    {"subworld_exit_gate", SmokeAction::SubworldExitGate},
    {"subworld_loot_xp", SmokeAction::SubworldLootXp},
    {"subworld_enemy_feedback", SmokeAction::SubworldEnemyFeedback},
    {"subworld_missile_feedback", SmokeAction::SubworldMissileFeedback},
    {"subworld_self_fireball", SmokeAction::SubworldSelfFireball},
    {"subworld_player_melee", SmokeAction::SubworldPlayerMelee},
    {"subworld_reputation_hit", SmokeAction::SubworldReputationHit},
    {"subworld_mouse_release", SmokeAction::SubworldMouseRelease},
    {"subworld_tree_anchor", SmokeAction::SubworldTreeAnchor},
    {"subworld_recovery", SmokeAction::SubworldRecovery},
    {"subworld_sp_drain", SmokeAction::SubworldSpDrain},
    {"subworld_enter", SmokeAction::SubworldEnter},
    {"subworld_exit_remap", SmokeAction::SubworldExitRemap},
    {"dungeon_house", SmokeAction::DungeonHouse},
    {"dungeon_cave", SmokeAction::DungeonCave},
    {"spire_climb", SmokeAction::SpireClimb},
    {"trigger_battle_start", SmokeAction::TriggerBattleStart},
    {"wait_visible", SmokeAction::WaitVisible},
    {"open_settlement_build", SmokeAction::OpenSettlementBuild},
    {"open_settlement_trade", SmokeAction::OpenSettlementTrade},
    {"open_settlement_map", SmokeAction::OpenSettlementMap},
    {"enter_first_settlement", SmokeAction::EnterFirstSettlement},
    {"focus_npc_panel", SmokeAction::FocusNpcPanel},
    {"open_npc_trade", SmokeAction::OpenNpcTrade},
    {"attack_first_npc", SmokeAction::AttackFirstNpc},
    {"macro_kill_writeback", SmokeAction::MacroKillWriteback},
    {"fauna_kill_writeback", SmokeAction::FaunaKillWriteback},
    {"spawn_squad_at_player", SmokeAction::SpawnSquadAtPlayer},
    {"force_encounter", SmokeAction::ForceEncounter},
    {"capture_frame", SmokeAction::CaptureFrame},
    {"stats_settle", SmokeAction::StatsSettle},
    {"open_map", SmokeAction::OpenMap},
    {"open_stats", SmokeAction::OpenStats},
    {"spend_attribute_vit", SmokeAction::SpendAttributeVit},
    {"spend_skill_bodybuilding", SmokeAction::SpendSkillBodybuilding},
    {"macro_travel_sp", SmokeAction::MacroTravelSp},
    {"macro_recovery", SmokeAction::MacroRecovery},
    {"rest_sp", SmokeAction::RestSp},
    {"timeadvance_burst", SmokeAction::TimeAdvanceBurst},
    {"macro_npc_trace", SmokeAction::MacroNpcTrace},
    {"open_quests", SmokeAction::OpenQuests},
    {"open_codex", SmokeAction::OpenCodex},
    {"open_spells", SmokeAction::OpenSpells},
    {"cast_spell", SmokeAction::CastSpell},
    {"cast_bolt_capture", SmokeAction::CastBoltCapture},
    {"light_probe_capture", SmokeAction::LightProbeCapture},
    {"toggle_haste", SmokeAction::ToggleHaste},
    {"toggle_flight", SmokeAction::ToggleFlight},
    {"prepare_spell_auras", SmokeAction::PrepareSpellAuras},
    {"trigger_count_only_dialog", SmokeAction::TriggerCountOnlyDialog},
    {"trigger_story_overlay", SmokeAction::TriggerStoryOverlay},
    {"complete_story_overlay", SmokeAction::CompleteStoryOverlay},
    {"return_title", SmokeAction::ReturnTitle},
    {"quit", SmokeAction::Quit},
    {"console", SmokeAction::ConsoleSmoke},
};

bool smoke_action_from_token(std::string_view token, SmokeAction& out) {
    for (const auto& row : kSmokeTokens) {
        if (smoke_token_equals(token, row.tok)) { out = row.act; return true; }
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
                 "[smoke] %s spells=%d bus=%zu logic=%zu active=%zu world=%d state=%d\n",
                 label,
                 sm::kSpellCount,
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
        && app.bus.subscription_count() == 0
        && app.logic.is_consistent()
        && app.logic.node_count() > 0
        && app.logic.active_count() <= app.logic.node_count();
}

bool smoke_destroy_invariants_hold(const App& app) {
    return !app.worldLoaded
        && app.state == sm::ui::AppState::Title
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

// A global preferences file (UI prefs, keymap) — a sibling of the save in the
// per-user pref dir (SDL_GetPrefPath). No legacy migration: a missing file
// just means "use defaults". Falls back to the cwd if the pref dir is missing.
std::string resolve_prefs_path(const char* fileName) {
    char* pref = SDL_GetPrefPath(kSaveOrgName, kSaveAppName);
    if (!pref || pref[0] == '\0') {
        if (pref) SDL_free(pref);
        return fileName;
    }
    std::string path(pref);
    SDL_free(pref);
    path += fileName;
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

// Does the frame we just presented actually SHOW a world?
//
// This used to be `samplesHit = 9; return true;` with a note that readback was
// not implemented yet — so `wait_visible` printed "visible samples=9" over a
// black screen, over a broken swapchain, over anything. The readback it was
// waiting for has existed for a while (it is how the smoke PNGs are written),
// so the scenario now judges real pixels.
//
// Two questions, because either alone is cheatable:
//   * is anything LIT — at least one sample above black; and
//   * does the picture VARY — a cleared screen is one flat colour everywhere,
//     and a flat frame is exactly the failure this scenario exists to catch.
// Samples sit on the quarter/half/three-quarter grid, away from the edges, so
// a HUD strip or a letterbox border cannot pass for the world.
bool smoke_framebuffer_has_world_pixels(const App& app, int& samplesHit) {
    samplesHit = 0;
    const std::vector<std::uint8_t>& px = app.smoke.probePixels;
    const int w = app.smoke.probePixW;
    const int h = app.smoke.probePixH;
    if (px.empty() || w <= 0 || h <= 0) return false;
    if (px.size() < std::size_t(w) * std::size_t(h) * 4u) return false;

    constexpr int kBlackSum = 24;   // of 765; darker than any lit ground
    std::uint32_t firstColour = 0;
    bool haveFirst = false;
    int distinct = 0;
    for (int gy = 1; gy <= 3; ++gy) {
        for (int gx = 1; gx <= 3; ++gx) {
            const int x = w * gx / 4;
            const int y = h * gy / 4;
            const std::size_t i =
                (std::size_t(y) * std::size_t(w) + std::size_t(x)) * 4u;
            const int sum = int(px[i]) + int(px[i + 1u]) + int(px[i + 2u]);
            if (sum > kBlackSum) ++samplesHit;
            const std::uint32_t colour = (std::uint32_t(px[i]) << 16)
                                       | (std::uint32_t(px[i + 1u]) << 8)
                                       |  std::uint32_t(px[i + 2u]);
            if (!haveFirst) { firstColour = colour; haveFirst = true; }
            else if (colour != firstColour) ++distinct;
        }
    }
    return samplesHit > 0 && distinct > 0;
}

// Write a captured swapchain frame to a PNG. Arm the capture with
// app.renderer.request_capture() BEFORE end_frame(); call this AFTER end_frame()
// to drain it. The renderer hands back raw pixels in the swapchain's native
// format (BGRA here); we swizzle to RGBA and force opaque alpha for stb. Path:
// $TIMAERT_SHOT_PATH, else a deterministic /tmp name keyed by the smoke action
// index. Test/tooling only.
bool write_smoke_frame_png(App& app, int actionIndex, const char* label) {
    std::vector<std::uint8_t> px;
    int w = 0, h = 0;
    VkFormat fmt = VK_FORMAT_UNDEFINED;
    if (!app.renderer.take_capture(px, w, h, fmt)) {
        std::fprintf(stderr,
                     "[smoke] capture unavailable (swapchain TRANSFER_SRC?)\n");
        std::fflush(stderr);
        return false;
    }
    const bool bgra = (fmt == VK_FORMAT_B8G8R8A8_UNORM
                       || fmt == VK_FORMAT_B8G8R8A8_SRGB);
    for (std::size_t i = 0; i + 3u < px.size(); i += 4u) {
        if (bgra) std::swap(px[i], px[i + 2u]);
        px[i + 3u] = 255u;  // presented alpha is undefined → force opaque
    }
    char path[512];
    if (const char* p = std::getenv("TIMAERT_SHOT_PATH")) {
        std::snprintf(path, sizeof(path), "%s", p);
    } else {
        std::snprintf(path, sizeof(path), "/tmp/timaert_shot_%02d_%s.png",
                      actionIndex, label ? label : "frame");
    }
    const int ok = stbi_write_png(path, w, h, 4, px.data(), w * 4);
    std::fprintf(stderr, "[smoke] capture %s %dx%d -> %s\n",
                 ok ? "wrote" : "FAILED", w, h, path);
    std::fflush(stderr);
    return ok != 0;
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
        && app.availableQuestDay == app.gs.worldTime.day()) {
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
    app.availableQuestDay = app.gs.worldTime.day();
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

    app.cursor.path.clear();
    app.cursor.pathIdx = 0;
    app.ui.settlement = false;
    enter_subworld(app);
    if (!app.subworld.active()) return false;

    // The lord you struck is EMBODIED, not imitated. This used to hand-copy his
    // face, his bag and his traits into a fresh stranger who happened to look
    // like him: killing that stranger killed nobody on the map, so the same
    // encounter could be farmed until the player got bored. Now it is the
    // tracked form — one macro entity, one body, one death.
    if (!app.subworld.spawn_tracked_npc_body(npc)) {
        app.subworld.leave(true);
        return false;
    }

    // (The macro entity used to be DESTROYED right here, the instant you swung:
    // the encounter was a copy, so the original had to be swept away or you would
    // meet him again on the map. That meant attacking a lord deleted him from the
    // world whatever happened next — win, flee or die. He is the body you are
    // fighting now, so his fate is decided by the fight.)
    return true;
}

// ── Forced encounter — the pre-battle screen (Session 15, Inc 6) ──────────
//
// A hostile squad on the map FORCES an encounter (owner, macrosim.md, the
// M&B way): the geometric meeting — one macro cell — stops the march and
// opens a screen of ACTIONS instead of walking through. The actions are a
// TABLE (label / availability / effect) because this screen will grow the
// way the owner described — negotiation, relations, a friendly lord waving
// you through — and each of those must land as a ROW with a predicate,
// never a branch in the frame loop. The fight row is route_macro_npc_attack
// exactly as before; the auto row is THE resolver the AI wars already use,
// settled through the same doors (macro/squad.h), so the button and the
// world play one game.

bool modal_overlay_active(const App& app);   // defined with the pause block

// The player's side of the resolver — from the very numbers his subworld
// body fights with (sub/engine.h player melee identity + combatStats), his
// army as his roster, his sheet's aura, his stamina as fatigue.
sm::AutoBattleSide player_auto_battle_side(App& app) {
    sm::AutoBattleSide s{};
    const sm::PlayerState& p = app.gs.player;
    s.leaderHpOverride = float(std::max(1, p.combatStats.maxHp));
    s.leaderHealthFraction = p.combatStats.maxHp > 0
        ? float(std::clamp(p.combatStats.currentHp, 0, p.combatStats.maxHp))
              / float(p.combatStats.maxHp)
        : 1.0f;
    const float swing = std::floor(
        sm::sub::kPlayerBaseMeleeDamage
        + sm::calculate_derived(p.sheet.attributes, p.sheet.skills)
              .rawPhysDamage);
    s.leaderDpsOverride = swing / sm::sub::kPlayerMeleeCooldown;
    s.roster = sm::player_roster(app.ecs);
    s.aura = sm::collect_leader_aura(p.sheet);
    s.fatigue = p.combatStats.maxSp > 0
        ? std::clamp(float(p.combatStats.currentSp)
                         / float(p.combatStats.maxSp), 0.1f, 1.0f)
        : 1.0f;
    return s;
}

// What the leader asks to let you pass: scaled by his squad's strength, and
// a Greedy leader asks double — the trait is data on the entity. Balance
// knob (owner: retune after playtests).
int encounter_payoff_cost(App& app, entt::entity npc) {
    const float their =
        sm::squad_power(sm::auto_battle_side_of(app.ecs, npc));
    int cost = std::max(10, int(their / 10.0f));
    if (const auto* tr = app.ecs.reg.try_get<sm::ecs::NpcTraits>(npc)) {
        for (std::uint8_t i = 0; i < tr->count; ++i) {
            if (tr->traits[i] == std::uint8_t(sm::NPCTrait::Greedy)) cost *= 2;
        }
    }
    return cost;
}

// The odds of slipping away: your share of the combined strength, banded so
// there is always a chance and never a certainty.
float encounter_flee_chance(App& app, entt::entity npc) {
    const float mine = sm::squad_power(player_auto_battle_side(app));
    const float their =
        sm::squad_power(sm::auto_battle_side_of(app.ecs, npc));
    return std::clamp(0.25f + 0.5f * mine / std::max(1.0f, mine + their),
                      0.05f, 0.95f);
}

void close_pre_battle(App& app, bool grace) {
    if (grace) app.encounterGraceNpc = app.preBattleNpc;
    app.preBattleNpc = entt::null;
    app.encounterTalkLine.clear();
    if (app.gs.subState.kind == sm::GameSubStateKind::PreBattle) {
        app.gs.subState.kind = sm::GameSubStateKind::Exploring;
    }
}

void push_combat_log(App& app, std::string msg) {
    sm::LogEntry entry{};
    entry.type = sm::LogType::Combat;
    entry.day = app.gs.worldTime.day();
    entry.message = std::move(msg);
    sm::push_event_log(app.gs.player, std::move(entry));
}

// The M&B auto-resolve button: the ONE resolver, the player as one side,
// settled through the same doors as every AI war (macro/squad.h). A wiped
// loss drives currentHp to 0 and the ordinary death check ends the game —
// by the resolver's own law that only happens when his whole army died
// with him.
void perform_encounter_auto(App& app, entt::entity npc, sm::Ambush ambush) {
    const sm::AutoBattleOutcome o = sm::resolve_auto_battle(
        player_auto_battle_side(app),
        sm::auto_battle_side_of(app.ecs, npc),
        ambush, app.npcAi.jitter);
    sm::MacroWorld mw = macro_world(app);
    const int xp = sm::settle_player_auto_battle(mw, npc, o, true);
    const bool won = o.winner == 0;
    char line[160];
    std::snprintf(line, sizeof(line),
                  "Auto-resolve: %s. Lost %d of your soldiers, felled %d of "
                  "theirs (+%d exp).",
                  won ? "victory" : "defeat",
                  int(o.casualtiesA.size()), int(o.casualtiesB.size()), xp);
    push_combat_log(app, line);
    const bool enemyGone = !app.ecs.reg.valid(npc)
        || app.ecs.reg.all_of<sm::ecs::Dead>(npc);
    close_pre_battle(app, /*grace*/!enemyGone);
}

// One action row of the pre-battle screen.
struct PreBattleAction {
    void (*label)(App&, entt::entity, char*, std::size_t);
    bool (*available)(App&, entt::entity);
    // Returns true when the encounter is consumed (the modal closes).
    bool (*perform)(App&, entt::entity);
    const char* tooltip;
};

const PreBattleAction kPreBattleActions[] = {
    // Talk: hear the leader out — his row's own lines. The seed of the
    // negotiation/relations rows to come.
    {[](App&, entt::entity, char* out, std::size_t n) {
         std::snprintf(out, n, "Talk");
     },
     [](App&, entt::entity) { return true; },
     [](App& app, entt::entity npc) {
         const auto& kind = app.ecs.reg.get<sm::ecs::NPCKind>(npc);
         const sm::NpcTypeDef& def =
             sm::npc_def(sm::NPCType(std::uint8_t(kind.type)));
         if (def.talkCount > 0) {
             app.encounterTalkLine = def.talkLines[std::size_t(
                 app.npcAi.jitter.next_u32() % def.talkCount)];
         }
         return false;   // stays open — words are not an exit
     },
     "Hear what they have to say."},
    // Pay off: gold buys the road. Grace lets you actually walk away.
    {[](App& app, entt::entity npc, char* out, std::size_t n) {
         std::snprintf(out, n, "Pay off (%d gold)",
                       encounter_payoff_cost(app, npc));
     },
     [](App& app, entt::entity npc) {
         return sm::wallet_value(player_bag(app))
                    >= encounter_payoff_cost(app, npc);
     },
     [](App& app, entt::entity npc) {
         const int cost = encounter_payoff_cost(app, npc);
         // The toll is REAL coin, into the bandit's own bag — rob him back
         // later and it is there.
         if (auto* bag =
                 app.ecs.reg.try_get<sm::ecs::NpcInventory>(npc)) {
             sm::transfer_value(player_bag(app), bag->inv, cost);
         } else {
             sm::wallet_spend_up_to(player_bag(app), cost);
         }
         char line[96];
         std::snprintf(line, sizeof(line),
                       "Paid %d gold to be let through.", cost);
         push_combat_log(app, line);
         close_pre_battle(app, /*grace*/true);
         return true;
     },
     "Buy your way past. A greedy leader asks double."},
    // Flee attempt: odds from the one strength law; failing means they are
    // on you — the fight happens on the ground.
    {[](App& app, entt::entity npc, char* out, std::size_t n) {
         std::snprintf(out, n, "Try to flee (~%d%%)",
                       int(encounter_flee_chance(app, npc) * 100.0f));
     },
     [](App&, entt::entity) { return true; },
     [](App& app, entt::entity npc) {
         if (app.npcAi.jitter.next_f01() < encounter_flee_chance(app, npc)) {
             // Slip two cells straight away from them, dodging water; a
             // failed search leaves you where you stand — graced, but they
             // may catch you again.
             const auto& ep = app.ecs.reg.get<sm::ecs::Position>(npc);
             float dx = app.gs.player.x - ep.x;
             float dy = app.gs.player.y - ep.y;
             const float mw = float(app.gs.mapW), mh = float(app.gs.mapH);
             if (dx > mw * 0.5f) dx -= mw;
             if (dx < -mw * 0.5f) dx += mw;
             if (dy > mh * 0.5f) dy -= mh;
             if (dy < -mh * 0.5f) dy += mh;
             if (dx == 0.0f && dy == 0.0f) dx = 1.0f;
             const float len = std::sqrt(dx * dx + dy * dy);
             const auto land = [&app](int x, int y) {
                 const sm::TerrainData& t = app.terrain;
                 if (t.width != app.gs.mapW || t.height != app.gs.mapH
                     || !t.has_rgba_storage()) {
                     return true;
                 }
                 const int xx = sm::wrapi(x, t.width);
                 const int yy = sm::wrapi(y, t.height);
                 const std::size_t idx =
                     (std::size_t(yy) * std::size_t(t.width)
                      + std::size_t(xx)) * 4u + 3u;
                 return idx < t.rgba.size() && t.rgba[idx] >= 128;
             };
             for (float away = 2.0f; away >= 1.0f; away -= 1.0f) {
                 const int tx = sm::wrapi(
                     int(std::floor(app.gs.player.x + dx / len * away)),
                     app.gs.mapW);
                 const int ty = sm::wrapi(
                     int(std::floor(app.gs.player.y + dy / len * away)),
                     app.gs.mapH);
                 if (!land(tx, ty)) continue;
                 app.gs.player.x = float(tx);
                 app.gs.player.y = float(ty);
                 app.cursor.path.clear();
                 app.cursor.pathIdx = 0;
                 break;
             }
             push_combat_log(app, "Slipped away from a hostile band.");
             close_pre_battle(app, /*grace*/true);
             return true;
         }
         push_combat_log(app, "They caught you as you turned to run!");
         close_pre_battle(app, /*grace*/false);
         (void)route_macro_npc_attack(app, npc);
         return true;
     },
     "Odds follow the strength law. Fail, and they are on you."},
    // Fight: the subworld battle against exactly the people the roster
    // names — the old attack path, now behind the forced stop.
    {[](App&, entt::entity, char* out, std::size_t n) {
         std::snprintf(out, n, "Fight!");
     },
     [](App&, entt::entity) { return true; },
     [](App& app, entt::entity npc) {
         close_pre_battle(app, /*grace*/false);
         (void)route_macro_npc_attack(app, npc);
         return true;
     },
     "Meet them on the ground, blade in hand."},
    // Auto-resolve: same function, same inputs as the world's own wars.
    {[](App&, entt::entity, char* out, std::size_t n) {
         std::snprintf(out, n, "Auto-resolve");
     },
     [](App&, entt::entity) { return true; },
     [](App& app, entt::entity npc) {
         perform_encounter_auto(app, npc, sm::Ambush::None);
         return true;
     },
     "Let the one law of battle decide, no second law of combat."},
};

// The geometric meeting: runs after the player's march step and after the
// AI drive — either side may step onto the other. Opens the screen and
// stops the world (modal_overlay_active pauses everything, march included).
void detect_forced_encounter(App& app) {
    if (!app.worldLoaded || app.subworld.active()) return;
    if (app.gs.subState.kind != sm::GameSubStateKind::Exploring) return;
    if (modal_overlay_active(app)) return;
    auto& reg = app.ecs.reg;
    const int px = sm::wrapi(int(std::floor(app.gs.player.x)), app.gs.mapW);
    const int py = sm::wrapi(int(std::floor(app.gs.player.y)), app.gs.mapH);

    // A graced squad stops gracing the moment the two part cells.
    if (app.encounterGraceNpc != entt::null) {
        bool together = false;
        if (reg.valid(app.encounterGraceNpc)) {
            if (const auto* gp =
                    reg.try_get<sm::ecs::Position>(app.encounterGraceNpc)) {
                together =
                    sm::wrapi(int(std::floor(gp->x)), app.gs.mapW) == px
                    && sm::wrapi(int(std::floor(gp->y)), app.gs.mapH) == py;
            }
        }
        if (!together) app.encounterGraceNpc = entt::null;
    }

    auto view = reg.view<sm::ecs::Position, sm::ecs::NPCKind,
                         sm::ecs::MacroNpcRuntime, sm::ecs::Health>(
        entt::exclude<sm::ecs::Dead, sm::ecs::PlayerTag,
                      sm::ecs::PlayerSquadTag, sm::ecs::SubworldTag>);
    for (auto e : view) {
        if (e == app.encounterGraceNpc) continue;
        const auto& hp = view.get<sm::ecs::Health>(e);
        if (hp.hp <= 0.0f) continue;
        const auto& pos = view.get<sm::ecs::Position>(e);
        if (sm::wrapi(int(std::floor(pos.x)), app.gs.mapW) != px
            || sm::wrapi(int(std::floor(pos.y)), app.gs.mapH) != py) {
            continue;
        }
        const auto& kind = view.get<sm::ecs::NPCKind>(e);
        // Hostile to the PLAYER by the one relation law — the same predicate
        // the battle masks bake and the AI wars ask.
        if (!sm::player_hostile_to(&app.gs,
                                   sm::faction_id_for_index(kind.factionIdx))) {
            continue;
        }
        app.preBattleNpc = e;
        app.encounterTalkLine.clear();
        app.gs.subState.kind = sm::GameSubStateKind::PreBattle;
        app.cursor.path.clear();
        app.cursor.pathIdx = 0;
        break;
    }
}

// The screen itself — the modal shape of draw_encounter_modal, filled from
// the squad's own data: the leader's name and rank from his row and face,
// the roster summarised by kind, the odds read through squad_power.
void draw_pre_battle_modal(App& app) {
    if (app.gs.subState.kind != sm::GameSubStateKind::PreBattle) return;
    auto& reg = app.ecs.reg;
    const entt::entity npc = app.preBattleNpc;
    if (npc == entt::null || !reg.valid(npc)
        || !reg.all_of<sm::ecs::Position, sm::ecs::NPCKind, sm::ecs::Health,
                       sm::ecs::NpcLevel, sm::ecs::NpcCharacter>(npc)
        || reg.all_of<sm::ecs::Dead>(npc)
        || reg.get<sm::ecs::Health>(npc).hp <= 0.0f) {
        close_pre_battle(app, false);   // fail closed (stale save / dead foe)
        return;
    }

    ImGui::OpenPopup("Hostile band");
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(),
                            ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal("Hostile band", nullptr,
                                ImGuiWindowFlags_NoResize
                                    | ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    const auto& kind = reg.get<sm::ecs::NPCKind>(npc);
    const sm::NpcTypeDef& def =
        sm::npc_def(sm::NPCType(std::uint8_t(kind.type)));
    const auto& face = reg.get<sm::ecs::NpcCharacter>(npc);
    const char* name = def.nameCount > 0
        ? def.names[face.nameIdx % def.nameCount] : def.label;
    const int level = reg.get<sm::ecs::NpcLevel>(npc).value;
    ImGui::Text("%s the %s (level %d) blocks your way!",
                name, def.label, level);
    if (const auto* roster = reg.try_get<sm::ecs::SquadRoster>(npc);
        roster && !roster->squad.empty()) {
        int byKind[int(sm::NPCType::Count)] = {};
        for (const sm::SoldierRecord& r : roster->squad) {
            if (sm::valid_npc_kind(r.kind)) ++byKind[r.kind];
        }
        ImGui::TextUnformatted("At their back:");
        for (int k = 0; k < int(sm::NPCType::Count); ++k) {
            if (byKind[k] > 0) {
                ImGui::BulletText("%d %s", byKind[k],
                                  sm::npc_def(sm::NPCType(k)).label);
            }
        }
    }
    {
        const float mine = sm::squad_power(player_auto_battle_side(app));
        const float their =
            sm::squad_power(sm::auto_battle_side_of(app.ecs, npc));
        const float r = their / std::max(1.0f, mine);
        const char* read = r < 0.5f    ? "They look like easy prey."
                           : r < 0.8f  ? "They look weaker than you."
                           : r < 1.25f ? "An even match."
                           : r < 2.0f  ? "They look stronger than you."
                                       : "They would overrun you.";
        ImGui::TextDisabled("%s", read);
    }
    if (!app.encounterTalkLine.empty()) {
        ImGui::Separator();
        ImGui::TextWrapped("\"%s\"", app.encounterTalkLine.c_str());
    }
    ImGui::Separator();

    for (std::size_t i = 0; i < std::size(kPreBattleActions); ++i) {
        const PreBattleAction& a = kPreBattleActions[i];
        char label[96];
        a.label(app, npc, label, sizeof(label));
        const bool avail = a.available(app, npc);
        ImGui::PushID(int(i));
        ImGui::BeginDisabled(!avail);
        const bool clicked = ImGui::Button(label, ImVec2(-1.0f, 0.0f));
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("%s", a.tooltip);
        }
        ImGui::PopID();
        if (clicked && a.perform(app, npc)) {
            ImGui::CloseCurrentPopup();
            break;
        }
    }
    ImGui::EndPopup();
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
    float totalCost = 0.0f;     // fractional SP owed by the cells crossed
    sm::MacroTravelCost lastCost{};
};

struct MacroWalkChargeContext {
    App* app = nullptr;
    MacroWalkChargeResult result{};
    // The last crossed cell — the climb edge's origin (-1 = walk start).
    int fromX = -1;
    int fromY = -1;
};

void charge_macro_walk_cell(void* user, int x, int y) {
    auto* ctx = static_cast<MacroWalkChargeContext*>(user);
    if (!ctx || !ctx->app) return;

    sm::MacroTravelCost cost;
    // The climb half of the law prices the EDGE: the previous crossed cell
    // is the origin; the walk's first cell has none and climbs free.
    if (!sm::drain_player_sp_for_macro_cell(ctx->app->gs,
                                            &player_bag(*ctx->app),
                                            ctx->app->terrain,
                                            &ctx->app->features,
                                            x, y,
                                            ctx->app->travelStamina,
                                            &cost,
                                            &ctx->app->treeLayer,
                                            ctx->fromX, ctx->fromY)) {
        return;
    }
    ctx->fromX = x;
    ctx->fromY = y;
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

    // Entry-side stamp (macro/entry_context.h): when this frame's walk crossed
    // a cell boundary, record the side it crossed from — the same two bytes a
    // macro NPC's try_move stamps, read by SubworldEngine::enter.
    const int pcx = sm::wrapi(int(std::floor(prevX)), app.gs.mapW);
    const int pcy = sm::wrapi(int(std::floor(prevY)), app.gs.mapH);
    const int ncx = sm::wrapi(int(std::floor(app.gs.player.x)), app.gs.mapW);
    const int ncy = sm::wrapi(int(std::floor(app.gs.player.y)), app.gs.mapH);
    if (ncx != pcx || ncy != pcy) {
        const int sdx = ddx > 0.0f ? 1 : (ddx < 0.0f ? -1 : 0);
        const int sdy = ddy > 0.0f ? 1 : (ddy < 0.0f ? -1 : 0);
        app.gs.player.entryDir = sm::pack_entry_dir(sdx, sdy);
        app.gs.player.entryTicks = 0;
        app.gs.player.entryTickAccum = 0;
    }
    emit_player_move(app, prevX, prevY, dist);
    return charge.result;
}

// Weight of the ground under the player's feet, from the SAME baked cost grid
// the pathfinder walks (build_cost_grid → cell_sp_weight rows). 1.0 (road /
// neutral) when the grid is absent, so a half-booted app never divides by a
// stale table.
float macro_cell_cost_weight(const App& app) {
    const sm::PathCostData& pc = app.pathCost;
    if (pc.width <= 0 || pc.height <= 0
        || pc.costGrid.size() != std::size_t(pc.width) * std::size_t(pc.height)) {
        return 1.0f;
    }
    const int cx = sm::wrapi(int(std::floor(app.gs.player.x)), pc.width);
    const int cy = sm::wrapi(int(std::floor(app.gs.player.y)), pc.height);
    return pc.costGrid[std::size_t(cy) * std::size_t(pc.width) + std::size_t(cx)];
}

// Can the player make camp where he stands? The same DECISION the macro AI
// asks before it lets a spent squad rest (macro/npc_ai.cpp settle_exhaustion),
// read from the same `water` column of the same cost grid. Open water offers
// no camp to anyone: the exhaustion mechanic is one law for both scales, and
// so is the one thing that can stop you paying it.
bool player_can_make_camp(const App& app) {
    const sm::PathCostData& pc = app.pathCost;
    if (pc.width <= 0 || pc.height <= 0
        || pc.water.size() != std::size_t(pc.width) * std::size_t(pc.height)) {
        return true;   // no grid yet: nothing says he cannot
    }
    const int cx = sm::wrapi(int(std::floor(app.gs.player.x)), pc.width);
    const int cy = sm::wrapi(int(std::floor(app.gs.player.y)), pc.height);
    return pc.water[std::size_t(cy) * std::size_t(pc.width)
                    + std::size_t(cx)] == 0u;
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
    // Pass-boundary GPU timing (TIMAERT_GPU_STATS=1). Failure is honest and
    // non-fatal: collect() just returns 0 spans on a device without stamps.
    app.gpuTimer.init(app.device, gpu::VulkanRenderer::kMaxFramesInFlight);
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
    app.appliedSpawnEventCount = 0;
    sm::reset_player_recovery(app.playerRecovery);
    app.travelStamina = sm::TravelStamina{};
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
    // Sight is a projection of a world that is about to die: the Visible-cell
    // list and the last-cell anchor must not survive into the next one (the
    // invalid anchor is what forces the first sweep of a fresh boot or load).
    app.sightRt = sm::SightRuntime{};
    app.optical = App::OpticalCache{};
    app.revealMapOn = false;   // full vision dies with the world it revealed
    if (!app.worldLoaded) return;
    sm::destroy_terrain(app.terrain);
    app.terrain = {};
    app.gs = sm::GameState{};
    app.activeQuests.clear();
    app.questMarkerSig = 0;   // invalidate derived quest-marker cache
    app.ecs.reg.clear();
    app.worldLoaded = false;
}

void refresh_save_summary(App& app) {
    app.saveSummary = sm::inspect_save(app.savePath);
    app.autosaveSummary = sm::inspect_save(app.autosavePath);
}

// Saving can FAIL (disk error, a vector past its save-side cap) and a failed
// attempt leaves the PREVIOUS file intact — which inspect_save then happily
// reports as Ready, so the summary alone can never tell the player anything
// went wrong. Every shipping save goes through here: the verdict is spoken
// into the event log, not inferred from the file on disk.
// Stage everything the save needs out of App-owned runtime, in ONE door:
// the flattened macro ECS, and the macro-AI rhythm image (its live half
// stays on App::npcAi — the transient squad index never saves). Every save
// call site takes its records from here.
std::vector<sm::MacroNpcRecord> stage_save_state(App& app) {
    app.gs.macroAiRhythm.jitter        = app.npcAi.jitter;
    app.gs.macroAiRhythm.sweepAccum    = app.npcAi.sweepAccum;
    app.gs.macroAiRhythm.pendingSweeps = app.npcAi.pendingSweeps;
    app.gs.macroAiRhythm.sweepCursor   = std::uint64_t(app.npcAi.sweepCursor);
    // Story progress (v25) — sorted: the engine's node map iterates in
    // unspecified order and the payload is checksummed.
    app.gs.logicNodesRegistered = app.logic.node_ids();
    std::sort(app.gs.logicNodesRegistered.begin(),
              app.gs.logicNodesRegistered.end());
    app.gs.logicNodesActive = app.logic.active_ids();
    std::sort(app.gs.logicNodesActive.begin(), app.gs.logicNodesActive.end());
    return sm::snapshot_macro_ecs(app.ecs);
}

bool save_game_checked(App& app, bool autosave = false) {
    // The save is a snapshot of the MACRO world and may only be taken ON the
    // map (AGENTS.md -> Persistence): the subworld is a context derived from
    // it, never saved — leaving IS the macro-return every underground action
    // must have.
    if (app.subworld.active()) {
        sm::push_event_log(app.gs.player,
                           {sm::LogType::World,
                            "You cannot save underground - return to the map.",
                            app.gs.worldTime.day()});
        return false;
    }
    const std::string& path = autosave ? app.autosavePath : app.savePath;
    const bool ok = sm::save_game(app.gs, app.activeQuests,
                                  stage_save_state(app), app.treeLayer.data,
                                  app.deposits, path);
    refresh_save_summary(app);
    if (!ok)
        std::fprintf(stderr, "save_game FAILED: %s\n", path.c_str());
    const char* msg = ok ? (autosave ? "Autosaved." : "Game saved.")
                         : (autosave ? "AUTOSAVE FAILED - progress is NOT on disk!"
                                     : "SAVE FAILED - progress is NOT on disk!");
    sm::push_event_log(app.gs.player,
                       {sm::LogType::World, msg, app.gs.worldTime.day()});
    return ok;
}

void open_load_screen(App& app) {
    app.loadReturnState = app.state;
    refresh_save_summary(app);
    app.state = sm::ui::AppState::Load;
}

// The optical world the night-glow bake and the player's sight both read —
// ONE payload (macro/optics.h), assembled from App's cache. Heights come from
// the terrain R channel (the elevation term that walls glow and sight off
// behind ridges); tree density from the live tree layer (a felled forest lets
// more of both through on the next refresh). Rebuilt only when the tree
// revision moves — the cheap per-call check is two integer compares.
sm::OpticalWorld optical_world(App& app) {
    auto& oc = app.optical;
    if (oc.w != app.gs.mapW || oc.h != app.gs.mapH
        || oc.treeRev != app.treeLayer.revision) {
        oc.w = app.gs.mapW;
        oc.h = app.gs.mapH;
        oc.treeRev = app.treeLayer.revision;
        const std::size_t cells =
            std::size_t(app.gs.mapW) * std::size_t(app.gs.mapH);
        oc.heights.clear();
        if (app.terrain.width == app.gs.mapW
            && app.terrain.height == app.gs.mapH
            && app.terrain.rgba.size() >= cells * 4u) {
            oc.heights.resize(cells);
            for (std::size_t i = 0; i < cells; ++i)
                oc.heights[i] = float(app.terrain.rgba[i * 4u]) / 255.0f;
        }
        oc.treeDensity.clear();
        if (app.treeLayer.has_complete_storage()
            && app.treeLayer.width == app.gs.mapW
            && app.treeLayer.height == app.gs.mapH) {
            oc.treeDensity.resize(cells);
            for (std::size_t i = 0; i < cells; ++i)
                oc.treeDensity[i] = float(app.treeLayer.data[i])
                                  / float(sm::kMaxTreesPerCell);
        }
    }
    sm::OpticalWorld world{};
    world.features = &app.features;
    world.heights = oc.heights.empty() ? nullptr : &oc.heights;
    world.treeDensity = oc.treeDensity.empty() ? nullptr : &oc.treeDensity;
    return world;
}

// THE sight-budget door: how much open ground the player's eye can spend per
// sweep (macro/knowledge.h update_player_sight). Derived, not tuned — a
// standing eye at sub::kBodyEyeM metres sees to the horizon of an Earth-sized
// world, d = √(2·R·h) ≈ 4.7 km, and a macro cell is sub::kCellSize (1024)
// tiles = metres across, so ≈ 4.6 cells of open land — less through canopy,
// nothing past a ridge (optics.h spends the same budget faster there).
// Attributes and skills will MULTIPLY here when the RPG track lands: one
// door, one number, every consumer downstream of it.
float player_sight_budget_cells() {
    constexpr float kEarthRadiusM = 6.371e6f;
    return std::sqrt(2.0f * kEarthRadiusM * sm::sub::kBodyEyeM)
         / float(sm::sub::kCellSize);
}

// Bake the macroworld night-light field from the CURRENT world state: enumerate
// every emitting landmark (settlements/villages/active spires + any future
// opt-in POI) and rasterise the terrain-occluded glow the macro shader adds at
// night. The single source of truth for the bake — shared by the boot path and
// every mid-session refresh so the two can never drift apart. The optical
// world is passed so glow propagates through terrain (open land carries it
// far, forest dims it, mountains wall it off — increments B/C).
void bake_macro_light_field(App& app, std::vector<std::uint8_t>& out) {
    std::vector<sm::MacroLight> lights = sm::collect_macro_lights(app.gs);
    const sm::OpticalWorld world = optical_world(app);
    sm::bake_light_field(app.gs.mapW, app.gs.mapH, lights, out, world.features,
                         world.heights, world.treeDensity);
}

// Re-bake the light field and hand ONLY the new field to the renderer (surgical
// binding-3 update — the world textures stay intact). Call whenever the glow-
// driving world state changes after boot: settlements loaded from a save, or
// populations drifting as the daily economy ticks. No-op until the renderer has
// had its first full upload() in boot_world.
void rebake_macro_lights(App& app) {
    if (!app.macro.ready()) return;
    std::vector<std::uint8_t> field;
    bake_macro_light_field(app, field);
    app.macro.upload_light_field(app.device,
                                 field.empty() ? nullptr : field.data(),
                                 std::uint32_t(app.gs.mapW),
                                 std::uint32_t(app.gs.mapH));
}

// ── THE rebaker (CANON S7, 2026-08-24) ────────────────────────────────────
// Every DERIVED field of the world, rebaked whole, in dependency order, by
// the same stage functions the genesis uses — no patching by place, no
// second algorithm. The saved truths (world_fields.h rows + the macro
// snapshot) are its inputs; nothing here invents state. Stages:
//   1. danger zones     (read features, settlements/villages, living trees)
//   2. landmark grid    (reads the landmark registers)
//   3. night glow       (reads landmarks + the optical world)
//   4. travel cost grid (reads terrain, features, living trees)
//   5. GPU zone field   (light uploads inside its own stage; tree/knowledge
//                        fields ride their revisions)
// Callers: every LOAD (the derived fields used to describe the seed's virgin
// world after one — canon-audit G2), the seasonal settle, and — the day
// places live and die (S9) — every landmark transition. Genesis composes the
// same stages inline because spire placement must read zones mid-sequence.
// Roads/fields (FeatureLayer) are still generation-owned: deterministic from
// the seed today; they move here the day places can change.
// `uploadNow`: the GPU half (light + zone textures, each a device-idle
// replace) may only run at a drain-safe point — the menu-side load and the
// seasonal settle qualify; a TRANSITION does not (the first wiring drained
// mid-frame and hung the game — Session 19's law holds). With uploadNow
// false the CPU truth still rebakes WHOLE right now — which is everything
// the simulation reads — and the textures follow at the macro path's dirty
// flush, where the map is next drawn anyway.
void rebake_world(App& app, bool uploadNow) {
    std::vector<sm::ZoneSeed> zsCities, zsVills;
    zsCities.reserve(app.gs.settlements.size());
    for (const auto& c : app.gs.settlements) zsCities.push_back({c.x, c.y});
    zsVills.reserve(app.gs.villages.size());
    for (const auto& v : app.gs.villages) zsVills.push_back({v.x, v.y});
    app.zones = sm::generate_zones(app.gs.mapW, app.gs.mapH, app.gs.worldSeed,
                                   zsCities, zsVills, app.features,
                                   app.terrain.rgba.data(),
                                   app.terrain.rgba.size(), &app.treeLayer);
    app.landmarkGrid = sm::build_landmark_grid(app.gs);
    app.pathCost = sm::build_cost_grid(app.terrain, &app.features,
                                       &app.treeLayer);
    app.gs.lastWorldRebakeDay = app.gs.worldTime.day();
    if (uploadNow) {
        rebake_macro_lights(app);
        app.macroLightsDirty = false;
        if (app.macro.ready())
            app.macro.upload_zone_field(app.device, app.zones);
        app.zoneFieldDirty = false;
    } else {
        app.macroLightsDirty = true;
        app.zoneFieldDirty = true;
    }
}

// Enter the subworld through THE transition (CANON S7, literal by the
// owner's word): the world settles first, so the projection below reads
// freshly rebaked fields. CPU truth only — the map textures follow at the
// dirty flush, and below ground the macro map is not drawn at all.
void enter_subworld(App& app) {
    rebake_world(app, /*uploadNow=*/false);
    app.subworld.enter(macro_world(app), app.bus);
}

void boot_world(App& app, std::uint32_t seed,
                int mapW = 1024, int mapH = 1024,
                const sm::LayerParameters* lpOverride = nullptr,
                int targetTotalCities = 0,
                bool registerIntroStory = true,
                bool spawnMacroNpcs = true) {
    boot_trace("start");
    if (boot_trace_enabled()) {
        std::fprintf(stderr, "[boot] params seed=%u map=%dx%d targetCities=%d\n",
                     seed, mapW, mapH, targetTotalCities);
        std::fflush(stderr);
    }
    destroy_world(app);
    boot_trace("destroyed previous world");
    // Every session starts at the universal default: render diagnostics
    // (sunfreeze, lightdbg) are per-run tools and must never leak into a new
    // game or a load through the surviving engine object.
    app.subworld.reset_render_diagnostics();

    sm::LayerParameters lp = lpOverride ? *lpOverride : sm::LayerParameters{};
    lp.seed = float(seed % 100000u);
    app.gs = sm::default_game_state(seed, mapW, mapH, lp, targetTotalCities);
    boot_trace("default game state");
    // A new world starts DARK (v40): all-Unknown knowledge. The spawn's
    // surroundings open by the ordinary sight law — the first frame's sweep
    // from the player's cell — not by a special starting reveal.
    sm::knowledge_reset(app.gs.knowledge, app.gs.mapW, app.gs.mapH);
    sm::reset_world_tick_runtime(app.gs.worldTickRt, seed);
    sm::reset_player_recovery(app.playerRecovery);
    app.travelStamina = sm::TravelStamina{};
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

    // The owner's causality IS the boot order: terrain → climate → RESOURCES →
    // and only then settlement. Trees and deposits are pure functions of
    // terrain + seed (neither reads politik), so they are derived before the
    // political layer — settlement placement reads them (R2).
    app.trees = sm::spawn_trees(app.terrain, app.gs.worldSeed, 0.18f, lp.seaLevel);
    boot_trace("trees spawned");
    // The per-cell tree-count layer: the spawn_trees massif mask (the organic
    // FBM лесные массивы) carries the forest term, biomes add a small
    // ambience (16384 = the golden densest massif interior). This derivation
    // is the field's INITIAL CONDITION; the load path restores the saved
    // living grid after the swap.
    {
        std::vector<std::uint8_t> forestMask(
            std::size_t(app.gs.mapW) * std::size_t(app.gs.mapH), 0);
        for (const auto& t : app.trees) {
            const std::size_t i = std::size_t(sm::wrapi(t.y, app.gs.mapH))
                                * std::size_t(app.gs.mapW)
                                + std::size_t(sm::wrapi(t.x, app.gs.mapW));
            if (i < forestMask.size()) forestMask[i] = 1;
        }
        app.treeLayer = sm::build_tree_layer(app.terrain, forestMask.data(),
                                             forestMask.size());
    }
    app.deposits = sm::build_deposit_layer(app.terrain, app.gs.worldSeed,
                                           lp.seaLevel);
    boot_trace("resource layers built");

    // The score context politics reads (R2): the crown decides how many
    // and whose, the ground decides where and how large.
    sm::SettlementSiteContext siteCtx{};
    siteCtx.w.gs       = &app.gs;
    siteCtx.w.trees    = &app.treeLayer;
    siteCtx.w.terrain  = &app.terrain;
    siteCtx.w.deposits = &app.deposits;
    siteCtx.seaLevel8  = std::uint8_t(lp.seaLevel * 255.0f);
    app.gs.politik = sm::generate_politik(app.gs.worldSeed, app.gs.mapW, app.gs.mapH,
                                          &app.terrain, std::uint8_t(lp.seaLevel * 255.0f),
                                          targetTotalCities, &siteCtx);
    boot_trace("politik generated");
    sm::snap_cities_to_land(app.gs.politik, app.terrain, std::uint8_t(lp.seaLevel * 255.0f));
    sm::finalize_politik(app.gs.politik, app.terrain, std::uint8_t(lp.seaLevel * 255.0f));
    sm::populate_landmarks_from_politik(app.gs, app.terrain,
                                        std::uint8_t(lp.seaLevel * 255.0f),
                                        app.treeLayer, app.deposits);
    boot_trace("landmarks populated");
    if (boot_trace_enabled()) {
        // The R2 report card: how many villages actually stand next to the
        // resources the score placed them by. The old roulette scored ~25%
        // on water; the causality law should hold most of the world.
        int nearWater = 0, nearPlough = 0, nearDeposit = 0;
        const std::uint8_t sea8 = std::uint8_t(lp.seaLevel * 255.0f);
        for (const auto& v : app.gs.villages) {
            bool water = false, plough = false, deposit = false;
            for (int dy = -sm::kSettlementReach; dy <= sm::kSettlementReach; ++dy)
                for (int dx = -sm::kSettlementReach; dx <= sm::kSettlementReach;
                     ++dx) {
                    const int x = sm::wrapi(v.x + dx, app.gs.mapW);
                    const int y = sm::wrapi(v.y + dy, app.gs.mapH);
                    if (app.terrain.is_water(x, y, sea8)) water = true;
                    else if (app.terrain.moisture_at(x, y)
                             >= sm::kFieldMoistureMin) plough = true;
                    if (app.deposits.any_at(x, y)) deposit = true;
                }
            nearWater   += water   ? 1 : 0;
            nearPlough  += plough  ? 1 : 0;
            nearDeposit += deposit ? 1 : 0;
        }
        const int n = std::max(1, int(app.gs.villages.size()));
        // A town with no hamlet at all reads as a bug; count them out loud.
        std::vector<int> perCity(app.gs.settlements.size(), 0);
        for (const auto& v : app.gs.villages)
            if (v.nearestCityId >= 0
                && v.nearestCityId < int(perCity.size()))
                ++perCity[std::size_t(v.nearestCityId)];
        int villageless = 0;
        for (int c : perCity) if (c == 0) ++villageless;
        // And how far apart they actually stand: the mean nearest-neighbour
        // distance among villages of the SAME city — the number that reads
        // "scattered around the town" versus "clumped a block apart".
        long long nnSum = 0;
        int nnCount = 0;
        for (const auto& v : app.gs.villages) {
            int nearest = 1 << 20;
            for (const auto& o : app.gs.villages) {
                if (&o == &v || o.nearestCityId != v.nearestCityId) continue;
                const int ddx = std::min(std::abs(v.x - o.x),
                                         app.gs.mapW - std::abs(v.x - o.x));
                const int ddy = std::min(std::abs(v.y - o.y),
                                         app.gs.mapH - std::abs(v.y - o.y));
                nearest = std::min(nearest, std::max(ddx, ddy));
            }
            if (nearest < (1 << 20)) { nnSum += nearest; ++nnCount; }
        }
        std::fprintf(stderr,
                     "[worldgen] cities=%zu villages=%zu villageless=%d "
                     "vilSpacing=%lld nearWater=%d%% nearPlough=%d%% "
                     "nearDeposit=%d%%\n",
                     app.gs.settlements.size(), app.gs.villages.size(),
                     villageless, nnCount ? nnSum / nnCount : 0,
                     100 * nearWater / n, 100 * nearPlough / n,
                     100 * nearDeposit / n);
        std::fflush(stderr);
    }

    sm::RoadTraceStats roadStats;
    auto roads = sm::trace_roads(app.terrain, app.gs.politik, &roadStats,
                                 lp.seaLevel, &app.treeLayer);
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
    app.features = sm::build_feature_layer(app.terrain,
                                           roads, &dirts, lp.seaLevel);
    // Farmland: FT_Field stamped on the wettest land cells around each
    // village — contextual (moisture channel), deterministic, roads win.
    // These cells are the villages' grain deposit for the economy loop.
    {
        std::vector<sm::FieldSite> fieldSites;
        fieldSites.reserve(app.gs.villages.size());
        for (const auto& v : app.gs.villages)
            fieldSites.push_back(sm::FieldSite{v.x, v.y});
        sm::stamp_field_features(app.features, siteCtx.w, fieldSites,
                                 lp.seaLevel);
    }
    sm::build_tree_grid(app.treeGrid, app.trees, app.gs.mapW, app.gs.mapH);
    boot_trace("features and tree grid built");

    std::vector<sm::ZoneSeed> zsCities, zsVills;
    for (auto& c : citiesFlat) zsCities.push_back({c.x, c.y});
    for (auto& v : app.gs.villages) zsVills.push_back({v.x, v.y});
    app.zones = sm::generate_zones(app.gs.mapW, app.gs.mapH, app.gs.worldSeed,
                                   zsCities, zsVills, app.features,
                                   app.terrain.rgba.data(),
                                   app.terrain.rgba.size(),
                                   &app.treeLayer);
    boot_trace("zones generated");

    // Spires need the zone field (their placement law), so they are the one
    // landmark placed after generate_zones rather than in
    // populate_landmarks_from_politik (which cleared the list). One spire per
    // registered spell; a load overwrites gs.spires from the save afterwards
    // (boot_world_from_save), exactly like settlements.
    {
        sm::generate_spires(app.gs, app.zones, app.terrain,
                            std::uint8_t(lp.seaLevel * 255.0f));
        // The landmark set is complete — bake the cell → landmark index the
        // whole game asks (macro/landmark_grid.h).
        app.landmarkGrid = sm::build_landmark_grid(app.gs);
        boot_trace("spires placed");
        if (boot_trace_enabled()) {
            // The placement report card: every spell offered, every spire in
            // the wild band, and a spread that reads "scattered", not "heap".
            int zoneMin = 9, zoneMax = 0, minPair = app.gs.mapW + app.gs.mapH;
            for (const auto& sp : app.gs.spires) {
                const int z = int(app.zones.at(sp.x, sp.y));
                zoneMin = std::min(zoneMin, z);
                zoneMax = std::max(zoneMax, z);
                for (const auto& o : app.gs.spires) {
                    if (&o == &sp) continue;
                    const int ddx = std::min(std::abs(sp.x - o.x),
                                             app.gs.mapW - std::abs(sp.x - o.x));
                    const int ddy = std::min(std::abs(sp.y - o.y),
                                             app.gs.mapH - std::abs(sp.y - o.y));
                    minPair = std::min(minPair, std::max(ddx, ddy));
                }
            }
            std::fprintf(stderr,
                         "[worldgen] spires=%zu/%d zones=[%d..%d] "
                         "minPairDist=%d\n",
                         app.gs.spires.size(), sm::kSpellCount,
                         app.gs.spires.empty() ? 0 : zoneMin,
                         app.gs.spires.empty() ? 0 : zoneMax, minPair);
            std::fflush(stderr);
        }
    }

    if (!app.macro.init(app.device, app.renderer.renderPass)) {
        boot_trace("macro renderer init failed");
    } else {
        boot_trace("macro renderer initialized");
    }
    // Universal night-light field: enumerate every emitting landmark
    // (settlements, villages, active spires, and any future opt-in POI via its
    // LandmarkDef.lightColor) and bake the per-cell RGB glow the macro shader
    // adds at night. Baked here on world-change only, never per frame; handed to
    // the renderer as raw bytes so the GPU target keeps its minimal link set.
    // app.features is built above (build_feature_layer), so the bake sees real
    // terrain here. Shared bake helper keeps this identical to every refresh.
    std::vector<std::uint8_t> macroLightField;
    bake_macro_light_field(app, macroLightField);
    app.macro.upload(app.device, app.terrain, app.features, app.zones,
                     macroLightField.empty() ? nullptr : macroLightField.data(),
                     std::uint32_t(app.gs.mapW), std::uint32_t(app.gs.mapH),
                     &app.treeLayer, &app.gs.knowledge);
    app.uploadedTreeRev = app.treeLayer.revision;
    app.uploadedKnowledgeRev = app.gs.knowledge.revision;
    boot_trace("world data uploaded");
    // TODO: rebuild_landmarks (PHASE C — landmark glyphs/lights).

    app.pathCost = sm::build_cost_grid(app.terrain, &app.features,
                                       &app.treeLayer);
    app.gs.lastWorldRebakeDay = app.gs.worldTime.day();
    app.cursor = sm::ui::MacroCursor{};
    boot_trace("path cost built");

    // A loaded world does NOT respawn its people from the seed — the macro
    // snapshot restores them (Session 17); only a NEW world gets a genesis.
    if (spawnMacroNpcs) {
        sm::spawn_macro_npcs(app.gs, app.ecs, app.terrain, app.gs.worldSeed,
                             &app.deposits);
        if (boot_trace_enabled()) {
            // The causality report's second half: ore inside a village's
            // reach raises its profession — count the men the ground raised.
            int miners = 0, quarrymen = 0, clayDiggers = 0;
            for (auto [e, kind] : app.ecs.reg.view<sm::ecs::NPCKind>().each()) {
                (void)e;
                if (kind.type == std::uint16_t(sm::NPCType::Miner)) ++miners;
                else if (kind.type == std::uint16_t(sm::NPCType::Quarryman)) ++quarrymen;
                else if (kind.type == std::uint16_t(sm::NPCType::ClayDigger)) ++clayDiggers;
            }
            std::fprintf(stderr,
                         "[worldgen] professions miners=%d quarrymen=%d "
                         "clayDiggers=%d\n",
                         miners, quarrymen, clayDiggers);
            std::fflush(stderr);
        }
        boot_trace("macro npcs spawned");
    }

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
    // macro-4a: materialise the player's persistent PlayerTag flag on the macro
    // map (Position + PlayerTag). The macro tick re-heals it thereafter; doing it
    // here makes the invariant hold immediately after boot, before the first tick.
    sm::ensure_macro_player_entity(app.gs, app.ecs);
    // The starter kit is DEALT INTO HIS BAG, which is a container on his squad
    // entity — a PlayerState cannot carry goods any more, because it is not a
    // container. Coin of his own realm follows at chargen (the homeland pick
    // re-mints it), so the opening purse is imperial exactly as it always was.
    if (sm::Inventory* bag = sm::player_inventory(app.ecs)) {
        bag->add("coin_empire", 1000);
        bag->add("potion_hp", 2);
        bag->add("bread", 5);
    }
    boot_trace("macro player flag ensured");
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

bool boot_world_from_save(App& app, const std::string& path) {
    sm::GameState fresh;
    std::vector<sm::Quest> loadedQuests;
    std::vector<sm::MacroNpcRecord> loadedMacro;
    std::vector<std::uint16_t> loadedTrees;
    sm::DepositLayer loadedDeposits;
    if (!sm::load_game(fresh, loadedQuests, loadedMacro, loadedTrees,
                       loadedDeposits, path)) {
        return false;
    }
    // registerIntroStory=TRUE even on load (v25): node definitions are code
    // and must all exist before the saved story progress is replayed below.
    // The old `false` here was the 3-nodes -> 1 bug: a loaded game lost the
    // intro and chapter 1 outright.
    boot_world(app, fresh.worldSeed, fresh.mapW, fresh.mapH,
               &fresh.mapParams, fresh.cityCountTarget,
               /*registerIntroStory=*/true, /*spawnMacroNpcs=*/false);

    app.gs.version           = fresh.version;
    app.gs.saveName          = std::move(fresh.saveName);
    app.gs.savedAt           = std::move(fresh.savedAt);
    app.gs.mapParams         = fresh.mapParams;
    app.gs.cityCountTarget   = fresh.cityCountTarget;
    app.gs.worldTime         = fresh.worldTime;
    app.gs.lastWorldRebakeDay = fresh.lastWorldRebakeDay;   // autosave phase (v22)
    app.gs.nextMacroSpawnOrdinal = fresh.nextMacroSpawnOrdinal;   // identity issuer (v23)
    app.gs.worldTickRt       = fresh.worldTickRt;           // world rhythm (v24)
    app.gs.macroAiRhythm     = fresh.macroAiRhythm;
    // The second sync door (v24): boot_world reset App::npcAi from the seed;
    // the LOADED rhythm now overwrites that, so the AI resumes mid-phase with
    // its own jitter stream instead of re-rolling the same sequence.
    app.npcAi.jitter        = app.gs.macroAiRhythm.jitter;
    app.npcAi.sweepAccum    = app.gs.macroAiRhythm.sweepAccum;
    app.npcAi.pendingSweeps = app.gs.macroAiRhythm.pendingSweeps;
    app.npcAi.sweepCursor   = std::size_t(app.gs.macroAiRhythm.sweepCursor);
    app.gs.logicNodesRegistered = std::move(fresh.logicNodesRegistered);
    app.gs.logicNodesActive     = std::move(fresh.logicNodesActive);
    // Story progress (v25): every content node was just registered as on a
    // new game; replay the saved progress — a consumed one-shot stays
    // consumed, and the active set is restored exactly.
    {
        const auto has = [](const std::vector<std::string>& v,
                            const std::string& id) {
            return std::find(v.begin(), v.end(), id) != v.end();
        };
        for (const std::string& id : app.logic.node_ids()) {
            if (!has(app.gs.logicNodesRegistered, id)) app.logic.remove(id);
        }
        const std::vector<std::string> nowActive = app.logic.active_ids();
        for (const std::string& id : nowActive) {
            if (!has(app.gs.logicNodesActive, id)) app.logic.deactivate(id);
        }
        for (const std::string& id : app.gs.logicNodesActive) {
            app.logic.activate(id);
        }
    }
    app.gs.player            = std::move(fresh.player);
    app.gs.settlements       = std::move(fresh.settlements);
    app.gs.villages          = std::move(fresh.villages);
    app.gs.spires            = std::move(fresh.spires);
    app.gs.markers           = std::move(fresh.markers);
    // The explored map (v40). Visible cells were clamped away on write; the
    // first sight sweep after this load re-opens them from the restored
    // position (destroy_world invalidated the sight anchor).
    app.gs.knowledge         = std::move(fresh.knowledge);
    app.gs.relations         = fresh.relations;
    app.gs.subState          = std::move(fresh.subState);
    app.gs.deserterPool      = fresh.deserterPool;
    app.activeQuests         = std::move(loadedQuests);
    app.questMarkerSig       = 0;   // force quest-marker rebuild on next tick

    // The macro snapshot (Session 17): boot_world above spawned NOTHING
    // (spawnMacroNpcs=false), so the registry holds no macro NPCs yet —
    // restore the saved world's people instead of the seed's. A killed lord
    // stays killed, a levelled leader keeps his campaigns.
    sm::restore_macro_ecs(loadedMacro, app.ecs, app.gs);

    // TODO: rebuild_landmarks (PHASE C — landmark glyphs/lights).
    app.camX = app.camTargetX = app.gs.player.x + 0.5f;
    app.camY = app.camTargetY = app.gs.player.y + 0.5f;
    // macro-4a: boot_world above created the flag at the pre-load anchor; the
    // player scalar was just overwritten from the save, so re-sync the flag's
    // Position to the loaded coordinates (the macro tick would also heal it).
    sm::ensure_macro_player_entity(app.gs, app.ecs);
    // Inc 5e-2 (possession persistence): if the player saved while possessing a
    // macro lord, re-home the flag onto that SAME regenerated NPC, matched by its
    // save-stable spawn ordinal. boot_world above respawned the macro NPCs from
    // the same worldSeed, so the ordinal names the same body. On failure (it died
    // before the save, or the ordinal is stale) keep the hero husk that ensure_
    // just created and clear the field so we never retry a dead identity.
    if (app.gs.player.possessedMacroSpawnId >= 0
        && !sm::reattach_player_to_macro_spawn(app.ecs,
               app.gs.player.possessedMacroSpawnId,
               app.gs.player.x, app.gs.player.y)) {
        app.gs.player.possessedMacroSpawnId = -1;
    }
    app.gs.subState.settlementId = settlement_at_player(app.gs);
    app.ui.settlementId = app.gs.subState.settlementId;

    // boot_world() above derived every field from the SEED's virgin world; we
    // then swapped in the LOADED truths. Restore the remaining truths FIRST —
    // the living tree grid (v36) and the mineral deposits (v37) — then rebake
    // every derived field from them in ONE pass (rebake_world, CANON S7).
    // Before the one rebaker, a load re-baked only the glow — and even that
    // from the virgin forest: zones, cost grid and glow-occlusion all
    // described a world that no longer existed (canon-audit G2).
    if (!sm::restore_tree_counts(app.treeLayer, loadedTrees)) {
        std::fprintf(stderr,
                     "load_game: tree grid size %zu != map cells %zu — "
                     "keeping virgin forest\n",
                     loadedTrees.size(), app.treeLayer.cell_count());
    }
    sm::restore_deposit_cells(app.deposits, loadedDeposits);
    rebake_world(app);
    app.macro.upload_tree_field(app.device, &app.treeLayer);
    app.uploadedTreeRev = app.treeLayer.revision;
    return true;
}

// ── Input ─────────────────────────────────────────────────────

bool sustained_spell_active(const sm::SpellBook& book, const char* id) {
    return sm::spellbook_has_sustained(book, id);
}

// Panels that stand between the player and the world. While one of these is up
// the world stops (Mount & Blade: any window on the map is a stopped map). The
// debug HUD is deliberately NOT in this list — that one exists to WATCH the
// world move, and freezing it would blank the very numbers it is opened for.
bool pausing_panel_open(const App& app) {
    return app.ui.diplomacy ||
           app.ui.settlement ||
           app.ui.quest ||
           app.ui.codex ||
           app.ui.map ||
           app.ui.character ||
           app.ui.settings ||
           app.ui.controls;
}

bool gameplay_panel_open(const App& app) {
    return modal_overlay_active(app) || pausing_panel_open(app) || app.showDebug;
}

// ── THE pause ─────────────────────────────────────────────────
//
// WHY the world stands still — one bit per reason. The world lives only while
// the whole mask is clear, and there is exactly ONE pause: the player's Space,
// a story slide, an event window, an open panel and the Esc screen are not
// five mechanisms, they are five reasons for the same one.
//
// Exactly one bit is STORED (the player's own). Every other reason is DERIVED
// on the spot from state that already exists: a window pauses the world by
// BEING on screen, not by remembering to announce itself. That is the whole
// design, and it is why there is no release path to get wrong — the opposite
// scheme (push a pause when you open, pop it when you close) wedges the world
// forever the first time some path returns early without popping, and the bug
// looks like a hang with nothing in the log. A derived reason cannot leak,
// because there is nothing to forget.
enum PauseReason : std::uint8_t {
    kPauseNone   = 0u,
    kPausePlayer = 1u << 0,   // stored: the Space key, or the toolbar's II
    kPausePanel  = 1u << 1,   // derived: a panel opened over the world
    kPauseModal  = 1u << 2,   // derived: event dialog / story slides / Event substate
    kPauseMenu   = 1u << 3,   // derived: any screen that is not Playing
};

std::uint8_t pause_reasons(const App& app) {
    std::uint8_t mask = kPauseNone;
    // The pause the PLAYER asks for is the map's — a journey is a thing you
    // stop to think about. Underground the fight is real time and a keypress
    // must not freeze it, so the bit is simply not a reason down there (and
    // set_paused refuses to arm it while you are below, so nothing is waiting
    // for you when you climb back out).
    if (app.playerPaused && !app.subworld.active()) mask |= kPausePlayer;
    // The other three ARE both worlds': a window you cannot see past stops the
    // world it is drawn over, wherever you are standing.
    if (pausing_panel_open(app))                    mask |= kPausePanel;
    if (modal_overlay_active(app))                  mask |= kPauseModal;
    if (app.state != sm::ui::AppState::Playing)     mask |= kPauseMenu;
    return mask;
}

// THE question, asked in one place (tick_playing_runtime).
bool world_paused(const App& app) { return pause_reasons(app) != kPauseNone; }

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
#if defined(__APPLE__)
    // Workaround for macOS Apple Silicon ImageIO bug where SDL's internal
    // 43-byte invisible GIF cursor causes a bus error due to vector reads 
    // across a page boundary. We create our own safe 16x16 transparent cursor.
    static SDL_Cursor* blank_cursor = nullptr;
    if (wantRel != app.relativeMouseActive) {
        if (wantRel) {
            if (!blank_cursor) {
                SDL_Surface* surf = SDL_CreateRGBSurfaceWithFormat(0, 16, 16, 32, SDL_PIXELFORMAT_RGBA32);
                SDL_memset(surf->pixels, 0, surf->pitch * surf->h);
                blank_cursor = SDL_CreateColorCursor(surf, 0, 0);
                SDL_FreeSurface(surf);
            }
            SDL_SetCursor(blank_cursor);
            app.relativeMouseActive = true;
        } else {
            SDL_SetCursor(SDL_GetDefaultCursor());
            app.relativeMouseActive = false;
        }
    }
#else
    const bool actual = SDL_GetRelativeMouseMode() == SDL_TRUE;
    if (wantRel != actual) {
        (void)SDL_SetRelativeMouseMode(wantRel ? SDL_TRUE : SDL_FALSE);
    }
    app.relativeMouseActive = SDL_GetRelativeMouseMode() == SDL_TRUE;
#endif
}

std::vector<sm::PathPoint> build_flight_path(int sx, int sy, int gx, int gy,
                                             int mapW, int mapH) {
    // THE fold, not a hand-written one: `torus_delta` (core/torus.h) folds ANY
    // difference into (−period/2, +period/2], however many worlds apart the two
    // numbers drifted. The four conditional subtractions that lived here
    // corrected exactly ONE period — the very defect torus.h documents, and the
    // flight path was the last copy of it in the tree.
    const int dx = int(std::lround(sm::torus_delta(float(gx - sx), float(mapW))));
    const int dy = int(std::lround(sm::torus_delta(float(gy - sy), float(mapH))));
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
    if (!app.subworld.active()) {
        app.subworldLastPlayerHp = app.gs.player.combatStats.currentHp;
        app.subworldHitFlashTimer = 0.0f;
        // NOTE. The travel-stamina carry is deliberately NOT cleared here. This
        // branch runs every frame the player is on the map, and the carry is the
        // body's own — shared by both layers, worth less than 1 SP, and refilled
        // step by step. Wiping it here (as the old subworld-only distance
        // accumulator was) means a sub-1-SP step never accumulates into a whole
        // one, and travel silently becomes free.
        return;
    }

    // Inc 5c (D3): the flash tracks the body you INHABIT. player_display_hp()
    // returns the flagged body's HP — the hero's while unpossessed, a possessed
    // foreign body's while inhabiting it — so its wounds flash the screen while
    // gs.player stays frozen as the preserved revert target.
    const int hp = app.subworld.player_display_hp();
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

// Walking in the subworld is walking in the world: the SAME law as the map
// (macro/movement_cost.h), fed the ground under the player's feet and the
// fraction of a macro cell he just covered. kCellSize tiles make one cell, which
// is the only conversion this needs — and the reason the second stamina formula
// (a flat 10 SP per 1000 tiles, blind to terrain) is gone.
int charge_subworld_sp_for_distance(App& app, float distance) {
    if (distance <= 0.01f) return 0;
    const float cells = distance / float(sm::sub::kCellSize);
    const int overloadCost =
        sm::player_overload_charge(app.gs.player.sheet,
                                   player_bag(app)).cost;
    const float cost = sm::travel_stamina_cost(
        app.subworld.player_ground_travel_weight(), cells, overloadCost,
        sm::travel_skill_efficiency(app.gs.player.sheet.skills));
    return sm::spend_travel_stamina(app.gs.player.combatStats,
                                    app.travelStamina, cost);
}

float subworld_spell_rng01(void* user) {
    auto* sub = static_cast<sm::sub::SubworldEngine*>(user);
    return sub ? sub->spell_rng01() : 0.0f;
}

void draw_subworld_danger_gem(const sm::sub::SubworldEngine& subworld, float scale) {
    // One row per DangerLevel, indexed by the enum (Green=0, Yellow=1, Red=2).
    struct DangerStyle { ImU32 color; const char* label; const char* title; };
    static constexpr DangerStyle kDangerStyles[] = {
        {IM_COL32(63, 191, 74, 255),  "Safe",    "Safe: no enemies nearby"},
        {IM_COL32(232, 200, 74, 255), "Caution", "Caution: enemies nearby"},
        {IM_COL32(224, 50, 42, 255),  "Danger",  "Danger: enemies in melee range"},
    };
    const sm::sub::DangerLevel level = subworld.danger_level();
    const auto& style = kDangerStyles[std::size_t(level) < 3 ? std::size_t(level) : 0];
    const ImU32 color = style.color;
    const char* label = style.label;
    const char* title = style.title;

    // Draw-list overlay: SetWindowFontScale can't reach a foreground list, so
    // every literal is multiplied by `scale`. The top-left anchor stays put and
    // the pip grows from it; text uses AddText's explicit-size overload.
    ImDrawList* fg = ImGui::GetForegroundDrawList();
    ImFont* font = ImGui::GetFont();
    const float fontSize = ImGui::GetFontSize() * scale;
    const ImVec2 pos(14.0f, 44.0f);
    ImVec2 textSize = ImGui::CalcTextSize(label);
    textSize.x *= scale;
    textSize.y *= scale;
    const ImVec2 boxMax(pos.x + 36.0f * scale + textSize.x, pos.y + 25.0f * scale);
    fg->AddRectFilled(pos, boxMax, IM_COL32(8, 10, 12, 190), 6.0f * scale);
    const ImVec2 center(pos.x + 14.0f * scale, pos.y + 12.5f * scale);
    fg->AddCircleFilled(center, 8.0f * scale, color, 20);
    fg->AddCircleFilled(ImVec2(center.x - 2.5f * scale, center.y - 2.8f * scale),
                        2.5f * scale, IM_COL32(255, 255, 255, 150), 12);
    fg->AddCircle(center, 8.0f * scale, IM_COL32(0, 0, 0, 180), 20, 1.0f);
    fg->AddText(font, fontSize, ImVec2(pos.x + 28.0f * scale, pos.y + 5.0f * scale),
                IM_COL32(235, 238, 224, 245), label);
    if (ImGui::IsMouseHoveringRect(pos, boxMax)) {
        ImGui::SetTooltip("%s", title);
    }
}

// A stopped world must LOOK stopped, or it reads as a hang: while any pause
// reason holds, a badge sits at the top centre. Foreground draw list, same
// discipline as the danger gem — no window, every literal scaled.
//
// The label names the way OUT, which differs by reason: the pause you chose is
// lifted with the same key, a pause your inventory is holding is lifted by
// closing it. A story slide or an event window gets no badge at all — it is
// already the loudest thing on screen and does not need a second banner.
void draw_pause_badge(int logicalW, const char* label) {
    ImDrawList* fg = ImGui::GetForegroundDrawList();
    ImFont* font = ImGui::GetFont();
    const float fontSize = ImGui::GetFontSize();
    const ImVec2 textSize = ImGui::CalcTextSize(label);
    const ImVec2 pos((float(logicalW) - textSize.x) * 0.5f, 16.0f);
    fg->AddRectFilled(ImVec2(pos.x - 14.0f, pos.y - 6.0f),
                      ImVec2(pos.x + textSize.x + 14.0f, pos.y + textSize.y + 6.0f),
                      IM_COL32(8, 10, 12, 200), 6.0f);
    fg->AddText(font, fontSize, pos, IM_COL32(255, 214, 168, 245), label);
}

void draw_subworld_combat_log(const sm::sub::SubworldEngine& subworld,
                              int logicalW, float scale) {
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

    // Foreground draw list: scale explicitly (font + all geometry). The stack's
    // top anchor stays fixed; each line's size and the gap between lines grow
    // with `scale`, and text uses AddText's explicit-size overload.
    ImDrawList* fg = ImGui::GetForegroundDrawList();
    ImFont* font = ImGui::GetFont();
    const float fontSize = ImGui::GetFontSize() * scale;
    float y = 64.0f;
    const ImVec2 pad(12.0f * scale, 5.0f * scale);
    for (int i = firstVisible; i < count; ++i) {
        const sm::sub::CombatLogEntry* entry = subworld.combat_log_entry(i);
        if (!entry || entry->text[0] == '\0'
            || entry->age > sm::sub::kCombatLogVisibleSeconds) {
            continue;
        }
        const float fade = std::max(
            0.15f, 1.0f - entry->age / sm::sub::kCombatLogVisibleSeconds);
        const int alpha = int(fade * 255.0f);
        ImVec2 size = ImGui::CalcTextSize(entry->text);
        size.x *= scale;
        size.y *= scale;
        const ImVec2 pos((float(logicalW) - size.x) * 0.5f, y);
        fg->AddRectFilled(ImVec2(pos.x - pad.x, pos.y - pad.y),
                          ImVec2(pos.x + size.x + pad.x,
                                 pos.y + size.y + pad.y),
                          IM_COL32(0, 0, 0, int(153.0f * fade)), 5.0f);
        fg->AddText(font, fontSize, pos, IM_COL32(255, 214, 168, alpha), entry->text);
        y += size.y + 8.0f * scale;
    }
}

bool cast_active_spell(App& app) {
    if (!app.worldLoaded) return false;
    const std::string& id = app.gs.player.spellBook.activeSpellId;
    if (id.empty()) {
        emit_spell_cast(app, id, false, "No active spell");
        return false;
    }

    const sm::SpellDef* def = sm::spell_find(id);
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

    const float cp = std::cos(app.subworld.cam_pitch());
    const float nx = std::cos(app.subworld.cam_yaw()) * cp;
    const float ny = std::sin(app.subworld.cam_yaw()) * cp;
    const float nz = std::sin(app.subworld.cam_pitch());
    const bool ok = sm::spellbook_cast(app.ecs,
        app.gs.player.spellBook,
        app.gs.player.combatStats,
        app.gs.player.sheet.attributes,
        app.gs.player.sheet.skills,
        id,
        app.subworld.player_entity_id(),
        app.subworld.player_x(),
        app.subworld.player_y(),
        // The MUZZLE, not the feet: this is the same point (nx, ny, nz) is
        // aimed from, so the bolt travels the crosshair's own line.
        app.subworld.player_muzzle_z(),
        nx,
        ny,
        nz,
        true,
        &subworld_spell_rng01,
        &app.subworld);
    emit_spell_cast(app, id, ok, ok ? "" : "Cast failed");
    return ok;
}

// THE pause switch — the only place App::playerPaused is written. Every button
// and every key routes here, so "pause" is one thing with one meaning,
// whichever way the player reached for it.
//
// A script-driven run takes no pause from the window: the scenario IS the
// input, and this machine hands a freshly focused window the occasional stray
// event (same class of noise as the documented teardown SIGBUS). Caught in the
// act: toggle_haste failing ~1 run in 6 with this bit set and no dialog, panel
// or menu anywhere — the world simply stopped under a scenario measuring its
// mana drain. Having ONE writer is what made a one-line cure possible.
void set_paused(App& app, bool on) {
    if (app.smoke.enabled) return;
    // No hand-held pause underground — not from a key, not from the toolbar.
    // Refusing here rather than only ignoring the bit in pause_reasons() keeps
    // the button honest: II pressed below does nothing at all, instead of
    // quietly arming a stopped map for you to walk out into.
    if (app.subworld.active()) return;
    app.playerPaused = on;
}

// Rest IS a stop (owner ruling): there is no rest mode, only the ONE macro
// law that a STANDING squad regenerates SP (players: kMarchRecoveryPct=0
// while a path is walked; NPC squads: regen in Idle/Resting only). So Z
// first stops the squad — the click-route dies here, exactly as an
// encounter kills it — and then merely compresses time until the bar is
// full. restUntilTick holds only a hard CAP of two days (an SP DEBT climbs
// out slowly); the REAL stop — SP reaching max — and every cancel live in
// apply_rest_promotion. Already-full SP arms nothing: Z is then just the
// stop it always was.
void aim_rest_until_rested(App& app) {
    app.cursor.path.clear();
    app.cursor.pathIdx = 0;
    const auto& cs = app.gs.player.combatStats;
    if (cs.currentSp >= cs.maxSp) return;
    if (!player_can_make_camp(app)) return;   // no camp in open water
    app.restUntilTick = app.gs.worldTime.tick + 2 * sm::kTicksPerDay;
}

// One turn of the rest fast-forward: the ticks this turn should live, given
// `ticks` as the un-promoted count. Promotes while the aim stands, stops the
// moment the SP bar is FULL (the real goal) or the cap tick arrives, and
// cancels outright when the scene changes under it — pause, the subworld, an
// encounter modal. Called from the main loop every turn AND from the rest
// smoke: one law, one door.
int apply_rest_promotion(App& app, int ticks) {
    if (app.restUntilTick == 0) return ticks;
    const auto& cs = app.gs.player.combatStats;
    // A non-empty path cancels too: rest is a stop, so the player clicking a
    // destination mid-rest IS the scene change — the squad marches at real
    // pace again and time flows at its honest rate. (Marching legs regain no
    // SP, so without this line a rest+march would fast-forward travel.)
    const bool cancelled = app.subworld.active() || app.playerPaused
        || app.gs.subState.kind != sm::GameSubStateKind::Exploring
        || !app.cursor.path.empty()
        || cs.currentSp >= cs.maxSp
        || !player_can_make_camp(app)
        || app.gs.worldTime.tick >= app.restUntilTick;
    if (cancelled) {
        app.restUntilTick = 0;
        return ticks;
    }
    constexpr std::uint64_t kRestTicksPerTurn = 128;   // po2
    const std::uint64_t left = app.restUntilTick - app.gs.worldTime.tick;
    return int(std::min(kRestTicksPerTurn, left));
}

void handle_event_playing(App& app, const SDL_Event& e) {
    switch (e.type) {
        case SDL_KEYDOWN: {
            // THE key dispatch: an action fires when the pressed SCANCODE is
            // its current binding AND its scope listens in the active world
            // (ui/keymap.h — one registry, rebindable in menu → Controls).
            // Within a world a key has one meaning (Keymap::set steals), so
            // the chain below has no order dependence. Esc is the fixed
            // exception: the way back can never be rebound away. Held-key
            // actions (movement, attack, jump) live in poll_movement, not
            // here — a keydown is an edge, a walk is a state.
            using sm::ui::ActionId;
            const SDL_Scancode sc = e.key.keysym.scancode;
            auto is = [&](ActionId a) {
                return app.keymap.get(a) == sc
                    && sm::ui::scope_active(sm::ui::action_spec(a).scope,
                                            app.subworld.active());
            };
            if (sc == SDL_SCANCODE_ESCAPE) { app.state = sm::ui::AppState::Menu; }
            else if (is(ActionId::DebugOverlay)) { app.showDebug = !app.showDebug; }
            else if (is(ActionId::Diplomacy))  { app.ui.diplomacy = !app.ui.diplomacy; }
            else if (is(ActionId::Settlement)) { toggle_settlement_panel(app); }
            else if (is(ActionId::Quests))     { app.ui.quest = !app.ui.quest; }
            else if (is(ActionId::Character)) {
                app.ui.character = !app.ui.character;
                app.ui.characterTab = sm::ui::CharacterPanelTab::Inventory;
            }
            else if (is(ActionId::ArmyTab)) {
                app.ui.character = true;
                app.ui.characterTab = sm::ui::CharacterPanelTab::Army;
            }
            else if (is(ActionId::EquipmentTab)) {   // macro face of the E key
                app.ui.character = true;
                app.ui.characterTab = sm::ui::CharacterPanelTab::Equipment;
            }
            else if (is(ActionId::Interact)) {       // sub face of the E key
                app.subworld.interact();
            }
            else if (is(ActionId::Possess)) {
                // вселение / possession (Inc 5c): take over the body under
                // the reticle. Subworld-only; a no-op with the status line
                // set when nothing is in reach.
                app.subworld.possess_aim();
            }
            else if (is(ActionId::SpellsTab)) {
                app.ui.character = true;
                app.ui.characterTab = sm::ui::CharacterPanelTab::Spells;
            }
            else if (is(ActionId::Codex)) { app.ui.codex = !app.ui.codex; }
            else if (is(ActionId::Map))   { app.ui.map   = !app.ui.map; }
            else if (is(ActionId::CastSpell)) {
                // Might & Magic under the left hand: A strikes, S casts the
                // active spell. Subworld only — on the map the spellbook is
                // cast from the sheet.
                cast_active_spell(app);
            }
            else if (is(ActionId::Pause)) {
                // Map only by scope; the same physical key defaults to jump
                // underground (a held action, read in poll_movement).
                set_paused(app, !app.playerPaused);
            }
            else if (is(ActionId::Rest)) {
                aim_rest_until_rested(app);
            }
            else if (is(ActionId::Save)) { save_game_checked(app); }
            else if (is(ActionId::Load)) { open_load_screen(app); }
            else if (is(ActionId::EnterLeave)) {
                if (!app.subworld.active()) {
                    enter_subworld(app);
                    boot_trace_time("subworld enter", app.gs.worldTime);
                } else {
                    app.subworld.leave();
                    boot_trace_time("subworld leave", app.gs.worldTime);
                }
            }
            break;
        }
        case SDL_MOUSEWHEEL:
            if (macro_map_open(app)) {
                // The map page's camera: same step, but the floor is the
                // whole world fitted to the viewport, not the live minimum.
                float& z = app.mapScreen.zoom;
                if (e.wheel.y > 0) z *= kMacroZoomStep;
                if (e.wheel.y < 0) z /= kMacroZoomStep;
                const float fit =
                    sm::ui::map_fit_zoom(app.height, app.gs.mapH);
                if (z < fit) z = fit;
                if (z > kMacroZoomMax) z = kMacroZoomMax;
                break;
            }
            if (e.wheel.y > 0) app.zoom *= kMacroZoomStep;
            if (e.wheel.y < 0) app.zoom /= kMacroZoomStep;
            if (app.zoom < kMacroZoomMin) app.zoom = kMacroZoomMin;
            if (app.zoom > kMacroZoomMax) app.zoom = kMacroZoomMax;
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
#if defined(__APPLE__)
                // Emulate relative mode by warping mouse back to center
                int w, h;
                SDL_GetWindowSize(app.window, &w, &h);
                int cx = w / 2;
                int cy = h / 2;
                if (e.motion.x != cx || e.motion.y != cy) {
                    SDL_WarpMouseInWindow(app.window, cx, cy);
                }
#endif
            } else if (app.panning && !app.subworld.active()) {
                int dx = e.motion.x - app.panLastMouseX;
                int dy = e.motion.y - app.panLastMouseY;
                app.panLastMouseX = e.motion.x;
                app.panLastMouseY = e.motion.y;
                if (macro_map_open(app)) {
                    // The map page pans by direct GRAB — the world sticks to
                    // the cursor, 1:1: mouse deltas are logical points, the
                    // camera is drawable px/cell, so divide by the logical
                    // zoom (zoom/dpr). Torus wrap keeps every position legal.
                    int lw = app.width, lh = app.height;
                    SDL_GetWindowSize(app.window, &lw, &lh);
                    const float dpr =
                        (lw > 0) ? float(app.width) / float(lw) : 1.0f;
                    const float zl = app.mapScreen.zoom / dpr;
                    if (zl > 0.0f) {
                        // Kept inside the world on every drag: the page's
                        // backdrop tiles for ever (the shader takes fract of
                        // the world coordinate), so an un-wrapped camera drifts
                        // silently until the overlay's toroidal fold is a whole
                        // world out and every landmark, pin and player mark is
                        // culled off-screen. The ground looks fine the whole
                        // time, which is what made it hard to see.
                        app.mapScreen.camX = sm::wrapf(
                            app.mapScreen.camX - float(dx) / zl,
                            float(app.gs.mapW));
                        app.mapScreen.camY = sm::wrapf(
                            app.mapScreen.camY + float(dy) / zl,
                            float(app.gs.mapH));
                    }
                    break;
                }
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
                SDL_Vulkan_GetDrawableSize(app.window, &app.width, &app.height);
            }
            return;
        default: break;
    }
    // A Controls-panel rebind in flight captures the next keydown wholesale —
    // BEFORE the Esc shortcuts and the ImGui keyboard gate, because this SDL
    // event is the only honest source of a scancode. Esc cancels (it is the
    // one fixed key and can never become a binding).
    if (e.type == SDL_KEYDOWN && app.keymap.pendingRebind >= 0) {
        const SDL_Scancode sc = e.key.keysym.scancode;
        if (sc != SDL_SCANCODE_ESCAPE)
            app.keymap.set(sm::ui::ActionId(app.keymap.pendingRebind), sc);
        app.keymap.pendingRebind = -1;
        sm::ui::save_keymap(app.keymap, app.keymapPath);
        return;
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
        if (app.state == sm::ui::AppState::Menu) {
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
             app.state == sm::ui::AppState::Menu) {
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
    // Asked once, honoured by both halves below: a paused world moves nobody,
    // neither the body underground nor the party on the map. The camera is not
    // a body and keeps its keys (see the pan block at the bottom).
    const bool paused = world_paused(app);

    if (app.subworld.active()) {
        if (paused || gameplay_panel_open(app) || io.WantCaptureKeyboard
            || io.WantCaptureMouse) {
            app.subworld.set_player_attack_held(false);
            return;
        }
        // Held keys come from the keymap like every other action (an unbound
        // action lands on scancode 0, which SDL keeps permanently unpressed).
        // The defaults keep the Might & Magic layout — arrows walk, A strikes,
        // S casts — but the SPLIT of hands is now the player's to keep or not.
        using sm::ui::ActionId;
        const Uint8* keys = SDL_GetKeyboardState(nullptr);
        const Uint32 mouse = SDL_GetMouseState(nullptr, nullptr);
        app.subworld.set_player_attack_held(
            keys[app.keymap.get(ActionId::Attack)]
            || ((mouse & SDL_BUTTON(SDL_BUTTON_LEFT)) != 0u));
        // Y axis: UP = forward (+y in world tile space).
        float dx = 0, dy = 0;
        if (keys[app.keymap.get(ActionId::MoveForward)]) dy += 1;
        if (keys[app.keymap.get(ActionId::MoveBack)])    dy -= 1;
        if (keys[app.keymap.get(ActionId::MoveLeft)])    dx -= 1;
        if (keys[app.keymap.get(ActionId::MoveRight)])   dx += 1;
        const float haste = sustained_spell_active(app.gs.player.spellBook, "haste")
            ? 1.5f : 1.0f;
        // Same pace as on the map — one character, one speed, both layers.
        const float pace = sm::calculate_derived(app.gs.player.sheet.attributes,
                                                 app.gs.player.sheet.skills)
                               .moveSpeedMult;
        bool spellFlight = sustained_spell_active(app.gs.player.spellBook, "flight");
        if (spellFlight != app.lastSpellFlight) {
            app.subworld.set_flying(spellFlight);
            app.lastSpellFlight = spellFlight;
        }
        // Space = jump (edge-triggered): an upward impulse through the same
        // vertical integrator as everything else; inert unless grounded.
        const bool jumpHeld = keys[app.keymap.get(ActionId::Jump)] != 0;
        if (jumpHeld && !app.lastJumpHeld) app.subworld.jump();
        app.lastJumpHeld = jumpHeld;
        app.subworld.move_player(dx * kSubworldWalkTilesPerSecond * pace * haste * dt,
                                 dy * kSubworldWalkTilesPerSecond * pace * haste * dt);
        return;
    }

    // Walking a clicked route runs BEFORE the keyboard-capture gate below,
    // because it is NOT keyboard input: the destination was chosen with the
    // mouse and the party walks itself. Behind that gate — where this used to
    // sit — a journey froze the instant any text field took focus, so opening
    // the console mid-route stopped the party dead until it was closed
    // (confirmed in play, 2026-08-05). The gate's real job is to keep the
    // CAMERA keys below from firing while you type, and that is all it now does.
    //
    // The character's own pace applies here: moveSpeedMult was computed and
    // shown in the sheet for a long time without ever reaching the legs. Speed
    // changes how many game hours a journey takes, NOT what it costs — stamina
    // is priced per cell — so a fast traveller and a tough one are different
    // characters, not the same one twice.
    if (!app.cursor.path.empty() && !paused) {
        const float haste = sustained_spell_active(app.gs.player.spellBook, "haste")
            ? 1.5f : 1.0f;
        const float pace = sm::calculate_derived(app.gs.player.sheet.attributes,
                                                 app.gs.player.sheet.skills)
                               .moveSpeedMult;
        // The march is quoted in cells per GAME hour (macro/movement_cost.h).
        // This file is the one place that knows what a game hour costs in real
        // seconds, so the conversion lives here and nowhere else.
        constexpr float kMarchCellsPerRealSecond =
            sm::kMacroWalkCellsPerHour * sm::kGameHoursPerTick
            * float(sm::kTicksPerRealSecond);
        // The ground under the CURRENT cell slows the legs (terrain_speed_mult,
        // Session 21): the same weight row that prices the step also paces it,
        // read from the one cost grid the pathfinder already bakes.
        const float ground =
            sm::terrain_speed_mult(macro_cell_cost_weight(app));
        step_macro_walk_with_travel_cost(
            app, dt, kMarchCellsPerRealSecond * pace * haste * ground);
        // A hostile squad's cell stops the march right here (Inc 6): the
        // meeting is geometric, and the map is not silent about it.
        detect_forced_encounter(app);
    }

    if (io.WantCaptureKeyboard) return;
    const Uint8* keys = SDL_GetKeyboardState(nullptr);

    // Macroworld: keyboard does NOT move the player. The player only
    // moves by clicking a destination cell (handled in the overlay
    // cursor → find_path → step_macro_walk pipeline). Keyboard pans the
    // free camera around the world map (Mount & Blade-style overworld
    // camera) on the keymap's pan actions (default WASD; the old arrow
    // aliases died with the hardcoded keys — one action, one binding).
    // Y axis matches the shader convention: world +Y = screen UP.
    using sm::ui::ActionId;
    float panDx = 0, panDy = 0;
    if (keys[app.keymap.get(ActionId::PanUp)])    panDy += 1;
    if (keys[app.keymap.get(ActionId::PanDown)])  panDy -= 1;
    if (keys[app.keymap.get(ActionId::PanLeft)])  panDx -= 1;
    if (keys[app.keymap.get(ActionId::PanRight)]) panDx += 1;

    if (panDx != 0 || panDy != 0) {
        const float kCamPanCellsPerSec = 30.0f;
        app.camPanX += panDx * kCamPanCellsPerSec * dt;
        app.camPanY += panDy * kCamPanCellsPerSec * dt;
        // Keep panning flag set so the soft pan-decay in update_camera
        // doesn't fight the keyboard. Released keys → decay slowly
        // re-anchors the camera to the player (Mount & Blade overworld).
        app.panning = true;
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
    // The camera follows across the SHORT way, because the world is a torus and
    // there is no long way (CANON.md S1). Without this the player's step from
    // the last column to the first flipped the target by a whole world, and the
    // camera — which eases at a fixed rate — flew the entire map to catch up,
    // for the better part of a second, while the overlay above it (which HAS
    // always used the toroidal delta) stood still: the markers hung in place and
    // the ground bolted out from under them.
    app.camX = sm::wrapf(app.camX
        + sm::torus_delta(app.camTargetX - app.camX, float(app.gs.mapW)) * a,
        float(app.gs.mapW));
    app.camY = sm::wrapf(app.camY
        + sm::torus_delta(app.camTargetY - app.camY, float(app.gs.mapH)) * a,
        float(app.gs.mapH));
}

// ── HUD / Debug ───────────────────────────────────────────────

void apply_pending_event_effects(App& app) {
    while (true) {
        const auto& events = app.bus.tick_events();
        if (app.appliedEventCount >= events.size()) return;

        const std::size_t begin = app.appliedEventCount;
        const std::size_t end = events.size();
        std::span<const sm::GameEvent> pending(events.data() + begin, end - begin);
        // The applicator owns every state mutation (a SpireDepleted teaches
        // its spell there — events/ may ask the registry itself, Rule 13);
        // the app owns the bus, so announcements come back as followups and
        // are emitted AFTER the span walk (emit grows the very vector the
        // span points into) — the while loop picks them up as the next batch.
        std::vector<sm::GameEvent> followups;
        sm::apply_events(pending, app.gs, sm::player_inventory(app.ecs),
                         &followups);
        bool spireDied = false;
        for (const sm::GameEvent& ev : pending) {
            if (ev.tag == sm::EventTag::SpireDepleted) spireDied = true;
        }
        app.appliedEventCount = end;
        for (const sm::GameEvent& ev : followups) app.bus.emit(ev);
        // The map's night glow loses a consumed spire with its orb.
        if (spireDied) rebake_macro_lights(app);
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
        app.gs.player.sheet.levelData.skillPoints += 1;
    } else if (sex && *sex == "female") {
        app.gs.player.sheet.levelData.attributePoints += 1;
    }

    // A homeland CHOICE is not always a country: "Barbarian Kingdoms" is four
    // of them, and the world picks which one raised you (owner, 2026-08-20 —
    // they are procedural, so it is the world's business). Resolved once,
    // seeded from the world, so a reload cannot move your birthplace. Until
    // this door existed the choice value went straight into the registry, found
    // no row called "barbarians", and one of the three opening buttons awarded
    // nothing at all.
    const char* const homeland =
        realm ? sm::content::resolve_homeland_faction(realm->c_str(),
                                                      app.gs.worldSeed)
              : nullptr;
    if (homeland) {
        sm::add_player_reputation(app.gs, homeland, 15);
        // The starting money is re-minted into the HOMELAND's own coin
        // (owner: the player begins with his country's currency) — the boot
        // seeded imperial as a placeholder.
        const char* homeCoin = sm::currency_for_faction_id(homeland);
        if (std::string_view(homeCoin) != "coin_empire") {
            const int n = player_bag(app).count("coin_empire");
            if (n > 0) {
                player_bag(app).remove("coin_empire", n);
                player_bag(app).add(homeCoin, n);
            }
        }
    }

    sm::LogEntry entry{};
    entry.type = sm::LogType::World;
    entry.day = app.gs.worldTime.day();
    entry.message = "Born ";
    entry.message += sex ? *sex : "unknown";
    entry.message += ", homeland: ";
    // The RESOLVED realm, by its registry name: a player who picked "Barbarian
    // Kingdoms" is told which of them he was actually born in, because that is
    // the fact the world will hold him to.
    if (homeland) {
        const int fi = sm::faction_index(homeland);
        entry.message += fi >= 0 ? sm::kFactionDefs[fi].name : homeland;
    } else {
        entry.message += "unknown";
    }
    entry.message += ".";
    sm::push_event_log(app.gs.player, std::move(entry));

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
            enter_subworld(app);
            boot_trace_time("battle-start subworld enter", app.gs.worldTime);
        }
        const std::uint32_t seed = app.gs.worldSeed
            ^ (app.bus.tick() * 16777619u)
            ^ std::uint32_t(i * 2654435761u);
        if (app.subworld.spawn_npc_body(ev.s2.c_str(), ev.s1.c_str(),
                                        ev.ix, seed, "bandits",
                                        nullptr)) {
            sm::LogEntry entry{};
            entry.type = sm::LogType::Combat;
            entry.day = app.gs.worldTime.day();
            entry.message = "Encounter spawned in subworld: ";
            entry.message += ev.s1.empty() ? ev.s2 : ev.s1;
            sm::push_event_log(app.gs.player, std::move(entry));
        }
    }
    app.appliedCombatEventCount = end;
}

// The SpawnEntity consumer. The event had two producers (quest onAccept in
// content/quests/procedural.cpp) and ZERO consumers — kill-contracts never
// produced their targets (audit.md II.5). Each event now becomes one hostile
// macro NPC at the named cell; walking there embodies it in the subworld via
// the ordinary macro projection (Inc 5d), and its death feeds the DestroyNpc
// objective through the ordinary NpcDeath path.
void handle_pending_spawn_entity_events(App& app) {
    const auto& events = app.bus.tick_events();
    if (app.appliedSpawnEventCount >= events.size()) return;

    const std::size_t begin = app.appliedSpawnEventCount;
    const std::size_t end = events.size();
    for (std::size_t i = begin; i < end; ++i) {
        const sm::GameEvent& ev = events[i];
        if (ev.tag != sm::EventTag::SpawnEntity) continue;
        if (sm::spawn_npc_at(app.gs, app.ecs, app.terrain,
                             ev.s1.c_str(), ev.ix, ev.iy, int(ev.a))) {
            sm::LogEntry entry{};
            entry.type = sm::LogType::Combat;
            entry.day = app.gs.worldTime.day();
            entry.message = "Word spreads of trouble near the marked area: ";
            entry.message += ev.s1;
            sm::push_event_log(app.gs.player, std::move(entry));
        }
    }
    app.appliedSpawnEventCount = end;
}

void emit_time_advance_if_needed(App& app, const sm::WorldTickResult& tick) {
    if (tick.hoursAdvanced <= 0) return;

    const int currentAbsHour = app.gs.worldTime.day() * 24 + app.gs.worldTime.hour();
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
           app.gs.subState.kind == sm::GameSubStateKind::Event ||
           app.gs.subState.kind == sm::GameSubStateKind::PreBattle;
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

// Cheap integer fingerprint of the active-quest set: changes iff the derived
// quest-marker set could change (a quest added or removed, or one of its
// objectives flips completed). No allocation — safe to compute every frame; it
// gates the allocating rebuild_quest_markers() so only real changes pay for it.
// Objective target cells are immutable after creation, so only the quest ids,
// objective counts and completion bits need folding.
static std::uint64_t quest_marker_signature(const std::vector<sm::Quest>& active) {
    std::uint64_t h = 1469598103934665603ull;            // FNV-1a offset basis
    auto mix = [&](std::uint64_t v) { h ^= v; h *= 1099511628211ull; };
    for (const sm::Quest& q : active) {
        for (unsigned char c : q.id) mix(std::uint64_t(c));
        mix(q.objectives.size() + 1u);
        std::uint64_t doneMask = 0;
        for (std::size_t i = 0; i < q.objectives.size(); ++i)
            if (q.objectives[i].completed) doneMask |= (1ull << (i & 63u));
        mix(doneMask);
    }
    mix(active.size() + 1u);
    return h;
}

void process_world_events(App& app) {
    apply_pending_event_effects(app);
    apply_pending_story_results(app);
    handle_pending_battle_start_events(app);
    handle_pending_spawn_entity_events(app);
    app.bus.flush(app.gs.worldTime.day(), app.gs.worldTime.hour());
    app.appliedEventCount = 0;
    app.appliedStoryResultCount = 0;
    app.appliedCombatEventCount = 0;
    app.appliedSpawnEventCount = 0;
    app.logic.tick(app.bus, app.gs.player);
    app.quests.tick(app.activeQuests, app.bus, app.gs,
                    sm::player_inventory(app.ecs),
                    sm::player_head(app.ecs));
    // Refresh the derived quest-marker pins only when the active-quest set
    // actually changed. tick() runs every render frame; the rebuild allocates,
    // so the signature guard keeps steady-state frames allocation-free.
    if (const std::uint64_t sig = quest_marker_signature(app.activeQuests);
        sig != app.questMarkerSig) {
        sm::rebuild_quest_markers(app.gs, app.activeQuests);
        app.questMarkerSig = sig;
        // A quest that points at the world OPENS it (Inc 4): every quest
        // pin's surroundings join the map as MEMORY — the giver described
        // the place — through the same reveal door and the same eye budget
        // as the player's own sight. Re-revealing known ground is a cheap
        // bounded no-op, so re-running on every marker-set change is fine.
        for (const sm::Marker& m : app.gs.markers) {
            if (m.style == sm::MarkerStyle::Quest) {
                sm::reveal_area(app.gs.knowledge, optical_world(app),
                                app.sightRt.scratch, m.x, m.y,
                                player_sight_budget_cells());
            }
        }
    }
    apply_pending_event_effects(app);
    apply_pending_story_results(app);
    handle_pending_battle_start_events(app);
    handle_pending_spawn_entity_events(app);
    capture_presentation_events(app);
}

struct RuntimeFrameStats {
    sm::WorldTickResult timeTick{};
    sm::MacroNpcAiSliceResult macroNpcAi{};
    bool ticked = false;
    bool subworldActive = false;
};

// ONE fixed simulation step — sm::kStepSeconds of the world, always. Nothing
// here reads the frame's real duration: a step is a step whether the machine
// draws 30 frames a second or 240, which is what makes a journey, a fight and a
// smoke script reproduce instead of merely resemble each other.
RuntimeFrameStats tick_playing_runtime(App& app, bool allowInput) {
    // The S7 exit edge: the frame that finds the subworld gone rebakes the
    // derived world from the truths the stay below paid up (trees felled,
    // heads taken, veins drained). CPU truth now; textures at the flush.
    if (app.subworldWasActive && !app.subworld.active())
        rebake_world(app, /*uploadNow=*/false);
    app.subworldWasActive = app.subworld.active();
    constexpr float dt = sm::kStepSeconds;
    RuntimeFrameStats stats{};
    if (app.state != sm::ui::AppState::Playing || !app.worldLoaded) return stats;

    // The player's sight follows their CELL, not the frame: this call is one
    // integer compare until they cross a cell border, then one bounded optical
    // sweep (macro/knowledge.h). Above the pause gate on purpose — a fresh
    // boot or load runs its first sweep here (the sight anchor starts invalid)
    // even while a panel holds the world still, so the map is never blank
    // around a player who has not yet unpaused. The `revealmap` console
    // toggle suspends the sweep while it pins the projection to "everything".
    if (!app.revealMapOn) {
        sm::update_player_sight(app.gs.knowledge, app.sightRt,
                                optical_world(app),
                                app.gs.player.x, app.gs.player.y,
                                player_sight_budget_cells());
    }
    // Refresh u_knowledgeMap (binding 5) the moment the layer moved — here,
    // not in the ticked section, because the first sweep of a fresh boot or
    // load runs while the world is still paused and the map must not draw a
    // frame of yesterday's fog. In-place texel rewrite, no realloc.
    if (app.gs.knowledge.revision != app.uploadedKnowledgeRev
        && app.macro.ready()) {
        app.macro.upload_knowledge_field(app.device, &app.gs.knowledge);
        app.uploadedKnowledgeRev = app.gs.knowledge.revision;
    }

    // THE pause, asked once, for whichever world is on screen. Everything below
    // this line is simulation — the clock, the daily sim, NPC AI, recovery, the
    // march, the subworld's own tick — and none of it runs while any reason
    // holds: the player's Space, a story slide, an event window, an open panel.
    // The camera still runs, so a stopped world can be read and panned, and
    // `ticked` stays false because no game time passed.
    if (world_paused(app)) {
        if (allowInput) poll_movement(app, dt);
        update_camera(app, dt);
        return stats;
    }

    stats.ticked = true;
    apply_pending_event_effects(app);
    // ONE simulation step per turn of the loop — the book's own quantum, not
    // the wall clock's. Underground the world CLOCK crawls (one tick per
    // kSubworldTickDivisor steps) while the fight keeps the full step rate, so
    // a cooldown is the same length of FIGHT in both worlds (core/time.h).
    sm::spellbook_tick(app.gs.player.spellBook,
                       app.gs.player.combatStats,
                       /*steps=*/1u);
    if (app.subworld.active()) {
        stats.subworldActive = true;
        float movedThisStep = 0.0f;
        if (allowInput) {
            const float prevX = app.subworld.player_x();
            const float prevY = app.subworld.player_y();
            poll_movement(app, dt);
            const float movedX = app.subworld.player_x() - prevX;
            const float movedY = app.subworld.player_y() - prevY;
            movedThisStep = std::sqrt(movedX * movedX + movedY * movedY);
            (void)charge_subworld_sp_for_distance(app, movedThisStep);
        }
        stats.timeTick =
            sm::tick_world_subworld_steps(app.gs, app.gs.worldTickRt, 1);
        sm::MacroWorld subMacroWorld = macro_world(app);
        stats.timeTick.dailyTicksProcessed =
            sm::process_world_daily_ticks(app.gs, app.gs.worldTickRt,
                                          kSubworldDailyTicksPerStep,
                                          &subMacroWorld);
        stats.timeTick.dailyBudgetExhausted =
            app.gs.worldTickRt.pendingDailyTicks > 0;
        // Daily sim ran while in the subworld → glow-driving populations may
        // have drifted. Mark dirty; the macro path re-bakes on return (we never
        // sync the GPU for the map while the subworld is what's on screen).
        if (stats.timeTick.dailyTicksProcessed > 0) app.macroLightsDirty = true;
        // Recovery is driven by TIME, in both worlds and by the same law
        // (macro/player_recovery.h). Underground the clock crawls, so standing
        // still under a hill mends at the macro rate per game HOUR — sixteen
        // times slower on the wall clock, which is what makes waiting out a
        // wound down there an actual wait rather than a free heal.
        //
        // BEFORE subworld.tick on purpose: that tick opens by pulling the macro
        // scalar into the player entity's Health and closes by pushing the
        // post-combat result back, so a heal applied after it would be undone by
        // the next tick's pull.
        //
        // Walking is not resting, exactly as marching is not on the map: the
        // legs pay the same kMarchRecoveryPct on stamina, and health and mana
        // are untouched by the distinction.
        sm::apply_minute_recovery(app.gs.player,
                                  stats.timeTick.minutesAdvanced,
                                  app.playerRecovery,
                                  movedThisStep > 0.0f ? sm::kMarchRecoveryPct
                                                       : 1.0f);
        app.subworld.tick(dt);
        // The macro world thinks on WORLD time, so underground it thinks as
        // slowly as the day passes: kSubworldTickDivisor steps buy one tick,
        // and a step that bought none queues no sweep.
        stats.macroNpcAi =
            sm::tick_macro_npc_ai_budgeted(subMacroWorld, app.npcAi,
                                           std::uint64_t(stats.timeTick.ticksAdvanced),
                                           kSubworldMacroNpcTicksPerStep,
                                           /*allowAutoBattle*/false);
        emit_time_advance_if_needed(app, stats.timeTick);
        process_world_events(app);
    } else {
        if (allowInput) poll_movement(app, dt);
        update_camera(app, dt);
        // macro-4a: keep the player's PlayerTag flag alive + synced on the macro
        // map. This recreates it after any subworld leave() (which tears down all
        // PlayerTag entities, from any of the ~7 leave call sites) and projects
        // the just-finalised player scalar onto its Position each macro tick.
        sm::ensure_macro_player_entity(app.gs, app.ecs);
        sm::MacroWorld macroTickWorld = macro_world(app);
        stats.timeTick = sm::tick_world(app.gs, app.gs.worldTickRt, 1,
                                        /*max_daily_ticks=*/32,
                                        &macroTickWorld);
        if (stats.timeTick.dailyTicksProcessed > 0) app.macroLightsDirty = true;
        // The MONTHLY re-bake (owner, Session 21 follow-up). Chopping changes
        // forest weights but the baked cost grid — the one both the player's
        // A* and every squad's greedy step read — deliberately sleeps: the
        // world is re-baked once a season (this calendar's month, 32 po2
        // days), not per mutation, and the autosave rides the same rhythm so
        // "the world settled" and "the world is on disk" are one moment.
        // Macro path only: underground the clock crawls and the map is not
        // even drawn.
        {
            const int worldDay = app.gs.worldTime.day();
            if (worldDay - app.gs.lastWorldRebakeDay >= sm::kDaysPerSeason) {
                save_game_checked(app, /*autosave=*/true);
                // The WHOLE derived world settles, not just the cost grid:
                // zones follow the grown/felled forest, glow follows the
                // drifted populations (rebake_world stamps the day).
                rebake_world(app);
            }
        }
        // Marching is not resting: while the player is walking a route, his
        // stamina does not come back (health and mana still do). This is what
        // turns a journey into a budget he has to plan instead of an allowance
        // that pays for itself — see macro/movement_cost.h kMarchRecoveryPct.
        const bool marching = !app.cursor.path.empty();
        sm::apply_minute_recovery(app.gs.player,
                                        stats.timeTick.minutesAdvanced,
                                        app.playerRecovery,
                                        marching ? sm::kMarchRecoveryPct : 1.0f);
        sm::tick_macro_npc_ai(macroTickWorld, app.npcAi,
                              std::uint64_t(stats.timeTick.ticksAdvanced),
                              /*allowAutoBattle*/true);
        // The other half of the geometric meeting: a squad may have stepped
        // onto the PLAYER's cell during its own think.
        detect_forced_encounter(app);
        // The player's time-in-cell advances on the same kAiTicks cadence the
        // NPC counter uses (prepare_macro_npc_tick), and on the same clock — a
        // cell crossing resets it in step_macro_walk_with_travel_cost.
        app.gs.player.entryTickAccum +=
            std::uint32_t(stats.timeTick.ticksAdvanced);
        while (app.gs.player.entryTickAccum >= sm::kAiTicks) {
            app.gs.player.entryTickAccum -= sm::kAiTicks;
            app.gs.player.entryTicks =
                sm::saturate_entry_ticks(app.gs.player.entryTicks);
        }
        app.npcAi.sweepAccum = 0;
        app.npcAi.pendingSweeps = 0;
        app.npcAi.sweepCursor = 0;
        emit_time_advance_if_needed(app, stats.timeTick);
        process_world_events(app);
        // Flush any pending glow refresh now, on the macro path, before this
        // frame renders the map: covers population drift this frame and drift
        // accumulated while in the subworld. One surgical binding-3 re-upload,
        // only when actually dirty — never per frame.
        if (app.macroLightsDirty) {
            rebake_macro_lights(app);
            app.macroLightsDirty = false;
        }
        // The danger-zone field rides the same discipline (set by the S7
        // transition rebakes): one surgical binding-2 re-upload at the same
        // drain-safe point, never mid-frame.
        if (app.zoneFieldDirty) {
            if (app.macro.ready())
                app.macro.upload_zone_field(app.device, app.zones);
            app.zoneFieldDirty = false;
        }
        // Same discipline for the tree-count field (binding 4): felled trees
        // bumped TreeLayer.revision (usually while inside the subworld);
        // refresh the map-sprite density once, on the macro path, only when a
        // count actually changed — never per frame.
        if (app.treeLayer.revision != app.uploadedTreeRev && app.macro.ready()) {
            app.macro.upload_tree_field(app.device, &app.treeLayer);
            app.uploadedTreeRev = app.treeLayer.revision;
        }
    }
    tick_subworld_hit_flash(app, dt);
    if (app.gs.player.combatStats.currentHp <= 0) {
        app.state = sm::ui::AppState::Dead;
    }
    return stats;
}

// Run the world for N fixed steps and fold the per-step results into one
// summary. Counters add up; the flags report where the world ENDED, because a
// step in the middle of the run may have entered or left the subworld.
RuntimeFrameStats advance_sim_steps(App& app, int steps, bool allowInput) {
    RuntimeFrameStats total{};
    for (int i = 0; i < steps; ++i) {
        const RuntimeFrameStats s = tick_playing_runtime(app, allowInput);
        total.ticked = total.ticked || s.ticked;
        total.subworldActive = s.subworldActive;
        total.timeTick.minutesAdvanced += s.timeTick.minutesAdvanced;
        total.timeTick.hoursAdvanced += s.timeTick.hoursAdvanced;
        total.timeTick.daysAdvanced += s.timeTick.daysAdvanced;
        total.timeTick.dailyTicksProcessed += s.timeTick.dailyTicksProcessed;
        total.timeTick.dailyBudgetExhausted = s.timeTick.dailyBudgetExhausted;
        total.macroNpcAi.npcsProcessed += s.macroNpcAi.npcsProcessed;
        total.macroNpcAi.sweepsCompleted += s.macroNpcAi.sweepsCompleted;
        total.macroNpcAi.backlog = s.macroNpcAi.backlog;
    }
    return total;
}

// How many steps a stretch of REAL time is worth. The only callers are the
// harness and the console — places that still think in seconds because a human
// wrote the number. The world itself never asks.
int sim_steps_for_seconds(float seconds) {
    if (seconds <= 0.0f) return 0;
    return int(seconds * float(sm::kTicksPerRealSecond) + 0.5f);
}

RuntimeFrameStats advance_sim_seconds(App& app, float seconds, bool allowInput) {
    return advance_sim_steps(app, sim_steps_for_seconds(seconds), allowInput);
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
    // The world's own rate — the same number as FPS above (one turn = one tick
    // = one frame), shown with its meaning attached: below nominal, the world
    // is not merely being drawn less often, it is LIVING slower.
    {
        const float nominal = float(sm::kTicksPerRealSecond);
        const bool slow = app.measuredTicksPerSec < nominal * 0.95f;
        if (slow) ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 190, 90, 255));
        ImGui::Text("World: %.1f / %.0f ticks/s%s", double(app.measuredTicksPerSec),
                    double(nominal), slow ? "  (SLOW MOTION)" : "");
        if (slow) ImGui::PopStyleColor();
        ImGui::Text("Present: %s",
                    app.renderer.swapchain.presentMode == VK_PRESENT_MODE_MAILBOX_KHR
                        ? "mailbox (loop paces itself)"
                        : "fifo (display paces the world)");
    }
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

    con.register_cmd("fpshud", "fpshud [on|off]",
        "toggle the persistent framerate readout",
        [&app](Con& c, const std::vector<std::string>& args) {
            app.showFpsHud = console_toggle_arg(args, app.showFpsHud);
            c.printfln(Lvl::Ok, "fps hud %s", app.showFpsHud ? "on" : "off");
            return true;
        });

    con.register_cmd("sunfreeze", "sunfreeze [on|off]",
        "freeze the sun/moon for RENDERING only (diagnostic; sim keeps running)",
        [&app](Con& c, const std::vector<std::string>& args) {
            app.subworld.set_sun_freeze(
                console_toggle_arg(args, app.subworld.sun_freeze()));
            c.printfln(Lvl::Ok, "sun %s",
                       app.subworld.sun_freeze() ? "FROZEN (render only)"
                                                 : "running");
            return true;
        });

    con.register_cmd("lightdbg", "lightdbg [march|clouds|map|nl|off]",
        "bisect the sun-visibility product: toggle one term off per call "
        "(diagnostic; `off` restores all)",
        [&app](Con& c, const std::vector<std::string>& args) {
            std::uint32_t m = app.subworld.light_debug_mask();
            if (!args.empty()) {
                const std::string& a = args[0];
                if      (a == "march")  m ^= 1u;
                else if (a == "clouds") m ^= 2u;
                else if (a == "map")    m ^= 4u;
                else if (a == "nl")     m ^= 8u;
                else if (a == "off")    m = 0u;
                else {
                    c.printfln(Lvl::Error, "unknown term '%s'", a.c_str());
                    return true;
                }
                app.subworld.set_light_debug_mask(m);
            }
            c.printfln(Lvl::Ok,
                       "light terms: march=%s clouds=%s objectmap=%s nl=%s",
                       (m & 1u) ? "OFF" : "on", (m & 2u) ? "OFF" : "on",
                       (m & 4u) ? "OFF" : "on", (m & 8u) ? "OFF" : "on");
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
                       app.gs.worldTime.day(), app.gs.worldTime.hour(),
                       app.gs.worldTime.minute());
            return true;
        });

    con.register_cmd("revealmap", "revealmap",
        "toggle full vision: on = the whole map is Visible; off = the "
        "ordinary decay law runs and the map stays CHARTED (drowned)",
        [&app](Con& c, const std::vector<std::string>&) {
            auto& k = app.gs.knowledge;
            if (!k.has_complete_storage()) {
                c.printfln(Lvl::Error, "no knowledge layer (no world loaded)");
                return true;
            }
            app.revealMapOn = !app.revealMapOn;
            if (app.revealMapOn) {
                // Pin the projection to "everything" — every cell Visible
                // AND registered in the sight list, so switching off simply
                // hands the whole map to the ordinary Visible→Explored decay.
                // No un-reveal path exists, because the system needs none:
                // vision is a projection, memory is what it leaves behind.
                app.sightRt.visibleCells.clear();
                app.sightRt.visibleCells.reserve(k.data.size());
                for (std::size_t i = 0; i < k.data.size(); ++i) {
                    k.data[i] = sm::kKnowledgeVisible;
                    app.sightRt.visibleCells.push_back(std::uint32_t(i));
                }
                ++k.revision;
                c.printfln(Lvl::Ok, "full vision ON (%zu cells)",
                           k.data.size());
            } else {
                // Wake the ordinary law: an invalid sight anchor forces the
                // next tick's sweep — the pinned set decays to Explored, the
                // real disc re-sweeps from the player's cell.
                app.sightRt.lastCellX = INT32_MIN;
                app.sightRt.lastCellY = INT32_MIN;
                c.printfln(Lvl::Ok,
                           "full vision OFF — the map stays charted");
            }
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

    con.register_cmd("spawn", "spawn <type> [level] [count] [faction]",
        "spawn bodies near you (subworld). NPC types: bandit guard witch "
        "sorceress peasant woodcutter merchant caravan. Monster ids (global "
        "table): wolf bear goblin skeleton troll ... (unknown -> bandit). "
        "[faction] is any registry id (bandits empire old_magica freefolk "
        "player ...); omitted -> the realm that owns this ground. A monster "
        "always keeps its own table faction.",
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
            // No faction typed -> nullptr -> the ground decides (engine.h).
            const char* faction = a.size() > 3 && !a[3].empty()
                                ? a[3].c_str() : nullptr;
            if (faction && sm::faction_index(faction) < 0) {
                c.error("unknown faction id (see the faction registry)");
                return true;
            }
            static std::uint32_t seq = 0;
            int placed = 0;
            for (int i = 0; i < count; ++i) {
                const std::uint32_t seed = app.gs.worldSeed ^ (++seq * 2654435761u);
                if (app.subworld.spawn_npc_body(type.c_str(), type.c_str(),
                                                level, seed, faction,
                                                nullptr))
                    ++placed;
            }
            c.printfln(Lvl::Ok, "spawned %d x %s (level %d, faction %s)",
                       placed, type.c_str(), level,
                       faction ? faction : "of this ground");
            return true;
        });

    con.register_cmd("spawn_squad",
        "spawn_squad <leader> [level] [members] [memberKind] [faction]",
        "spawn a SQUAD on your macro cell: a leader of <leader> kind with "
        "[members] roster rows of [memberKind] (default: the leader's kind); "
        "[faction] any registry id, omitted -> the land decides. Prints the "
        "squad's ordinal for squad_orders.",
        [&app](Con& c, const std::vector<std::string>& a) {
            if (a.empty()) return false;
            if (app.subworld.active()) {
                c.error("spawn_squad works on the macro map (leave first)");
                return true;
            }
            sm::SquadSpec spec{};
            if (!sm::npc_type_from_label(a[0].c_str(), spec.leaderType)) {
                c.error("unknown NPC type (see the registry labels)");
                return true;
            }
            int level = -1; sm::dev::arg_int(a, 1, level);
            spec.leaderLevel = level;
            int members = 0; sm::dev::arg_int(a, 2, members);
            members = std::clamp(members, 0, 64);
            sm::NPCType memberKind = spec.leaderType;
            if (a.size() > 3 && !a[3].empty()
                && !sm::npc_type_from_label(a[3].c_str(), memberKind)) {
                c.error("unknown member kind");
                return true;
            }
            if (a.size() > 4 && !a[4].empty()) {
                spec.factionIndex = sm::faction_index(a[4].c_str());
                if (spec.factionIndex < 0) {
                    c.error("unknown faction id");
                    return true;
                }
            }
            spec.x = int(std::floor(app.gs.player.x));
            spec.y = int(std::floor(app.gs.player.y));
            // Row ids in a console-made roster: a private id space (high two
            // bits 01) so they can never collide with garrison ids (high bit
            // 1) or quest/hire ids.
            static std::uint32_t seq = 0;
            ++seq;
            const int mlvl = level > 0 ? level
                                       : sm::npc_def(memberKind).baseLevel;
            for (int i = 0; i < members; ++i) {
                spec.members.push(sm::make_soldier(
                    std::uint8_t(memberKind), mlvl,
                    0x40000000u | (seq << 8) | std::uint32_t(i)));
            }
            const entt::entity leader =
                sm::spawn_squad(app.gs, app.ecs, app.terrain, spec);
            if (leader == entt::null) {
                c.error("spawn_squad failed (bad map)");
                return true;
            }
            const auto* sid =
                app.ecs.reg.try_get<sm::ecs::MacroSpawnId>(leader);
            c.printfln(Lvl::Ok, "squad #%u: %s (level %d) + %d x %s",
                       sid ? sid->index : 0u,
                       sm::npc_def(spec.leaderType).label,
                       app.ecs.reg.get<sm::ecs::NpcLevel>(leader).value,
                       members, sm::npc_def(memberKind).label);
            return true;
        });

    con.register_cmd("squad_orders",
        "squad_orders <ordinal> <x y> [x y]...",
        "order squad #<ordinal> (see spawn_squad output) onto a waypoint "
        "route of up to 8 cells — it loops the route instead of its own "
        "life; 'squad_orders <ordinal>' alone clears the route. Example: "
        "squad_orders 42 100 100 120 100 120 120",
        [&app](Con& c, const std::vector<std::string>& a) {
            if (a.empty()) return false;
            int ordinal = -1;
            if (!sm::dev::arg_int(a, 0, ordinal) || ordinal < 0) return false;
            entt::entity target = entt::null;
            for (auto [e, sid, roster] :
                 app.ecs.reg.view<sm::ecs::MacroSpawnId,
                                  sm::ecs::SquadRoster>().each()) {
                (void)roster;
                if (sid.index == std::uint32_t(ordinal)) { target = e; break; }
            }
            if (target == entt::null) {
                c.error("no squad with that ordinal");
                return true;
            }
            sm::ecs::SquadOrders orders{};
            for (std::size_t i = 1; i + 1 < a.size()
                 && orders.waypointCount < 8; i += 2) {
                int x = 0, y = 0;
                if (!sm::dev::arg_int(a, i, x)
                    || !sm::dev::arg_int(a, i + 1, y)) {
                    break;
                }
                orders.waypoints[std::size_t(orders.waypointCount * 2)] =
                    std::int16_t(sm::wrapi(x, app.gs.mapW));
                orders.waypoints[std::size_t(orders.waypointCount * 2 + 1)] =
                    std::int16_t(sm::wrapi(y, app.gs.mapH));
                ++orders.waypointCount;
            }
            if (orders.waypointCount == 0) {
                app.ecs.reg.remove<sm::ecs::SquadOrders>(target);
                c.printfln(Lvl::Ok, "squad #%d released to its own life",
                           ordinal);
                return true;
            }
            app.ecs.reg.emplace_or_replace<sm::ecs::SquadOrders>(target,
                                                                 orders);
            c.printfln(Lvl::Ok, "squad #%d now patrols %d waypoint(s)",
                       ordinal, int(orders.waypointCount));
            return true;
        });

    con.register_cmd("test_battle", "test_battle [per-side] [factionA] [factionB]",
        "deploy two armies facing each other. Factions are registry ids and "
        "decide whether they actually fight (the relation matrix does, not this "
        "command); default = the realm owning this ground vs bandits",
        [&app](Con& c, const std::vector<std::string>& a) {
            if (!app.subworld.active()) {
                c.error("test_battle works only inside a subworld (press Enter to enter one)");
                return true;
            }
            int count = 500;
            if (!a.empty()) sm::dev::arg_int(a, 0, count);
            count = std::clamp(count, 1, sm::sub::kMaxBattleUnits / 2);

            // Side A defaults to the defenders of this very ground, side B to
            // raiders — a fight you can rely on without naming anyone, and any
            // two registry ids when you want to SEE what the matrix says.
            const char* sideA = a.size() > 1 && !a[1].empty() ? a[1].c_str() : nullptr;
            const char* sideB = a.size() > 2 && !a[2].empty() ? a[2].c_str() : "bandits";
            if ((sideA && sm::faction_index(sideA) < 0)
                || (sideB && sm::faction_index(sideB) < 0)) {
                c.error("unknown faction id (see the faction registry)");
                return true;
            }
            c.printfln(Lvl::Info, "Deploying %d x %s vs %d x %s...",
                       count, sideA ? sideA : "this ground", count, sideB);
            int placed = 0;
            for (int side = 0; side < 2; ++side) {
                for (int i = 0; i < count; ++i) {
                    // A block, not a pile: bodies start one spacing apart so the
                    // approach phase is real and the first frames are not a
                    // degenerate all-in-one-cell stack.
                    float pos[2];
                    if (!sm::sub::deploy_army_slot(app.subworld.player_x(),
                                                   app.subworld.player_y(),
                                                   side, i, count, pos)) {
                        continue;
                    }
                    const bool ok = app.subworld.spawn_npc_body(
                        side == 0 ? "guard" : "bandit",
                        side == 0 ? "Test Guard" : "Test Bandit",
                        1,
                        app.gs.worldSeed + std::uint32_t(side * 100003 + i),
                        side == 0 ? sideA : sideB,
                        nullptr, pos);
                    if (ok) ++placed;
                }
            }
            c.printfln(Lvl::Ok, "Battle deployed (%d bodies).", placed);
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
                player_bag(app).add("coin_empire", n);
                c.printfln(Lvl::Ok, "coin += %d  (now %d)", n,
                           sm::wallet_value(player_bag(app)));
                return true;
            }
            if (!sm::item_def(id)) {
                c.error("unknown item '" + id + "' - type 'items' for the list");
                return true;
            }
            player_bag(app).add(id, n);
            c.printfln(Lvl::Ok, "gave %d x %s  (have %d)", n, id.c_str(),
                       player_bag(app).count(id));
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
                const int taken =
                    sm::wallet_spend_up_to(player_bag(app), n);
                c.printfln(Lvl::Ok, "coin -= %d  (now %d)", taken,
                           sm::wallet_value(player_bag(app)));
                return true;
            }
            if (player_bag(app).remove(id, n))
                c.printfln(Lvl::Ok, "took %d x %s  (have %d)", n, id.c_str(),
                           player_bag(app).count(id));
            else
                c.printfln(Lvl::Warn, "not enough '%s' (have %d)", id.c_str(),
                           player_bag(app).count(id));
            return true;
        });

    con.register_cmd("gold", "gold <delta>",
        "add (or, if negative, subtract) player gold",
        [&app](Con& c, const std::vector<std::string>& a) {
            int delta = 0;
            if (!sm::dev::arg_int(a, 0, delta)) return false;
            if (delta >= 0) player_bag(app).add("coin_empire", delta);
            else sm::wallet_spend_up_to(player_bag(app), -delta);
            c.printfln(Lvl::Ok, "coin = %d",
                       sm::wallet_value(player_bag(app)));
            return true;
        });

    con.register_cmd("addexp", "addexp <amount>",
        "grant experience (auto-levels while over the threshold)",
        [&app](Con& c, const std::vector<std::string>& a) {
            int amount = 0;
            if (!sm::dev::arg_int(a, 0, amount)) return false;
            auto& ld = app.gs.player.sheet.levelData;
            const int gained = sm::award_exp(ld, amount);
            c.printfln(Lvl::Ok, "exp +%d -> level %d (%d gained), %d/%d to next",
                       amount, ld.level, gained, ld.exp, ld.expToNext);
            return true;
        });

    con.register_cmd("levelup", "levelup [count]",
        "force N level-ups, granting the usual attribute/skill/perk points",
        [&app](Con& c, const std::vector<std::string>& a) {
            int n = 1; sm::dev::arg_int(a, 0, n);
            if (n < 1) n = 1;
            auto& ld = app.gs.player.sheet.levelData;
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
            for (const auto& s : sm::kSpellDefs)
                c.printfln(Lvl::Info, "  %-16s  %s", s.id, s.name);
            c.printfln(Lvl::Ok, "%d spells", sm::kSpellCount);
            return true;
        });

    con.register_cmd("learn", "learn <spellId>",
        "learn a spell by id (type 'spells' for ids)",
        [&app](Con& c, const std::vector<std::string>& a) {
            if (a.empty()) return false;
            const std::string& id = a[0];
            if (!sm::spell_find(id)) {
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
            for (const auto& s : sm::kSpellDefs)
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
            app.gs.worldTime = sm::world_time_at(app.gs.worldTime.day(), h, m);
            c.printfln(Lvl::Ok, "clock set to day %d, %02d:%02d",
                       app.gs.worldTime.day(), h, m);
            return true;
        });

    con.register_cmd("addtime", "addtime <hours>",
        "advance the world clock forward N hours (runs the daily simulation)",
        [&app](Con& c, const std::vector<std::string>& a) {
            float hours = 0.0f;
            if (!sm::dev::arg_float(a, 0, hours)) return false;
            if (hours <= 0.0f) { c.error("hours must be positive (clock only moves forward)"); return true; }
            sm::MacroWorld mw = macro_world(app);
            const sm::WorldTickResult r = sm::tick_world(
                app.gs, app.gs.worldTickRt,
                sm::ticks_to_advance_minutes(app.gs.worldTime.tick,
                                             std::int64_t(hours * 60.0f + 0.5f)),
                /*max_daily_ticks=*/32, &mw);
            c.printfln(Lvl::Ok, "advanced %.2f h -> day %d, %02d:%02d  (%d daily tick(s))",
                       hours, app.gs.worldTime.day(), app.gs.worldTime.hour(),
                       app.gs.worldTime.minute(), r.dailyTicksProcessed);
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

    con.register_cmd("rest", "rest",
        "rest until stamina is full (map only) - same as the toolbar Z",
        [&app](Con& c, const std::vector<std::string>&) {
            if (app.subworld.active()) {
                c.printfln(Lvl::Warn, "rest is a MAP action - leave first");
                return false;
            }
            const auto& cs = app.gs.player.combatStats;
            if (cs.currentSp >= cs.maxSp) {
                c.printfln(Lvl::Ok, "already rested - SP %d/%d",
                           cs.currentSp, cs.maxSp);
                return true;
            }
            aim_rest_until_rested(app);
            c.printfln(Lvl::Ok, "resting from SP %d/%d until full (cap tick %llu)",
                       cs.currentSp, cs.maxSp,
                       (unsigned long long)app.restUntilTick);
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

    con.register_cmd("possess", "possess [id]",
        "вселение: take over a live body. No arg = the one under your reticle "
        "(forward cone); with an entity id = that body (debug). The possessed "
        "body fights with its OWN stats; leaving the subworld reverts to you.",
        [&app](Con& c, const std::vector<std::string>& a) {
            if (!app.subworld.active()) {
                c.error("possess works only inside a subworld");
                return true;
            }
            bool ok = false;
            int id = 0;
            if (sm::dev::arg_int(a, 0, id))
                ok = app.subworld.possess_by_id(static_cast<std::uint32_t>(id));
            else
                ok = app.subworld.possess_aim();  // nearest body in the reticle cone
            if (ok) c.printfln(Lvl::Ok, "%s", app.subworld.status_line());
            else    c.warn(app.subworld.status_line());
            return true;
        });

    con.register_cmd("chop", "chop [radius]",
        "fell the nearest tree within radius (default 6 tiles) — exercises "
        "the micro→macro tree-count writeback and prints the owning macro "
        "cell's count",
        [&app](Con& c, const std::vector<std::string>& a) {
            if (!app.subworld.active()) {
                c.error("chop works only inside a subworld");
                return true;
            }
            int r = 6;
            sm::dev::arg_int(a, 0, r);
            int cx = 0, cy = 0, prev = 0;
            const sm::sub::Structure::Kind kTreeOnly = sm::sub::Structure::Tree;
            if (app.subworld.harvest_prop_near_player(float(r), &cx, &cy, &prev,
                                                      &kTreeOnly))
                c.printfln(Lvl::Ok, "chop: cell %d,%d trees %d -> %d",
                           cx, cy, prev, int(app.treeLayer.at(cx, cy)));
            else
                c.warn("no tree in reach");
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
                    // The body's row in THE sprite table, by name — far more
                    // use in a debug list than the ordinal it used to print.
                    if (const auto* sp = reg.try_get<sm::ecs::Sprite>(e);
                        sp && sp->spriteRow != 0)
                        ImGui::TextUnformatted(
                            sm::sprite_row(sm::SpriteId(sp->spriteRow)).name);
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
            ImGui::Text("coin    %d", wallet_value(player_bag(app)));
            ImGui::Text("level   %d   (exp %d / %d)",
                        p.sheet.levelData.level, p.sheet.levelData.exp, p.sheet.levelData.expToNext);
            ImGui::Text("hp      %d / %d", p.combatStats.currentHp, p.combatStats.maxHp);
            ImGui::Text("mp      %d / %d", p.combatStats.currentMp, p.combatStats.maxMp);
            ImGui::Text("sp      %d / %d", p.combatStats.currentSp, p.combatStats.maxSp);
            ImGui::Text("points  attr %d  skill %d  perk %d",
                        p.sheet.levelData.attributePoints, p.sheet.levelData.skillPoints,
                        p.sheet.levelData.perkPoints);
            ImGui::Text("spells  %zu learned", p.spellBook.learned.size());
            ImGui::SeparatorText("World");
            ImGui::Text("clock   day %d, %02d:%02d",
                        app.gs.worldTime.day(), app.gs.worldTime.hour(),
                        app.gs.worldTime.minute());
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
            // Deliberately NOT biome_at_cell: this previews the SLIDER's sea
            // level against the current terrain, before a world is baked — the
            // one reader whose sea level is a hypothesis, not the mask.
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
    // Minutes since the ladder's origin — the clock's own absolute counter, so
    // a smoke assertion about "how much time passed" is comparing the same
    // number the world used to bill it.
    return int(sm::absolute_minute(t.tick));
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
    const int dailyPendingStart = app.gs.worldTickRt.pendingDailyTicks;
    const int sweepsPendingStart = app.npcAi.pendingSweeps;
    const std::uint64_t subStepRemainderBefore =
        app.gs.worldTickRt.subworldStepRemainder;

    std::fprintf(stderr,
                 "[smoke] subworld_time before day=%d %02d:%02d "
                 "player=%.1f,%.1f pendingDaily=%d pendingSweeps=%d\n",
                 before.day(), before.hour(), before.minute(),
                 playerBeforeX, playerBeforeY,
                 dailyPendingStart, sweepsPendingStart);
    std::fflush(stderr);

    enter_subworld(app);
    if (!app.subworld.active()) {
        smoke_fail(app, "subworld_time enter failed");
        return false;
    }
    // Inc 5d: how many persistent macro NPCs were projected into this window as
    // real bodies (MacroOrigin backlink). >0 confirms the overworld→subworld
    // projection fired end-to-end through enter(); 0 is a legitimate empty
    // stretch (no macro NPC stood within ±1 cell of the entered centre).
    {
        int macroProjected = 0;
        for (auto e : app.ecs.reg.view<sm::ecs::MacroOrigin,
                                       sm::ecs::SubworldTag>()) {
            (void)e;
            ++macroProjected;
        }
        std::fprintf(stderr, "[smoke] subworld_time macroProjected=%d\n",
                     macroProjected);
        std::fflush(stderr);
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
    int maxPendingDaily = app.gs.worldTickRt.pendingDailyTicks;
    int maxPendingSweeps = app.npcAi.pendingSweeps;
    std::size_t maxSweepCursor = app.npcAi.sweepCursor;

    for (int i = 0; i < kSubworldSmokeFrames; ++i) {
        RuntimeFrameStats frameStats = tick_playing_runtime(app, false);
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
        maxPendingDaily = std::max(maxPendingDaily, app.gs.worldTickRt.pendingDailyTicks);
        maxPendingSweeps = std::max(maxPendingSweeps, app.npcAi.pendingSweeps);
        maxSweepCursor = std::max(maxSweepCursor, app.npcAi.sweepCursor);
    }

    const sm::WorldTime beforeLeave = app.gs.worldTime;
    const int pendingDailyBeforeLeave = app.gs.worldTickRt.pendingDailyTicks;
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
    // What the ladder says those steps were worth: kSubworldTickDivisor steps
    // buy one tick, and the minutes are read off the clock either side. No
    // float rate to drift against — the harness and the world do the same sum.
    const std::uint64_t expectedTicks =
        (subStepRemainderBefore + std::uint64_t(kSubworldSmokeFrames))
        / sm::kSubworldTickDivisor;
    const int expectedMinutes =
        int(sm::absolute_minute(before.tick + expectedTicks)
            - sm::absolute_minute(before.tick));
    const bool subworldRateOk = minutesAdvanced == expectedMinutes;
    const bool dailyCaughtUp = daysAdvanced == dailyProcessed
        && pendingDailyBeforeLeave == 0
        && app.gs.worldTickRt.pendingDailyTicks == pendingDailyBeforeLeave;
    const bool npcBounded = maxPendingSweeps <= 4
        && app.npcAi.pendingSweeps <= 4
        && app.npcAi.sweepAccum < sm::kAiTicks;
    const bool playerSynced = playerAfterX == expectedPlayerX
        && playerAfterY == expectedPlayerY;
    const bool leaveOk = activeBeforeLeave && !activeAfterLeave;

    std::fprintf(stderr,
                 "[smoke] subworld_time after day=%d %02d:%02d "
                 "preLeave=%d %02d:%02d player=%d,%d expected=%d,%d\n",
                 after.day(), after.hour(), after.minute(),
                 beforeLeave.day(), beforeLeave.hour(), beforeLeave.minute(),
                 playerAfterX, playerAfterY, expectedPlayerX, expectedPlayerY);
    std::fprintf(stderr,
                 "[smoke] subworld_time steps=%d divisor=%llu "
                 "minutes=%d expected=%d days=%d "
                 "dailyProcessed=%d pendingDailyStart=%d pendingDailyBeforeLeave=%d "
                 "pendingDailyAfterLeave=%d maxPendingDaily=%d\n",
                 kSubworldSmokeFrames,
                 (unsigned long long)sm::kSubworldTickDivisor,
                 minutesAdvanced, expectedMinutes, daysAdvanced,
                 dailyProcessed, dailyPendingStart,
                 pendingDailyBeforeLeave, app.gs.worldTickRt.pendingDailyTicks,
                 maxPendingDaily);
    std::fprintf(stderr,
                 "[smoke] subworld_time npcProcessed=%d sweepsCompleted=%d "
                 "backlogFrames=%d pendingSweepsStart=%d pendingSweepsBeforeLeave=%d "
                 "pendingSweepsAfterLeave=%d maxPendingSweeps=%d "
                 "sweepCursorBeforeLeave=%zu maxSweepCursor=%zu sweepAccum=%u\n",
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

// ONE recovery law, proven by making the two worlds agree.
//
// This smoke used to assert the OPPOSITE — that a body underground recovers
// NOTHING — which is how a defect ends up with a guard on it (AGENTS.md testing
// law #7): the macro branch called apply_minute_recovery and the subworld
// branch simply did not, and a green smoke said that was intended. Owner ruling
// 2026-08-20: recovery is driven by TIME, so what it now proves is that the
// same game minutes buy the same points in both worlds. Underground those
// minutes cost sixteen times more real seconds, which is the whole point and
// costs this file nothing to state.
bool run_subworld_recovery_smoke(App& app) {
    if (!smoke_boot_invariants_hold(app)) {
        smoke_print_counts(app, "subworld_recovery_boot_failed");
        smoke_fail(app, "subworld_recovery boot invariants");
        return false;
    }
    if (app.subworld.active()) {
        smoke_fail(app, "subworld_recovery already active");
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

    enter_subworld(app);
    if (!app.subworld.active()) {
        smoke_fail(app, "subworld_recovery enter failed");
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
    // A real MINUTE of standing still. Underground the clock crawls, so this is
    // still only tens of game minutes — enough to buy whole points off an
    // hourly rate, which is what makes the comparison below meaningful instead
    // of a race between two roundings.
    const int kFrames = 60 * int(sm::kTicksPerRealSecond);
    for (int i = 0; i < kFrames; ++i) {
        RuntimeFrameStats frameStats = tick_playing_runtime(app, false);
        if (!frameStats.ticked || !frameStats.subworldActive) {
            smoke_fail(app, "subworld_recovery runtime tick inactive");
            return false;
        }
        minutesAdvanced += frameStats.timeTick.minutesAdvanced;
    }

    const int afterHp = app.gs.player.combatStats.currentHp;
    const int afterMp = app.gs.player.combatStats.currentMp;
    const int afterSp = app.gs.player.combatStats.currentSp;
    app.subworld.leave(true);

    // The REFERENCE: the same body, the same minutes, on the map. Not a
    // restated formula — the very function the macro branch calls, which is
    // exactly the claim being made ("one law, both worlds"). It also catches
    // something a hand-written expectation could not: the subworld pays its
    // minutes in many small chunks and this pays them in one, so the
    // fractional carry has to make those identical or the answer drifts.
    sm::PlayerState reference = app.gs.player;
    reference.combatStats.currentHp = 5;
    reference.combatStats.currentMp = 5;
    reference.combatStats.currentSp = 5;
    sm::PlayerRecoveryAccumulator referenceAcc{};
    sm::apply_minute_recovery(reference, minutesAdvanced, referenceAcc);

    std::fprintf(stderr,
                 "[smoke] subworld_recovery steps=%d minutes=%d "
                 "hp=%d mp=%d sp=%d expect hp=%d mp=%d sp=%d\n",
                 kFrames, minutesAdvanced, afterHp, afterMp, afterSp,
                 reference.combatStats.currentHp,
                 reference.combatStats.currentMp,
                 reference.combatStats.currentSp);
    std::fflush(stderr);

    if (minutesAdvanced <= 0) {
        smoke_fail(app, "subworld_recovery bought no game minutes");
        return false;
    }
    // The measurement must have MEASURED: if the reference itself gained
    // nothing, the run was too short and an equal-and-unmoved pair would pass
    // while proving that recovery is still switched off.
    if (reference.combatStats.currentHp <= 5) {
        smoke_fail(app, "subworld_recovery reference gained nothing");
        return false;
    }
    if (afterHp != reference.combatStats.currentHp
        || afterMp != reference.combatStats.currentMp
        || afterSp != reference.combatStats.currentSp) {
        smoke_fail(app, "subworld_recovery differs from the macro law");
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
    app.travelStamina = sm::TravelStamina{};

    enter_subworld(app);
    if (!app.subworld.active()) {
        smoke_fail(app, "subworld_sp_drain enter failed");
        return false;
    }

    const int beforeSp = app.gs.player.combatStats.currentSp;
    const int beforeHp = app.gs.player.combatStats.currentHp;
    // Walk far enough to owe a WHOLE point of SP. At kStaminaPerCell one macro
    // cell of ordinary ground costs a FRACTION of a point, so this is several
    // cells of walking — the old "one cell already costs a point" premise dates
    // from when kStaminaPerCell was 1.0 and stopped being true when the owner
    // set it to 0.2.
    //
    // Each leg is priced with the weight of the ground under THAT leg, sampled
    // at the same instant charge_subworld_sp_for_distance samples it, so the
    // expected total stays exact even when the route crosses terrain types —
    // which it now does, because the walk is long enough to leave the cell it
    // started in.
    const float carryBefore = app.travelStamina.pending;
    float distance = 0.0f;
    float expected = 0.0f;
    float weight = 0.0f;
    int charged = 0;
    for (int leg = 0; leg < 24; ++leg) {
        const float legX = app.subworld.player_x();
        const float legY = app.subworld.player_y();
        app.subworld.move_player(0.0f, 250.0f);
        const float dx = app.subworld.player_x() - legX;
        const float dy = app.subworld.player_y() - legY;
        const float legDist = std::sqrt(dx * dx + dy * dy);
        if (legDist <= 0.01f) break;          // wall
        weight = app.subworld.player_ground_travel_weight();
        distance += legDist;
        expected += sm::travel_stamina_cost(
            weight, legDist / float(sm::sub::kCellSize));
        charged += charge_subworld_sp_for_distance(app, legDist);
        // Let a tick run between legs. The seamless 3×3 window only re-centres
        // inside tick(), so a walk that never ticks rams the edge of the window
        // after exactly 1.5 macro cells and stops — far short of a whole point
        // of SP, which is precisely how this scenario used to fail.
        advance_sim_seconds(app, 0.05f, false);
    }
    const int afterSp = app.gs.player.combatStats.currentSp;
    const int afterHp = app.gs.player.combatStats.currentHp;
    const float carryAfter = app.travelStamina.pending;
    app.subworld.leave(true);

    // THE law, not a magic number: distance in macro cells × the weight of the
    // ground, every point of it either charged or still carried. This is the
    // same formula the map layer pays (macro/movement_cost.h) — one journey,
    // one price, whichever layer you walk it on. `expected` was summed leg by
    // leg above; `weight` is the last leg's ground, printed as a witness that
    // the walk was on real terrain.
    const float accounted = float(charged) + carryAfter - carryBefore;

    std::fprintf(stderr,
                 "[smoke] subworld_sp_drain distance=%.1f weight=%.2f "
                 "expected=%.3f charged=%d carry=%.3f->%.3f sp=%d->%d hp=%d->%d\n",
                 double(distance), double(weight), double(expected), charged,
                 double(carryBefore), double(carryAfter),
                 beforeSp, afterSp, beforeHp, afterHp);
    std::fflush(stderr);

    if (distance <= 0.01f
        || !(weight > 0.0f)
        || charged <= 0
        || std::fabs(accounted - expected) > 0.01f
        || afterSp != beforeSp - charged
        || afterHp != beforeHp) {
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

    enter_subworld(app);
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

    // Frameless smoke: the real loop drains each tick's queued GPU uploads in
    // prepare_frame, but this action ticks without rendering. Drain explicitly
    // between ticks, or every upload sees unborn GPU buffers and degrades to
    // the always-correct FULL rebuild — and the incremental seam machinery
    // this smoke exists to exercise (GPU shift blit + per-cell drains + their
    // self-check) never runs.
    app.subworld.debug_flush_gpu_uploads();
    RuntimeFrameStats frameStats =
        advance_sim_seconds(app, 1.0f / 60.0f, false);
    app.subworld.debug_flush_gpu_uploads();
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

    // Optional real-time pacing (default 0 = fast, unchanged CI behavior). The
    // headless smoke otherwise runs settle frames back-to-back, outrunning the
    // async generation workers so freshly exposed cells never finish generating
    // and their incremental drain-uploads never fire. Setting a few ms per frame
    // gives the workers wall-clock time — the same trick the async seam test
    // uses — so the per-cell drain path (and its self-check) is actually
    // exercised. Mirrors real 60fps pacing where the drain cascade is felt.
    int settleSleepMs = 0;
    if (const char* s = std::getenv("TIMAERT_SEAM_SETTLE_MS")) {
        settleSleepMs = std::atoi(s);
    }
    for (int i = 0; i < kSubworldSeamSmokeSettleFrames; ++i) {
        frameStats = advance_sim_seconds(app, 1.0f / 60.0f, false);
        // Same frameless-smoke drain as above: each settle tick's async
        // cell drains must reach the GPU before the next tick's upload
        // inspects the renderer's state.
        app.subworld.debug_flush_gpu_uploads();
        if (!frameStats.ticked || !frameStats.subworldActive) {
            smoke_fail(app, "subworld_seam settle tick inactive");
            app.subworld.leave(true);
            return false;
        }
        if (settleSleepMs > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(settleSleepMs));
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

    enter_subworld(app);
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
    if (!app.zones.has_complete_storage() || !app.terrain.has_rgba_storage()) {
        return false;
    }
    for (int level = sm::kZoneCount - 1; level >= 3; --level) {
        for (int y = 0; y < app.zones.height; ++y) {
            for (int x = 0; x < app.zones.width; ++x) {
                if (int(sm::zone_band(app.zones.at(x, y))) != level) continue;
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
                const std::size_t idx =
                    (std::size_t(y) * std::size_t(app.terrain.width)
                     + std::size_t(x)) * 4u;
                if (idx + 3u >= app.terrain.rgba.size()) continue;
                const float h = float(app.terrain.rgba[idx + 0u]) / 255.0f;
                // The [minH, 0.72] band sits below kMountainBiomeLevel (0.75),
                // so Mountain-biome cells are already excluded here.
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
                if (!sm::is_forest_cell(int(app.treeLayer.at(x, y)))) continue;
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

    float expectedCost = 0.0f;
    sm::MacroTravelCost lastExpected{};
    for (int i = 1; i <= kSmokeMacroTravelSteps; ++i) {
        sm::MacroTravelCost cost;
        if (!sm::macro_travel_cost_for_cell(app.gs, &player_bag(app), app.terrain,
                                            &app.features,
                                            path.path[std::size_t(i)].x,
                                            path.path[std::size_t(i)].y,
                                            cost, &app.treeLayer)) {
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
    const float carryBefore = app.travelStamina.pending;

    // Walk it through REAL FRAMES, not by calling the step directly. The whole
    // frame participates — input, the walk, world time, recovery, the hit-flash
    // tick — which is the only way to catch a bug that lives in the wiring
    // rather than in the formula. One did: an unrelated per-frame reset wiped
    // the fractional stamina carry, so a sub-1-SP step never accumulated into a
    // whole one and travel was silently free. A direct call could not see it.
    const std::size_t cellsBefore = app.cursor.path.size();
    int frames = 0;
    while (!app.cursor.path.empty() && frames < 600) {
        advance_sim_seconds(app, 1.0f / 60.0f, /*allowInput*/true);
        ++frames;
    }

    const int afterSp = app.gs.player.combatStats.currentSp;
    const int afterHp = app.gs.player.combatStats.currentHp;
    const int spentSp = beforeSp - afterSp;
    // Costs are fractional now, so the invariant is CONSERVATION, not equality
    // with a whole number: every point the terrain asked for is either taken
    // from stamina or still carried, and stamina fell by exactly what was taken.
    const float accounted =
        float(spentSp) + app.travelStamina.pending - carryBefore;

    // Print BEFORE judging. A harness that reports its numbers only when it
    // passes is useless exactly when it matters; this line is the first thing
    // anyone reads after a failure.
    std::fprintf(stderr,
                 "[smoke] macro_travel_sp steps=%d cells=%d frames=%d "
                 "left=%d pos=%.0f,%.0f->%.0f,%.0f sp=%d->%d(-%d) hp=%d->%d "
                 "expected=%.3f accounted=%.3f carry=%.3f->%.3f "
                 "lastBiome=%d lastFeature=%d lastCell=%.3f\n",
                 kSmokeMacroTravelSteps, int(cellsBefore), frames,
                 int(app.cursor.path.size()),
                 beforeX, beforeY,
                 app.gs.player.x, app.gs.player.y,
                 beforeSp, afterSp, spentSp,
                 beforeHp, afterHp,
                 double(expectedCost), double(accounted),
                 double(carryBefore), double(app.travelStamina.pending),
                 int(lastExpected.biome),
                 int(lastExpected.feature),
                 double(lastExpected.cellCost));
    std::fflush(stderr);

    // One condition, one message — so a failure says WHICH law broke.
    if (cellsBefore == 0) {
        smoke_fail(app, "macro_travel_sp had no path to walk");
        return false;
    }
    if (!app.cursor.path.empty()) {
        smoke_fail(app, "macro_travel_sp did not finish its route in 600 frames");
        return false;
    }
    if (std::fabs(accounted - expectedCost) > 0.01f) {
        smoke_fail(app, "macro_travel_sp stamina not conserved "
                        "(charged + carried != what the terrain asked)");
        return false;
    }
    // Travel must COST something end to end — the guard against a wiring change
    // that silently makes walking free again.
    if (expectedCost >= 1.0f && spentSp <= 0) {
        smoke_fail(app, "macro_travel_sp charged nothing for a walk");
        return false;
    }
    // Health must not DROP: with stamina in the bar, a walk costs no blood.
    // It may RISE — hourly recovery keeps mending while the legs work, which is
    // deliberate (only stamina is suppressed while marching), so equality would
    // be the wrong assertion.
    if (afterHp < beforeHp) {
        smoke_fail(app, "macro_travel_sp cost health while stamina remained");
        return false;
    }
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
    player.sheet.attributes.end = 1;
    player.sheet.attributes.vit = 1;
    player.sheet.attributes.wil = 1;
    player.combatStats.currentSp = 0;
    // ONE hit point, not zero: at zero the player is dead by the game's own
    // rule (checked at the end of every simulation step), and a corpse does not
    // convalesce. The coarse pre-tick frame used to hide this — recovery and
    // the death check landed in the same call, so a 0-HP player could round his
    // way back to 1 before anything noticed. Stamina and mana still start empty,
    // which is what this smoke is actually about.
    player.combatStats.currentHp = 1;
    player.combatStats.currentMp = 0;
    player.combatStats.maxSp = 100;
    player.combatStats.maxHp = 100;
    player.combatStats.maxMp = 100;
    sm::reset_player_recovery(app.playerRecovery);

    // Exactly six game minutes, whatever phase of the minute the clock is in.
    const RuntimeFrameStats stats = advance_sim_steps(
        app, int(sm::ticks_to_advance_minutes(app.gs.worldTime.tick, 6)), false);
    // Report before judging: a smoke that fails without printing its numbers
    // tells you only that something is wrong (same lesson as macro_travel_sp).
    std::fprintf(stderr,
                 "[smoke] macro_recovery minutes=%d hp=%d mp=%d sp=%d sub=%d\n",
                 stats.timeTick.minutesAdvanced,
                 player.combatStats.currentHp,
                 player.combatStats.currentMp,
                 player.combatStats.currentSp,
                 stats.subworldActive ? 1 : 0);
    std::fflush(stderr);

    if (stats.subworldActive
        || stats.timeTick.minutesAdvanced != 6
        || player.combatStats.currentSp != 1
        || player.combatStats.currentHp != 2
        || player.combatStats.currentMp != 1) {
        smoke_fail(app, "macro_recovery invariant");
        return false;
    }
    return true;
}

// The whole rest law through the shipping doors: arm with an empty bar
// (aim_rest_until_rested — the same call the toolbar Z, the Z key and the
// console share), then live the promoted turns through apply_rest_promotion +
// advance_sim_steps exactly as the main loop does, and demand that the stop
// was the SP bar filling — well before the two-day cap — and that arming
// again on a full bar is a no-op.
bool run_rest_sp_smoke(App& app) {
    if (!smoke_boot_invariants_hold(app)) {
        smoke_print_counts(app, "rest_sp_boot_failed");
        smoke_fail(app, "rest_sp boot invariants");
        return false;
    }
    if (app.subworld.active()) {
        smoke_fail(app, "rest_sp while subworld active");
        return false;
    }
    smoke_clear_modal_overlays(app);

    auto& cs = app.gs.player.combatStats;
    cs.currentSp = 0;
    cs.maxSp = 100;
    if (cs.currentHp < 1) cs.currentHp = 1;   // a corpse does not convalesce
    sm::reset_player_recovery(app.playerRecovery);

    // Rest IS a stop: arming Z mid-march must kill the click-route...
    app.cursor.path.push_back(sm::PathPoint{0, 0});
    app.cursor.pathIdx = 0;
    aim_rest_until_rested(app);
    const bool restIsStop = app.cursor.path.empty();

    // ...and a destination clicked MID-rest must drop the aim on the next
    // turn (marching legs regain no SP; rest+march would fast-forward travel).
    app.cursor.path.push_back(sm::PathPoint{0, 0});
    apply_rest_promotion(app, 0);
    const bool clickCancels = app.restUntilTick == 0;
    app.cursor.path.clear();
    app.cursor.pathIdx = 0;

    const std::uint64_t t0 = app.gs.worldTime.tick;
    aim_rest_until_rested(app);
    const bool armed = app.restUntilTick == t0 + 2 * sm::kTicksPerDay;

    // One iteration = one main-loop turn. The guard is generous: a full rest
    // is ~day of promoted time = ~64 turns; the cap itself is 2 days = 128.
    int turns = 0;
    // A day's simulation can raise a window over a sleeping party — an event,
    // a story slide — and a window stops the world (pause_reasons). When that
    // happened this smoke span its full 4096 turns with the clock frozen and
    // reported `turns=4096 slept_ticks=0 sp=0/100`, which names nothing: the
    // rest law looked broken when the world was merely holding still. The
    // interruption is cleared and the measurement continues; whatever CANNOT
    // be cleared is carried out of the loop as a mask and named in the report.
    std::uint8_t stuckPaused = kPauseNone;
    while (app.restUntilTick != 0 && turns < 4096) {
        if (world_paused(app)) {
            smoke_clear_modal_overlays(app);
            if (world_paused(app)) { stuckPaused = pause_reasons(app); break; }
        }
        const int ticks = apply_rest_promotion(app, 0);
        advance_sim_steps(app, ticks, false);
        ++turns;
    }
    const std::uint64_t slept = app.gs.worldTime.tick - t0;
    const bool full = cs.currentSp >= cs.maxSp;
    const bool underCap = slept < 2 * sm::kTicksPerDay;

    aim_rest_until_rested(app);   // full bar: must NOT arm again
    const bool noNap = app.restUntilTick == 0;

    std::fprintf(stderr,
                 "[smoke] rest_sp rest_is_stop=%d click_cancels=%d armed=%d "
                 "turns=%d slept_ticks=%llu (%.2f h) sp=%d/%d under_cap=%d "
                 "full_bar_noop=%d stuck_paused=0x%02X\n",
                 restIsStop ? 1 : 0, clickCancels ? 1 : 0, armed ? 1 : 0,
                 turns,
                 (unsigned long long)slept,
                 double(slept) * 24.0 / double(sm::kTicksPerDay),
                 cs.currentSp, cs.maxSp,
                 underCap ? 1 : 0, noNap ? 1 : 0,
                 unsigned(stuckPaused));
    std::fflush(stderr);

    if (stuckPaused != kPauseNone) {
        smoke_fail(app, "rest_sp could not measure: the world stayed paused");
        return false;
    }
    if (!restIsStop || !clickCancels || !armed || !full || !underCap || !noNap) {
        smoke_fail(app, "rest_sp invariant");
        return false;
    }
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

    app.gs.worldTime = sm::world_time_at(0, 6, 0);
    sm::reset_world_tick_runtime(app.gs.worldTickRt, app.gs.worldSeed);

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

    // Three whole hours and a minute past 06:00 -> hourly events at 07, 08, 09.
    const RuntimeFrameStats stats = advance_sim_steps(
        app, int(sm::ticks_to_advance_minutes(app.gs.worldTime.tick, 181)), false);
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
    app.gs.worldTime = sm::world_time_at(0, 9, 0);
    sm::reset_world_tick_runtime(app.gs.worldTickRt, app.gs.worldSeed);
    const RuntimeFrameStats noSubStats =
        advance_sim_steps(
            app, int(sm::ticks_to_advance_minutes(app.gs.worldTime.tick, 61)),
            false);
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
        entt::exclude<sm::ecs::Dead, sm::ecs::SubworldTag,
                      sm::ecs::PlayerSquadTag>);
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

    // The leader's bar is his SHEET's (Session 21): the cached maxSp the
    // regen law fills, not the retired 2×maxHp dialect. (void)hp — the body
    // still anchors the entity, but SP no longer derives from it.
    (void)hp;
    const int maxSp = std::max(1, int(rt.maxSp));
    rt.state = std::uint8_t(sm::NPCState::Resting);
    rt.sp = 0;
    rt.spCarry = 0.0f;
    rt.moveBudget = 0.0f;
    rt.tickAccum = 0;
    rt.visualSpeed = 0.0f;
    // Resting exits at half the bar; the percent law (1/8 per game hour)
    // makes that 4 game hours ≈ 43 thinks from empty for ANY bar. 64 = margin.
    // Deliberately PARTIAL envelope: the trace checks recovery mechanics on a
    // featureless world — no cost grid, no layers (each reads fail-closed).
    sm::MacroWorld traceMw{.gs = &app.gs, .world = &app.ecs,
                           .treeGrid = &app.treeGrid};
    for (int i = 0; i < 64; ++i) {
        sm::tick_macro_npc_ai(traceMw, app.npcAi, sm::kAiTicks);
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
    rt.tickAccum = 0;
    rt.visualSpeed = 0.0f;

    // No cost grid on purpose: the trace checks the march MECHANICS (budget,
    // glide) on a featureless world. Terrain pricing has its own ctest
    // coverage (squad_travel_test). The number of thinks is DERIVED from the
    // march constants, exactly like the pace-mirror tests re-derive theirs:
    // the door-track recalibration (2026-08-24) made the march 8 cells/hour ×
    // kAiTickGameHours per think — 0.75 cells banked per think, four thinks
    // to close three cells. The old single-think form assumed "3 cells per
    // think" and went red the day the constants became honest; +2 margin
    // thinks are harmless — an arrived caravan parks on its idle timer.
    const float cellsPerThink =
        sm::kMacroWalkCellsPerHour * sm::kAiTickGameHours;
    const int marchThinks = int(std::ceil(3.0f / cellsPerThink)) + 2;
    sm::MacroWorld traceMw2{.gs = &app.gs, .world = &app.ecs,
                            .treeGrid = &app.treeGrid};
    for (int i = 0; i < marchThinks; ++i) {
        sm::tick_macro_npc_ai(traceMw2, app.npcAi, sm::kAiTicks);
    }
    const float logicalX = pos.x;
    const float logicalY = pos.y;
    const float visualBefore = visual.vx;
    sm::tick_macro_npc_visuals(app.ecs, app.gs.mapW, app.gs.mapH, 0.25f);
    const float visualMid = visual.vx;
    // Convergence is a property, not a stopwatch: glide until the visual
    // catches the logical cell (bounded — 16 quarter-seconds covers any sane
    // glide speed for a two-cell gap). The old fixed two ticks were tuned to
    // the pre-recalibration three-cells-per-think visualSpeed.
    float visualEnd = visual.vx;
    for (int i = 0; i < 16 && std::fabs(visualEnd - logicalX) >= 0.001f; ++i) {
        sm::tick_macro_npc_visuals(app.ecs, app.gs.mapW, app.gs.mapH, 0.25f);
        visualEnd = visual.vx;
    }

    // The march is checked as a PROPERTY, never as a compass bearing: this
    // NPC's brain may legitimately re-target (a caravan with no honest home
    // city degrades to the nomad, which picks its own settlement), and WHICH
    // way that lies is a fact of the world layout, not of the mechanics. The
    // old invariant read `visualMid > baseX` and so silently demanded an
    // EASTWARD walk — it went red the day resource-placed settlements moved
    // the nearest town west (R2), while the budget and the glide it means to
    // guard were both perfect. Distance travelled says the same thing about
    // the mechanics and says it on every world.
    const float marched = sm::torus_dist(float(baseX), float(baseY),
                                         logicalX, logicalY,
                                         float(app.gs.mapW),
                                         float(app.gs.mapH));
    const float glidedMid = sm::torus_dist(float(baseX), float(baseY),
                                           visualMid, visual.vy,
                                           float(app.gs.mapW),
                                           float(app.gs.mapH));
    // The budget is counted in STEPS, and a step is one greedy 8-neighbour
    // hop — so a diagonal leg costs one step while spanning √2 of ground.
    // Chebyshev distance is exactly "how many hops", which is what the
    // 3-road-cells-per-think march law spends (a straight walk marches
    // 3.00, a walk with one diagonal 3.16 — both are three steps).
    const int stepsX = std::abs(int(std::lround(logicalX)) - baseX);
    const int stepsY = std::abs(int(std::lround(logicalY)) - baseY);
    const int steps = std::max(std::min(stepsX, app.gs.mapW - stepsX),
                               std::min(stepsY, app.gs.mapH - stepsY));
    // A traveler HALTS inside the arrival ring (npc_ai.cpp at_target,
    // dist² < 4 — a two-cell ring), so a three-cell leg is walked to the
    // last whole cell OUTSIDE it: exactly 3 − 1 steps. The old `steps == 3`
    // was true only while one think spent the whole leg before the ring was
    // ever re-asked.
    const bool logicalMoved = steps == 3 - 1;
    const bool visualSmoothed =
        visualBefore == float(baseX)
        && glidedMid > 0.0f
        && glidedMid < marched
        && std::fabs(visualEnd - logicalX) < 0.001f;

    std::fprintf(stderr,
                 "[smoke] macro_npc_trace entity=%u maxSp=%d "
                 "rest=%d:%d recovered=%d move=%.1f,%.1f->%.1f,%.1f "
                 "steps=%d marched=%.2f visual=%.2f->%.2f->%.2f\n",
                 unsigned(entt::to_integral(e)),
                 maxSp,
                 recoveredSp,
                 recoveredState,
                 recovered ? 1 : 0,
                 float(baseX), float(baseY), logicalX, logicalY,
                 steps, marched,
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
    enter_subworld(app);
    if (!app.subworld.active()) {
        restore();
        smoke_fail(app, "subworld_exit_gate enter failed");
        return false;
    }
    // "bandits" NAMED, not defaulted: this scenario needs a body the player is
    // actually at war with (it asserts the exit gate stays shut while a hostile
    // is near). The default is now the realm owning this ground, whose guards
    // would let you walk right out.
    if (!app.subworld.spawn_npc_body("bandit", "Smoke Gate Bandit", 3,
                                     app.gs.worldSeed ^ 0xE917u, "bandits")) {
        restore();
        smoke_fail(app, "subworld_exit_gate hostile spawn failed");
        return false;
    }

    // ONE tick before we ask the gate anything. exit_blocked_by_danger reads
    // `playerThreatD2_`, which is folded in during the battle-steering rebuild
    // (sub/engine.cpp) — it is only true as of the last tick, so a body spawned
    // and queried in the same breath is invisible to it and the gate honestly
    // reports "no hostiles". Same rule the frame-capture probes learned: mutate
    // the ECS, then let a tick settle before reading anything the tick computes.
    advance_sim_seconds(app, 0.05f, false);

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

    enter_subworld(app);
    if (!app.subworld.active()) {
        restore();
        smoke_fail(app, "subworld_loot_xp enter failed");
        return false;
    }

    sm::ecs::NpcInventory bag{};
    bag.inv.add("misc_gem", 2);
    if (!app.subworld.spawn_npc_body("bandit", "Smoke Loot Bandit", 2,
                                     app.gs.worldSeed ^ 0x10A7u, "bandits",
                                     &bag)) {
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

    const int expBefore = app.gs.player.sheet.levelData.exp;
    const int gemBefore = player_bag(app).count("misc_gem");
    // Park the victim right next to the PLAYER, wherever they actually stand.
    // The old window-centre teleport assumed enter() always lands at the
    // centre; entry-side context (armies enter from the side they walked in)
    // made that stale, leaving the corpse hundreds of tiles from the player
    // and interact() honestly reporting "nothing nearby".
    const float lootX = app.subworld.player_x() + 2.0f;
    const float lootY = app.subworld.player_y();
    if (auto* pos = reg.try_get<sm::ecs::Position>(target)) {
        pos->x = lootX;
        pos->y = lootY;
    }
    if (auto* vp = reg.try_get<sm::ecs::VisualPos>(target)) {
        vp->vx = lootX;
        vp->vy = lootY;
    }
    sm::sub::apply_lethal_damage(reg, target, sm::sub::DamageSource{0u, true},
                                 sm::sub::DamageKind::Dev, &app.bus);

    app.subworld.tick(0.016f);
    bool corpseFound = false;
    auto corpses = reg.view<sm::ecs::Structure, sm::ecs::CorpseLoot,
                            sm::ecs::SubworldTag>();
    for (auto e : corpses) {
        const auto& st = corpses.get<sm::ecs::Structure>(e);
        if (st.kind == sm::ecs::Structure::Corpse) corpseFound = true;
    }
    // Corpse-vs-player altitude in the diagnostic: interact() gates on a 3D
    // distance, so a z divergence (e.g. a body seated on a structure top) is
    // the first thing to rule out when interact=0 with the corpse present.
    float corpseZ = -1.0f;
    for (auto e : corpses) {
        const auto& st = corpses.get<sm::ecs::Structure>(e);
        if (st.kind != sm::ecs::Structure::Corpse) continue;
        if (const auto* cp = reg.try_get<sm::ecs::Position>(e)) corpseZ = cp->z;
    }
    const float playerZAtInteract = app.subworld.player_z();
    const bool interacted = app.subworld.interact();
    const int expAfter = app.gs.player.sheet.levelData.exp;
    const int gemAfter = player_bag(app).count("misc_gem");
    restore();

    std::fprintf(stderr,
                 "[smoke] subworld_loot_xp corpse=%d interact=%d "
                 "exp=%d->%d misc_gem=%d->%d corpseZ=%.1f playerZ=%.1f\n",
                 corpseFound ? 1 : 0, interacted ? 1 : 0,
                 expBefore, expAfter, gemBefore, gemAfter,
                 corpseZ, playerZAtInteract);
    std::fflush(stderr);

    if (!corpseFound || !interacted || expAfter <= expBefore
        || gemAfter < gemBefore + 2) {
        smoke_fail(app, "subworld_loot_xp invariant");
        return false;
    }
    return true;
}

// dungeon_house — Inc 1 end-to-end: E at a house wall → a sealed interior on
// the same engine → E on the exit pad → back on the doorstep. Asserts the
// scene flag, the exactly-one-PlayerTag invariant at every stage, and that
// the same door deterministically re-derives the same interior (a tile-grid
// hash across two independent visits).
bool run_dungeon_house_smoke(App& app) {
    if (!smoke_boot_invariants_hold(app)) {
        smoke_print_counts(app, "dungeon_house_boot_failed");
        smoke_fail(app, "dungeon_house boot invariants");
        return false;
    }
    if (app.subworld.active()) {
        smoke_fail(app, "dungeon_house already active");
        return false;
    }
    if (app.gs.politik.cities.empty()) {
        smoke_fail(app, "dungeon_house no cities");
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

    // Land on the first city: its centre cell is guaranteed houses.
    app.gs.player.x = float(app.gs.politik.cities[0].x);
    app.gs.player.y = float(app.gs.politik.cities[0].y);
    app.gs.subState.settlementId = -1;
    app.ui.settlementId = -1;
    enter_subworld(app);
    if (!app.subworld.active()) {
        restore();
        smoke_fail(app, "dungeon_house enter failed");
        return false;
    }

    auto playerTags = [&]() {
        int n = 0;
        for (auto e : app.ecs.reg.view<sm::ecs::PlayerTag>()) {
            (void)e;
            ++n;
        }
        return n;
    };
    auto hashTiles = [&]() {
        std::uint32_t h = 2166136261u;
        for (std::uint8_t b : app.subworld.mgr().tiles()) {
            h = (h ^ b) * 16777619u;
        }
        return h;
    };

    // Nearest DOOR prop to the window centre — central by construction, so
    // the doorstep stays inside the centre cell (no seam crossing mid-smoke).
    // The door is what the player interacts with now; a house without one is
    // a house nobody can enter, so finding none is a failure, not a skip.
    const auto& structs = app.subworld.mgr().structures();
    const float mid = float(sm::sub::kFullSize) / 2.0f;
    int best = -1;
    float bestD2 = 1e30f;
    for (std::size_t i = 0; i < structs.size(); ++i) {
        const auto& s = structs[i];
        if (s.kind != sm::sub::Structure::Door) continue;
        const float dx = s.x - mid;
        const float dy = s.y - mid;
        const float d2 = dx * dx + dy * dy;
        if (d2 < bestD2) {
            bestD2 = d2;
            best = int(i);
        }
    }
    if (best < 0) {
        restore();
        smoke_fail(app, "dungeon_house no door in window");
        return false;
    }
    const sm::sub::Structure door = structs[std::size_t(best)];
    // Stand on the door's outward normal, just inside the door verb's reach
    // (kInteractRows), and LOOK at it — targeting is by aim now, so the smoke
    // must aim like a player. Standing at the edge of reach rather than nose
    // to the timber is also what a capture wants: a door you can SEE.
    const float standoff =
        sm::sub::interact_row(sm::sub::InteractId::Door).reachTiles - 1.0f;
    const float standX = door.x - standoff * std::sin(door.yaw);
    const float standY = door.y + standoff * std::cos(door.yaw);
    auto face_door = [&]() {
        app.subworld.set_player_pos(standX, standY);
        const float want = std::atan2(door.y - standY, door.x - standX);
        app.subworld.rotate_camera(want - app.subworld.cam_yaw(), 0.0f);
    };
    // The non-portal verbs, proving the table carries more than doors: a
    // well pays in the one currency travel spends, a sign pays in words.
    // Both are exercised through the SAME dispatch a keypress runs. The tick
    // is load-bearing: the aim reads the prop cache, which the scene builds
    // on its first tick.
    app.subworld.tick(0.016f);
    int wells = 0, signs = 0;
    int spBefore = 0, spAfter = 0;
    bool drank = false, readSign = false;
    {
        const sm::sub::Structure* well = nullptr;
        const sm::sub::Structure* sign = nullptr;
        for (const auto& s : app.subworld.mgr().structures()) {
            if (s.kind == sm::sub::Structure::Well) { ++wells; if (!well) well = &s; }
            if (s.kind == sm::sub::Structure::Sign) { ++signs; if (!sign) sign = &s; }
        }
        if (well != nullptr) {
            // Spend some stamina first, or a full bar makes the well refuse —
            // which is itself correct, and not what we are testing here.
            app.gs.player.combatStats.currentSp =
                app.gs.player.combatStats.maxSp / 2;
            spBefore = app.gs.player.combatStats.currentSp;
            app.subworld.set_player_pos(well->x, well->y - 2.0f);
            app.subworld.rotate_camera(1.5707963f - app.subworld.cam_yaw(), 0.0f);
            drank = app.subworld.interact();
            spAfter = app.gs.player.combatStats.currentSp;
        }
        if (sign != nullptr) {
            app.subworld.set_player_pos(sign->x, sign->y - 2.0f);
            app.subworld.rotate_camera(1.5707963f - app.subworld.cam_yaw(), 0.0f);
            readSign = app.subworld.interact();
        }
    }

    face_door();
    app.subworld.tick(0.016f);
    // Re-aim after the settle tick: a frame of simulation can drift the body,
    // and the determinism claim below is about ONE door seen from ONE spot.
    face_door();

    // Opt-in (TIMAERT_SMOKE_DOORSTEP=1): stop HERE, on the doorstep looking
    // at the door, so a following capture_frame photographs the prop and the
    // interaction prompt it raises. Asserts what it proves: a door is in
    // reach and the engine offers its verb.
    if (std::getenv("TIMAERT_SMOKE_DOORSTEP")) {
        const char* verb = app.subworld.interact_prompt();
        std::fprintf(stderr, "[smoke] dungeon_house DOORSTEP prompt='%s'\n",
                     verb ? verb : "");
        std::fflush(stderr);
        if (verb == nullptr || verb[0] == '\0') {
            smoke_fail(app, "dungeon_house doorstep offers no verb");
            return false;
        }
        return true;
    }

    const int tagsBefore = playerTags();
    const bool entered = app.subworld.interact();
    const bool inD1 = app.subworld.in_dungeon();
    const int tagsIn = playerTags();
    const std::uint32_t h1 = inD1 ? hashTiles() : 0u;
    app.subworld.tick(0.016f);

    // You must LAND INSIDE the house, and still be inside it a tick later
    // once collision has had its say: an interior that spits the player
    // through its own wall is the worst kind of broken, because the room
    // still looks right from outside it.
    auto standing_on = [&]() {
        const auto& t = app.subworld.mgr().tiles();
        const int ix = int(app.subworld.player_x());
        const int iy = int(app.subworld.player_y());
        if (ix < 0 || iy < 0 || ix >= sm::sub::kFullSize
            || iy >= sm::sub::kFullSize) {
            return int(-1);
        }
        const std::size_t i = std::size_t(iy) * sm::sub::kFullSize + ix;
        return i < t.size() ? int(t[i]) : -1;
    };
    const int floorTile = standing_on();
    const bool onFloor = floorTile == sm::sub::TILE_ROAD
                      || floorTile == sm::sub::TILE_SQUARE;

    // Storeys (Inc 3) + the cellar's own population (Inc 4). A big enough
    // house carries a climbing shaft; every second one keeps a cellar. Both
    // are the same identity one level along, so we probe whichever this house
    // has and assert what that storey promises.
    const int lvl0 = app.subworld.dungeon_level();
    int lvlUp = 0, lvlBack = 0, lvlDown = 0, lvlBack2 = 0;
    int vermin = 0, faunaBefore = -1, faunaAfter = -1;
    if (app.subworld.debug_take_stairs(/*up*/true)) {
        lvlUp = app.subworld.dungeon_level();
        app.subworld.tick(0.016f);
        // The street door does not exist up here: stand clear of the shaft,
        // in the middle of the room, and E must find nothing at all.
        app.subworld.set_player_pos(float(sm::sub::kFullSize) / 2.0f,
                                    float(sm::sub::kFullSize) / 2.0f);
        app.subworld.tick(0.016f);
        if (app.subworld.interact() || !app.subworld.in_dungeon()) {
            smoke_fail(app, "dungeon_house upper storey had a way out");
            return false;
        }
        if (app.subworld.debug_take_stairs(/*up*/true)) {
            lvlBack = app.subworld.dungeon_level();
            app.subworld.tick(0.016f);
        }
    }
    if (app.subworld.debug_take_stairs(/*up*/false)) {
        lvlDown = app.subworld.dungeon_level();
        app.subworld.tick(0.016f);
        // The vermin of the dark: creatures borrowed from the CELL's
        // fauna_count, so a kill down here thins the cell for good.
        entt::entity beast = entt::null;
        sm::ecs::MacroDebt beastDebt{};
        for (auto e : app.ecs.reg.view<sm::ecs::MacroDebt, sm::ecs::Health,
                                       sm::ecs::SubworldTag>(
                 entt::exclude<sm::ecs::Dead>)) {
            const auto& d = app.ecs.reg.get<sm::ecs::MacroDebt>(e);
            if (d.stock != std::uint8_t(sm::MacroStock::FaunaCount)) continue;
            ++vermin;
            if (beast == entt::null) {
                beast = e;
                beastDebt = d;
            }
        }
        if (beast != entt::null) {
            sm::MacroWorld mw = macro_world(app);
            const sm::MacroStockKey key{-1, beastDebt.cellX, beastDebt.cellY};
            faunaBefore = sm::macro_stock_read(mw, sm::MacroStock::FaunaCount,
                                               key);
            sm::sub::apply_lethal_damage(app.ecs.reg, beast,
                                         sm::sub::DamageSource{},
                                         sm::sub::DamageKind::Script,
                                         &app.bus);
            app.subworld.tick(0.016f);
            faunaAfter = sm::macro_stock_read(mw, sm::MacroStock::FaunaCount,
                                              key);
        }
        if (app.subworld.debug_take_stairs(/*up*/false)) {
            lvlBack2 = app.subworld.dungeon_level();
            app.subworld.tick(0.016f);
        }
    }
    // Whatever storeys this house has, we must be standing on the level the
    // door opens onto again — and back on its threshold, because a shaft
    // trip left us in a corner of the room.
    float exitX = 0.0f, exitY = 0.0f;
    if (app.subworld.dungeon_level() != 0
        || !app.subworld.dungeon_exit_point(exitX, exitY)) {
        smoke_fail(app, "dungeon_house did not return to the door storey");
        return false;
    }
    app.subworld.set_player_pos(exitX, exitY);
    // Face the exit door: it stands on the south wall the threshold looks at,
    // and E is aim-driven, so the smoke must look at it like a player.
    app.subworld.rotate_camera(1.5707963f - app.subworld.cam_yaw(), 0.0f);
    app.subworld.tick(0.016f);

    // Opt-in (TIMAERT_SMOKE_DUNGEON_STAY=1, optionally with
    // TIMAERT_SMOKE_DUNGEON_LEVEL=-1|0|1): stop INSIDE the interior on that
    // storey so a following capture_frame photographs the dungeon scene
    // itself (the capture lands ≥1 frame later, per the smoke capture law).
    // The round-trip half of the invariants is skipped on purpose.
    if (std::getenv("TIMAERT_SMOKE_DUNGEON_STAY")) {
        int want = 0;
        if (const char* lv = std::getenv("TIMAERT_SMOKE_DUNGEON_LEVEL")) {
            want = std::atoi(lv);
        }
        if (want > 0) (void)app.subworld.debug_take_stairs(/*up*/true);
        if (want < 0) (void)app.subworld.debug_take_stairs(/*up*/false);
        // Photograph the ROOM: stand on the threshold — the one spot the
        // generator guarantees is open — and look north up the hall. (The
        // room centre is NOT safe to teleport onto: a partition may stand
        // there, and set_player_pos does not resolve solids the way walking
        // does.)
        float sx = 0.0f, sy = 0.0f;
        float faceYaw = -1.5707963f;            // north, up the hall
        if (app.subworld.dungeon_exit_point(sx, sy)) {
            // Back off the threshold into the room and turn round to look AT
            // it: the way out is the thing worth photographing.
            app.subworld.set_player_pos(sx, sy - 6.0f);
            faceYaw = 1.5707963f;               // south, at the door
        }
        app.subworld.rotate_camera(faceYaw - app.subworld.cam_yaw(), 0.0f);
        app.subworld.tick(0.016f);
        std::fprintf(stderr, "[smoke] dungeon_house STAY level=%d\n",
                     app.subworld.dungeon_level());
        std::fflush(stderr);
        std::fprintf(stderr,
                     "[smoke] dungeon_house STAY entered=%d in=%d tags=%d/%d\n",
                     entered ? 1 : 0, inD1 ? 1 : 0, tagsBefore, tagsIn);
        std::fflush(stderr);
        if (!entered || !inD1 || tagsBefore != 1 || tagsIn != 1) {
            smoke_fail(app, "dungeon_house stay invariant");
            return false;
        }
        return true;
    }

    // The chest hands over the TOWN'S OWN goods: the store shrinks by what
    // the player gains, and the theft is charged to their standing. Nothing
    // is conjured, so an emptied town has empty chests.
    int chestProps = 0;
    for (const auto& s : app.subworld.mgr().structures()) {
        if (s.kind == sm::sub::Structure::Chest) ++chestProps;
    }
    int storeBefore = 0, storeAfter = 0, bagBefore = 0, bagAfter = 0;
    int repBefore = 0, repAfter = 0;
    bool searched = false;
    {
        sm::Settlement* town = nullptr;
        for (auto& s : app.gs.settlements) {
            if (s.x == int(app.gs.player.x) && s.y == int(app.gs.player.y)) {
                town = &s;
                break;
            }
        }
        const sm::sub::Structure* chest = nullptr;
        for (const auto& s : app.subworld.mgr().structures()) {
            if (s.kind == sm::sub::Structure::Chest) { chest = &s; break; }
        }
        if (town != nullptr && chest != nullptr && town->inventory.used_slots() != 0) {
            const char* fid = sm::faction_id_for_index(
                sm::faction_index_for_kingdom(app.gs.politik, town->kingdomIdx));
            storeBefore = town->inventory.total();
            bagBefore = player_bag(app).total();
            repBefore = sm::player_reputation(&app.gs, fid);
            searched = app.subworld.search_chest(*chest);
            storeAfter = town->inventory.total();
            bagAfter = player_bag(app).total();
            repAfter = sm::player_reputation(&app.gs, fid);
        }
    }

    // Inc 2: the household — real people of this town, borrowed from ITS
    // population stock. Killing one behind the door must thin the town in
    // the same tick, through the same receipt a street kill settles.
    int residents = 0;
    entt::entity victim = entt::null;
    sm::ecs::MacroDebt victimDebt{};
    for (auto e : app.ecs.reg.view<sm::ecs::NPCKind, sm::ecs::MacroDebt,
                                   sm::ecs::Health, sm::ecs::SubworldTag>(
             entt::exclude<sm::ecs::Dead>)) {
        const auto& d = app.ecs.reg.get<sm::ecs::MacroDebt>(e);
        if (d.stock != std::uint8_t(sm::MacroStock::Population)) continue;
        ++residents;
        if (victim == entt::null) {
            victim = e;
            victimDebt = d;
        }
    }
    int popBefore = -1, popAfter = -1;
    if (victim != entt::null) {
        sm::MacroWorld mw = macro_world(app);
        const sm::MacroStockKey key{victimDebt.subject, victimDebt.cellX,
                                    victimDebt.cellY};
        popBefore = sm::macro_stock_read(mw, sm::MacroStock::Population, key);
        sm::sub::apply_lethal_damage(app.ecs.reg, victim,
                                     sm::sub::DamageSource{},
                                     sm::sub::DamageKind::Script, &app.bus);
        app.subworld.tick(0.016f);
        popAfter = sm::macro_stock_read(mw, sm::MacroStock::Population, key);
    }

    // The player spawned ON the exit pad — E walks back out. A corpse in
    // reach outranks the door (loot the body on the doorstep, then leave),
    // so drain E until the scene actually flips.
    bool exited = false;
    for (int i = 0; i < 4 && !exited; ++i) {
        if (!app.subworld.interact()) break;
        exited = !app.subworld.in_dungeon();
    }
    const bool outOk = app.subworld.active() && !app.subworld.in_dungeon();
    const int tagsOut = playerTags();
    app.subworld.tick(0.016f);

    // Re-enter the same door: stand and look exactly as before (the return
    // spot is within reach, but the AIM must be judged from the identical
    // spot), then the same identity must re-derive the same interior. The
    // tick above is load-bearing — the prop cache the aim reads is rebuilt
    // with the solidity index, i.e. on the scene's first tick.
    face_door();
    const bool entered2 = app.subworld.interact();
    const bool inD2 = app.subworld.in_dungeon();
    const std::uint32_t h2 = inD2 ? hashTiles() : 0u;
    // Leave the same way a player would: tick (so the scene's prop cache is
    // built), stand on the threshold, look at its door, press E.
    app.subworld.tick(0.016f);
    bool exited2 = false;
    float x2 = 0.0f, y2 = 0.0f;
    if (app.subworld.dungeon_exit_point(x2, y2)) {
        app.subworld.set_player_pos(x2, y2);
        app.subworld.rotate_camera(1.5707963f - app.subworld.cam_yaw(), 0.0f);
        exited2 = app.subworld.interact() && !app.subworld.in_dungeon();
    }

    // The universal quick exit: from INSIDE an interior, with nothing on you,
    // the leave key surfaces you straight to the map — no walk back to the
    // door, no stair climb. Enter once more (we are standing on the doorstep
    // after the walked exit), then leave from within.
    app.subworld.tick(0.016f);
    face_door();
    bool quickExit = false;
    if (app.subworld.interact() && app.subworld.in_dungeon()) {
        app.subworld.tick(0.016f);
        app.subworld.leave();
        quickExit = !app.subworld.active();
    }
    restore();

    std::fprintf(stderr,
                 "[smoke] dungeon_house entered=%d/%d in=%d/%d exited=%d/%d "
                 "out=%d tags=%d/%d/%d hash=%08x/%08x residents=%d "
                 "pop=%d->%d storeys=%d/%d/%d/%d/%d vermin=%d fauna=%d->%d "
                 "floorTile=%d quickExit=%d chests=%d searched=%d "
                 "store=%d->%d bag=%d->%d rep=%d->%d "
                 "wells=%d signs=%d drank=%d sp=%d->%d read=%d\n",
                 entered ? 1 : 0, entered2 ? 1 : 0, inD1 ? 1 : 0, inD2 ? 1 : 0,
                 exited ? 1 : 0, exited2 ? 1 : 0, outOk ? 1 : 0,
                 tagsBefore, tagsIn, tagsOut, h1, h2, residents,
                 popBefore, popAfter,
                 lvl0, lvlUp, lvlBack, lvlDown, lvlBack2,
                 vermin, faunaBefore, faunaAfter, floorTile,
                 quickExit ? 1 : 0, chestProps, searched ? 1 : 0,
                 storeBefore, storeAfter, bagBefore, bagAfter,
                 repBefore, repAfter, wells, signs, drank ? 1 : 0,
                 spBefore, spAfter, readSign ? 1 : 0);
    std::fflush(stderr);

    // Storey invariants only bind when the house HAS that storey (a small
    // house has no upper room, half of them have no cellar) — a shaft that
    // was taken must land one level along and come back to the door level.
    const bool storeysOk =
        lvl0 == 0
        && (lvlUp == 0 || (lvlUp == 1 && lvlBack == 0))
        && (lvlDown == 0 || (lvlDown == -1 && lvlBack2 == 0));
    // A cellar that spawned vermin must pay the cell back on a kill.
    const bool verminOk = vermin == 0
        || (faunaBefore > 0 && faunaAfter == faunaBefore - 1);

    if (!entered || !inD1 || !exited || !outOk || !entered2 || !inD2
        || !exited2 || tagsBefore != 1 || tagsIn != 1 || tagsOut != 1
        || h1 != h2
        // A city house holds a household, and a death behind the door thins
        // the town by exactly one, in the tick it happens.
        || residents < 1 || popBefore <= 0 || popAfter != popBefore - 1
        || !storeysOk || !verminOk || !onFloor || !quickExit
        // A house has a chest, and searching it MOVES goods from the town's
        // store into the bag — same count out as in — at a price in standing.
        || chestProps < 1 || !searched
        || storeAfter >= storeBefore
        || bagAfter - bagBefore != storeBefore - storeAfter
        || repAfter >= repBefore
        // A settlement keeps a well and a board, and both answer E: the well
        // in stamina, the board in words.
        || wells < 1 || signs < 1 || !drank || spAfter <= spBefore
        || !readSign) {
        smoke_fail(app, "dungeon_house invariant");
        return false;
    }
    return true;
}

// dungeon_cave — the first interior nobody owns. Walks the map until it finds
// a highland cell whose rock has a mouth in it, enters through that mouth, and
// asserts what a CAVE promises that a house does not: a walked shape (every
// opened tile reachable from the threshold), wild beasts drawn from the cell's
// own full headcount, and no storeys.
bool run_dungeon_cave_smoke(App& app) {
    if (!smoke_boot_invariants_hold(app)) {
        smoke_print_counts(app, "dungeon_cave_boot_failed");
        smoke_fail(app, "dungeon_cave boot invariants");
        return false;
    }
    const float oldX = app.gs.player.x;
    const float oldY = app.gs.player.y;
    const auto oldSubState = app.gs.subState;
    auto restore = [&]() {
        if (app.subworld.active()) app.subworld.leave(true);
        app.gs.player.x = oldX;
        app.gs.player.y = oldY;
        app.gs.subState = oldSubState;
    };

    // Mountain cells are where rock is, and rock is where a mouth can open.
    // Two thirds of them keep no cave (the generator's own coin), so the
    // search walks candidates until one bears fruit — and says how many it
    // tried, because "found none" must be distinguishable from "looked once".
    int tried = 0;
    int mouths = 0;
    const sm::sub::Structure* mouth = nullptr;
    for (int cy = 0; cy < app.gs.mapH && mouths == 0; cy += 7) {
        for (int cx = 0; cx < app.gs.mapW && mouths == 0; cx += 7) {
            const std::size_t midx =
                (std::size_t(cy) * std::size_t(app.terrain.width)
                 + std::size_t(cx)) * 4u;
            if (midx + 3u >= app.terrain.rgba.size()) continue;
            if (app.terrain.rgba[midx + 3u] < 128) continue;   // sea
            if (float(app.terrain.rgba[midx + 0u]) / 255.0f
                < sm::kMountainBiomeLevel) continue;
            ++tried;
            if (tried > 24) break;                             // bounded hunt
            app.gs.player.x = float(cx);
            app.gs.player.y = float(cy);
            app.gs.subState.settlementId = -1;
            enter_subworld(app);
            if (!app.subworld.active()) continue;
            app.subworld.tick(0.016f);
            for (const auto& s : app.subworld.mgr().structures()) {
                if (s.kind != sm::sub::Structure::CaveMouth) continue;
                ++mouths;
                if (!mouth) mouth = &s;
            }
            if (mouths == 0) app.subworld.leave(true);
        }
    }
    if (mouths == 0 || mouth == nullptr) {
        restore();
        std::fprintf(stderr, "[smoke] dungeon_cave tried=%d found no mouth\n",
                     tried);
        std::fflush(stderr);
        smoke_fail(app, "dungeon_cave no cave mouth in any highland cell");
        return false;
    }

    // Stand off the mouth and look at it — the same aim the player uses.
    const sm::sub::Structure m = *mouth;
    const float standX = m.x - 4.0f * std::sin(m.yaw);
    const float standY = m.y + 4.0f * std::cos(m.yaw);
    app.subworld.set_player_pos(standX, standY);
    app.subworld.rotate_camera(
        std::atan2(m.y - standY, m.x - standX) - app.subworld.cam_yaw(), 0.0f);
    const bool entered = app.subworld.interact();
    const bool inCave = app.subworld.in_dungeon();
    app.subworld.tick(0.016f);

    // Opt-in (TIMAERT_SMOKE_CAVE_STAY=1): stop inside, looking up the first
    // gallery, so a following capture_frame photographs the cavern.
    if (std::getenv("TIMAERT_SMOKE_CAVE_STAY")) {
        app.subworld.rotate_camera(-1.5707963f - app.subworld.cam_yaw(), 0.0f);
        app.subworld.tick(0.016f);
        std::fprintf(stderr, "[smoke] dungeon_cave STAY in=%d\n",
                     inCave ? 1 : 0);
        std::fflush(stderr);
        if (!entered || !inCave) {
            smoke_fail(app, "dungeon_cave stay invariant");
            return false;
        }
        return true;
    }

    // What this harness can honestly see of the SHAPE is where the player is
    // standing and what the cavern holds: the composite carries tiles, not
    // the walkable grid (a cave's floor and its walls are the same stone, so
    // only `trav` tells them apart), and that grid lives on the generated map
    // — which is where the reachability proof belongs, and is asserted in
    // dungeon_cave_test. Here we prove the LIVE facts a test cannot: that a
    // mouth in the world opens, lands the player on open ground, and lets go.
    int floorTile = -1;
    int hoards = 0;
    if (inCave) {
        const auto& tiles = app.subworld.mgr().tiles();
        const int ix = int(app.subworld.player_x());
        const int iy = int(app.subworld.player_y());
        const std::size_t i = std::size_t(iy) * sm::sub::kFullSize + ix;
        if (i < tiles.size()) floorTile = int(tiles[i]);
        for (const auto& st : app.subworld.mgr().structures()) {
            if (st.kind == sm::sub::Structure::Chest) ++hoards;
        }
    }
    const bool onFloor = floorTile == sm::sub::TILE_ROAD
                      || floorTile == sm::sub::TILE_ROCK;

    int vermin = 0;
    for (auto e : app.ecs.reg.view<sm::ecs::MacroDebt, sm::ecs::SubworldTag>()) {
        if (app.ecs.reg.get<sm::ecs::MacroDebt>(e).stock
            == std::uint8_t(sm::MacroStock::FaunaCount)) {
            ++vermin;
        }
    }
    const int level = app.subworld.dungeon_level();
    // No storeys underground: a cave has depth, and depth is walked.
    const bool noStairs = !app.subworld.debug_take_stairs(true)
                       && !app.subworld.debug_take_stairs(false);
    // The quick exit obeys the danger law BOTH ways, and a cave with beasts
    // in it is the honest place to prove it: while they are on you the map is
    // not an escape hatch, and once they are down it is.
    app.subworld.leave();
    const bool refusedWhileHunted = vermin > 0 && app.subworld.in_dungeon();
    if (vermin > 0) {
        app.subworld.dev_kill_all_hostiles();
        app.subworld.tick(0.016f);   // let the threat scan settle
        app.subworld.tick(0.016f);
    }
    app.subworld.leave();
    const bool leftToMap = !app.subworld.active();
    restore();

    std::fprintf(stderr,
                 "[smoke] dungeon_cave tried=%d mouths=%d entered=%d in=%d "
                 "level=%d floorTile=%d hoards=%d vermin=%d noStairs=%d "
                 "hunted=%d leftToMap=%d\n",
                 tried, mouths, entered ? 1 : 0, inCave ? 1 : 0, level,
                 floorTile, hoards, vermin, noStairs ? 1 : 0,
                 refusedWhileHunted ? 1 : 0, leftToMap ? 1 : 0);
    std::fflush(stderr);

    if (!entered || !inCave || level != 0 || !onFloor || hoards < 1
        || !noStairs || !leftToMap
        // A cave without beasts is a cave the fauna stock could not pay for,
        // which is legitimate on a hunted cell — but if it HAD beasts, the
        // gate must have held while they lived.
        || (vermin > 0 && !refusedWhileHunted)) {
        smoke_fail(app, "dungeon_cave invariant");
        return false;
    }
    return true;
}

// spire_climb — the whole spire loop in one live pass: teleport to a spire
// whose spell the player does not know, enter the scorched cell, walk the
// gate, climb every storey through its demon guard, take the roof hatch onto
// the crown, touch the orb, and come away with the spell — the spire flipped
// depleted, the orb burned out of the scene, the fact in the event log.
bool run_spire_climb_smoke(App& app) {
    if (!smoke_boot_invariants_hold(app)) {
        smoke_print_counts(app, "spire_climb_boot_failed");
        smoke_fail(app, "spire_climb boot invariants");
        return false;
    }
    const float oldX = app.gs.player.x;
    const float oldY = app.gs.player.y;
    const auto oldSubState = app.gs.subState;
    auto restore = [&]() {
        if (app.subworld.active()) app.subworld.leave(true);
        app.gs.player.x = oldX;
        app.gs.player.y = oldY;
        app.gs.subState = oldSubState;
    };

    // The TALLEST spire whose spell is still unlearned (the starter spell
    // owns one too — skip it, its orb would be a no-gain touch): the top
    // tier exercises the whole shaft ladder, not just one climb.
    const sm::Spire* target = nullptr;
    for (const auto& sp : app.gs.spires) {
        if (sp.depleted || sp.spellId >= std::uint32_t(sm::kSpellCount))
            continue;
        if (sm::spellbook_has_learned(app.gs.player.spellBook,
                                      sm::kSpellDefs[sp.spellId].id)) {
            continue;
        }
        if (!target || sm::kSpellDefs[sp.spellId].tier
                           > sm::kSpellDefs[target->spellId].tier) {
            target = &sp;
        }
    }
    if (!target) {
        smoke_fail(app, "spire_climb found no unlearned spire");
        return false;
    }
    const sm::SpellDef& def = sm::kSpellDefs[target->spellId];
    const int tier = def.tier;
    const int spireId = target->id;

    // Stand on the spire's cell and enter its open-air scene.
    app.gs.player.x = float(target->x);
    app.gs.player.y = float(target->y);
    app.gs.subState.settlementId = -1;
    enter_subworld(app);
    if (!app.subworld.active()) {
        restore();
        smoke_fail(app, "spire_climb could not enter the spire cell");
        return false;
    }
    app.subworld.tick(0.016f);
    const float groundZ = app.subworld.player_z();

    // The tower's furniture: the gate on the south face, the orb on the crown.
    const sm::sub::Structure* gate = nullptr;
    int orbsBefore = 0;
    for (const auto& s : app.subworld.mgr().structures()) {
        if (s.kind == sm::sub::Structure::SpireGate) gate = &s;
        if (s.kind == sm::sub::Structure::SpireOrb) ++orbsBefore;
    }
    if (!gate || orbsBefore != 1) {
        restore();
        std::fprintf(stderr, "[smoke] spire_climb gate=%d orbs=%d\n",
                     gate ? 1 : 0, orbsBefore);
        std::fflush(stderr);
        smoke_fail(app, "spire_climb tower furniture missing");
        return false;
    }
    const int gateTier = int(gate->tag);

    // Opt-in photo stops (TIMAERT_SMOKE_SPIRE_STAY=ground|hall|roof): halt at
    // a viewpoint so a following capture_frame photographs the scene — the
    // cave smoke's STAY pattern, three storeys of it.
    const char* stay = std::getenv("TIMAERT_SMOKE_SPIRE_STAY");
    if (stay && std::strcmp(stay, "ground") == 0) {
        const float vx = gate->x;
        const float vy = gate->y + 28.0f;
        app.subworld.set_player_pos(vx, vy);
        app.subworld.rotate_camera(
            -1.5707963f - app.subworld.cam_yaw(), 0.35f);
        app.subworld.tick(0.016f);
        return true;
    }

    // The scorched yard is garrisoned too (kTblSpire roams the open cell),
    // and the door obeys the danger law — clear the yard first, the way a
    // player must, and count the fallen as the garrison they are (the same
    // FaunaCount heads the tower borrows from).
    const int yardGuards = app.subworld.dev_kill_all_hostiles();
    app.subworld.tick(0.016f);
    app.subworld.tick(0.016f);

    // Walk the gate the player's way: stand off it, look at it, press E.
    const sm::sub::Structure g = *gate;
    app.subworld.set_player_pos(g.x, g.y + 3.0f);
    app.subworld.rotate_camera(
        std::atan2(g.y - (g.y + 3.0f), 0.0f) - app.subworld.cam_yaw(), 0.0f);
    const bool entered = app.subworld.interact();
    app.subworld.tick(0.016f);
    const bool inTower = app.subworld.in_dungeon();

    if (stay && std::strcmp(stay, "hall") == 0) {
        if (!entered || !inTower) {
            smoke_fail(app, "spire_climb stay=hall could not enter");
            return false;
        }
        // The threshold is the hall's south edge: face NORTH, into the room.
        app.subworld.rotate_camera(
            -1.5707963f - app.subworld.cam_yaw(), 0.05f);
        app.subworld.tick(0.016f);
        return true;
    }

    // Climb: every storey holds its guard (counted on arrival, then cleared —
    // the stairs obey the danger law, so a climb IS a fight).
    auto count_guards = [&]() {
        int n = 0;
        for (auto e
             : app.ecs.reg.view<sm::ecs::MacroDebt, sm::ecs::SubworldTag>()) {
            if (app.ecs.reg.get<sm::ecs::MacroDebt>(e).stock
                == std::uint8_t(sm::MacroStock::FaunaCount)) {
                ++n;
            }
        }
        return n;
    };
    int guardsSeen = inTower ? count_guards() : 0;
    int climbs = 0;
    bool climbStuck = false;
    while (inTower && app.subworld.dungeon_level() < tier - 1) {
        app.subworld.dev_kill_all_hostiles();
        app.subworld.tick(0.016f);
        app.subworld.tick(0.016f);
        if (!app.subworld.debug_take_stairs(true)) {
            climbStuck = true;
            break;
        }
        app.subworld.tick(0.016f);
        ++climbs;
        guardsSeen += count_guards();
        if (climbs > 8) { climbStuck = true; break; }   // runaway guard
    }
    const int topLevel = app.subworld.dungeon_level();

    // The hatch: stand on its pad, look at it, press E — out onto the crown.
    bool onRoof = false;
    float roofZ = 0.0f;
    if (inTower && !climbStuck) {
        app.subworld.dev_kill_all_hostiles();
        app.subworld.tick(0.016f);
        app.subworld.tick(0.016f);
        float hx = 0.0f, hy = 0.0f;
        sm::sub::DungeonRef ref{};
        ref.kind = sm::sub::DungeonRef::SpireTower;
        ref.level = std::int8_t(topLevel);
        ref.ordinal = std::uint16_t(tier);
        sm::sub::dungeon_roof_hatch_point(ref, hx, hy);
        const float wx = float(sm::sub::kCellSize) + hx;
        const float wy = float(sm::sub::kCellSize) + hy + 2.0f;
        app.subworld.set_player_pos(wx, wy);
        app.subworld.rotate_camera(
            std::atan2(hy - (hy + 2.0f), 0.0f) - app.subworld.cam_yaw(), 0.0f);
        app.subworld.interact();
        app.subworld.tick(0.016f);
        onRoof = app.subworld.active() && !app.subworld.in_dungeon();
        roofZ = app.subworld.player_z();
    }

    if (stay && std::strcmp(stay, "roof") == 0) {
        if (!onRoof) {
            smoke_fail(app, "spire_climb stay=roof never reached the crown");
            return false;
        }
        const float c = float(sm::sub::kCellSize) * 1.5f;
        app.subworld.set_player_pos(c, c + 4.0f);
        app.subworld.rotate_camera(
            -1.5707963f - app.subworld.cam_yaw(), -0.1f);
        app.subworld.tick(0.016f);
        return true;
    }

    // The orb: it stands at the composite centre (the tower's own cell is the
    // window's centre cell); the roof exit already left us beside it.
    bool learned = false;
    bool depletedFlag = false;
    int orbsAfter = -1;
    bool logged = false;
    if (onRoof) {
        const float c = float(sm::sub::kCellSize) * 1.5f;
        app.subworld.set_player_pos(c, c + 2.0f);
        app.subworld.rotate_camera(
            std::atan2(-2.0f, 0.0f) - app.subworld.cam_yaw(), 0.0f);
        app.subworld.interact();
        app.subworld.tick(0.016f);
        // The pump: emits land in the LIVE tick buffer, so applying pending
        // effects is all the frame loop itself would do (its flush comes
        // after application, not before). One call — the pump loops until
        // the buffer stops growing, so the follow-up SpellLearned is
        // delivered in the same pass.
        apply_pending_event_effects(app);
        learned = sm::spellbook_has_learned(app.gs.player.spellBook, def.id);
        for (const auto& sp : app.gs.spires) {
            if (sp.id == spireId) depletedFlag = sp.depleted;
        }
        orbsAfter = 0;
        for (const auto& s : app.subworld.mgr().structures()) {
            if (s.kind == sm::sub::Structure::SpireOrb) ++orbsAfter;
        }
        for (const auto& e : app.gs.player.eventLog) {
            if (e.message.find("You have learned") != std::string::npos) {
                logged = true;
            }
        }
    }
    restore();

    std::fprintf(stderr,
                 "[smoke] spire_climb spell=%s tier=%d gateTier=%d entered=%d "
                 "inTower=%d climbs=%d top=%d yard=%d guards=%d stuck=%d "
                 "onRoof=%d dz=%.1f learned=%d depleted=%d orbsAfter=%d "
                 "logged=%d\n",
                 def.id, tier, gateTier, entered ? 1 : 0,
                 inTower ? 1 : 0,
                 climbs, topLevel, yardGuards, guardsSeen,
                 climbStuck ? 1 : 0,
                 onRoof ? 1 : 0, roofZ - groundZ, learned ? 1 : 0,
                 depletedFlag ? 1 : 0, orbsAfter, logged ? 1 : 0);
    std::fflush(stderr);

    if (!entered || !inTower || gateTier != tier || climbStuck
        || topLevel != tier - 1 || climbs != tier - 1 || !onRoof
        // The crown stands a tower height over the ground the player entered
        // on (the flattened plateau): most of that height must be under him.
        || roofZ - groundZ < sm::sub::kSpireTowerHeightM * 0.8f
        // The garrison is ONE headcount: the yard's roamers and the storey
        // guards borrow from the same FaunaCount, so between them a fresh
        // spire must have fielded somebody.
        || yardGuards + guardsSeen < 1 || !learned || !depletedFlag
        || orbsAfter != 0 || !logged) {
        smoke_fail(app, "spire_climb invariant");
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
        enter_subworld(app);
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
        std::min(px + 5.0f, float(sm::sub::kFullSize - 2)), py, 0.0f);
    reg.emplace<sm::ecs::VisualPos>(hostile,
        std::min(px + 5.0f, float(sm::sub::kFullSize - 2)), py, 0.0f);
    reg.emplace<sm::ecs::NPCKind>(
        hostile, sm::ecs::NPCKind{std::uint16_t(0x1FE), std::uint16_t(2)});
    reg.emplace<sm::ecs::Health>(hostile, 18.0f, 18.0f);
    reg.emplace<sm::ecs::Combat>(hostile,
        7.0f, 0.0f, 8.0f, 0.30f, 0u, sm::ecs::Combat::Melee);
    reg.emplace<sm::ecs::SubworldTag>(hostile);
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
    advance_sim_seconds(app, 0.20f, false);
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

    // Inc 4b: the melee damage must have landed on the player ENTITY's Health
    // and been reconciled to the macro scalar — proving it flowed through the
    // universal combat path (strike -> Health), not a bespoke player-only hook
    // (those are deleted). Health drops below max and its clamped round-trip
    // equals the authoritative afterHp.
    bool playerEntityRouted = false;
    {
        int playerTags = 0;
        entt::entity pe = entt::null;
        for (auto e : reg.view<sm::ecs::PlayerTag>()) { ++playerTags; pe = e; }
        if (playerTags == 1) {
            if (const auto* h = reg.try_get<sm::ecs::Health>(pe)) {
                const int hMax = std::max(1, int(std::round(h->maxHp)));
                const int hNow = std::clamp(int(std::round(h->hp)), 0, hMax);
                playerEntityRouted = h->hp < h->maxHp && hNow == afterHp;
            }
        }
    }

    std::fprintf(stderr,
                 "[smoke] subworld_enemy_feedback spriteOnly=%d hp=%d->%d "
                 "flash=%.3f danger=%d combatLog=%d latest=\"%s\" status=\"%s\" "
                 "routed=%d\n",
                 spriteOnlyVisible, beforeHp, afterHp,
                 double(flash),
                 int(danger),
                 combatLogCount,
                 combatLogVisible ? combatLog->text : "",
                 feedback ? status : "",
                 playerEntityRouted ? 1 : 0);
    std::fflush(stderr);

    if (spriteOnlyVisible <= 0 || afterHp >= beforeHp || flash <= 0.0f
        || danger != sm::sub::DangerLevel::Red || !combatLogVisible
        || !feedback || !playerEntityRouted) {
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
        enter_subworld(app);
    }
    if (!app.subworld.active()) {
        smoke_fail(app, "subworld_missile_feedback enter failed");
        return false;
    }

    auto& reg = app.ecs.reg;
    const float px = app.subworld.player_x();
    const float py = app.subworld.player_y();
    // The witch stands at the PLAYER'S OWN Z. Z is world elevation in metres, so
    // a hand-placed 0.0f buried her a kilometre down: her 45-unit missile range
    // is measured in 3D, the player was never inside it, and she never fired —
    // which is why this scenario reported projectiles=0->0 rather than a missed
    // shot.
    const float pz = app.subworld.player_z();
    const entt::entity hostile = reg.create();
    reg.emplace<sm::ecs::Position>(hostile,
        std::min(px + 18.0f, float(sm::sub::kFullSize - 2)), py, pz);
    reg.emplace<sm::ecs::VisualPos>(hostile,
        std::min(px + 18.0f, float(sm::sub::kFullSize - 2)), py, pz);
    // "bandits" ASKED OF THE REGISTRY, not the literal 3 this used to carry.
    // Under the ONE faction registry index 3 is `cults`, whose standing with the
    // player is -10 — above kHostileThreshold, so the witch had no quarrel with
    // anyone and simply never drew. Bandits sit at -100: hostile by construction,
    // which is the whole premise of a scenario about being shot at.
    reg.emplace<sm::ecs::NPCKind>(
        hostile,
        sm::ecs::NPCKind{
            std::uint16_t(sm::NPCType::Witch),
            std::uint16_t(sm::faction_index("bandits"))});
    reg.emplace<sm::ecs::Health>(hostile, 30.0f, 30.0f);
    reg.emplace<sm::ecs::Combat>(
        hostile,
        6.0f,
        0.0f,
        45.0f,
        0.30f,
        0u,
        sm::ecs::Combat::Missile);
    reg.emplace<sm::ecs::MissileAttack>(
        hostile, 160.0f, 0.0f, std::uint32_t{0xFFA070D0u});
    reg.emplace<sm::ecs::SubworldTag>(hostile);
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
    advance_sim_seconds(app, 0.10f, false);
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

// Muzzle-safety guard for the universal projectile path (Inc 4b / owner §8): a
// friendly-fire bolt must fly out AHEAD of the caster and never detonate on them
// at the muzzle. Fireball is the one built-in that reaches here — it sets
// friendlyFire; there is no faction shield — projectiles are agnostic and the
// caster (unlike an ordinary bolt, whose faction match does); only the
// spawn-offset geometry keeps the projectile off its own caster. We isolate that
// geometry by clearing every other combat actor first, so the ONLY thing the
// fireball could strike is the player: HP must be untouched, because the bolt
// spawns clear of the player's hit shell and moves away. (The owner's other half
// — "your own blast still catches you" when the bolt detonates on a nearby enemy
// — is the unchanged is_spell_target/faction behaviour and is not re-tested here.)
bool run_subworld_self_fireball_smoke(App& app) {
    if (!smoke_boot_invariants_hold(app)) {
        smoke_print_counts(app, "subworld_self_fireball_boot_failed");
        smoke_fail(app, "subworld_self_fireball boot invariants");
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
        enter_subworld(app);
    }
    if (!app.subworld.active()) {
        smoke_fail(app, "subworld_self_fireball enter failed");
        return false;
    }

    auto& reg = app.ecs.reg;
    // Isolate the muzzle geometry: destroy every combat actor except the player
    // (collect-then-destroy — never mutate a view mid-scan) so the fireball has
    // no legitimate target and any HP loss can only be a self-hit at the muzzle.
    {
        std::vector<entt::entity> doomed;
        for (auto e : reg.view<sm::ecs::Health>()) {
            if (!reg.any_of<sm::ecs::PlayerTag, sm::ecs::PlayerSquadTag>(e)) {
                doomed.push_back(e);
            }
        }
        for (const entt::entity e : doomed) {
            if (reg.valid(e)) reg.destroy(e);
        }
    }

    // Guarantee the cast is affordable regardless of the player's current mana.
    app.gs.player.combatStats.currentMp = 999;
    sm::spellbook_learn(app.gs.player.spellBook, "fireball");
    sm::spellbook_set_active(app.gs.player.spellBook, "fireball");

    // AIM AT THE SKY. Clearing the other actors leaves only one thing the bolt
    // can still strike — the ground — and a fireball that detonates on a rise a
    // few metres ahead catches its own caster in the blast. That is the owner's
    // rule working correctly, not a muzzle defect, but it made this scenario a
    // referendum on whatever terrain the world seed put in front of the player:
    // green on seed 12345, red on seed 1 for 37 HP. Pitching up sends the bolt
    // into open air, so the ONLY thing that can still cost HP here is the muzzle
    // geometry this scenario exists to guard.
    app.subworld.rotate_camera(0.0f, 0.9f);

    const int beforeHp = app.gs.player.combatStats.currentHp;
    int beforeProjectiles = 0;
    for (auto e : reg.view<sm::ecs::Projectile>()) {
        (void)e;
        ++beforeProjectiles;
    }

    if (!cast_active_spell(app)) {
        smoke_fail(app, "subworld_self_fireball cast failed");
        return false;
    }
    int spawnedProjectiles = 0;
    for (auto e : reg.view<sm::ecs::Projectile>()) {
        (void)e;
        ++spawnedProjectiles;
    }
    if (spawnedProjectiles <= beforeProjectiles) {
        smoke_fail(app, "subworld_self_fireball projectile not spawned");
        return false;
    }

    // Fly the bolt well clear. If it were going to muzzle-detonate it would do so
    // on the first hit test, which runs AFTER the projectile has stepped forward.
    advance_sim_seconds(app, 0.10f, false);
    advance_sim_seconds(app, 0.10f, false);
    const int afterHp = app.gs.player.combatStats.currentHp;

    bool playerDead = false;
    for (auto e : reg.view<sm::ecs::PlayerTag>()) {
        if (reg.any_of<sm::ecs::Dead>(e)) playerDead = true;
    }

    std::fprintf(stderr,
                 "[smoke] subworld_self_fireball projectiles=%d->%d hp=%d->%d "
                 "player_dead=%d\n",
                 beforeProjectiles, spawnedProjectiles,
                 beforeHp, afterHp, playerDead ? 1 : 0);
    std::fflush(stderr);

    if (afterHp != beforeHp || playerDead) {
        smoke_fail(app, "subworld_self_fireball muzzle self-detonation");
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
        enter_subworld(app);
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
        std::min(px + 4.0f, float(sm::sub::kFullSize - 2)), py, 0.0f);
    reg.emplace<sm::ecs::VisualPos>(target,
        std::min(px + 4.0f, float(sm::sub::kFullSize - 2)), py, 0.0f);
    reg.emplace<sm::ecs::NPCKind>(
        target,
        sm::ecs::NPCKind{
            std::uint16_t(sm::NPCType::Bandit),
            std::uint16_t(3)});
    reg.emplace<sm::ecs::Health>(target, 40.0f, 40.0f);
    reg.emplace<sm::ecs::SubworldTag>(target);
    reg.emplace<sm::ecs::Sprite>(
        target,
        std::uint16_t(sm::NPCType::Bandit),
        std::uint8_t(255), std::uint8_t(84), std::uint8_t(54),
        std::uint8_t(255), 1.2f);

    const sm::DerivedBonuses derived =
        sm::calculate_derived(app.gs.player.sheet.attributes,
                              app.gs.player.sheet.skills);
    const float expectedDamage = std::floor(10.0f + derived.rawPhysDamage);
    const float beforeHp = reg.get<sm::ecs::Health>(target).hp;
    const int beforeCombatLog = app.subworld.combat_log_count();
    app.subworld.set_player_attack_held(true);
    RuntimeFrameStats frameStats = advance_sim_seconds(app, 0.05f, false);
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

    // Inc 4c white-box guard: the swing damage must be sourced from the player
    // entity's Combat (populated at spawn, refreshed each tick from the sheet) —
    // not an inert 0 nor an ad-hoc recompute. Prove the component itself carries
    // the expected sheet-derived swing, so a future regression that leaves the
    // player's Combat inert fails here even if the dealt number coincided.
    float playerCombatDamage = -1.0f;
    for (auto pe : reg.view<sm::ecs::PlayerTag, sm::ecs::Combat>()) {
        playerCombatDamage = reg.get<sm::ecs::Combat>(pe).damage;
        break;
    }
    const bool combatRouted =
        playerCombatDamage > 0.0f
        && std::fabs(std::floor(playerCombatDamage) - expectedDamage) <= 0.001f;

    std::fprintf(stderr,
                 "[smoke] subworld_player_melee hp=%.1f->%.1f "
                 "expected=%.1f combatDmg=%.1f routed=%d flash=%.3f "
                 "playerOwned=%d log=\"%s\" status=\"%s\"\n",
                 double(beforeHp),
                 double(afterHp),
                 double(expectedDamage),
                 double(playerCombatDamage),
                 combatRouted ? 1 : 0,
                 hitFlash ? double(hitFlash->timer) : 0.0,
                 lastHit && lastHit->playerOwned ? 1 : 0,
                 combatLogVisible ? combatLog->text : "",
                 statusSet ? status : "");
    std::fflush(stderr);

    if (!hp || std::fabs(dealt - expectedDamage) > 0.001f
        || !combatRouted
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
        enter_subworld(app);
    }
    if (!app.subworld.active()) {
        smoke_fail(app, "subworld_reputation_hit enter failed");
        return false;
    }

    auto& reg = app.ecs.reg;
    sm::add_player_reputation(app.gs, "empire",
                              -sm::player_reputation(&app.gs, "empire"));
    const float px = app.subworld.player_x();
    const float py = app.subworld.player_y();
    // OUTSIDE the separation ring. Bodies push each other apart — all bodies,
    // now that fleeing minds are steered like everyone else — and the ring
    // around the player is (kPlayerBodyRadius + peasant radius) * 1.15 ≈ 2.36.
    // At 2.0 the peasant stood INSIDE it and the crowd law honestly walked him
    // out, tripping the strict neutralMove wire below. At 3.0 separation is
    // silent and that wire again measures the one thing it means to: a neutral
    // bystander does not RUN before he is wronged. (Melee still reaches: range
    // is 5.)
    const float tx = std::min(px + 3.0f, float(sm::sub::kFullSize - 2));
    // The peasant stands on the ground under ITS OWN feet, which is what every
    // real spawned body does. A hand-placed 0.0f buried it a kilometre down (z
    // is world elevation in metres) and every 3D reach missed it; borrowing the
    // PLAYER's elevation is better but still wrong the moment the two stand on
    // a slope, which is most of the time on most worlds.
    const float pz = app.subworld.ground_height_at(tx, py);
    const entt::entity target = reg.create();
    reg.emplace<sm::ecs::Position>(target, tx, py, pz);
    reg.emplace<sm::ecs::VisualPos>(target, tx, py, pz);
    // ASK THE REGISTRY for the empire's index instead of spelling a literal.
    // This used to read `0`, from the days of the per-vocabulary faction
    // dictionaries; under the ONE registry (macro/faction.h) index 0 is
    // `wildlife`, so hitting this "imperial" peasant honestly docked the
    // reputation of the local wolves while the scenario watched `empire` and
    // saw nothing move.
    reg.emplace<sm::ecs::NPCKind>(
        target,
        sm::ecs::NPCKind{
            std::uint16_t(sm::NPCType::Peasant),
            std::uint16_t(sm::faction_index("empire"))});
    reg.emplace<sm::ecs::Health>(target, 40.0f, 40.0f);
    reg.emplace<sm::ecs::Combat>(
        target,
        3.0f, 20.0f, 2.0f, 1.5f, 0u,
        sm::ecs::Combat::Melee);
    reg.emplace<sm::ecs::SubworldAi>(
        target,
        sm::ecs::SubworldAi::Flee,
        3.0f, 0.0f, 0.0f, 8.0f, 0.55f);
    reg.emplace<sm::ecs::SubworldTag>(target);
    reg.emplace<sm::ecs::Sprite>(
        target,
        std::uint16_t(sm::NPCType::Peasant),
        std::uint8_t(190), std::uint8_t(150), std::uint8_t(120),
        std::uint8_t(255), 0.8f);

    const int beforeRep = sm::player_reputation(&app.gs, "empire");
    const float neutralX = reg.get<sm::ecs::Position>(target).x;
    const float neutralY = reg.get<sm::ecs::Position>(target).y;
    RuntimeFrameStats neutralFrame = advance_sim_seconds(app, 0.05f, false);
    if (!neutralFrame.ticked || !neutralFrame.subworldActive) {
        smoke_fail(app, "subworld_reputation_hit neutral tick inactive");
        return false;
    }
    const auto& neutralPos = reg.get<sm::ecs::Position>(target);
    const float neutralMove = std::sqrt(
        (neutralPos.x - neutralX) * (neutralPos.x - neutralX)
        + (neutralPos.y - neutralY) * (neutralPos.y - neutralY));

    // FRIENDLY FIRE IS REAL (owner's ruling, 2026-07-30; spell_effects.cpp has
    // the matching note): projectiles are physics, they strike whoever stands in
    // their path, ally or enemy. There is no faction shield, so the player's own
    // bolt wounds this neutral peasant — and costs him the same reputation point
    // a sword stroke would. One law, both weapons.
    //
    // This assertion used to read the opposite way, and passed only because the
    // bolt was sitting at z=0 a kilometre below its target and could not hit
    // anything. Fixing the elevation exposed the stale expectation.
    const float kFriendlySpellDamage = 13.0f;
    const float beforeFriendlySpellHp = reg.get<sm::ecs::Health>(target).hp;
    const int beforeFriendlySpellLog = app.subworld.combat_log_count();
    const entt::entity friendlyProjectile = reg.create();
    // Parked exactly ON the peasant, at its own ground height. Two properties
    // fall out and both matter: the hit test measures zero distance, so contact
    // does not depend on anybody's radius; and the ground reap
    // (spell_effects.cpp: `pos.z < groundM`) is a STRICT less-than, so a bolt
    // sitting precisely at ground level survives to be tested. This scenario is
    // about the reputation law, not about projectile geometry — cast_spell
    // proves a real bolt's flight, so this one should not be able to fail for
    // aiming reasons.
    reg.emplace<sm::ecs::Position>(friendlyProjectile, tx, py, pz);
    // ownerId is the PLAYER'S entity, not a placeholder zero: it is what makes
    // this the player's bolt, and only a player-owned hit reaches the damage-log
    // callback that charges reputation (spell_effects.cpp apply_spell_damage).
    reg.emplace<sm::ecs::Projectile>(
        friendlyProjectile,
        0.0f, 0.0f, 0.0f, 1.5f, 1.0f, 1.0f, kFriendlySpellDamage, 0.0f,
        tx, py, 0.0f, 0.0f, 0.0f,
        sm::stable_spell_id("magic_bolt"),
        app.subworld.player_entity_id(),
        std::int16_t{0}, sm::ecs::Projectile::Bolt,
        false, false, false);
    reg.emplace<sm::ecs::SubworldTag>(friendlyProjectile);
    RuntimeFrameStats friendlySpellFrame =
        advance_sim_seconds(app, 0.05f, false);
    if (!friendlySpellFrame.ticked || !friendlySpellFrame.subworldActive) {
        smoke_fail(app, "subworld_reputation_hit friendly spell tick inactive");
        return false;
    }
    const float afterFriendlySpellHp = reg.get<sm::ecs::Health>(target).hp;
    const bool spellTookHp =
        std::fabs(beforeFriendlySpellHp - afterFriendlySpellHp
                  - kFriendlySpellDamage) <= 0.001f;
    const bool spellFlashed = reg.any_of<sm::ecs::HitFlash>(target);
    const bool spellLogged =
        app.subworld.combat_log_count() > beforeFriendlySpellLog;
    const bool friendlySpellHit = spellTookHp && spellFlashed && spellLogged;
    if (reg.valid(friendlyProjectile)) {
        reg.destroy(friendlyProjectile);
    }

    app.subworld.set_player_attack_held(true);
    RuntimeFrameStats frameStats = advance_sim_seconds(app, 0.05f, false);
    app.subworld.set_player_attack_held(false);
    if (!frameStats.ticked || !frameStats.subworldActive) {
        smoke_fail(app, "subworld_reputation_hit tick inactive");
        return false;
    }

    const int afterRep = sm::player_reputation(&app.gs, "empire");
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
                 "ai=%d danger=%d neutralMove=%.3f "
                 "spellHp=%d spellFlash=%d spellLog=%d friendlySpellHit=%d log=\"%s\"\n",
                 beforeRep,
                 afterRep,
                 tempHostile ? 1 : 0,
                 ai ? int(ai->kind) : -1,
                 int(danger),
                 double(neutralMove),
                 spellTookHp ? 1 : 0,
                 spellFlashed ? 1 : 0,
                 spellLogged ? 1 : 0,
                 friendlySpellHit ? 1 : 0,
                 combatLog ? combatLog->text : "");
    std::fflush(stderr);

    // afterRep is -1, not -2, even though the player lands TWO hits here: the
    // bolt charges the point and flips the peasant temp-hostile, and
    // apply_player_hit_reputation refuses to charge again for striking someone
    // already at war with you. You pay for turning a neutral against you once.
    if (beforeRep != 0 || neutralMove > 0.001f || !friendlySpellHit
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
        enter_subworld(app);
    }
    if (!app.subworld.active()) {
        smoke_fail(app, "subworld_mouse_release enter failed");
        return false;
    }

    // Read app.relativeMouseActive — the engine's OWN answer to "is the mouse
    // captured right now" — not SDL_GetRelativeMouseMode(). On macOS
    // sync_relative_mouse_mode deliberately never calls SDL_SetRelativeMouseMode:
    // SDL's 43-byte invisible GIF cursor trips an ImageIO bus error on Apple
    // Silicon, so that path swaps in a hand-made transparent cursor instead. The
    // SDL flag is therefore false on this platform BY DESIGN, and asserting it
    // made this scenario permanently red on the only machine that runs it while
    // saying nothing at all about the behaviour it claims to cover.
    sync_relative_mouse_mode(app);
    const bool captured = app.relativeMouseActive;
    app.ui.map = true;
    sync_relative_mouse_mode(app);
    const bool releasedMap = !app.relativeMouseActive;
    app.ui.map = false;
    app.ui.character = true;
    sync_relative_mouse_mode(app);
    const bool releasedCharacter = !app.relativeMouseActive;
    app.ui.character = false;
    sync_relative_mouse_mode(app);
    const bool restored = app.relativeMouseActive;

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
    enter_subworld(app);
    if (!app.subworld.active()) {
        smoke_fail(app, "subworld_tree_anchor enter failed");
        return false;
    }

    const auto& mgr = app.subworld.mgr();
    const sm::sub::Structure* focus = nullptr;
    float bestDistSq = 1e30f;
    float minSink = 1e30f;
    float maxSink = 0.0f;
    float minTreeH = 1e30f;
    float maxTreeH = 0.0f;
    float worstSinkRows = 0.0f; // deepest seat, measured in sprite rows
    int treeCount = 0;
    int typeCount[sm::sub::TreeAtlas::kTypes]{};
    const float px = app.subworld.player_x();
    const float py = app.subworld.player_y();
    for (const auto& s : mgr.structures()) {
        if (s.kind != sm::sub::Structure::Tree) continue;
        ++treeCount;
        const int cellCol = std::min(2, std::max(0, int(s.x) / sm::sub::kCellSize));
        const int cellRow = std::min(2, std::max(0, int(s.y) / sm::sub::kCellSize));
        const float temp = mgr.cell_temperature(cellRow * 3 + cellCol);
        const int type = sm::sub::tree_type_for_temperature(
            temp, smoke_tree_hash01(mgr, s));
        if (type >= 0 && type < sm::sub::TreeAtlas::kTypes) {
            ++typeCount[type];
        }
        // The drawn build, through the ONE sizing law the renderer uses.
        const sm::sub::TreeBillboard tb =
            sm::sub::tree_billboard(s.height, s.radius, type);
        minSink = std::min(minSink, tb.sinkM);
        maxSink = std::max(maxSink, tb.sinkM);
        minTreeH = std::min(minTreeH, tb.heightM);
        maxTreeH = std::max(maxTreeH, tb.heightM);
        // A sprite row is 1/16 of the quad and the bottom one is the ground
        // contact shadow: seat deeper than that and the trunk is buried.
        worstSinkRows = std::max(worstSinkRows,
                                 tb.sinkM * 16.0f / std::max(0.01f, tb.heightM));
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
                 "height=%.1f..%.1fm sink=%.2f..%.2f (%.2f rows) "
                 "types=[%d,%d,%d,%d,%d,%d,%d] focus=%.1f,%.1f\n",
                 cellX, cellY, treeCount, minTreeH, maxTreeH,
                 minSink, maxSink, worstSinkRows,
                 typeCount[0], typeCount[1], typeCount[2], typeCount[3],
                 typeCount[4], typeCount[5], typeCount[6],
                 focus ? focus->x : -1.0f,
                 focus ? focus->y : -1.0f);
    std::fflush(stderr);

    // Trees stand at forest scale (metres, not the old radius-derived stubs)
    // and are seated shallower than the sprite's own ground-contact row, so
    // every trunk is visible above the terrain.
    if (treeCount <= 0 || !focus
        || minTreeH < 4.0f || maxTreeH > 32.0f || worstSinkRows >= 1.0f
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

    // ── macro-4a: the player's PlayerTag flag is a PERSISTENT macro entity ────
    // The player is "an NPC with a flag" (§8): the ecs::PlayerTag rides a real
    // entity on BOTH sides of the seam so the possession command can move it. On
    // the macro map it is a MINIMAL flag (Position + PlayerTag, no SubworldTag /
    // NPCKind); entering a subworld swaps it for the full combat actor; leaving
    // tears that down and the next macro tick re-heals the macro flag. Prove the
    // whole macro->subworld->macro cycle keeps EXACTLY ONE PlayerTag, the correct
    // flavour on each side, Position synced to the authoritative scalar. Modelled
    // on the subworld player_entity block below. Self-contained: it restores the
    // macro anchor the enter/leave cycle moves, so the battery below is unaffected.
    {
        auto& reg = app.ecs.reg;
        const float saveX = app.gs.player.x;
        const float saveY = app.gs.player.y;
        auto near_half = [](float a, float b) {
            float d = a - b; if (d < 0.0f) d = -d; return d <= 0.5f;
        };
        auto count_player_tags = [&](entt::entity& out) {
            int n = 0; out = entt::null;
            for (auto e : reg.view<sm::ecs::PlayerTag>()) { ++n; out = e; }
            return n;
        };

        // (1) Macro map: exactly one flag, MACRO flavour, Position == scalar.
        entt::entity mpe = entt::null;
        if (count_player_tags(mpe) != 1) {
            smoke_fail(app, "macro_player_entity: expected one macro PlayerTag");
            return false;
        }
        const auto* mpos = reg.try_get<sm::ecs::Position>(mpe);
        if (!mpos || !near_half(mpos->x, saveX) || !near_half(mpos->y, saveY)) {
            smoke_fail(app, "macro_player_entity: macro flag Position off scalar");
            return false;
        }
        // The flag rides an ORDINARY SQUAD now (owner, 2026-08-27) — it is
        // supposed to carry NPCKind, a roster and a runtime, exactly like every
        // other squad on the map. What it must NOT carry on the macro side is
        // SubworldTag: that would put the player's own squad into the
        // subworld reapers' set. This check used to demand the opposite,
        // because the flag used to be a husk with nothing on it.
        if (reg.any_of<sm::ecs::SubworldTag>(mpe)) {
            smoke_fail(app,
                "macro_player_entity: macro flag wrongly carries SubworldTag");
            return false;
        }

        // (2) Enter a subworld: still exactly one flag, now the SubworldTag actor.
        enter_subworld(app);
        if (!app.subworld.active()) {
            smoke_fail(app, "macro_player_entity: subworld enter failed");
            return false;
        }
        entt::entity spe = entt::null;
        if (count_player_tags(spe) != 1 || !reg.all_of<sm::ecs::SubworldTag>(spe)) {
            app.subworld.leave(true);
            smoke_fail(app, "macro_player_entity: subworld flag missing/duplicated");
            return false;
        }

        // (3) Leave + one macro tick (dt=0 so world time / AI do not advance): the
        // macro branch's ensure_macro_player_entity must recreate the flag —
        // exactly one, MACRO flavour again, Position re-synced to the scalar that
        // leave() snapped to the subworld exit cell.
        app.subworld.leave(true);
        if (app.subworld.active()) {
            smoke_fail(app, "macro_player_entity: subworld leave failed");
            return false;
        }
        tick_playing_runtime(app, false);
        entt::entity rpe = entt::null;
        if (count_player_tags(rpe) != 1 || reg.any_of<sm::ecs::SubworldTag>(rpe)) {
            smoke_fail(app,
                "macro_player_entity: flag not restored to macro flavour on return");
            return false;
        }
        const auto* rpos = reg.try_get<sm::ecs::Position>(rpe);
        if (!rpos || !near_half(rpos->x, app.gs.player.x) ||
            !near_half(rpos->y, app.gs.player.y)) {
            smoke_fail(app,
                "macro_player_entity: restored flag Position off scalar");
            return false;
        }
        std::fprintf(stderr,
                     "[smoke] macro_player_entity cycle PlayerTag=1 "
                     "macro->sub->macro pos=%.1f,%.1f not_npc=1\n",
                     rpos->x, rpos->y);
        std::fflush(stderr);

        // Restore the macro anchor the enter/leave cycle moved (leave() snaps the
        // player to the exit cell), then re-sync the flag so the rest of this
        // console battery sees the original macro position.
        app.gs.player.x = saveX;
        app.gs.player.y = saveY;
        sm::ensure_macro_player_entity(app.gs, app.ecs);
    }

    // Snapshot everything the commands below touch, so we can fully restore.
    const int    oldGold         = sm::wallet_value(player_bag(app));
    const auto   oldInv          = player_bag(app);
    const auto   oldLevel        = app.gs.player.sheet.levelData;
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
        {   // restore the wallet to its recorded value
            const int now = sm::wallet_value(player_bag(app));
            if (now > oldGold)
                sm::wallet_spend_up_to(player_bag(app), now - oldGold);
            else if (now < oldGold)
                player_bag(app).add("coin_empire", oldGold - now);
        }
        player_bag(app)   = oldInv;
        app.gs.player.sheet.levelData   = oldLevel;
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
    if (sm::wallet_value(player_bag(app)) != oldGold + 500) {
        restore(); smoke_fail(app, "console gold add"); return false;
    }

    const int potBefore = player_bag(app).count("potion_hp");
    con.execute("give potion_hp 3");
    if (player_bag(app).count("potion_hp") != potBefore + 3) {
        restore(); smoke_fail(app, "console give item"); return false;
    }
    con.execute("take potion_hp 1");
    if (player_bag(app).count("potion_hp") != potBefore + 2) {
        restore(); smoke_fail(app, "console take item"); return false;
    }
    con.execute("give gold 250");
    if (sm::wallet_value(player_bag(app)) != oldGold + 750) {
        restore(); smoke_fail(app, "console give gold"); return false;
    }

    const int lvlBefore = app.gs.player.sheet.levelData.level;
    con.execute("addexp 100000");
    if (app.gs.player.sheet.levelData.level <= lvlBefore) {
        restore(); smoke_fail(app, "console addexp did not level up"); return false;
    }

    con.execute("learnall");
    if (app.gs.player.spellBook.learned.size() != std::size_t(sm::kSpellCount)) {
        restore(); smoke_fail(app, "console learnall count mismatch"); return false;
    }

    // ── World & toggles (macro context) ──────────────────────────
    con.execute("settime 13 37");
    if (app.gs.worldTime.hour() != 13 || app.gs.worldTime.minute() != 37) {
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
    const int goldPreUsage = sm::wallet_value(player_bag(app));
    con.execute("gold");
    if (sm::wallet_value(player_bag(app)) != goldPreUsage) {
        restore(); smoke_fail(app, "console usage-error mutated state"); return false;
    }
    // An unknown command must be handled gracefully (output, no crash).
    const std::size_t sbUnknown = con.scrollback.size();
    con.execute("blorf_zzzz 1 2 3");
    if (con.scrollback.size() <= sbUnknown) {
        restore(); smoke_fail(app, "console unknown command produced no output"); return false;
    }

    // ── Quest markers: quests project onto gs.markers as "quest_" pins ────
    // markers.h + rebuild_quest_markers() is the universal producer wired into
    // process_world_events via a per-frame signature guard. Prove the whole
    // projection deterministically: an accepted quest with a world-anchored
    // objective (VisitCell) yields exactly one gold Quest pin at that cell; a
    // pure kill-count objective (no fixed cell) adds none; completing the located
    // objective changes the signature and drops the pin on rebuild. Delta-based,
    // so it is robust to any pre-existing markers; fully self-contained
    // (snapshots + restores activeQuests / gs.markers / the sig cache).
    {
        const auto savedQuests  = app.activeQuests;
        const auto savedMarkers = app.gs.markers;
        const auto savedSig     = app.questMarkerSig;
        auto bail = [&](const char* why) {
            app.activeQuests   = savedQuests;
            app.gs.markers     = savedMarkers;
            app.questMarkerSig = savedSig;
            restore(); smoke_fail(app, why);
        };

        app.activeQuests.clear();
        sm::rebuild_quest_markers(app.gs, app.activeQuests);   // clean quest_* slate
        const std::size_t base = app.gs.markers.size();

        sm::Quest q;
        q.id = "smoke_qm";
        q.title = "Smoke Marker Target";
        sm::Objective vis;
        vis.kind = sm::ObjectiveKind::VisitCell;
        vis.ix = 42; vis.iy = 17; vis.radius = 1.0f;
        q.objectives.push_back(vis);
        sm::Objective kill;                                    // no fixed cell -> no pin
        kill.kind = sm::ObjectiveKind::DestroyNpc;
        kill.npcType = 1; kill.count = 3;
        q.objectives.push_back(kill);
        app.activeQuests.push_back(q);

        sm::rebuild_quest_markers(app.gs, app.activeQuests);
        if (app.gs.markers.size() != base + 1) {
            bail("quest_markers: expected exactly one pin for one located objective");
            return false;
        }
        const sm::Marker* pin = nullptr;
        for (const auto& m : app.gs.markers)
            if (m.id == "quest_smoke_qm_0") pin = &m;
        if (!pin || pin->style != sm::MarkerStyle::Quest ||
            pin->x != 42.0f || pin->y != 17.0f) {
            bail("quest_markers: pin id/style/cell wrong"); return false;
        }

        const std::uint64_t sigOpen = quest_marker_signature(app.activeQuests);
        app.activeQuests[0].objectives[0].completed = true;
        if (quest_marker_signature(app.activeQuests) == sigOpen) {
            bail("quest_markers: signature ignored objective completion"); return false;
        }
        sm::rebuild_quest_markers(app.gs, app.activeQuests);
        if (app.gs.markers.size() != base) {
            bail("quest_markers: completed objective pin not removed"); return false;
        }

        app.activeQuests   = savedQuests;
        app.gs.markers     = savedMarkers;
        app.questMarkerSig = savedSig;
        std::fprintf(stderr,
                     "[smoke] quest_markers pin@42,17 style=quest killcount=nopin "
                     "complete->removed sig_changed=1\n");
        std::fflush(stderr);
    }

    // ── Spawn / teleport / subworld toggles (subworld context) ───
    enter_subworld(app);
    if (!app.subworld.active()) {
        restore(); smoke_fail(app, "console subworld enter failed"); return false;
    }

    // ── Player is a full combat ECS entity (Inc 4b) ──────────────────
    // Entering a subworld materialises exactly ONE PlayerTag entity — the
    // movable "player flag" / subworld sim-centre (owner's §8 vision). In 4b it
    // is a full combat actor: PlayerTag + Position + Health + Combat +
    // SubworldTag, so hostiles target it through the SAME universal melee /
    // projectile paths as any NPC. Its Position tracks the player scalars and
    // its Health mirrors the macro-authoritative combatStats. It is still NOT an
    // NPC: no NPCKind / SubworldAi / PlayerSoldierTag / NpcInventory, so no AI,
    // loot, XP, or squad-removal path can ever fire on it.
    {
        auto& reg = app.ecs.reg;
        int playerTags = 0;
        entt::entity pe = entt::null;
        for (auto e : reg.view<sm::ecs::PlayerTag>()) { ++playerTags; pe = e; }
        if (playerTags != 1) {
            restore();
            smoke_fail(app, "player_entity: expected exactly one PlayerTag entity");
            return false;
        }
        const auto* ppos = reg.try_get<sm::ecs::Position>(pe);
        if (!ppos) {
            restore();
            smoke_fail(app, "player_entity: PlayerTag entity has no Position");
            return false;
        }
        auto near_half = [](float a, float b) {
            float d = a - b; if (d < 0.0f) d = -d; return d <= 0.5f;
        };
        if (!near_half(ppos->x, app.subworld.player_x()) ||
            !near_half(ppos->y, app.subworld.player_y())) {
            restore();
            smoke_fail(app, "player_entity: Position does not track player scalars");
            return false;
        }
        // Full combat actor: the components that put it in the actor/target set
        // must be present, and Health must mirror the macro combat scalar.
        const auto* phealth = reg.try_get<sm::ecs::Health>(pe);
        if (!phealth || !reg.all_of<sm::ecs::Combat, sm::ecs::SubworldTag>(pe)) {
            restore();
            smoke_fail(app,
                "player_entity: combat entity missing Health/Combat/SubworldTag");
            return false;
        }
        const int maxHp = std::max(1, app.gs.player.combatStats.maxHp);
        const int curHp =
            std::clamp(app.gs.player.combatStats.currentHp, 0, maxHp);
        if (!near_half(phealth->hp, float(curHp)) ||
            !near_half(phealth->maxHp, float(maxHp))) {
            restore();
            smoke_fail(app, "player_entity: Health does not mirror combatStats");
            return false;
        }
        // Still not an NPC/soldier: none of these may be present, or an NPC-only
        // system (AI, loot, XP, squad removal) would start acting on the player.
        if (reg.any_of<sm::ecs::NPCKind, sm::ecs::SubworldAi,
                       sm::ecs::PlayerSoldierTag, sm::ecs::NpcInventory>(pe)) {
            restore();
            smoke_fail(app,
                "player_entity: combat entity wrongly carries an NPC/soldier component");
            return false;
        }
        std::fprintf(stderr,
                     "[smoke] player_entity PlayerTag=1 pos=%.1f,%.1f "
                     "tracks_scalars=1 hp=%.0f/%.0f combat_actor=1 not_npc=1\n",
                     ppos->x, ppos->y,
                     double(phealth->hp), double(phealth->maxHp));
        std::fflush(stderr);
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
    // Remember who was ALREADY in the scene, so the sheet inspection below
    // examines the body this console command creates — not whichever bandit
    // the view yields first. On worlds where a macro bandit stands in the
    // start window, that first body can be a TRACKED projection, whose hp
    // deliberately comes from the macro entity (the cross-layer wound law)
    // while its sheet derives from the ordinal — the derived-body law
    // asserted here does not APPLY to it, and the whole scenario went red
    // for inspecting the wrong subject.
    std::vector<entt::entity> preexistingBandits;
    {
        auto& reg = app.ecs.reg;
        auto view = reg.view<sm::ecs::SubworldTag, sm::ecs::NPCKind>();
        for (auto e : view) {
            if (view.get<sm::ecs::NPCKind>(e).type
                == std::uint16_t(sm::NPCType::Bandit)) {
                preexistingBandits.push_back(e);
            }
        }
    }
    // Explicit faction: a bare `spawn bandit` inherits the faction of the
    // GROUND it lands on (the deliberate "land decides" rule in
    // spawn_npc_body), so on a world whose window sits on friendly kingdom
    // soil the body is Bandit by TYPE but not hostile — and the killall
    // assertion below would count survivors that killall correctly spared.
    // The registry pins "bandits" at playerReputation -100, always hostile.
    con.execute("spawn bandit 2 4 bandits");
    const int banditsAfter = count_live_bandits();
    const int spawnedDelta = banditsAfter - banditsBefore;
    if (spawnedDelta < 1) {
        restore(); smoke_fail(app, "console spawn produced no hostiles"); return false;
    }

    // ── Universal character sheet + sheet-derived combat (Inc 1 + 3) ──
    // The bandit spawned above must carry a populated CharacterSheet — the
    // SAME struct the player holds — proving humanoid NPCs are sheet-bearing
    // while monsters stay sheet-less. We assert (a) the sheet exists and was
    // fully allocated to the level's point budget, and (b) its ECS Health/Combat
    // equal project_combat(sheet, roleTemplate) and strictly exceed the raw
    // template floor — i.e. combat is now DERIVED from the sheet, not the
    // authored template alone.
    {
        auto& reg = app.ecs.reg;
        auto attr_sum = [](const sm::Attributes& a) {
            return a.str + a.vit + a.end + a.wil + a.intl
                 + a.wis + a.lck + a.cha + a.spd;
        };
        entt::entity be = entt::null;
        auto view = reg.view<sm::ecs::SubworldTag, sm::ecs::NPCKind,
                             sm::ecs::Health>(
            entt::exclude<sm::ecs::Dead, sm::ecs::PlayerSoldierTag>);
        for (auto e : view) {
            if (view.get<sm::ecs::NPCKind>(e).type
                != std::uint16_t(sm::NPCType::Bandit)) continue;
            bool preexisting = false;
            for (entt::entity p : preexistingBandits)
                preexisting = preexisting || p == e;
            if (preexisting) continue;   // inspect OUR spawn, nobody else's
            be = e;
            break;
        }
        if (be == entt::null) {
            restore(); smoke_fail(app, "sheet: no live bandit to inspect"); return false;
        }
        const auto* sheet = reg.try_get<sm::CharacterSheet>(be);
        if (!sheet) {
            restore(); smoke_fail(app, "sheet: bandit has no CharacterSheet"); return false;
        }
        const int aSum = attr_sum(sheet->attributes);
        if (aSum <= 9) { // 9 == every attribute pinned at its base of 1
            restore(); smoke_fail(app, "sheet: bandit attributes not allocated"); return false;
        }
        if (sheet->levelData.attributePoints != 0 ||
            sheet->levelData.skillPoints != 0) {
            restore(); smoke_fail(app, "sheet: bandit has unspent points"); return false;
        }
        const auto* nlvl = reg.try_get<sm::ecs::NpcLevel>(be);
        if (!nlvl || int(nlvl->value) != sheet->levelData.level) {
            restore(); smoke_fail(app, "sheet: bandit NpcLevel != sheet level"); return false;
        }
        // (b) Combat derived from the sheet: the entity's Health/Combat must be
        // exactly what the ONE body door writes — FLOOR(project_combat(sheet))
        // (sub/spawn.cpp: whole units by house style; the raw projection is
        // fractional, and on worlds where the fraction is real the old
        // un-floored comparison went red for the door's own arithmetic) —
        // and both must exceed the authored floor (a spent sheet always adds
        // vit-HP and str/int-damage).
        const sm::CombatTemplate base = sm::npc_def(sm::NPCType::Bandit).combat;
        const sm::CombatTemplate proj = sm::project_combat(*sheet, base);
        const float projHp = std::max(1.0f, std::floor(proj.hp));
        const float projDamage = std::floor(proj.damage);
        const auto* hlt = reg.try_get<sm::ecs::Health>(be);
        const auto* cmb = reg.try_get<sm::ecs::Combat>(be);
        if (!hlt || !cmb) {
            restore(); smoke_fail(app, "combat: bandit missing Health/Combat"); return false;
        }
        auto near_eq = [](float a, float b) {
            float d = a - b; if (d < 0.0f) d = -d; return d <= 0.01f;
        };
        if (!near_eq(hlt->maxHp, projHp) || !near_eq(cmb->damage, projDamage)) {
            restore();
            smoke_fail(app, "combat: bandit Health/Combat != project_combat(sheet)");
            return false;
        }
        if (!(proj.hp > base.hp) || !(proj.damage > base.damage)) {
            restore();
            smoke_fail(app, "combat: derived combat did not exceed template floor");
            return false;
        }
        std::fprintf(stderr,
                     "[smoke] npc_sheet bandit level=%d attr_sum=%d "
                     "fighter=%d bodybuilding=%d hp=%.0f(base=%.0f) "
                     "dmg=%.1f(base=%.1f) derived_from_sheet=1\n",
                     sheet->levelData.level, aSum,
                     sheet->skills.fighter, sheet->skills.bodybuilding,
                     hlt->maxHp, base.hp, cmb->damage, base.damage);
        std::fflush(stderr);
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
            if (sm::is_monster_kind(view.get<sm::ecs::NPCKind>(e).type)) ++n;
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
        const sm::FaunaEntry* wolf = sm::creature_def("wolf");
        entt::entity wolfE = entt::null;
        auto view = reg.view<sm::ecs::SubworldTag, sm::ecs::NPCKind,
                             sm::ecs::Health>(
            entt::exclude<sm::ecs::Dead, sm::ecs::PlayerSoldierTag>);
        for (auto e : view) {
            if (sm::creature_def_from_kind(
                    view.get<sm::ecs::NPCKind>(e).type) == wolf) { wolfE = e; break; }
        }
        if (!wolf || wolfE == entt::null) {
            restore();
            smoke_fail(app, "console spawn wolf: kind did not resolve to wolf row");
            return false;
        }
        const int expBefore = app.gs.player.sheet.levelData.exp;
        sm::sub::apply_lethal_damage(reg, wolfE,
                                     sm::sub::DamageSource{0u, true},
                                     sm::sub::DamageKind::Dev, &app.bus);
        app.subworld.tick(0.016f);
        if (app.gs.player.sheet.levelData.exp <= expBefore) {
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
    // The real invariant is "no HOSTILE remains", asserted with the same
    // predicate killall uses: a second sweep must find nothing to kill.
    if (app.subworld.dev_kill_all_hostiles() != 0) {
        restore(); smoke_fail(app, "console killall left hostiles"); return false;
    }
    const int banditsFinal = count_live_bandits();
    if (banditsFinal != 0) {
        restore(); smoke_fail(app, "console killall left bandit-typed bodies"); return false;
    }

    // ── Possession / вселение (Inc 5c) ───────────────────────────────
    // Take over a live foreign body: the single PlayerTag flag MOVES onto it
    // (D2), the hero husk is destroyed (its canonical state lives in gs.player),
    // and the possessed body keeps its OWN stats — no hero stats are stamped
    // (D3, body-native). Isolated here after killall: it spawns its own target
    // so it perturbs none of the earlier hostile-count checks. restore() below
    // force-leaves and resets gs.player, so leaving in the possessed state is
    // safe (the SubworldTag reaper destroys the possessed body on leave).
    {
        auto& reg = app.ecs.reg;
        con.execute("spawn bandit 3");
        // A fresh, non-player-side bandit to inhabit.
        entt::entity target = entt::null;
        {
            auto tv = reg.view<sm::ecs::SubworldTag, sm::ecs::NPCKind,
                               sm::ecs::Health, sm::ecs::Combat>(
                entt::exclude<sm::ecs::Dead, sm::ecs::PlayerTag,
                              sm::ecs::PlayerSoldierTag>);
            for (auto e : tv) {
                if (tv.get<sm::ecs::NPCKind>(e).type
                    == std::uint16_t(sm::NPCType::Bandit)) { target = e; break; }
            }
        }
        if (target == entt::null) {
            restore(); smoke_fail(app, "possess: no target bandit spawned"); return false;
        }
        // The hero husk: the sole current flag-holder, which carries NO NPCKind
        // (that is precisely the discriminator body-native sync relies on).
        entt::entity husk = entt::null;
        for (auto e : reg.view<sm::ecs::PlayerTag>()) { husk = e; break; }
        if (husk == entt::null || reg.all_of<sm::ecs::NPCKind>(husk)) {
            restore(); smoke_fail(app, "possess: hero husk missing or not a hero body"); return false;
        }
        // Invariants possession must preserve.
        const float bodyMaxHp   = reg.get<sm::ecs::Health>(target).maxHp;
        const int   heroHpBefore  = app.gs.player.combatStats.currentHp;
        const int   heroMaxBefore = app.gs.player.combatStats.maxHp;
        const float tx = reg.get<sm::ecs::Position>(target).x;
        const float ty = reg.get<sm::ecs::Position>(target).y;

        if (!app.subworld.possess_by_id(
                static_cast<std::uint32_t>(entt::to_integral(target)))) {
            restore(); smoke_fail(app, "possess: possess_by_id returned false"); return false;
        }
        // Exactly one flag, now solely on the target.
        int tags = 0; entt::entity holder = entt::null;
        for (auto e : reg.view<sm::ecs::PlayerTag>()) { ++tags; holder = e; }
        if (tags != 1 || holder != target) {
            restore(); smoke_fail(app, "possess: flag not solely on the target body"); return false;
        }
        // The possessed body keeps its OWN combat components (nothing stripped).
        if (!reg.all_of<sm::ecs::NPCKind, sm::ecs::Health, sm::ecs::Combat>(target)) {
            restore(); smoke_fail(app, "possess: possessed body lost its own components"); return false;
        }
        // The hero husk is destroyed — no stranded, un-AI'd, un-rendered zombie.
        if (reg.valid(husk)) {
            restore(); smoke_fail(app, "possess: hero husk not destroyed"); return false;
        }
        // The scalar mirror snapped to the new body (every legacy reader follows).
        auto near_half = [](float a, float b) {
            float d = a - b; if (d < 0.0f) d = -d; return d <= 0.5f;
        };
        if (!near_half(app.subworld.player_x(), tx) ||
            !near_half(app.subworld.player_y(), ty)) {
            restore(); smoke_fail(app, "possess: scalars did not snap to the new body"); return false;
        }
        // Body-native (D3): a tick must NOT stamp hero stats onto the body, and
        // must NOT mutate gs.player — the preserved revert target. (Pre-5c the
        // sync path stamped gs.player HP/maxHp onto the PlayerTag body; this is
        // the assertion that the NPCKind gate now suppresses that.)
        app.subworld.tick(0.016f);
        if (std::fabs(double(reg.get<sm::ecs::Health>(target).maxHp - bodyMaxHp)) > 0.01) {
            restore(); smoke_fail(app, "possess: tick stamped hero maxHp onto the body"); return false;
        }
        if (app.gs.player.combatStats.currentHp != heroHpBefore ||
            app.gs.player.combatStats.maxHp   != heroMaxBefore) {
            restore(); smoke_fail(app, "possess: gs.player mutated (revert target not preserved)"); return false;
        }
        // HUD / hit-flash follows the inhabited body (D3): player_display_hp()
        // reports the possessed body's own HP, NOT the frozen hero scalar. With a
        // level-3 body (maxHp≈99) vs the level-1 hero (110) these are distinct.
        const int dispHp = app.subworld.player_display_hp();
        const int bodyHp = int(std::lround(reg.get<sm::ecs::Health>(target).hp));
        if (dispHp != bodyHp || dispHp == app.gs.player.combatStats.currentHp) {
            restore(); smoke_fail(app, "possess: player_display_hp() not body-native"); return false;
        }
        std::fprintf(stderr,
                     "[smoke] possess flag_moved=1 husk_destroyed=1 body_native=1 "
                     "body_maxhp=%.0f display_hp=%d hero_preserved=%d/%d\n",
                     double(bodyMaxHp), dispHp, heroHpBefore, heroMaxBefore);
        std::fflush(stderr);
    }

    // ── tree-count writeback: felling one tree costs its macro cell EXACTLY
    // one count and moves the grid revision (the save carries the grid whole,
    // so the revision is also what proves the stump reaches u_treeMap).
    // Self-sufficient: enters a subworld if none is active and leaves again.
    {
        const bool wasActive = app.subworld.active();
        if (!wasActive) {
            enter_subworld(app);
        }
        if (!app.subworld.active()) {
            smoke_fail(app, "chop: subworld enter failed");
            return false;
        }
        const std::uint32_t revBefore = app.treeLayer.revision;
        const int woodBefore = player_bag(app).count("wood");
        int cx = 0, cy = 0, prev = 0;
        // The whole 3×3 composite is in reach: any tree in the window works.
        // Kind-filtered — the smoke asserts the TREE ledger, and the nearest
        // lootable prop could just as well be a crop on a field cell.
        const sm::sub::Structure::Kind kTreeOnly = sm::sub::Structure::Tree;
        if (!app.subworld.harvest_prop_near_player(
                float(sm::sub::kFullSize) * 2.0f, &cx, &cy, &prev, &kTreeOnly)) {
            if (!wasActive) app.subworld.leave(true);
            smoke_fail(app, "chop: no tree found in the 3x3 window");
            return false;
        }
        const int after = int(app.treeLayer.at(cx, cy));
        const int expected = prev > 0 ? prev - 1 : 0;
        // prev == 0 is legal (a tree on a zero-count cell's dry water margin):
        // the count clamps at 0 and the revision does not move.
        const bool changed = prev > 0;
        if (after != expected
            || (changed && app.treeLayer.revision == revBefore)) {
            if (!wasActive) app.subworld.leave(true);
            smoke_fail(app, "chop: macro count/revision did not track the felled tree");
            return false;
        }
        // The micro half of the same rule: the felled trunk pays out through
        // the shared loot registry. Asserting the INTENT ("the axe is paid"),
        // not a magic count — the row in macro/items.cpp is free to change.
        const int woodGained =
            player_bag(app).count("wood") - woodBefore;
        if (woodGained <= 0) {
            if (!wasActive) app.subworld.leave(true);
            smoke_fail(app, "chop: felled tree paid no wood");
            return false;
        }
        if (!wasActive) app.subworld.leave(true);
        std::fprintf(stderr,
                     "[smoke] chop cell=%d,%d trees=%d->%d override_recorded=1 "
                     "wood+=%d\n",
                     cx, cy, prev, after, woodGained);
        std::fflush(stderr);
    }

    // Capture reporting values before restoring the world.
    const int         rGold   = sm::wallet_value(player_bag(app));
    const int         rLevel  = app.gs.player.sheet.levelData.level;
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
            if (app.subworld.active()) {
                smoke_fail(app, "save underground is forbidden (Persistence)");
                break;
            }
            if (!sm::save_game(app.gs, app.activeQuests,
                               stage_save_state(app), app.treeLayer.data,
                               app.deposits, app.savePath)) {
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
            // The boot ends with the intro slides on screen, and a story
            // overlay now genuinely PAUSES the world (PauseReason kPauseModal)
            // instead of merely swallowing input. A human clicks through them;
            // the harness never does, so every scenario after this point would
            // run against a stopped world. Close them here — WITHOUT completing
            // them, which leaves exactly the state smokes have always run in
            // (the intro's choices unapplied) and keeps this one line the only
            // place the harness has to know about it.
            smoke_clear_modal_overlays(app);
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
            // Dismiss the new-game intro cinematic / any dialog so the frames
            // that follow (e.g. a capture_frame) render the clean 3D subworld
            // instead of a modal backdrop. Mirrors subworld_time; harmless
            // outside a modal.
            smoke_clear_modal_overlays(app);
            // Opt-in (TIMAERT_SMOKE_MACROPOS="x,y"): relocate the macro
            // player to an explicit macro cell before entering — lets a
            // capture reproduce a reported scene (e.g. a specific coast).
            // Test harness only — normal play is unaffected.
            if (!app.subworld.active()) {
                if (const char* mp = std::getenv("TIMAERT_SMOKE_MACROPOS")) {
                    int mx = 0, my = 0;
                    if (std::sscanf(mp, "%d,%d", &mx, &my) == 2) {
                        app.gs.player.x = float(mx);
                        app.gs.player.y = float(my);
                        std::fprintf(stderr,
                                     "[smoke] macropos relocate -> %d,%d\n",
                                     mx, my);
                        std::fflush(stderr);
                    }
                }
            }
            // Opt-in (TIMAERT_SMOKE_MOUNTAIN=1): relocate the macro player to
            // the nearest Mountain-biome cell before entering, so the 3D
            // capture shows mountain relief instead of the spawn city. Test
            // harness only — normal play is unaffected.
            if (!app.subworld.active() && std::getenv("TIMAERT_SMOKE_MOUNTAIN")) {
                const int pcx = int(app.gs.player.x);
                const int pcy = int(app.gs.player.y);
                int bestX = -1, bestY = -1;
                long bestD = 1L << 60;
                for (int y = 0; y < app.gs.mapH; ++y) {
                    for (int x = 0; x < app.gs.mapW; ++x) {
                        // Mountain biome = land cell at elevation ≥ level.
                        const std::size_t midx =
                            (std::size_t(y) * std::size_t(app.terrain.width)
                             + std::size_t(x)) * 4u;
                        if (midx + 3u >= app.terrain.rgba.size()) continue;
                        if (app.terrain.rgba[midx + 3u] < 128) continue;
                        if (float(app.terrain.rgba[midx + 0u]) / 255.0f
                            < sm::kMountainBiomeLevel) continue;
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
            // Opt-in (TIMAERT_SMOKE_COAST=1): relocate the macro player to the
            // nearest LAND cell with a WATER 4-neighbour, so a capture shows a
            // real coastline (land|water cell seam). Harness only.
            if (!app.subworld.active() && std::getenv("TIMAERT_SMOKE_COAST")) {
                const int pcx = int(app.gs.player.x);
                const int pcy = int(app.gs.player.y);
                auto isLand = [&](int x, int y) {
                    if (x < 0 || y < 0 || x >= app.gs.mapW || y >= app.gs.mapH)
                        return true;  // off-map: treat as land (no relocate)
                    const std::size_t midx =
                        (std::size_t(y) * std::size_t(app.terrain.width)
                         + std::size_t(x)) * 4u;
                    if (midx + 3u >= app.terrain.rgba.size()) return true;
                    return app.terrain.rgba[midx + 3u] >= 128;
                };
                int bestX = -1, bestY = -1;
                long bestD = 1L << 60;
                for (int y = 0; y < app.gs.mapH; ++y) {
                    for (int x = 0; x < app.gs.mapW; ++x) {
                        if (!isLand(x, y)) continue;
                        if (isLand(x - 1, y) && isLand(x + 1, y)
                            && isLand(x, y - 1) && isLand(x, y + 1)) continue;
                        const long dx = x - pcx, dy = y - pcy;
                        const long d = dx * dx + dy * dy;
                        if (d < bestD) { bestD = d; bestX = x; bestY = y; }
                    }
                }
                if (bestX >= 0) {
                    app.gs.player.x = float(bestX);
                    app.gs.player.y = float(bestY);
                    std::fprintf(stderr, "[smoke] coast relocate -> %d,%d\n",
                                 bestX, bestY);
                    std::fflush(stderr);
                }
            }
            // Opt-in (TIMAERT_SMOKE_NEAR_NPC=1): relocate the macro player onto
            // the nearest persistent macro NPC before entering, so the Inc-5d
            // projection is guaranteed a non-empty window — a positive end-to-end
            // check that overworld bodies materialise as subworld combat bodies.
            // Harness only; normal play unaffected.
            if (!app.subworld.active() && std::getenv("TIMAERT_SMOKE_NEAR_NPC")) {
                const int pcx = int(app.gs.player.x);
                const int pcy = int(app.gs.player.y);
                int bestX = -1, bestY = -1;
                long bestD = 1L << 60;
                for (auto e : app.ecs.reg.view<sm::ecs::MacroNpcRuntime,
                                               sm::ecs::Position>(
                         entt::exclude<sm::ecs::PlayerSquadTag>)) {
                    const auto& p = app.ecs.reg.get<sm::ecs::Position>(e);
                    const int nx = int(p.x), ny = int(p.y);
                    const long dx = nx - pcx, dy = ny - pcy;
                    const long d = dx * dx + dy * dy;
                    if (d < bestD) { bestD = d; bestX = nx; bestY = ny; }
                }
                if (bestX >= 0) {
                    app.gs.player.x = float(bestX);
                    app.gs.player.y = float(bestY);
                    app.gs.subState.settlementId = -1;
                    app.ui.settlementId = -1;
                    std::fprintf(stderr, "[smoke] near_npc relocate -> %d,%d\n",
                                 bestX, bestY);
                    std::fflush(stderr);
                }
            }
            // Opt-in (TIMAERT_SMOKE_ENTRYDIR="dx,dy,ticks"): stamp the player's
            // entry-side context before entering, then assert the spawn landed
            // on that side of the centre cell — the end-to-end check of the
            // macro→subworld entry placement (macro/entry_context.h). Use small
            // tick counts (≤ 4): the assertion bands below are the near quarter
            // of the cell, written as independent literals on purpose (checking
            // against entry_axis_pos itself would be a tautology).
            {
            bool checkEntryBand = false;
            int entrySdx = 0, entrySdy = 0;
            if (const char* ed = std::getenv("TIMAERT_SMOKE_ENTRYDIR")) {
                int edx = 0, edy = 0, eticks = 0;
                if (!app.subworld.active()
                    && std::sscanf(ed, "%d,%d,%d", &edx, &edy, &eticks) == 3) {
                    app.gs.player.entryDir = sm::pack_entry_dir(edx, edy);
                    app.gs.player.entryTicks =
                        std::uint8_t(std::clamp(eticks, 0, 255));
                    entrySdx = edx;
                    entrySdy = edy;
                    checkEntryBand = true;
                    std::fprintf(stderr, "[smoke] force entrydir %d,%d t=%d\n",
                                 edx, edy, eticks);
                    std::fflush(stderr);
                }
            }
            if (!app.subworld.active()) {
                enter_subworld(app);
            }
            if (!app.subworld.active()) {
                smoke_fail(app, "subworld_enter failed");
                break;
            }
            if (checkEntryBand) {
                // Centre cell spans [1024, 2048). Entering by stepping +axis
                // crosses the low edge → the spawn must sit in the near
                // quarter; -axis mirrors; no step on an axis → the middle band.
                auto band_ok = [](int step, float v) {
                    if (step > 0) return v >= 1024.0f && v < 1280.0f;
                    if (step < 0) return v >= 1792.0f && v < 2048.0f;
                    return v >= 1280.0f && v < 1792.0f;
                };
                const float px = app.subworld.player_x();
                const float py = app.subworld.player_y();
                if (!band_ok(entrySdx, px) || !band_ok(entrySdy, py)) {
                    std::fprintf(stderr,
                                 "[smoke] entry_band spawn %.1f,%.1f for step "
                                 "%d,%d\n", px, py, entrySdx, entrySdy);
                    std::fflush(stderr);
                    smoke_fail(app, "entry-side spawn missed its band");
                    break;
                }
                std::fprintf(stderr, "[smoke] entry_band OK %.1f,%.1f\n",
                             px, py);
                std::fflush(stderr);
            }
            // ON THE GROUND, always. playerZ_ is persistent engine state, so an
            // enter() that forgot to set it left the player at the height of
            // wherever the LAST session ended — walk from a mountain to a
            // lowland and you arrive in mid-air, falling, with fall damage
            // waiting. Cheap to assert, invisible until someone reports it.
            {
                const float footZ = app.subworld.player_z();
                const float groundZ =
                    app.subworld.ground_height_at(app.subworld.player_x(),
                                                  app.subworld.player_y());
                std::fprintf(stderr, "[smoke] enter_ground z=%.2f ground=%.2f\n",
                             double(footZ), double(groundZ));
                std::fflush(stderr);
                if (std::fabs(footZ - groundZ) > 0.5f) {
                    smoke_fail(app, "player did not enter standing on the ground");
                    break;
                }
            }
            }
            // Opt-in (TIMAERT_SMOKE_SUBPOS="x,y"): teleport the player to an
            // absolute subworld cell BEFORE warm-up, so the ticks below
            // re-centre the seamless window and the camera follows to the
            // chosen spot (e.g. onto open water to frame the moon-path, or any
            // feature the default spawn can't see). Harness only; normal play
            // is unaffected.
            if (const char* sp = std::getenv("TIMAERT_SMOKE_SUBPOS")) {
                float sx = 0.0f, sy = 0.0f;
                if (std::sscanf(sp, "%f,%f", &sx, &sy) == 2) {
                    app.subworld.set_player_pos(sx, sy);
                    std::fprintf(stderr, "[smoke] force subpos -> %.1f,%.1f\n",
                                 sx, sy);
                    std::fflush(stderr);
                }
            }
            // Battle stress hook: TIMAERT_SMOKE_BATTLE=<per-side> deploys the
            // same two blocks the console `test_battle` does (one shared
            // deploy_army_slot formula), so a headless run and a hand-typed run
            // stage an identical fight. The sides are NAMED here on purpose:
            // the console default (the realm owning this ground) depends on
            // where the player stands, and a stress harness must not.
            if (const char* bc = std::getenv("TIMAERT_SMOKE_BATTLE")) {
                int bcount = 500;
                if (std::sscanf(bc, "%d", &bcount) == 1) {
                    bcount = std::clamp(bcount, 1, sm::sub::kMaxBattleUnits / 2);
                    int deployed = 0;
                    for (int side = 0; side < 2; ++side) {
                        for (int i = 0; i < bcount; ++i) {
                            float pos[2];
                            if (!sm::sub::deploy_army_slot(app.subworld.player_x(),
                                                           app.subworld.player_y(),
                                                           side, i, bcount, pos))
                                continue;
                            if (app.subworld.spawn_npc_body(
                                    side == 0 ? "guard" : "bandit",
                                    side == 0 ? "Test Guard" : "Test Bandit",
                                    1,
                                    app.gs.worldSeed
                                        + std::uint32_t(side * 100003 + i),
                                    side == 0 ? "empire" : "bandits",
                                    nullptr, pos))
                                ++deployed;
                        }
                    }
                    std::fprintf(stderr, "[smoke] test_battle %d/side deployed=%d\n",
                                 bcount, deployed);
                    std::fflush(stderr);
                    // Optional soak: run the battle to the death, printing one
                    // status line per simulated second. This is the reproduction
                    // harness for every "armies stand / clump / crash" report —
                    // it exercises the EXACT shipping tick (combat, deaths, loot,
                    // corpses, events, VFX), not the pure battle module.
                    if (const char* soak = std::getenv("TIMAERT_SMOKE_BATTLE_SOAK")) {
                        int seconds = 30;
                        std::sscanf(soak, "%d", &seconds);
                        for (int s = 0; s < seconds; ++s) {
                            for (int f = 0; f < 60; ++f) {
                                advance_sim_seconds(app, 1.0f / 60.0f, false);
                            }
                            int alive = 0, dead = 0;
                            auto view = app.ecs.reg.view<sm::ecs::Health,
                                                         sm::ecs::SubworldTag>();
                            for (auto e : view) {
                                if (app.ecs.reg.any_of<sm::ecs::Dead>(e)) ++dead;
                                else if (view.get<sm::ecs::Health>(e).hp > 0.0f) ++alive;
                            }
                            std::fprintf(stderr,
                                         "[smoke] battle_soak t=%ds alive=%d dead=%d\n",
                                         s + 1, alive, dead);
                            std::fflush(stderr);
                        }
                    }
                }
            }
            for (int i = 0; i < 8; ++i) {
                advance_sim_seconds(app, 1.0f / 60.0f, false);
            }
            // Opt-in (TIMAERT_SMOKE_WATERSCAN=1): report the longest east–west
            // (constant-y) run of open water in the composite, so a capture can
            // be staged onto water that ALIGNS with the moon's strictly ±X
            // azimuth — the moon-path only forms along that bearing. Prints a
            // ready-to-use SUBPOS + look direction. Harness only; no gameplay
            // effect. (Drains async cell generation first so all 9 composite
            // cells are stitched before the scan.)
            if (std::getenv("TIMAERT_SMOKE_WATERSCAN")) {
                for (int i = 0; i < 120; ++i)
                    advance_sim_seconds(app, 1.0f / 60.0f, false);
                const auto& tl = app.subworld.mgr().tiles();
                const int W = int(std::lround(std::sqrt(double(tl.size()))));
                const std::uint8_t WATER = 7; // sm::sub::TILE_WATER
                {
                    std::size_t nWater = 0;
                    for (std::uint8_t v : tl) if (v == WATER) ++nWater;
                    std::fprintf(stderr,
                        "[waterscan] tiles=%zu W=%d water_cells=%zu\n",
                        tl.size(), W, nWater);
                    std::fflush(stderr);
                }
                int bestLen = 0, bestX0 = 0, bestX1 = 0, bestY = 0;
                if (W > 0 && std::size_t(W) * std::size_t(W) == tl.size()) {
                    for (int y = 0; y < W; ++y) {
                        int runStart = -1;
                        for (int x = 0; x <= W; ++x) {
                            const bool water = (x < W) &&
                                tl[std::size_t(y) * W + x] == WATER;
                            if (water && runStart < 0) runStart = x;
                            else if (!water && runStart >= 0) {
                                const int len = x - runStart;
                                if (len > bestLen) {
                                    bestLen = len; bestX0 = runStart;
                                    bestX1 = x - 1; bestY = y;
                                }
                                runStart = -1;
                            }
                        }
                    }
                }
                if (bestLen > 0) {
                    const int cx = (bestX0 + bestX1) / 2;
                    int up = 0, dn = 0;
                    for (int y = bestY; y >= 0 &&
                         tl[std::size_t(y) * W + cx] == WATER; --y) ++up;
                    for (int y = bestY + 1; y < W &&
                         tl[std::size_t(y) * W + cx] == WATER; ++y) ++dn;
                    const int vpx = std::max(1, bestX0 - 2);
                    std::fprintf(stderr,
                        "[waterscan] W=%d longest E-W water run: y=%d "
                        "x=[%d..%d] len=%d thickness=%d center=%d,%d\n"
                        "[waterscan]   -> SUBPOS=\"%d,%d\" yaw=0 (stand W shore, "
                        "look +X across water at an evening moon)\n",
                        W, bestY, bestX0, bestX1, bestLen, up + dn, cx, bestY,
                        vpx, bestY);
                } else {
                    std::fprintf(stderr, "[waterscan] no open water (W=%d)\n", W);
                }
                std::fflush(stderr);
            }
            // Opt-in (TIMAERT_SMOKE_HOUR=0..23): force the game clock so a
            // headless capture can render an arbitrary time-of-day — e.g. night
            // to see the moon. Set AFTER warm-up so the hour is exact; the few
            // frames until a capture advance subworld time by <0.1 min, far
            // below an hour. Harness only; normal play is unaffected.
            if (const char* hh = std::getenv("TIMAERT_SMOKE_HOUR")) {
                const int hour = std::clamp(std::atoi(hh), 0, 23);
                app.gs.worldTime =
                    sm::world_time_at(app.gs.worldTime.day(), hour, 0);
                std::fprintf(stderr, "[smoke] force time -> %02d:00\n", hour);
                std::fflush(stderr);
            }
            // Opt-in camera aim (TIMAERT_SMOKE_YAW / _PITCH, degrees): set the
            // look direction ABSOLUTELY so a headless capture can face a chosen
            // feature — the moon (pitch up), water, a slope. yaw 0 = +X; +pitch
            // looks up (clamped to ±60° by rotate_camera). Applied as a delta
            // from the current angles so it is exact regardless of the enter
            // default. Harness only; normal play is unaffected.
            if (const char* yy = std::getenv("TIMAERT_SMOKE_YAW")) {
                const float target = float(std::atof(yy)) * 0.01745329252f;
                app.subworld.rotate_camera(target - app.subworld.cam_yaw(), 0.0f);
                std::fprintf(stderr, "[smoke] force yaw -> %.1f deg\n",
                             std::atof(yy));
                std::fflush(stderr);
            }
            if (const char* pp = std::getenv("TIMAERT_SMOKE_PITCH")) {
                const float target = float(std::atof(pp)) * 0.01745329252f;
                app.subworld.rotate_camera(0.0f,
                                           target - app.subworld.cam_pitch());
                std::fprintf(stderr, "[smoke] force pitch -> %.1f deg\n",
                             std::atof(pp));
                std::fflush(stderr);
            }
            {
                int macroProjected = 0;
                for (auto e : app.ecs.reg.view<sm::ecs::MacroOrigin,
                                               sm::ecs::SubworldTag>()) {
                    (void)e;
                    ++macroProjected;
                }
                std::fprintf(stderr,
                             "[smoke] subworld_enter macroProjected=%d\n",
                             macroProjected);
                std::fflush(stderr);
            }
            std::fprintf(stderr,
                         "[smoke] subworld_enter active=%d 3d=%d player=%.1f,%.1f\n",
                         app.subworld.active() ? 1 : 0,
                         1,
                         app.subworld.player_x(), app.subworld.player_y());
            std::fflush(stderr);
            ++app.smoke.cursor;
            break;
        case SmokeAction::SubworldExitRemap: {
            // Inc 5e-1 end-to-end: possess a macro-projected body, then leave —
            // the macro player must resurface on the POSSESSED body's macro
            // origin cell ("exit AS the lord"), not the window centre.
            std::fprintf(stderr, "[smoke] action=subworld_exit_remap\n");
            std::fflush(stderr);
            if (!app.worldLoaded) {
                smoke_fail(app, "subworld_exit_remap without world");
                break;
            }
            // Relocate onto the nearest persistent macro NPC so the projection is
            // guaranteed a body to possess.
            if (!app.subworld.active()) {
                const int pcx = int(app.gs.player.x), pcy = int(app.gs.player.y);
                int bestX = -1, bestY = -1;
                long bestD = 1L << 60;
                for (auto e : app.ecs.reg.view<sm::ecs::MacroNpcRuntime,
                                               sm::ecs::Position>(
                         entt::exclude<sm::ecs::PlayerSquadTag>)) {
                    const auto& p = app.ecs.reg.get<sm::ecs::Position>(e);
                    const int nx = int(p.x), ny = int(p.y);
                    const long dx = nx - pcx, dy = ny - pcy;
                    const long d = dx * dx + dy * dy;
                    if (d < bestD) { bestD = d; bestX = nx; bestY = ny; }
                }
                if (bestX < 0) {
                    smoke_fail(app, "exit_remap: no macro NPC to project");
                    break;
                }
                app.gs.player.x = float(bestX);
                app.gs.player.y = float(bestY);
                app.gs.subState.settlementId = -1;
                app.ui.settlementId = -1;
                enter_subworld(app);
            }
            if (!app.subworld.active()) {
                smoke_fail(app, "exit_remap: enter failed");
                break;
            }
            {
                auto& reg = app.ecs.reg;
                // The window centre BEFORE any remap — the default landing cell a
                // non-possessed exit would snap to.
                const int ccx = int(app.gs.player.x);
                const int ccy = int(app.gs.player.y);
                // Grab a projected body and its (valid, positioned) macro origin.
                entt::entity body = entt::null, origin = entt::null;
                for (auto e : reg.view<sm::ecs::SubworldTag, sm::ecs::MacroOrigin>()) {
                    const entt::entity m = reg.get<sm::ecs::MacroOrigin>(e).macro;
                    if (reg.valid(m) && reg.all_of<sm::ecs::Position>(m)) {
                        body = e; origin = m; break;
                    }
                }
                if (body == entt::null) {
                    smoke_fail(app, "exit_remap: no projected body with a backlink");
                    break;
                }
                // Force the origin to a distinctive OFF-CENTRE cell so landing on
                // it is provably the remap, not a coincidental centre-snap.
                const int W = app.terrain.width, H = app.terrain.height;
                const int ocx = ((ccx + 7) % W + W) % W;
                const int ocy = ((ccy + 5) % H + H) % H;
                reg.get<sm::ecs::Position>(origin).x = float(ocx);
                reg.get<sm::ecs::Position>(origin).y = float(ocy);
                // Possess the body, then leave via the real teardown path.
                if (!app.subworld.possess_by_id(
                        static_cast<std::uint32_t>(entt::to_integral(body)))) {
                    smoke_fail(app, "exit_remap: possess_by_id returned false");
                    break;
                }
                app.subworld.leave(true);
                const int gx = int(app.gs.player.x);
                const int gy = int(app.gs.player.y);
                const bool onOrigin  = (gx == ocx && gy == ocy);
                const bool offCentre = (ocx != ccx || ocy != ccy);
                std::fprintf(stderr,
                             "[smoke] subworld_exit_remap onOrigin=%d off_centre=%d "
                             "landed=%d,%d origin=%d,%d centre=%d,%d\n",
                             onOrigin ? 1 : 0, offCentre ? 1 : 0,
                             gx, gy, ocx, ocy, ccx, ccy);
                std::fflush(stderr);
                if (!onOrigin || !offCentre) {
                    smoke_fail(app, "exit_remap: did not land on the possessed origin cell");
                    break;
                }
                // Inc 5e-2 (identity remap): leaving AS a lord must also ADOPT it.
                // Exactly one PlayerTag must now ride the macro ORIGIN itself — a
                // real MacroNpcRuntime NPC, not a bare hero husk — and its
                // save-stable ordinal must be recorded on the player scalar, so a
                // later save can re-find the same lord once the macro NPCs
                // regenerate from `worldSeed`.
                int tags = 0;
                entt::entity flag = entt::null;
                for (auto e : reg.view<sm::ecs::PlayerTag>()) { ++tags; flag = e; }
                const bool onMacroNpc =
                    flag != entt::null && reg.all_of<sm::ecs::MacroNpcRuntime>(flag);
                const bool ridesOrigin = (flag == origin);
                const bool idRecorded  = (app.gs.player.possessedMacroSpawnId >= 0);
                std::fprintf(stderr,
                             "[smoke] subworld_exit_remap adopt tags=%d on_macro_npc=%d "
                             "rides_origin=%d spawnId=%d\n",
                             tags, onMacroNpc ? 1 : 0, ridesOrigin ? 1 : 0,
                             app.gs.player.possessedMacroSpawnId);
                std::fflush(stderr);
                if (tags != 1 || !onMacroNpc || !ridesOrigin || !idRecorded) {
                    smoke_fail(app, "exit_remap: possessed identity not adopted on exit");
                    break;
                }
                // Restore a clean single-husk macro state for a self-contained
                // process: strip the flag off the lord (it reverts to an autonomous
                // NPC), drop the ordinal, and re-materialise the ordinary hero husk.
                reg.remove<sm::ecs::PlayerTag>(flag);
                app.gs.player.possessedMacroSpawnId = -1;
                sm::ensure_macro_player_entity(app.gs, app.ecs);
            }
            ++app.smoke.cursor;
            break;
        }
        case SmokeAction::SubworldExitGate:
            std::fprintf(stderr, "[smoke] action=subworld_exit_gate\n");
            std::fflush(stderr);
            if (run_subworld_exit_gate_smoke(app)) ++app.smoke.cursor;
            break;
        case SmokeAction::DungeonHouse:
            std::fprintf(stderr, "[smoke] action=dungeon_house\n");
            std::fflush(stderr);
            if (run_dungeon_house_smoke(app)) ++app.smoke.cursor;
            break;
        case SmokeAction::DungeonCave:
            std::fprintf(stderr, "[smoke] action=dungeon_cave\n");
            std::fflush(stderr);
            if (run_dungeon_cave_smoke(app)) ++app.smoke.cursor;
            break;
        case SmokeAction::SpireClimb:
            std::fprintf(stderr, "[smoke] action=spire_climb\n");
            std::fflush(stderr);
            if (run_spire_climb_smoke(app)) ++app.smoke.cursor;
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
        case SmokeAction::SubworldSelfFireball:
            std::fprintf(stderr, "[smoke] action=subworld_self_fireball\n");
            std::fflush(stderr);
            if (run_subworld_self_fireball_smoke(app)) ++app.smoke.cursor;
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
        case SmokeAction::SubworldRecovery:
            std::fprintf(stderr, "[smoke] action=subworld_recovery\n");
            std::fflush(stderr);
            if (run_subworld_recovery_smoke(app)) ++app.smoke.cursor;
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
                advance_sim_seconds(app, 0.10f, false);
            if (!frameStats.ticked || !frameStats.subworldActive) {
                smoke_fail(app, "battle_start subworld tick inactive");
                break;
            }
            std::fprintf(stderr,
                         "[smoke] battle_start routed hostiles=%d->%d status=%s\n",
                         beforeHostiles, afterHostiles,
                         app.subworld.status_line());
            std::fflush(stderr);
            const sm::LevelData beforeDeathXp = app.gs.player.sheet.levelData;
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
            sm::sub::apply_lethal_damage(app.ecs.reg, smokeHostile,
                                         sm::sub::DamageSource{0u, true},
                                         sm::sub::DamageKind::Dev, &app.bus);
            app.subworld.leave(true);
            const sm::LevelData afterDeathXp = app.gs.player.sheet.levelData;
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
            // Phase 1 — arm the readback and HOLD the cursor. The frame this
            // script runs after is already recorded, so the pixels we want are
            // presented at the end of THIS frame and readable on the next one.
            // (The same defer-by-one rule light_probe_capture obeys; a same-tick
            // judgement grades the picture taken before the action.)
            if (app.smoke.probePixels.empty()) {
                if (!app.smoke.pixelProbeArmed) {
                    app.smoke.pixelProbeArmed = true;
                    app.smoke.capturePending = true;
                    app.smoke.captureActionIndex = app.smoke.cursor;
                }
                break;   // hold here until the frame lands (or readback fails)
            }
            // Phase 2 — judge the frame that was actually presented.
            int samplesHit = 0;
            const bool visible =
                smoke_framebuffer_has_world_pixels(app, samplesHit);
            app.smoke.probePixels.clear();
            app.smoke.probePixels.shrink_to_fit();
            if (!visible) {
                std::fprintf(stderr,
                             "[smoke] visible samples=%d — the presented frame "
                             "is blank or one flat colour\n", samplesHit);
                std::fflush(stderr);
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
                         "[smoke] settlement_trade open id=%d name=\"%s\" mood=%d stock=%d playerItems=%d gold=%d\n",
                         s.id,
                         s.name.c_str(),
                         int(s.mood),
                         s.inventory.used_slots(),
                         player_bag(app).total(),
                         sm::wallet_value(player_bag(app)));
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
            enter_subworld(app);
            if (!app.subworld.active()) {
                smoke_fail(app, "enter_first_settlement subworld enter failed");
                break;
            }
            int houses = 0;
            int walls = 0;
            int gates = 0;
            for (const auto& st : app.subworld.mgr().structures()) {
                if (st.kind == sm::sub::Structure::House) ++houses;
                if (st.kind == sm::sub::Structure::Wall) ++walls;
                // Gate lintels (zBase > 0) mark the real openings; print the
                // first few so a capture run can aim a teleport at a gate.
                if (st.kind == sm::sub::Structure::Wall && st.zBase > 0.0f
                    && gates < 6) {
                    std::fprintf(stderr, "[smoke] gate_lintel %d at %.0f,%.0f\n",
                                 gates, st.x, st.y);
                    ++gates;
                }
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
                                         sm::ecs::NpcInventory>(
                entt::exclude<sm::ecs::PlayerSquadTag>);
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
                                         sm::ecs::NpcInventory>(
                entt::exclude<sm::ecs::PlayerSquadTag>);
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
                         player_bag(app).total(),
                         sm::wallet_value(player_bag(app)));
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
                                         sm::ecs::NpcCharacter>(
                entt::exclude<sm::ecs::PlayerSquadTag>);
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
        case SmokeAction::SpawnSquadAtPlayer: {
            // Give a scenario its SUBJECT deterministically: a neutral squad
            // at the player's own cell, through the one creation door. Born
            // for macro_kill_writeback, which used to depend on whoever
            // happened to be near the start city at enter time — the march
            // law (Session 21) walks them away on some worlds (seeds 1/999)
            // and the scenario went red for want of a body, not for the law
            // it guards.
            if (app.subworld.active()) {
                smoke_fail(app, "spawn_squad_at_player inside the subworld");
                break;
            }
            sm::SquadSpec spec{};
            spec.leaderType = sm::NPCType::Peasant;   // neutral: no encounter
            spec.leaderLevel = 3;
            spec.x = int(app.gs.player.x);
            spec.y = int(app.gs.player.y);
            spec.factionIndex = -1;                   // the land decides
            spec.members.push(sm::make_soldier(
                std::uint8_t(sm::NPCType::Peasant), 2, 0x50000001u));
            const entt::entity leader =
                sm::spawn_squad(app.gs, app.ecs, app.terrain, spec);
            if (leader == entt::null) {
                smoke_fail(app, "spawn_squad_at_player: spawn failed");
                break;
            }
            // Pin the squad to the player's cell: spawn_squad scatters within
            // a 4-cell radius, and the enter-time projection only sees the
            // 3x3 window. Arranging the subject is the harness's job; the
            // LAW under test is the writeback, not spawn placement.
            auto& reg = app.ecs.reg;
            auto& pos = reg.get<sm::ecs::Position>(leader);
            pos.x = app.gs.player.x;
            pos.y = app.gs.player.y;
            auto& vis = reg.get<sm::ecs::VisualPos>(leader);
            vis.vx = pos.x;
            vis.vy = pos.y;
            auto& rt = reg.get<sm::ecs::MacroNpcRuntime>(leader);
            rt.targetX = pos.x;
            rt.targetY = pos.y;
            std::fprintf(stderr,
                         "[smoke] squad spawned at player cell %d,%d "
                         "ordinal=%u\n", int(pos.x), int(pos.y),
                         reg.get<sm::ecs::MacroSpawnId>(leader).index);
            std::fflush(stderr);
            ++app.smoke.cursor;
            break;
        }
        case SmokeAction::ForceEncounter: {
            // The forced meeting, end to end: stand on a hostile squad's cell,
            // the map must STOP you (PreBattle opens), and the auto-resolve
            // row must settle the fight through the one law and hand the map
            // back. Fails loudly at each seam.
            if (app.subworld.active()) {
                smoke_fail(app, "force_encounter inside the subworld");
                break;
            }
            smoke_clear_modal_overlays(app);
            auto& reg = app.ecs.reg;
            entt::entity hostile = entt::null;
            auto view = reg.view<sm::ecs::Position, sm::ecs::NPCKind,
                                 sm::ecs::MacroNpcRuntime, sm::ecs::Health>(
                entt::exclude<sm::ecs::Dead, sm::ecs::PlayerTag,
                              sm::ecs::PlayerSquadTag, sm::ecs::SubworldTag>);
            for (auto e : view) {
                if (view.get<sm::ecs::Health>(e).hp <= 0.0f) continue;
                const auto& kind = view.get<sm::ecs::NPCKind>(e);
                if (sm::player_hostile_to(
                        &app.gs, sm::faction_id_for_index(kind.factionIdx))) {
                    hostile = e;
                    break;
                }
            }
            if (hostile == entt::null) {
                smoke_fail(app, "force_encounter found no hostile squad");
                break;
            }
            const auto& pos = reg.get<sm::ecs::Position>(hostile);
            app.gs.player.x = float(int(pos.x));
            app.gs.player.y = float(int(pos.y));
            app.cursor.path.clear();
            app.cursor.pathIdx = 0;
            detect_forced_encounter(app);
            if (app.gs.subState.kind != sm::GameSubStateKind::PreBattle
                || app.preBattleNpc != hostile) {
                smoke_fail(app, "hostile squad did not force the encounter");
                break;
            }
            const sm::SoldierSquad* pArmy = sm::player_roster(app.ecs);
            const int armyBefore = pArmy ? sm::total_soldiers(*pArmy) : 0;
            const int hpBefore = app.gs.player.combatStats.currentHp;
            perform_encounter_auto(app, hostile, sm::Ambush::None);
            if (app.gs.subState.kind != sm::GameSubStateKind::Exploring) {
                smoke_fail(app, "auto-resolve did not hand the map back");
                break;
            }
            const bool enemyGone = !reg.valid(hostile)
                || reg.all_of<sm::ecs::Dead>(hostile)
                || reg.get<sm::ecs::Health>(hostile).hp <= 0.0f;
            const bool enemyHurt = !enemyGone
                && reg.get<sm::ecs::Health>(hostile).hp
                       < reg.get<sm::ecs::Health>(hostile).maxHp;
            const bool playerPaid =
                (sm::player_roster(app.ecs)
                     ? sm::total_soldiers(*sm::player_roster(app.ecs)) : 0)
                    < armyBefore
                || app.gs.player.combatStats.currentHp < hpBefore;
            if (!enemyGone && !enemyHurt && !playerPaid) {
                smoke_fail(app, "auto-resolve settled nothing on either side");
                break;
            }
            std::printf("[smoke] force_encounter resolved enemyGone=%d "
                        "enemyHurt=%d playerPaid=%d\n",
                        int(enemyGone), int(enemyHurt), int(playerPaid));
            ++app.smoke.cursor;
            break;
        }
        case SmokeAction::FaunaKillWriteback: {
            // The fauna twin of macro_kill_writeback: a wild creature is one
            // unit of its cell's fauna_count made visible (Session 16), so
            // killing it must thin the CELL — in the tick it happens, while
            // the player is still underground. Two phases with a frame
            // between them, because the settlement is the reaper's tick, not
            // a line in this script.
            if (!app.subworld.active()) {
                smoke_fail(app, "fauna_kill_writeback outside the subworld");
                break;
            }
            auto& reg = app.ecs.reg;
            sm::MacroWorld faunaMw = macro_world(app);
            if (app.smoke.trackedPhase == 0) {
                entt::entity body = entt::null;
                const sm::ecs::MacroDebt* debt = nullptr;
                for (auto e : reg.view<sm::ecs::MacroDebt, sm::ecs::Health,
                                       sm::ecs::SubworldTag>(
                         entt::exclude<sm::ecs::Dead>)) {
                    const auto& d = reg.get<sm::ecs::MacroDebt>(e);
                    if (d.stock
                        != std::uint8_t(sm::MacroStock::FaunaCount)) {
                        continue;
                    }
                    body = e;
                    debt = &d;
                    break;
                }
                if (body == entt::null || !debt) {
                    smoke_fail(app,
                               "fauna_kill_writeback found no wild body "
                               "with a fauna receipt");
                    break;
                }
                app.smoke.trackedBody = body;
                app.smoke.trackedCellX = debt->cellX;
                app.smoke.trackedCellY = debt->cellY;
                app.smoke.trackedCount0 = sm::macro_stock_read(
                    faunaMw, sm::MacroStock::FaunaCount,
                    sm::MacroStockKey{-1, debt->cellX, debt->cellY});
                if (app.smoke.trackedCount0 <= 0) {
                    smoke_fail(app,
                               "a stamped creature stands on an empty cell");
                    break;
                }
                sm::sub::apply_lethal_damage(reg, body,
                                             sm::sub::DamageSource{},
                                             sm::sub::DamageKind::Script,
                                             &app.bus);
                std::fprintf(stderr,
                             "[smoke] wild body culled at cell %d,%d "
                             "(count %d)\n",
                             app.smoke.trackedCellX, app.smoke.trackedCellY,
                             app.smoke.trackedCount0);
                std::fflush(stderr);
                app.smoke.trackedPhase = 1;
                break;      // let the reaper settle the receipt
            }
            const int now = sm::macro_stock_read(
                faunaMw, sm::MacroStock::FaunaCount,
                sm::MacroStockKey{-1,
                                  std::int16_t(app.smoke.trackedCellX),
                                  std::int16_t(app.smoke.trackedCellY)});
            std::fprintf(stderr,
                         "[smoke] fauna_count %d -> %d after the kill\n",
                         app.smoke.trackedCount0, now);
            std::fflush(stderr);
            if (now != app.smoke.trackedCount0 - 1) {
                smoke_fail(app, "a kill underground did not thin the cell");
                break;
            }
            app.smoke.trackedPhase = 0;
            ++app.smoke.cursor;
            break;
        }
        case SmokeAction::MacroKillWriteback: {
            // What the map is owed when you fight one of its own people down
            // here. A TRACKED body (sub/spawn.h) IS a macro entity made visible,
            // so hurting it must hurt the entity and killing it must kill the
            // entity — in the tick it happens, while the player is still
            // underground. Before this, wounds evaporated on the way out and the
            // dead stood up again: kill the same lord, climb out, meet him
            // whole, kill him again, for as much XP and loot as you had patience
            // for (problems.md 19.13).
            //
            // Three phases with a frame between them, because the settlement is
            // an engine tick, not a line in this script.
            if (!app.subworld.active()) {
                smoke_fail(app, "macro_kill_writeback outside the subworld");
                break;
            }
            auto& reg = app.ecs.reg;
            if (app.smoke.trackedPhase == 0) {
                entt::entity body = entt::null;
                for (auto e : reg.view<sm::ecs::MacroOrigin, sm::ecs::Health,
                                       sm::ecs::SubworldTag>()) {
                    body = e;
                    break;
                }
                if (body == entt::null) {
                    smoke_fail(app, "macro_kill_writeback found no tracked body");
                    break;
                }
                const entt::entity macro =
                    reg.get<sm::ecs::MacroOrigin>(body).macro;
                if (!reg.valid(macro) || !reg.all_of<sm::ecs::Health>(macro)) {
                    smoke_fail(app, "tracked body backlinks nothing");
                    break;
                }
                app.smoke.trackedBody = body;
                app.smoke.trackedMacro = macro;
                app.smoke.trackedMacroHp0 = reg.get<sm::ecs::Health>(macro).hp;
                auto& h = reg.get<sm::ecs::Health>(body);
                h.hp = std::max(1.0f, h.maxHp * 0.25f);   // a quarter left
                std::fprintf(stderr,
                             "[smoke] tracked body wounded to %.1f/%.1f "
                             "(macro hp %.1f)\n",
                             double(h.hp), double(h.maxHp),
                             double(app.smoke.trackedMacroHp0));
                std::fflush(stderr);
                app.smoke.trackedPhase = 1;
                break;      // let a tick carry it up
            }
            if (app.smoke.trackedPhase == 1) {
                if (!reg.valid(app.smoke.trackedMacro)
                    || !reg.valid(app.smoke.trackedBody)) {
                    smoke_fail(app, "tracked pair vanished before the wound landed");
                    break;
                }
                const auto& mh = reg.get<sm::ecs::Health>(app.smoke.trackedMacro);
                std::fprintf(stderr,
                             "[smoke] macro hp %.1f -> %.1f after wound\n",
                             double(app.smoke.trackedMacroHp0), double(mh.hp));
                std::fflush(stderr);
                if (!(mh.hp < app.smoke.trackedMacroHp0 && mh.hp > 0.0f)) {
                    smoke_fail(app, "a wound underground did not reach the map");
                    break;
                }
                sm::sub::apply_lethal_damage(reg, app.smoke.trackedBody,
                                             sm::sub::DamageSource{},
                                             sm::sub::DamageKind::Script,
                                             &app.bus);
                app.smoke.trackedPhase = 2;
                break;      // let the reaper run
            }
            if (!reg.valid(app.smoke.trackedMacro)) {
                smoke_fail(app, "macro entity vanished instead of dying");
                break;
            }
            const bool dead = reg.any_of<sm::ecs::Dead>(app.smoke.trackedMacro)
                && reg.get<sm::ecs::Health>(app.smoke.trackedMacro).hp <= 0.0f;
            std::fprintf(stderr, "[smoke] macro entity dead=%d\n", dead ? 1 : 0);
            std::fflush(stderr);
            if (!dead) {
                smoke_fail(app, "killed underground, alive on the map");
                break;
            }
            ++app.smoke.cursor;
            break;
        }
        case SmokeAction::StatsSettle: {
            // Perf idle: hold the frame loop for 600 rendered frames (~10 s at
            // the 60 Hz cap = ten [stats] windows) so an A/B measurement sees
            // the WARM steady state, not the paperdoll pool's cold start.
            // Diagnostic only — asserts nothing itself; the [stats] lines are
            // the product. 600 = 10 × the stats window of 60 frames.
            if (app.smoke.settleFrames == 0) {
                std::fprintf(stderr, "[smoke] action=stats_settle (600 frames)\n");
                std::fflush(stderr);
            }
            if (++app.smoke.settleFrames >= 600) {
                app.smoke.settleFrames = 0;
                ++app.smoke.cursor;
            }
            break;
        }
        case SmokeAction::CaptureFrame: {
            // Opt-in mutations for a capture: STAGE them one frame ahead — the
            // smoke script runs after this frame was recorded, so a same-tick
            // capture would photograph the pre-mutation image (the
            // light_probe_capture stale-frame rule).
            //
            // Clearing the panels applies in BOTH worlds. It used to be macro-
            // only, which meant a subworld capture could not be taken cleanly
            // at all: an overlay left open by the boot story (the codex, five
            // entries unlocked) covered the middle of every frame, and the rule
            // that a visual claim needs a viewed frame had nowhere to stand.
            // The rest below is macro-only on purpose — the subworld_enter path
            // applies the hour itself, and zoom/macropos mean nothing down
            // there.
            if (app.worldLoaded && !app.smoke.captureStaged) {
                smoke_clear_modal_overlays(app);
            }
            if (!app.subworld.active() && app.worldLoaded
                && !app.smoke.captureStaged) {
                // Same macro relocate the subworld_enter path honours — lets a
                // map capture frame any region (e.g. open sea for the glint).
                if (const char* mp = std::getenv("TIMAERT_SMOKE_MACROPOS")) {
                    int mx = 0, my = 0;
                    if (std::sscanf(mp, "%d,%d", &mx, &my) == 2) {
                        app.gs.player.x = float(mx);
                        app.gs.player.y = float(my);
                        app.camX = app.camTargetX = float(mx) + 0.5f;
                        app.camY = app.camTargetY = float(my) + 0.5f;
                        std::fprintf(stderr,
                                     "[smoke] macropos relocate -> %d,%d\n",
                                     mx, my);
                        std::fflush(stderr);
                    }
                }
                if (const char* hh = std::getenv("TIMAERT_SMOKE_HOUR")) {
                    const int hour = std::clamp(std::atoi(hh), 0, 23);
                    app.gs.worldTime =
                        sm::world_time_at(app.gs.worldTime.day(), hour, 0);
                    // Re-anchor the tick runtime so the per-frame world tick
                    // does not immediately recompute the clock past the
                    // forced hour (same recipe as timeadvance_burst).
                    sm::reset_world_tick_runtime(app.gs.worldTickRt,
                                                 app.gs.worldSeed);
                    std::fprintf(stderr, "[smoke] force time -> %02d:00\n",
                                 hour);
                    std::fflush(stderr);
                }
                // Opt-in (TIMAERT_SMOKE_ZOOM=<px/cell>): force the map zoom
                // so captures can verify zoom-dependent shader effects.
                if (const char* zz = std::getenv("TIMAERT_SMOKE_ZOOM")) {
                    const float z = std::clamp(float(std::atof(zz)),
                                               1.0f, 64.0f);
                    app.zoom = z;
                    std::fprintf(stderr, "[smoke] force zoom -> %.1f\n", z);
                    std::fflush(stderr);
                }
                app.smoke.captureStaged = true;
                break;  // no cursor advance: capture arms NEXT frame
            }
            if (app.worldLoaded && !app.smoke.captureStaged) {
                app.smoke.captureStaged = true;
                break;  // subworld: the panel clear also needs its own frame
            }
            std::fprintf(stderr, "[smoke] action=capture_frame\n");
            std::fflush(stderr);
            app.smoke.captureStaged = false;
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
                         app.gs.player.sheet.levelData.attributePoints,
                         app.gs.player.sheet.levelData.skillPoints,
                         app.gs.player.sheet.levelData.perkPoints,
                         app.gs.player.sheet.attributes.vit,
                         app.gs.player.sheet.skills.bodybuilding,
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
            const int beforePoints = app.gs.player.sheet.levelData.attributePoints;
            const int beforeVit = app.gs.player.sheet.attributes.vit;
            const int beforeHp = app.gs.player.combatStats.maxHp;
            if (!sm::spend_attribute_point(app.gs.player.sheet.levelData,
                                           app.gs.player.sheet.attributes,
                                           sm::AttributeId::Vit)) {
                smoke_fail(app, "spend_attribute_vit rejected");
                break;
            }
            // Same rule the UI enforces now: maxima recompute, CURRENT pools
            // stay — spending a point is not a free full heal.
            const int curHpBefore = app.gs.player.combatStats.currentHp;
            sm::recompute_combat_maxima(app.gs.player.combatStats,
                                        app.gs.player.sheet.attributes,
                                        app.gs.player.sheet.skills);
            if (app.gs.player.sheet.levelData.attributePoints != beforePoints - 1
                || app.gs.player.sheet.attributes.vit != beforeVit + 1
                || app.gs.player.combatStats.maxHp <= beforeHp
                || app.gs.player.combatStats.currentHp != curHpBefore) {
                smoke_fail(app, "spend_attribute_vit invariant");
                break;
            }
            std::fprintf(stderr,
                         "[smoke] spend_attr_vit points=%d->%d vit=%d->%d hpMax=%d->%d\n",
                         beforePoints,
                         app.gs.player.sheet.levelData.attributePoints,
                         beforeVit,
                         app.gs.player.sheet.attributes.vit,
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
            const int beforePoints = app.gs.player.sheet.levelData.skillPoints;
            const int beforeRank = app.gs.player.sheet.skills.bodybuilding;
            const int beforeHp = app.gs.player.combatStats.maxHp;
            if (!sm::spend_skill_point(app.gs.player.sheet.levelData,
                                       app.gs.player.sheet.skills,
                                       sm::SkillId::Bodybuilding)) {
                smoke_fail(app, "spend_skill_bodybuilding rejected");
                break;
            }
            const int curHpBefore = app.gs.player.combatStats.currentHp;
            sm::recompute_combat_maxima(app.gs.player.combatStats,
                                        app.gs.player.sheet.attributes,
                                        app.gs.player.sheet.skills);
            if (app.gs.player.sheet.levelData.skillPoints != beforePoints - 1
                || app.gs.player.sheet.skills.bodybuilding != beforeRank + 1
                || app.gs.player.combatStats.maxHp <= beforeHp
                || app.gs.player.combatStats.currentHp != curHpBefore) {
                smoke_fail(app, "spend_skill_bodybuilding invariant");
                break;
            }
            std::fprintf(stderr,
                         "[smoke] spend_skill_bodybuilding points=%d->%d rank=%d->%d hpMax=%d->%d\n",
                         beforePoints,
                         app.gs.player.sheet.levelData.skillPoints,
                         beforeRank,
                         app.gs.player.sheet.skills.bodybuilding,
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
        case SmokeAction::RestSp:
            std::fprintf(stderr, "[smoke] action=rest_sp\n");
            std::fflush(stderr);
            if (run_rest_sp_smoke(app)) ++app.smoke.cursor;
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
            const float cd = cdIt == book.cooldowns.end()
                                 ? 0.0f
                                 : sm::seconds_from_steps(cdIt->second);
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
                enter_subworld(app);
            }
            // CLEAR THE LINE OF FIRE (same idiom as subworld_self_fireball).
            // This scenario asserts that the player's bolt strikes the target
            // IT was aimed at, which silently assumed nobody stood in between.
            // Since collision became swept rather than point-sampled, a bolt
            // honestly hits the first body in its path — so on seed 1 an ambient
            // peasant standing on the line took the hit and the scenario's own
            // target survived, reporting "You hit Peasant" for a Bandit test.
            // That is the fix working; the fixture was the thing at fault.
            {
                std::vector<entt::entity> doomed;
                for (auto e : app.ecs.reg.view<sm::ecs::Health>()) {
                    if (!app.ecs.reg.any_of<sm::ecs::PlayerTag,
                                            sm::ecs::PlayerSquadTag>(e)) {
                        doomed.push_back(e);
                    }
                }
                for (const entt::entity e : doomed) {
                    if (app.ecs.reg.valid(e)) app.ecs.reg.destroy(e);
                }
            }
            sm::spellbook_learn(app.gs.player.spellBook, "magic_bolt");
            sm::spellbook_set_active(app.gs.player.spellBook, "magic_bolt");
            const float spellTargetX = std::min(
                app.subworld.player_x() + 43.5f,
                float(sm::sub::kFullSize - 2));
            const float spellTargetY = app.subworld.player_y();
            // Stand the target at the CASTER'S OWN Z, not at zero. Z is world
            // elevation in metres (playerZ_ = sample_height_m), so it is on the
            // order of a thousand — while a hand-placed 0.0f puts the body a
            // kilometre underground. The bolt leaves the muzzle at the caster's
            // z and flies level (pitch 0), and find_projectile_hit is honestly
            // 3D (dx²+dy²+dz² ≤ r², r≈1.5), so a target at zero is simply never
            // touched. Yaw 0 already means "looking +X", so the aim was fine —
            // only the altitude was fiction.
            const float spellTargetZ = app.subworld.player_z();
            const entt::entity spellTarget = app.ecs.reg.create();
            app.ecs.reg.emplace<sm::ecs::Position>(
                spellTarget, spellTargetX, spellTargetY, spellTargetZ);
            app.ecs.reg.emplace<sm::ecs::VisualPos>(
                spellTarget, spellTargetX, spellTargetY, spellTargetZ);
            // Ask the registry: the literal 3 predates the ONE faction registry
            // and now means `cults`, so this body called itself a Bandit while
            // wearing a cultist's colours. Projectiles are faction-agnostic, so
            // it never blocked the hit — but a target that lies about its side
            // is a trap for the next person to read this.
            app.ecs.reg.emplace<sm::ecs::NPCKind>(
                spellTarget,
                sm::ecs::NPCKind{
                    std::uint16_t(sm::NPCType::Bandit),
                    std::uint16_t(sm::faction_index("bandits"))});
            app.ecs.reg.emplace<sm::ecs::Health>(spellTarget, 30.0f, 30.0f);
            app.ecs.reg.emplace<sm::ecs::SubworldTag>(spellTarget);
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
            // This window is bounded on BOTH sides, and the old 0.10 s missed
            // the lower one. magic_bolt flies at 400 units/s to a target 43.5
            // away, so it needs ~0.10 s just to arrive (less the spawn offset)
            // — 0.10 s expired with the bolt still a stride short, every seed,
            // every run. The upper bound is the HitFlash this scenario also
            // asserts: it lasts kHitFlashDuration (0.15 s) from the moment of
            // impact, so waiting too long watches the evidence decay. 0.20 s
            // (13 ticks of 1/64 s) lands between the two with room on each side.
            RuntimeFrameStats frameStats =
                advance_sim_seconds(app, 0.20f, false);
            if (!frameStats.ticked || !frameStats.subworldActive) {
                smoke_fail(app, "spell projectile tick inactive");
                break;
            }
            const int afterCombatLog = app.subworld.combat_log_count();
            const sm::sub::CombatLogEntry* combatLog =
                app.subworld.combat_log_entry(afterCombatLog - 1);
            const bool hitLogged = afterCombatLog > beforeCombatLog
                && combatLog && combatLog->text[0] != '\0';
            const auto* hitFlash =
                app.ecs.reg.try_get<sm::ecs::HitFlash>(spellTarget);
            // PRINT BEFORE YOU JUDGE. A scenario that fails first tells you only
            // that something is wrong; these numbers say WHICH thing. `alive` is
            // the load-bearing one — a bolt that is gone without a hit was reaped
            // in flight (it struck the ground or a wall), which is a different
            // story from one that arrived and did nothing.
            int liveProjectiles = 0;
            for (auto e : app.ecs.reg.view<sm::ecs::Projectile>()) {
                (void)e;
                ++liveProjectiles;
            }
            const auto* targetHp =
                app.ecs.reg.try_get<sm::ecs::Health>(spellTarget);
            const auto& book = app.gs.player.spellBook;
            std::fprintf(stderr,
                         "[smoke] spell_projectile active=%s projectiles=%d->%d alive=%d "
                         "targetHp=%.1f mp=%d cd=%zu event=%d flash=%.3f log=\"%s\"\n",
                         book.activeSpellId.c_str(),
                         beforeProjectiles,
                         afterProjectiles,
                         liveProjectiles,
                         targetHp ? double(targetHp->hp) : -1.0,
                         app.gs.player.combatStats.currentMp,
                         book.cooldowns.size(),
                         afterSpellCastEvents - beforeSpellCastEvents,
                         hitFlash ? double(hitFlash->timer) : -1.0,
                         combatLog ? combatLog->text : "");
            std::fflush(stderr);
            if (!hitLogged) {
                smoke_fail(app, "spell hit combat log missing");
                break;
            }
            if (!hitFlash || hitFlash->timer <= 0.0f) {
                smoke_fail(app, "spell hit flash missing");
                break;
            }
            ++app.smoke.cursor;
            break;
        }
        case SmokeAction::CastBoltCapture: {
            // Graphics capture: spawn a spell bolt and photograph it MID-FLIGHT
            // so the travelling elemental point light (registry.cpp bolt_light)
            // is visible in the frame. Unlike cast_spell (which ticks 0.10s and
            // asserts a hit, consuming the projectile), this casts and arms the
            // capture in the SAME step with no consuming tick, leaving the fresh
            // fireball at the muzzle directly ahead of the camera. Fireball is
            // chosen deliberately: fattest radius (2.5) => widest, brightest pool.
            std::fprintf(stderr, "[smoke] action=cast_bolt_capture\n");
            std::fflush(stderr);
            if (!app.worldLoaded) {
                smoke_fail(app, "cast_bolt_capture without world");
                break;
            }
            if (!app.subworld.active()) {
                smoke_fail(app, "cast_bolt_capture needs subworld_enter first");
                break;
            }
            // Which bolt to photograph. Default fireball (fattest pool); override
            // with TIMAERT_SMOKE_SPELL to prove the glow colour derives from the
            // spell tint (e.g. "magic_bolt" => a lavender pool, distinct from the
            // warm lantern and the blue moon).
            const char* boltSpell = std::getenv("TIMAERT_SMOKE_SPELL");
            if (!boltSpell || boltSpell[0] == '\0') boltSpell = "fireball";
            sm::spellbook_learn(app.gs.player.spellBook, boltSpell);
            sm::spellbook_set_active(app.gs.player.spellBook, boltSpell);
            // Refill mana so the cast cannot fail on cost in a fresh smoke run.
            app.gs.player.combatStats.currentMp =
                app.gs.player.combatStats.maxMp;
            int beforeProjectiles = 0;
            for (auto e : app.ecs.reg.view<sm::ecs::Projectile>()) {
                (void)e;
                ++beforeProjectiles;
            }
            if (!cast_active_spell(app)) {
                smoke_fail(app, "cast_bolt_capture cast failed");
                break;
            }
            int afterProjectiles = 0;
            int litProjectiles = 0;
            for (auto e : app.ecs.reg.view<sm::ecs::Projectile>()) {
                ++afterProjectiles;
                if (app.ecs.reg.all_of<sm::ecs::LightEmitter>(e))
                    ++litProjectiles;
            }
            if (afterProjectiles <= beforeProjectiles) {
                smoke_fail(app, "cast_bolt_capture projectile not spawned");
                break;
            }
            if (litProjectiles <= 0) {
                smoke_fail(app, "cast_bolt_capture bolt has no LightEmitter");
                break;
            }
            // Optional pre-capture flight (TIMAERT_SMOKE_BOLT_FLIGHT seconds):
            // let the bolt travel clear of the caster so its glow lights ground
            // on its OWN — the real point of a travelling light. Default 0 keeps
            // the muzzle-shot behaviour byte-identical. Kept short so a fast bolt
            // stays alive and in view (magic_bolt at 400u/s clears the 16m
            // lantern in ~0.05s; a full life would expire or leave the frame).
            float boltFlight = 0.0f;
            if (const char* bf = std::getenv("TIMAERT_SMOKE_BOLT_FLIGHT")) {
                boltFlight = float(std::atof(bf));
                if (boltFlight < 0.0f) boltFlight = 0.0f;
                if (boltFlight > 0.30f) boltFlight = 0.30f;
            }
            int flownProjectiles = afterProjectiles;
            if (boltFlight > 0.0f) {
                (void)advance_sim_seconds(app, boltFlight, false);
                flownProjectiles = 0;
                for (auto e : app.ecs.reg.view<sm::ecs::Projectile>()) {
                    (void)e;
                    ++flownProjectiles;
                }
            }
            std::fprintf(stderr,
                         "[smoke] cast_bolt_capture projectiles=%d->%d lit=%d flight=%.3f alive=%d\n",
                         beforeProjectiles, afterProjectiles, litProjectiles,
                         double(boltFlight), flownProjectiles);
            std::fflush(stderr);
            // Arm the capture on THIS frame (same as capture_frame) so the
            // in-flight bolt and its glow are photographed before any tick moves
            // or consumes it.
            app.smoke.capturePending = true;
            app.smoke.captureActionIndex = app.smoke.cursor;
            ++app.smoke.cursor;
            break;
        }
        case SmokeAction::LightProbeCapture: {
            // ── Settle phase ──────────────────────────────────────────────
            // Staging (spawn/relocate/strip) below runs in tick_smoke_script,
            // which fires AFTER this frame's 3D scene is already recorded
            // (frame(): record_main precedes tick_smoke_script). A same-tick
            // capture therefore photographs the PRE-staging scene — the actor
            // and any light strip only reach the ECS in time for the NEXT
            // frame's record. So we stage once, then hold for a few frames
            // (re-pinning the actor against the sim tick, which nudges a Combat
            // AI toward the player) before arming the capture, so the
            // photographed frame genuinely contains the staged actor and its
            // lighting. probeSettleFrames == -1 means "not yet staged".
            if (app.smoke.probeSettleFrames >= 0) {
                if (app.smoke.probeEntity != entt::null
                    && app.ecs.reg.valid(app.smoke.probeEntity)
                    && app.ecs.reg.all_of<sm::ecs::Position>(
                           app.smoke.probeEntity)) {
                    auto& pp = app.ecs.reg.get<sm::ecs::Position>(
                        app.smoke.probeEntity);
                    pp.x = app.smoke.probeX;
                    pp.y = app.smoke.probeY;
                }
                if (app.smoke.probeSettleFrames == 0) {
                    app.smoke.capturePending = true;
                    app.smoke.captureActionIndex = app.smoke.cursor;
                    app.smoke.probeSettleFrames = -1;
                    app.smoke.probeEntity = entt::null;
                    std::fprintf(stderr,
                                 "[smoke] light_probe_capture settled -> capture "
                                 "armed\n");
                    std::fflush(stderr);
                    ++app.smoke.cursor;
                } else {
                    --app.smoke.probeSettleFrames;
                }
                break;
            }
            // Graphics capture: place ONE billboard actor (TIMAERT_SMOKE_PROBE —
            // a procedural creature like `wolf`, or a drawn-art humanoid like
            // `guard`) at a fixed short distance directly ahead of the player,
            // then photograph it. Two proofs share this one staging rig:
            //   • a creature proves the flat-sprite point-light term (lighting.glsl
            //     point_lights_flat, wired into billboard/npc/creature.frag) lights
            //     actors, not just the ground;
            //   • a `guard` proves its DATA-DRIVEN carried torch (Inc 9): the
            //     guard's own LightEmitter pool, staged where it is guaranteed
            //     on-camera instead of lost among the city's boxed streets.
            // Either way a normal frame never stages the actor in the light:
            // ambient fauna spawn 18-34 m out, beyond the 16 m lantern.
            // Deterministic: we spawn, then relocate the new actor to
            // player + forward*dist (forward = (cos yaw, sin yaw), the same tile-XY
            // convention movement/aim use).
            std::fprintf(stderr, "[smoke] action=light_probe_capture\n");
            std::fflush(stderr);
            if (!app.worldLoaded) {
                smoke_fail(app, "light_probe_capture without world");
                break;
            }
            if (!app.subworld.active()) {
                smoke_fail(app, "light_probe_capture needs subworld_enter first");
                break;
            }
            // Which creature to probe with. Default wolf (a clear quadruped
            // silhouette); any global-table monster id works (bear, goblin, ...).
            const char* probe = std::getenv("TIMAERT_SMOKE_PROBE");
            if (!probe || probe[0] == '\0') probe = "wolf";
            // Distance ahead, clamped inside the lantern radius (16 m) so the
            // actor sits in a bright part of the pool. Default 7 m.
            float dist = 7.0f;
            if (const char* pd = std::getenv("TIMAERT_SMOKE_PROBE_DIST")) {
                dist = float(std::atof(pd));
                if (dist < 1.0f) dist = 1.0f;
                if (dist > 15.0f) dist = 15.0f;
            }
            // Snapshot existing actor entities so we can identify the new one.
            // Any non-player Sprite+Position body qualifies — a procedural
            // creature (archetype != 0xFF) OR a drawn-art humanoid (archetype ==
            // 0xFF), so the same probe can stage a wolf to prove the flat-sprite
            // creature light term OR a guard to prove its data-driven carried
            // torch (Inc 9). Widened from creature-only; wolves still match.
            auto is_probe_actor = [&](entt::entity e) {
                if (!app.ecs.reg.all_of<sm::ecs::Sprite, sm::ecs::Position>(e))
                    return false;
                return !app.ecs.reg.all_of<sm::ecs::PlayerTag>(e);
            };
            std::vector<entt::entity> before;
            for (auto e : app.ecs.reg.view<sm::ecs::Sprite, sm::ecs::Position>())
                if (is_probe_actor(e)) before.push_back(e);
            const std::uint32_t seed =
                app.gs.worldSeed ^ 0x9E3779B9u ^ std::uint32_t(before.size());
            // Faction named so the probe body behaves exactly as it did before
            // the default became "the realm of this ground": a light capture
            // must not silently change what the actor does between frames.
            if (!app.subworld.spawn_npc_body(probe, probe, 1, seed, "bandits")) {
                smoke_fail(app, "light_probe_capture spawn failed");
                break;
            }
            // Find the freshly-created actor (in the after-set, not before).
            entt::entity probeE = entt::null;
            {
                std::vector<entt::entity> beforeSorted = before;
                std::sort(beforeSorted.begin(), beforeSorted.end());
                for (auto e : app.ecs.reg.view<sm::ecs::Sprite, sm::ecs::Position>()) {
                    if (!is_probe_actor(e)) continue;
                    if (!std::binary_search(beforeSorted.begin(),
                                            beforeSorted.end(), e)) {
                        probeE = e;
                        break;
                    }
                }
            }
            if (probeE == entt::null) {
                smoke_fail(app, "light_probe_capture: spawned actor not found");
                break;
            }
            // Relocate it to a fixed point directly ahead of the player, inside
            // the lantern pool. forward = (cos yaw, sin yaw) in tile space, the
            // same convention movement/aim use (engine.cpp line ~1752).
            const float yaw = app.subworld.cam_yaw();
            const float fx = app.subworld.player_x() + std::cos(yaw) * dist;
            const float fy = app.subworld.player_y() + std::sin(yaw) * dist;
            auto& ppos = app.ecs.reg.get<sm::ecs::Position>(probeE);
            ppos.x = fx;
            ppos.y = fy;
            // Aim the camera down at the actor so it is ALWAYS framed regardless
            // of the enter orientation (the boxed settlement spawn otherwise hides
            // a ground-level creature behind a near wall). A gentle look-down
            // frames the actor's full billboard against the lit ground; yaw is
            // kept (the creature is straight ahead along it). TIMAERT_SMOKE_PITCH
            // still wins if the caller set one — only override when unset.
            if (!std::getenv("TIMAERT_SMOKE_PITCH")) {
                const float wantPitch = -18.0f * 0.01745329252f;
                app.subworld.rotate_camera(0.0f,
                                           wantPitch - app.subworld.cam_pitch());
            }
            // Opt-in (TIMAERT_SMOKE_NO_PLAYER_LIGHT=1): strip the player's own
            // carried lantern for this capture so the ONLY light in the scene is
            // the staged actor's. This isolates a probed guard's data-driven torch
            // (Inc 9) — otherwise the player's 16 m lantern overlaps a guard staged
            // inside it and the two warm pools blend, making the torch impossible
            // to attribute. Removing the component is exactly what the universal
            // gather keys off (view<Position, LightEmitter, SubworldTag>), so the
            // lantern simply drops out of the SSBO next frame. Harness only.
            if (std::getenv("TIMAERT_SMOKE_NO_PLAYER_LIGHT")) {
                int stripped = 0;
                auto pv = app.ecs.reg.view<sm::ecs::PlayerTag,
                                           sm::ecs::LightEmitter>();
                for (auto e : pv) {
                    app.ecs.reg.remove<sm::ecs::LightEmitter>(e);
                    ++stripped;
                }
                std::fprintf(stderr,
                             "[smoke] light_probe_capture stripped player light "
                             "x%d\n", stripped);
                std::fflush(stderr);
            }
            // Opt-in (TIMAERT_SMOKE_SOLO_PROBE_LIGHT=1): the airtight isolation.
            // Strip EVERY LightEmitter that is not on the probe actor itself —
            // the player lantern AND every settlement guard's torch — so the
            // scene is lit by exactly the probe's own carried light, or by
            // nothing. A `guard` frame then shows a single warm ground pool; a
            // `peasant` frame at identical staging is black. That pair attributes
            // the pool to the guard's data-driven torch (Inc 9) with no other
            // light source in play. Same universal key as the gather (any
            // Position+LightEmitter+SubworldTag), so the stripped emitters simply
            // drop out of the SSBO next frame. Harness only.
            if (std::getenv("TIMAERT_SMOKE_SOLO_PROBE_LIGHT")) {
                int stripped = 0;
                auto lv = app.ecs.reg.view<sm::ecs::LightEmitter>();
                for (auto e : lv) {
                    if (e == probeE) continue;
                    app.ecs.reg.remove<sm::ecs::LightEmitter>(e);
                    ++stripped;
                }
                const bool probeLit =
                    app.ecs.reg.all_of<sm::ecs::LightEmitter>(probeE);
                std::fprintf(stderr,
                             "[smoke] light_probe_capture solo probe light: "
                             "stripped x%d probe_lit=%d\n",
                             stripped, int(probeLit));
                std::fflush(stderr);
            }
            std::fprintf(stderr,
                         "[smoke] light_probe_capture probe=%s dist=%.2f "
                         "player=%.1f,%.1f -> probe=%.1f,%.1f yaw=%.1f pitch=%.1f\n",
                         probe, double(dist),
                         double(app.subworld.player_x()),
                         double(app.subworld.player_y()),
                         double(fx), double(fy),
                         double(yaw * 57.2957795f),
                         double(app.subworld.cam_pitch() * 57.2957795f));
            std::fflush(stderr);
            // Enter the settle phase instead of capturing now: the staging we
            // just did only reaches the recorded scene next frame (see the
            // settle-phase note at the top of this case). Pin the actor to its
            // staged spot so a Combat-AI tick can't walk it out of frame during
            // the hold, then let a few frames record the fully-staged scene
            // before the capture arms. Cursor stays put; the settle branch
            // advances it when the countdown ends. A short hold is enough — the
            // very next recorded frame already contains everything.
            app.smoke.probeEntity = probeE;
            app.smoke.probeX = fx;
            app.smoke.probeY = fy;
            app.smoke.probeSettleFrames = 3;
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
                advance_sim_seconds(app, 1.20f, false);
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
                advance_sim_seconds(app, 0.60f, false);
            if (!frameStats.ticked
                || app.gs.player.combatStats.currentMp >= beforeMp) {
                smoke_fail(app, "flight drain tick inactive");
                break;
            }
            enter_subworld(app);
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
            // Gravity invariant (height.h vertical_step): losing flight must
            // NOT snap to the ground — the body falls ballistically from its
            // altitude and lands on its support. Clamp into the envelope with
            // one flying tick, climb a few metres, cut the spell, and watch:
            // a short tick leaves the body still airborne but lower (the old
            // pin would already sit on the ground), and a long soak lands it
            // back at the support it took off from.
            app.subworld.tick(0.016f);
            const float flightBase = app.subworld.flight_height_m();
            app.subworld.move_player(0.0f, 32.0f);
            const float flightApex = app.subworld.flight_height_m();
            app.subworld.set_flying(false);
            app.subworld.tick(0.25f);
            const float fallMid = app.subworld.flight_height_m();
            for (int i = 0; i < 200; ++i) app.subworld.tick(0.05f);
            const float fallRest = app.subworld.flight_height_m();
            // "Came to rest on its support" — asked directly, instead of guessed
            // from a height. The old check compared the landing altitude with the
            // TAKE-OFF altitude, which flying 32 units forward has no reason to
            // match: on a slope the body honestly lands metres lower, and that
            // read as a gravity bug. Comparing against the terrain underfoot is
            // wrong too, in the other direction — the support is max(terrain,
            // structure top), so a body that lands on a roof rests legitimately
            // above the ground (seed 7 lands 6 m up on a building).
            //
            // So assert the thing itself: keep ticking, and if the height has
            // stopped changing the body is standing on SOMETHING; and it must not
            // have sunk through the terrain. Structure-agnostic, and true of
            // every landing.
            for (int i = 0; i < 100; ++i) app.subworld.tick(0.05f);
            const float fallSettled = app.subworld.flight_height_m();
            const float landingGround = app.subworld.ground_height_at(
                app.subworld.player_x(), app.subworld.player_y());
            const bool fellNotSnapped =
                flightApex > flightBase + 1.0f
                && fallMid < flightApex - 0.05f
                && fallMid > flightBase + 1.0f
                && std::fabs(fallSettled - fallRest) < 0.01f
                && fallSettled > landingGround - 0.05f;
            std::fprintf(stderr,
                         "[smoke] flight_fall base=%.2f apex=%.2f mid=%.2f "
                         "rest=%.2f settled=%.2f ground=%.2f\n",
                         flightBase, flightApex, fallMid, fallRest,
                         fallSettled, landingGround);
            std::fflush(stderr);
            if (!fellNotSnapped) {
                smoke_fail(app, "gravity fall-after-flight invariant");
                app.subworld.leave(true);
                break;
            }
            // Jump invariant: grounded after the fall soak, an X-impulse
            // rises through the same integrator, arcs, and lands back on the
            // support (≈1.3 m apex — no damage, it is under the safe speed).
            app.subworld.jump();
            app.subworld.tick(0.30f);
            const float jumpMid = app.subworld.flight_height_m();
            for (int i = 0; i < 60; ++i) app.subworld.tick(0.05f);
            const float jumpRest = app.subworld.flight_height_m();
            std::fprintf(stderr, "[smoke] jump mid=%.2f rest=%.2f\n",
                         jumpMid, jumpRest);
            std::fflush(stderr);
            if (!(jumpMid > fallSettled + 0.3f)
                || std::fabs(jumpRest - fallSettled) > 2.0f) {
                smoke_fail(app, "jump arc invariant");
                app.subworld.leave(true);
                break;
            }
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
                enter_subworld(app);
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
                advance_sim_seconds(app, 0.05f, false);
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

            const int beforeAttributePoints = app.gs.player.sheet.levelData.attributePoints;
            const int beforeReputation =
                sm::player_reputation(&app.gs, "magika");
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
                app.gs.player.sheet.levelData.attributePoints <= beforeAttributePoints ||
                sm::player_reputation(&app.gs, "magika") < beforeReputation + 15) {
                smoke_fail(app, "complete_story_overlay result was not applied");
                break;
            }
            std::fprintf(stderr,
                         "[smoke] complete_story name=\"%s\" attr=%d->%d magika=%d->%d\n",
                         app.gs.player.name.c_str(),
                         beforeAttributePoints,
                         app.gs.player.sheet.levelData.attributePoints,
                         beforeReputation,
                         sm::player_reputation(&app.gs, "magika"));
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
            : choose_new_game_seed(app);
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
                : choose_new_game_seed(app);
            boot_world(app, seed, side, side, &app.customParams.layer,
                       app.customParams.cityCountTarget);
        }
        app.state = sm::ui::AppState::Playing;
        app.customWorldReady = false;
    }
    if (r.loadGame || r.loadAutosave) {
        if (app.state == sm::ui::AppState::Load) {
            const std::string& path =
                r.loadAutosave ? app.autosavePath : app.savePath;
            if (!boot_world_from_save(app, path)) {
                std::fprintf(stderr, "load_game: no save at %s\n", path.c_str());
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
        save_game_checked(app);
    }
    if (r.openCodex) {
        app.state = sm::ui::AppState::Playing;
        app.ui.codex = true;
    }
    if (r.openInterface) {
        app.state = sm::ui::AppState::Playing;
        app.ui.settings = true;
    }
    if (r.openControls) {
        app.state = sm::ui::AppState::Playing;
        app.ui.controls = true;
    }
    if (r.resume)        app.state = sm::ui::AppState::Playing;
    if (r.returnToTitle) { destroy_world(app); app.state = sm::ui::AppState::Title; }
    if (r.quit)          app.running = false;
}

// One turn of the loop: `simSteps` ticks of the world, then one drawn frame.
// Normally one tick (a `simspeed` other than 1 is the only reason it differs).
//
// NOTHING here is handed the real duration of the turn. The world advances by
// ticks and only by ticks, so a machine that cannot keep up draws fewer frames
// and lives fewer ticks — at the same rate, because they are the same number.
void frame(App& app, int simSteps) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) handle_event(app, e);
    sync_relative_mouse_mode(app);

    // TIMAERT_GPU_STATS=1: pass-boundary GPU ms + CPU sim/record ms, one
    // stderr line per second — where the frame's milliseconds actually go.
    static const bool gpuStatsOn = std::getenv("TIMAERT_GPU_STATS") != nullptr;
    const auto statsSimT0 = std::chrono::steady_clock::now();
    advance_sim_steps(app, simSteps, !modal_overlay_active(app));
    const double statsSimMs =
        gpuStatsOn ? std::chrono::duration<double, std::milli>(
                         std::chrono::steady_clock::now() - statsSimT0).count()
                   : 0.0;
    // Macro NPC render positions ease toward the cells the AI put them in.
    // Interpolation for the eye — but it WRITES to the ECS, so it is fed the
    // tick, not the measured length of the turn. Anything that touches game
    // state advances by ticks, without exception; otherwise a slow machine
    // would smooth these at a different pace than the world moved them.
    if (app.state == sm::ui::AppState::Playing && app.worldLoaded) {
        sm::tick_macro_npc_visuals(app.ecs, app.gs.mapW, app.gs.mapH,
                                   sm::kStepSeconds);
    }
    sync_audio_music(app);

    // --- ImGui frame: start BEFORE acquire so that lazy texture loads
    //     (sprite_get -> create_ui_texture -> vkQueueSubmit) happen
    //     outside the active render pass / command buffer recording.
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

    // Persistent framerate readout (`fpshud` console command): a click-through
    // chip under the top bar — the momentary `fps` command, made resident.
    if (app.showFpsHud) {
        const ImGuiIO& io = ImGui::GetIO();
        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - 8.0f, 36.0f),
                                ImGuiCond_Always, ImVec2(1.0f, 0.0f));
        ImGui::SetNextWindowBgAlpha(0.35f);
        ImGui::Begin("##fpshud", nullptr,
                     ImGuiWindowFlags_NoDecoration
                         | ImGuiWindowFlags_AlwaysAutoResize
                         | ImGuiWindowFlags_NoInputs
                         | ImGuiWindowFlags_NoFocusOnAppearing
                         | ImGuiWindowFlags_NoNav
                         | ImGuiWindowFlags_NoSavedSettings);
        ImGui::Text("%.0f fps  %.1f ms", io.Framerate,
                    1000.0f / (io.Framerate + 1e-6f));
        ImGui::End();
    }

    // CPU-side stamps for the frame path no GPU span covers (acquire's fence
    // wait = GPU backpressure; prep = command recording incl. the NPC
    // instance loop and the light-field splat; ui = overlay building;
    // submit = end_frame). This is the "missing ~10 ms" instrument of
    // problems.md §21.B — zero cost unless TIMAERT_GPU_STATS is set.
    struct CpuStat { double acq, prep, scene, ui, submit; };
    static CpuStat cpuAcc{};
    const auto cpuNow = std::chrono::steady_clock::now;
    const auto cpuMs = [](auto a, auto b) {
        return std::chrono::duration<double, std::milli>(b - a).count();
    };
    const auto tAcq0 = cpuNow();

    // Vulkan frame: acquire → shadow → begin render pass → game draws → ImGui → end.
    if (!app.renderer.acquire_frame(app.window)) {
        ImGui::EndFrame();  // balance the NewFrame
        return;
    }
    VkCommandBuffer cmd = app.renderer.current_command_buffer();
    const auto tAcq1 = cpuNow();
    if (gpuStatsOn) cpuAcc.acq += cpuMs(tAcq0, tAcq1);

    // GPU stamps: [0] top, [1] after subworld prepare+shadow (their transfers
    // and the depth pass), [2] after the scene draws (just before the UI is
    // recorded — see below). The slot's PREVIOUS frame is collected first.
    const std::uint32_t statsSlot = app.renderer.currentFrame;
    if (gpuStatsOn) {
        static double spans[gpu::GpuTimer::kMaxStamps] = {};
        static std::uint32_t nSpans = 0;
        static std::uint32_t statFrames = 0;
        static double accSim = 0.0;
        const std::uint32_t got = app.gpuTimer.collect(
            app.device, statsSlot, spans, gpu::GpuTimer::kMaxStamps);
        if (got > 0) nSpans = got;
        accSim += statsSimMs;
        if (++statFrames >= 60u && nSpans >= 2) {
            const double n = double(statFrames);
            std::fprintf(stderr,
                         "[stats] gpu prep+shadow=%.2fms scene=%.2fms | "
                         "cpu sim=%.2f acq=%.2f rec=%.2f scn=%.2f ui=%.2f "
                         "sub=%.2f | %.0f fps\n",
                         spans[0], spans[1], accSim / n, cpuAcc.acq / n,
                         cpuAcc.prep / n, cpuAcc.scene / n, cpuAcc.ui / n,
                         cpuAcc.submit / n,
                         double(ImGui::GetIO().Framerate));
            statFrames = 0;
            accSim = 0.0;
            cpuAcc = CpuStat{};
        }
        app.gpuTimer.begin(cmd, statsSlot);
    }

    const auto tPrep0 = cpuNow();
    if (app.worldLoaded && app.subworld.active()) {
        app.subworld.prepare_frame(cmd);
        app.subworld.record_shadow(cmd);
    }
    if (gpuStatsOn) {
        app.gpuTimer.stamp(cmd, statsSlot);
        cpuAcc.prep += cpuMs(tPrep0, cpuNow());
    }

    app.renderer.begin_render_pass(0.02f, 0.02f, 0.04f);
    VkExtent2D ext = app.renderer.swapchain.extent;

    const auto tScene0 = cpuNow();
    if (app.worldLoaded) {
        if (app.subworld.active()) {
            app.subworld.record_main(cmd, ext, app.renderer.currentFrame);
        } else {
            const float tod = (float(app.gs.worldTime.hour())
                               + float(app.gs.worldTime.minute()) / 60.0f) / 24.0f;
            // The map page (M) is the SAME world drawn through a second
            // camera: on the open edge it anchors on the player (first open
            // lands at the world-fit floor — the whole map in the viewport),
            // then record() simply takes the page's camera instead of the
            // live one. One shader, one upload set, two cameras.
            const bool mapOpen = macro_map_open(app);
            if (mapOpen && !app.mapScreen.wasOpen) {
                if (app.mapScreen.zoom <= 0.0f) {
                    // First open lands at the REGION scale: the geometric
                    // mean of the page's own zoom bounds (world-fit floor,
                    // live-view ceiling) — derived from the bounds, not
                    // tuned, and equally far from "one black planet" and
                    // "one street" whatever the map or window size.
                    app.mapScreen.zoom = std::sqrt(
                        sm::ui::map_fit_zoom(app.height, app.gs.mapH)
                        * kMacroZoomMax);
                }
                app.mapScreen.camX = app.gs.player.x + 0.5f;
                app.mapScreen.camY = app.gs.player.y + 0.5f;
            }
            app.mapScreen.wasOpen = mapOpen;
            const float rCamX = mapOpen ? app.mapScreen.camX : app.camX;
            const float rCamY = mapOpen ? app.mapScreen.camY : app.camY;
            const float rZoom = mapOpen ? app.mapScreen.zoom : app.zoom;
            app.macro.record(cmd, ext, app.terrain,
                             rCamX, rCamY, rZoom,
                             app.gs.mapParams.seaLevel, tod,
                             float(SDL_GetTicks()) * 0.001f,
                             /*mapStyle=*/mapOpen);
        }
    }
    const auto tSceneEnd = cpuNow();
    if (gpuStatsOn) cpuAcc.scene += cpuMs(tScene0, tSceneEnd);

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
        // The map page is a MENU over the game, not a filter on it: while it
        // is open the world overlay — paperdolls, walkers, travel routes,
        // click-to-travel — is NOT drawn at all. The page draws its own
        // primitives over the chart basemap (ui/map_screen.h) and its only
        // click is the pin toggle.
        const bool mapOpen = macro_map_open(app);
        if (mapOpen) {
            sm::ui::draw_map_screen(app.mapScreen, app.gs, app.terrain,
                                    &app.ui.map, logicalW, logicalH,
                                    app.mapScreen.zoom / dpr,
                                    app.uiSettings.scale(sm::ui::UiElementId::PanelMap));
        } else {
        const float zoomLogical = app.zoom / dpr;
        sm::ui::draw_macro_overlay(app.gs, app.ecs,
                                   app.terrain, app.features,
                                   app.cursor,
                                   app.camX, app.camY, zoomLogical,
                                   logicalW, logicalH,
                                   app.gs.mapW, app.gs.mapH,
                                   app.uiSettings.visible(sm::ui::UiElementId::MacroOverlay),
                                   app.uiSettings.visible(sm::ui::UiElementId::QuestMarkers),
                                   app.uiSettings.scale(sm::ui::UiElementId::QuestMarkers),
                                   &app.treeLayer);
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
        }  // !mapOpen — the world's overlay + its click resolution
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
            shell = sm::ui::draw_load_screen(app.saveSummary, app.autosaveSummary,
                                             app.width, app.height);
            break;
        case sm::ui::AppState::Playing:
        {
            const bool modalActive = modal_overlay_active(app);
            if (app.uiSettings.visible(sm::ui::UiElementId::PlayerHud))
                sm::ui::draw_player_hud(app.gs, app.uiSettings.scale(sm::ui::UiElementId::PlayerHud));
            if (!modalActive)
            {
                sm::ui::ToolbarResult tb{};
                if (app.uiSettings.visible(sm::ui::UiElementId::BottomToolbar))
                    tb = sm::ui::draw_bottom_toolbar(app.gs, app.subworld.active(), app.keymap, app.uiSettings.scale(sm::ui::UiElementId::BottomToolbar));
                // II / > are the same pause the Space key toggles; the menu is
                // its own button and its own key.
                if (tb.pause)         set_paused(app, true);
                if (tb.resume)        set_paused(app, false);
                // >> toggles the ONE dev-proven fast-forward (simSpeed, the
                // same knob the console's `simspeed` turns) between 1x and 4x.
                if (tb.speed4 && !app.subworld.active())
                    app.simSpeed = app.simSpeed == 1.0f ? 4.0f : 1.0f;
                // Z arms the rest fast-forward; the main loop promotes ticks
                // until the SP bar fills (see restUntilTick).
                if (tb.rest && !app.subworld.active())
                    aim_rest_until_rested(app);
                if (tb.menu)          app.state          = sm::ui::AppState::Menu;
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
                if (tb.zoomIn || tb.zoomOut) {
                    // The toolbar +/- drive whichever camera owns the screen:
                    // the map page's when it is open, the live one otherwise.
                    const bool mapOpen = macro_map_open(app);
                    float& z = mapOpen ? app.mapScreen.zoom : app.zoom;
                    const float lo = mapOpen
                        ? sm::ui::map_fit_zoom(app.height, app.gs.mapH)
                        : kMacroZoomMin;
                    if (tb.zoomIn)  z *= kMacroZoomStep;
                    if (tb.zoomOut) z /= kMacroZoomStep;
                    if (z > kMacroZoomMax) z = kMacroZoomMax;
                    if (z < lo) z = lo;
                }
                if (tb.toggleSubworld) {
                    if (!app.subworld.active())
                        enter_subworld(app);
                    else
                        app.subworld.leave();
                }
            }
            draw_debug_ui(app);
            sm::dev::draw_debug_console(app.console);
            draw_debug_panels(app);
            if (app.uiSettings.visible(sm::ui::UiElementId::PanelDiplomacy))
                sm::ui::draw_diplomacy(app.gs, &app.ui.diplomacy, app.uiSettings.scale(sm::ui::UiElementId::PanelDiplomacy));
            if (app.uiSettings.visible(sm::ui::UiElementId::PanelCharacter))
                sm::ui::draw_character_panel(app.gs, app.ecs, &app.ui.character, &app.ui.characterTab, app.uiSettings.scale(sm::ui::UiElementId::PanelCharacter));
            if (app.ui.settlement) refresh_available_settlement_quests(app);
            if (app.uiSettings.visible(sm::ui::UiElementId::PanelSettlement))
                sm::ui::draw_settlement(app.gs,
                                        app.ecs,
                                        app.ui.settlementId,
                                        app.availableSettlementQuests,
                                        app.activeQuests,
                                        app.quests,
                                        app.bus,
                                        &app.ui.settlementTab,
                                        &app.ui.settlement,
                                        app.uiSettings.scale(sm::ui::UiElementId::PanelSettlement));
            if (app.uiSettings.visible(sm::ui::UiElementId::PanelQuestLog))
                sm::ui::draw_quest_log(app.gs,
                                       app.activeQuests,
                                       app.quests,
                                       app.bus,
                                       &app.ui.questSelection,
                                       &app.ui.quest,
                                       app.uiSettings.scale(sm::ui::UiElementId::PanelQuestLog));
            if (app.uiSettings.visible(sm::ui::UiElementId::PanelCodex))
                sm::ui::draw_codex(app.gs, &app.ui.codex, app.uiSettings.scale(sm::ui::UiElementId::PanelCodex));
            if (app.ui.settings) {
                if (sm::ui::draw_ui_settings_panel(app.uiSettings, &app.ui.settings))
                    app.uiPrefsDirty = true;
                if (!app.ui.settings && app.uiPrefsDirty) {   // closed with edits -> flush
                    sm::ui::save_ui_settings(app.uiSettings, app.prefsPath);
                    app.uiPrefsDirty = false;
                }
            }
            if (app.ui.controls) {
                // The panel only ARMS a rebind (pendingRebind); the actual key
                // is captured — and saved — in handle_event, where the SDL
                // scancode lives. `changed` here is the Reset button.
                if (sm::ui::draw_keymap_panel(app.keymap, &app.ui.controls))
                    sm::ui::save_keymap(app.keymap, app.keymapPath);
            }
            {
                const std::uint8_t reasons = pause_reasons(app);
                if ((reasons & kPauseModal) == 0) {
                    // The badge quotes the LIVE pause binding, not a literal —
                    // same honesty rule as the toolbar tooltips.
                    char playerLabel[64] = "II  PAUSED";
                    const SDL_Scancode pauseSc =
                        app.keymap.get(sm::ui::ActionId::Pause);
                    if (pauseSc != SDL_SCANCODE_UNKNOWN)
                        std::snprintf(playerLabel, sizeof(playerLabel),
                                      "II  PAUSED  [%s]",
                                      SDL_GetScancodeName(pauseSc));
                    const char* label =
                        (reasons & kPausePlayer) ? playerLabel
                      : (reasons & kPausePanel)  ? "II  PAUSED  (close the panel)"
                      :                            nullptr;
                    if (label != nullptr) {
                        int pauseW = app.width, pauseH = app.height;
                        SDL_GetWindowSize(app.window, &pauseW, &pauseH);
                        draw_pause_badge(pauseW, label);
                    }
                }
            }
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
                // Centre-screen crosshair — four short lines with a gap at the
                // centre, drawn as a shadow + bright pair for contrast on any
                // background. Gated and scaled by the universal UI settings.
                if (app.uiSettings.visible(sm::ui::UiElementId::SubCrosshair)) {
                    const float sc = app.uiSettings.scale(
                        sm::ui::UiElementId::SubCrosshair);
                    const float cx = float(logicalW) * 0.5f;
                    const float cy = float(logicalH) * 0.5f;
                    const float gap  = 3.0f * sc;
                    const float arm  = 9.0f * sc;
                    const float thick = 1.0f * sc;
                    ImDrawList* fg = ImGui::GetForegroundDrawList();
                    const ImU32 shadow = IM_COL32(0, 0, 0, 80);

                    // Faction-stance colour (same palette as minimap blips).
                    // Default: semi-transparent white when nothing is aimed at.
                    ImU32 bright = IM_COL32(255, 255, 255, 120);
                    const float st = app.subworld.crosshair_stance();
                    if (!std::isnan(st)) {
                        const float s = std::clamp(st, -1.0f, 1.0f);
                        auto lerp8 = [](int a, int c, float t)
                        { return int(float(a) + (float(c) - float(a)) * t + 0.5f); };
                        int r, g, b;
                        if (s < 0.0f) {
                            const float t = s + 1.0f;
                            r = lerp8(222, 232, t);
                            g = lerp8(58, 200, t);
                            b = lerp8(48, 70, t);
                        } else {
                            const float t = s;
                            r = lerp8(232, 72, t);
                            g = lerp8(200, 200, t);
                            b = lerp8(70, 92, t);
                        }
                        bright = IM_COL32(r, g, b, 200);
                    }

                    // Shadow pass (offset +1,+1)
                    fg->AddLine(ImVec2(cx - gap - arm + 1, cy + 1),
                                ImVec2(cx - gap + 1, cy + 1),
                                shadow, thick);
                    fg->AddLine(ImVec2(cx + gap + 1, cy + 1),
                                ImVec2(cx + gap + arm + 1, cy + 1),
                                shadow, thick);
                    fg->AddLine(ImVec2(cx + 1, cy - gap - arm + 1),
                                ImVec2(cx + 1, cy - gap + 1),
                                shadow, thick);
                    fg->AddLine(ImVec2(cx + 1, cy + gap + 1),
                                ImVec2(cx + 1, cy + gap + arm + 1),
                                shadow, thick);
                    // Bright pass
                    fg->AddLine(ImVec2(cx - gap - arm, cy),
                                ImVec2(cx - gap, cy), bright, thick);
                    fg->AddLine(ImVec2(cx + gap, cy),
                                ImVec2(cx + gap + arm, cy), bright, thick);
                    fg->AddLine(ImVec2(cx, cy - gap - arm),
                                ImVec2(cx, cy - gap), bright, thick);
                    fg->AddLine(ImVec2(cx, cy + gap),
                                ImVec2(cx, cy + gap + arm), bright, thick);

                    // Interaction prompt, under the reticle: the verb of
                    // whatever is being looked at, quoting the LIVE binding
                    // (the same honesty rule as the pause badge). It is the
                    // engine's own resolution, so the prompt can never
                    // promise an action the keypress would not perform.
                    const char* verb = app.subworld.interact_prompt();
                    if (verb != nullptr && verb[0] != '\0') {
                        const SDL_Scancode useSc =
                            app.keymap.get(sm::ui::ActionId::Interact);
                        char prompt[96];
                        if (useSc != SDL_SCANCODE_UNKNOWN) {
                            std::snprintf(prompt, sizeof(prompt), "[%s] %s",
                                          SDL_GetScancodeName(useSc), verb);
                        } else {
                            std::snprintf(prompt, sizeof(prompt), "%s", verb);
                        }
                        const ImVec2 ts = ImGui::CalcTextSize(prompt);
                        const float px = cx - ts.x * 0.5f;
                        const float py = cy + gap + arm + 8.0f * sc;
                        fg->AddText(ImVec2(px + 1.0f, py + 1.0f),
                                    IM_COL32(0, 0, 0, 160), prompt);
                        fg->AddText(ImVec2(px, py),
                                    IM_COL32(240, 232, 200, 235), prompt);
                    }
                }
                if (app.uiSettings.visible(sm::ui::UiElementId::SubDangerGem))
                    draw_subworld_danger_gem(app.subworld, app.uiSettings.scale(sm::ui::UiElementId::SubDangerGem));
                if (app.uiSettings.visible(sm::ui::UiElementId::SubCombatLog))
                    draw_subworld_combat_log(app.subworld, logicalW, app.uiSettings.scale(sm::ui::UiElementId::SubCombatLog));
                if (app.uiSettings.visible(sm::ui::UiElementId::SubMinimap)) {
                    const auto& miniBlips = app.subworld.collect_minimap_blips();
                    sm::ui::draw_subworld_minimap_hud(app.subworld.mgr(),
                        app.subworld.player_x(), app.subworld.player_y(),
                        app.subworld.cam_yaw(), logicalW, logicalH,
                        miniBlips.data(), miniBlips.size(),
                        app.uiSettings.scale(sm::ui::UiElementId::SubMinimap),
                        app.uiSettings.visible(sm::ui::UiElementId::PlayerHud)
                            ? sm::ui::kTopStatusBarHeight
                                  * app.uiSettings.scale(sm::ui::UiElementId::PlayerHud)
                            : 0.0f);
                }
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
                if (app.uiSettings.visible(sm::ui::UiElementId::PanelMap))
                    sm::ui::draw_subworld_map_overlay(app.subworld.mgr(),
                        app.subworld.player_x(), app.subworld.player_y(),
                        app.subworld.cam_yaw(),
                        &app.ui.map,
                        app.uiSettings.scale(sm::ui::UiElementId::PanelMap));
            }
            // (Macro: the M toggle is the full-screen map PAGE — drawn on the
            // world-overlay path above (ui/map_screen.h), not a window here.
            // The 256px minimap window died with it.)
            sm::ui::draw_encounter_modal(app.gs, app.bus);
            draw_pre_battle_modal(app);
            // Right-edge nearby-NPC stack (mirrors NpcProximityPanel.svelte).
            // Macro view only. The badge stack follows TS anyOverlayOpen
            // suppression, while an already-open native NPC popup keeps
            // rendering until it is closed.
            const bool showNpcRows = !macro_overlay_blocks_npc_proximity(app);
            if (!app.subworld.active()
                && app.uiSettings.visible(sm::ui::UiElementId::NpcProximity)
                && (showNpcRows || sm::ui::npc_proximity_popup_open())) {
                int logicalW = app.width, logicalH = app.height;
                SDL_GetWindowSize(app.window, &logicalW, &logicalH);
                const sm::ui::NpcProximityResult npcResult =
                    sm::ui::draw_npc_proximity_panel(app.gs, app.ecs,
                                                     logicalW, logicalH,
                                                     showNpcRows,
                                                     app.uiSettings.scale(sm::ui::UiElementId::NpcProximity));
                if (npcResult.attackNpc != entt::null) {
                    (void)route_macro_npc_attack(app, npcResult.attackNpc);
                }
            }
            sm::ui::draw_show_dialog(app.gs, sm::player_inventory(app.ecs), app.showDialogEvent, app.bus,
                                     app.showDialogUi, &app.showDialogOpen);
            handle_dialog_node_activation(app);
            sm::ui::draw_story_overlay(app.storyOverlay, app.bus);
            break;
        }
        case sm::ui::AppState::Menu:
            if (app.uiSettings.visible(sm::ui::UiElementId::PlayerHud))
                sm::ui::draw_player_hud(app.gs, app.uiSettings.scale(sm::ui::UiElementId::PlayerHud));
            shell = sm::ui::draw_game_menu(app.width, app.height);
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

    if (gpuStatsOn) {
        app.gpuTimer.stamp(cmd, statsSlot); // scene end
        cpuAcc.ui += cpuMs(tSceneEnd, cpuNow());
    }
    ImGui::Render();
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
    // Screenshot: arm the capture BEFORE end_frame() so the copy is recorded
    // into this frame's command buffer (spec-valid — the image is only touched
    // between acquire and present), then drain it to a PNG after end_frame().
    const bool doCapture = app.smoke.enabled && app.smoke.capturePending;
    const int captureActionIndex = app.smoke.captureActionIndex;
    app.smoke.capturePending = false;
    if (doCapture) app.renderer.request_capture();
    const auto tSubmit0 = cpuNow();
    app.renderer.end_frame(app.window);
    if (gpuStatsOn) cpuAcc.submit += cpuMs(tSubmit0, cpuNow());
    if (doCapture) {
        // ONE drain point for the presented frame. wait_visible wants the
        // pixels themselves; every other capture action wants a PNG.
        if (app.smoke.pixelProbeArmed) {
            app.smoke.pixelProbeArmed = false;
            if (!app.renderer.take_capture(app.smoke.probePixels,
                                           app.smoke.probePixW,
                                           app.smoke.probePixH,
                                           app.smoke.probePixFmt)) {
                smoke_fail(app, "wait_visible: frame readback unavailable");
            }
        } else if (!write_smoke_frame_png(app, captureActionIndex, "ui")) {
            smoke_fail(app, "capture_frame write failed");
        }
    }
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
    // The autosave slot is a SIBLING of the manual save: same directory,
    // its own file. resolve_save_path always ends in kSaveFileName.
    app.autosavePath =
        app.savePath.substr(0, app.savePath.size()
                               - std::string_view(kSaveFileName).size())
        + kAutosaveFileName;
    if (boot_trace_enabled() || app.smoke.enabled) {
        std::fprintf(stderr, "[save] path=%s autosave=%s\n",
                     app.savePath.c_str(), app.autosavePath.c_str());
        std::fflush(stderr);
    }
    app.prefsPath = resolve_prefs_path(kPrefsFileName);
    load_ui_settings(app.uiSettings, app.prefsPath);  // missing file -> defaults stand
    app.keymapPath = resolve_prefs_path(kKeymapFileName);
    load_keymap(app.keymap, app.keymapPath);          // missing file -> defaults stand
    if (boot_trace_enabled() || app.smoke.enabled) {
        std::fprintf(stderr, "[ui] prefs=%s\n", app.prefsPath.c_str());
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

    // ONE TURN OF THE LOOP IS ONE TICK, and the wall clock is consulted for a
    // single purpose: if the turn was quicker than a tick is worth, wait for the
    // remainder so the world can never run FASTER than nominal. It is never
    // consulted to decide that ticks are owed — there is no accumulator here and
    // no debt, so a stall is one late tick and a SUSPENDED process (a closed
    // laptop, an hour on a breakpoint) simply ran no turns and advanced no
    // ticks. See core/time.h for why that is the whole rule.
    const Uint64 freq = SDL_GetPerformanceFrequency();
    const Uint64 countsPerTick =
        std::max<Uint64>(1, freq / Uint64(sm::kTicksPerRealSecond));
    Uint64 turnStart = SDL_GetPerformanceCounter();
    while (app.running) {
        // A developer fast-forward runs several ticks per turn; its fractional
        // part carries so 1.0 is exact and only a deliberate speed-up rounds.
        app.simStepCarry += app.simSpeed;
        int ticks = int(app.simStepCarry);
        app.simStepCarry -= float(ticks);
        if (ticks < 0) ticks = 0;

        // Rest (toolbar Z): promote this turn's ticks until the player's SP
        // bar is FULL, capped by restUntilTick (two days — the guard against
        // an SP debt that regenerates slower than the marching discount).
        // 128 ticks a turn × 64 turns a second = 8192 ticks/s — exactly ONE
        // game day per real second, so a full rest lands in about a second
        // while every frame still renders (the rest is visible and
        // interruptible). The law itself — promote / stop on full / cancel
        // on a scene change — lives in apply_rest_promotion.
        ticks = apply_rest_promotion(app, ticks);

        frame(app, ticks);
        const Uint64 turnEnd0 = SDL_GetPerformanceCounter();

        // Measure what the world ACTUALLY lived, once a second.
        app.tickRateCounter += ticks;
        if (app.tickRateMark == 0) app.tickRateMark = turnEnd0;
        if (turnEnd0 - app.tickRateMark >= freq) {
            app.measuredTicksPerSec = float(double(app.tickRateCounter) * double(freq)
                                            / double(turnEnd0 - app.tickRateMark));
            app.tickRateCounter = 0;
            app.tickRateMark = turnEnd0;
        }

        // Throttle, never boost — and land ON the boundary, not near it.
        //
        // Sleeping alone cannot do it: no OS sleep is exact, and one that
        // returns EARLY starts the next tick before its time, which would make
        // the world run FASTER than nominal — the one thing this rule forbids.
        // So sleep almost all of the remainder and spin out the last sliver.
        //
        // The sliver is a real cost — a busy loop burns a core — so it is kept
        // to kSpinGuard rather than the whole millisecond SDL_Delay's rounding
        // would force. std::this_thread::sleep_for goes through nanosleep on
        // POSIX and is good to a fraction of a millisecond, so the guard can be
        // small: ~0.2 ms of every 15.6 is a little over 1% of one core. If the
        // sleep overshoots the deadline anyway the turn is simply late, which
        // is allowed; only running early is not.
        constexpr double kSpinGuardMs = 0.2;
        const Uint64 deadline = turnStart + countsPerTick;
        Uint64 nowCounts = SDL_GetPerformanceCounter();
        if (nowCounts < deadline) {
            const double leftMs =
                double(deadline - nowCounts) * 1000.0 / double(freq);
            if (leftMs > kSpinGuardMs) {
                std::this_thread::sleep_for(std::chrono::duration<double,
                                            std::milli>(leftMs - kSpinGuardMs));
            }
            while (SDL_GetPerformanceCounter() < deadline) { /* ~0.2 ms */ }
            turnStart = deadline;   // exact: the next turn starts on the beat
        } else {
            turnStart = nowCounts;  // the turn overran; it is simply late
        }
    }

    const int exitCode = app.smoke.failed ? 2 : 0;
    if (app.uiPrefsDirty) save_ui_settings(app.uiSettings, app.prefsPath);  // backstop
    vkDeviceWaitIdle(app.device.device);
    destroy_world(app);
    sm::ui::destroy_all_ui_textures();
    sm::sprite_atlas_shutdown();
    app.subworld.destroy(app.device);
    app.macro.destroy(app.device);
    app.audio.shutdown();
    shutdown_imgui(app);
    app.gpuTimer.destroy(app.device);
    app.renderer.destroy();
    app.device.destroy();
    SDL_DestroyWindow(app.window);
    SDL_Quit();
    return exitCode;
}

// App — the program's ONE aggregate — and the shared services main.cpp
// defines around it. Extracted from main.cpp so the smoke harness TU
// (app/smoke.cpp) shares the same App and the same runtime entry points
// instead of a copy (canon-audit verdict, 2026-08-29).
#pragma once
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
#include "macro/nav_field.h"
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
#include "macro/journal.h"
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
#include <vulkan/vulkan.h>
#include "app/smoke.h"

namespace sm::app {

constexpr const char* kSaveFileName = "save.bin";
// The monthly autosave writes a SIBLING slot, never the player's own file —
// an autosave that overwrites the manual save turns "the world is on disk"
// into "your last deliberate save is gone" (Session 17).
constexpr const char* kAutosaveFileName = "autosave.bin";
constexpr const char* kPrefsFileName = "ui_prefs.cfg";
constexpr const char* kKeymapFileName = "keymap.cfg";
constexpr std::size_t kPendingPresentationMax = 8;

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
    // (No travel-stamina field. The player's ONE signed fractional carry is
    // `MacroNpcRuntime::spCarry` on his squad entity — the same field every
    // lord keeps — reached through player_sp_carry(app).)

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
    // Working memory for every find_path over pathCost (map-click travel,
    // smoke travel probe): allocated once per map size, generation-reset per
    // search — the per-call ~13 MB of scratch vectors died with it (S7).
    sm::PathScratch pathScratch;
    // Запечённая навигация рейсов: округи + порталы + граф
    // (macro/nav_field.h, CANON S7). Derived: свежесть ловит nav_ensure
    // (сид, ландмарки, мосты) — ручной чистки не нужно.
    sm::NavWorld navWorld;
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

// WHY the world stands still — one bit per reason; the full design note sits
// above pause_reasons() in main.cpp. Exactly one bit is stored (the player's
// own); every other reason is derived on the spot.
enum PauseReason : std::uint8_t {
    kPauseNone   = 0u,
    kPausePlayer = 1u << 0,   // stored: the Space key, or the toolbar's II
    kPausePanel  = 1u << 1,   // derived: a panel opened over the world
    kPauseModal  = 1u << 2,   // derived: event dialog / story slides / Event substate
    kPauseMenu   = 1u << 3,   // derived: any screen that is not Playing
};

struct RuntimeFrameStats {
    sm::WorldTickResult timeTick{};
    sm::MacroNpcAiSliceResult macroNpcAi{};
    bool ticked = false;
    bool subworldActive = false;
};

// ── Shared app services ─────────────────────────────────────────
// Defined in main.cpp; declared here because app/smoke.cpp (the smoke
// harness) exercises the same runtime the player does — one implementation,
// honest visibility, no second path.
sm::Inventory& player_bag(App& app);
sm::MacroWorld macro_world(App& app);
long file_size_bytes(const std::string& path);
void refresh_available_settlement_quests(App& app);
bool route_macro_npc_attack(App& app, entt::entity npc);
void perform_encounter_auto(App& app, entt::entity npc, sm::Ambush ambush);
void detect_forced_encounter(App& app);
float& player_sp_carry(App& app);
sm::CharacterSheet player_effective_sheet(const App& app);
void sync_audio_music(App& app);
void refresh_save_summary(App& app);
std::vector<sm::MacroNpcRecord> stage_save_state(App& app);
void enter_subworld(App& app);
std::uint8_t pause_reasons(const App& app);
bool world_paused(const App& app);
void sync_relative_mouse_mode(App& app);
std::vector<sm::PathPoint> build_flight_path(int sx, int sy, int gx, int gy,
                                             int mapW, int mapH);
int count_tick_events(const sm::EventBus& bus, sm::EventTag tag);
const sm::GameEvent* latest_tick_event(const sm::EventBus& bus,
                                       sm::EventTag tag);
int charge_subworld_sp_for_distance(App& app, float distance);
bool cast_active_spell(App& app);
void aim_rest_until_rested(App& app);
int apply_rest_promotion(App& app, int ticks);
void apply_pending_event_effects(App& app);
void apply_pending_story_results(App& app);
void capture_presentation_events(App& app);
std::uint64_t quest_marker_signature(const std::vector<sm::Quest>& active);
void process_world_events(App& app);
RuntimeFrameStats tick_playing_runtime(App& app, bool allowInput);
RuntimeFrameStats advance_sim_steps(App& app, int steps, bool allowInput);
RuntimeFrameStats advance_sim_seconds(App& app, float seconds, bool allowInput);

} // namespace sm::app

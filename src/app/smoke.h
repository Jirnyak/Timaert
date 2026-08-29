// The smoke harness's narrow interface — extracted from main.cpp (canon-audit
// verdict, 2026-08-29: half of a 12k-line TU was test harness). The harness
// itself (every run_*_smoke, run_console_smoke, tick_smoke_script and the
// helpers only they use) lives in app/smoke.cpp; main.cpp keeps only the
// calls listed at the bottom of this header. SmokeAction/SmokeScript sit here
// because App carries the script state (App::smoke).
#pragma once
#include <array>
#include <cstdint>
#include <vector>
#include <vulkan/vulkan.h>
#include "ecs/world.h"     // entt::entity for the tracked-body probes
#include "ui/screens.h"    // sm::ui::ShellResult, the harness's frame verdict

namespace sm::app {

struct App;

constexpr const char* kSmokeScriptEnv = "TIMAERT_SMOKE_SCRIPT";
constexpr const char* kSmokeSeedEnv = "TIMAERT_SMOKE_SEED";

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
    ChronicleRate,
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

// Defined in app/smoke.cpp. This is the WHOLE surface main.cpp drives the
// harness through; anything else the harness needs it takes from app_state.h.
bool parse_smoke_script(const char* script, SmokeScript& out);
sm::ui::ShellResult tick_smoke_script(App& app);
void smoke_after_shell_actions(App& app);
void smoke_fail(App& app, const char* reason);
bool write_smoke_frame_png(App& app, int actionIndex, const char* label);

} // namespace sm::app

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
#include "ecs/components.h"   // sm::MacroHandle — the tracked-record probe
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
    SubworldWalk,
    SubworldSeam,
    SubworldAudio,
    SubworldExitGate,
    SubworldLootXp,
    SubworldEnemyFeedback,
    SubworldMissileFeedback,
    SubworldSelfFireball,
    SubworldPlayerMelee,
    SubworldPlayerBow,
    SubworldReputationHit,
    SubworldMouseRelease,
    SubworldTreeAnchor,
    SubworldRecovery,
    SubworldSpDrain,
    TurnBasedCycle,
    SubworldEnter,
    SubworldExitRemap,
    PossessedDeath,
    DungeonHouse,
    DungeonCave,
    PrologueRoad,
    SpireClimb,
    TriggerBattleStart,
    WaitVisible,
    OpenSettlementBuild,
    OpenSettlementTrade,
    OpenSettlementMap,
    EnterFirstSettlement,
    CityGateProbe,
    CityDayPump,
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
    OpenCraft,
    SpendAttributeEnd,
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
    ConsoleSmoke,
    ReturnTitle,
    Quit,
};

struct SmokeScript {
    static constexpr int kMaxActions = 16;
    std::array<SmokeAction, kMaxActions> actions{};
    int count = 0;
    int cursor = 0;
    // THE ASSERTION COUNTER — the discipline tests/check.h has had for a year
    // and this harness never did. A smoke scenario used to carry its verdict
    // in one `if (a && b && … && q) smoke_fail(app, "<name> invariant")`, so a
    // run could say nothing at all and still exit green: FIFTEEN of the 67
    // suite lines asserted not one fact about their own subject, and the whole
    // suite evaluated NINE counted facts between them (census 2026-09-26).
    // `spire_perf` was the clearest case and is gone: it measured ms/tick,
    // printed the budget and `return true`d unconditionally — a scenario in a
    // PASS/FAIL suite that could not go red. Its measurement was honest and
    // deliberate (CANON S28, «потолков нет»), which is exactly why it did not
    // belong in the suite: an instrument reports, a suite judges.
    //
    // A check writes here, on BOTH paths, so "this run measured nothing" is a
    // countable statement instead of a silence. Per-ACTION, not per-run,
    // because smoke.sh wraps every scenario in new_game,wait_boot_done…quit:
    // count per run and the prefix's own checks would answer for the scenario.
    //
    // MIGRATION, named so it cannot be mistaken for the finished state: the
    // verdict "an action that ran zero checks FAILS" is not armed yet. 308 of
    // the harness's assertion sites still speak through smoke_fail(), which
    // fires only when a fact is already broken and is invisible when it holds;
    // arming before they migrate would redden all 67, and exempting them with
    // a flag would be the very crutch this counter exists to remove. Until
    // then every quit prints the gap (M-131).
    std::array<std::uint16_t, kMaxActions> actionChecks{};
    int checksRun = 0;
    int checksFailed = 0;
    // A PRECONDITION died (smoke_fail): the script stops where it stands,
    // because there is nothing left to ask. Kept apart from `failed` — which
    // a mere broken FACT also sets — so that a red check no longer silences
    // the rest of the run and the verdict report never gets to print.
    bool aborted = false;
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
    // city_gate_probe aims the camera at a gate it just measured, then holds
    // for the same reason light_probe_capture does — a teleport re-seats the
    // camera in the ENGINE tick, which runs after this frame was recorded, so
    // arming the capture in the same tick photographs the old viewpoint. The
    // aim is re-applied every held frame because the engine's own look logic
    // owns the camera between them. -1 = idle.
    int gateAimFrames = -1;
    float gateAimX = 0.0f, gateAimY = 0.0f, gateAimZ = 0.0f;
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
    // Запись — хэндлом store (шаг 2 1е): свидетель смерти слота обязан
    // тухнуть вместе с поколением, как всякая долгоживущая ссылка.
    sm::MacroHandle trackedMacro{};
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
void smoke_check(App& app, bool ok, const char* what, const char* file, int line);
bool write_smoke_frame_png(App& app, int actionIndex, const char* label);

} // namespace sm::app

// ONE fact, named where it stands. Two things separate this from smoke_fail:
//
//   * it counts on the green path too, so a scenario that measured nothing is
//     distinguishable from one whose every fact held; and
//   * it does NOT stop the run. A conjunction of seventeen facts under one
//     verdict reports the FIRST thing that tripped and nothing after it —
//     that is why `spire_climb` printed ten zeroes for facts its execution had
//     never reached, and why the red read as "the whole spire is broken" when
//     one fact (the roof) was. Every check speaks for itself, so the log names
//     what actually broke; downstream facts that depend on it still fall, but
//     each falls BY NAME and its cascade is visible as a cascade.
//
// The message states what MUST hold, so the failure line reads as the broken
// promise; file:line comes from the compiler, never from a stale string.
#define SMOKE_CHECK(app, expr, why) \
    ::sm::app::smoke_check((app), static_cast<bool>(expr), (why), __FILE__, __LINE__)

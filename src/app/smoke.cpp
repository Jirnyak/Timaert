// The smoke harness — every run_*_smoke scenario, run_console_smoke,
// tick_smoke_script and the helpers ONLY they use. Moved out of main.cpp
// verbatim (canon-audit verdict, 2026-08-29: the harness was ~52% of a
// 12k-line TU). The harness exercises the SAME runtime the player does —
// everything it drives is a shared service declared in app/app_state.h and
// defined in main.cpp; nothing here re-implements game law.
#include "app/app_state.h"
#include "app/smoke.h"

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

namespace sm::app {

constexpr int kSubworldSmokeFrames = 1000;
constexpr int kSubworldSeamSmokeSettleFrames = 120;
constexpr int kSmokeMacroTravelSteps = 3;

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
    {"chronicle_rate", SmokeAction::ChronicleRate},
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
    // A hypothetical body gets a hypothetical carry: this reference is not the
    // player, and must not spend out of his.
    float referenceCarry = 0.0f;
    sm::apply_minute_recovery(reference, minutesAdvanced, referenceAcc,
                              referenceCarry);

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
    player_sp_carry(app) = 0.0f;
    // Stamina has ONE signed carry now, and rest fills the very remainder a
    // march spends out of — which is the point of it, and which means a single
    // frame of recovery lands inside the number this smoke is measuring. It
    // used to land in a separate regen-only accumulator nobody counted, so the
    // leak was invisible rather than absent. The ruler measures what the
    // GROUND charged, so the body does not mend while it is being read.
    const float spRegenWas = app.gs.player.combatStats.spRegen;
    app.gs.player.combatStats.spRegen = 0.0f;

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
    const float carryBefore = player_sp_carry(app);
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
    const float carryAfter = player_sp_carry(app);
    app.subworld.leave(true);

    // THE law, not a magic number: distance in macro cells × the weight of the
    // ground, every point of it either charged or still carried. This is the
    // same formula the map layer pays (macro/movement_cost.h) — one journey,
    // one price, whichever layer you walk it on. `expected` was summed leg by
    // leg above; `weight` is the last leg's ground, printed as a witness that
    // the walk was on real terrain.
    // The carry is SIGNED and a march drives it DOWN, so what is still owed
    // reads as a negative remainder: the ground asked for everything the bar
    // gave up plus everything the carry sank by.
    const float accounted = float(charged) + (carryBefore - carryAfter);
    app.gs.player.combatStats.spRegen = spRegenWas;   // the body may mend again

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

// First city of the ONE roster — the "front of gs.settlements" the smoke
// scripts used to pin before the landmark merge (v62).
const sm::Landmark* smoke_first_city(const App& app) {
    for (const auto& lm : app.gs.landmarks) {
        if (lm.type == sm::LandmarkType::City) return &lm;
    }
    return nullptr;
}

bool smoke_find_open_subworld_cell(const App& app, int& outX, int& outY) {
    if (!app.terrain.has_rgba_storage() || app.terrain.width <= 0
        || app.terrain.height <= 0) {
        return false;
    }
    auto hasLandmark = [&](int x, int y) {
        for (const auto& lm : app.gs.landmarks) {
            if (lm.x == x && lm.y == y) return true;
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

bool smoke_find_macro_travel_path(App& app, sm::PathResult& out) {
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
                                                    sx + dx, sy + dy,
                                                    app.pathScratch);
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
    // Same reason as subworld_sp_drain: one signed carry means a frame of rest
    // lands inside the number being measured. The ruler measures the ground.
    const float spRegenWas = app.gs.player.combatStats.spRegen;
    app.gs.player.combatStats.spRegen = 0.0f;
    const int beforeSp = app.gs.player.combatStats.currentSp;
    const int beforeHp = app.gs.player.combatStats.currentHp;
    const float beforeX = app.gs.player.x;
    const float beforeY = app.gs.player.y;
    const float carryBefore = player_sp_carry(app);

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
        float(spentSp) + (carryBefore - player_sp_carry(app));
    app.gs.player.combatStats.spRegen = spRegenWas;

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
                 double(carryBefore), double(player_sp_carry(app)),
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
    player.sheet.attributes[sm::AttributeId::End] = 1;
    player.sheet.attributes[sm::AttributeId::Vit] = 1;
    player.sheet.attributes[sm::AttributeId::Wil] = 1;
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

    // A second, separate burst: one hour past 09:00 must deliver exactly one
    // TimeAdvance at 10:00. The old check read this off the bus's history
    // ring; the ring is gone (the past lives in the chronicle/journal, S20.1),
    // so the live source is the same subscription contract as above.
    int lateCount = 0;
    int lateDay = -1;
    int lateHour = -1;
    const std::uint32_t lateSubId =
        app.bus.on(sm::EventTag::TimeAdvance, [&](const sm::GameEvent& ev) {
            ++lateCount;
            lateDay = int(ev.a);
            lateHour = ev.iy;
        });
    app.gs.worldTime = sm::world_time_at(0, 9, 0);
    sm::reset_world_tick_runtime(app.gs.worldTickRt, app.gs.worldSeed);
    const RuntimeFrameStats lateStats =
        advance_sim_steps(
            app, int(sm::ticks_to_advance_minutes(app.gs.worldTime.tick, 61)),
            false);
    app.bus.unsubscribe(lateSubId);
    const bool lateBurstOk = lateStats.ticked
        && !lateStats.subworldActive
        && lateStats.timeTick.hoursAdvanced == 1
        && lateCount == 1
        && lateDay == 0
        && lateHour == 10;

    const bool ok = subscriberOk && lateBurstOk;

    std::fprintf(stderr,
                 "[smoke] timeadvance_burst hoursAdvanced=%d count=%d "
                 "events=[%d:%02d,%d:%02d,%d:%02d] lateCount=%d latest=%d:%02d\n",
                 stats.timeTick.hoursAdvanced,
                 count,
                 days[0], hours[0],
                 days[1], hours[1],
                 days[2], hours[2],
                 lateCount,
                 lateDay, lateHour);
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

    // The trace lane must be QUIET: the trace measures rest and march
    // MECHANICS, and any other squad within perception range can lawfully
    // rewrite the story (a hostile on the cell freezes the think for the
    // encounter door; an auto-battle can kill the traced squad outright and
    // the corpse sweep then DESTROYS the entity under our references —
    // both seen live on seed 999, 2026-08-29). So: start from the far side
    // of the torus from the player and take the first grid spot with no
    // other macro NPC within 16 cells (> 2× kSquadSightCells — perception
    // cannot reach the lane, and the 3-cell march stays inside the margin).
    int baseX = sm::wrapi(int(app.gs.player.x) + app.gs.mapW / 2,
                          app.gs.mapW);
    int baseY = sm::wrapi(int(app.gs.player.y) + app.gs.mapH / 2,
                          app.gs.mapH);
    {
        auto others = app.ecs.reg.view<sm::ecs::Position,
                                       sm::ecs::MacroNpcRuntime>(
            entt::exclude<sm::ecs::Dead, sm::ecs::SubworldTag>);
        auto lane_clear = [&](int cx, int cy) {
            for (auto o : others) {
                if (o == e) continue;
                const auto& op = others.get<sm::ecs::Position>(o);
                const int dx = std::abs(int(op.x) - cx);
                const int dy = std::abs(int(op.y) - cy);
                const int cheb = std::max(std::min(dx, app.gs.mapW - dx),
                                          std::min(dy, app.gs.mapH - dy));
                if (cheb < 16) return false;
            }
            return true;
        };
        for (int probe = 0; probe < 1024 && !lane_clear(baseX, baseY);
             ++probe) {
            baseX = sm::wrapi(baseX + 37, app.gs.mapW);   // coprime strides
            baseY = sm::wrapi(baseY + (probe % 31 == 30 ? 41 : 0),
                              app.gs.mapH);
        }
    }
    pos.x = float(baseX);
    pos.y = float(baseY);
    visual.vx = pos.x;
    visual.vy = pos.y;

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
    // allowAutoBattle=false: the world keeps thinking, but no meeting
    // resolves during the trace — a battle death would let the corpse sweep
    // destroy entities under this smoke's live references.
    for (int i = 0; i < 64; ++i) {
        sm::tick_macro_npc_ai(traceMw, app.npcAi, sm::kAiTicks,
                              /*allowAutoBattle=*/false);
        if (rt.state == std::uint8_t(sm::NPCState::Idle)) {
            break;
        }
    }
    const int recoveredSp = rt.sp;
    const int recoveredState = int(rt.state);
    const bool recovered =
        recoveredSp >= maxSp / 2
        && rt.state == std::uint8_t(sm::NPCState::Idle);

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
        sm::tick_macro_npc_ai(traceMw2, app.npcAi, sm::kAiTicks,
                              /*allowAutoBattle=*/false);
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
                 "[smoke] macro_npc_trace entity=%u kind=%d marathon=%d "
                 "maxSp=%d "
                 "rest=%d:%d recovered=%d move=%.1f,%.1f->%.1f,%.1f "
                 "steps=%d marched=%.2f visual=%.2f->%.2f->%.2f\n",
                 unsigned(entt::to_integral(e)),
                 int(kind.type), int(rt.marathonRank),
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
        // The street door does not exist up here. Mid-room E may legally
        // find FURNITURE — the storey's own props, or the shaft itself when
        // the house grew it near the room's middle (worldgen owns the
        // layout, the smoke does not) — but it must never put us on the
        // street: the only way out of a house is its ground-floor door.
        app.subworld.set_player_pos(float(sm::sub::kFullSize) / 2.0f,
                                    float(sm::sub::kFullSize) / 2.0f);
        app.subworld.tick(0.016f);
        const bool foundProp = app.subworld.interact();
        app.subworld.tick(0.016f);
        if (!app.subworld.in_dungeon()) {
            smoke_fail(app, "dungeon_house upper storey had a way out");
            return false;
        }
        if (foundProp && app.subworld.dungeon_level() != lvlUp) {
            // E hit the shaft — ride it back so the storey bookkeeping
            // below sees the same house it always did.
            app.subworld.debug_take_stairs(/*up*/true);
            app.subworld.tick(0.016f);
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
        sm::Landmark* town = nullptr;
        for (auto& s : app.gs.landmarks) {
            if (s.type != sm::LandmarkType::City) continue;
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
    const sm::Landmark* target = nullptr;
    for (const auto& sp : app.gs.landmarks) {
        if (sp.type != sm::LandmarkType::Spire) continue;
        if (sp.depleted || sp.spellId >= std::uint32_t(sm::kSpellCount))
            continue;
        if (sm::spellbook_has_learned(app.gs.player.spellBook,
                                      int(sp.spellId))) {
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
        learned = sm::spellbook_has_learned(app.gs.player.spellBook,
                                            sm::spell_ordinal(def.id));
        if (const sm::Landmark* sp = sm::landmark_by_id(app.gs, spireId))
            depletedFlag = sp->depleted;
        orbsAfter = 0;
        for (const auto& s : app.subworld.mgr().structures()) {
            if (s.kind == sm::sub::Structure::SpireOrb) ++orbsAfter;
        }
        for (const auto& line : app.gs.sessionFeed.lines) {
            if (std::strstr(line.text, "You have learned") != nullptr) {
                logged = true;
            }
        }
    }
    restore();

    // THE MICRO→MACRO DOOR, proved end to end (CANON S20.1): a deed done
    // underground is a fact ON THE MAP, filed at the macro cell that contained
    // the subworld. If the two layers kept two memories, this count would be
    // zero and nobody walking past next week would ever know a spire was
    // drained here.
    int remembered = 0;
    sm::chronicle_recent(app.gs.chronicle, /*sinceDay*/0, /*limit*/64,
                         [](void* u, const sm::WorldFact& f) {
                             if (f.kind == std::uint16_t(sm::FactKind::Explored))
                                 ++*static_cast<int*>(u);
                         }, &remembered);

    std::fprintf(stderr,
                 "[smoke] spire_climb spell=%s tier=%d gateTier=%d entered=%d "
                 "inTower=%d climbs=%d top=%d yard=%d guards=%d stuck=%d "
                 "onRoof=%d dz=%.1f learned=%d depleted=%d orbsAfter=%d "
                 "logged=%d remembered=%d\n",
                 def.id, tier, gateTier, entered ? 1 : 0,
                 inTower ? 1 : 0,
                 climbs, topLevel, yardGuards, guardsSeen,
                 climbStuck ? 1 : 0,
                 onRoof ? 1 : 0, roofZ - groundZ, learned ? 1 : 0,
                 depletedFlag ? 1 : 0, orbsAfter, logged ? 1 : 0, remembered);
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
        || orbsAfter != 0 || !logged
        // The world above must remember what was done below.
        || remembered < 1) {
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
    sm::spellbook_learn(app.gs.player.spellBook,
                        sm::spell_ordinal("fireball"));
    sm::spellbook_set_active(app.gs.player.spellBook,
                             sm::spell_ordinal("fireball"));

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
// The ring's size is a formula with one unmeasured term — the world's facts
// per day (chronicle.h, kChronicleFacts: "retune it from the counter, not
// from feel"). This smoke IS that counter's instrument: a full game day of
// the LIVE loop (world_tick's daily step, the AI's caravans and gatherers,
// auto-battles — every writer there is), measured by the one number the
// contract ships, and printed so the cap can be tuned from evidence.
bool run_chronicle_rate_smoke(App& app) {
    if (!smoke_boot_invariants_hold(app)) {
        smoke_print_counts(app, "chronicle_rate_boot_failed");
        smoke_fail(app, "chronicle_rate boot invariants");
        return false;
    }
    if (app.subworld.active()) {
        smoke_fail(app, "chronicle_rate while subworld active");
        return false;
    }
    smoke_clear_modal_overlays(app);

    const std::uint32_t seqBefore = app.gs.chronicle.nextSeq;
    const int dayBefore = app.gs.worldTime.day();
    // One whole game day of honest ticks (S3: day = 8192).
    const RuntimeFrameStats stats = advance_sim_steps(
        app,
        int(sm::ticks_to_advance_minutes(app.gs.worldTime.tick, 24 * 60)),
        false);
    const std::uint32_t factsInDay = app.gs.chronicle.nextSeq - seqBefore;
    std::printf("[smoke] chronicle_rate day=%d->%d factsInDay=%u "
                "factsToday=%u annals=%zu ringCap=%u\n",
                dayBefore, app.gs.worldTime.day(), unsigned(factsInDay),
                unsigned(app.gs.chronicle.factsToday),
                app.gs.chronicle.annals.size(),
                unsigned(sm::kChronicleFacts));
    // The tuning information itself: WHICH kinds carry the volume. Walk the
    // ring for the window's records (they are the newest; eviction cannot
    // have touched them unless the day overflowed the whole ring).
    {
        std::array<unsigned, std::size_t(sm::FactKind::Count)> byKind{};
        for (const sm::WorldFact& f : app.gs.chronicle.ring) {
            if (f.seq < seqBefore || f.seq >= app.gs.chronicle.nextSeq)
                continue;
            if (f.kind < std::uint16_t(sm::FactKind::Count)) ++byKind[f.kind];
        }
        std::printf("[smoke] chronicle_rate kinds:");
        for (std::size_t k = 0; k < byKind.size(); ++k) {
            if (byKind[k] == 0u) continue;
            std::printf(" %s=%u",
                        sm::fact_kind_def(sm::FactKind(k)).key, byKind[k]);
        }
        std::printf("\n");
    }
    std::fflush(stdout);
    if (!stats.ticked) {
        smoke_fail(app, "chronicle_rate advanced nothing");
        return false;
    }
    // A living world writes SOMETHING in a day (deaths, deals, hunger). A
    // silent day means every writer came unplugged — the exact regression
    // this instrument exists to catch.
    if (factsInDay == 0) {
        smoke_fail(app, "a whole world day passed and the chronicle heard "
                        "nothing: the writers are unplugged");
        return false;
    }
    return true;
}

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
    if (sm::spellbook_learned_count(app.gs.player.spellBook)
        != sm::kSpellCount) {
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
            // Walk the NAMED scores, not a hand-listed nine: an attribute the
            // game names later joins this sum by existing, and the reserved
            // tail of the envelope is not a score anybody spent.
            int sum = 0;
            for (int i = 0; i < int(sm::AttributeId::Count); ++i)
                sum += a.of(sm::AttributeId(i));
            return sum;
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
                     sheet->skills.of(sm::SkillId::Fighter),
                     sheet->skills.of(sm::SkillId::Bodybuilding),
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
    const std::size_t rSpells =
        std::size_t(sm::spellbook_learned_count(app.gs.player.spellBook));
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
            const sm::Landmark* firstCity = smoke_first_city(app);
            if (!firstCity) {
                smoke_fail(app, "open_settlement_build without settlements");
                break;
            }
            smoke_clear_modal_overlays(app);
            const sm::Landmark& s = *firstCity;
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
            const sm::Landmark* firstCity = smoke_first_city(app);
            if (!firstCity) {
                smoke_fail(app, "open_settlement_trade without settlements");
                break;
            }
            smoke_clear_modal_overlays(app);
            const sm::Landmark& s = *firstCity;
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
            const sm::Landmark* firstCity = smoke_first_city(app);
            if (!firstCity) {
                smoke_fail(app, "open_settlement_map without settlements");
                break;
            }
            smoke_clear_modal_overlays(app);
            const sm::Landmark& s = *firstCity;
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
            const sm::Landmark* firstCity = smoke_first_city(app);
            if (!firstCity) {
                smoke_fail(app, "enter_first_settlement without settlements");
                break;
            }
            if (app.subworld.active()) {
                app.subworld.leave(true);
            }
            smoke_clear_modal_overlays(app);
            const sm::Landmark& s = *firstCity;
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
            // ...AND THE WORLD REMEMBERED IT. This is the chronicle's first
            // end-to-end proof (CANON S20.1): a fight happened on a cell, and
            // the question the whole system exists to answer — "what happened
            // near here recently" — must now come back with it. A chronicle
            // that files facts nobody can find is a chronicle in name only.
            struct Traces { int n = 0; int bodies = 0; };
            Traces traces;
            sm::chronicle_near(
                app.gs.chronicle,
                sm::wrapi(int(app.gs.player.x), app.gs.mapW),
                sm::wrapi(int(app.gs.player.y), app.gs.mapH),
                /*radiusCells*/1, /*sinceDay*/0,
                [](void* u, const sm::WorldFact& f) {
                    if (f.kind != std::uint16_t(sm::FactKind::Killed)) return;
                    Traces& t = *static_cast<Traces*>(u);
                    ++t.n;
                    t.bodies += f.amount;
                }, &traces);
            std::printf("[smoke] force_encounter resolved enemyGone=%d "
                        "enemyHurt=%d playerPaid=%d traces=%d bodies=%d "
                        "chronicle_today=%u\n",
                        int(enemyGone), int(enemyHurt), int(playerPaid),
                        traces.n, traces.bodies,
                        unsigned(app.gs.chronicle.factsToday));
            std::fflush(stdout);
            // A battle that killed somebody MUST have left a trace. If nothing
            // died, there is nothing to remember, and demanding a trace would
            // be demanding the world lie.
            const bool somethingDied = enemyGone || enemyHurt || playerPaid;
            if (somethingDied && traces.n == 0) {
                smoke_fail(app, "the world fought and remembered nothing: the "
                                "witcher would find no trail");
                break;
            }
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
            // Two honest outcomes (CANON S4, 2026-08-29): the macro entity
            // stands marked Dead at hp 0, OR it has already LEFT the map —
            // the end-of-tick sweep (destroy_dead_macro_squads) reaps a
            // drained Dead squad, and the macro clock still ticks (slowly)
            // underground, so which of the two we observe is timing. What
            // would fail is the old bug: alive-and-whole on the map.
            const bool gone = !reg.valid(app.smoke.trackedMacro);
            const bool dead = gone
                || (reg.any_of<sm::ecs::Dead>(app.smoke.trackedMacro)
                    && reg.get<sm::ecs::Health>(app.smoke.trackedMacro).hp
                           <= 0.0f);
            std::fprintf(stderr, "[smoke] macro entity dead=%d gone=%d\n",
                         dead ? 1 : 0, gone ? 1 : 0);
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
                         app.gs.player.sheet.attributes.of(sm::AttributeId::Vit),
                         app.gs.player.sheet.skills.of(sm::SkillId::Bodybuilding),
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
            const int beforeVit = app.gs.player.sheet.attributes.of(sm::AttributeId::Vit);
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
                || app.gs.player.sheet.attributes.of(sm::AttributeId::Vit) != beforeVit + 1
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
                         app.gs.player.sheet.attributes.of(sm::AttributeId::Vit),
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
            const int beforeRank = app.gs.player.sheet.skills.of(sm::SkillId::Bodybuilding);
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
                || app.gs.player.sheet.skills.of(sm::SkillId::Bodybuilding) != beforeRank + 1
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
                         app.gs.player.sheet.skills.of(sm::SkillId::Bodybuilding),
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
        case SmokeAction::ChronicleRate:
            std::fprintf(stderr, "[smoke] action=chronicle_rate\n");
            std::fflush(stderr);
            if (run_chronicle_rate_smoke(app)) ++app.smoke.cursor;
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
            const int activeOrd = book.activeSpell;
            const float cd = sm::spell_ordinal_ok(activeOrd)
                ? sm::seconds_from_steps(book.cooldownSteps[activeOrd])
                : 0.0f;
            int sustainedCount = 0;
            for (int i = 0; i < sm::kSpellCount; ++i)
                sustainedCount += book.sustained[i] ? 1 : 0;
            std::fprintf(stderr,
                         "[smoke] spell_overlay learned=%d active=%s mp=%d/%d cd=%.2f sustained=%d\n",
                         sm::spellbook_learned_count(book),
                         sm::spell_ordinal_ok(activeOrd)
                             ? sm::kSpellDefs[activeOrd].id : "(none)",
                         app.gs.player.combatStats.currentMp,
                         app.gs.player.combatStats.maxMp,
                         cd,
                         sustainedCount);
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
            sm::spellbook_learn(app.gs.player.spellBook, sm::spell_ordinal("magic_bolt"));
            sm::spellbook_set_active(app.gs.player.spellBook, sm::spell_ordinal("magic_bolt"));
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
                || spellEvent->s1
                       != sm::kSpellDefs[app.gs.player.spellBook.activeSpell].id
                || spellEvent->a != sm::stable_spell_id(
                    sm::kSpellDefs[app.gs.player.spellBook.activeSpell].id)) {
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
                         sm::spell_ordinal_ok(book.activeSpell)
                             ? sm::kSpellDefs[book.activeSpell].id : "(none)",
                         beforeProjectiles,
                         afterProjectiles,
                         liveProjectiles,
                         targetHp ? double(targetHp->hp) : -1.0,
                         app.gs.player.combatStats.currentMp,
                         std::size_t(sm::spell_ordinal_ok(book.activeSpell)
                                         ? book.cooldownSteps[book.activeSpell]
                                         : 0u),
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
            sm::spellbook_learn(app.gs.player.spellBook, sm::spell_ordinal(boltSpell));
            sm::spellbook_set_active(app.gs.player.spellBook, sm::spell_ordinal(boltSpell));
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
            sm::spellbook_learn(app.gs.player.spellBook, sm::spell_ordinal("haste"));
            sm::spellbook_set_active(app.gs.player.spellBook, sm::spell_ordinal("haste"));
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
                app.gs.player.spellBook, sm::spell_ordinal("haste"));
            const int afterMp = app.gs.player.combatStats.currentMp;
            // ...and it MAKES HIM FASTER. The smoke used to prove only that
            // mana drained, so the whole reason to cast it went unmeasured —
            // and the ×1.5 that delivered it lived as a literal beside the
            // pace formula, where no test could see it either. The pace is
            // read the way the game reads it: off his effective sheet.
            const sm::CharacterSheet hasted = player_effective_sheet(app);
            const float hastePace =
                sm::calculate_derived(hasted.attributes, hasted.skills)
                    .moveSpeedMult;
            const float basePace =
                sm::calculate_derived(app.gs.player.sheet.attributes,
                                      app.gs.player.sheet.skills)
                    .moveSpeedMult;
            if (!active || afterMp >= beforeMp || !(hastePace > basePace)) {
                smoke_fail(app, "haste sustained drain/speed invariant");
                break;
            }
            std::fprintf(stderr,
                         "[smoke] sustained_haste active=%d mp=%d->%d carry=%.3f "
                         "pace=%.4f->%.4f\n",
                         active ? 1 : 0,
                         beforeMp,
                         afterMp,
                         app.gs.player.spellBook.sustainedDrainCarry,
                         double(basePace), double(hastePace));
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
            sm::spellbook_learn(app.gs.player.spellBook, sm::spell_ordinal("flight"));
            sm::spellbook_set_active(app.gs.player.spellBook, sm::spell_ordinal("flight"));
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
                app.gs.player.spellBook, sm::spell_ordinal("flight"));
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

            sm::spellbook_learn(app.gs.player.spellBook, sm::spell_ordinal("haste"));
            sm::spellbook_learn(app.gs.player.spellBook, sm::spell_ordinal("flight"));
            if (!sm::spellbook_has_sustained(
                    app.gs.player.spellBook, sm::spell_ordinal("haste"))) {
                sm::spellbook_set_active(app.gs.player.spellBook, sm::spell_ordinal("haste"));
                if (!cast_active_spell(app)) {
                    smoke_fail(app, "prepare_spell_auras haste failed");
                    break;
                }
            }
            if (!sm::spellbook_has_sustained(
                    app.gs.player.spellBook, sm::spell_ordinal("flight"))) {
                sm::spellbook_set_active(app.gs.player.spellBook, sm::spell_ordinal("flight"));
                if (!cast_active_spell(app)) {
                    smoke_fail(app, "prepare_spell_auras flight failed");
                    break;
                }
            }

            app.subworld.set_flying(true);
            RuntimeFrameStats frameStats =
                advance_sim_seconds(app, 0.05f, false);
            const bool haste = sm::spellbook_has_sustained(
                app.gs.player.spellBook, sm::spell_ordinal("haste"));
            const bool flight = sm::spellbook_has_sustained(
                app.gs.player.spellBook, sm::spell_ordinal("flight"));
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

} // namespace sm::app

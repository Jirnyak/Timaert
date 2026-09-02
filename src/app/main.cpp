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
#include "macro/world_gen.h"
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
#include "app/app_state.h"
#include "app/smoke.h"

#include "imgui.h"
#include "backends/imgui_impl_sdl2.h"
#include "backends/imgui_impl_vulkan.h"
#include <vulkan/vulkan.h>

// The screenshot PNG encoder (stb impl) moved to app/smoke.cpp with its one
// caller, write_smoke_frame_png.

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

// Named, not anonymous: app/smoke.cpp defines the smoke harness inside this
// same namespace and calls the shared services declared in app/app_state.h,
// so the definitions here need external linkage.
namespace sm::app {

// kSaveFileName/kAutosaveFileName/kPrefsFileName/kKeymapFileName and
// kPendingPresentationMax live in app/app_state.h beside App, whose member
// defaults they seed.
constexpr const char* kSaveOrgName = "Timaert";
constexpr const char* kSaveAppName = "timaert_c";
constexpr int kSubworldDailyTicksPerStep = 1;
// Macro NPCs dispatched per simulation STEP while the player is underground —
// a budget, so a huge macro population cannot make one step unbounded. Named
// for the step, not the frame: since the world runs on a fixed tick a frame may
// carry several steps or none.
constexpr int kSubworldMacroNpcTicksPerStep = 64;
constexpr float kSubworldHitFlashSeconds = 0.30f;
// Macro-map zoom: one step factor and one clamp, shared by the mouse wheel
// and the toolbar buttons (the four literals used to be written out at both).
constexpr float kMacroZoomStep = 1.15f;
constexpr float kMacroZoomMin = 4.0f;
constexpr float kMacroZoomMax = 96.0f;
// Smoke env-var names live in app/smoke.h; the smoke pacing constants live
// in app/smoke.cpp with the harness.

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

// SmokeAction/SmokeScript live in app/smoke.h; App itself in app/app_state.h
// (the smoke harness TU shares both).


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
// An ECS entity's SAVE-STABLE identity, which is what a fact must carry: the
// registry is never serialised, so entity bits mean nothing tomorrow while a
// MacroSpawnId means the same body for the life of the world. 0 = this body
// has no identity — a roster row, a body below — and the chronicle will say
// so by naming the place instead of a figure.
std::uint32_t macro_identity_of(const App& app, std::uint32_t entityBits) {
    if (entityBits == 0u) return 0u;
    const entt::entity e = entt::entity(entityBits);
    if (!app.ecs.reg.valid(e)) return 0u;
    const auto* id = app.ecs.reg.try_get<sm::ecs::MacroSpawnId>(e);
    return id ? id->index : 0u;
}

// Has this band done enough to be somebody? ONE number answers, and it lives
// on the band (ecs::MacroNpcRuntime::renown), so there is no flag beside it to
// disagree. A body with no runtime — a roster row, a corpse — is nobody.
bool squad_is_named(const App& app, entt::entity e) {
    if (!app.ecs.reg.valid(e)) return false;
    const auto* rt = app.ecs.reg.try_get<sm::ecs::MacroNpcRuntime>(e);
    return rt && sm::renown_is_named(rt->renown);
}

// (STANDING and THE deed door moved to the macro layer — macro/squad.h
// renown_slot / renown_of / grant_renown / record_deed, beside their landmark
// twin record_landmark_fact in state.h — so the app, the UI and the subworld
// engine all knock on ONE door. The one-door law of S20.1 had drifted across
// two layers, and the drift was writers that filed facts for free.)

// The app-side lending of NAMES to the chronicle (sm::FactNaming): the
// chronicle speaks ordinals and must not learn how the world names things,
// so the app lends it three resolvers (chronicle.h, "SAYING IT IN WORDS").
// Returned pointers are c_str() of live GameState strings — valid for the
// frame the sentence is rendered in, which is the only life a sentence has.
sm::FactNaming app_fact_naming(App& app) {
    sm::FactNaming n{};
    n.user = &app.gs;
    n.squad = [](void* u, std::uint32_t ordinal) -> const char* {
        (void)u;
        // Squads have no display names yet (a lord's name is a future
        // column); the one the journal's reader IS gets the honest pronoun.
        return ordinal == sm::ecs::kPlayerSquadOrdinal ? "You" : nullptr;
    };
    n.landmark = [](void* u, std::uint32_t id) -> const char* {
        auto& gs = *static_cast<sm::GameState*>(u);
        const sm::Landmark* lm = sm::landmark_by_id(gs, int(id));
        if (lm && !lm->name.empty()) return lm->name.c_str();
        return nullptr;   // a spire has no name of its own — "a place"
    };
    n.faction = [](void* u, std::uint32_t index) -> const char* {
        (void)u;
        return index < std::uint32_t(sm::kFactionCount)
                   ? sm::kFactionDefs[index].name
                   : nullptr;
    };
    return n;
}

void raise_macro_fact(void* user, const sm::BattleFact& fact) {
    auto& app = *static_cast<App*>(user);
    if (fact.kind != sm::BattleFact::Kind::Death) return;
    sm::GameEvent ev{sm::EventTag::NpcDeath};
    ev.a = fact.victim;
    ev.b = fact.killer;
    ev.ix = fact.npcType < std::uint16_t(sm::NPCType::Count)
        ? int(fact.npcType) : sm::kNoNpcType;
    app.bus.emit(ev);

    // ...AND THE WORLD REMEMBERS IT. This is the first real writer into the
    // chronicle (CANON S20.1), and it is the one that matters most for the
    // owner's test case: a monster squad that keeps killing near a village
    // leaves exactly these traces, and the witcher finds it by them.
    //
    // The killer is the SUBJECT because a chronicle records deeds, not
    // misfortunes; a killer with no identity (a roster row settling its own
    // side's losses) leaves the place itself as the subject, which is how a
    // nameless slaughter still shows up on the trail without pretending
    // somebody famous did it.
    sm::WorldFact wf{};
    wf.day = app.gs.worldTime.day();
    wf.kind = std::uint16_t(sm::FactKind::Killed);
    const std::uint32_t victimId = macro_identity_of(app, fact.victim);
    // A killer with no identity leaves the PLACE as the subject; `record_deed`
    // below overwrites this when the killer turns out to be somebody.
    wf.subjectKind = std::uint8_t(sm::FactSubject::Cell);
    if (victimId != 0u) {
        wf.objectKind =
            sm::fact_subject(sm::FactSubject::Squad,
                             squad_is_named(app, entt::entity(fact.victim)));
        wf.object = victimId;
    }
    wf.amount = 1;
    // WHERE it happened: the killer's cell if he has one, else the victim's.
    // A fact with no place would be invisible to the only question the
    // chronicle exists to answer.
    const entt::entity where = fact.killer != 0u
        ? entt::entity(fact.killer) : entt::entity(fact.victim);
    if (app.ecs.reg.valid(where)) {
        if (const auto* p = app.ecs.reg.try_get<sm::ecs::Position>(where)) {
            wf.x = std::int16_t(sm::wrapi(int(p->x), app.gs.mapW));
            wf.y = std::int16_t(sm::wrapi(int(p->y), app.gs.mapH));
        }
    }
    // Filed AND paid, through the one door (macro/squad.h): the deed makes
    // something of the doer. A nameless band that keeps doing notable things
    // crosses the bar and becomes a figure, after which its deeds go into the
    // annals. Before that they are weather — which is exactly why sixteen
    // thousand bands do not drown the world's memory.
    sm::record_deed(app.ecs, app.gs, wf,
                    fact.killer != 0u ? entt::entity(fact.killer)
                                      : entt::null);
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
    mw.nav      = &app.navWorld;
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

// ── Smoke harness ────────────────────────────────────────────
// The tokenizer, every run_*_smoke scenario, run_console_smoke and
// tick_smoke_script live in app/smoke.cpp (interface: app/smoke.h).

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


// ── Boot ──────────────────────────────────────────────────────

std::uint32_t choose_new_game_seed(const App& app) {
    std::uint32_t seed = 0;
    if (app.smoke.enabled && parse_u32(std::getenv(kSmokeSeedEnv), seed)) {
        return seed;
    }
    return std::uint32_t(SDL_GetTicks()) ^ 0xC0FFEEu;
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

// The save lives beside the prefs, in the same per-user pref dir — ONE path.
// The old exe-dir fallback and its copy-forward migration are gone: there is
// no save compatibility (CANON S21), so there is nothing worth migrating.
std::string resolve_save_path() { return resolve_prefs_path(kSaveFileName); }

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


const sm::Landmark* settlement_by_id(const sm::GameState& gs, int id) {
    const sm::Landmark* lm = sm::landmark_by_id(gs, id);
    return (lm && lm->type == sm::LandmarkType::City) ? lm : nullptr;
}

int settlement_at_player(const sm::GameState& gs, float radius = 3.0f) {
    const float r2 = radius * radius;
    for (const auto& s : gs.landmarks) {
        if (s.type != sm::LandmarkType::City) continue;
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
        const sm::Landmark* previous = settlement_by_id(app.gs, previousId);
        sm::GameEvent leave{sm::EventTag::PlayerLeaveSettlement};
        leave.a = std::uint32_t(previousId);
        leave.ix = previousId;
        if (previous) leave.s1 = previous->name;
        app.bus.emit(leave);
    }
    app.gs.subState.settlementId = id;
    if (app.ui.settlement) app.ui.settlementId = id;
    if (id < 0) return;
    const sm::Landmark* s = settlement_by_id(app.gs, id);
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
    const sm::Landmark* s = settlement_by_id(app.gs, id);
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

// The player's side of the resolver — assembled by THE one door every other
// squad's side goes through (macro/squad.h auto_battle_side_of), because his
// squad is an ordinary squad. This function used to be that door's hand-built
// twin: it restated health-as-a-fraction, fatigue-as-sp-over-max and the
// roster lookup in its own words, reading PlayerState where the door read the
// entity — two answers about one squad, free to disagree the moment either
// moved. All that is left of it is the ONE number macro is not allowed to
// know: what a swing of his is worth, which is the subworld's melee identity
// (sub/engine.h) and belongs to the layer that can see both worlds.
sm::AutoBattleSide player_auto_battle_side(App& app) {
    const sm::PlayerState& p = app.gs.player;
    sm::AutoBattleSide s = sm::auto_battle_side_of(
        app.ecs, sm::player_squad_entity(app.ecs), &p.sheet);
    const float swing = std::floor(
        sm::sub::kPlayerBaseMeleeDamage
        + sm::calculate_derived(p.sheet.attributes, p.sheet.skills)
              .rawPhysDamage);
    s.leaderDpsOverride = swing / sm::sub::kPlayerMeleeCooldown;
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
    // Session words die with the moment (v58): the fading HUD feed, never
    // the save. What the world remembers went through the chronicle already.
    sm::session_feed_push(app.gs.sessionFeed, msg.c_str());
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
         // later and it is there. What his bag refuses stays yours, and the
         // line reports what actually changed hands.
         int paid = cost;
         if (auto* bag =
                 app.ecs.reg.try_get<sm::ecs::NpcInventory>(npc)) {
             paid = sm::transfer_value(player_bag(app), bag->inv, cost);
         } else {
             paid = sm::wallet_spend_up_to(player_bag(app), cost);
         }
         char line[96];
         std::snprintf(line, sizeof(line),
                       "Paid %d gold to be let through.", paid);
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

float& player_sp_carry(App& app);   // defined with the world-query helpers

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
                                            player_sp_carry(*ctx->app),
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

// His carry, through the one door. The scratch fallback is the same shape
// player_bag uses: a frame before the world exists must not crash, and a
// carry with nowhere to live is a carry nobody reads.
float& player_sp_carry(App& app) {
    static float scratch = 0.0f;
    float* carry = sm::player_sp_carry(app.ecs);
    return carry ? *carry : scratch;
}

// EVERYTHING STANDING ON THE PLAYER, summed once.
//
// His sheet is the character he built; what fights is that character PLUS what
// he is wearing and what is currently burning on him. Both are rows of the one
// bonus registry, so both are the same sum — and because the sum is read
// through a modified COPY (`effective_sheet`), taking the coat off or letting
// the spell lapse is simply not adding it next time.
//
// This door is why haste stopped being a literal. It used to be `×1.5` spelled
// at two call sites, keyed by a string compare on "haste", duplicating the
// number the spell's own row already carried — a fourth place the same fact
// lived. Now the row says "+20 SPD at a novice's hand", the caster's training
// scales it (spell_bonus), and the pace formula simply reads a sheet.
sm::BonusTotals player_standing_bonuses(const App& app) {
    sm::BonusTotals t{};
    // What he wears. Opt-in: a player who has equipped nothing has no
    // component, and the limiting case costs a lookup.
    if (const entt::entity e = sm::player_squad_entity(
            const_cast<sm::ecs::World&>(app.ecs));
        e != entt::null) {
        if (const auto* eq =
                app.ecs.reg.try_get<sm::ecs::BodyEquipment>(e)) {
            const sm::BonusTotals worn = sm::worn_bonuses(eq->gear);
            for (int i = 0; i < sm::kMaxAttributes; ++i)
                t.attr[std::size_t(i)] += worn.attr[std::size_t(i)];
            for (int i = 0; i < sm::kMaxSkills; ++i)
                t.skill[std::size_t(i)] += worn.skill[std::size_t(i)];
        }
    }
    // ...and what is burning on him. A sustained spell contributes while it
    // burns and stops the moment it does not — no bookkeeping, because nothing
    // was ever written down.
    for (int ord = 0; ord < sm::kSpellCount; ++ord) {
        if (!app.gs.player.spellBook.sustained[ord]) continue;
        const sm::SpellDef* def = &sm::kSpellDefs[ord];
        for (const sm::Bonus& b : def->effects) {
            sm::accumulate(t, sm::spell_bonus(b, app.gs.player.sheet.skills));
        }
    }
    return t;
}

// The sheet the world should actually ask about him.
sm::CharacterSheet player_effective_sheet(const App& app) {
    return sm::effective_sheet(app.gs.player.sheet,
                               player_standing_bonuses(app));
}

// Is a RULE of the world switched on for him right now? A rule has no
// magnitude to scale, so this is a row lookup and a boolean, not a number —
// the one scan beside the SpellBook itself (macro/spell_book_state.h).
bool player_rule_active(const App& app, sm::SpellRuleId rule) {
    return sm::spellbook_rule_active(app.gs.player.spellBook, rule);
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
    player_sp_carry(app) = 0.0f;
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
        sm::session_feed_push(app.gs.sessionFeed,
                              "You cannot save underground - return to the map.");
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
    sm::session_feed_push(app.gs.sessionFeed, msg);
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
    for (const auto& lm : app.gs.landmarks) {
        if (lm.type == sm::LandmarkType::City)
            zsCities.push_back({lm.x, lm.y});
        else if (lm.type == sm::LandmarkType::Village)
            zsVills.push_back({lm.x, lm.y});
    }
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

    sm::reset_player_recovery(app.playerRecovery);
    player_sp_carry(app) = 0.0f;
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

    // THE one macro-world baker (macro/world_gen.h, CANON S8/S26): bodily the
    // old boot sequence, extracted 2026-08-30 so the headless balance harness
    // raises the SAME world through the SAME function. App-side below stays
    // only what a pure world cannot own: the bus, renderers, camera, chargen.
    sm::WorldGenParams gp{};
    gp.seed = seed;
    gp.mapW = mapW;
    gp.mapH = mapH;
    gp.lpOverride = lpOverride;
    gp.targetTotalCities = targetTotalCities;
    gp.spawnMacroNpcs = spawnMacroNpcs;
    gp.trace = boot_trace_enabled();
    sm::WorldGenOut go{};
    go.gs           = &app.gs;
    go.terrain      = &app.terrain;
    go.trees        = &app.trees;
    go.treeLayer    = &app.treeLayer;
    go.deposits     = &app.deposits;
    go.features     = &app.features;
    go.zones        = &app.zones;
    go.treeGrid     = &app.treeGrid;
    go.landmarkGrid = &app.landmarkGrid;
    go.pathCost     = &app.pathCost;
    go.world        = &app.ecs;
    sm::generate_macro_world(go, gp);
    boot_trace("macro world generated");
    // The bus is the DOOR into the world's memory; the memory itself is the
    // world's. Attached after genesis so a fresh world and a loaded one both
    // have one, pointing at the chronicle that lives the whole session.
    app.bus.attach_chronicle(&app.gs.chronicle);

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
    app.cursor = sm::ui::MacroCursor{};
    // Anchor camera at the player's CELL CENTRE (cell N spans [N..N+1]
    // in world units, so its centre sits at N+0.5). The per-sprite
    // +0.5 in macro_overlay.cpp lines up with this so the player +
    // every NPC render at their cell centre, never at the cell
    // crossing. (The player himself is anchored by generate_macro_world.)
    app.camX = app.camTargetX = app.gs.player.x + 0.5f;
    app.camY = app.camTargetY = app.gs.player.y + 0.5f;
    app.camPanX = app.camPanY = 0;
    boot_trace("camera anchored");
    // The starter kit is DEALT INTO HIS BAG, which is a container on his squad
    // entity — a PlayerState cannot carry goods any more, because it is not a
    // container. Coin of his own realm follows at chargen (the homeland pick
    // re-mints it), so the opening purse is imperial exactly as it always was.
    // Chargen content, so it stays app-side — a balance-harness world gets a
    // player squad but no gift.
    if (sm::Inventory* bag = sm::player_inventory(app.ecs)) {
        bag->add("coin_empire", 1000);
        bag->add("potion_hp", 2);
        bag->add("bread", 5);
    }
    boot_trace("starter kit dealt");
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
    app.gs.nextQuestOrdinal      = fresh.nextQuestOrdinal;        // quest issuer (v63)
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
    app.gs.landmarks         = std::move(fresh.landmarks);
    app.gs.markers           = std::move(fresh.markers);
    // The explored map (v40). Visible cells were clamped away on write; the
    // first sight sweep after this load re-opens them from the restored
    // position (destroy_world invalidated the sight anchor).
    app.gs.knowledge         = std::move(fresh.knowledge);
    // The world's own memory comes back with it. `boot_world` above sized a
    // FRESH chronicle for this map; the saved one replaces it, and the bus's
    // door still points at the same member — the world owns the past, the bus
    // only writes to it.
    app.gs.chronicle         = std::move(fresh.chronicle);
    app.gs.relations         = fresh.relations;
    app.gs.subState          = std::move(fresh.subState);
    app.gs.deserterPool      = fresh.deserterPool;
    // Features built by squads (v71): the list is the truth; the grid gets
    // them re-stamped below, before the rebaker reads it.
    app.gs.builtFeatures     = std::move(fresh.builtFeatures);
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
    // Features BUILT BY SQUADS (v71, CANON S10): boot_world baked the SEED's
    // virgin grid; re-stamp the crews' works onto it BEFORE the rebaker
    // reads features (zones, cost). The list is append-order — the same
    // order the crews built in.
    for (const sm::BuiltFeature& bf : app.gs.builtFeatures) {
        const int wx = sm::FeatureLayer::wrap_coord(bf.x, app.features.width);
        const int wy = sm::FeatureLayer::wrap_coord(bf.y, app.features.height);
        app.features.data[std::size_t(wy) * std::size_t(app.features.width)
                          + std::size_t(wx)] = sm::FeatureType(bf.ft);
    }
    rebake_world(app);
    app.macro.upload_tree_field(app.device, &app.treeLayer);
    app.uploadedTreeRev = app.treeLayer.revision;
    return true;
}

// ── Input ─────────────────────────────────────────────────────

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
// (The PauseReason bits live in app/app_state.h beside App.)

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
    // (No `s2 = reason`. Six call sites filled it and NOBODY has ever read it
    // — not production, not a test, not the harness. A string built and
    // deep-copied into the tick buffer and the history for no reader at all.)
    (void)reason;
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
        sm::overload_charge(app.gs.player.sheet, player_bag(app)).cost;
    const float cost = sm::travel_stamina_cost(
        app.subworld.player_ground_travel_weight(), cells, overloadCost,
        sm::travel_skill_efficiency(app.gs.player.sheet.skills));
    return sm::spend_travel_stamina(app.gs.player.combatStats,
                                    player_sp_carry(app), cost);
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
// The SESSION FEED (state.h SessionFeed): the M&M message strip — the last
// few session words fading out near the bottom of the screen, newest lowest.
// Presentation only, and this draw is also where a line's ttl burns down:
// screen time is the only time a session word has (owner, 2026-08-28).
void draw_session_feed(App& app) {
    sm::SessionFeed& feed = app.gs.sessionFeed;
    const ImGuiIO& io = ImGui::GetIO();
    ImDrawList* fg = ImGui::GetForegroundDrawList();
    const float lineH = ImGui::GetTextLineHeightWithSpacing();
    int shown = 0;
    for (int i = 0; i < sm::SessionFeed::kLines; ++i) {
        const int idx = (int(feed.head) - 1 - i + 2 * sm::SessionFeed::kLines)
                        % sm::SessionFeed::kLines;
        sm::SessionFeed::Line& l = feed.lines[idx];
        if (l.ttl <= 0.0f || l.text[0] == '\0') continue;
        l.ttl -= io.DeltaTime;
        const float a = l.ttl < 1.0f ? (l.ttl < 0.0f ? 0.0f : l.ttl) : 1.0f;
        const ImVec2 size = ImGui::CalcTextSize(l.text);
        const float x = (io.DisplaySize.x - size.x) * 0.5f;
        const float y = io.DisplaySize.y - 172.0f - float(shown) * lineH;
        fg->AddText(ImVec2(x + 1.0f, y + 1.0f),
                    IM_COL32(0, 0, 0, int(150 * a)), l.text);
        fg->AddText(ImVec2(x, y),
                    IM_COL32(235, 226, 195, int(235 * a)), l.text);
        ++shown;
    }
}

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
    const int ord = app.gs.player.spellBook.activeSpell;
    if (!sm::spell_ordinal_ok(ord)) {
        emit_spell_cast(app, "", false, "No active spell");
        return false;
    }
    const sm::SpellDef* def = &sm::kSpellDefs[ord];
    const std::string id = def->id;   // the EVENT still speaks the string id

    const bool inMicro = app.subworld.active();
    const sm::CastCheck check = sm::spellbook_can_cast_ex(
        app.gs.player.spellBook, app.gs.player.combatStats, ord, inMicro);
    if (!check.ok) {
        emit_spell_cast(app, id, false, check.reason.c_str(),
                        check.cooldownRemaining);
        return false;
    }

    if (!inMicro) {
        // A world-map cast needs something the world map can DO with it: a
        // standing stat effect, or a rule to switch. Both are rows now.
        bool saysSomething = def->rule != sm::SpellRuleId::None;
        for (const sm::Bonus& b : def->effects) {
            if (b.row != 0 && b.value != 0) saysSomething = true;
        }
        if (!def->sustained || !saysSomething) {
            emit_spell_cast(app, id, false, "World-map spell effect not implemented");
            return false;
        }
        sm::spellbook_start_cast(app.gs.player.spellBook,
                                 app.gs.player.combatStats, ord);
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
        ord,
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
            // HANDS OFF THE KEYS — and that now means SAYING SO. While the
            // legs were a displacement applied per call, not calling was
            // enough: no call, no step. An intent PERSISTS, and several of
            // these conditions do not pause the world (the debug overlay and
            // a focused console are not pause reasons), so a silent return
            // would leave him marching at full pace with nobody at the keys —
            // burning stamina and refusing to rest while you type.
            app.subworld.set_move_intent(0.0f, 0.0f);
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
        // Same pace as on the map — one character, one speed, both layers.
        // And haste is IN that character now: it is +SPD on his effective
        // sheet, not a multiplier bolted on beside the formula, so a swift
        // ring and a swiftness spell are the same kind of fast.
        const sm::CharacterSheet eff = player_effective_sheet(app);
        const float pace = sm::calculate_derived(eff.attributes, eff.skills)
                               .moveSpeedMult;
        bool spellFlight = player_rule_active(app, sm::SpellRuleId::Flight);
        if (spellFlight != app.lastSpellFlight) {
            app.subworld.set_flying(spellFlight);
            app.lastSpellFlight = spellFlight;
        }
        // Space = jump (edge-triggered): an upward impulse through the same
        // vertical integrator as everything else; inert unless grounded.
        const bool jumpHeld = keys[app.keymap.get(ActionId::Jump)] != 0;
        if (jumpHeld && !app.lastJumpHeld) app.subworld.jump();
        app.lastJumpHeld = jumpHeld;
        // His legs state an INTENT (tiles/s), exactly like every brain in the
        // subworld writes one; the ONE mover turns it into a position, so he
        // pays the ground, the slope and the solids a peasant pays. No `dt`
        // here on purpose: an intent is a speed, not a displacement.
        //
        // A MAN'S pace, from the same scale every body is stated on: the
        // world's march (1.0 = what the map says a man walks) times his own
        // sheet. The private ×0.4 that used to sit here died with it — it was
        // a second speed law for one body, and the body it was for is an NPC
        // like any other (CANON S4).
        const float walk = sm::march_speed(sm::kHumanMarchMult) * pace;
        app.subworld.set_move_intent(dx * walk, dy * walk);
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
        const sm::CharacterSheet effSheet = player_effective_sheet(app);
        const float pace =
            sm::calculate_derived(effSheet.attributes, effSheet.skills)
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
            app, dt, kMarchCellsPerRealSecond * pace * ground);
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

    std::string born = "Born ";
    born += sex ? *sex : "unknown";
    born += ", homeland: ";
    // The RESOLVED realm, by its registry name: a player who picked "Barbarian
    // Kingdoms" is told which of them he was actually born in, because that is
    // the fact the world will hold him to.
    if (homeland) {
        const int fi = sm::faction_index(homeland);
        born += fi >= 0 ? sm::kFactionDefs[fi].name : homeland;
    } else {
        born += "unknown";
    }
    born += ".";
    sm::session_feed_push(app.gs.sessionFeed, born.c_str());

    app.logic.activate("plot_chapter_1");

    // (A "q_travel_*" first-steps dialog loop stood here for months — a TS
    // relic scanning for quests NOTHING in this codebase ever creates. Dead
    // code, removed with the quest id strings, 2026-08-29.)
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
            std::string line = "Encounter spawned in subworld: ";
            line += ev.s1.empty() ? ev.s2 : ev.s1;
            sm::session_feed_push(app.gs.sessionFeed, line.c_str());
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
            std::string line = "Word spreads of trouble near the marked area: ";
            line += ev.s1;
            sm::session_feed_push(app.gs.sessionFeed, line.c_str());
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
std::uint64_t quest_marker_signature(const std::vector<sm::Quest>& active) {
    std::uint64_t h = 1469598103934665603ull;            // FNV-1a offset basis
    auto mix = [&](std::uint64_t v) { h ^= v; h *= 1099511628211ull; };
    for (const sm::Quest& q : active) {
        mix(std::uint64_t(q.ordinal) + 1u);
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
    app.bus.flush();
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

// (RuntimeFrameStats lives in app/app_state.h; the smoke harness reads it.)

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
        // Where he stands BEFORE the world steps. The input below only states
        // an intent now — the ONE mover moves him inside subworld.tick, with
        // every other body — so the distance he actually covered is only
        // known after that tick, and the stamina for it is charged there
        // (see `movedThisStep` below).
        const float prevX = app.subworld.player_x();
        const float prevY = app.subworld.player_y();
        float movedThisStep = 0.0f;
        if (allowInput) {
            poll_movement(app, dt);
        } else {
            app.subworld.set_move_intent(0.0f, 0.0f);  // hands off the keys
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
                                  player_sp_carry(app),
                                  app.subworld.player_marching()
                                      ? sm::kMarchRecoveryPct : 1.0f);
        app.subworld.tick(dt);
        // He has now been moved — by the same pass that moved every other
        // body — so this is where the ground he actually covered is known,
        // and where his legs pay for it.
        {
            const float movedX = app.subworld.player_x() - prevX;
            const float movedY = app.subworld.player_y() - prevY;
            movedThisStep = std::sqrt(movedX * movedX + movedY * movedY);
            (void)charge_subworld_sp_for_distance(app, movedThisStep);
        }
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
        // ...and standing in the open sea is not resting either: a body that
        // cannot make camp cannot recover, which is the same sentence a macro
        // squad's think obeys (npc_ai.cpp settle_march_rhythm). It is what
        // keeps an ocean lethal now that the exhaustion bite is a law rather
        // than a water special case — stopping mid-crossing buys nothing.
        const bool marching = !app.cursor.path.empty();
        const bool resting = !marching && player_can_make_camp(app);
        sm::apply_minute_recovery(app.gs.player,
                                        stats.timeTick.minutesAdvanced,
                                        app.playerRecovery,
                                        player_sp_carry(app),
                                        resting ? 1.0f : sm::kMarchRecoveryPct);
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
    // The player LEARNS this tick's facts — participation and his own cell
    // (macro/journal.h). Runs on both sides of the seam: underground the
    // engine files into the very macro cell he stands in.
    sm::player_journal_capture(app.gs);
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
    std::size_t dbgVillages = 0;
    for (const auto& lm : app.gs.landmarks)
        if (lm.type == sm::LandmarkType::Village) ++dbgVillages;
    ImGui::Text("Kingdoms %zu  Cities %zu  Villages %zu",
                app.gs.politik.kingdoms.size(),
                app.gs.politik.cities.size(),
                dbgVillages);
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
            const sm::Landmark* found = nullptr;
            int id = 0;
            if (sm::dev::arg_int(a, 0, id))
                if (const sm::Landmark* lm = sm::landmark_by_id(app.gs, id);
                    lm && lm->type == sm::LandmarkType::City) found = lm;
            if (!found) {
                std::string q;
                for (std::size_t i = 0; i < a.size(); ++i) { if (i) q += ' '; q += a[i]; }
                for (const auto& s : app.gs.landmarks)
                    if (s.type == sm::LandmarkType::City
                        && console_icontains(s.name, q)) { found = &s; break; }
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
            count = std::clamp(count, 1, sm::sub::kMaxBodyCrowd / 2);

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
            if (sm::spellbook_learn(app.gs.player.spellBook,
                                    sm::spell_ordinal(id)))
                c.printfln(Lvl::Ok, "learned %s", id.c_str());
            else
                c.printfln(Lvl::Warn, "already knew %s", id.c_str());
            return true;
        });

    con.register_cmd("learnall", "learnall",
        "learn every spell in the registry",
        [&app](Con& c, const std::vector<std::string>&) {
            int learned = 0;
            for (int ord = 0; ord < sm::kSpellCount; ++ord)
                if (sm::spellbook_learn(app.gs.player.spellBook, ord)) ++learned;
            c.printfln(Lvl::Ok, "learned %d new spell(s); know %d total", learned,
                       sm::spellbook_learned_count(app.gs.player.spellBook));
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
            ImGui::Text("spells  %d learned",
                        sm::spellbook_learned_count(p.spellBook));
            ImGui::SeparatorText("World");
            ImGui::Text("clock   day %d, %02d:%02d",
                        app.gs.worldTime.day(), app.gs.worldTime.hour(),
                        app.gs.worldTime.minute());
            ImGui::Text("seed    %u", app.gs.worldSeed);
            ImGui::Text("map     %d x %d", app.gs.mapW, app.gs.mapH);
            std::size_t nCities = 0, nVillages = 0, nSpires = 0;
            for (const auto& lm : app.gs.landmarks) {
                if (lm.type == sm::LandmarkType::City) ++nCities;
                else if (lm.type == sm::LandmarkType::Village) ++nVillages;
                else if (lm.type == sm::LandmarkType::Spire) ++nSpires;
            }
            ImGui::Text("world   %zu settlements  %zu villages  %zu spires",
                        nCities, nVillages, nSpires);
            ImGui::SeparatorText("Dev");
            ImGui::Text("simspeed %.2fx", double(app.simSpeed));
            ImGui::Text("subworld %s", app.subworld.active() ? "active" : "-");
            ImGui::Text("godmode  %s", app.subworld.god_mode() ? "ON" : "off");
            ImGui::Text("flight   %s", app.subworld.flying() ? "ON" : "off");
        }
        ImGui::End();
    }

    // ── Journal: a VIEW on the chronicle (CANON S20.1) ─────────
    // What the player LEARNED — copies captured by macro/journal.h — said in
    // words derived at this very moment (fact_sentence + the app's naming
    // resolvers). No sentence is stored anywhere; change the tables and the
    // same past says itself differently.
    if (app.panels.journal) {
        ImGui::SetNextWindowSize(ImVec2(440, 300), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Journal", &app.panels.journal)) {
            const auto& j = app.gs.player.journal;
            ImGui::Text("%zu facts learned (showing last 200)", j.size());
            if (app.gs.player.journalFull) {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(1.f, 0.4f, 0.3f, 1.f),
                                   "[journal full]");
            }
            // The annals' cap is LOUD by contract (chronicle.h annalsFull:
            // a world whose legend outgrew its cap has a tuning problem, and
            // dropping history quietly is how you never find out) — but the
            // flag had no listener. Same channel as [journal full] above.
            if (app.gs.chronicle.annalsFull) {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(1.f, 0.4f, 0.3f, 1.f),
                                   "[world annals full]");
            }
            ImGui::Separator();
            if (ImGui::BeginChild("jlog")) {
                const sm::FactNaming naming = app_fact_naming(app);
                const std::size_t start = j.size() > 200 ? j.size() - 200 : 0;
                char line[192];
                for (std::size_t i = start; i < j.size(); ++i) {
                    sm::fact_sentence(j[i], naming, line, int(sizeof line));
                    ImGui::TextWrapped("[day %d] %s", j[i].day, line);
                }
                if (j.empty()) ImGui::TextDisabled("(nothing learned yet)");
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
            if (sm::spellbook_rule_active(app.gs.player.spellBook,
                                          sm::SpellRuleId::Flight)) {
                app.cursor.path = build_flight_path(sx, sy,
                    app.cursor.requestX, app.cursor.requestY,
                    app.gs.mapW, app.gs.mapH);
                app.cursor.pathIdx = 1; // skip current cell
            } else {
                sm::PathResult r = sm::find_path(app.pathCost, sx, sy,
                                                 app.cursor.requestX,
                                                 app.cursor.requestY,
                                                 app.pathScratch);
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
            draw_session_feed(app);
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
            if (app.uiSettings.visible(sm::ui::UiElementId::PanelQuestLog)) {
                // The Close button quotes the LIVE Quests binding (S22).
                const SDL_Scancode questsSc =
                    app.keymap.get(sm::ui::ActionId::Quests);
                sm::ui::draw_quest_log(app.gs,
                                       app.activeQuests,
                                       app.quests,
                                       app.bus,
                                       &app.ui.questSelection,
                                       &app.ui.quest,
                                       app.uiSettings.scale(sm::ui::UiElementId::PanelQuestLog),
                                       questsSc != SDL_SCANCODE_UNKNOWN
                                           ? SDL_GetScancodeName(questsSc)
                                           : nullptr);
            }
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

} // namespace sm::app

int main(int /*argc*/, char* /*argv*/[]) {
    using namespace sm::app;
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

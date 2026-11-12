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
#include "ecs/components.h"
#include "events/event_bus.h"
#include "events/effect_applicator.h"
#include "events/logic_nodes.h"
#include "events/node_registry.h"
#include "bus/local_frame.h" // локальный фрейм пяти пакетов (M-171, ломоть 4)
#include "macro/state.h"
#include "bus/session.h"   // Session — сессия: лента, кэш флажка, аренда (В1)
#include "bus/store.h"     // гладкая память макро-сквадов (M-106)
#include "macro/knowledge.h"
#include "ui/map_screen.h"
#include "macro/map_generator.h"
#include "macro/settlement_score.h"
#include "macro/spawners.h"
#include "macro/tree_layer.h"
#include "macro/nav_field.h"
#include "macro/deposit_layer.h"
#include "macro/spires.h"
#include "macro/zones.h"
#include "macro/vk_macro_renderer.h"
#include "macro/macro_lighting.h"
#include "tables/biomes.h"
#include "macro/world_tick.h"
#include "macro/npc_ai.h"
#include "bus/entry_context.h"
#include "tables/faction.h"
#include "macro/npc_spawn.h"
#include "macro/currency.h"
#include "macro/squad.h"
#include "macro/player_entity.h"
#include "macro/chronicle.h"   // ChronicleLinks — выводной индекс летописи
#include "macro/pathfinding.h"
#include "bus/anketa.h"
#include "macro/recovery.h"
#include "macro/travel.h"
#include "macro/audio.h"
#include "macro/save.h"
#include "tables/seasons.h"
#include "content/spells/casting.h"
#include "content/spells/spell_book.h"
#include "content/plot/intro.h"
#include "content/plot/scene.h"
#include "sub/engine.h"
#include "sub/damage.h"
#include "sub/objects.h"     // единый массив объектов сцены (M-150)
#include "sub/dgn/dispatch.h"
#include "sub/height.h"
#include "sub/map_data.h"
#include "sub/tree_atlas.h"
#include "macro/fauna.h"
#include "ui/overlays.h"
#include "ui/screens.h"
#include "ui/intro_screens.h"   // SplashState / IntroSlidesState (pre-world)
#include "ui/macro_overlay.h"
#include "ui/ui_gpu.h"
#include "ui/keymap.h"
#include "ui/ui_settings.h"
#include "app/debug_console.h"

#include "imgui.h"
#include <vulkan/vulkan.h>
#include "app/smoke.h"

namespace sm {

// РЕЖИМ ОБОЛОЧКИ — НЕ СОСТОЯНИЕ МИРА (M-190). Тип и поле жили в
// `GameState@src/macro/state.h` и ехали в сейве; с kSaveVersion 157 они здесь,
// на `App`, и в файл не едут: загрузка всегда встаёт в Exploring, а
// `settlementId` пересчитывается от места игрока
// (`settlement_at_player`). ОСТАТОК M-190 НЕ ИСПОЛНЕН: режим всё ещё ХРАНИТСЯ,
// а не выводится из открытых панелей по образцу паузы; значения Paused,
// Trading и ViewingMap прод-писателей не имеют.
enum class GameSubStateKind : std::uint8_t {
    Exploring, Paused, Trading, ViewingMap,
    // Forced pre-battle encounter (Session 15): a hostile squad on the map
    // stopped the player — the M&B screen is up and the world is paused. The
    // target squad is runtime App state (`App::preBattleNpc`).
    PreBattle,
};
struct GameSubState {
    GameSubStateKind kind = GameSubStateKind::Exploring;
    int settlementId = 0;   // 0 = без места (закон нуля-ординала)
};

} // namespace sm

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

    // Launch opens on the studio splash; returnToTitle lands on Title —
    // the splash plays once per run, never again mid-session.
    sm::ui::AppState state = sm::ui::AppState::Splash;
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
    // СЕССИЯ (`Session@src/bus/session.h`, вердикт владельца 2026-11-10, В1):
    // лента HUD, кэш носителя флажка игрока, счётчик события состава и аренда
    // окна. Жили членами `GameState` и уехали сюда: состояние мира снимается
    // прямым дампом, а эти четыре миром не являются. Симуляция видит сессию
    // адресом в конверте слоёв (`macro_world`), в сейв она не едет.
    sm::Session          session;
    // ЛОКАЛЬНЫЙ ФРЕЙМ пяти пакетов (ЗАКОН КАНОНИЧЕСКОГО ФРЕЙМА, ломоть 4):
    // один экземпляр на приложение, субмир держит на него const-указатель.
    // Заполняется ТОЛЬКО app-ом (fill_local_frame) в плановых точках: вход в
    // сцену, граница мирового тика под землёй, ре-центр/смена якоря
    // (needs_frame_resync), расхождение хэндлов игрока после его же
    // сквозной записи (вселение).
    sm::LocalContext     frame;
    sm::TerrainData      terrain{};
    // (Поле фич жило здесь членом оболочки и уехало в состояние мира —
    // `GameState::features@src/macro/state.h`, M-456: живой мир его пишет,
    // значит ему место там, откуда снимается сейв.)
    sm::ZoneLayer        zones;
    // (Живое поле леса жило здесь членом оболочки и уехало в состояние мира
    // — `GameState::treeLayer@src/macro/state.h`, ломоть 3 сейв-эпика.)
    // uploadedTreeRev mirrors gs.treeLayer.revision so the u_treeMap texture
    // refreshes only when a count actually changed.
    std::uint32_t        uploadedTreeRev = 0;
    // Mirrors gs.knowledge.revision so the u_knowledgeMap texture refreshes
    // only when sight actually moved (a cell crossing / a quest reveal).
    std::uint32_t        uploadedKnowledgeRev = 0;
    // Зеркало `gs.features.revision` (M-248 / M-457 п.7): текстура поля фич
    // перезаливается только когда байт какой-то клетки реально сменился —
    // город умер в руину, деревня сняла фичу, структура встала на клетку.
    std::uint32_t        uploadedFeatureRev = 0;
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
    // (Колонка `lastStandingBonuses` СНЕСЕНА — M-301. Она была памятью
    //  СТОРОЖА: «что стояло на игроке прошлым шагом», чтобы тик сам заметил
    //  смену и позвал дверь листа. Сторожа больше нет — пересчёт живёт внутри
    //  дверей классов изменений, — и помнить прошлый шаг стало незачем.
    //  Опасность, которой она оправдывалась — «плановая перезапись затопчет
    //  runtime, которого никто не трогал», — умерла раньше неё, вместе с
    //  кэшированными темпами регена.)
    // (`sm::ecs::World ecs` умер ломтём E вместе с реестром: тела сцены —
    // слоты `sceneObjects` ниже, store и арена ходят явными параметрами.)
    // ГЛАДКАЯ ПАМЯТЬ МАКРОМИРА (bus/store.h, M-106): колонки состояния
    // макро-сквадов. Родится один раз на старте приложения — преаллокация
    // мира (1460 МиБ по капу, вердикт владельца 2026-09-25).
    std::unique_ptr<sm::MacroStore> macroStore;
    // ИНДЕКС ЛЕТОПИСИ (M-423): три цепочки по клетке, субъекту и объекту —
    // выводное от лога фактов состояния мира, поэтому живёт здесь, а не в
    // `gs`, и в сейв не едет. Родится один раз на старте рядом со store
    // (48 МиБ по капу лога); догоняет лог сам, в момент вопроса.
    std::unique_ptr<sm::ChronicleLinks> chronicleLinks;
    // ЕДИНЫЙ МАССИВ ОБЪЕКТОВ СЦЕНЫ (sub/objects.h, M-150): постоянный
    // носитель слотов тел/трупов/снарядов окна; транзиент сессии — в сейв
    // не едет (сейв — только макро-слой, §10). Родится на старте рядом со
    // store; ходит ЯВНО (ломоть E: ctx-мост objects_attach умер с реестром).
    std::unique_ptr<sm::sub::SubObjects> sceneObjects;
    sm::EventBus         bus;
    sm::LogicNodeEngine  logic;
    // (Квест-машина снесена целиком 2026-10-23, ломоть 3 M-72 вариант А:
    //  QuestEngine, activeQuests, доска офферов и кэш маркеров умерли.
    //  Задание игрока вернётся конструктором задач на форме цели рода 2.)
    std::size_t          appliedEventCount = 0;
    std::size_t          appliedStoryResultCount = 0;
    // (appliedCombatEventCount died with EventTag::BattleStart — M-116;
    //  appliedSpawnEventCount — с тегом SpawnEntity, M-410.)
    // (No playerRecovery accumulator. The fractional HP/MP rest remainders
    // are Pools::hpCarry/mpCarry on the player's squad entity — the same
    // slots every lord's are, and unlike the App-side pair they ride the
    // save. Landing 4.)
    sm::MacroNpcAiRuntime npcAi;
    sm::sub::SubworldEngine subworld;
    sm::AudioSystem      audio;
    sm::MusicId          audioDesired = sm::MusicId::Count;
    sm::MusicId          audioFailed = sm::MusicId::Count;
    int                  subworldLastPlayerHp = -1;
    float                subworldHitFlashTimer = 0.0f;
    // (No restRegenSuppressed. The harness froze the rest law so a march
    // could be measured without regen leaking into the number; since
    // 2026-10-06 a marching body gets no regen BY LAW — `regenerates` is
    // false while a route is walked — so the instrument was measuring a
    // condition the world now guarantees. A field whose only reader is a gate
    // that cannot fire is a colonka-sirota, DOD п.9.)
    // (No pending-cast wind-up: a cast resolves at its own click and the
    // time it costs is the BODY's one recovery gate — ecs::Combat::
    // recoverySteps, owner verdict 2026-09-09.)
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

    // Пошаговый режим «как в Might & Magic» (owner verdict 2026-09-17, CANON
    // S13; built 2026-09-19): the P key's stored TOGGLE — whether the player
    // WANTS turns. Whether the scene actually stands is derived every frame
    // (kPauseTurnStop below): mode on ∧ subworld ∧ the body's one recovery
    // gate at 0 ∧ no attack being asked. Session state like playerPaused:
    // not saved — a loaded game starts flowing.
    bool  turnBasedMode = false;

    bool  showDebug = false;
    bool  showDialogOpen = false;
    sm::GameEvent showDialogEvent{};
    sm::ui::DialogOverlayState showDialogUi{};
    std::uint32_t showDialogCapturedTick = std::uint32_t(-1);
    sm::ui::StoryOverlayState storyOverlay;
    std::uint32_t showStoryCapturedTick = std::uint32_t(-1);
    // An authored scene is holding the MAP shut (owner, playtest 2: an
    // opened map while the opening scene plays ruins the immersion). The
    // map is KNOWLEDGE, and a man ambushed on a road has none yet — the
    // optical sweep does not run until the scene's own story beat lets him
    // go, so the world's first sight arrives with the arrival slide. Set
    // from the scene row's own column (content/plot/scene holdsMap),
    // cleared by that beat's StoryResult. Session state: a pocket is never
    // saved, so a load lands in the world with the map opening normally.
    bool sceneHoldsMap = false;
    std::array<sm::GameEvent, kPendingPresentationMax> pendingPresentationEvents{};
    std::size_t pendingPresentationCount = 0;
    std::uint32_t pendingPresentationTick = std::uint32_t(-1);
    std::size_t pendingPresentationSeen = 0;
    sm::ui::Toggles ui;
    sm::ui::UiSettings uiSettings;   // universal UI show/hide + size prefs (global)
    sm::ui::Keymap     keymap;       // universal rebindable key bindings (global)
    bool uiPrefsDirty = false;       // set when the Interface panel changes a value
    sm::ui::MacroCursor cursor;
    // Глаза карты — представление (M-244a, КЭШ: пересобирается глайдом с
    // заявленных точек; мир и сейв его не знают). 128 КиБ значением.
    sm::ui::MacroEyeCache macroEyes;
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
    // ОТЛАДОЧНЫЙ ВИД ЗАПЕЧЁННЫХ ПОЛЕЙ (M-239) и свежесть его образа.
    // Сверяются ЗА КАДР четыре числа — ровно как `nav_ensure` сверяет своё
    // запекание четырьмя сравнениями, — и перезалив идёт только на
    // расхождении. Четыре, а не одно: вид меняет игрок, а образ под ним
    // меняет ПЕРЕПЁК (другой сид = другой мир, другая эпоха = другие округи,
    // и отдельно сам факт «запечено ли уже»). Прибор, показывающий вчерашнее
    // поле, есть лгущий прибор, а это дороже пяти байт состояния.
    sm::NavDebugView navView = sm::NavDebugView::Off;
    sm::NavDebugView uploadedNavView = sm::NavDebugView::Off;
    std::uint32_t uploadedNavSeed = 0;
    std::uint32_t uploadedNavEpoch = 0;
    bool uploadedNavBaked = false;
    sm::ui::CustomGameParams customParams; // remembered across visits to the menu
    // The pre-world character creation screen's working state. Reset on every
    // entry (a new hero starts blank); `creationCustom` remembers whether
    // Start should boot the custom-params world or the default one.
    sm::ui::CreationState creation;
    bool creationCustom = false;
    // The pre-world intro slideshow's finger (reset on every New Game).
    sm::ui::IntroSlidesState introSlides;
    // The studio splash's particles (runs once, at launch).
    sm::ui::SplashState splash;
    ImTextureID  customPreviewTex   = ImTextureID();  // biome-coloured world preview
    int          customPreviewSide  = 0;        // 0 = no preview built yet
    bool         customWorldReady   = false;    // true after a regen succeeds
    // Мировые действия оболочки, ОТЛОЖЕННЫЕ до начала следующего оборота
    // (`apply_world_shell_actions`, main.cpp). Читает её ровно одно место —
    // начало `frame()`, до `acquire_frame`; пишет ровно одно —
    // `apply_shell_actions`, которое решает развилки (`state == Load`,
    // `worldLoaded`) СРАЗУ и кладёт сюда решение, а не сырой флаг.
    // Зачем колонка (DOD п.9): рождение и снос мира зовут
    // `MacroRendererVk::upload` → `vkDeviceWaitIdle` + снос образов +
    // `vkUpdateDescriptorSets`, а экраны оболочки применяются в ХВОСТЕ кадра,
    // внутри уже записываемого командного буфера, где `macro.record` привязал
    // `set_` с этими самыми образами. Валидация Vulkan называла это 18 и 33
    // строками `invalid state` на загрузке и на второй новой игре (M-122).
    // GPU рисует, мир живёт на CPU (AGENTS §6): мир рождается там, где кадра
    // нет вовсе.
    sm::ui::ShellResult pendingWorldShell{};
    SmokeScript smoke;

    // Developer console (Quake-style REPL + inspector panels). Toggled with
    // the backtick key in the Playing state. Commands are registered in
    // `register_console_commands` and capture `App&`.
    sm::dev::Console console;
    // Режим оболочки и «в каком поселении игрок стоит» — рантайм сессии, в
    // сейв не едет (M-190, v157; тип — в шапке этого файла).
    sm::GameSubState subState{};
    // Forced encounter (Session 15 Inc 6). The squad the pre-battle screen is
    // about. Runtime-only. (Рядом стояла ячейка «помилованного» сквада —
    // снята 2026-11-12: договор встречи есть связь двух сквадов со сроком в
    // их анкетах, а не состояние сессии.)
    sm::MacroHandle preBattleNpc{};        // {slot,gen}; протухание мертвит
                                           // поколение
    // THE subject panel's squad (меню-сессия: одна панель на всех — клик по
    // сквад-ряду открывает её на нём; kMacroNoSlot = панель показывает
    // место). Runtime-only, как preBattleNpc; хэндл
    // {slot,gen} вместо entt-энтити — шаг 1г.
    sm::MacroHandle subjectSquad{};
    std::string  encounterTalkLine;
    // Toolbar `>>` / dev console: how many world ticks a turn buys (1.0 =
    // normal). Only the interactive loop honours it; scripted/smoke steps keep
    // their fixed dt so determinism is preserved.
    //
    // AN INTENT, NOT A PERMISSION. Whether it is honoured is DERIVED every turn
    // from the scene (main.cpp promote_turn_ticks / fast_forward_allowed), and
    // a scene that refuses drops this back to 1.0 rather than suspending it.
    // Nothing else may reset it: the load path, the menu and the subworld
    // entrance deliberately do NOT know about this field, which is exactly why
    // an armed 4× can no longer survive them (it used to survive all three).
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
    // derived: turn-based mode holds the scene while the player's body is
    // FREE (recovery gate 0, no attack armed) — owner verdict 2026-09-17,
    // «когда рековери закончилось всё застывает». Macro freezes WITH the
    // scene, exactly as under any other reason of the one pause.
    kPauseTurnStop = 1u << 4,
};

struct RuntimeFrameStats {
    sm::WorldTickResult timeTick{};
    bool ticked = false;
    bool subworldActive = false;
};

// ── Shared app services ─────────────────────────────────────────
// Defined in main.cpp; declared here because app/smoke.cpp (the smoke
// harness) exercises the same runtime the player does — one implementation,
// honest visibility, no second path.
sm::Inventory& player_bag(App& app);
sm::MacroWorld macro_world(App& app);
// Оптический вид мира из кэша приложения (высоты + плотность крон). Объявлен
// здесь ради смоука `forest_stays_felled`: он сверяет кэш с живым лесом.
sm::OpticalWorld optical_world(App& app);
// (`file_size_bytes` СНЕСЕНА 2026-11-09: её единственный звонящий — гейт
//  смоука `save_game`, который не мог упасть, потому что стоял ПОСЛЕ проверки
//  `status == Ready`, а та включает контрольную сумму файла.)
bool route_macro_npc_attack(App& app, sm::MacroHandle npc);
void perform_encounter_auto(App& app, sm::MacroHandle npc, sm::Ambush ambush);
bool settle_encounter_accord(App& app, sm::MacroHandle npc, int days);
void close_pre_battle(App& app);
void detect_forced_encounter(App& app);
float& player_sp_carry(App& app);
sm::ecs::Pools& player_pools(App& app);
sm::CharacterSheet player_effective_sheet(const App& app);
void sync_audio_music(App& app);
void refresh_save_summary(App& app);
void stage_save_state(App& app);
void enter_subworld(App& app);
std::uint8_t pause_reasons(const App& app);
bool world_paused(const App& app);
void sync_relative_mouse_mode(App& app);
std::vector<sm::PathPoint> build_flight_path(int sx, int sy, int gx, int gy,
                                             int mapW);
// (count_tick_events/latest_tick_event умерли с тегом SpellCast — M-410.)
// HIS march caches — the two columns every squad carries (travelRank,
// marathonRank, carryPerSoulG), on the FLAG. The hourly burn reads them, and so
// does the camp-repays question.
sm::ecs::MacroNpcRuntime* player_march_cache(App& app);
bool cast_active_spell(App& app);
void aim_rest_until_rested(App& app);
int apply_rest_promotion(App& app, int ticks);
void apply_pending_event_effects(App& app);
void apply_pending_story_results(App& app);
void capture_presentation_events(App& app);
// КТО ДЕРЖИТ ЭКРАН — ОДИН ОТВЕТ, И ТЕПЕРЬ ОН ВИДЕН СМОУКУ. Объявлена здесь
// затем, чтобы `smoke_clear_modal_overlays` могла СПРОСИТЬ её, а не
// пересказывать: пересказ разъехался — помощник гасил две причины из трёх
// (диалог и повесть), а третью, `PreBattle` встречи, оставлял стоять, и
// сценарий, просивший диалог, получал «open=0 pending=1» ровно тогда, когда
// миру случалось свести игрока с врагом. Второй список причин — тот же второй
// ответ на один вопрос (DOD п.6).
bool modal_overlay_active(const App& app);
void process_world_events(App& app);
// Play an authored scene over the standing world (release.md §3): raises
// the row's pocket, places its cast, holds the map if it says so. Declared
// for the smoke harness, which drives this very door rather than a copy.
void begin_scene(App& app, const sm::content::SceneDef& scene);
RuntimeFrameStats tick_playing_runtime(App& app, bool allowInput);
RuntimeFrameStats advance_sim_steps(App& app, int steps, bool allowInput);
// The avatar body's one occupancy gate (ecs::Combat::recoverySteps), 0 when
// free — the number the turn-based stop derives from (main.cpp).
std::uint32_t player_gate_steps(const App& app);
// ЕДИНИЧНАЯ ДВЕРЬ ГАРНЕССА над тик-родным `advance_sim_steps` выше: переводит
// отрезок времени в шаги и зовёт её. Прод-звонящих у неё НОЛЬ — игровой цикл
// зовёт тик-родную напрямую, — и это НЕ делает её сиротой: в секундах думают
// ровно те, кто пишет число РУКОЙ (гарнесс и консоль), а мир не спрашивает
// никогда (вывод — у `sim_steps_for_seconds@src/app/main.cpp`).
//
// И ЭТО НЕ НАРУШЕНИЕ «КАЛИБРОВАТЬ ТОЛЬКО В ТИКАХ» — проверено 2026-11-09, когда
// я объявил её нарушением 35 раз и ошибся: закон запрещает выводить величины из
// РЕАЛЬНОЙ секунды, потому что та есть свойство ЖЕЛЕЗА игрока. Здесь секунда —
// ровно `kTicksPerRealSecond` = 64 тика ПО ПОСТРОЕНИЮ (сутки 8192 тика, и на
// любом мониторе они идут 128 секунд), то есть фиксированное кратное тика, а не
// железная величина. Той же единицей живёт мир: `SpellDef::speed` — tiles/s,
// `kSubworldWalkTilesPerSecond` — 24. Снос этой двери заставил бы два сайта
// полёта болта переводить tiles/s в тики РУКОЙ, то есть ухудшил бы читаемость
// ради мнимого закона.
RuntimeFrameStats advance_sim_seconds(App& app, float seconds, bool allowInput);

} // namespace sm::app

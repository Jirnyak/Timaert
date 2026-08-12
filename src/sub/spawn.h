// Subworld spawn — populates the ECS with creatures from the per-cell
// fauna table. Mirrors `subworld/spawn.ts` populator: each visible cell
// rolls its own table once per cell entry, scaled by the world tile area.
#pragma once
#include <cstdint>
#include <vector>
#include "ecs/world.h"
#include "sub/seamless_manager.h"
#include "macro/fauna.h"
#include "macro/aura.h"
#include "macro/biomes.h"
#include "macro/features.h"
#include "macro/army.h"
#include "macro/npc.h"
#include "macro/macro_stock.h"

namespace sm::sub {

// Data-driven subworld combat stance: maps the registry's NpcTypeDef.ai column
// to the subworld AI kind, so EVERY humanoid spawn path (settlement population
// and macro→sub projection) resolves hostility from the same data row — one
// column, no per-site hardcode. Fighters are the overworld-aggressive
// (bandits) and the armed keepers of order (guards); every other type flees
// when threatened. Future combat styles (formation archers, kiting casters,
// hit-and-run beasts) plug in HERE as new SubworldAi kinds + registry rows —
// the spawn sites never change.
inline ecs::SubworldAi::Kind subworld_ai_for(AIBehaviour ai) {
    return combatant_behaviour(ai) ? ecs::SubworldAi::Combat
                                   : ecs::SubworldAi::Flee;
}

// Universal per-humanoid component attachers — ONE home for the rules every
// spawn site shares (settlement populator, squads, macro projection in
// spawn.cpp; console/encounter spawner in engine.cpp). Both used to exist as
// byte-identical file-local twins in the two TUs.
void maybe_emplace_missile_attack(entt::registry& reg, entt::entity e,
                                  const CombatTemplate& combat);
void maybe_emplace_carried_light(entt::registry& reg, entt::entity e,
                                 const NpcTypeDef& def);

// ── THE birth of a subworld humanoid: two forms, one axis ─────────────────
//
// THE RULE (owner, 2026-08-06). The macro world is the source of truth and it
// holds exactly two kinds of thing, so a body can come into being in exactly
// two ways. There is no third, and that is the point: every difference the five
// hand-written spawners had between them turned out to be this one axis wearing
// a different hat, and a form nobody can half-fill is a form nobody can forget
// to fill.
//
//   DERIVED — an embodied NUMBER or table row. A townsman standing for one unit
//   of his settlement's population; a soldier standing for one line of a roster;
//   an animal standing for one head of the cell's game. Nothing about him is
//   remembered above, so everything about him is derived from a seed: his face,
//   his sheet, and — at the instant he dies — his loot. He stores NOTHING that
//   his seed already decides. A city of five thousand is five thousand integers
//   and no bags.
//
//   TRACKED — the visible form of a macro ENTITY. A lord, a squad leader, a
//   caravan master. His face, his wounds and his belongings are STATE that lives
//   above; the body copies them down, and what happens to the body is written
//   back up. He is the only body that remembers anything, because he is the only
//   body there is something to remember about.
struct HumanoidBody {
    NPCType       type;
    float         x = 0.0f;
    float         y = 0.0f;
    std::uint16_t faction = 0;
    int           level = 1;
    // The deterministic stream this body is drawn from: sheet, face, and the
    // loot rolled at its death. One seed, so re-entering a cell reproduces the
    // same people down to what they are carrying.
    std::uint32_t seed = 0;
    // Role: what this body is DOING here. Citizens live their errands, soldiers
    // and hostiles fight. Everything else about them is identical.
    bool          combatant = false;
};

// What the macro world LENT for a derived body. `MacroStock::Count` means it
// lent nothing — a body spawned by fiat (a quest ambush, a smoke scenario, a
// console command). That case is spelled out rather than implied, because
// "borrowed from nothing" must be a decision somebody made, not a field
// somebody forgot: see macro/macro_stock.h, where a stamped debt is the only
// way to draw on a stock at all.
struct BodyLoan {
    MacroStock    stock = MacroStock::Count;
    MacroStockKey key{};
    static BodyLoan none() { return BodyLoan{}; }
    static BodyLoan from(MacroStock s, MacroStockKey k) { return BodyLoan{s, k}; }
};

// Form 1 — DERIVED. `faceSalt` separates two bodies drawn from the same seed
// (slot index, ordinal in the crowd). Stamps the loan's receipt, if any.
// `aura` is the squad leader's buff (macro/aura.h): a set of sheet modifiers
// applied INTO this body's own sheet before combat is projected from it —
// ruling №2, never a second multiplier beside the sheet. nullptr (every
// non-squad body) applies nothing.
entt::entity spawn_derived_body(entt::registry& reg, const HumanoidBody& body,
                                std::uint32_t faceSalt,
                                const BodyLoan& loan = BodyLoan::none(),
                                const AuraMods* aura = nullptr);

// Form 2 — TRACKED. Reads WHAT this body is (type, faction, rank), WHO it is
// (face) and HOW HURT it is straight from the macro entity, and hands the body
// back its `MacroOrigin` backlink so the return trip — wounds up, death up — has
// somewhere to write. Returns entt::null if `macro` is not a body-shaped macro
// entity (no kind / health / rank / face, or a kind outside the humanoid rows):
// a caller cannot accidentally get a half-tracked body.
entt::entity spawn_tracked_body(entt::registry& reg, entt::entity macro,
                                float x, float y, std::uint32_t seed,
                                bool combatant);

// ── Per-cell population (seamless persistence) ───────────────────────────
//
// Populate ONE cell of the 3×3 window — the offset (ox,oy) ∈ {-1,0,1}² from the
// window centre — with its own world creatures: fauna rolled from THAT cell's
// resolved table plus, if it is a settlement, its citizens. Faction / AI /
// colour / radius all come from the FaunaEntry — the engine stays creature-
// agnostic. Everything lands in the cell's sub-region
// [ (ox+1)*kCellSize .. +kCellSize )².
//
// This does NOT clear first: the engine orchestrates clear / rebase / despawn
// (see the helpers below) so that content shared between the old and new 3×3
// windows survives a seam crossing untouched — only cells that actually leave
// the window are evicted, and only cells newly brought in are spawned. That is
// the fix for "a city vanishes when you step one cell out of it".
//
// Fauna is fully deterministic from `cellSeed` (the cell's ABSOLUTE macro seed
// from resolve_context): re-entering a cell reproduces the same procedural set,
// which is where the future per-macro-cell "visitation age" counter plugs in —
// mix that epoch into `cellSeed` and re-entries vary controllably with zero
// per-entity storage. Settlement citizens are re-derived from the persistent
// macro context, so cities are preserved across re-entry by construction.
//
// `landmarkPop` is settlement population (0 if none) and feeds the √(pop/100)
// level bonus from the context scale. `zoneLevel` is the macro zones difficulty
// (0..9); zones >2 add (z-2) levels and 1+(z-2)*0.18 hp/damage multipliers.
//
// `settlementFaction` is the registry index EVERY citizen of this cell's town is
// stamped with — the faction of the kingdom that owns it, resolved by the caller
// through macro/politik.h's faction_index_for_kingdom. It has no default on
// purpose: a settlement's allegiance must be an answered question at every call
// site, never a silent "empire". Fauna is unaffected — a creature's faction
// comes from its own FaunaEntry row.
void spawn_cell_npcs(ecs::World& w,
                     Biome biome,
                     int treeCount,
                     LandmarkKind landmark,
                     const SeamlessSubworldManager& mgr,
                     int ox,
                     int oy,
                     std::uint32_t cellSeed,
                     std::uint16_t settlementFaction,
                     int landmarkPop = 0,
                     int zoneLevel   = 0,
                     // Which macro stock the citizens are borrowed FROM: the
                     // settlement/village id and the cell it stands in. Every
                     // citizen is stamped with it (macro/macro_stock.h), so a
                     // death in the subworld is paid back to the map above.
                     // -1 means "no named place here" — nothing is borrowed.
                     int landmarkSubjectId = -1,
                     int macroCellX = 0,
                     int macroCellY = 0,
                     // The wild headcount standing on this cell — the honest
                     // CAP on how many creatures embody (macro_stock fauna
                     // row). Each one is stamped with the cell's FaunaCount
                     // debt, so a kill thins the cell for good; -1 = no macro
                     // context wired (tests/harness) = the old unbounded roll.
                     int faunaCount = -1);

// ── Dungeon residents (sub/dgn interiors) ────────────────────────────────
//
// The household of ONE interior: `count` civilians drawn from the SAME
// population stock as the street crowd — each body carries the settlement's
// Population loan, so a death behind a door pays the town back through the
// exact write-back path a street kill uses (owner ruling 2026-08-12: one
// stock system, никакой второй копии). Spots are rolled inside the window-
// tile rectangle [x0,x1]×[y0,y1] on plain interior floor (TILE_SQUARE), so
// residents never spawn inside partition walls or furniture. Deterministic
// from `seed`. Returns the number actually placed.
int spawn_dungeon_residents(ecs::World& w,
                            const SeamlessSubworldManager& mgr,
                            std::uint32_t seed,
                            std::uint16_t settlementFaction,
                            int count,
                            int levelBonus,
                            float x0, float y0, float x1, float y1,
                            MacroStockKey populationKey);

// The vermin of ONE interior (a cellar, a cave floor): creatures rolled from
// the SAME global monster table the open world uses — `tableKind` picks the
// row family (a cellar reads the Ruin table: what lives in the dark under
// men's floors), `biome` and `treeCount` complete the ordinary resolve — and
// borrowed from the SAME fauna_count stock as the cell above, so a kill down
// here thins the cell for good and the ONE regrowth law (32 game days a head,
// macro/fauna.h) brings it back. `budget` is that stock: nothing embodies
// beyond what still stands. Placement is plain interior floor (TILE_SQUARE)
// inside [x0,x1]×[y0,y1]. Deterministic from `seed`; returns how many stood up.
int spawn_dungeon_vermin(ecs::World& w,
                         const SeamlessSubworldManager& mgr,
                         std::uint32_t seed,
                         LandmarkKind tableKind,
                         Biome biome,
                         int treeCount,
                         int levelBonus,
                         float hpMult,
                         float damageMult,
                         int budget,
                         float x0, float y0, float x1, float y1,
                         MacroStockKey faunaKey);

// Destroy every world-owned subworld creature (fauna + citizens), preserving the
// player-side projections (PlayerTag / PlayerSoldierTag) that follow the player
// across re-centres rather than belonging to a cell. Used for a clean slate on
// enter / leave; the per-cell path above avoids it on ordinary seam crossings.
void clear_subworld_world_entities(ecs::World& w);

// Shift every SubworldTag entity's Position + VisualPos by (dxTiles,dyTiles).
// Applied on a seam re-centre with (-dx*kCellSize, -dy*kCellSize) so that fixed
// physical content — and the player's own squad — track the recentred composite
// window instead of drifting by one macro cell each crossing.
void rebase_subworld_entities(ecs::World& w, float dxTiles, float dyTiles);

// Destroy world-owned subworld creatures whose Position now lies outside the
// composite window [0,kFullSize)²; player-side projections are never evicted.
// Run after rebase on a re-centre to evict exactly the cells that left the 3×3.
void despawn_subworld_entities_outside_window(ecs::World& w);

// Project a macro squad into the current subworld as real ECS NPC entities. The
// macro SoldierSquad remains the persistent source of truth.
//
// THE RULE (owner, 2026-08-04): a squad's members all wear the faction of the
// squad's OWNER on the macro layer, whoever the members happen to be. A warband
// of hired barbarians marching under your banner fights as YOUR realm, not as
// barbarians; a city garrison projected the same way will fight as its city.
// That is why the faction is a parameter and not a literal: it is derived at the
// call site from whoever owns the squad (the player → the "player" registry row;
// a garrison → its settlement's kingdom), so no new persisted field is needed and
// a party — which is its leader NPC — simply passes its leader's faction.
//
// `aura` follows the same logic as the faction (macro/aura.h): the LEADER's
// buff, collected once at the call site from whoever leads the squad (the
// player → collect_leader_aura(gs.player.sheet)) and applied into every
// member's sheet at birth. nullptr = a leaderless context, nothing applied.
void spawn_player_squad(ecs::World& w,
                        const SoldierSquad& squad,
                        const SeamlessSubworldManager& mgr,
                        float playerX,
                        float playerY,
                        std::uint32_t seed,
                        std::uint16_t faction,
                        const AuraMods* aura = nullptr);

void spawn_player_squad(ecs::World& w,
                        const SoldierSquad& squad,
                        const std::vector<std::uint8_t>& tiles,
                        float playerX,
                        float playerY,
                        std::uint32_t seed,
                        std::uint16_t faction,
                        const AuraMods* aura = nullptr);

// ── Macro→subworld projection (Inc 5d) ───────────────────────────────────
//
// Project every persistent macro NPC standing in the current 3×3 window into the
// subworld as a real, full combat body — so the lords, bandits, and peasants who
// roam the overworld are physically MET (and, via possession, taken over) where
// they actually are. The macro NPC entity stays authoritative and untouched (the
// macro tick is frozen while a subworld is active); each projection carries a
// `MacroOrigin` backlink to its source so the reaper spares it and leave() (5e)
// can land the player back on the right macro cell.
//
// A macro NPC is "in the window" when its integer cell is within ±1 of the
// window centre (centerCx,centerCy) on the map torus (mapW×mapH) — the SAME nine
// cells the seamless manager loads. Each projection mirrors the settlement-
// citizen layout: NPCKind/faction and NpcCharacter copied verbatim, HP copied as
// body-native persistent state, Combat DERIVED from a fresh universal
// CharacterSheet, SubworldAi hostility keyed off NpcTypeDef.ai (Aggressive→fight,
// else flee). Placement scatters within the cell's sub-region, dodging water.
//
// A macro NPC IS a squad (ecs::SquadRoster doctrine): the entity itself is the
// leader — projected as the TRACKED body — and every roster row is one unit of
// the squad's roster STOCK made visible, projected around the leader as a
// DERIVED body wearing the OWNER's faction (the banner rule above) and
// carrying the receipt (MacroStock::Roster, subject = the squad's MacroSpawnId
// ordinal, detail = the member's entityId) that pays its death back into the
// roster. An empty roster projects a lone wanderer, exactly as before.
//
// Enter-only: it does NOT re-run on a seam crossing, so a macro NPC in a newly
// entered neighbour cell is not yet materialised — an accepted v1 scope, since
// the persistent macro entity is never lost. Returns the number of bodies
// projected, members included (bounded by kMaxProjectedMacroNpcs). `seed`
// should be the world seed mixed with the window centre so a re-entry
// reproduces the same scene. The raw-tiles form exists for tests, like the
// squad spawn above; the mgr form delegates to it.
int project_macro_npcs_into_subworld(ecs::World& w,
                                     const SeamlessSubworldManager& mgr,
                                     int centerCx, int centerCy,
                                     int mapW, int mapH,
                                     std::uint32_t seed);

int project_macro_npcs_into_subworld(ecs::World& w,
                                     const std::vector<std::uint8_t>& tiles,
                                     int centerCx, int centerCy,
                                     int mapW, int mapH,
                                     std::uint32_t seed);

// ── Exit remap query (Inc 5e-1) ──────────────────────────────────────────
//
// Resolve where the macro player should land when leaving the subworld while
// `body` wears the player flag. If `body` was PROJECTED from a macro NPC (it
// carries a `MacroOrigin` whose macro entity is still valid and positioned) —
// i.e. the player possessed a lord/bandit/peasant — the result is that macro
// entity's cell wrapped to the map torus [0,mapW)×[0,mapH): you "exit AS" the
// body you possessed (D5). Otherwise `has == false` and the caller falls back to
// the window centre — the hero husk (`spawn_player_entity`) and ambient/citizen
// bodies carry no backlink, so a normal un-possessed exit is unaffected. Pure
// registry query: no engine / GameState coupling, unit-testable in isolation.
// Runtime-only (`MacroOrigin` is never serialised); the save-stable identity
// that Inc 5e-2 persists is the macro NPC's spawn ordinal, not this handle.
// `macro` names the origin macro entity when `has` (entt::null otherwise) so the
// caller can ADOPT it as the persistent player (adopt_possessed_macro_as_player).
struct MacroExitCell { bool has; int cx; int cy; entt::entity macro; };
MacroExitCell macro_exit_cell_for_body(ecs::World& w, entt::entity body,
                                       int mapW, int mapH);

// Inc 5e-2 (identity remap). After leave() lands the macro player on a possessed
// body's origin cell, ADOPT that macro NPC as the persistent player: move the
// single PlayerTag flag onto `macro` — the overworld player now IS the
// lord/bandit/peasant you inhabited, fighting on its own sheet — and return its
// deterministic MacroSpawnId ordinal for save persistence
// (PlayerState::possessedMacroSpawnId). Returns -1 and moves NO flag when `macro`
// is null / invalid / not a real macro NPC (no MacroNpcRuntime); if it is a real
// macro NPC the flag is placed but the return is still -1 when it lacks a
// MacroSpawnId (synthetic setups only — make_npc always stamps one). Pure
// registry mutation; the caller owns the surrounding flag/husk lifecycle.
int adopt_possessed_macro_as_player(ecs::World& w, entt::entity macro);

// ── Possession / вселение (Inc 5c) ───────────────────────────────────────
//
// The player is one PlayerTag flag riding an ECS body (the "player is an NPC
// with a flag" model). Possession MOVES that single flag onto another live
// body so the player inhabits it; because every consumer — camera, input,
// incoming combat, minimap, and (on exit) the macro landing — was made
// flag-following in Inc 5a, they all follow with no per-consumer change.
//
// D2 (literal flag move) + D3 (body-native): the possessed body keeps its OWN
// Health / Combat / sheet — NO hero stats are stamped onto it, and gs.player is
// preserved untouched as the revert target. The engine's sync/reconcile become
// body-native by testing the ONE discriminator these functions maintain: the
// hero body (spawn_player_entity) carries no NPCKind, every possessable scene
// body does.
//
// current_player_body: the single entity currently carrying PlayerTag, or
// entt::null (never null mid-subworld — exactly one flag is always live).
entt::entity current_player_body(ecs::World& w);

// Move the player flag onto `target` (must be a live, positioned scene body).
// Removes PlayerTag from the current body; if that body was the hero husk (no
// NPCKind) it is destroyed — the hero's canonical state lives in gs.player, so
// nothing is lost and no inert, un-rendered, un-AI'd zombie is stranded in the
// scene. A vacated FOREIGN body keeps all its components and, with the flag
// gone, its AI / rendering / targetability resume automatically (every such
// path is PlayerTag-gated). Emplaces PlayerTag on `target`. No-op returning
// false if target is null / invalid / unpositioned / already the player. Pure
// ECS: the caller re-mirrors the position scalars from the new body afterwards.
bool possess_entity(ecs::World& w, entt::entity target);

} // namespace sm::sub

// Subworld spawn — populates the ECS with creatures from the per-cell
// fauna table. Mirrors `subworld/spawn.ts` populator: each visible cell
// rolls its own table once per cell entry, scaled by the world tile area.
#pragma once
#include <cstdint>
#include <vector>
#include "ecs/world.h"
#include "sub/collide.h"
#include "sub/height.h"
#include "sub/seamless_manager.h"
#include "macro/fauna.h"
#include "macro/character_sheet.h"
#include "macro/biomes.h"
#include "macro/features.h"
#include "macro/army.h"
#include "macro/npc.h"
#include "macro/macro_stock.h"

namespace sm::sub {

// The row's behaviour column folded to a subworld stance — ONE door for men
// and beasts alike (they used to fold through two, one per vanished enum).
// Fighters fight, prey runs, a roamer roams, and everything whose macro job
// has no meaning down here (a gatherer inside a battle, a caravan) keeps the
// civilian's answer: get away.
inline ecs::SubworldAi::Kind subworld_ai_for(AIBehaviour ai) {
    if (combatant_behaviour(ai))        return ecs::SubworldAi::Combat;
    if (ai == AIBehaviour::Wanderer)    return ecs::SubworldAi::Wander;
    return ecs::SubworldAi::Flee;
}

// Universal per-humanoid component attachers — ONE home for the rules every
// spawn site shares (settlement populator, squads, macro projection in
// spawn.cpp; console/encounter spawner in engine.cpp). Both used to exist as
// byte-identical file-local twins in the two TUs.
void maybe_emplace_missile_attack(entt::registry& reg, entt::entity e,
                                  const CombatTemplate& combat);
// ЛЕТУН (полёт-посадка 2026-09-10): строка с cruiseM > 0 рождается с
// ecs::Flying — гравитация снята, конверт [опора, потолок] ОБЩИЙ с
// игроком, вертикальное намерение пишет её же мозг (sub/ai.cpp).
void maybe_emplace_flying(entt::registry& reg, entt::entity e,
                          const CombatTemplate& combat);
void maybe_emplace_carried_light(entt::registry& reg, entt::entity e,
                                 const NpcTypeDef& def);

// ── THE birth of a subworld BODY: two forms, one axis ─────────────────────
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
// A body of ANY row — a peasant, a lord, a wolf. There was a second birth
// beside this one for creatures until 2026-08-20, differing in exactly two
// numbers (a radius the row authors and a tint the sprite table already held)
// and in one assumption that was never true: that a beast is a different KIND
// of thing from a man (CANON.md S4/S16).
struct BodySpec {
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
    // WHO THIS ALREADY IS, when the world keeps a record of him. A projected
    // lord is not a fresh roll of his own row — he is HIMSELF, and the macro
    // layer has been storing his sheet since v90. Left null the body is drawn
    // from `seed` as before, which is the honest answer for a citizen the
    // world has never written down.
    //
    // Borrowed for the length of the call only (the ECS-ref grabla: a
    // component reference does not survive a spawn, and spawning is exactly
    // what happens next).
    const CharacterSheet* sheet = nullptr;
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
// `squadBonuses` is what this body's LEADER gives it (character_sheet.h
// squad_bonuses) — the ordinary BonusTotals every modifier in the game
// produces, applied into this body's own sheet before combat is projected
// from it. Ruling №2, never a second multiplier beside the sheet; and since
// 2026-08-27 not a second SYSTEM either. nullptr (every non-squad body)
// applies nothing.
entt::entity spawn_derived_body(entt::registry& reg, const BodySpec& body,
                                std::uint32_t faceSalt,
                                const BodyLoan& loan = BodyLoan::none(),
                                const BonusTotals* squadBonuses = nullptr);

// Form 2 — TRACKED. Reads WHAT this body is (type, faction, rank), WHO it is
// (face) and HOW HURT it is straight from the macro entity, and hands the body
// back its `MacroOrigin` backlink so the return trip — wounds up, death up — has
// somewhere to write. Returns entt::null if `macro` is not a body-shaped macro
// entity (no kind / health / rank / face, or a kind outside the humanoid rows):
// a caller cannot accidentally get a half-tracked body.
entt::entity spawn_tracked_body(entt::registry& reg, entt::entity macro,
                                float x, float y, std::uint32_t seed,
                                bool combatant);

// Does this projection OWN NOTHING — is it an address rather than a copy?
//
// The mirror law (owner 2026-09-12, sub/record.h): a projected body carries no
// bag, no gear, no book and no personality of its own; all of it belongs to the
// record it projects, and every reader goes through the door to reach it.
//
// This predicate is the inverse of the one it replaced, and the inversion is
// the whole landing. `tracked_body_inherits_all` asked whether the COPY was
// complete — a question that only makes sense while copies exist, and one the
// project answered wrong once already (SAVE-1: the copier was a hand-written
// run of `if (try_get) emplace` lines, `BodyEquipment` was not among them, and
// an armoured lord fought naked while nothing said a word). With no copy there
// is nothing to be incomplete.
//
// Both halves are checked, because either alone is satisfiable for the wrong
// reason: the body must hold none of it, AND the record must be where it lives.
bool tracked_body_owns_nothing(const entt::registry& reg,
                               entt::entity macro, entt::entity body);

// Re-derive a standing body's OUTGOING numbers from its record — but only if
// what stands on that record actually changed (sub/record.h StandingMirror,
// `BonusTotals::operator==`). Returns true when it rebuilt.
//
// This is the half of the mirror that is NOT the bars: a body's swing comes
// from its sheet through its row's template, and under the mirror law the
// record can change while the body stands — he levels from a kill he lands,
// something is put on him. Gated rather than unconditional because re-rolling
// a sheet costs five times what comparing costs, and almost never changes
// anything (the measurement is in CANON and in record.h).
//
// No-ops for anything that is not a tracked body: no cache, no record, no
// creature row (the hero husk — his hands are assembled from the same effective
// sheet by the engine, since a husk has no row to project from).
bool refresh_body_strike(entt::registry& reg, entt::entity body);

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
// `landmarkPop` is settlement population (0 if none). It decides HOW MANY people
// stand here and nothing else: a big town simply has more guards on its streets,
// and each of them is the same guard his row describes. There is no level bonus
// and no stat multiplier on this path — a creature's strength IS its row (CANON.md
// S12). The danger zone shapes WHICH rows a place rolls, never their numbers; the
// old √(pop/100) level bonus and the zone's +1 level / ×(1+0.18·(z−2)) hp+damage
// markup were a hidden auto-level and were deleted 2026-08-20 by owner's ruling.
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
                     LandmarkType landmark,
                     std::uint8_t danger,
                     std::uint8_t depositsNear,
                     const SeamlessSubworldManager& mgr,
                     int ox,
                     int oy,
                     std::uint32_t cellSeed,
                     // The world seed, for the interior-reserve law below
                     // (interior_household_share keys households off it).
                     std::uint32_t worldSeed,
                     std::uint16_t settlementFaction,
                     int landmarkPop,
                     // Which macro stock the citizens are borrowed FROM: the
                     // settlement/village id and the cell it stands in. Every
                     // citizen is stamped with it (macro/macro_stock.h), so a
                     // death in the subworld is paid back to the map above.
                     // -1 means "no named place here" — nothing is borrowed.
                     int landmarkSubjectId,
                     int macroCellX,
                     int macroCellY,
                     // The wild headcount standing on this cell — the honest
                     // CAP on how many creatures embody (macro_stock fauna
                     // row). Each one is stamped with the cell's FaunaCount
                     // debt, so a kill thins the cell for good; -1 = no macro
                     // context wired (tests/harness) = the old unbounded roll.
                     int faunaCount,
                     // The place's STANDING ARMY at home (§42 Инк 7:
                     // Landmark::garrison — the roster whose owner is a
                     // landmark). Every record embodies as a fighting body
                     // with the Garrison loan: killed on the wall = struck
                     // from the roll; out on patrol / hired away = not in
                     // this roster = not on the street. nullptr = none.
                     const SoldierSquad* garrison,
                     // WHAT HOUR IT IS, and it is not optional. How many of a
                     // place's people stand on its streets is a question about
                     // the sun (city_layout.h crowd_outdoor_share01); the rest
                     // are behind their own doors. A caller that did not have
                     // to say would be a caller silently asking for midnight.
                     const WorldTime& now);

// ── Dungeon residents (sub/dgn interiors) ────────────────────────────────
//
// The household of ONE interior: `count` civilians drawn from the SAME
// population stock as the street crowd — each body carries the settlement's
// Population loan, so a death behind a door pays the town back through the
// exact write-back path a street kill uses (owner ruling 2026-08-12: one
// The interior reserve of ONE storey behind a door (CANON S28 partition:
// interiors reserve their souls, the street is the REMAINDER — one soul
// embodies once). This is THE household law, in one place: the engine's
// interior spawn takes `min(stock now, this share)`, and the street
// spawner SUBTRACTS the same shares before it fills the square — the two
// can never disagree about who lives behind a door. Deterministic from
// (worldSeed, door cell, building ordinal, storey).
int interior_household_share(std::uint32_t worldSeed, int cellX, int cellY,
                             std::uint16_t ordinal, int level,
                             int landmarkPop, int doorsInCell,
                             const WorldTime& now);

// How many doors this cell actually has to keep people behind.
//
// THE HEARTH IS SIZED FROM THIS, not from the house count the layout law asked
// for, and the difference is the whole point: a town wants `pop / mean` houses
// but seats only about three quarters of them — the frontage runs out of lane
// and the fallback scatter runs out of ground. Sized from the wish, the doors
// of a city of twelve hundred held eight hundred people and four hundred had
// nowhere to sleep, which showed the first night the sun sent them home.
//
// Sized from the doors that EXIST, the town's people fill the town's houses
// however many the ground allowed. Both readers of the hearth law count the
// same way over the same list, so they cannot disagree.
int doors_in_cell(const std::vector<Structure>& structures,
                  float originX, float originY);

// The garrison share of ONE storey of a place's own interior (a spire
// tower's floor): the souls kept inside are `pop - (pop >> the registry's
// crowdOutsideShift)`, split evenly over the storeys with the remainder to
// the lower floors. Pure arithmetic of the LIVE population — clearing a
// floor thins the place, and every re-derived share thins with it.
int interior_garrison_share(LandmarkType landmark, int landmarkPop,
                            int storeys, int level);

// The interior reserve of ONE CELL: the sum of every soul its doors keep
// behind them — households behind House doors, the garrison behind a
// tower's gate — each asked of the same pure laws the engine uses when a
// door is opened (which law a door speaks is the dungeon kind ROW's own
// columns: householdAbove / placeGarrison). Street crowd = population −
// this (the §42 partition witness asserts the sum exactly).
int interior_reserve_for_cell(const std::vector<Structure>& structures,
                              LandmarkType landmark,
                              std::uint32_t worldSeed,
                              int cellX, int cellY,
                              float originX, float originY,
                              int landmarkPop, const WorldTime& now);

// stock system, никакой второй копии). Placement is the scene's OWN floor
// catalog (map_data.h StandPoint, CANON S28) — a uniform draw without
// replacement over every standable tile the generator emitted, so a body
// can stand anywhere the interior truly walks (a cave's whole gallery
// chain, never just its mouth rectangle) and NOTHING is dropped silently:
// an empty catalog refuses out loud. `originX/originY` map the catalog's
// cell-local tiles into window tiles. Deterministic from `seed`. Returns
// the number actually placed.
int spawn_dungeon_residents(ecs::World& w,
                            std::uint32_t seed,
                            std::uint16_t settlementFaction,
                            // WHOSE household this is: the door cell's own
                            // landmark kind. The residents roll the crowd
                            // stripe of THAT place's registry row (fauna.h
                            // pick_crowd_row) — a hall in a spire is manned
                            // by the spire's crowd, not by townsfolk (§42:
                            // the old hardcoded City here dressed every
                            // interior in the world as a town house).
                            LandmarkType landmark,
                            // The door cell's danger byte and deposit gates
                            // complete the same context the street rolls.
                            std::uint8_t danger,
                            std::uint8_t depositsNear,
                            int count,
                            const std::vector<StandPoint>& floorCatalog,
                            float originX, float originY,
                            MacroStockKey populationKey,
                            // Role is CONTEXT, not a second spawner: a
                            // hearth's family lives its errands (false), a
                            // garrisoned storey FIGHTS for its place (true).
                            bool combatant = false);

// The vermin of ONE interior (a cellar, a cave floor): creatures rolled from
// the SAME global monster table the open world uses — `tableKind` picks the
// row family (a cellar reads the Ruin table: what lives in the dark under
// men's floors), `biome` and `treeCount` complete the ordinary resolve — and
// borrowed from the SAME fauna_count stock as the cell above, so a kill down
// here thins the cell for good and the ONE regrowth law (32 game days a head,
// macro/fauna.h) brings it back. `budget` is that stock: nothing embodies
// beyond what still stands. Placement is the scene's floor catalog, exactly
// as for residents above. Deterministic from `seed`; returns how many stood up.
int spawn_dungeon_vermin(ecs::World& w,
                         std::uint32_t seed,
                         LandmarkType tableKind,
                         std::uint8_t danger,
                         Biome biome,
                         int treeCount,
                         int budget,
                         const std::vector<StandPoint>& floorCatalog,
                         float originX, float originY,
                         MacroStockKey faunaKey);

// Destroy every world-owned subworld creature (fauna + citizens), preserving the
// player-side projections (AvatarTag / PlayerSoldierTag) that follow the player
// across re-centres rather than belonging to a cell. Used for a clean slate on
// enter / leave; the per-cell path above avoids it on ordinary seam crossings.
void clear_subworld_world_entities(ecs::World& w);

// Shift every SubworldTag entity's Position + VisualPos by (dxTiles,dyTiles).
// Applied on a seam re-centre with (-dx*kCellSize, -dy*kCellSize) so that fixed
// physical content — and the player's own squad — track the recentred composite
// window instead of drifting by one macro cell each crossing.
void rebase_subworld_entities(ecs::World& w, float dxTiles, float dyTiles);

// Signed toroidal offset of macro cell `a` from window centre `c` on a torus of
// circumference `n`. A result in {-1,0,1} means `a` is in the loaded 3×3 window
// at that cell offset; anything else is outside it. THE translation from a
// world address to a window one — everything that must survive a re-centre
// stores the macro cell and asks this at the moment it needs a window position.
int toroidal_cell_offset(int a, int c, int n);

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
// `squadBonuses` follows the same logic as the faction: the LEADER's gift,
// collected once at the call site from whoever leads the squad (the player →
// squad_bonuses over his owned component, squad.h sheet_of) and applied into
// every member's sheet at birth. nullptr = a leaderless context, nothing
// applied.
// `rosterSubject`/`rosterCx`/`rosterCy` — WHOSE roster stock the members'
// death receipts strike (macro/macro_stock.h "roster"): the FLAG record's own
// MacroSpawnId and cell, read at the call site — a worn lord's men pay their
// deaths back into HIS squad, not into the player ordinal (A2, 2026-09-17).
// Defaults name the ordinary hero squad, which is what every fixture has.
void spawn_player_squad(ecs::World& w,
                        const SoldierSquad& squad,
                        const SeamlessSubworldManager& mgr,
                        float playerX,
                        float playerY,
                        std::uint32_t seed,
                        std::uint16_t faction,
                        const BonusTotals* squadBonuses = nullptr,
                        std::int32_t rosterSubject =
                            std::int32_t(ecs::kPlayerSquadOrdinal),
                        std::int16_t rosterCx = 0,
                        std::int16_t rosterCy = 0);

void spawn_player_squad(ecs::World& w,
                        const SoldierSquad& squad,
                        const std::vector<std::uint8_t>& tiles,
                        float playerX,
                        float playerY,
                        std::uint32_t seed,
                        std::uint16_t faction,
                        const BonusTotals* squadBonuses = nullptr,
                        std::int32_t rosterSubject =
                            std::int32_t(ecs::kPlayerSquadOrdinal),
                        std::int16_t rosterCx = 0,
                        std::int16_t rosterCy = 0);

// ── Macro→subworld projection (Inc 5d) ───────────────────────────────────
//
// Project every persistent macro NPC standing in the current 3×3 window into the
// subworld as a real, full combat body — so the lords, bandits, and peasants who
// roam the overworld are physically MET (and, with the possession spell, worn)
// where they actually are. The macro RECORD is authoritative and the projected
// body is its MIRROR: each projection carries a `MacroOrigin` backlink through
// which every lasting write — wounds, spends, loot — goes to the record
// (sub/record.h), so the reaper spares it and leave() can land the player back
// on the right macro cell. («Authoritative AND UNTOUCHED», which this said
// until 2026-09-18, was the fold-up architecture the mirror law replaced.)
//
// A macro NPC is "in the window" when its integer cell is within ±1 of the
// window centre (centerCx,centerCy) on the map torus (mapW×mapH) — the SAME nine
// cells the seamless manager loads. Each projection mirrors the settlement-
// citizen layout: NPCKind/faction and NpcCharacter copied verbatim, HP MIRRORED
// from the record (not «body-native persistent state» — that was the wording
// of the era when a body owned its bars; mirror_bodies_from_record re-pulls
// them every tick top, and the record is where a wound actually lands), Combat
// DERIVED from a fresh universal CharacterSheet, SubworldAi hostility keyed off
// NpcTypeDef.ai (Aggressive→fight, else flee). Placement scatters within the
// cell's sub-region, dodging water.
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
// projected, members included — EVERY macro body standing in the window
// (§42 Инк 6, owner: «потолков нет» — kMaxProjectedMacroNpcs is dead; an
// army of hundreds walks in whole, and the one physical bound left is the
// scene crowd grid, which already shouts when it binds). `seed` should be
// the world seed mixed with the window centre so a re-entry reproduces the
// same scene. The raw-tiles form exists for tests, like the squad spawn
// above; the mgr form delegates to it.
//
// `solids` (optional): the scene's solidity index. A water tile that carries
// a solid above the water plane (a bridge deck, a jetty, a wall walk) is DRY
// FOOTING (sub/height.h is_dry_footing) and a body may materialise on it —
// the placement asks what would hold it up, not what the ground is.
int project_macro_npcs_into_subworld(ecs::World& w,
                                     const SeamlessSubworldManager& mgr,
                                     int centerCx, int centerCy,
                                     int mapW, int mapH,
                                     std::uint32_t seed,
                                     const StructureIndex* solids = nullptr);

int project_macro_npcs_into_subworld(ecs::World& w,
                                     const std::vector<std::uint8_t>& tiles,
                                     int centerCx, int centerCy,
                                     int mapW, int mapH,
                                     std::uint32_t seed,
                                     const StructureIndex* solids = nullptr);

// (Вселение — перенос флажка — живёт в sub/possess.h: header-only дверь,
// которую зовут эффект спелла possession и харнесс, без линковки слоя
// спавна.)

} // namespace sm::sub

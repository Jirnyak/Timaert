#include "macro/save.h"
#include "macro/state.h"
#include "macro/macro_snapshot.h"
#include "macro/deposit_layer.h"
#include "macro/agent_memory.h"
#include "macro/codex.h"
#include "macro/currency.h"
#include "macro/npc.h"
#include "macro/entry_context.h"
#include "events/event_bus.h"
#include "events/quests/quest_engine.h"
#include "events/quests/quest_types.h"

#include <cstdint>
#include <cstdio>
#include "check.h"

#include <cstdlib>
#include <cmath>
#include <string>
#include <vector>

namespace {

// The charged `int fail()` is dead (Testing law #1: tests/check.h is the
// only way to fail). A gate that trips records into the ONE counter nothing
// can invert, then bails exactly as the old flow did; reaching the end of
// run_roundtrip() records the single positive check, so a run that bailed
// early can never read as green and a run that ran nothing fails by count.
#define FAIL_BAIL(msg)                                              \
    do {                                                            \
        ::sm::test::check(false, (msg), __FILE__, __LINE__);        \
        return;                                                     \
    } while (0)

std::string join_path(const std::string& dir, const char* name) {
    if (dir.empty()) return name;
    const char last = dir[dir.size() - 1u];
    if (last == '/' || last == '\\') return dir + name;
#if defined(_WIN32)
    return dir + "\\" + name;
#else
    return dir + "/" + name;
#endif
}

std::string temp_save_path(const char* name) {
    const char* dir = std::getenv("TEMP");
    if (!dir || dir[0] == '\0') dir = std::getenv("TMP");
    if (!dir || dir[0] == '\0') dir = ".";
    return join_path(dir, name);
}

bool read_all(const std::string& path, std::vector<std::uint8_t>& out) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    if (std::fseek(f, 0, SEEK_END) != 0) {
        std::fclose(f);
        return false;
    }
    const long n = std::ftell(f);
    if (n < 0 || std::fseek(f, 0, SEEK_SET) != 0) {
        std::fclose(f);
        return false;
    }
    out.resize(static_cast<std::size_t>(n));
    if (!out.empty()) {
        const std::size_t got = std::fread(out.data(), 1, out.size(), f);
        if (got != out.size()) {
            std::fclose(f);
            return false;
        }
    }
    const bool closeOk = std::fclose(f) == 0;
    return closeOk;
}

void add_soldiers(sm::SoldierSquad& squad, sm::NPCType kind, int count,
                  std::uint32_t idBase) {
    for (int i = 0; i < count; ++i) {
        squad.push(sm::make_soldier(
            static_cast<std::uint8_t>(kind), sm::npc_def(kind).baseLevel,
            idBase + static_cast<std::uint32_t>(i)));
    }
}

bool write_all(const std::string& path, const std::vector<std::uint8_t>& bytes,
               std::size_t limit) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    const std::size_t n = limit < bytes.size() ? limit : bytes.size();
    const std::size_t wrote = n ? std::fwrite(bytes.data(), 1, n, f) : 0;
    const int closeRc = std::fclose(f);
    return wrote == n && closeRc == 0;
}

bool nearf(float a, float b) {
    return std::fabs(a - b) < 0.0001f;
}

bool valid_saved_at(const std::string& s) {
    return s.size() == 24
        && s[4] == '-' && s[7] == '-'
        && s[10] == 'T' && s[13] == ':' && s[16] == ':'
        && s[19] == '.' && s[23] == 'Z';
}

void remove_slot_files(const std::string& path) {
    std::remove(path.c_str());
    std::remove((path + ".tmp").c_str());
    std::remove((path + ".bak").c_str());
}

// The macro-ECS snapshot fixture (v23): two records, every field non-default —
// one living leader with debt, orders and a roster; one dead one, because the
// whole point of the snapshot is that a killed lord STAYS dead across a load.
std::vector<sm::MacroNpcRecord> make_macro_records() {
    std::vector<sm::MacroNpcRecord> out;

    sm::MacroNpcRecord a{};
    a.spawnId.index = 7;
    a.pos = {33.5f, 44.25f, 0.0f};
    a.visual = {33.0f, 44.0f, 1.5f};
    a.kind = {std::uint16_t(sm::NPCType::Bandit), 3};
    a.health = {17.0f, 42.0f};
    a.level = {5};
    a.runtime.homeSettlementId = 2;
    a.runtime.targetSettlementId = 4;
    a.runtime.targetX = 100.0f;
    a.runtime.targetY = 200.0f;
    a.runtime.stateTimer = 11;
    a.runtime.teleportCooldown = 3;
    a.runtime.sp = -25;               // exhaustion DEBT must survive a load
    a.runtime.maxSp = 130;
    a.runtime.travelRank = 2;
    a.runtime.marathonRank = 4;
    a.runtime.moveMult = 1.25f;
    a.runtime.spCarry = 0.5f;
    a.runtime.moveBudget = 0.75f;
    a.runtime.state = 2;
    a.runtime.entryDir = 0x12;
    a.runtime.entryTicks = 9;
    a.runtime.visualSpeed = 0.25f;
    a.runtime.tickAccum = 77;
    a.runtime.xp = 555;               // the leader's campaigns
    a.traits.count = 2;
    a.traits.traits[0] = 1;
    a.traits.traits[1] = 4;
    a.character.visualSeed = 0xABCD1234u;
    a.character.bodyShape = 2;
    a.character.nameIdx = 7;
    a.character.tintR = 10;
    a.character.tintG = 20;
    a.character.tintB = 30;
    a.hasOrders = 1;
    a.orders.waypointCount = 2;
    a.orders.currentWaypoint = 1;
    a.orders.waypoints[0] = 5;
    a.orders.waypoints[1] = 6;
    a.orders.waypoints[2] = 7;
    a.orders.waypoints[3] = 8;
    // A REAL catalog row: the runtime carries ordinals now, so a fabricated
    // id is refused at the door instead of riding to disk and surfacing as
    // "Unknown item" in a panel three systems later.
    a.inventory.add("bread", 3);
    a.roster.push(sm::make_soldier(std::uint16_t(sm::NPCType::Guard), 4, 900u));
    a.roster.push(sm::make_soldier(std::uint16_t(sm::NPCType::Peasant), 2, 901u));
    // v42: a BEAST in the roster. A squad is a squad whatever it is made of
    // (CANON.md S4/S16), and while `kind` was a byte the monster half of the id
    // space (0x100 | catalog row) could not be written down at all — the record
    // was silently dropped by the validity gate on the way out.
    a.roster.push(sm::make_soldier(
        std::uint16_t(sm::NPCType::Wolf), 3, 902u));
    out.push_back(std::move(a));

    sm::MacroNpcRecord d{};
    d.spawnId.index = 9;
    d.pos = {1.0f, 2.0f, 0.0f};
    d.visual = {1.0f, 2.0f, 0.0f};
    d.kind = {std::uint16_t(sm::NPCType::Guard), 1};
    d.health = {0.0f, 55.0f};
    d.level = {3};
    d.dead = 1;
    out.push_back(std::move(d));
    // THE PLAYER's own squad — an ordinary record of the same snapshot, told
    // apart only by its reserved ordinal (owner, 2026-08-27: «игрок = обычный
    // сквад просто с флажком»). His men used to be a field of PlayerState;
    // that they now ride here, by the same law as any lord's warband, is
    // exactly what this record proves.
    sm::MacroNpcRecord player{};
    player.spawnId.index = sm::ecs::kPlayerSquadOrdinal;
    player.pos = {12.0f, 13.0f, 0.0f};
    player.visual = {12.0f, 13.0f, 0.0f};
    player.kind = {std::uint16_t(sm::NPCType::Adventurer),
                   std::uint16_t(sm::faction_index(sm::kPlayerFactionId))};
    player.health = {40.0f, 40.0f};
    player.level = {3};
    // ...and what he WEARS. Equipment is opt-in on the entity, so the record
    // must carry the shape AND the occupied cells: a saved coat that comes
    // back on a naked body is the same class of loss as a saved bag that comes
    // back empty.
    {
        sm::ItemRef coat{};
        coat.def = std::uint16_t(sm::item_index("arm_leather"));
        coat.count = 1;
        coat.affix[0] = {std::uint8_t(sm::BonusId::Vit), 4};
        if (sm::equip(player.gear, coat) < 0) {
            std::fprintf(stderr, "fixture: the coat did not go on\n");
        }
    }
    player.inventory.add("coin_empire", 999);
    player.inventory.add("misc_gem", 3);
    player.inventory.add("bread", 11);
    // The player's HEAD rides his record like any leader's (v28 column): a
    // debt fact, summed by the fact arithmetic.
    sm::remember(player.memory,
                 sm::make_debt_fact(sm::kDebtToSettlement, 7, 15, 3));
    add_soldiers(player.roster, sm::NPCType::Peasant, 4, 1000u);
    add_soldiers(player.roster, sm::NPCType::Woodcutter, 3, 1100u);
    add_soldiers(player.roster, sm::NPCType::Guard, 2, 1200u);
    player.roster.push(sm::SoldierRecord{
        9999u, static_cast<std::uint8_t>(sm::NPCType::Guard), -12});
    out.push_back(player);
    return out;
}

sm::GameState make_state() {
    sm::GameState gs{};
    gs.version = sm::kSaveVersion;
    gs.saveName = "roundtrip";
    gs.worldSeed = 0x12345678u;
    gs.mapW = 512;
    gs.mapH = 256;
    gs.mapParams.seed = float(gs.worldSeed % 100000u);
    gs.mapParams.seaLevel = 0.55f;
    gs.mapParams.heightScale = 1.25f;
    gs.mapParams.moistureScale = 0.75f;
    gs.cityCountTarget = 77;

    // THE WORLD'S OWN MEMORY (CANON S20.1). Both tiers get something to lose:
    // a nameless killing that only the RING should keep, and a lord taking a
    // city that the ANNALS must remember for good. A chronicle that came back
    // empty would cost the witcher his trail and the legends mode its legend.
    sm::chronicle_init(gs.chronicle, gs.mapW, gs.mapH);
    {
        sm::WorldFact anonymous{};
        anonymous.day = 11;
        anonymous.kind = std::uint16_t(sm::FactKind::Killed);
        anonymous.subjectKind = std::uint8_t(sm::FactSubject::Cell);
        anonymous.x = 40; anonymous.y = 40;
        anonymous.amount = 3;
        sm::chronicle_record(gs.chronicle, anonymous);

        sm::WorldFact conquest{};
        conquest.day = 12;
        conquest.kind = std::uint16_t(sm::FactKind::OwnerChanged);
        // Figure-ness is a marked bit for EVERYONE now (2026-08-28): the
        // fixture says outright that this lord and this city are somebodies.
        conquest.subjectKind = sm::fact_subject(sm::FactSubject::Squad, true);
        conquest.subject = 4242u;
        conquest.objectKind =
            sm::fact_subject(sm::FactSubject::Landmark, true);
        conquest.object = 9u;
        conquest.x = 41; conquest.y = 41;
        sm::chronicle_record(gs.chronicle, conquest);
    }
    // The knowledge grid (v40): a real world always covers the map (the
    // reader rejects any other non-zero size). Explored cells prove the
    // memory persists; the Visible one proves the write-side clamp — sight
    // is a projection of where the player stands, never save cargo.
    sm::knowledge_reset(gs.knowledge, gs.mapW, gs.mapH);
    gs.knowledge.data[0] = sm::kKnowledgeExplored;
    gs.knowledge.data[777] = sm::kKnowledgeExplored;
    gs.knowledge.data[1234] = sm::kKnowledgeVisible;
    // Deliberately NOT on a minute boundary: the clock is one integer now, so
    // a save states the instant to the tick, not to the nearest minute.
    gs.worldTime = sm::world_time_at(12, 13, 14);
    gs.worldTime.tick += 3;
    // Autosave/re-bake phase (v22) — non-default so a dropped field reddens.
    gs.lastWorldRebakeDay = 9;
    // Identity issuer (v23): must sit above every fixture ordinal.
    gs.nextMacroSpawnOrdinal = 41;
    // World rhythms (v24) — every field non-default.
    gs.worldTickRt.pendingDailyTicks = 3;
    gs.worldTickRt.nextDailyTickDay = 13;
    gs.worldTickRt.subworldStepRemainder = 11;
    gs.worldTickRt.jitter.state = 0xDEADBEEFu;
    gs.macroAiRhythm.jitter.state = 0xFEEDF00Du;
    gs.macroAiRhythm.sweepAccum = 21;
    gs.macroAiRhythm.pendingSweeps = 2;
    gs.macroAiRhythm.sweepCursor = 57;
    // Story progress (v25): the intro was consumed, chapter 1 is live.
    gs.logicNodesRegistered = {"plot_chapter_1", "sys_settlement"};
    gs.logicNodesActive = {"plot_chapter_1"};

    gs.player.name = "Tester";
    gs.player.ageDays = 1234;
    gs.player.x = 41.5f;
    gs.player.y = 82.25f;
    // (His coin and goods ride his SQUAD's snapshot record — see the player
    // record in make_macro_records — because his bag is an ordinary
    // NpcInventory on his squad entity.)
    // (His HEAD rides the same record: AgentMemory is a column of every
    // leader's macro record, and the player is a leader.)
    gs.player.sheet.attributes[sm::AttributeId::Str] = 7;
    gs.player.sheet.attributes[sm::AttributeId::Vit] = 8;
    gs.player.sheet.attributes[sm::AttributeId::End] = 9;
    gs.player.sheet.attributes[sm::AttributeId::Wil] = 10;
    gs.player.sheet.attributes[sm::AttributeId::Intl] = 11;
    gs.player.sheet.attributes[sm::AttributeId::Wis] = 12;
    gs.player.sheet.attributes[sm::AttributeId::Lck] = 13;
    gs.player.sheet.attributes[sm::AttributeId::Cha] = 14;
    gs.player.sheet.attributes[sm::AttributeId::Spd] = 15;
    gs.player.sheet.skills[sm::SkillId::Bodybuilding] = 1;
    gs.player.sheet.skills[sm::SkillId::Meditation] = 2;
    gs.player.sheet.skills[sm::SkillId::Travel] = 3;
    gs.player.sheet.skills[sm::SkillId::Fighter] = 4;
    gs.player.sheet.skills[sm::SkillId::Marathon] = 5;
    gs.player.sheet.skills[sm::SkillId::Spellcraft] = 6;
    gs.player.sheet.skills[sm::SkillId::Weightlifting] = 7;
    gs.player.sheet.levelData.level = 6;
    gs.player.sheet.levelData.exp = 321;
    gs.player.sheet.levelData.expToNext = 6543;
    gs.player.sheet.levelData.attributePoints = 4;
    gs.player.sheet.levelData.skillPoints = 5;
    gs.player.sheet.levelData.perkPoints = 6;
    gs.player.combatStats.currentHp = 33;
    gs.player.combatStats.maxHp = 111;
    gs.player.combatStats.currentMp = 44;
    gs.player.combatStats.maxMp = 222;
    gs.player.combatStats.currentSp = 55;
    gs.player.combatStats.maxSp = 333;
    gs.player.combatStats.hpRegen = 1.25f;
    gs.player.combatStats.mpRegen = 2.5f;
    gs.player.combatStats.spRegen = 3.75f;
    sm::add_perk(gs.player.sheet.perks, sm::PerkID::Natural);
    sm::add_perk(gs.player.sheet.perks, sm::PerkID::Educated);
    gs.player.codexUnlockedBits = sm::codex_bit(sm::CodexArticleId::Witches)
                                | sm::codex_bit(sm::CodexArticleId::Market);
    {   // the JOURNAL (v58): two learned facts ride the save entry-for-entry
        sm::WorldFact jf{};
        jf.day = 12;
        jf.kind = std::uint16_t(sm::FactKind::Killed);
        jf.subjectKind = sm::fact_subject(sm::FactSubject::Squad, true);
        jf.subject = sm::ecs::kPlayerSquadOrdinal;
        jf.x = 3; jf.y = 4; jf.amount = 2;
        gs.player.journal.push_back(jf);
        jf.kind = std::uint16_t(sm::FactKind::Traded);
        jf.amount = 55;
        gs.player.journal.push_back(jf);
        gs.player.journalSeenSeq = 77u;
    }
    // The book speaks ORDINALS of the real registry now (v59) — a fixture
    // can no longer invent a spell the table does not hold, which is the law
    // working, not a test limitation.
    const int sparkOrd = sm::spell_ordinal("fireball");
    const int hasteOrd = sm::spell_ordinal("haste");
    sm::spellbook_learn(gs.player.spellBook, sparkOrd);
    sm::spellbook_set_active(gs.player.spellBook, sparkOrd);
    gs.player.spellBook.cooldownSteps[sparkOrd] = sm::steps_from_seconds(2.5f);
    sm::spellbook_learn(gs.player.spellBook, hasteOrd);
    sm::spellbook_toggle_sustained(gs.player.spellBook, hasteOrd);
    gs.player.factionPeaceUntilDay[
        std::size_t(sm::ensure_faction_slot(gs, "guild"))] = 55;
    gs.player.settledQuestOffers.push_back(
        {/*giverSettlementId*/ 7, /*bornDay*/ 3, /*offerSlot*/ 2});
    gs.player.completedQuestCount = 5u;
    gs.player.failedQuestCount = 2u;
    // Entry-side context (kSaveVersion 15): a non-default direction + tick
    // count must survive the trip — a save made mid-march re-enters the
    // subworld on the same side.
    gs.player.entryDir = sm::pack_entry_dir(0, 1);   // walked in from the south
    gs.player.entryTicks = 7;
    // (The player's MEN are not a field of PlayerState any more: his squad is
    // an ordinary squad entity, so his roster rides the macro snapshot with
    // every other squad's — see the player record in make_macro_records.)
    add_soldiers(gs.deserterPool, sm::NPCType::Woodcutter, 2, 1300u);

    sm::Landmark settlement{};
    settlement.type = sm::LandmarkType::City;
    settlement.id = 7;
    settlement.name = "Round City";
    settlement.x = 40;
    settlement.y = 80;
    settlement.population = 777;
    settlement.mood = sm::SettlementMood::Tense;
    settlement.inventory.add("wood", 19);
    settlement.history.push(1, 700);
    settlement.history.push(12, 777);
    add_soldiers(settlement.garrison, sm::NPCType::Guard, 5, 2000u);
    add_soldiers(settlement.garrison, sm::NPCType::Peasant, 1, 2100u);
    settlement.kingdomIdx = 2;
    // Honest-day readouts (v29) — every field non-default.
    settlement.starvedYesterday = 12;
    settlement.unmetYesterday = 34;
    settlement.famineActive = 1;
    settlement.popGrowthCarry = 0.375f;
    gs.landmarks.push_back(settlement);

    sm::Landmark village{};
    village.type = sm::LandmarkType::Village;
    village.id = 70;
    village.name = "Round Hamlet";
    village.x = 45;
    village.y = 85;
    village.population = 111;
    village.mood = sm::SettlementMood::Stable;
    village.inventory.add("food_meat", 4);
    village.history.push(3, 90);
    village.history.push(10, 111);
    village.nearestCityId = settlement.id;
    village.kingdomIdx = 2;
    village.starvedYesterday = 5;
    village.unmetYesterday = 7;
    village.famineActive = 1;
    village.popGrowthCarry = -0.25f;
    gs.landmarks.push_back(village);

    sm::Landmark spire{};
    spire.type = sm::LandmarkType::Spire;
    spire.id = 3;
    spire.x = 12;
    spire.y = 34;
    spire.spellId = 99;
    spire.depleted = true;
    gs.landmarks.push_back(spire);

    sm::Marker marker{};
    marker.id = "marker.round";
    marker.style = sm::MarkerStyle::Danger;
    marker.x = 15.5f;
    marker.y = 16.25f;
    marker.label = "Round Danger";
    gs.markers.push_back(marker);

    // A faction the REGISTRY has never heard of — a guild that formed in play.
    // It claims one of the matrix's reserved tail slots (macro/relations.h) and
    // is an ordinary row from that moment: it can hold a relation with another
    // runtime faction, and it survives the save because its claimed NAME does.
    const sm::FactionSlot guild = sm::ensure_faction_slot(gs, "guild");
    const sm::FactionSlot other = sm::ensure_faction_slot(gs, "other");
    sm::set_relation(gs.relations, guild, other, -5);
    // Standing is a pair in the same matrix, not a player-side map: this adds
    // the player pair to the very row above, and must not disturb it.
    sm::add_player_reputation(gs, "guild", 42);

    gs.subState.kind = sm::GameSubStateKind::Trading;
    gs.subState.settlementId = settlement.id;
    gs.subState.eventId = "event.round";
    gs.subState.enemyId = "enemy.round";
    gs.subState.pendingEncounterIdx = 4;



    // v33: sparse fauna-count overrides — a hunted cell and an emptied one.
    gs.resourceScars[std::size_t(sm::ResourceFieldId::Fauna)][42u * 1024u + 17u] = 3u;
    gs.resourceScars[std::size_t(sm::ResourceFieldId::Fauna)][11u] = 0u;
    // v34: sparse crop-harvest scars — a reaped cell.
    gs.resourceScars[std::size_t(sm::ResourceFieldId::Wheat)][23u * 1024u + 5u] = 12u;

    return gs;
}

// v36: the living tree grid rides the save WHOLE (the forest is the
// registry's carrier row) — a small distinctive grid stands in for the
// world's; the save layer does not couple its size to mapW.
std::vector<std::uint16_t> make_tree_counts() {
    std::vector<std::uint16_t> treeCounts(64, 600u);
    treeCounts[17] = 12000u;   // a thickened cell
    treeCounts[7]  = 0u;       // a clear-cut cell - must NOT resurrect
    return treeCounts;
}

// v37: the deposit cells ride the save whole, one sparse map per kind. The
// fixture holds a part-drained stone cell, a DRY iron cell (remaining 0 is a
// visible fact, not an absence), and one cell carrying BOTH kinds — the
// discovered-vein-in-a-quarry case the per-kind storage exists for.
sm::DepositLayer make_deposits() {
    sm::DepositLayer d;
    d.width = 512;
    d.height = 256;
    d.cells[std::size_t(sm::DepositKind::Stone)][99u] = 1500;
    d.cells[std::size_t(sm::DepositKind::Iron)][100u] = 0;
    d.cells[std::size_t(sm::DepositKind::Stone)][100u] = 60000;
    d.cells[std::size_t(sm::DepositKind::Clay)][7u] = 4096;
    return d;
}

sm::Quest make_quest() {
    sm::Quest q{};
    // Identity/provenance (v63): the ordinal is issued by accept (left 0
    // here); the offer triple must survive the trip.
    q.offerSlot = 4;
    q.bornDay = 3;
    q.title = "Active Quest";
    q.description = "Roundtrip active quest";
    q.category = sm::QuestCategory::Procedural;
    q.giverSettlementId = 7;

    // Every Objective field non-default — a dropped field must redden.
    sm::Objective objective{};
    objective.kind = sm::ObjectiveKind::WaitAt;
    objective.ix = 40;
    objective.iy = 80;
    objective.cellX = 5;
    objective.cellY = 6;
    objective.subX = 512;
    objective.subY = 640;
    objective.radius = 3.0f;
    objective.itemId = "itm_bread";
    objective.quantity = 4;
    objective.targetSettlementId = 9;
    objective.npcType = 11;
    objective.count = 3;
    objective.killed = 1;
    objective.zoneRadius = 2.5f;
    objective.hoursRequired = 3;
    objective.hoursWaited = 1;
    objective.action = "chop";
    q.objectives.push_back(objective);

    sm::Reward reward{};
    reward.kind = sm::RewardKind::Gold;
    reward.amount = 170;
    q.rewards.push_back(reward);

    sm::GameEvent ev{sm::EventTag::QuestStart};
    q.onAccept.push_back(ev);
    sm::GameEvent spawn{sm::EventTag::SpawnEntity};
    spawn.s1 = "bandit";
    spawn.ix = 12;
    spawn.iy = 14;
    spawn.a = 3;
    q.onAccept.push_back(spawn);
    sm::GameEvent enter{sm::EventTag::PlayerEnterSettlement};
    enter.s1 = "Round City";
    enter.a = 7;
    enter.ix = 7;
    q.onAccept.push_back(enter);
    sm::GameEvent leave{sm::EventTag::PlayerLeaveSettlement};
    leave.s1 = "Round City";
    leave.a = 7;
    leave.ix = 7;
    q.onAccept.push_back(leave);
    // The battery below covers every payload FIELD (a, ix, iy, fx/fy, s1, s2)
    // across distinct surviving tags — the original used the since-deleted
    // dead tags; the field coverage is what matters to the serializer.
    sm::GameEvent rep{sm::EventTag::ReputationChange};
    rep.a = 42;
    rep.ix = -5;
    q.onAccept.push_back(rep);
    sm::GameEvent battle{sm::EventTag::BattleStart};
    battle.s1 = "Unrest";
    battle.s2 = "Prosperous";
    battle.a = 7;
    battle.ix = 7;
    q.onAccept.push_back(battle);
    sm::GameEvent effect{sm::EventTag::ApplyEffect};
    effect.s1 = "hp";
    effect.ix = 10;
    effect.iy = 12;
    q.onAccept.push_back(effect);
    sm::GameEvent codex{sm::EventTag::CodexUnlock};
    codex.s1 = "Bandit";
    codex.ix = 1;
    codex.a = 23;
    q.onAccept.push_back(codex);
    sm::GameEvent cell{sm::EventTag::WorldCellChange};
    cell.ix = 4;
    cell.iy = 5;
    cell.fx = 0.75f;
    q.onAccept.push_back(cell);
    sm::GameEvent visit{sm::EventTag::SettlementVisit};
    visit.s1 = "realm_a";
    visit.s2 = "realm_b";
    visit.ix = -10;
    visit.iy = 15;
    q.onAccept.push_back(visit);
    sm::GameEvent learned{sm::EventTag::SpellLearned};
    learned.s1 = "dlg_intro";
    learned.a = 42;
    q.onAccept.push_back(learned);
    sm::GameEvent advance{sm::EventTag::TimeAdvance};
    advance.fx = 12.5f;
    advance.fy = 18.25f;
    q.onAccept.push_back(advance);
    q.expireDay = 99;
    q.difficulty = 2;
    return q;
}

void run_roundtrip() {
    if (static_cast<std::uint8_t>(sm::GameSubStateKind::Event) != 4u) {
        FAIL_BAIL("unexpected sub-state enum layout");
    }

    const std::string path = temp_save_path("timaert_save_roundtrip_v8.bin");
    const std::string truncatedPath = temp_save_path("timaert_save_roundtrip_v8_truncated.bin");
    const std::string corruptPath = temp_save_path("timaert_save_roundtrip_v8_corrupt.bin");
    const std::string badVersionPath = temp_save_path("timaert_save_roundtrip_v8_bad_version.bin");

    remove_slot_files(path);
    remove_slot_files(truncatedPath);
    remove_slot_files(corruptPath);
    remove_slot_files(badVersionPath);

    sm::GameState gs = make_state();
    const std::vector<std::uint16_t> treeCounts = make_tree_counts();
    const sm::DepositLayer deposits = make_deposits();
    sm::EventBus bus;
    sm::QuestEngine questEngine;
    std::vector<sm::Quest> quests;
    questEngine.accept(quests, make_quest(), gs, bus);
    if (quests.size() != 1) FAIL_BAIL("QuestEngine::accept did not activate quest");
    bool sawQuestStart = false;
    for (const auto& ev : bus.tick_events())
        sawQuestStart = sawQuestStart || ev.tag == sm::EventTag::QuestStart;
    if (!sawQuestStart) {
        FAIL_BAIL("QuestEngine::accept did not emit QuestStart");
    }

    const std::vector<sm::MacroNpcRecord> macroFixture = make_macro_records();
    if (!sm::save_game(gs, quests, macroFixture, treeCounts, deposits,
                       path)) {
        FAIL_BAIL("save_game returned false");
    }

    std::vector<std::uint8_t> bytes;
    if (!read_all(path, bytes)) FAIL_BAIL("save file unreadable");
    if (bytes.empty()) FAIL_BAIL("save file is empty");

    const sm::SaveSummary summary = sm::inspect_save(path);
    if (summary.status != sm::SaveInspectStatus::Ready) FAIL_BAIL("inspect not ready");
    if (summary.version != sm::kSaveVersion) FAIL_BAIL("version mismatch");
    if (summary.worldSeed != gs.worldSeed) FAIL_BAIL("summary seed mismatch");
    if (summary.day != gs.worldTime.day()) FAIL_BAIL("summary day mismatch");
    if (summary.saveName != "roundtrip") FAIL_BAIL("summary save name mismatch");
    if (!valid_saved_at(summary.savedAt)) FAIL_BAIL("summary savedAt invalid");

    sm::GameState loaded{};
    std::vector<sm::Quest> loadedQuests;
    std::vector<sm::MacroNpcRecord> loadedMacro;
    std::vector<std::uint16_t> loadedTrees;
    sm::DepositLayer loadedDeposits;
    if (!sm::load_game(loaded, loadedQuests, loadedMacro, loadedTrees,
                       loadedDeposits, path)) {
        FAIL_BAIL("load_game failed");
    }
    if (loaded.version != sm::kSaveVersion) FAIL_BAIL("loaded version mismatch");
    if (loaded.saveName != "roundtrip") FAIL_BAIL("save name lost");
    if (loaded.savedAt != summary.savedAt) FAIL_BAIL("savedAt lost");
    if (loaded.worldSeed != gs.worldSeed) FAIL_BAIL("world seed lost");
    if (loaded.mapW != 512 || loaded.mapH != 256) FAIL_BAIL("map size lost");
    if (loaded.cityCountTarget != 77) FAIL_BAIL("city target lost");
    if (!nearf(loaded.mapParams.seaLevel, 0.55f)
        || !nearf(loaded.mapParams.heightScale, 1.25f)
        || !nearf(loaded.mapParams.moistureScale, 0.75f)) {
        FAIL_BAIL("map params lost");
    }
    if (loaded.worldTime.day() != 12 || loaded.worldTime.hour() != 13
        || loaded.worldTime.minute() != 14) {
        FAIL_BAIL("world time lost");
    }
    if (loaded.lastWorldRebakeDay != 9) {
        FAIL_BAIL("lastWorldRebakeDay (autosave phase) lost");
    }
    if (loaded.nextMacroSpawnOrdinal != 41) {
        FAIL_BAIL("nextMacroSpawnOrdinal (identity issuer) lost");
    }
    if (loaded.worldTickRt.pendingDailyTicks != 3
        || loaded.worldTickRt.nextDailyTickDay != 13
        || loaded.worldTickRt.subworldStepRemainder != 11
        || loaded.worldTickRt.jitter.state != 0xDEADBEEFu) {
        FAIL_BAIL("world-tick runtime (queue/remainder/jitter) lost");
    }
    if (loaded.macroAiRhythm.jitter.state != 0xFEEDF00Du
        || loaded.macroAiRhythm.sweepAccum != 21
        || loaded.macroAiRhythm.pendingSweeps != 2
        || loaded.macroAiRhythm.sweepCursor != 57) {
        FAIL_BAIL("macro-AI rhythm lost");
    }
    if (loaded.logicNodesRegistered.size() != 2
        || loaded.logicNodesRegistered[0] != "plot_chapter_1"
        || loaded.logicNodesActive.size() != 1
        || loaded.logicNodesActive[0] != "plot_chapter_1") {
        FAIL_BAIL("story progress (logic nodes) lost");
    }

    // ── The knowledge grid (v40) ──────────────────────────────────────────
    if (loaded.knowledge.width != loaded.mapW
        || loaded.knowledge.height != loaded.mapH
        || !loaded.knowledge.has_complete_storage()) {
        FAIL_BAIL("knowledge grid does not cover the loaded map");
    }
    if (loaded.knowledge.data[0] != sm::kKnowledgeExplored
        || loaded.knowledge.data[777] != sm::kKnowledgeExplored) {
        FAIL_BAIL("explored cells (the player's memory) lost");
    }
    if (loaded.knowledge.data[1234] != sm::kKnowledgeExplored) {
        FAIL_BAIL("Visible must decay to Explored across a save - sight is a "
                  "projection, not cargo");
    }
    if (loaded.knowledge.data[42] != sm::kKnowledgeUnknown) {
        FAIL_BAIL("an unvisited cell invented knowledge across the save");
    }

    // ── The world's own memory comes back, both tiers ────────────────────
    {
        const sm::Chronicle& ch = loaded.chronicle;
        if (ch.nextSeq != gs.chronicle.nextSeq) {
            FAIL_BAIL("the chronicle's sequence lost — every fact's identity "
                      "and every chain link is that number");
        }
        if (ch.annals.size() != 1u
            || ch.annals[0].subject != 4242u
            || ch.annals[0].object != 9u
            || ch.annals[0].kind != std::uint16_t(sm::FactKind::OwnerChanged)) {
            FAIL_BAIL("the annals lost the lord who took the city");
        }
        // The RING must come back ASKABLE, not merely present: the chains are
        // derived on load, and a chronicle whose links did not rebuild would
        // answer nothing while looking perfectly full.
        struct Seen { int n = 0; bool anonymous = false; bool conquest = false; };
        Seen seen;
        sm::chronicle_near(ch, 40, 40, /*radiusCells*/1, /*sinceDay*/0,
                           [](void* u, const sm::WorldFact& f) {
                               Seen& s2 = *static_cast<Seen*>(u);
                               ++s2.n;
                               if (f.kind == std::uint16_t(sm::FactKind::Killed))
                                   s2.anonymous = true;
                               if (f.kind == std::uint16_t(sm::FactKind::OwnerChanged))
                                   s2.conquest = true;
                           }, &seen);
        if (!seen.anonymous) {
            FAIL_BAIL("the ring came back unaskable: the witcher lost the "
                      "trail across a save");
        }
        if (!seen.conquest || seen.n != 2) {
            FAIL_BAIL("the rebuilt chains did not return every nearby fact");
        }
    }

    // ── The macro-ECS snapshot (v23) round-trips record-for-record ────────
    if (loadedMacro.size() != macroFixture.size()) {
        FAIL_BAIL("macro snapshot record count lost");
    }
    // What the PLAYER wears comes back on him. Equipment is opt-in on the
    // entity and its cells are a flat array with holes, so the file carries
    // cell indices — and a saved coat that returned on a naked body would be
    // the same class of loss as a saved bag that returned empty.
    {
        const sm::MacroNpcRecord* worn = nullptr;
        for (const sm::MacroNpcRecord& r : loadedMacro) {
            if (r.spawnId.index == sm::ecs::kPlayerSquadOrdinal) worn = &r;
        }
        if (!worn) FAIL_BAIL("the player's own record did not come back");
        if (sm::worn_cells(worn->gear) != 1) FAIL_BAIL("worn gear lost");
        int cell = -1;
        for (int i = 0; i < worn->gear.cells(); ++i) {
            if (!worn->gear.worn[std::size_t(i)].empty()) cell = i;
        }
        if (cell < 0 || worn->gear.part_at(cell) != sm::BodyPartId::Torso) {
            FAIL_BAIL("the coat came back on the wrong part of him");
        }
        const sm::ItemRef& coat = worn->gear.worn[std::size_t(cell)];
        if (coat.def != std::uint16_t(sm::item_index("arm_leather"))
            || coat.count != 1) {
            FAIL_BAIL("the coat came back as another item");
        }
        // The rolled affix rides with it: a procedural item that lost its
        // roll would be a different item wearing the same name.
        if (coat.affix[0].row != std::uint8_t(sm::BonusId::Vit)
            || coat.affix[0].value != 4) {
            FAIL_BAIL("the coat's rolled affix lost");
        }
        if (sm::worn_armor(worn->gear) <= 0) {
            FAIL_BAIL("and it stops nothing, so the row did not come back");
        }
    }
    {
        const sm::MacroNpcRecord& a = loadedMacro[0];
        const sm::MacroNpcRecord& want = macroFixture[0];
        if (a.spawnId.index != want.spawnId.index) FAIL_BAIL("macro ordinal lost");
        if (a.pos.x != want.pos.x || a.pos.y != want.pos.y) {
            FAIL_BAIL("macro position lost");
        }
        if (a.kind.type != want.kind.type
            || a.kind.factionIdx != want.kind.factionIdx) {
            FAIL_BAIL("macro kind/faction lost");
        }
        if (a.health.hp != want.health.hp || a.health.maxHp != want.health.maxHp) {
            FAIL_BAIL("macro wounds lost");
        }
        if (a.level.value != want.level.value) FAIL_BAIL("macro level lost");
        if (a.runtime.sp != -25) FAIL_BAIL("macro SP debt lost");
        if (a.runtime.xp != 555) FAIL_BAIL("macro leader xp lost");
        if (a.runtime.targetX != want.runtime.targetX
            || a.runtime.state != want.runtime.state
            || a.runtime.entryDir != want.runtime.entryDir
            || a.runtime.tickAccum != want.runtime.tickAccum) {
            FAIL_BAIL("macro runtime state lost");
        }
        if (a.traits.count != 2 || a.traits.traits[1] != 4) {
            FAIL_BAIL("macro traits lost");
        }
        if (a.character.visualSeed != 0xABCD1234u
            || a.character.nameIdx != 7) {
            FAIL_BAIL("macro character identity lost");
        }
        if (a.hasOrders != 1 || a.orders.waypointCount != 2
            || a.orders.currentWaypoint != 1
            || a.orders.waypoints[3] != 8) {
            FAIL_BAIL("macro squad orders lost");
        }
        if (a.dead != 0) FAIL_BAIL("living macro NPC loaded dead");
        if (a.inventory.used_slots() != 1
            || a.inventory.count("bread") != 3) {
            FAIL_BAIL("macro inventory lost");
        }
        if (a.roster.size() != 3
            || a.roster[0].kind != std::uint16_t(sm::NPCType::Guard)
            || a.roster[0].level != 4
            || a.roster[1].entityId != 901u) {
            FAIL_BAIL("macro roster lost");
        }
        // The beast came back a beast — not truncated to its low byte, not
        // dropped, not turned into whatever humanoid that byte would name.
        if (a.roster[2].kind != std::uint16_t(sm::NPCType::Wolf)
            || a.roster[2].level != 3
            || a.roster[2].entityId != 902u
            || !sm::is_monster_kind(a.roster[2].kind)) {
            FAIL_BAIL("a beast member did not survive the save");
        }
    }
    if (loadedMacro[1].dead != 1 || loadedMacro[1].health.hp != 0.0f) {
        FAIL_BAIL("the killed lord did not stay dead across the save");
    }
    if (loaded.worldTime.tick != gs.worldTime.tick) {
        FAIL_BAIL("world time not exact to the tick");
    }

    const sm::PlayerState& p = loaded.player;
    if (p.name != "Tester") FAIL_BAIL("player name lost");
    if (p.ageDays != 1234 || !nearf(p.x, 41.5f) || !nearf(p.y, 82.25f)) {
        FAIL_BAIL("player position or age lost");
    }

    if (p.sheet.attributes.of(sm::AttributeId::Str) != 7 || p.sheet.attributes.of(sm::AttributeId::Intl) != 11 || p.sheet.attributes.of(sm::AttributeId::Spd) != 15) {
        FAIL_BAIL("player attributes lost");
    }
    if (p.sheet.skills.of(sm::SkillId::Bodybuilding) != 1 || p.sheet.skills.of(sm::SkillId::Spellcraft) != 6
        || p.sheet.skills.of(sm::SkillId::Weightlifting) != 7) {
        FAIL_BAIL("player skills lost");
    }
    if (p.sheet.levelData.level != 6 || p.sheet.levelData.exp != 321
        || p.sheet.levelData.expToNext != 6543
        || p.sheet.levelData.attributePoints != 4
        || p.sheet.levelData.skillPoints != 5
        || p.sheet.levelData.perkPoints != 6) {
        FAIL_BAIL("player level data lost");
    }
    if (p.combatStats.currentHp != 33 || p.combatStats.maxMp != 222
        || !nearf(p.combatStats.spRegen, 3.75f)) {
        FAIL_BAIL("player combat stats lost");
    }
    if (!sm::has_perk(p.sheet.perks, sm::PerkID::Natural)
        || !sm::has_perk(p.sheet.perks, sm::PerkID::Educated)) {
        FAIL_BAIL("player perks lost");
    }

    if (sm::player_reputation(&loaded, "guild") != 42) {
        FAIL_BAIL("player standing lost (his row in the faction matrix)");
    }
    if (sm::faction_relation(&loaded, "guild", sm::kPlayerFactionId) != 42) {
        FAIL_BAIL("player standing is not symmetric after a save round-trip");
    }
    if (p.entryDir != sm::pack_entry_dir(0, 1) || p.entryTicks != 7) {
        FAIL_BAIL("entry-side context lost");
    }


    if (p.journal.size() != 2
        || p.journal[0].kind != std::uint16_t(sm::FactKind::Killed)
        || p.journal[1].amount != 55
        || p.journalSeenSeq != 77u) {
        FAIL_BAIL("the player's journal did not round-trip entry-for-entry");
    }
    if (!sm::spellbook_has_learned(p.spellBook, sm::spell_ordinal("fireball"))
        || !sm::spellbook_has_learned(p.spellBook,
                                      sm::spell_ordinal("haste"))) {
        FAIL_BAIL("spell learned state lost");
    }
    if (p.spellBook.activeSpell != sm::spell_ordinal("fireball")) {
        FAIL_BAIL("active spell lost");
    }
    // Steps are integers: an exact compare, not a float tolerance. The old
    // nearf() here was tolerance for a quantity that never needed any.
    if (p.spellBook.cooldownSteps[sm::spell_ordinal("fireball")]
        != sm::steps_from_seconds(2.5f)) {
        FAIL_BAIL("spell cooldown lost");
    }
    if (!sm::spellbook_has_sustained(p.spellBook,
                                     sm::spell_ordinal("haste"))) {
        FAIL_BAIL("sustained spell state lost");
    }
    if (p.completedQuestCount != 5u || p.failedQuestCount != 2u) {
        FAIL_BAIL("quest completion tallies lost");
    }
    if (p.settledQuestOffers.size() != 1u
        || p.settledQuestOffers[0].giverSettlementId != 7
        || p.settledQuestOffers[0].bornDay != 3
        || p.settledQuestOffers[0].offerSlot != 2) {
        FAIL_BAIL("settled quest offers lost");
    }
    const sm::Landmark* cityLm = sm::landmark_by_id(loaded, 7);
    if (!cityLm || cityLm->type != sm::LandmarkType::City
        || cityLm->population != 777) {
        FAIL_BAIL("settlement lost");
    }
    const sm::Landmark& city = *cityLm;
    if (city.name != "Round City" || city.mood != sm::SettlementMood::Tense
        || city.inventory.count("wood") != 19
        || city.history.size() != 2 || city.history.population_at(1) != 777
        || city.history.day_at(0) != 1
        || sm::count_soldiers_of_kind(
            city.garrison, static_cast<std::uint8_t>(sm::NPCType::Peasant)) != 1) {
        FAIL_BAIL("settlement details lost");
    }
    if (city.starvedYesterday != 12 || city.unmetYesterday != 34
        || city.famineActive != 1 || !nearf(city.popGrowthCarry, 0.375f)) {
        FAIL_BAIL("settlement honest-day readouts (v29) lost");
    }
    const sm::Landmark* vilLm = sm::landmark_by_id(loaded, 70);
    if (!vilLm || vilLm->type != sm::LandmarkType::Village
        || vilLm->starvedYesterday != 5
        || vilLm->unmetYesterday != 7
        || vilLm->famineActive != 1
        || !nearf(vilLm->popGrowthCarry, -0.25f)) {
        FAIL_BAIL("village honest-day readouts (v29) lost");
    }
    const sm::Landmark* spireLm = sm::landmark_by_id(loaded, 3);
    if (!spireLm || spireLm->type != sm::LandmarkType::Spire
        || !spireLm->depleted || spireLm->spellId != 99) {
        FAIL_BAIL("spire lost");
    }
    if (loaded.markers.empty() || loaded.markers[0].id != "marker.round"
        || loaded.markers[0].style != sm::MarkerStyle::Danger) {
        FAIL_BAIL("marker lost");
    }
    const sm::FactionSlot loadedGuild =
        sm::faction_slot(loaded.relations, "guild");
    const sm::FactionSlot loadedOther =
        sm::faction_slot(loaded.relations, "other");
    if (loadedGuild == sm::kNoFactionSlot || loadedOther == sm::kNoFactionSlot) {
        FAIL_BAIL("runtime faction lost its claimed slot");
    }
    if (sm::relation_of(loaded.relations, loadedGuild, loadedOther) != -5) {
        FAIL_BAIL("faction relation lost");
    }
    if (loaded.player.factionPeaceUntilDay[std::size_t(loadedGuild)] != 55) {
        FAIL_BAIL("truce clock lost");
    }
    if (loaded.subState.kind != sm::GameSubStateKind::Trading
        || loaded.subState.settlementId != 7
        || loaded.subState.pendingEncounterIdx != 4) {
        FAIL_BAIL("sub-state lost");
    }
    if (sm::count_soldiers_of_kind(
            loaded.deserterPool, static_cast<std::uint8_t>(sm::NPCType::Woodcutter)) != 2) {
        FAIL_BAIL("deserter pool lost");
    }
    for (std::size_t k = 0; k < std::size_t(sm::kDepositKindCount); ++k) {
        if (loadedDeposits.cells[k] != deposits.cells[k]) {
            FAIL_BAIL("deposit cells lost (kind block mismatch)");
        }
    }
    if (loadedDeposits.cells[std::size_t(sm::DepositKind::Iron)].at(100u) != 0
        || loadedDeposits.cells[std::size_t(sm::DepositKind::Stone)].at(100u)
               != 60000) {
        FAIL_BAIL("the dry vein and its host quarry did not both survive");
    }
    if (loadedTrees != treeCounts) FAIL_BAIL("tree grid lost");
    if (loadedTrees.at(7) != 0u || loadedTrees.at(17) != 12000u) {
        FAIL_BAIL("felled/thickened tree cells did not round-trip");
    }
    if (loaded.resourceScars[std::size_t(sm::ResourceFieldId::Fauna)].size() != 2
        || loaded.resourceScars[std::size_t(sm::ResourceFieldId::Fauna)].at(42u * 1024u + 17u) != 3u
        || loaded.resourceScars[std::size_t(sm::ResourceFieldId::Fauna)].at(11u) != 0u) {
        FAIL_BAIL("fauna overrides lost");
    }
    if (loaded.resourceScars[std::size_t(sm::ResourceFieldId::Wheat)].size() != 1
        || loaded.resourceScars[std::size_t(sm::ResourceFieldId::Wheat)].at(23u * 1024u + 5u) != 12u) {
        FAIL_BAIL("crop harvest scars lost");
    }
    if (loadedQuests.size() != 1 || loadedQuests[0].ordinal != 1u
        || loadedQuests[0].offerSlot != 4
        || loadedQuests[0].bornDay != 3
        || loaded.nextQuestOrdinal < 2u) {
        FAIL_BAIL("active quest lost");
    }
    if (!loadedQuests[0].objectives.empty()) {
        const sm::Objective& o = loadedQuests[0].objectives[0];
        if (o.cellX != 5 || o.cellY != 6 || o.subX != 512 || o.subY != 640
            || o.itemId != "itm_bread" || o.quantity != 4
            || o.targetSettlementId != 9 || o.npcType != 11
            || o.count != 3 || o.killed != 1 || o.zoneRadius != 2.5f
            || o.action != "chop") {
            FAIL_BAIL("objective fields lost");
        }
    }
    if (loadedQuests[0].objectives.empty()
        || loadedQuests[0].objectives[0].hoursWaited != 1
        || loadedQuests[0].rewards.empty()
        || loadedQuests[0].rewards[0].amount != 170
        || loadedQuests[0].onAccept.size() != 12
        || loadedQuests[0].onAccept[0].tag != sm::EventTag::QuestStart
        || loadedQuests[0].onAccept[1].tag != sm::EventTag::SpawnEntity
        || loadedQuests[0].onAccept[1].s1 != "bandit"
        || loadedQuests[0].onAccept[1].a != 3u
        || loadedQuests[0].onAccept[2].tag != sm::EventTag::PlayerEnterSettlement
        || loadedQuests[0].onAccept[2].s1 != "Round City"
        || loadedQuests[0].onAccept[2].a != 7u
        || loadedQuests[0].onAccept[2].ix != 7
        || loadedQuests[0].onAccept[3].tag != sm::EventTag::PlayerLeaveSettlement
        || loadedQuests[0].onAccept[3].s1 != "Round City"
        || loadedQuests[0].onAccept[3].a != 7u
        || loadedQuests[0].onAccept[3].ix != 7
        || loadedQuests[0].onAccept[4].tag != sm::EventTag::ReputationChange
        || loadedQuests[0].onAccept[4].a != 42u
        || loadedQuests[0].onAccept[4].ix != -5
        || loadedQuests[0].onAccept[5].tag != sm::EventTag::BattleStart
        || loadedQuests[0].onAccept[5].s1 != "Unrest"
        || loadedQuests[0].onAccept[5].s2 != "Prosperous"
        || loadedQuests[0].onAccept[6].tag != sm::EventTag::ApplyEffect
        || loadedQuests[0].onAccept[6].s1 != "hp"
        || loadedQuests[0].onAccept[6].ix != 10
        || loadedQuests[0].onAccept[6].iy != 12
        || loadedQuests[0].onAccept[7].tag != sm::EventTag::CodexUnlock
        || loadedQuests[0].onAccept[7].s1 != "Bandit"
        || loadedQuests[0].onAccept[7].ix != 1
        || loadedQuests[0].onAccept[7].a != 23u
        || loadedQuests[0].onAccept[8].tag != sm::EventTag::WorldCellChange
        || loadedQuests[0].onAccept[8].ix != 4
        || loadedQuests[0].onAccept[8].iy != 5
        || loadedQuests[0].onAccept[8].fx != 0.75f
        || loadedQuests[0].onAccept[9].tag != sm::EventTag::SettlementVisit
        || loadedQuests[0].onAccept[9].s1 != "realm_a"
        || loadedQuests[0].onAccept[9].s2 != "realm_b"
        || loadedQuests[0].onAccept[9].ix != -10
        || loadedQuests[0].onAccept[9].iy != 15
        || loadedQuests[0].onAccept[10].tag != sm::EventTag::SpellLearned
        || loadedQuests[0].onAccept[10].s1 != "dlg_intro"
        || loadedQuests[0].onAccept[10].a != 42u
        || loadedQuests[0].onAccept[11].tag != sm::EventTag::TimeAdvance
        || loadedQuests[0].onAccept[11].fx != 12.5f
        || loadedQuests[0].onAccept[11].fy != 18.25f) {
        FAIL_BAIL("active quest details lost");
    }

    if (!write_all(truncatedPath, bytes, bytes.size() / 2u)) {
        FAIL_BAIL("could not write truncated file");
    }
    sm::GameState sentinel{};
    sentinel.mapW = 11;
    std::vector<sm::Quest> sentinelQuests;
    std::vector<sm::MacroNpcRecord> sentinelMacro;
    std::vector<std::uint16_t> sentinelTrees(3, 42u);
    sm::DepositLayer sentinelDeposits;
    sentinelQuests.push_back(make_quest());
    sentinelQuests[0].ordinal = 777u;
    if (sm::load_game(sentinel, sentinelQuests, sentinelMacro, sentinelTrees,
                      sentinelDeposits, truncatedPath)) {
        FAIL_BAIL("truncated payload accepted");
    }
    if (sentinel.mapW != 11 || sentinelQuests[0].ordinal != 777u
        || sentinelTrees.size() != 3) {
        FAIL_BAIL("failed truncated load mutated state");
    }

    std::vector<std::uint8_t> corrupt = bytes;
    if (corrupt.size() <= 20u) FAIL_BAIL("header too small");
    corrupt[corrupt.size() - 1u] ^= 0x5au;
    if (!write_all(corruptPath, corrupt, corrupt.size())) {
        FAIL_BAIL("could not write corrupt file");
    }
    sentinel.mapW = 22;
    sentinelQuests[0].ordinal = 778u;
    if (sm::load_game(sentinel, sentinelQuests, sentinelMacro, sentinelTrees,
                      sentinelDeposits, corruptPath)) {
        FAIL_BAIL("corrupt payload accepted");
    }
    if (sentinel.mapW != 22 || sentinelQuests[0].ordinal != 778u) {
        FAIL_BAIL("failed corrupt load mutated state");
    }

    std::vector<std::uint8_t> badVersion = bytes;
    if (badVersion.size() < 8u) FAIL_BAIL("header too small for version");
    badVersion[4] = 0x0f;
    badVersion[5] = 0x27;
    badVersion[6] = 0x00;
    badVersion[7] = 0x00;
    if (!write_all(badVersionPath, badVersion, badVersion.size())) {
        FAIL_BAIL("could not write bad version file");
    }
    sm::GameState badState{};
    std::vector<sm::Quest> badQuests;
    std::vector<sm::MacroNpcRecord> badMacro;
    std::vector<std::uint16_t> badTrees;
    sm::DepositLayer badDeposits;
    if (sm::load_game(badState, badQuests, badMacro, badTrees, badDeposits,
                      badVersionPath)) {
        FAIL_BAIL("bad version accepted");
    }
    const sm::SaveSummary badSummary = sm::inspect_save(badVersionPath);
    if (badSummary.status != sm::SaveInspectStatus::VersionMismatch) {
        FAIL_BAIL("bad version inspect status wrong");
    }

    // A roster row naming a kind the tables do not know must REFUSE the save,
    // whichever roster carries it — the deserter pool is one of the four the
    // shared writer serves.
    sm::GameState invalidSquadState = gs;
    invalidSquadState.deserterPool.push(sm::SoldierRecord{
        10001u, static_cast<std::uint8_t>(sm::NPCType::Count), 1});
    if (sm::save_game(invalidSquadState, quests, macroFixture, treeCounts,
                      deposits,
                      temp_save_path("timaert_invalid_squad_save.bin"))) {
        FAIL_BAIL("invalid squad kind saved");
    }

    // A macro record with a garbage NPC type must refuse to save — the same
    // fail-closed rule the soldier rows live under.
    {
        std::vector<sm::MacroNpcRecord> invalidMacro = make_macro_records();
        invalidMacro[0].kind.type = std::uint16_t(sm::NPCType::Count);
        if (sm::save_game(gs, quests, invalidMacro, treeCounts, deposits,
                          temp_save_path("timaert_invalid_macro_save.bin"))) {
            FAIL_BAIL("invalid macro npc kind saved");
        }
    }

    // (The event-log ring died in v58: session words are a fading HUD feed
    // that never touches the save; the player's past is his journal of
    // chronicle records, round-tripped above.)

    std::printf("OK save_roundtrip_test path=%s bytes=%zu map=%dx%d quest=%u\n",
                path.c_str(), bytes.size(), loaded.mapW, loaded.mapH,
                unsigned(loadedQuests[0].ordinal));

    remove_slot_files(truncatedPath);
    remove_slot_files(corruptPath);
    remove_slot_files(badVersionPath);
    remove_slot_files(path);
    CHECK(true, "every roundtrip gate above passed");
}

} // namespace

int main() {
    run_roundtrip();
    return sm::test::report("save_roundtrip_test");
}

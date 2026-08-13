#include "macro/save.h"
#include "macro/state.h"
#include "macro/macro_snapshot.h"
#include "macro/deposit_layer.h"
#include "macro/agent_memory.h"
#include "macro/currency.h"
#include "macro/npc.h"
#include "macro/entry_context.h"
#include "events/event_bus.h"
#include "events/quests/quest_engine.h"
#include "events/quests/quest_types.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <string>
#include <vector>

namespace {

int fail(const char* msg) {
    std::fprintf(stderr, "FAIL save_roundtrip_test: %s\n", msg);
    return 1;
}

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
        squad.members.push_back(sm::make_soldier(
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

bool has_string(const std::vector<std::string>& values, const char* needle) {
    for (const auto& value : values) {
        if (value == needle) return true;
    }
    return false;
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
    a.inventory.add("itm_bread", 3);
    a.roster.push_back(sm::make_soldier(std::uint8_t(sm::NPCType::Guard), 4, 900u));
    a.roster.push_back(sm::make_soldier(std::uint8_t(sm::NPCType::Peasant), 2, 901u));
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
    gs.player.inventory.add("coin_empire", 999);
    // The player's HEAD (v32): a debt fact, summed by the fact arithmetic.
    sm::remember(gs.player.memory,
                 sm::make_debt_fact(sm::kDebtToSettlement, 7, 15, 3));
    gs.player.sheet.attributes.str = 7;
    gs.player.sheet.attributes.vit = 8;
    gs.player.sheet.attributes.end = 9;
    gs.player.sheet.attributes.wil = 10;
    gs.player.sheet.attributes.intl = 11;
    gs.player.sheet.attributes.wis = 12;
    gs.player.sheet.attributes.lck = 13;
    gs.player.sheet.attributes.cha = 14;
    gs.player.sheet.attributes.spd = 15;
    gs.player.sheet.skills.bodybuilding = 1;
    gs.player.sheet.skills.meditation = 2;
    gs.player.sheet.skills.travel = 3;
    gs.player.sheet.skills.fighter = 4;
    gs.player.sheet.skills.marathon = 5;
    gs.player.sheet.skills.spellcraft = 6;
    gs.player.sheet.skills.weightlifting = 7;
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
    gs.player.inventory.add("bread", 11);
    gs.player.inventory.add("misc_gem", 3);
    gs.player.codexUnlocked.push_back("codex.alpha");
    gs.player.eventLog.push_back(
        sm::LogEntry{sm::LogType::World, "saved event", 12});
    sm::spellbook_learn(gs.player.spellBook, "spell.spark");
    sm::spellbook_set_active(gs.player.spellBook, "spell.spark");
    gs.player.spellBook.cooldowns["spell.spark"] = 2.5f;
    sm::spellbook_learn(gs.player.spellBook, "haste");
    sm::spellbook_toggle_sustained(gs.player.spellBook, "haste");
    gs.player.factionPeaceUntilDay["guild"] = 55;
    gs.player.completedQuestIds.push_back("q_done_round");
    gs.player.failedQuestIds.push_back("q_failed_round");
    // Entry-side context (kSaveVersion 15): a non-default direction + tick
    // count must survive the trip — a save made mid-march re-enters the
    // subworld on the same side.
    gs.player.entryDir = sm::pack_entry_dir(0, 1);   // walked in from the south
    gs.player.entryTicks = 7;
    add_soldiers(gs.player.army, sm::NPCType::Peasant, 4, 1000u);
    add_soldiers(gs.player.army, sm::NPCType::Woodcutter, 3, 1100u);
    add_soldiers(gs.player.army, sm::NPCType::Guard, 2, 1200u);
    gs.player.army.members.push_back(sm::SoldierRecord{
        9999u, static_cast<std::uint8_t>(sm::NPCType::Guard), -12});
    add_soldiers(gs.deserterPool, sm::NPCType::Woodcutter, 2, 1300u);

    sm::Settlement settlement{};
    settlement.id = 7;
    settlement.name = "Round City";
    settlement.x = 40;
    settlement.y = 80;
    settlement.population = 777;
    settlement.mood = sm::SettlementMood::Tense;
    settlement.inventory.add("wood", 19);
    settlement.history.days = {1, 12};
    settlement.history.population = {700, 777};
    add_soldiers(settlement.garrison, sm::NPCType::Guard, 5, 2000u);
    add_soldiers(settlement.garrison, sm::NPCType::Peasant, 1, 2100u);
    settlement.kingdomIdx = 2;
    // Honest-day readouts (v29) — every field non-default.
    settlement.starvedYesterday = 12;
    settlement.unmetYesterday = 34;
    settlement.famineActive = 1;
    settlement.popGrowthCarry = 0.375f;
    gs.settlements.push_back(settlement);

    sm::Village village{};
    village.id = 70;
    village.name = "Round Hamlet";
    village.x = 45;
    village.y = 85;
    village.population = 111;
    village.mood = sm::SettlementMood::Stable;
    village.inventory.add("food_meat", 4);
    village.history.days = {3, 10};
    village.history.population = {90, 111};
    village.nearestCityId = settlement.id;
    village.kingdomIdx = 2;
    village.starvedYesterday = 5;
    village.unmetYesterday = 7;
    village.famineActive = 1;
    village.popGrowthCarry = -0.25f;
    gs.villages.push_back(village);

    sm::Spire spire{};
    spire.id = 3;
    spire.x = 12;
    spire.y = 34;
    spire.spellId = 99;
    spire.depleted = true;
    gs.spires.push_back(spire);

    sm::Marker marker{};
    marker.id = "marker.round";
    marker.style = sm::MarkerStyle::Danger;
    marker.x = 15.5f;
    marker.y = 16.25f;
    marker.label = "Round Danger";
    gs.markers.push_back(marker);

    sm::Faction faction{};
    faction.id = "guild";
    faction.name = "Guild";
    faction.description = "Runtime faction";
    faction.color = 0x00FF00u;
    faction.relations["other"] = -5;
    gs.factions.emplace(faction.id, faction);
    // Standing is a row in the relation matrix now, not a player-side map: this
    // adds the player pair to the very row above, and must not disturb it.
    sm::add_player_reputation(gs, "guild", 42);

    gs.subState.kind = sm::GameSubStateKind::Trading;
    gs.subState.settlementId = settlement.id;
    gs.subState.eventId = "event.round";
    gs.subState.enemyId = "enemy.round";
    gs.subState.pendingEncounterIdx = 4;


    // Deposit overrides (v30 packed): a part-drained stone cell, a dry iron
    // one, and a DISCOVERED vein on a cell the derivation never named.
    gs.depositOverrides[99u] = sm::pack_deposit_override(
        sm::DepositKind::Stone, 1500);
    gs.depositOverrides[100u] = sm::pack_deposit_override(
        sm::DepositKind::Iron, 0);
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

sm::Quest make_quest(const char* id) {
    sm::Quest q{};
    q.id = id;
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
    ev.s1 = q.id;
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

} // namespace

int main() {
    if (static_cast<std::uint8_t>(sm::GameSubStateKind::Event) != 4u) {
        return fail("unexpected sub-state enum layout");
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
    sm::EventBus bus;
    sm::QuestEngine questEngine;
    std::vector<sm::Quest> quests;
    questEngine.accept(quests, make_quest("q_active"), gs.player, bus);
    if (quests.size() != 1) return fail("QuestEngine::accept did not activate quest");
    bool sawQuestStart = false;
    for (const auto& ev : bus.tick_events())
        sawQuestStart = sawQuestStart || ev.tag == sm::EventTag::QuestStart;
    if (!sawQuestStart) {
        return fail("QuestEngine::accept did not emit QuestStart");
    }

    const std::vector<sm::MacroNpcRecord> macroFixture = make_macro_records();
    if (!sm::save_game(gs, quests, macroFixture, treeCounts, path)) {
        return fail("save_game returned false");
    }

    std::vector<std::uint8_t> bytes;
    if (!read_all(path, bytes)) return fail("save file unreadable");
    if (bytes.empty()) return fail("save file is empty");

    const sm::SaveSummary summary = sm::inspect_save(path);
    if (summary.status != sm::SaveInspectStatus::Ready) return fail("inspect not ready");
    if (summary.version != sm::kSaveVersion) return fail("version mismatch");
    if (summary.worldSeed != gs.worldSeed) return fail("summary seed mismatch");
    if (summary.day != gs.worldTime.day()) return fail("summary day mismatch");
    if (summary.saveName != "roundtrip") return fail("summary save name mismatch");
    if (!valid_saved_at(summary.savedAt)) return fail("summary savedAt invalid");

    sm::GameState loaded{};
    std::vector<sm::Quest> loadedQuests;
    std::vector<sm::MacroNpcRecord> loadedMacro;
    std::vector<std::uint16_t> loadedTrees;
    if (!sm::load_game(loaded, loadedQuests, loadedMacro, loadedTrees, path)) {
        return fail("load_game failed");
    }
    if (loaded.version != sm::kSaveVersion) return fail("loaded version mismatch");
    if (loaded.saveName != "roundtrip") return fail("save name lost");
    if (loaded.savedAt != summary.savedAt) return fail("savedAt lost");
    if (loaded.worldSeed != gs.worldSeed) return fail("world seed lost");
    if (loaded.mapW != 512 || loaded.mapH != 256) return fail("map size lost");
    if (loaded.cityCountTarget != 77) return fail("city target lost");
    if (!nearf(loaded.mapParams.seaLevel, 0.55f)
        || !nearf(loaded.mapParams.heightScale, 1.25f)
        || !nearf(loaded.mapParams.moistureScale, 0.75f)) {
        return fail("map params lost");
    }
    if (loaded.worldTime.day() != 12 || loaded.worldTime.hour() != 13
        || loaded.worldTime.minute() != 14) {
        return fail("world time lost");
    }
    if (loaded.lastWorldRebakeDay != 9) {
        return fail("lastWorldRebakeDay (autosave phase) lost");
    }
    if (loaded.nextMacroSpawnOrdinal != 41) {
        return fail("nextMacroSpawnOrdinal (identity issuer) lost");
    }
    if (loaded.worldTickRt.pendingDailyTicks != 3
        || loaded.worldTickRt.nextDailyTickDay != 13
        || loaded.worldTickRt.subworldStepRemainder != 11
        || loaded.worldTickRt.jitter.state != 0xDEADBEEFu) {
        return fail("world-tick runtime (queue/remainder/jitter) lost");
    }
    if (loaded.macroAiRhythm.jitter.state != 0xFEEDF00Du
        || loaded.macroAiRhythm.sweepAccum != 21
        || loaded.macroAiRhythm.pendingSweeps != 2
        || loaded.macroAiRhythm.sweepCursor != 57) {
        return fail("macro-AI rhythm lost");
    }
    if (loaded.logicNodesRegistered.size() != 2
        || loaded.logicNodesRegistered[0] != "plot_chapter_1"
        || loaded.logicNodesActive.size() != 1
        || loaded.logicNodesActive[0] != "plot_chapter_1") {
        return fail("story progress (logic nodes) lost");
    }

    // ── The macro-ECS snapshot (v23) round-trips record-for-record ────────
    if (loadedMacro.size() != macroFixture.size()) {
        return fail("macro snapshot record count lost");
    }
    {
        const sm::MacroNpcRecord& a = loadedMacro[0];
        const sm::MacroNpcRecord& want = macroFixture[0];
        if (a.spawnId.index != want.spawnId.index) return fail("macro ordinal lost");
        if (a.pos.x != want.pos.x || a.pos.y != want.pos.y) {
            return fail("macro position lost");
        }
        if (a.kind.type != want.kind.type
            || a.kind.factionIdx != want.kind.factionIdx) {
            return fail("macro kind/faction lost");
        }
        if (a.health.hp != want.health.hp || a.health.maxHp != want.health.maxHp) {
            return fail("macro wounds lost");
        }
        if (a.level.value != want.level.value) return fail("macro level lost");
        if (a.runtime.sp != -25) return fail("macro SP debt lost");
        if (a.runtime.xp != 555) return fail("macro leader xp lost");
        if (a.runtime.targetX != want.runtime.targetX
            || a.runtime.state != want.runtime.state
            || a.runtime.entryDir != want.runtime.entryDir
            || a.runtime.tickAccum != want.runtime.tickAccum) {
            return fail("macro runtime state lost");
        }
        if (a.traits.count != 2 || a.traits.traits[1] != 4) {
            return fail("macro traits lost");
        }
        if (a.character.visualSeed != 0xABCD1234u
            || a.character.nameIdx != 7) {
            return fail("macro character identity lost");
        }
        if (a.hasOrders != 1 || a.orders.waypointCount != 2
            || a.orders.currentWaypoint != 1
            || a.orders.waypoints[3] != 8) {
            return fail("macro squad orders lost");
        }
        if (a.dead != 0) return fail("living macro NPC loaded dead");
        if (a.inventory.stacks.size() != 1
            || a.inventory.stacks[0].id != "itm_bread"
            || a.inventory.stacks[0].count != 3) {
            return fail("macro inventory lost");
        }
        if (a.roster.size() != 2
            || a.roster[0].kind != std::uint8_t(sm::NPCType::Guard)
            || a.roster[0].level != 4
            || a.roster[1].entityId != 901u) {
            return fail("macro roster lost");
        }
    }
    if (loadedMacro[1].dead != 1 || loadedMacro[1].health.hp != 0.0f) {
        return fail("the killed lord did not stay dead across the save");
    }
    if (loaded.worldTime.tick != gs.worldTime.tick) {
        return fail("world time not exact to the tick");
    }

    const sm::PlayerState& p = loaded.player;
    if (p.name != "Tester") return fail("player name lost");
    if (p.ageDays != 1234 || !nearf(p.x, 41.5f) || !nearf(p.y, 82.25f)) {
        return fail("player position or age lost");
    }
    if (sm::wallet_value(p.inventory) != 999) return fail("player coin lost");
    {
        const sm::MemoryEntry* debt = sm::recall(
            p.memory, sm::AgentMemoryKind::Debt, 7, sm::kDebtToSettlement);
        if (!debt || sm::memory_amount(*debt) != 15) {
            return fail("the player's debt fact lost");
        }
    }
    if (p.sheet.attributes.str != 7 || p.sheet.attributes.intl != 11 || p.sheet.attributes.spd != 15) {
        return fail("player attributes lost");
    }
    if (p.sheet.skills.bodybuilding != 1 || p.sheet.skills.spellcraft != 6
        || p.sheet.skills.weightlifting != 7) {
        return fail("player skills lost");
    }
    if (p.sheet.levelData.level != 6 || p.sheet.levelData.exp != 321
        || p.sheet.levelData.expToNext != 6543
        || p.sheet.levelData.attributePoints != 4
        || p.sheet.levelData.skillPoints != 5
        || p.sheet.levelData.perkPoints != 6) {
        return fail("player level data lost");
    }
    if (p.combatStats.currentHp != 33 || p.combatStats.maxMp != 222
        || !nearf(p.combatStats.spRegen, 3.75f)) {
        return fail("player combat stats lost");
    }
    if (!sm::has_perk(p.sheet.perks, sm::PerkID::Natural)
        || !sm::has_perk(p.sheet.perks, sm::PerkID::Educated)) {
        return fail("player perks lost");
    }
    if (p.inventory.count("misc_gem") != 3 || p.inventory.count("bread") != 11) {
        return fail("inventory lost");
    }
    if (sm::player_reputation(&loaded, "guild") != 42) {
        return fail("player standing lost (his row in the faction matrix)");
    }
    if (sm::faction_relation(&loaded, "guild", sm::kPlayerFactionId) != 42) {
        return fail("player standing is not symmetric after a save round-trip");
    }
    if (p.entryDir != sm::pack_entry_dir(0, 1) || p.entryTicks != 7) {
        return fail("entry-side context lost");
    }
    if (sm::count_soldiers_of_kind(
            p.army, static_cast<std::uint8_t>(sm::NPCType::Peasant)) != 4
        || sm::count_soldiers_of_kind(
            p.army, static_cast<std::uint8_t>(sm::NPCType::Guard)) != 3) {
        return fail("player army lost");
    }
    bool foundNormalizedSoldier = false;
    for (const auto& soldier : p.army.members) {
        if (soldier.entityId == 9999u) {
            if (soldier.level != 1) return fail("soldier level not normalized on save");
            foundNormalizedSoldier = true;
        }
    }
    if (!foundNormalizedSoldier) return fail("normalized soldier lost");
    if (!has_string(p.codexUnlocked, "codex.alpha")
        || p.eventLog.empty() || p.eventLog[0].message != "saved event") {
        return fail("codex or event log lost");
    }
    if (!sm::spellbook_has_learned(p.spellBook, "spell.spark")
        || !sm::spellbook_has_learned(p.spellBook, "haste")) {
        return fail("spell learned state lost");
    }
    if (p.spellBook.activeSpellId != "spell.spark") {
        return fail("active spell lost");
    }
    const auto cdIt = p.spellBook.cooldowns.find("spell.spark");
    if (cdIt == p.spellBook.cooldowns.end() || !nearf(cdIt->second, 2.5f)) {
        return fail("spell cooldown lost");
    }
    if (!sm::spellbook_has_sustained(p.spellBook, "haste")) {
        return fail("sustained spell state lost");
    }
    const auto peaceIt = p.factionPeaceUntilDay.find("guild");
    if (peaceIt == p.factionPeaceUntilDay.end() || peaceIt->second != 55
        || p.completedQuestIds.empty() || p.completedQuestIds[0] != "q_done_round") {
        return fail("quest completion or peace state lost");
    }
    if (p.failedQuestIds.empty() || p.failedQuestIds[0] != "q_failed_round") {
        return fail("failed quest ledger lost");
    }
    if (loaded.settlements.empty() || loaded.settlements[0].population != 777) {
        return fail("settlement lost");
    }
    const sm::Settlement& city = loaded.settlements[0];
    if (city.name != "Round City" || city.mood != sm::SettlementMood::Tense
        || city.inventory.count("wood") != 19
        || city.history.days.size() != 2 || city.history.population[1] != 777
        || sm::count_soldiers_of_kind(
            city.garrison, static_cast<std::uint8_t>(sm::NPCType::Peasant)) != 1) {
        return fail("settlement details lost");
    }
    if (city.starvedYesterday != 12 || city.unmetYesterday != 34
        || city.famineActive != 1 || !nearf(city.popGrowthCarry, 0.375f)) {
        return fail("settlement honest-day readouts (v29) lost");
    }
    if (loaded.villages.empty()
        || loaded.villages[0].starvedYesterday != 5
        || loaded.villages[0].unmetYesterday != 7
        || loaded.villages[0].famineActive != 1
        || !nearf(loaded.villages[0].popGrowthCarry, -0.25f)) {
        return fail("village honest-day readouts (v29) lost");
    }
    if (loaded.spires.empty() || !loaded.spires[0].depleted
        || loaded.spires[0].spellId != 99) {
        return fail("spire lost");
    }
    if (loaded.markers.empty() || loaded.markers[0].id != "marker.round"
        || loaded.markers[0].style != sm::MarkerStyle::Danger) {
        return fail("marker lost");
    }
    const auto factionIt = loaded.factions.find("guild");
    if (factionIt == loaded.factions.end()) return fail("faction lost");
    const auto relationIt = factionIt->second.relations.find("other");
    if (relationIt == factionIt->second.relations.end() || relationIt->second != -5) {
        return fail("faction relation lost");
    }
    if (loaded.subState.kind != sm::GameSubStateKind::Trading
        || loaded.subState.settlementId != 7
        || loaded.subState.pendingEncounterIdx != 4) {
        return fail("sub-state lost");
    }
    if (sm::count_soldiers_of_kind(
            loaded.deserterPool, static_cast<std::uint8_t>(sm::NPCType::Woodcutter)) != 2) {
        return fail("deserter pool lost");
    }
    if (loaded.depositOverrides.size() != 2
        || sm::override_kind(loaded.depositOverrides.at(99u))
               != sm::DepositKind::Stone
        || sm::override_remaining(loaded.depositOverrides.at(99u)) != 1500
        || sm::override_kind(loaded.depositOverrides.at(100u))
               != sm::DepositKind::Iron
        || sm::override_remaining(loaded.depositOverrides.at(100u)) != 0) {
        return fail("deposit overrides (kind + remaining) lost");
    }
    if (loadedTrees != treeCounts) return fail("tree grid lost");
    if (loadedTrees.at(7) != 0u || loadedTrees.at(17) != 12000u) {
        return fail("felled/thickened tree cells did not round-trip");
    }
    if (loaded.resourceScars[std::size_t(sm::ResourceFieldId::Fauna)].size() != 2
        || loaded.resourceScars[std::size_t(sm::ResourceFieldId::Fauna)].at(42u * 1024u + 17u) != 3u
        || loaded.resourceScars[std::size_t(sm::ResourceFieldId::Fauna)].at(11u) != 0u) {
        return fail("fauna overrides lost");
    }
    if (loaded.resourceScars[std::size_t(sm::ResourceFieldId::Wheat)].size() != 1
        || loaded.resourceScars[std::size_t(sm::ResourceFieldId::Wheat)].at(23u * 1024u + 5u) != 12u) {
        return fail("crop harvest scars lost");
    }
    if (loadedQuests.size() != 1 || loadedQuests[0].id != "q_active") {
        return fail("active quest lost");
    }
    if (!loadedQuests[0].objectives.empty()) {
        const sm::Objective& o = loadedQuests[0].objectives[0];
        if (o.cellX != 5 || o.cellY != 6 || o.subX != 512 || o.subY != 640
            || o.itemId != "itm_bread" || o.quantity != 4
            || o.targetSettlementId != 9 || o.npcType != 11
            || o.count != 3 || o.killed != 1 || o.zoneRadius != 2.5f
            || o.action != "chop") {
            return fail("objective fields lost");
        }
    }
    if (loadedQuests[0].objectives.empty()
        || loadedQuests[0].objectives[0].hoursWaited != 1
        || loadedQuests[0].rewards.empty()
        || loadedQuests[0].rewards[0].amount != 170
        || loadedQuests[0].onAccept.size() != 12
        || loadedQuests[0].onAccept[0].s1 != "q_active"
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
        return fail("active quest details lost");
    }

    if (!write_all(truncatedPath, bytes, bytes.size() / 2u)) {
        return fail("could not write truncated file");
    }
    sm::GameState sentinel{};
    sentinel.mapW = 11;
    std::vector<sm::Quest> sentinelQuests;
    std::vector<sm::MacroNpcRecord> sentinelMacro;
    std::vector<std::uint16_t> sentinelTrees(3, 42u);
    sentinelQuests.push_back(make_quest("sentinel"));
    if (sm::load_game(sentinel, sentinelQuests, sentinelMacro, sentinelTrees,
                      truncatedPath)) {
        return fail("truncated payload accepted");
    }
    if (sentinel.mapW != 11 || sentinelQuests[0].id != "sentinel"
        || sentinelTrees.size() != 3) {
        return fail("failed truncated load mutated state");
    }

    std::vector<std::uint8_t> corrupt = bytes;
    if (corrupt.size() <= 20u) return fail("header too small");
    corrupt[corrupt.size() - 1u] ^= 0x5au;
    if (!write_all(corruptPath, corrupt, corrupt.size())) {
        return fail("could not write corrupt file");
    }
    sentinel.mapW = 22;
    sentinelQuests[0].id = "sentinel_corrupt";
    if (sm::load_game(sentinel, sentinelQuests, sentinelMacro, sentinelTrees,
                      corruptPath)) {
        return fail("corrupt payload accepted");
    }
    if (sentinel.mapW != 22 || sentinelQuests[0].id != "sentinel_corrupt") {
        return fail("failed corrupt load mutated state");
    }

    std::vector<std::uint8_t> badVersion = bytes;
    if (badVersion.size() < 8u) return fail("header too small for version");
    badVersion[4] = 0x0f;
    badVersion[5] = 0x27;
    badVersion[6] = 0x00;
    badVersion[7] = 0x00;
    if (!write_all(badVersionPath, badVersion, badVersion.size())) {
        return fail("could not write bad version file");
    }
    sm::GameState badState{};
    std::vector<sm::Quest> badQuests;
    std::vector<sm::MacroNpcRecord> badMacro;
    std::vector<std::uint16_t> badTrees;
    if (sm::load_game(badState, badQuests, badMacro, badTrees,
                      badVersionPath)) {
        return fail("bad version accepted");
    }
    const sm::SaveSummary badSummary = sm::inspect_save(badVersionPath);
    if (badSummary.status != sm::SaveInspectStatus::VersionMismatch) {
        return fail("bad version inspect status wrong");
    }

    sm::GameState invalidSquadState = gs;
    invalidSquadState.player.army.members.push_back(sm::SoldierRecord{
        10001u, static_cast<std::uint8_t>(sm::NPCType::Count), 1});
    if (sm::save_game(invalidSquadState, quests, macroFixture, treeCounts,
                      temp_save_path("timaert_invalid_squad_save.bin"))) {
        return fail("invalid squad kind saved");
    }

    // A macro record with a garbage NPC type must refuse to save — the same
    // fail-closed rule the soldier rows live under.
    {
        std::vector<sm::MacroNpcRecord> invalidMacro = make_macro_records();
        invalidMacro[0].kind.type = std::uint16_t(sm::NPCType::Count);
        if (sm::save_game(gs, quests, invalidMacro, treeCounts,
                          temp_save_path("timaert_invalid_macro_save.bin"))) {
            return fail("invalid macro npc kind saved");
        }
    }

    // ── Event-log ring (state.h push_event_log). The door drops the OLDEST
    // entry past the cap so a ten-year campaign's log stays saveable; the
    // save-side guard stays armed against anything that bypasses the door.
    sm::GameState ringState = gs;
    ringState.player.eventLog.clear();
    for (std::size_t i = 0; i < sm::kMaxEventLogEntries + 100u; ++i) {
        sm::push_event_log(ringState.player,
                           {sm::LogType::World,
                            "entry " + std::to_string(i), int(i)});
    }
    if (ringState.player.eventLog.size() != sm::kMaxEventLogEntries) {
        return fail("event-log ring did not cap at kMaxEventLogEntries");
    }
    if (ringState.player.eventLog.front().message != "entry 100") {
        return fail("event-log ring did not drop the OLDEST entries");
    }
    if (ringState.player.eventLog.back().message
        != "entry " + std::to_string(sm::kMaxEventLogEntries + 99u)) {
        return fail("event-log ring lost the newest entry");
    }
    const std::string ringPath = temp_save_path("timaert_ring_log_save.bin");
    remove_slot_files(ringPath);
    if (!sm::save_game(ringState, quests, macroFixture, treeCounts,
                       ringPath)) {
        return fail("a ring-capped (full) event log must still save");
    }
    sm::GameState ringLoaded{};
    std::vector<sm::Quest> ringQuests;
    std::vector<sm::MacroNpcRecord> ringMacro;
    std::vector<std::uint16_t> ringTrees;
    if (!sm::load_game(ringLoaded, ringQuests, ringMacro, ringTrees,
                       ringPath)) {
        return fail("full-log save did not load back");
    }
    if (ringLoaded.player.eventLog.size() != sm::kMaxEventLogEntries
        || ringLoaded.player.eventLog.front().message != "entry 100"
        || ringLoaded.player.eventLog.back().message
           != "entry " + std::to_string(sm::kMaxEventLogEntries + 99u)) {
        return fail("full event log did not round-trip entry-for-entry");
    }
    // NEGATIVE CONTROL: bypass the door and overflow — the writer must refuse,
    // proving the cap that used to silently kill saves is still enforced.
    ringState.player.eventLog.push_back({sm::LogType::World, "overflow", 0});
    if (sm::save_game(ringState, quests, macroFixture, treeCounts,
                      ringPath)) {
        return fail("an over-cap event log saved — write guard disarmed");
    }
    remove_slot_files(ringPath);

    std::printf("OK save_roundtrip_test path=%s bytes=%zu map=%dx%d quest=%s\n",
                path.c_str(), bytes.size(), loaded.mapW, loaded.mapH,
                loadedQuests[0].id.c_str());

    remove_slot_files(truncatedPath);
    remove_slot_files(corruptPath);
    remove_slot_files(badVersionPath);
    remove_slot_files(path);
    return 0;
}

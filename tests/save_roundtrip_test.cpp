#include "macro/save.h"
#include "macro/state.h"
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

bool has_resource(const std::vector<sm::ResourceId>& values, sm::ResourceId id) {
    for (sm::ResourceId value : values) {
        if (value == id) return true;
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
    gs.worldTime = sm::WorldTime{12, 13, 14};

    gs.player.name = "Tester";
    gs.player.ageDays = 1234;
    gs.player.x = 41.5f;
    gs.player.y = 82.25f;
    gs.player.gold = 999;
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
    gs.player.sheet.skills.endurance = 5;
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
    gs.player.inventory.add("food_bread", 11);
    gs.player.inventory.add("misc_gem", 3);
    gs.player.reputation["guild"] = 42;
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
    settlement.inventory.add("mat_wood", 19);
    settlement.history.days = {1, 12};
    settlement.history.population = {700, 777};
    add_soldiers(settlement.garrison, sm::NPCType::Guard, 5, 2000u);
    add_soldiers(settlement.garrison, sm::NPCType::Peasant, 1, 2100u);
    settlement.eco.wealth = 12.5f;
    settlement.eco.happiness = 0.6f;
    settlement.eco.resources[static_cast<std::size_t>(sm::ResourceId::Wood)] = 8.0f;
    settlement.eco.goods[1] = 9.0f;
    settlement.eco.resourcePrices[static_cast<std::size_t>(sm::ResourceId::Wood)] = 1.75f;
    settlement.eco.goodPrices[2] = 4.25f;
    settlement.eco.localResources = {sm::ResourceId::Wood, sm::ResourceId::Iron};
    settlement.kingdomIdx = 2;
    settlement.economy = "trade";
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
    village.eco.resources[static_cast<std::size_t>(sm::ResourceId::Grain)] = 5.5f;
    village.eco.goods[0] = 1.25f;
    village.eco.resourcePrices[static_cast<std::size_t>(sm::ResourceId::Grain)] = 1.1f;
    village.eco.goodPrices[0] = 2.2f;
    village.eco.wealth = 3.5f;
    village.eco.happiness = 0.7f;
    village.nearestCityId = settlement.id;
    village.lastTradeDay = 10;
    village.kingdomIdx = 2;
    village.eco.localResources = {sm::ResourceId::Grain, sm::ResourceId::Clay};
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

    gs.subState.kind = sm::GameSubStateKind::Trading;
    gs.subState.settlementId = settlement.id;
    gs.subState.eventId = "event.round";
    gs.subState.enemyId = "enemy.round";
    gs.subState.pendingEncounterIdx = 4;

    sm::TradeRoute route{};
    route.originId = settlement.id;
    route.destId = village.id;
    route.arrivalDay = 45;
    route.valid = true;
    route.cargo.push_back(
        sm::CargoEntry{static_cast<std::uint8_t>(sm::ResourceId::Wood),
                       true, 6, 3.5f});
    route.cargo.push_back(sm::CargoEntry{2u, false, 4, 8.5f});
    gs.activeTradeRoutes.push_back(route);
    gs.cityLastTradeDay[settlement.id] = 12;

    // v13: sparse tree-count overrides (felled cells).
    gs.treeOverrides[42u * 1024u + 17u] = 12000u;
    gs.treeOverrides[7u] = 0u;

    return gs;
}

sm::Quest make_quest(const char* id) {
    sm::Quest q{};
    q.id = id;
    q.title = "Active Quest";
    q.description = "Roundtrip active quest";
    q.category = sm::QuestCategory::Procedural;
    q.giverSettlementId = 7;

    sm::Objective objective{};
    objective.kind = sm::ObjectiveKind::WaitAt;
    objective.ix = 40;
    objective.iy = 80;
    objective.radius = 3.0f;
    objective.hoursRequired = 3;
    objective.hoursWaited = 1;
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
    sm::GameEvent hp{sm::EventTag::NpcHpChange};
    hp.a = 42;
    hp.ix = -5;
    q.onAccept.push_back(hp);
    sm::GameEvent mood{sm::EventTag::SettlementMoodChange};
    mood.s1 = "Unrest";
    mood.s2 = "Prosperous";
    mood.a = 7;
    mood.ix = 7;
    q.onAccept.push_back(mood);
    sm::GameEvent stat{sm::EventTag::PlayerStatChange};
    stat.s1 = "hp";
    stat.ix = 10;
    stat.iy = 12;
    q.onAccept.push_back(stat);
    sm::GameEvent battleEnd{sm::EventTag::BattleEnd};
    battleEnd.s1 = "Bandit";
    battleEnd.ix = 1;
    battleEnd.a = 23;
    q.onAccept.push_back(battleEnd);
    sm::GameEvent surge{sm::EventTag::MagicSurge};
    surge.ix = 4;
    surge.iy = 5;
    surge.fx = 0.75f;
    q.onAccept.push_back(surge);
    sm::GameEvent faction{sm::EventTag::FactionRelationChange};
    faction.s1 = "realm_a";
    faction.s2 = "realm_b";
    faction.ix = -10;
    faction.iy = 15;
    q.onAccept.push_back(faction);
    sm::GameEvent dialogStart{sm::EventTag::DialogStart};
    dialogStart.s1 = "dlg_intro";
    dialogStart.a = 42;
    q.onAccept.push_back(dialogStart);
    sm::GameEvent camera{sm::EventTag::CameraMove};
    camera.fx = 12.5f;
    camera.fy = 18.25f;
    q.onAccept.push_back(camera);
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
    sm::EventBus bus;
    sm::QuestEngine questEngine;
    std::vector<sm::Quest> quests;
    questEngine.accept(quests, make_quest("q_active"), gs.player, bus);
    if (quests.size() != 1) return fail("QuestEngine::accept did not activate quest");
    if (!bus.has_tag(sm::EventTag::QuestStart)
        || !bus.has_tag(sm::EventTag::QuestAccepted)) {
        return fail("QuestEngine::accept did not emit QuestStart alias");
    }

    if (!sm::save_game(gs, quests, path)) return fail("save_game returned false");

    std::vector<std::uint8_t> bytes;
    if (!read_all(path, bytes)) return fail("save file unreadable");
    if (bytes.empty()) return fail("save file is empty");

    const sm::SaveSummary summary = sm::inspect_save(path);
    if (summary.status != sm::SaveInspectStatus::Ready) return fail("inspect not ready");
    if (summary.version != sm::kSaveVersion) return fail("version mismatch");
    if (summary.worldSeed != gs.worldSeed) return fail("summary seed mismatch");
    if (summary.day != gs.worldTime.day) return fail("summary day mismatch");
    if (summary.saveName != "roundtrip") return fail("summary save name mismatch");
    if (!valid_saved_at(summary.savedAt)) return fail("summary savedAt invalid");

    sm::GameState loaded{};
    std::vector<sm::Quest> loadedQuests;
    if (!sm::load_game(loaded, loadedQuests, path)) return fail("load_game failed");
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
    if (loaded.worldTime.day != 12 || loaded.worldTime.hour != 13
        || loaded.worldTime.minute != 14) {
        return fail("world time lost");
    }

    const sm::PlayerState& p = loaded.player;
    if (p.name != "Tester") return fail("player name lost");
    if (p.ageDays != 1234 || !nearf(p.x, 41.5f) || !nearf(p.y, 82.25f)) {
        return fail("player position or age lost");
    }
    if (p.gold != 999) return fail("player gold lost");
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
    if (p.inventory.count("misc_gem") != 3 || p.inventory.count("food_bread") != 11) {
        return fail("inventory lost");
    }
    const auto repIt = p.reputation.find("guild");
    if (repIt == p.reputation.end() || repIt->second != 42) return fail("reputation lost");
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
        || city.inventory.count("mat_wood") != 19
        || city.history.days.size() != 2 || city.history.population[1] != 777
        || sm::count_soldiers_of_kind(
            city.garrison, static_cast<std::uint8_t>(sm::NPCType::Peasant)) != 1) {
        return fail("settlement details lost");
    }
    if (!nearf(city.eco.resources[static_cast<std::size_t>(sm::ResourceId::Wood)], 8.0f)
        || !nearf(city.eco.goods[1], 9.0f)
        || !has_resource(city.eco.localResources, sm::ResourceId::Iron)
        || city.kingdomIdx != 2 || city.economy != "trade") {
        return fail("settlement economy lost");
    }
    if (loaded.villages.empty() || loaded.villages[0].lastTradeDay != 10) {
        return fail("village lost");
    }
    const sm::Village& loadedVillage = loaded.villages[0];
    if (loadedVillage.inventory.count("food_meat") != 4
        || loadedVillage.history.days.size() != 2 || loadedVillage.history.days[1] != 10
        || !nearf(loadedVillage.eco.wealth, 3.5f)
        || !has_resource(loadedVillage.eco.localResources, sm::ResourceId::Clay)) {
        return fail("village details lost");
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
    if (loaded.activeTradeRoutes.empty()
        || loaded.activeTradeRoutes[0].arrivalDay != 45
        || loaded.activeTradeRoutes[0].cargo.size() != 2
        || !loaded.activeTradeRoutes[0].cargo[0].kindIsResource
        || loaded.activeTradeRoutes[0].cargo[1].kindIsResource) {
        return fail("trade route lost");
    }
    if (loaded.cityLastTradeDay[7] != 12) return fail("city trade day lost");
    if (loaded.treeOverrides.size() != 2
        || loaded.treeOverrides.at(42u * 1024u + 17u) != 12000u
        || loaded.treeOverrides.at(7u) != 0u) {
        return fail("tree overrides lost");
    }
    if (loadedQuests.size() != 1 || loadedQuests[0].id != "q_active") {
        return fail("active quest lost");
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
        || loadedQuests[0].onAccept[4].tag != sm::EventTag::NpcHpChange
        || loadedQuests[0].onAccept[4].a != 42u
        || loadedQuests[0].onAccept[4].ix != -5
        || loadedQuests[0].onAccept[5].tag != sm::EventTag::SettlementMoodChange
        || loadedQuests[0].onAccept[5].s1 != "Unrest"
        || loadedQuests[0].onAccept[5].s2 != "Prosperous"
        || loadedQuests[0].onAccept[6].tag != sm::EventTag::PlayerStatChange
        || loadedQuests[0].onAccept[6].s1 != "hp"
        || loadedQuests[0].onAccept[6].ix != 10
        || loadedQuests[0].onAccept[6].iy != 12
        || loadedQuests[0].onAccept[7].tag != sm::EventTag::BattleEnd
        || loadedQuests[0].onAccept[7].s1 != "Bandit"
        || loadedQuests[0].onAccept[7].ix != 1
        || loadedQuests[0].onAccept[7].a != 23u
        || loadedQuests[0].onAccept[8].tag != sm::EventTag::MagicSurge
        || loadedQuests[0].onAccept[8].ix != 4
        || loadedQuests[0].onAccept[8].iy != 5
        || loadedQuests[0].onAccept[8].fx != 0.75f
        || loadedQuests[0].onAccept[9].tag != sm::EventTag::FactionRelationChange
        || loadedQuests[0].onAccept[9].s1 != "realm_a"
        || loadedQuests[0].onAccept[9].s2 != "realm_b"
        || loadedQuests[0].onAccept[9].ix != -10
        || loadedQuests[0].onAccept[9].iy != 15
        || loadedQuests[0].onAccept[10].tag != sm::EventTag::DialogStart
        || loadedQuests[0].onAccept[10].s1 != "dlg_intro"
        || loadedQuests[0].onAccept[10].a != 42u
        || loadedQuests[0].onAccept[11].tag != sm::EventTag::CameraMove
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
    sentinelQuests.push_back(make_quest("sentinel"));
    if (sm::load_game(sentinel, sentinelQuests, truncatedPath)) {
        return fail("truncated payload accepted");
    }
    if (sentinel.mapW != 11 || sentinelQuests[0].id != "sentinel") {
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
    if (sm::load_game(sentinel, sentinelQuests, corruptPath)) {
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
    if (sm::load_game(badState, badQuests, badVersionPath)) {
        return fail("bad version accepted");
    }
    const sm::SaveSummary badSummary = sm::inspect_save(badVersionPath);
    if (badSummary.status != sm::SaveInspectStatus::VersionMismatch) {
        return fail("bad version inspect status wrong");
    }

    sm::GameState invalidSquadState = gs;
    invalidSquadState.player.army.members.push_back(sm::SoldierRecord{
        10001u, static_cast<std::uint8_t>(sm::NPCType::Count), 1});
    if (sm::save_game(invalidSquadState, quests,
                      temp_save_path("timaert_invalid_squad_save.bin"))) {
        return fail("invalid squad kind saved");
    }

    std::printf("OK save_roundtrip_test path=%s bytes=%zu map=%dx%d quest=%s\n",
                path.c_str(), bytes.size(), loaded.mapW, loaded.mapH,
                loadedQuests[0].id.c_str());

    remove_slot_files(truncatedPath);
    remove_slot_files(corruptPath);
    remove_slot_files(badVersionPath);
    remove_slot_files(path);
    return 0;
}

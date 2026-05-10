#include "macro/save.h"
#include "macro/state.h"
#include "events/event_bus.h"
#include "events/quests/quest_engine.h"
#include "events/quests/quest_types.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
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

bool write_all(const std::string& path, const std::vector<std::uint8_t>& bytes,
               std::size_t limit) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    const std::size_t n = limit < bytes.size() ? limit : bytes.size();
    const std::size_t wrote = n ? std::fwrite(bytes.data(), 1, n, f) : 0;
    const int closeRc = std::fclose(f);
    return wrote == n && closeRc == 0;
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
    gs.player.inventory.add("food_bread", 11);
    gs.player.inventory.add("misc_gem", 3);
    gs.player.reputation["guild"] = 42;
    gs.player.codexUnlocked.push_back("codex.alpha");
    gs.player.eventLog.push_back(
        sm::LogEntry{sm::LogType::World, "saved event", 12});
    gs.player.spellBookSpellIds.push_back("spell.spark");
    gs.player.factionPeaceUntilDay["guild"] = 55;
    gs.player.completedQuestIds.push_back(123);
    gs.player.army.set(sm::UnitType::Swordsman, 4);
    gs.deserterPool.set(sm::UnitType::Archer, 2);

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
    settlement.garrison.set(sm::UnitType::Spearman, 5);
    settlement.eco.wealth = 12.5f;
    settlement.eco.happiness = 0.6f;
    settlement.eco.resources[static_cast<std::size_t>(sm::ResourceId::Wood)] = 8.0f;
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
    village.nearestCityId = settlement.id;
    village.lastTradeDay = 10;
    village.kingdomIdx = 2;
    village.eco.localResources.push_back(sm::ResourceId::Grain);
    gs.villages.push_back(village);

    sm::Spire spire{};
    spire.id = 3;
    spire.x = 12;
    spire.y = 34;
    spire.spellId = 99;
    spire.depleted = true;
    gs.spires.push_back(spire);

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
    gs.activeTradeRoutes.push_back(route);
    gs.cityLastTradeDay[settlement.id] = 12;

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

    sm::GameEvent ev{sm::EventTag::QuestAccepted};
    ev.s1 = q.id;
    q.onAccept.push_back(ev);
    q.expireDay = 99;
    q.difficulty = 2;
    return q;
}

} // namespace

int main() {
    const std::string path = temp_save_path("timaert_save_roundtrip_v4.bin");
    const std::string truncatedPath = temp_save_path("timaert_save_roundtrip_v4_truncated.bin");
    const std::string corruptPath = temp_save_path("timaert_save_roundtrip_v4_corrupt.bin");
    const std::string badVersionPath = temp_save_path("timaert_save_roundtrip_v4_bad_version.bin");

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
    if (!bus.has_tag(sm::EventTag::QuestAccepted)) {
        return fail("QuestEngine::accept did not emit QuestAccepted");
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

    sm::GameState loaded{};
    std::vector<sm::Quest> loadedQuests;
    if (!sm::load_game(loaded, loadedQuests, path)) return fail("load_game failed");
    if (loaded.mapW != 512 || loaded.mapH != 256) return fail("map size lost");
    if (loaded.cityCountTarget != 77) return fail("city target lost");
    if (loaded.mapParams.seaLevel != 0.55f) return fail("map params lost");
    if (loaded.player.name != "Tester") return fail("player name lost");
    if (loaded.player.gold != 999) return fail("player gold lost");
    if (loaded.player.inventory.count("misc_gem") != 3) return fail("inventory lost");
    if (loaded.settlements.empty() || loaded.settlements[0].population != 777) {
        return fail("settlement lost");
    }
    if (loaded.villages.empty() || loaded.villages[0].lastTradeDay != 10) {
        return fail("village lost");
    }
    if (loaded.factions.find("guild") == loaded.factions.end()) return fail("faction lost");
    if (loaded.activeTradeRoutes.empty()
        || loaded.activeTradeRoutes[0].arrivalDay != 45) {
        return fail("trade route lost");
    }
    if (loaded.cityLastTradeDay[7] != 12) return fail("city trade day lost");
    if (loadedQuests.size() != 1 || loadedQuests[0].id != "q_active") {
        return fail("active quest lost");
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

    std::printf("OK save_roundtrip_test path=%s bytes=%zu map=%dx%d quest=%s\n",
                path.c_str(), bytes.size(), loaded.mapW, loaded.mapH,
                loadedQuests[0].id.c_str());

    remove_slot_files(truncatedPath);
    remove_slot_files(corruptPath);
    remove_slot_files(badVersionPath);
    remove_slot_files(path);
    return 0;
}

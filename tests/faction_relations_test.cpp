// Locks the faction REGISTRY contract (macro/faction.h) — the single source of
// truth for every faction — and the relation matrix create_factions() samples
// from it.
//
// sub/engine.cpp decides all NPC-vs-NPC combat from this matrix:
//     faction_relation(gs, a, b) = gs.factions[a].relations[b]   (symmetric)
// hostile when < kHostileThreshold (-50). And ecs::NPCKind.factionIdx is an
// index into kFactionDefs for humanoids and monsters alike, so the registry's
// integrity IS the combat system's integrity.
//
// History this test guards against (all shipped at some point):
//   • "magika" was emitted by a spawn vocabulary but never registered — every
//     relation involving wandering mages silently read neutral;
//   • the registry was split (universal list + kingdom list), so the two could
//     drift; kingdoms are now ordinary registry rows and politik must reference
//     an existing row;
//   • five parallel id/index vocabularies with colliding indices (a bandit NPC
//     and a demon creature shared index 3) — one index space now;
//   • relations decided by an if-chain over id strings, then by a sampled
//     temperament-band matrix — BOTH cut 2026-09-21: политики нет до
//     играбельного ядра, матрица рождается нейтральной, и ЭТОТ тест теперь
//     свидетель именно нейтральности (свидетель вырезанного закона обязан
//     сторожить новый, иначе он остаток).

#include "check.h"
#include "macro/state.h"
#include "macro/faction.h"   // registry + kHostileThreshold — THE hostility line lives with the relations
#include "macro/politik.h"

#include <cstdio>
#include <cstring>
#include <string>

namespace {

int fail(const char* msg) {
    // Testing law #1: the verdict lives in the ONE check.h counter — the
    // returned int is vestigial and IGNORED; main ends with report().
    sm::test::check(false, msg, "tests/faction_relations_test.cpp", 0);
    return 1;
}

// The lookup under test, asked the way the game asks it: by SLOT over the flat
// matrix (macro/relations.h). An id the world never placed answers neutral,
// because its slot does not exist — the same fail-closed answer the map form
// gave for a missing key.
int relation(const sm::GameState& gs, const char* a, const char* b) {
    return sm::relation_of(gs.relations, sm::faction_slot(gs.relations, a),
                           sm::faction_slot(gs.relations, b));
}

bool hostile(const sm::GameState& gs, const char* a, const char* b) {
    return relation(gs, a, b) < sm::kHostileThreshold;
}

} // namespace

int main() {
    using namespace sm;

    // ── Registry integrity (seed-independent) ─────────────────────────────
    // Unique non-empty ids; every kingdom the politik layer grows references an
    // existing registry row (the old split-registry drift is impossible).
    for (int i = 0; i < kFactionCount; ++i) {
        if (!kFactionDefs[i].id || kFactionDefs[i].id[0] == '\0') {
            return fail("registry row with empty id");
        }
        for (int j = i + 1; j < kFactionCount; ++j) {
            if (std::strcmp(kFactionDefs[i].id, kFactionDefs[j].id) == 0) {
                return fail("duplicate faction id in the registry");
            }
        }
        if (faction_index(kFactionDefs[i].id) != i) {
            return fail("faction_index does not invert the registry");
        }
        if (std::strcmp(faction_id_for_index(std::uint16_t(i)),
                        kFactionDefs[i].id) != 0) {
            return fail("faction_id_for_index does not match the registry");
        }
    }
    for (const auto& kd : realm_seed_defs()) {
        if (faction_index(kd.factionId) < 0) {
            return fail("realm faction id has no registry row — identity would vanish");
        }
    }
    // Sentinels degrade safely, never alias a real faction.
    if (faction_index(nullptr) >= 0 || faction_index("") >= 0
        || faction_id_for_index(kNoFaction)[0] != '\0'
        || faction_def_by_index(kNoFaction) != nullptr) {
        return fail("no-faction sentinel does not degrade to neutral");
    }

    // ── МАТРИЦА РОЖДАЕТСЯ НЕЙТРАЛЬНОЙ (вердикт владельца 2026-09-21) ─────
    // Прежде здесь стояли проверки враждебности фракций: демоны против
    // империи, бандиты против королевств, «магика» с ненулевыми связями. Все
    // они сторожили СЭМПЛИНГ из темпераментных банд — дословный порт
    // прототипа, вырезанный вместе с панелью дипломатии. Политика вернётся
    // после играбельного ядра ОДНИМ законом над реестром интересов, и тогда
    // сюда придут её свидетели.
    for (std::uint32_t seed : {12345u, 1u, 777u, 2026u}) {
        GameState gs;
        create_factions(gs, seed);

        // Every registry faction holds its own slot — including "magika", the
        // id that historically was emitted but never registered.
        for (int i = 0; i < kFactionCount; ++i) {
            if (sm::faction_slot(gs.relations, kFactionDefs[i].id) != i) {
                return fail("registry faction is not at its own ordinal");
            }
        }

        // МАТРИЦА = АВТОРСКАЯ ТАБЛИЦА, БУКВА В БУКВУ. Каждая пара обязана
        // равняться тому, что говорит kFactionRelations, — и ничему больше:
        // это и сторожит «одно число на пару, без броска».
        for (int a = 0; a < kFactionCount; ++a) {
            for (int b = a + 1; b < kFactionCount; ++b) {
                const int want = sm::authored_relation(kFactionDefs[a].id,
                                                       kFactionDefs[b].id);
                if (relation(gs, kFactionDefs[a].id, kFactionDefs[b].id) != want) {
                    return fail("матрица разошлась с авторской таблицей пар");
                }
            }
        }

        // СИД НА МАТРИЦУ НЕ ВЛИЯЕТ ВООБЩЕ: вместе с бандами ушёл и RNG-поток
        // генезиса фракций. Демоны враждебны империи на КАЖДОМ сиде, а не по
        // удаче броска — ровно то, чего прежняя система не гарантировала.
        if (relation(gs, "demons", "empire") != -100
            || !hostile(gs, "demons", "empire")) {
            return fail("демоны не враждебны империи — бой в субмире выключен");
        }
        if (!hostile(gs, "bandits", "empire") || !hostile(gs, "bandits", "timaert")) {
            return fail("бандиты не враждебны королевствам");
        }
        // Культ против магов — до дна шкалы; против прочих слегка, не война.
        if (relation(gs, "cults", "magika") != sm::kRelationMin) {
            return fail("охота на магов не дошла до дна шкалы");
        }
        if (hostile(gs, "cults", "empire")) {
            return fail("культ воюет с империей — звёздочка перебила пару");
        }
        // ЗВЕРЬ НЕЙТРАЛЕН (и был им всегда: банда Feral {-30,30} при пороге
        // -50 не давала враждебности ни на одном сиде) — олени не штурмуют
        // деревню.
        if (hostile(gs, "wildlife", "empire") || hostile(gs, "wildlife", "timaert")) {
            return fail("wildlife wrongly hostile (deer would swarm town)");
        }

        // Симметрия — закон записи (set_relation), а не свойство сэмпла.
        if (relation(gs, "demons", "empire") != relation(gs, "empire", "demons")) {
            return fail("relation matrix is not symmetric");
        }

        // Сам себе — ВЕРХ ШКАЛЫ, а не круглая сотня (CANON S26, закон
        // диапазона): своих не бьют ни при каком представлении.
        if (relation(gs, "empire", "empire") != sm::kRelationMax
            || hostile(gs, "empire", "empire") || hostile(gs, "demons", "demons")) {
            return fail("self-relation is not the top of the scale");
        }

        // Unknown ids still degrade to neutral (fail-closed for stale data).
        if (hostile(gs, "no_such_faction", "empire")
            || hostile(gs, "empire", "no_such_faction")) {
            return fail("unknown faction id did not degrade to neutral");
        }

        // ИГРОК — ОБЫЧНАЯ СТРОКА МАТРИЦЫ. Колонки playerReputation больше нет
        // (вырезана 2026-09-21): его встречают те же авторские пары, что и
        // всякого чужого, и проверяется это той же дверью.
        for (int i = 0; i < kFactionCount; ++i) {
            const char* id = kFactionDefs[i].id;
            if (std::strcmp(id, kPlayerFactionId) == 0) continue;
            const int want = sm::authored_relation(id, kPlayerFactionId);
            if (player_reputation(&gs, id) != want
                || faction_relation(&gs, id, kPlayerFactionId) != want) {
                return fail("игрок встречен не по авторской таблице");
            }
        }
        // Бандиты и демоны хотят его смерти БЕЗ отдельной колонки — по той же
        // звёздочке, что делает их врагами всем.
        if (!hostile(gs, "bandits", kPlayerFactionId)
            || !hostile(gs, "demons", kPlayerFactionId)) {
            return fail("игрока не встречают враждебно бандиты и демоны");
        }
        // And moving it moves both directions at once.
        add_player_reputation(gs, "empire", -30);
        if (player_reputation(&gs, "empire") != -30
            || faction_relation(&gs, "empire", kPlayerFactionId) != -30) {
            return fail("add_player_reputation did not write both directions");
        }
    }

    std::printf("OK faction_relations_test: registry=%d factions, one index "
                "space, matrix born NEUTRAL (politics cut), self=%d, player row "
                "seeded from the registry column (threshold=%d)\n",
                kFactionCount, sm::kRelationMax, sm::kHostileThreshold);
    CHECK(true, "every gate above held");
    return sm::test::report("faction_relations_test");
}

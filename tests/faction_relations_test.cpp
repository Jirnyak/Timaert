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
#include "tables/faction.h"   // registry + kHostileThreshold — THE hostility line lives with the relations
#include "macro/politik.h"

#include <cstdint>
#include <cstring>

namespace {

// The lookup under test, asked the way the game asks it: by SLOT over the
// faction ROWS (macro/factions.h, v121). An id the world never placed answers
// neutral, because its slot does not exist — the same fail-closed answer the
// map form gave for a missing key.
int relation(const sm::GameState& gs, const char* a, const char* b) {
    return sm::relation_of(gs.factions, sm::faction_slot(gs.factions, a),
                           sm::faction_slot(gs.factions, b));
}

bool hostile(const sm::GameState& gs, const char* a, const char* b) {
    return relation(gs, a, b) < sm::kHostileThreshold;
}

// ── Registry integrity (seed-independent) ─────────────────────────────────
// Unique non-empty ids, and the index door inverts the table in both
// directions. A loop that stopped at the first bad row would hide how much of
// the registry drifted, so every claim is counted over the WHOLE table.
void test_the_registry_is_one_index_space() {
    using namespace sm;
    int rows = 0, nameless = 0, duplicated = 0, notInverted = 0, badReverse = 0;
    for (int i = 0; i < kFactionCount; ++i) {
        ++rows;
        if (!kFactionDefs[i].id || kFactionDefs[i].id[0] == '\0') {
            ++nameless;
            continue;               // the claims below need an id to ask with
        }
        for (int j = i + 1; j < kFactionCount; ++j) {
            if (std::strcmp(kFactionDefs[i].id, kFactionDefs[j].id) == 0)
                ++duplicated;
        }
        if (faction_index(kFactionDefs[i].id) != i) ++notInverted;
        if (std::strcmp(faction_id_for_index(std::uint16_t(i)),
                        kFactionDefs[i].id) != 0) ++badReverse;
    }
    CHECK(rows == kFactionCount, "every registry row was actually examined");
    CHECK(rows > 0, "the registry is not empty — a world has factions");
    CHECK(nameless == 0, "every faction carries an id to be named by");
    CHECK(duplicated == 0,
          "no two factions answer to the same id — one index space, no "
          "colliding vocabularies");
    CHECK(notInverted == 0,
          "faction_index INVERTS the registry: a row's id maps back to its own "
          "ordinal");
    CHECK(badReverse == 0,
          "and faction_id_for_index maps the ordinal back to the same row");
}

void test_every_realm_names_an_existing_registry_row() {
    using namespace sm;
    int realms = 0, orphaned = 0;
    for (const auto& kd : realm_seed_defs()) {
        ++realms;
        if (faction_index(kd.factionId) < 0) ++orphaned;
    }
    CHECK(realms > 0, "the world is seeded with realms to check");
    CHECK(orphaned == 0,
          "every realm points at a registry row — the split registry that "
          "could drift is gone, so no realm's identity can vanish");
}

// Sentinels degrade safely, never alias a real faction. Each door alone.
void test_the_no_faction_sentinel_degrades_to_neutral() {
    using namespace sm;
    CHECK(faction_index(nullptr) < 0, "a null id is nobody, not row zero");
    CHECK(faction_index("") < 0, "an empty id is nobody either");
    CHECK(faction_id_for_index(kNoFaction)[0] == '\0',
          "the no-faction ordinal names no one");
    CHECK(faction_def_by_index(kNoFaction) == nullptr,
          "and hands back no row to read columns from");
}

// ── МАТРИЦА РОЖДАЕТСЯ ИЗ АВТОРСКОЙ ТАБЛИЦЫ (вердикт владельца 2026-09-21) ──
// Прежде здесь стояли проверки враждебности из СЭМПЛИНГА темпераментных банд —
// дословный порт прототипа, вырезанный вместе с панелью дипломатии. Политика
// вернётся после играбельного ядра ОДНИМ законом над реестром интересов, и
// тогда сюда придут её свидетели.
void check_one_seed(std::uint32_t seed) {
    using namespace sm;
    GameState gs;
    create_factions(gs, seed);

    // Every registry faction holds its own slot — including "magika", the id
    // that historically was emitted but never registered.
    int slots = 0, displaced = 0;
    for (int i = 0; i < kFactionCount; ++i) {
        ++slots;
        if (faction_slot(gs.factions, kFactionDefs[i].id) != i) ++displaced;
    }
    CHECK(slots == kFactionCount, "every faction was looked up in the matrix");
    CHECK(displaced == 0,
          "a born world seats every registry faction at its OWN ordinal");

    // МАТРИЦА = АВТОРСКАЯ ТАБЛИЦА, БУКВА В БУКВУ. Каждая пара обязана
    // равняться тому, что говорит kFactionRelations, — и ничему больше: это и
    // сторожит «одно число на пару, без броска».
    int pairs = 0, drifted = 0;
    for (int a = 0; a < kFactionCount; ++a) {
        for (int b = a + 1; b < kFactionCount; ++b) {
            ++pairs;
            const int want = authored_relation(kFactionDefs[a].id,
                                               kFactionDefs[b].id);
            if (relation(gs, kFactionDefs[a].id, kFactionDefs[b].id) != want)
                ++drifted;
        }
    }
    CHECK(pairs == kFactionCount * (kFactionCount - 1) / 2,
          "every unordered pair of factions was compared");
    CHECK(drifted == 0,
          "every pair equals the AUTHORED number and nothing else — one "
          "number per pair, no roll");

    // СИД НА МАТРИЦУ НЕ ВЛИЯЕТ ВООБЩЕ: вместе с бандами ушёл и RNG-поток
    // генезиса фракций. Враждебность есть свойство мира, а не удачи броска.
    CHECK(relation(gs, "demons", "empire") == -100,
          "demons meet the empire at the authored depth on EVERY seed");
    CHECK(hostile(gs, "demons", "empire"),
          "and that depth is past the hostility line — subworld combat is on");
    CHECK(hostile(gs, "bandits", "empire"),
          "bandits are hostile to the empire, every world");
    CHECK(hostile(gs, "bandits", "timaert"),
          "and to the realm the player starts in");
    // Культ против магов — до дна шкалы; против прочих слегка, не война.
    CHECK(relation(gs, "cults", "magika") == kRelationMin,
          "the cult's hunt for mages runs to the BOTTOM of the scale");
    CHECK(!hostile(gs, "cults", "empire"),
          "but the cult is not at war with the empire — one authored pair "
          "does not bleed into the rest of the row");
    // ЗВЕРЬ НЕЙТРАЛЕН (и был им всегда: банда Feral {-30,30} при пороге -50 не
    // давала враждебности ни на одном сиде) — олени не штурмуют деревню.
    CHECK(!hostile(gs, "wildlife", "empire"),
          "wildlife is not hostile to the empire — deer do not storm a town");
    CHECK(!hostile(gs, "wildlife", "timaert"),
          "nor to the realm: the beast is neutral to everyone's banner");

    // Симметрия — закон ЗАПИСИ (set_relation), а не свойство сэмпла.
    CHECK(relation(gs, "demons", "empire") == relation(gs, "empire", "demons"),
          "the matrix is symmetric: enmity reads the same from both sides");

    // Сам себе — ВЕРХ ШКАЛЫ, а не круглая сотня (CANON S26, закон диапазона).
    CHECK(relation(gs, "empire", "empire") == kRelationMax,
          "a faction stands with itself at the TOP of the scale, whatever the "
          "scale's representation");
    CHECK(!hostile(gs, "empire", "empire"), "and never fights itself");
    CHECK(!hostile(gs, "demons", "demons"), "not even the demons do");

    // Unknown ids still degrade to neutral (fail-closed for stale data).
    CHECK(!hostile(gs, "no_such_faction", "empire"),
          "an id the world never placed is met neutrally, not with a sword");
    CHECK(!hostile(gs, "empire", "no_such_faction"),
          "and neutrally in the other direction too");

    // ИГРОК — ОБЫЧНАЯ СТРОКА МАТРИЦЫ. Колонки playerReputation больше нет
    // (вырезана 2026-09-21): его встречают те же авторские пары, что и всякого
    // чужого, и проверяется это той же дверью.
    int rows = 0, byOwnLaw = 0, byTheDoor = 0;
    for (int i = 0; i < kFactionCount; ++i) {
        const char* id = kFactionDefs[i].id;
        if (std::strcmp(id, kPlayerFactionId) == 0) continue;
        ++rows;
        const int want = authored_relation(id, kPlayerFactionId);
        if (player_reputation(&gs, id) != want) ++byOwnLaw;
        if (faction_relation(&gs, id, kPlayerFactionId) != want) ++byTheDoor;
    }
    CHECK(rows == kFactionCount - 1,
          "every faction but the player's own was asked about the player");
    CHECK(byOwnLaw == 0,
          "the player is met by the AUTHORED pair, like any other stranger — "
          "he has no column of his own");
    CHECK(byTheDoor == 0,
          "and the general relation door answers the same as the player one — "
          "one law, two spellings");

    // Бандиты и демоны хотят его смерти БЕЗ отдельной колонки — по той же
    // звёздочке, что делает их врагами всем.
    CHECK(hostile(gs, "bandits", kPlayerFactionId),
          "bandits want the player dead — off the same wildcard that makes "
          "them everyone's enemy");
    CHECK(hostile(gs, "demons", kPlayerFactionId), "and so do the demons");

    // And moving it moves both directions at once.
    add_player_reputation(gs, "empire", -30);
    CHECK(player_reputation(&gs, "empire") == -30,
          "a deed against the empire moves the player's standing");
    CHECK(faction_relation(&gs, "empire", kPlayerFactionId) == -30,
          "...in BOTH directions at once — there is one number, not two");
}

void test_the_matrix_is_the_authored_table_on_every_seed() {
    int seeds = 0;
    for (std::uint32_t seed : {12345u, 1u, 777u, 2026u}) {
        ++seeds;
        check_one_seed(seed);
    }
    CHECK(seeds == 4, "all four worlds were actually built and examined");
}

} // namespace

int main() {
    test_the_registry_is_one_index_space();
    test_every_realm_names_an_existing_registry_row();
    test_the_no_faction_sentinel_degrades_to_neutral();
    test_the_matrix_is_the_authored_table_on_every_seed();
    return sm::test::report("faction_relations_test");
}

// АДРЕС ДАНЖА ЕСТЬ ПАРА (КЛЕТКА, ОРДИНАЛ) — свидетель закона M-248.
//
// Вердикт владельца 2026-11-10: «несколько входов из одной клетки в разные
// данжи — ЗАМЫСЕЛ, до «одна клетка — один данж» НЕ урезать; адрес — пара
// (cell, ordinal)». Ординал есть НОМЕР ДАНЖА В СВОЕЙ КЛЕТКЕ, один счёт на все
// роды; его выдаёт одна дверь (`dungeon_ordinal_next@src/sub/map_data.h`).
//
// ЧТО ЗДЕСЬ ДОКАЗЫВАЕТСЯ, И НА ЧЁМ:
//   1. МОДУЛИ ПОВЕРХНОСТИ, КАКИЕ ЕСТЬ В ДЕРЕВЕ, СТАВЯТ ВХОДЫ ПО ЗАКОНУ. Клетки
//      рождает производственная дверь `dispatch_generate` — город, деревня,
//      руина, шпиль, гора; у каждой клетки судится один и тот же закон входов
//      (`judge_entrances` ниже). Закон не знает родов по имени: он читает
//      колонки строки пропа (`structure_opens`, `structure_opens_top`).
//   2. ДВА РОДА В ОДНОЙ КЛЕТКЕ НЕ ДЕЛЯТ НОМЕР. На настоящую горную клетку с
//      устьем пещеры настоящей дверью застройки (`add_house_obb`) ставятся
//      дома: номера домов продолжают счёт клетки, а не начинают свой с нуля.
//      Именно здесь прежний закон («дом — номер дома среди домов») давал дом
//      №0 рядом с пещерой №0 и один поток сида на оба интерьера.
//   3. ДВЕРЬ НАХОДИТ СВОЁ ЗДАНИЕ ПО ЧИСЛУ, а не по месту в списке
//      (`door_building`), и находит его в клетке окна, а не в соседней.
//   4. БАШНЯ ШПИЛЯ: две двери (ворота и люк на короне) — ОДИН данж, один
//      ординал; тир едет числом структуры (`param`), и число ярусов от
//      ординала не зависит вовсе.
//   5. ПОТОК СИДА ИНТЕРЬЕРА РАЗЛИЧАЕТ АДРЕСА: разные ординалы одной клетки и
//      один ординал в разных клетках дают разные сиды.
#include "check.h"
#include "sub/dgn/dispatch.h"
#include "sub/gens/dispatch.h"
#include "sub/gens/kit/plots.h"
#include "sub/map_data.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <set>
#include <vector>

using namespace sm;
using namespace sm::sub;

namespace {

void generate(const CellContext& ctx, SubworldMapData& out) {
    float nbH[9];
    Biome nbB[9];
    std::uint8_t nbF[9];
    for (int i = 0; i < 9; ++i) {
        nbH[i] = ctx.macroHeight;
        nbB[i] = ctx.biome;
        nbF[i] = std::uint8_t(FT_None);
    }
    dispatch_generate(ctx, nbH, nbB, /*nbBiome5*/nullptr, nbF, out);
}

// Клетка открытого мира под фичей `feature` (FT_None — голый биом). Пара
// «фича + род места» честная: её штампует рождение сквада, и модуль зовётся по
// фиче (`kFeatureGenRows@src/sub/gens/dispatch.cpp`).
CellContext make_ctx(FeatureType feature, SquadType kind, Biome biome,
                     int pop, int tier, std::uint32_t seed) {
    CellContext ctx{};
    ctx.cx = 5;
    ctx.cy = 9;
    ctx.macroHeight = 0.64f;
    ctx.biome = biome;
    ctx.feature = feature;
    ctx.rooted.id = kind == SquadType::None ? -1 : 101;
    ctx.rooted.size = pop;
    ctx.rooted.kind = kind;
    ctx.rooted.tier = tier;
    ctx.seed = seed;
    return ctx;
}

struct EntranceCensus {
    int doors = 0;      // пропов-дверей всего (нижних и верхних)
    int dungeons = 0;   // разных ординалов = разных данжей клетки
    int buildings = 0;  // зданий (домов) в клетке
};

// ЗАКОН ВХОДОВ ОДНОЙ КЛЕТКИ. Возвращает перепись, чтобы звонящий мог
// потребовать, что клетка НЕ ПУСТА: закон над пустым списком верен всегда.
EntranceCensus judge_entrances(const SubworldMapData& map, const char* cell) {
    EntranceCensus c{};
    const std::uint16_t next = dungeon_ordinal_next(map);
    // По ординалу: род интерьера, число структуры, счёт нижних и верхних дверей.
    struct Slot { int ground = 0, top = 0; int kind = -1; int param = -1; };
    std::vector<Slot> slots(next);
    int outOfRange = 0, kindClash = 0, paramClash = 0;
    for (const Structure& s : map.structures) {
        const DungeonRef::Kind opens = structure_opens(s.kind);
        if (opens == DungeonRef::None) continue;
        ++c.doors;
        if (s.tag >= next) { ++outOfRange; continue; }
        Slot& slot = slots[s.tag];
        if (slot.kind >= 0 && slot.kind != int(opens)) ++kindClash;
        if (slot.param >= 0 && slot.param != int(s.param)) ++paramClash;
        slot.kind = int(opens);
        slot.param = int(s.param);
        if (structure_opens_top(s.kind)) ++slot.top; else ++slot.ground;
    }
    int gaps = 0, sharedGround = 0, orphanTop = 0;
    for (const Slot& slot : slots) {
        if (slot.kind < 0) { ++gaps; continue; }
        ++c.dungeons;
        if (slot.ground > 1) ++sharedGround;
        if (slot.top > 0 && slot.ground == 0) ++orphanTop;
    }
    char msg[256];
    std::snprintf(msg, sizeof msg,
                  "%s: every door's ordinal lies below the cell's next free one",
                  cell);
    CHECK(outOfRange == 0, msg);
    std::snprintf(msg, sizeof msg,
                  "%s: the ordinals are dense — 0..N-1 with no gap", cell);
    CHECK(gaps == 0, msg);
    std::snprintf(msg, sizeof msg,
                  "%s: no two ground doors share an ordinal — two dungeons of "
                  "one cell never share an address", cell);
    CHECK(sharedGround == 0, msg);
    std::snprintf(msg, sizeof msg,
                  "%s: doors sharing an ordinal open ONE dungeon — same kind, "
                  "same structure number", cell);
    CHECK(kindClash == 0 && paramClash == 0, msg);
    std::snprintf(msg, sizeof msg,
                  "%s: a top door belongs to a dungeon that has a ground door",
                  cell);
    CHECK(orphanTop == 0, msg);

    // Здания: у каждого дома ровно одна дверь с его числом, и дверь находит
    // именно его.
    int unresolved = 0, wrongBuilding = 0, doorless = 0;
    std::set<std::uint16_t> buildingTags;
    for (const Structure& b : map.structures) {
        if (b.kind != Structure::House) continue;
        ++c.buildings;
        buildingTags.insert(b.tag);
        int own = 0;
        for (const Structure& d : map.structures) {
            if (structure_opens(d.kind) == DungeonRef::House && d.tag == b.tag)
                ++own;
        }
        if (own != 1) ++doorless;
        const Structure* found = door_building(map.structures, 0.0f, 0.0f, b.tag);
        if (!found) ++unresolved;
        else if (found != &b) ++wrongBuilding;
    }
    std::snprintf(msg, sizeof msg,
                  "%s: every building has exactly one door carrying its "
                  "ordinal, and no two buildings share one", cell);
    CHECK(doorless == 0 && int(buildingTags.size()) == c.buildings, msg);
    std::snprintf(msg, sizeof msg,
                  "%s: a door resolves to ITS building by the number, not by "
                  "list position", cell);
    CHECK(unresolved == 0 && wrongBuilding == 0, msg);
    return c;
}

// Найти горную клетку, у которой есть устье: две трети горных клеток пещер не
// держат (`scatter_cave_mouths@src/sub/gens/mountain.cpp`), поэтому сид
// подбирается перебором, а не верой в один.
bool find_cave_cell(SubworldMapData& out, std::uint32_t& seedOut) {
    for (std::uint32_t seed = 1; seed <= 64; ++seed) {
        SubworldMapData m{};
        generate(make_ctx(FT_None, SquadType::None, Mountain, 0, 0, seed), m);
        for (const Structure& s : m.structures) {
            if (s.kind == Structure::CaveMouth) {
                out = std::move(m);
                seedOut = seed;
                return true;
            }
        }
    }
    return false;
}

// ── 1. Модули поверхности ставят входы по закону ────────────────────────────
void test_surface_modules_obey_the_entrance_law() {
    struct Case {
        const char* name; FeatureType feature; SquadType kind; Biome biome;
        int pop; int tier; bool wantsDoors;
    };
    const Case cases[] = {
        {"city",    FT_City,    SquadType::City,    Meadow, 2000, 0, true},
        {"village", FT_Village, SquadType::Village, Meadow, 120,  0, true},
        {"spire",   FT_Spire,   SquadType::Spire,   Meadow, 0,    3, true},
        // Руина и голая земля входов могут не иметь — закон судит то, что есть.
        {"ruin",    FT_Ruin,    SquadType::None,    Meadow, 0,    0, false},
        {"meadow",  FT_None,    SquadType::None,    Meadow, 0,    0, false},
    };
    int cellsWithDoors = 0;
    for (const Case& k : cases) {
        SubworldMapData map{};
        generate(make_ctx(k.feature, k.kind, k.biome, k.pop, k.tier,
                          0xC17A5EEDu), map);
        const EntranceCensus c = judge_entrances(map, k.name);
        if (c.doors > 0) ++cellsWithDoors;
        if (k.wantsDoors) {
            char msg[128];
            std::snprintf(msg, sizeof msg,
                          "%s: the cell raises at least one entrance", k.name);
            CHECK(c.doors > 0 && c.dungeons > 0, msg);
        }
    }
    CHECK(cellsWithDoors >= 3,
          "the law was judged on cells that actually carry entrances");

    // Город: МНОГО входов из одной клетки в РАЗНЫЕ данжи — замысел дословно.
    SubworldMapData city{};
    generate(make_ctx(FT_City, SquadType::City, Meadow, 2000, 0, 0xC17A5EEDu),
             city);
    const EntranceCensus cc = judge_entrances(city, "city (census)");
    CHECK(cc.dungeons >= 2 && cc.dungeons == cc.buildings,
          "a city cell holds several dungeons — one per building, each with "
          "its own ordinal");
}

// ── 2 + 3. Два рода в одной клетке; дверь находит здание по числу ───────────
void test_two_kinds_in_one_cell_never_share_an_ordinal() {
    SubworldMapData cell{};
    std::uint32_t seed = 0;
    CHECK_OR_RETURN(find_cave_cell(cell, seed),
                    "fixture: some mountain cell within 64 seeds opens a cave");
    const EntranceCensus before = judge_entrances(cell, "mountain");
    CHECK_OR_RETURN(before.dungeons >= 1 && before.buildings == 0,
                    "fixture: the mountain cell has caves and no buildings");

    // Дома встают настоящей дверью застройки на ту же клетку — ПОСЛЕ устьев.
    const std::uint16_t firstFree = dungeon_ordinal_next(cell);
    int raised = 0;
    const float spots[3][2] = {{200.0f, 200.0f}, {500.0f, 820.0f},
                               {820.0f, 300.0f}};
    for (const auto& p : spots) {
        if (kit::add_house_obb(cell, p[0], p[1], 6.0f, 5.0f, 0.0f, 4.0f,
                               /*requireClear*/false)) {
            ++raised;
        }
    }
    CHECK_OR_RETURN(raised >= 2, "fixture: at least two houses were raised");

    const EntranceCensus after = judge_entrances(cell, "mountain + houses");
    CHECK(after.dungeons == before.dungeons + raised
              && after.buildings == raised,
          "houses raised on a cell that already has caves CONTINUE the cell's "
          "count: every dungeon of the cell keeps its own ordinal");

    // Прямо: ни один дом не взял номер пещеры, и первый дом взял первый
    // свободный номер клетки (а не ноль «среди домов»).
    std::set<std::uint16_t> caveOrd, houseOrd;
    for (const Structure& s : cell.structures) {
        if (s.kind == Structure::CaveMouth) caveOrd.insert(s.tag);
        if (s.kind == Structure::Door) houseOrd.insert(s.tag);
    }
    int shared = 0;
    for (const std::uint16_t o : houseOrd) shared += int(caveOrd.count(o));
    CHECK(shared == 0 && !caveOrd.empty() && int(houseOrd.size()) == raised,
          "no house door carries a cave's ordinal");
    CHECK(!houseOrd.empty() && *houseOrd.begin() == firstFree && firstFree > 0,
          "the first house took the cell's next free ordinal, not a zero of "
          "its own kind");

    // Сиды интерьеров всех данжей клетки различны.
    std::set<std::uint32_t> seeds;
    std::set<std::uint16_t> all = caveOrd;
    all.insert(houseOrd.begin(), houseOrd.end());
    for (const std::uint16_t o : all)
        seeds.insert(dungeon_scene_seed(12345u, 5, 9, o, 0));
    CHECK(seeds.size() == all.size() && all.size() >= 3,
          "every dungeon of the cell has its own interior seed stream");

    // Устье пещеры зданием не является: по его числу здание не находится, и
    // движок берёт форму интерьера у самого проёма.
    int caveResolvesToBuilding = 0;
    for (const std::uint16_t o : caveOrd) {
        if (door_building(cell.structures, 0.0f, 0.0f, o)) ++caveResolvesToBuilding;
    }
    CHECK(caveResolvesToBuilding == 0,
          "a cave's ordinal names no building — its mouth is its own shape");

    // Клетка окна: та же клетка, сдвинутая в окне на (1, 2), находится своим
    // углом, а по чужому углу здание не находится.
    std::vector<Structure> shifted = cell.structures;
    const float ox = float(kCellSize), oy = 2.0f * float(kCellSize);
    for (Structure& s : shifted) { s.x += ox; s.y += oy; }
    const std::uint16_t probe = *houseOrd.begin();
    CHECK(door_building(shifted, ox, oy, probe) != nullptr
              && door_building(shifted, 0.0f, 0.0f, probe) == nullptr
              && door_building(shifted, ox, 0.0f, probe) == nullptr,
          "a building is looked up inside the door's OWN window cell — the "
          "same ordinal in a neighbouring cell is another dungeon");
}

// ── 4. Башня шпиля: один данж, две двери, тир — числом структуры ────────────
void test_spire_tower_is_one_dungeon_behind_two_doors() {
    int tiersSeen = 0;
    for (int tier = 1; tier <= 5; ++tier) {
        SubworldMapData map{};
        generate(make_ctx(FT_Spire, SquadType::Spire, Meadow, 0, tier,
                          0x2468ACE0u), map);
        const Structure* gate = nullptr;
        const Structure* hatch = nullptr;
        for (const Structure& s : map.structures) {
            if (s.kind == Structure::SpireGate) gate = &s;
            if (s.kind == Structure::SpireHatch) hatch = &s;
        }
        CHECK_OR_RETURN(gate && hatch,
                        "fixture: a spire cell raises its gate and its hatch");
        ++tiersSeen;
        CHECK(gate->tag == hatch->tag,
              "gate and crown hatch carry ONE ordinal — one tower, one dungeon");
        CHECK(int(gate->param) == tier && int(hatch->param) == tier,
              "the tier rides the structure number on both doors");
        CHECK(gate->tag == 0,
              "the lone tower of its cell is dungeon number 0 at ANY tier — "
              "the ordinal is an address, the tier does not ride in it");

        // Число ярусов читается с числа структуры и от ординала не зависит.
        DungeonRef ref{};
        ref.kind = DungeonRef::SpireTower;
        ref.param = gate->param;
        ref.ordinal = gate->tag;
        const int floors = dungeon_spire_tower_floors(ref);
        ref.ordinal = 40;
        CHECK(floors == tier && dungeon_spire_tower_floors(ref) == tier,
              "storeys = tier, whatever ordinal the tower holds in its cell");
    }
    CHECK(tiersSeen == 5, "all five tiers were raised and judged");
}

// ── 5. Сид интерьера различает адреса ───────────────────────────────────────
void test_scene_seed_separates_addresses() {
    std::set<std::uint32_t> seeds;
    int n = 0;
    for (int cx = 0; cx < 4; ++cx) {
        for (int cy = 0; cy < 4; ++cy) {
            for (std::uint16_t ord = 0; ord < 16; ++ord) {
                for (int level = -1; level <= 1; ++level) {
                    seeds.insert(dungeon_scene_seed(777u, cx, cy, ord,
                                                    std::int8_t(level)));
                    ++n;
                }
            }
        }
    }
    CHECK(int(seeds.size()) == n && n == 4 * 4 * 16 * 3,
          "(cell, ordinal, level) → seed is injective over the sample: no two "
          "addresses share a stream");
    CHECK(dungeon_scene_seed(777u, 2, 3, 5, 0)
              == dungeon_scene_seed(777u, 2, 3, 5, 0),
          "the same address re-derives the same stream");
}

} // namespace

int main() {
    test_surface_modules_obey_the_entrance_law();
    test_two_kinds_in_one_cell_never_share_an_ordinal();
    test_spire_tower_is_one_dungeon_behind_two_doors();
    test_scene_seed_separates_addresses();
    return sm::test::report("dungeon_address_test");
}

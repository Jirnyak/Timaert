// Locks the UNIVERSAL PROP / INTERACTION system: the kStructureKindRows
// columns that turned "a prop" into "a thing the world can draw, light and
// press E on" (sub/map_data.h), the two generators that place the first such
// props (sub/gens/dispatch.cpp city doors + street lanterns, sub/dgn/house.cpp
// interior door + stair blocks), and — since M-262 killed the snapshot cache —
// the law that replaced it: a revisited cell is REGENERATED from its identity,
// and the only memory of a felled tree is the macro COUNT.
//
// What is promised and asserted here:
//   1. TABLE CONSISTENCY — pure, no generation. Every kind that declares an
//      interaction has a usable InteractRow behind it (a non-empty verb; the
//      REACH is no longer a column of that row — it is the arm of the body
//      reaching, asserted once at the top of this file); every lit kind
//      carries a colour and
//      hangs its flame above its seat; every kind the SOLID pass draws has a
//      height floor to draw. Derived from the table, never from restated
//      numbers, and the row count is static_asserted against
//      Structure::kKindCount so adding a kind without a row cannot compile.
//   2. CITY DOORS — a city cell raises houses and gives EVERY one of them
//      exactly one door, tagged with the ordinal the engine counts back
//      (engine.cpp enter_dungeon_by_door counts Houses in generation order).
//      The tag set is exactly {0..houses-1}: a duplicate would open one
//      interior from two houses, a gap would leave a house unenterable. Each
//      door hangs ON its own house's surface.
//   3. LANTERNS — a city lights its lanes, and every lantern is a lit kind
//      (the engine hangs its LightEmitter off structure_is_lit, so a lantern
//      that is not lit is an unlit post).
//   4. INTERIOR PROPS — the way OUT is a prop, not paint: exactly one Door on
//      the ground storey, within an ARM's reach of the shared
//      dungeon_entry_point; and every shaft this storey carries stands a
//      Stairs block, tagged 1 up / 0 down, exactly on the shared
//      dungeon_stair_point. The cellar is a seeded coin, so the test takes
//      whichever branch dungeon_has_cellar names and ASSERTS that it took one.
//   5. REVISIT IS REGENERATION — the cell is a function of its identity (two
//      runs agree column for column), the macro tree count is the ONE memory
//      of felling (fewer trees, and no other kind moved), the thinning order
//      is the number's and not the player's (survivors are a positional
//      SUBSET), and prop height is non-negative now that the decay sign-bit
//      overlay is gone.
#include "check.h"
#include "sub/dgn/dispatch.h"
#include "sub/gens/dispatch.h"
#include "sub/map_data.h"
#include "tables/forest.h"
// Дотягивание — свойство ТЕЛА, не вещи (вердикт владельца 2026-09-26), поэтому
// свидетель берёт предел из строки того тела, которым игрок начинает игру, той
// же колонкой, что читает движок через player_arm_reach().
#include "tables/npc.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

// ОДИН ФАКТ ВМЕСТО ВОСЬМИ КОЛОНОК, И ОН ИЗВЕСТЕН КОМПИЛЯТОРУ (§8 п.6): предел
// любой интеракции — рука ТЕЛА, поэтому «внутрь предела можно встать»
// спрашивается один раз о руке, а не по разу о каждом роде пропа. Строка
// стартового тела и есть та, чью колонку отдаёт SubworldEngine::player_arm_reach.
static_assert(sm::kAdventurerCombat.attackRange > 0.0f,
              "рука тела — предел любой интеракции, и она положительна");

using namespace sm;
using namespace sm::sub;

namespace {

// The table's row count is a compile-time fact, not a hopeful comment: a kind
// added to the enum without a row here fails the build instead of falling into
// structure_kind_row's clamp-to-Tree fallback at runtime.
static_assert(sizeof(kStructureKindRows) / sizeof(kStructureKindRows[0])
                  == std::size_t(Structure::kKindCount),
              "every Structure::Kind needs exactly one prop row");
constexpr int kKindRows =
    int(sizeof(kStructureKindRows) / sizeof(kStructureKindRows[0]));

bool empty_str(const char* s) { return s == nullptr || s[0] == '\0'; }

// Full production route for an open-air cell: dispatch_generate resolves the
// mode itself, so a City context exercises exactly what the game runs.
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

CellContext make_city_ctx(std::uint32_t seed) {
    CellContext ctx{};
    ctx.cx = 5;
    ctx.cy = 9;
    ctx.macroHeight = 0.64f;
    ctx.biome = Meadow;
    ctx.feature = FT_None;
    ctx.place.id = 101;
    ctx.place.size = 2000;
    ctx.place.kind = SquadType::City;
    ctx.seed = seed;
    return ctx;
}

CellContext make_house_ctx(float footHx, float footHy, std::int8_t level) {
    CellContext ctx{};
    ctx.cx = 3;
    ctx.cy = -2;
    ctx.macroHeight = 0.62f;
    ctx.biome = Meadow;
    ctx.feature = FT_None;
    ctx.place.id = -1;
    ctx.place.size = 0;
    ctx.seed = 0xD00DCAFEu;
    ctx.dungeon.kind = DungeonRef::House;
    ctx.dungeon.level = level;
    ctx.dungeon.ordinal = 7;
    ctx.dungeon.footHx = footHx;
    ctx.dungeon.footHy = footHy;
    return ctx;
}

// Per-kind census — the ONE detector section 5 leans on. It counts what it was
// GIVEN (never a total remembered from before a mutation) and reports how many
// props it saw in total, so a census over an empty map cannot read as "equal".
struct KindCensus {
    int n[kKindRows] = {};
    int total = 0;
};
KindCensus census(const std::vector<Structure>& v) {
    KindCensus c{};
    for (const Structure& s : v) {
        if (int(s.kind) < kKindRows) ++c.n[int(s.kind)];
        ++c.total;
    }
    return c;
}

int count_kind(const std::vector<Structure>& v, Structure::Kind k) {
    int n = 0;
    for (const Structure& s : v) {
        if (s.kind == k) ++n;
    }
    return n;
}

// ── 1. The prop table is self-consistent ───────────────────────────────────
void test_table_consistency() {
    int rows = 0;
    int interactive = 0, lit = 0, solidDrawn = 0;
    int badInteract = 0, badLight = 0, badSolidHeight = 0;
    for (int k = 0; k < kKindRows; ++k) {
        ++rows;
        const auto kind = Structure::Kind(k);
        const StructureKindRow& row = structure_kind_row(kind);

        // An interactive kind must have a verb to print. РЕАЧ БОЛЬШЕ НЕ
        // КОЛОНКА ЭТОЙ СТРОКИ (2026-09-26): предел дотягивания один и он у
        // тела, поэтому «можно ли внутрь него встать» спрашивается ниже — один
        // раз о руке, а не по разу о каждом роде.
        const InteractId id = structure_interact(kind);
        if (id != InteractId::None) {
            ++interactive;
            if (empty_str(interact_row(id).verb)) ++badInteract;
        }

        // A lit kind carries a colour and hangs its flame above its seat;
        // light with no colour is a black lamp, light at 0 m burns in the mud.
        if (structure_is_lit(kind)) {
            ++lit;
            if (row.lightRgb == 0u || !(row.lightHeightM > 0.0f)) ++badLight;
        }

        // The solid pass draws a box of structure_visible_height, which floors
        // at minHeight: a Solid-drawn kind with no floor draws nothing when a
        // record comes through degenerate.
        if (structure_draw(kind) == StructureKindRow::Draw::Solid) {
            ++solidDrawn;
            if (!(structure_min_height(kind) > 0.0f)) ++badSolidHeight;
        }
    }
    CHECK(rows == Structure::kKindCount,
          "the sweep read one prop row for every Structure::Kind");
    CHECK(interactive > 0 && badInteract == 0,
          "every interactive kind has a verb to print");
    CHECK(lit > 0 && badLight == 0,
          "every lit kind carries a colour and hangs above its seat");
    CHECK(solidDrawn > 0 && badSolidHeight == 0,
          "every Solid-drawn kind has a height floor to draw");

    // The interaction table itself: every id but None names a verb, and None
    // is the scenery row — it must stay silent, or a tree would offer a prompt.
    int ids = 0, missingVerb = 0;
    for (int i = 0; i < int(InteractId::Count); ++i) {
        ++ids;
        const InteractRow& ir = interact_row(InteractId(i));
        if (InteractId(i) == InteractId::None) {
            if (!empty_str(ir.verb)) ++missingVerb;
        } else if (empty_str(ir.verb)) {
            ++missingVerb;
        }
    }
    CHECK(ids == int(InteractId::Count) && missingVerb == 0,
          "every InteractId but None names a verb; None is silent");

    // The two payload contracts the generators stamp and the engine reads.
    CHECK(structure_interact(Structure::Door) == InteractId::Door,
          "contract: a Door prop dispatches the Door interaction");
    // Both ends of a shaft take the same verb: a lid you open at your feet
    // and a ladder you climb are one interaction wearing two shapes.
    CHECK(structure_interact(Structure::Hatch) == InteractId::Stairs,
          "contract: a Hatch prop dispatches the shaft interaction");
    CHECK(structure_interact(Structure::Ladder) == InteractId::Stairs,
          "contract: a Ladder prop dispatches the same shaft interaction");
    // The crown's hatch is a DOOR into the tower, and it opens the far END of
    // the climb — the one prop in the game whose opensTop column is set.
    CHECK(structure_opens(Structure::SpireHatch) == DungeonRef::SpireTower
              && structure_opens_top(Structure::SpireHatch),
          "contract: the crown hatch opens the tower at its top storey");
    CHECK(!structure_opens_top(Structure::SpireGate),
          "contract: the gate at the foot opens the ground floor");
    // A door hangs flush on a wall that already blocks: solid, it would be a
    // door you bump into instead of opening.
    CHECK(!structure_is_solid(Structure::Door),
          "contract: a Door is not solid — the wall it hangs on blocks");
    CHECK(structure_is_lit(Structure::Lantern),
          "contract: a Lantern is a lit kind (the engine hangs its emitter "
          "off exactly this column)");
}

// ── 2 + 3. City doors and lanterns ─────────────────────────────────────────
// Both sections read ONE generated city (a 1024² cell is expensive; the map is
// handed on to section 5, which needs the very same cell anyway).
void test_city_props(const SubworldMapData& city) {
    const int houses = count_kind(city.structures, Structure::House);
    const int doors  = count_kind(city.structures, Structure::Door);
    CHECK_OR_RETURN(houses > 0, "fixture: the city cell raises houses at all");
    CHECK(doors == houses,
          "every house is given exactly one door — the promise the entry "
          "path relies on");

    // Tag census: the door's tag names its house by the ordinal the engine
    // counts back. The set must be exactly {0..houses-1} — a duplicate opens
    // one interior from two doors, a gap leaves a house unenterable.
    std::vector<int> tagged(std::size_t(houses), 0);
    int doorsSeen = 0, outOfRange = 0;
    for (const Structure& s : city.structures) {
        if (s.kind != Structure::Door) continue;
        ++doorsSeen;
        if (int(s.tag) >= houses) { ++outOfRange; continue; }
        ++tagged[std::size_t(s.tag)];
    }
    CHECK(doorsSeen == doors && outOfRange == 0,
          "every door's tag is an ordinal of a house that exists");
    int duplicates = 0, gaps = 0, ordinals = 0;
    for (int i = 0; i < houses; ++i) {
        ++ordinals;
        if (tagged[std::size_t(i)] == 0) ++gaps;
        if (tagged[std::size_t(i)] > 1) ++duplicates;
    }
    CHECK(ordinals == houses && gaps == 0 && duplicates == 0,
          "the door tags are exactly {0..houses-1} — no gap, no duplicate");

    // Each door hangs ON its own house: resolve the house by COUNTING Houses
    // in generation order, the same rule enter_dungeon_by_door uses, then
    // measure to that house's surface (never to its centre — a wide hall would
    // be unreachable from its own doorstep).
    int measured = 0, adrift = 0;
    float worstD2 = 0.0f;
    for (const Structure& d : city.structures) {
        if (d.kind != Structure::Door) continue;
        const Structure* house = nullptr;
        std::uint16_t seen = 0;
        for (const Structure& s : city.structures) {
            if (s.kind != Structure::House) continue;
            if (seen == d.tag) { house = &s; break; }
            ++seen;
        }
        if (!house) { ++adrift; continue; }
        ++measured;
        const float d2 = structure_surface_dist2(*house, d.x, d.y);
        if (d2 > worstD2) worstD2 = d2;
        if (d2 > 1.0f) ++adrift;   // a tile off its own wall is a door adrift
    }
    CHECK(measured == doors && adrift == 0,
          "every door lies on the surface of the house its tag names");
    CHECK(worstD2 >= 0.0f && measured > 0,
          "the door-placement sweep measured every door");

    // Lanterns: the town lights its lanes, and the light comes from the row.
    int lanterns = 0, unlit = 0;
    for (const Structure& s : city.structures) {
        if (s.kind != Structure::Lantern) continue;
        ++lanterns;
        if (!structure_is_lit(s.kind)) ++unlit;
    }
    CHECK(lanterns > 0, "a walled city lights its lanes with lantern props");
    CHECK(unlit == 0, "every placed lantern is a lit kind");
}

// ── 4. Interior props: the way out and the shafts ──────────────────────────
void test_interior_props() {
    const CellContext ctx = make_house_ctx(8.0f, 6.0f, /*level*/0);
    SubworldMapData out{};
    generate(ctx, out);

    // Exactly one door on the ground storey — the way back to the street.
    int doors = 0;
    const Structure* door = nullptr;
    for (const Structure& s : out.structures) {
        if (s.kind != Structure::Door) continue;
        ++doors;
        door = &s;
    }
    CHECK_OR_RETURN(doors == 1 && door != nullptr,
                    "the ground storey carries exactly one Door prop");
    float px = 0.0f, py = 0.0f;
    dungeon_entry_point(ctx.dungeon, px, py);
    // Предел — РУКА тела, которым игрок начинает (та же колонка, что отдаёт
    // SubworldEngine::player_arm_reach): дверь обязана стоять в пределах
    // вытянутой руки от площадки входа, иначе с порога её не открыть.
    const float reach = sm::kAdventurerCombat.attackRange;
    CHECK(structure_surface_dist2(*door, px, py) <= reach * reach,
          "the exit door stands within an arm's reach of the entry pad");

    // Shafts. The NW shaft climbs and exists whenever the room is big enough
    // to be worth a storey; the NE shaft descends on a seeded coin.
    CHECK_OR_RETURN(dungeon_has_upper(ctx.dungeon),
                    "fixture: this footprint's room seats an upper storey");
    float upX = 0.0f, upY = 0.0f, dnX = 0.0f, dnY = 0.0f;
    dungeon_stair_point(ctx.dungeon, /*up*/true,  upX, upY);
    dungeon_stair_point(ctx.dungeon, /*up*/false, dnX, dnY);

    // A climb is a ladder plus the opening it reaches, so the UP shaft is
    // counted by its ladder — the lid above it rides the same point and the
    // same tag, and is asserted flush with the ceiling by the tower's test.
    int upStairs = 0, downStairs = 0, offShaft = 0, stairsSeen = 0;
    for (const Structure& s : out.structures) {
        if (s.kind == Structure::Hatch && s.zBase > 0.0f) continue;
        if (s.kind != Structure::Hatch && s.kind != Structure::Ladder) continue;
        ++stairsSeen;
        if (s.tag == 1) {
            ++upStairs;
            // The generator stamps the SHARED rule's point, so the prop sits
            // exactly where the engine will look for it — not merely near it.
            if (s.x != upX || s.y != upY) ++offShaft;
        } else if (s.tag == 0) {
            ++downStairs;
            if (s.x != dnX || s.y != dnY) ++offShaft;
        } else {
            ++offShaft;   // a tag that is neither up nor down names no shaft
        }
    }
    CHECK(upStairs == 1,
          "a storey worth climbing stands exactly one tag==1 (up) stair block");
    CHECK(stairsSeen == upStairs + downStairs && offShaft == 0,
          "every stair block sits exactly on its shared shaft point");

    // The cellar is a design coin, not an invariant — take whichever branch
    // the roll names, and assert that a branch WAS taken (an unbranched
    // section here would assert nothing at all).
    int branches = 0;
    if (dungeon_has_cellar(ctx.dungeon, ctx.worldSeed, ctx.cx, ctx.cy)) {
        ++branches;
        CHECK(downStairs == 1,
              "a house with a cellar stands its tag==0 (down) stair block");
    } else {
        ++branches;
        CHECK(downStairs == 0,
              "a house without a cellar stands no descending stair block");
    }
    CHECK(branches == 1, "the cellar coin took exactly one asserted branch");
}

// ── 5. ВОЗВРАТ В КЛЕТКУ ЕСТЬ ГЕНЕРАЦИЯ, А ПАМЯТЬ О РУБКЕ — ЧИСЛО ──────
// Здесь стоял «круговой рейс снимка» — свидетель того, что слияние кэша
// возвращает в перепосещённую клетку все роды пропов. Он ушёл ВМЕСТЕ С
// МЕХАНИКОЙ (§5 п.6), потому что самого снимка больше нет: M-262, вердикт
// владельца 2026-10-18 — кэш был ВТОРОЙ ПАМЯТЬЮ срубленного рядом с числом
// макро-поля и жил мимо сейва.
//
// Закон, который стал на его место, сильнее снесённого и держит ровно то, чем
// снос оправдан (ЗАКОН ШВА, лосси-проекция в обе стороны):
//   · клетка есть ФУНКЦИЯ СВОЕЙ ИДЕНТИЧНОСТИ — два прогона одного и того же
//     контекста дают совпадающие пропы колонка в колонку, и именно поэтому
//     хранить их не надо вовсе;
//   · единственная память о рубке — ЧИСЛО КЛЕТКИ: меньше макро-счёт
//     деревьев — строго меньше деревьев в клетке, и ни одного другого рода
//     число не касается;
//   · прореживает число СВОИМ ПОРЯДКОМ, а не выбором игрока: выжившие
//     стволы есть ПОДМНОЖЕСТВО богатой клетки по положению. Это и есть
//     вердикт владельца «пня ваще нет»: игрок видит N−1 дерево, и пропасть
//     может не то, которое он валил. ВЫСОТЫ выживших тут НЕ
//     утверждаются, и это сознательно: каждый ствол тянет свой рост из
//     ОДНОГО потока `sizeRng` в порядке ставки, то есть прореженная клетка
//     не «богатая минус деревья» (`scatter_universal_trees@src/sub/base_generator.cpp`).
// И сюда же сел свидетель СНЕСЁННОГО ОВЕРЛЕЯ ВЕТХОСТИ: минус в высоте
// ставило только слияние снимка, и с его смертью отрицательная высота
// стала НЕДОСТИЖИМОЙ — значит это теперь утверждение о мире, а не страж.
// НЕГАТИВНЫЙ КОНТРОЛЬ, ПРОГНАННЫЙ И ЗАПИСАННЫЙ ЧИСЛОМ (§8 п.6; 2026-10-19,
// M-262). Таблица стоит ЗДЕСЬ, а не только в теле коммита: прибор, чья
// чувствительность доказана лишь в git log, следующему читателю недоступна.
//   мутация                                             → исход
//   ставка деревьев от константы 4096 вместо макро-счёта → 1 из 35 КРАСНАЯ
//                                                          («меньше счёт —
//                                                          строго меньше»)
//   место ставки не функция идентичности (статик-дрейф)  → 3 из 35 КРАСНЫХ
//                                                          (список, перепись,
//                                                          подмножество)
//   отрицательная высота ДО честного присвоения          → МОЛЧАНИЕ, код 0 —
//                                                          мутация НЕГОДНА:
//                                                          её затирала строка
//                                                          `s.height = treeH`
//                                                          ниже. Молчание
//                                                          прибора на такой
//                                                          мутации вердиктом
//                                                          НЕ является
//   отрицательная высота ПОСЛЕ присвоения                → 1 из 35 КРАСНАЯ
//   эквивалентная перезапись строки плотности            → МОЛЧАНИЕ, 35/35 —
//                                                          детектор не
//                                                          краснеет на всё
// Довоз величины ЧЕРЕЗ ФАЙЛ — не здесь, а в `save_roundtrip_test`: мутация
// «загрузка возвращает ровную сетку вместо сохранённой» даёт там 1 из 21
// красной («tree grid lost»); мутация «выбросить сетку» НЕ СОБИРАЕТСЯ вовсе.
bool same_prop(const Structure& a, const Structure& b) {
    return a.kind == b.kind && a.shape == b.shape && a.zWorld == b.zWorld
        && a.tag == b.tag
        && a.x == b.x && a.y == b.y && a.radius == b.radius
        && a.height == b.height && a.yaw == b.yaw
        && a.hx == b.hx && a.hy == b.hy && a.zBase == b.zBase;
}

// Генерация с ЯВНЫМ макро-счётом деревьев по кольцу — тот самый вход,
// который менеджер шва собирает из `CellContext::treeCount` живого мира.
void generate_with_trees(const CellContext& ctx, int treeCount,
                         SubworldMapData& out) {
    float nbH[9];
    Biome nbB[9];
    std::uint8_t nbF[9];
    int nbT[9];
    for (int i = 0; i < 9; ++i) {
        nbH[i] = ctx.macroHeight;
        nbB[i] = ctx.biome;
        nbF[i] = std::uint8_t(FT_None);
        nbT[i] = treeCount;
    }
    dispatch_generate(ctx, nbH, nbB, /*nbBiome5*/nullptr, nbF, out,
                      /*nbPlace*/nullptr, nbT, /*nbFertility*/nullptr);
}

void test_revisit_is_regeneration() {
    CellContext ctx{};
    ctx.cx = -7;
    ctx.cy = 4;
    ctx.macroHeight = 0.58f;
    ctx.biome = Meadow;
    ctx.feature = FT_None;
    ctx.place.id = -1;
    ctx.place.size = 0;
    ctx.seed = 0x7A1DF00Du;

    // ОБА СЧЁТА НИЖЕ ЛЕСНОГО ПОРОГА И БЕРУТСЯ ИЗ ТАБЛИЦЫ, а не с потолка:
    // `is_forest_cell` переводит клетку в ДРУГОЙ режим субмира, а два режима
    // сравнивать нечестно — сравнивается действие ЧИСЛА, а не смена генератора.
    const int rich = kForestClassTreeCount / 2;    // 4096
    const int thin = kForestClassTreeCount / 8;    // 1024
    static_assert(kForestClassTreeCount / 2 < kForestClassTreeCount
                  && kForestClassTreeCount / 8 < kForestClassTreeCount / 2,
                  "оба счёта нелесные, и прореженный строго меньше богатого");
    CHECK(!is_forest_cell(rich) && !is_forest_cell(thin),
          "fixture: оба счёта лежат по одну сторону лесного порога");

    SubworldMapData first{};
    SubworldMapData again{};
    generate_with_trees(ctx, rich, first);
    generate_with_trees(ctx, rich, again);

    const KindCensus c1 = census(first.structures);
    const KindCensus c2 = census(again.structures);
    CHECK_OR_RETURN(c1.total > 0 && c1.n[int(Structure::Tree)] > 0,
                    "fixture: клетка с макро-счётом деревьев рождает деревья");

    // (1) КЛЕТКА ЕСТЬ ФУНКЦИЯ СВОЕЙ ИДЕНТИЧНОСТИ — колонка в колонку.
    int compared = 0, differing = 0;
    const std::size_t common = std::min(first.structures.size(),
                                        again.structures.size());
    for (std::size_t i = 0; i < common; ++i) {
        ++compared;
        if (!same_prop(first.structures[i], again.structures[i])) ++differing;
    }
    CHECK(first.structures.size() == again.structures.size()
              && compared == int(first.structures.size()) && differing == 0,
          "два прогона одной идентичности дают ОДИН и тот же список пропов — "
          "хранить его не нужно");
    int kindsCompared = 0, kindsDiffering = 0;
    for (int k = 0; k < kKindRows; ++k) {
        ++kindsCompared;
        if (c1.n[k] != c2.n[k]) ++kindsDiffering;
    }
    CHECK(kindsCompared == Structure::kKindCount && c1.total == c2.total
              && kindsDiffering == 0,
          "перепись по родам у двух прогонов одной идентичности совпадает");

    // (2) ПАМЯТЬ О РУБКЕ — ЧИСЛО, и оно касается ТОЛЬКО деревьев.
    SubworldMapData thinned{};
    generate_with_trees(ctx, thin, thinned);
    const KindCensus ct = census(thinned.structures);
    CHECK(ct.n[int(Structure::Tree)] > 0
              && ct.n[int(Structure::Tree)] < c1.n[int(Structure::Tree)],
          "меньше макро-счёт — строго меньше деревьев в клетке");
    int otherKinds = 0, otherChanged = 0;
    for (int k = 0; k < kKindRows; ++k) {
        if (k == int(Structure::Tree)) continue;
        ++otherKinds;
        if (ct.n[k] != c1.n[k]) ++otherChanged;
    }
    CHECK(otherKinds == Structure::kKindCount - 1 && otherChanged == 0,
          "счёт деревьев двигает ТОЛЬКО деревья — детектор видит дыру "
          "древесной формы, а не сломанную клетку");

    // (3) ПОРЯДОК ПРОРЕЖИВАНИЯ — ЧИСЛА, а НЕ ИГРОКА: выжившие стволы
    // стоят там, где стояли в богатой клетке (гейт плотности монотонен по
    // счёту), поэтому «пень на месте срубленного» миру выразить нечем.
    std::vector<std::pair<float, float>> richTrees;
    for (const Structure& s : first.structures) {
        if (s.kind == Structure::Tree) richTrees.push_back({s.x, s.y});
    }
    std::sort(richTrees.begin(), richTrees.end());
    int thinTrees = 0, strangers = 0;
    for (const Structure& s : thinned.structures) {
        if (s.kind != Structure::Tree) continue;
        ++thinTrees;
        if (!std::binary_search(richTrees.begin(), richTrees.end(),
                                std::pair<float, float>{s.x, s.y})) {
            ++strangers;
        }
    }
    CHECK(thinTrees == ct.n[int(Structure::Tree)] && strangers == 0,
          "прореженная клетка — ПОДМНОЖЕСТВО богатой по положению стволов");

    // (4) ОВЕРЛЕЯ ВЕТХОСТИ НЕТ: отрицательную высоту писало слияние
    // снимка, и с его сносом у знака не осталось ни одного писателя.
    int heightsSeen = 0, negative = 0;
    for (const SubworldMapData* m : {&first, &thinned}) {
        for (const Structure& s : m->structures) {
            ++heightsSeen;
            if (s.height < 0.0f) ++negative;
        }
    }
    CHECK(heightsSeen == int(first.structures.size() + thinned.structures.size())
              && negative == 0,
          "высота пропа неотрицательна — оверлея ветхости больше нет");
}

} // namespace

int main() {
    test_table_consistency();
    test_interior_props();

    // ONE city generation for the door/lantern census — the cell is 1024².
    const CellContext city = make_city_ctx(0xC17A5EEDu);
    SubworldMapData visited{};
    generate(city, visited);
    test_city_props(visited);
    test_revisit_is_regeneration();

    return sm::test::report("prop_interaction_test");
}

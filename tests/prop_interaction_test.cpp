// Locks the UNIVERSAL PROP / INTERACTION system: the kStructureKindRows
// columns that turned "a prop" into "a thing the world can draw, light and
// press E on" (sub/map_data.h), the two generators that place the first such
// props (sub/gens/dispatch.cpp city doors + street lanterns, sub/dgn/house.cpp
// interior door + stair blocks), and the snapshot merge that has to carry ALL
// of them across a revisit (sub/map_factory.cpp restore_into).
//
// What is promised and asserted here:
//   1. TABLE CONSISTENCY — pure, no generation. Every kind that declares an
//      interaction has a usable InteractRow behind it (non-empty verb, a reach
//      you can actually stand inside); every lit kind carries a colour and
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
//      the ground storey, inside the Door row's own reach of the shared
//      dungeon_entry_point; and every shaft this storey carries stands a
//      Stairs block, tagged 1 up / 0 down, exactly on the shared
//      dungeon_stair_point. The cellar is a seeded coin, so the test takes
//      whichever branch dungeon_has_cellar names and ASSERTS that it took one.
//   5. SNAPSHOT ROUND TRIP — the regression this bought. restore_into used to
//      diff over a hardcoded 5 kinds (Tree/Rock/House/Wall/Bridge), so every
//      prop past Bridge — crops, fences, furniture, and now doors and
//      lanterns — was silently dropped from a revisited cell. Asserted for
//      EVERY kind in the table, with a negative control proving the census
//      can see a kind go missing.
#include "check.h"
#include "sub/dgn/dispatch.h"
#include "sub/gens/dispatch.h"
#include "sub/map_data.h"
#include "sub/map_factory.h"

#include <cmath>
#include <cstdint>
#include <vector>

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
    dispatch_generate(ctx, nbH, nbB, nbF, out);
}

CellContext make_city_ctx(std::uint32_t seed) {
    CellContext ctx{};
    ctx.cx = 5;
    ctx.cy = 9;
    ctx.macroHeight = 0.64f;
    ctx.biome = Meadow;
    ctx.feature = FT_None;
    ctx.landmark.id = 101;
    ctx.landmark.size = 2000;
    ctx.landmark.kind = LandmarkType::City;
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
    ctx.landmark.id = -1;
    ctx.landmark.size = 0;
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

        // An interactive kind must have a verb to print and a reach to stand
        // inside — an interaction with reach 0 is a prompt nobody can trigger.
        const InteractId id = structure_interact(kind);
        if (id != InteractId::None) {
            ++interactive;
            const InteractRow& ir = interact_row(id);
            if (!(ir.reachTiles > 0.0f) || empty_str(ir.verb)) ++badInteract;
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
          "every interactive kind has a verb and a positive reach");
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
            if (!empty_str(ir.verb) || ir.reachTiles != 0.0f) ++missingVerb;
        } else if (empty_str(ir.verb)) {
            ++missingVerb;
        }
    }
    CHECK(ids == int(InteractId::Count) && missingVerb == 0,
          "every InteractId but None names a verb; None is silent");

    // The two payload contracts the generators stamp and the engine reads.
    CHECK(structure_interact(Structure::Door) == InteractId::Door,
          "contract: a Door prop dispatches the Door interaction");
    CHECK(structure_interact(Structure::Stairs) == InteractId::Stairs,
          "contract: a Stairs prop dispatches the Stairs interaction");
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
    const float reach = interact_row(InteractId::Door).reachTiles;
    CHECK(structure_surface_dist2(*door, px, py) <= reach * reach,
          "the exit door stands within its own row's reach of the entry pad");

    // Shafts. The NW shaft climbs and exists whenever the room is big enough
    // to be worth a storey; the NE shaft descends on a seeded coin.
    CHECK_OR_RETURN(dungeon_has_upper(ctx.dungeon),
                    "fixture: this footprint's room seats an upper storey");
    float upX = 0.0f, upY = 0.0f, dnX = 0.0f, dnY = 0.0f;
    dungeon_stair_point(ctx.dungeon, /*up*/true,  upX, upY);
    dungeon_stair_point(ctx.dungeon, /*up*/false, dnX, dnY);

    int upStairs = 0, downStairs = 0, offShaft = 0, stairsSeen = 0;
    for (const Structure& s : out.structures) {
        if (s.kind != Structure::Stairs) continue;
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

// ── 5. Snapshot round trip + the negative control ──────────────────────────
void test_snapshot_round_trip(const CellContext& ctx,
                              const SubworldMapData& visited) {
    const SubworldMode mode = resolve_mode(ctx);
    CHECK(mode == SubworldMode::City,
          "fixture: the settlement context resolves to a City subworld");
    const SavedSubworld saved = snapshot_subworld(ctx.seed, mode, visited);

    // Walk out and back in: the cell is generated again from scratch, then the
    // snapshot is merged into it. Nothing was changed underground, so every
    // kind must survive at exactly its fresh count.
    SubworldMapData fresh{};
    generate(ctx, fresh);
    const KindCensus before = census(fresh.structures);
    CHECK_OR_RETURN(before.total > 0,
                    "fixture: the revisited cell generates props at all");
    restore_into(saved, fresh);
    const KindCensus after = census(fresh.structures);

    int kindsChecked = 0, dropped = 0, kindsPresent = 0, kindsPastBridge = 0;
    for (int k = 0; k < kKindRows; ++k) {
        ++kindsChecked;
        if (after.n[k] != before.n[k]) ++dropped;
        if (before.n[k] > 0) {
            ++kindsPresent;
            if (k > int(Structure::Bridge)) ++kindsPastBridge;
        }
    }
    CHECK(kindsChecked == Structure::kKindCount && dropped == 0,
          "a revisit restores every prop kind at its full count");
    CHECK(kindsPresent > 0 && after.total == before.total,
          "the revisited cell keeps exactly as many props as it generated");
    // The bug this bought: the merge used to stop at Bridge, so everything
    // past it vanished on a revisit. The city must actually carry such props,
    // or the assertion above would be measuring an empty range.
    CHECK(kindsPastBridge > 0,
          "the city carries props past Bridge — the range the old literal-5 "
          "merge dropped");
    CHECK(after.n[int(Structure::Door)] > 0
              && after.n[int(Structure::Lantern)] > 0,
          "doors and lanterns both survive the revisit");

    // The per-instance payload has to survive too — a restored door whose tag
    // was lost opens the wrong interior.
    const int houses = after.n[int(Structure::House)];
    std::vector<int> tagged(std::size_t(houses > 0 ? houses : 1), 0);
    int restoredDoors = 0, badTag = 0;
    for (const Structure& s : fresh.structures) {
        if (s.kind != Structure::Door) continue;
        ++restoredDoors;
        if (int(s.tag) < houses) ++tagged[std::size_t(s.tag)];
        else ++badTag;
    }
    int ordinals = 0, unpaired = 0;
    for (int i = 0; i < houses; ++i) {
        ++ordinals;
        if (tagged[std::size_t(i)] != 1) ++unpaired;
    }
    CHECK(restoredDoors == houses && badTag == 0,
          "the revisit restores one door per house");
    CHECK(ordinals == houses && unpaired == 0,
          "restored door tags are still exactly {0..houses-1}");

    // NEGATIVE CONTROL for the census above. The merge keeps, per kind, the
    // larger of the two sides (saved positions win, surplus fresh props are
    // appended, surplus saved props are marked decayed), so a kind can only
    // go missing if it is absent from BOTH sides — which is precisely what the
    // old kind-truncated loop did to every prop past Bridge: neither the saved
    // nor the fresh entries were ever visited. Reproduce that state and the
    // SAME census must report the loss.
    SavedSubworld strippedSave = saved;
    SubworldMapData strippedFresh{};
    generate(ctx, strippedFresh);
    int removedSaved = 0, removedFresh = 0;
    std::vector<Structure> keepSaved;
    for (const Structure& s : strippedSave.structures) {
        if (s.kind == Structure::Door) { ++removedSaved; continue; }
        keepSaved.push_back(s);
    }
    strippedSave.structures = keepSaved;
    std::vector<Structure> keepFresh;
    for (const Structure& s : strippedFresh.structures) {
        if (s.kind == Structure::Door) { ++removedFresh; continue; }
        keepFresh.push_back(s);
    }
    strippedFresh.structures = keepFresh;
    CHECK(removedSaved > 0 && removedFresh > 0,
          "fixture: there were doors on both sides to strip");
    restore_into(strippedSave, strippedFresh);
    const KindCensus lossy = census(strippedFresh.structures);
    CHECK(lossy.total > 0,
          "fixture: the stripped map still carries its other props");
    CHECK(lossy.n[int(Structure::Door)] < before.n[int(Structure::Door)],
          "with the Door kind lost by the merge the census MUST report the "
          "drop — the detector can see a missing kind");
    // …and it sees the loss ONLY there: the kinds the merge still visits are
    // untouched, so the control is a door-shaped hole, not a broken map.
    int otherKinds = 0, otherChanged = 0;
    for (int k = 0; k < kKindRows; ++k) {
        if (k == int(Structure::Door)) continue;
        ++otherKinds;
        if (lossy.n[k] != before.n[k]) ++otherChanged;
    }
    CHECK(otherKinds == Structure::kKindCount - 1 && otherChanged == 0,
          "the control loses doors and nothing else");
}

} // namespace

int main() {
    test_table_consistency();
    test_interior_props();

    // ONE city generation shared by the door/lantern census and the snapshot
    // round trip — the cell is 1024² and the round trip generates two more.
    const CellContext city = make_city_ctx(0xC17A5EEDu);
    SubworldMapData visited{};
    generate(city, visited);
    test_city_props(visited);
    test_snapshot_round_trip(city, visited);

    return sm::test::report("prop_interaction_test");
}

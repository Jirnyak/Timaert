// Spire placement (macro/spires.cpp). Pinned promises:
//   · one spire per learnable spell, id = list index, spellId = the spell's
//     registration ordinal, born un-depleted;
//   · a spire stands on land, inside the landmark table's own zone band
//     (landmark_def(Spire).minZone), and the top-tier spell's spire stands at
//     the band's CAP — the doom spell lives where the world is at its worst;
//   · no spire shares a cell with any named place (a co-located spire would
//     be shadowed by resolve_context's scan order), and the pick spreads:
//     best-candidate sampling never stacks two spires side by side;
//   · placement is a fact of (world seed, spell list): same seed reproduces
//     the sites, another seed moves them;
//   · a world with no admissible ground places NOTHING (negative control for
//     the zone gate and the occupancy veto both).
#include "check.h"

#include "macro/landmark_registry.h"
#include "macro/map_generator.h"
#include "macro/spires.h"
#include "macro/state.h"
#include "macro/zones.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>

namespace {

using namespace sm;

constexpr int kW = 64, kH = 64;
// Heights: land 128, sea floor 0; the gate between them mirrors the default
// map's 0.4 sea level (102/255).
constexpr std::uint8_t kSea8 = 102;

// Land everywhere except a water strip at x < 8 (so "on land" is a real
// constraint, not a tautology of the fixture).
TerrainData banded_terrain() {
    TerrainData t;
    t.width = kW;
    t.height = kH;
    t.rgba.assign(std::size_t(kW * kH) * 4u, 128);
    for (int y = 0; y < kH; ++y) {
        for (int x = 0; x < 8; ++x) {
            const std::size_t i = (std::size_t(y) * kW + std::size_t(x)) * 4u;
            t.rgba[i + 0] = 0;   // below sea
            t.rgba[i + 3] = 0;   // water mask
        }
    }
    return t;
}

// Zones rise west to east in bands: zone(x) = x * 10 / kW, so every zone
// 0..9 exists and the wild half (>= 5) is the eastern half of the map.
ZoneLayer banded_zones() {
    ZoneLayer z;
    z.width = kW;
    z.height = kH;
    z.data.assign(std::size_t(kW * kH), 0);
    for (int y = 0; y < kH; ++y)
        for (int x = 0; x < kW; ++x)
            z.data[std::size_t(y) * kW + std::size_t(x)] =
                std::uint8_t(x * kZoneCount / kW);
    return z;
}

GameState world(std::uint32_t seed) {
    GameState gs;
    gs.worldSeed = seed;
    gs.mapW = kW;
    gs.mapH = kH;
    return gs;
}

// The full tier ladder, one spell per tier (ordinals deliberately not 0-based
// consecutive from tier, so spellId == ordinal is a real assertion).
constexpr std::array<SpireSpellSpec, 5> kLadder{{
    {7, 1}, {3, 2}, {11, 3}, {0, 4}, {5, 5},
}};

int torus_cheb(int ax, int ay, int bx, int by) {
    int dx = std::abs(ax - bx);
    dx = std::min(dx, kW - dx);
    int dy = std::abs(ay - by);
    dy = std::min(dy, kH - dy);
    return std::max(dx, dy);
}

void test_one_spire_per_spell_in_the_band() {
    GameState gs = world(12345u);
    const TerrainData terrain = banded_terrain();
    const ZoneLayer zones = banded_zones();
    generate_spires(gs, zones, terrain, kSea8,
                    std::span<const SpireSpellSpec>(kLadder));

    const LandmarkDef& def = landmark_def(LandmarkType::Spire);
    CHECK_OR_RETURN(gs.spires.size() == kLadder.size(),
                    "every learnable spell got its spire");
    for (std::size_t i = 0; i < gs.spires.size(); ++i) {
        const Spire& sp = gs.spires[i];
        CHECK(sp.id == int(i), "spire id is its list index");
        CHECK(sp.spellId == kLadder[i].spellOrdinal,
              "the spire carries its spell's registration ordinal");
        CHECK(!sp.depleted, "a fresh spire holds its spell");
        CHECK(!terrain.is_water(sp.x, sp.y, kSea8), "a spire stands on land");
        CHECK(int(zones.at(sp.x, sp.y)) >= int(def.minZone),
              "a spire stands inside the landmark table's zone band");
    }
    // The top-tier spell demands the band's cap (the table's maxZone), and
    // this world has free zone-9 ground, so no relaxation may kick in.
    CHECK(int(zones.at(gs.spires.back().x, gs.spires.back().y))
              == int(def.maxZone),
          "the top-tier spire stands at the band's cap");
    // Best-candidate spread: with the whole wild half free, spires never end
    // up stacked or adjacent (the veto alone only forbids the same cell).
    int minPair = kW + kH;
    for (std::size_t a = 0; a < gs.spires.size(); ++a)
        for (std::size_t b = a + 1; b < gs.spires.size(); ++b)
            minPair = std::min(minPair,
                               torus_cheb(gs.spires[a].x, gs.spires[a].y,
                                          gs.spires[b].x, gs.spires[b].y));
    CHECK(minPair >= 2, "spires spread - no two side by side");
}

void test_placement_is_a_fact_of_the_seed() {
    const TerrainData terrain = banded_terrain();
    const ZoneLayer zones = banded_zones();
    GameState a = world(12345u), b = world(12345u), c = world(777u);
    generate_spires(a, zones, terrain, kSea8,
                    std::span<const SpireSpellSpec>(kLadder));
    generate_spires(b, zones, terrain, kSea8,
                    std::span<const SpireSpellSpec>(kLadder));
    generate_spires(c, zones, terrain, kSea8,
                    std::span<const SpireSpellSpec>(kLadder));

    CHECK_OR_RETURN(a.spires.size() == b.spires.size()
                        && a.spires.size() == kLadder.size(),
                    "both same-seed runs placed the full ladder");
    bool identical = true;
    for (std::size_t i = 0; i < a.spires.size(); ++i)
        identical = identical && a.spires[i].x == b.spires[i].x
                              && a.spires[i].y == b.spires[i].y;
    CHECK(identical, "same seed reproduces the same sites");

    bool moved = c.spires.size() != a.spires.size();
    for (std::size_t i = 0; !moved && i < a.spires.size(); ++i)
        moved = a.spires[i].x != c.spires[i].x
             || a.spires[i].y != c.spires[i].y;
    CHECK(moved, "another seed is another world - some spire moved");
}

void test_no_admissible_ground_places_nothing() {
    const TerrainData terrain = banded_terrain();
    // Negative control 1: a fully tame world (all zones 0) offers no site.
    {
        GameState gs = world(12345u);
        ZoneLayer tame;
        tame.width = kW;
        tame.height = kH;
        tame.data.assign(std::size_t(kW * kH), 0);
        generate_spires(gs, tame, terrain, kSea8,
                        std::span<const SpireSpellSpec>(kLadder));
        CHECK(gs.spires.empty(), "no wild land = no spires");
    }
    // Negative control 2: an all-ocean world offers no site either.
    {
        GameState gs = world(12345u);
        TerrainData ocean;
        ocean.width = kW;
        ocean.height = kH;
        ocean.rgba.assign(std::size_t(kW * kH) * 4u, 0);
        generate_spires(gs, banded_zones(), ocean, kSea8,
                        std::span<const SpireSpellSpec>(kLadder));
        CHECK(gs.spires.empty(), "no land = no spires");
    }
}

void test_named_places_veto_their_cells() {
    // The only wild ground is a 16×16 block (zone 9 at x,y in [32,48)); the
    // rest of the world is tame. With the block free the spire lands inside
    // it; with a village on EVERY block cell the spire has nowhere to go —
    // the occupancy veto is real. (A block, not a single cell: placement is
    // candidate sampling, and one cell in 4096 is a needle the sampler is not
    // promised to find.)
    const TerrainData terrain = banded_terrain();
    ZoneLayer pin;
    pin.width = kW;
    pin.height = kH;
    pin.data.assign(std::size_t(kW * kH), 0);
    for (int y = 32; y < 48; ++y)
        for (int x = 32; x < 48; ++x)
            pin.data[std::size_t(y) * kW + std::size_t(x)] = 9;
    const std::array<SpireSpellSpec, 1> one{{{0, 5}}};

    {
        GameState gs = world(12345u);
        generate_spires(gs, pin, terrain, kSea8,
                        std::span<const SpireSpellSpec>(one));
        CHECK_OR_RETURN(gs.spires.size() == 1,
                        "the wild block hosts the spire");
        CHECK(gs.spires[0].x >= 32 && gs.spires[0].x < 48
                  && gs.spires[0].y >= 32 && gs.spires[0].y < 48,
              "the spire stands inside the only admissible ground");
    }
    {
        GameState gs = world(12345u);
        for (int y = 32; y < 48; ++y)
            for (int x = 32; x < 48; ++x) {
                Village v{};
                v.x = x;
                v.y = y;
                gs.villages.push_back(v);
            }
        generate_spires(gs, pin, terrain, kSea8,
                        std::span<const SpireSpellSpec>(one));
        CHECK(gs.spires.empty(),
              "named places on every admissible cell veto the spire");
    }
}

} // namespace

int main() {
    test_one_spire_per_spell_in_the_band();
    test_placement_is_a_fact_of_the_seed();
    test_no_admissible_ground_places_nothing();
    test_named_places_veto_their_cells();
    return sm::test::report("spire_generation_test");
}

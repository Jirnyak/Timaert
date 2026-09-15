// THE REACH FIELD IS A PURE FUNCTION OF THE VEINS — every door, every time.
//
// "Is a live vein of this kind within a gatherer's reach of this cell" used to
// be answered by scanning the whole vein list, which was correct and cost
// 161 µs per call — 4.3 ms of a 6.8 ms seam crossing, on the sacred seam, for
// one byte (problems.md §52). It is a stamped field now: geology writes a disc
// when a vein is born and erases one when it runs dry, and the question is an
// array read.
//
// That trade buys speed with a DUTY: the field is only true while every door
// that moves a vein also moves its disc. A door that forgets does not crash
// and does not fail visibly — it grants a village a trade whose ore left the
// world, or withholds one whose ore arrived. So this file does not test the
// stamping; it tests the INVARIANT the stamping exists to hold:
//
//     reach[k][cell] != 0   ⟺   some live vein of kind k lies within R
//
// The right side is the QUESTION, spelled exactly as cell_facts used to ask it
// (torus_dist_sq <= R², the circle). That is not a second copy of the
// implementation — the implementation stamps discs outward, this reads the
// world inward — it is the specification the implementation answers to. Every
// door in deposit_layer.h is driven and the invariant re-checked after it.
#include "check.h"

#include "core/torus.h"
#include "macro/deposit_layer.h"
#include "macro/resource_field.h"

#include <cstdint>

namespace {

using namespace sm;

// THE QUESTION, brute force: does any live vein of this kind stand within a
// gatherer's reach of this cell? This is the code the field replaced.
bool vein_within_reach(const DepositLayer& layer, DepositKind kind,
                       int x, int y) {
    bool found = false;
    layer.cells[std::size_t(kind)].for_each_live(
            [&](std::uint32_t idx, std::int32_t remaining) {
        (void)remaining;                       // every live cell holds units
        if (found) return;
        const int vx = int(idx % std::uint32_t(layer.width));
        const int vy = int(idx / std::uint32_t(layer.width));
        const float d2 = torus_dist_sq(float(vx), float(vy),
                                       float(x), float(y),
                                       float(layer.width),
                                       float(layer.height));
        if (d2 <= float(kGathererReach) * float(kGathererReach)) found = true;
    });
    return found;
}

// Sweep the whole map, every kind. Returns the number of cells where the field
// and the question disagree, and reports how many it actually looked at — a
// sweep that measured nothing must never read as agreement.
int disagreements(const DepositLayer& layer, int& examined) {
    int bad = 0;
    examined = 0;
    for (std::size_t k = 0; k < std::size_t(kDepositKindCount); ++k) {
        for (int y = 0; y < layer.height; ++y) {
            for (int x = 0; x < layer.width; ++x) {
                const bool field = layer.kind_near(DepositKind(k), x, y);
                const bool truth = vein_within_reach(layer, DepositKind(k), x, y);
                if (field != truth) ++bad;
                ++examined;
            }
        }
    }
    return bad;
}

// The same little world deposit_layer_test builds: sea at the left edge, a
// river column wetting the land, a tall mountain band where metal concentrates.
TerrainData make_world() {
    const int w = 64, h = 64;
    TerrainData td;
    td.width = w;
    td.height = h;
    td.rgba.assign(std::size_t(w) * h * 4u, 0);
    td.riverData.assign(std::size_t(w) * h, 0);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const std::size_t s = std::size_t(y * w + x) * 4u;
            std::uint8_t height = 140;
            if (x < 4) height = 40;
            if (y >= 48) height = 250;
            td.rgba[s + 0] = height;
            td.rgba[s + 1] = 200;
            td.rgba[s + 2] = 128;
            td.rgba[s + 3] = height < 102 ? 0 : 255;
            if (x == 8 && y < 48) td.riverData[y * w + x] = 255;
        }
    }
    return td;
}

// The first live cell of a kind, as a coordinate pair.
bool first_vein(const DepositLayer& layer, DepositKind kind, int& x, int& y) {
    const ResourceGrid& g = layer.cells[std::size_t(kind)];
    const std::uint32_t idx = g.first_live();
    if (idx == ResourceGrid::kNoCell) return false;
    x = g.x_of(idx);
    y = g.y_of(idx);
    return true;
}

} // namespace

int main() {
    using namespace sm::test;

    DepositLayer layer = build_deposit_layer(make_world(), 0x51A2B3C4u, 0.40f);

    // ── 0. THE FIXTURE HAS GEOLOGY, AND THE SWEEP CAN SEE BOTH ANSWERS ────
    // Without this the whole file could pass on an empty world by agreeing
    // that nothing is near anything (AGENTS testing law 2/3).
    {
        int live = 0;
        for (const auto& g : layer.cells) live += int(g.liveCells);
        CHECK(live > 0, "the fixture world has veins at all");
        int near = 0, far = 0;
        for (int y = 0; y < layer.height; ++y)
            for (int x = 0; x < layer.width; ++x) {
                if (layer.kind_near(DepositKind::Stone, x, y)) ++near;
                else ++far;
            }
        CHECK(near > 0 && far > 0,
              "the field says YES somewhere and NO somewhere — it discriminates");
    }

    // ── 1. BORN WITH THE WORLD ────────────────────────────────────────────
    {
        int examined = 0;
        const int bad = disagreements(layer, examined);
        CHECK(examined == layer.width * layer.height * kDepositKindCount,
              "the sweep examined every cell of every kind");
        CHECK(bad == 0, "a freshly built world's reach field IS the question");
    }

    // ── 2. A VEIN RUNS DRY ────────────────────────────────────────────────
    // The door that annihilates must take the disc with it. This is the case
    // that fails SILENTLY when forgotten: the ore is gone and the trade stays.
    {
        int vx = 0, vy = 0;
        CHECK(first_vein(layer, DepositKind::Iron, vx, vy),
              "the fixture has an iron vein to work out");
        const bool ok = set_deposit_remaining(layer, DepositKind::Iron,
                                              vx, vy, 0);
        CHECK(ok, "the quantity door accepted the write");
        int examined = 0;
        CHECK(disagreements(layer, examined) == 0,
              "a worked-out vein takes its reach with it");
    }

    // ── 3. GENESIS, AND A REFILL THAT IS NOT A BIRTH ──────────────────────
    // The counting failure mode: refilling a standing vein must not stamp a
    // second disc, or the cell survives its own death and grants forever.
    {
        create_deposit(layer, DepositKind::Silver, 20, 52, 500);
        int examined = 0;
        CHECK(disagreements(layer, examined) == 0,
              "a discovered vein brings its reach with it");
        create_deposit(layer, DepositKind::Silver, 20, 52, 900);   // refill
        CHECK(set_deposit_remaining(layer, DepositKind::Silver, 20, 52, 0),
              "the refilled vein is workable");
        CHECK(disagreements(layer, examined) == 0,
              "a refill is not a second birth — one death still ends it");
    }

    // ── 4. A MINE SWALLOWS ITS CLUSTER ────────────────────────────────────
    // consolidate_deposit_cluster erases the absorbed veins directly, which
    // is a third way for the field to drift away from the truth.
    {
        int vx = 0, vy = 0;
        if (first_vein(layer, DepositKind::Stone, vx, vy)) {
            consolidate_deposit_cluster(layer, DepositKind::Stone, vx, vy);
            int examined = 0;
            CHECK(disagreements(layer, examined) == 0,
                  "the veins a mine absorbs leave the reach field too");
        }
    }

    // ── 5. AND ACROSS A LOAD ──────────────────────────────────────────────
    // The field is DERIVED and never saved, so the load path owes it a
    // re-stamp. A world restored without one answers every question NO.
    {
        DepositLayer fresh = build_deposit_layer(make_world(), 0x51A2B3C4u, 0.40f);
        restore_deposit_cells(fresh, layer);
        int examined = 0;
        CHECK(disagreements(fresh, examined) == 0,
              "a loaded world's reach field is re-derived, not restored");
        // ...and it is the LOADED geology it describes, not the fixture's.
        int live = 0;
        for (const auto& g : fresh.cells) live += int(g.liveCells);
        int liveSrc = 0;
        for (const auto& g : layer.cells) liveSrc += int(g.liveCells);
        CHECK(live == liveSrc && liveSrc > 0,
              "the load really did carry the mutated geology across");
    }

    return report("deposit_reach_test");
}

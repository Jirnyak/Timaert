// THE REACH FIELD IS A PURE FUNCTION OF THE VEIN FIELD — every door, every
// time.
//
// "Is a live vein within a gatherer's reach of this cell" used to be answered
// by scanning the whole vein list, which was correct and cost 161 µs per call
// — 4.3 ms of a 6.8 ms seam crossing, on the sacred seam, for one byte
// (problems.md §52). It is a stamped field now: a vein's write stamps a disc,
// its death erases one, and the question is an array read.
//
// That trade buys speed with a DUTY: the field is only true while every door
// that moves a vein also moves its disc. So this file does not test the
// stamping; it tests the INVARIANT the stamping exists to hold:
//
//     reach[cell] != 0   ⟺   some live vein lies within R
//
// The right side is the QUESTION, spelled exactly as cell_facts used to ask
// it (torus_dist_sq <= R², the circle) — the specification the stamp answers
// to, never a second copy of it.
//
// (ЖИЛЫ ЕДИНЫЕ, 2026-10-22: рода у поля нет, грид один. Приборы «quantity
// door», «consolidation» и «load path» умерли со своими дверьми: природное
// поле геймплеем не мутируется и в сейв не едет; рождение/долив остались
// одной дверью генезиса create_deposit.)
#include "check.h"

#include "core/torus.h"
#include "macro/deposit_layer.h"
#include "macro/resource_field.h"

#include <cstdint>

namespace {

using namespace sm;

// THE QUESTION, brute force: does any live vein stand within a gatherer's
// reach of this cell? This is the code the field replaced.
bool vein_within_reach(const DepositLayer& layer, int x, int y) {
    bool found = false;
    layer.cells.for_each_live([&](std::uint32_t idx, std::int32_t remaining) {
        (void)remaining;                       // every live cell holds units
        if (found) return;
        const int vx = layer.cells.x_of(idx);
        const int vy = layer.cells.y_of(idx);
        const float d2 = torus_dist_sq(float(vx), float(vy),
                                       float(x), float(y),
                                       float(layer.width),
                                       float(layer.height));
        if (d2 <= float(kGathererReach) * float(kGathererReach)) found = true;
    });
    return found;
}

// Sweep the whole map. Returns the number of cells where the field and the
// question disagree, and reports how many it actually looked at — a sweep
// that measured nothing must never read as agreement.
int disagreements(const DepositLayer& layer, int& examined) {
    int bad = 0;
    examined = 0;
    for (int y = 0; y < layer.height; ++y) {
        for (int x = 0; x < layer.width; ++x) {
            const bool field = layer.vein_near(x, y);
            const bool truth = vein_within_reach(layer, x, y);
            if (field != truth) ++bad;
            ++examined;
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
    // плоскость моря — у карты
    td.seaLevel16 = sm::field_word_of(sm::kDefaultSeaLevel);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const std::size_t s = std::size_t(y * w + x) * 4u;
            float level01 = 140.0f / 255.0f;
            if (x < 4) level01 = 40.0f / 255.0f;
            if (y >= 48) level01 = 250.0f / 255.0f;
            td.rgba[s + 0] = sm::field_word_of(level01);
            td.rgba[s + 1] = sm::field_word_of(200.0f / 255.0f);
            td.rgba[s + 2] = sm::field_word_of(128.0f / 255.0f);
            td.rgba[s + 3] = level01 < sm::kDefaultSeaLevel
                                 ? std::uint16_t(0)
                                 : std::uint16_t(sm::kFieldWordMax);
            if (x == 8 && y < 48) {
                td.rgba[s + 0] = sm::river_bed_word(td.seaLevel16);
                td.rgba[s + 3] = std::uint16_t(0);
            }
        }
    }
    return td;
}

} // namespace

int main() {
    using namespace sm::test;

    DepositLayer layer = build_deposit_layer(make_world(), 0x51A2B3C4u);

    // ── 0. THE FIXTURE HAS GEOLOGY, AND THE SWEEP CAN SEE BOTH ANSWERS ────
    // Without this the whole file could pass on an empty world by agreeing
    // that nothing is near anything (AGENTS testing law 2/3).
    {
        CHECK(layer.cells.liveCells > 0, "the fixture world has veins at all");
        int near = 0, far = 0;
        for (int y = 0; y < layer.height; ++y)
            for (int x = 0; x < layer.width; ++x) {
                if (layer.vein_near(x, y)) ++near;
                else ++far;
            }
        CHECK(near > 0 && far > 0,
              "the field says YES somewhere and NO somewhere — it discriminates");
    }

    // ── 1. BORN WITH THE WORLD ────────────────────────────────────────────
    {
        int examined = 0;
        const int bad = disagreements(layer, examined);
        CHECK(examined == layer.width * layer.height,
              "the sweep examined every cell");
        CHECK(bad == 0, "a freshly built world's reach field IS the question");
    }

    // ── 2. GENESIS, AND A REFILL THAT IS NOT A BIRTH ──────────────────────
    // The counting failure mode: refilling a standing vein must not stamp a
    // second disc — the reach count would survive the cell's own emptiness
    // and grant forever.
    {
        create_deposit(layer, 20, 52, 500);
        int examined = 0;
        CHECK(disagreements(layer, examined) == 0,
              "a discovered vein brings its reach with it");
        create_deposit(layer, 20, 52, 900);   // refill, not a second birth
        // Смерть клетки — дверью абсолютной установки (стена ломтя 1 закрыла
        // сырой write; в живом мире природное поле не умирает вовсе, но
        // структура ResourceGrid обязана держать счёт дисков и на нуле —
        // этим же write живут шрамы и worked).
        field_set(layer.cells, layer.wrap_index(20, 52), 0);
        CHECK(disagreements(layer, examined) == 0,
              "a refill is not a second birth — one death still ends it");
    }

    return report("deposit_reach_test");
}

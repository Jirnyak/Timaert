#include "check.h"
#include "sub/gens/dispatch.h"
#include "sub/dgn/dispatch.h"   // the tower's shared dimensions
#include "macro/tree_layer.h"
#include "sub/base_generator.h"
#include "sub/collide.h"   // kStepUpM — the arch must be WALKED
#include "sub/height.h"
#include "sub/material.h"
#include "core/rng.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <vector>

namespace {

struct Glade {
    int x;
    int y;
    int r;
};

float noise01(int x, int y, std::uint32_t seed) {
    return float(sm::hash3(std::uint32_t(x), std::uint32_t(y), seed))
        / 4294967295.0f;
}

float smooth_noise01(float x, float y, std::uint32_t seed) {
    const int ix = int(std::floor(x));
    const int iy = int(std::floor(y));
    const float fx = x - float(ix);
    const float fy = y - float(iy);
    const float sx = fx * fx * (3.0f - 2.0f * fx);
    const float sy = fy * fy * (3.0f - 2.0f * fy);
    const float n00 = noise01(ix,     iy,     seed);
    const float n10 = noise01(ix + 1, iy,     seed);
    const float n01 = noise01(ix,     iy + 1, seed);
    const float n11 = noise01(ix + 1, iy + 1, seed);
    return n00 * (1.0f - sx) * (1.0f - sy)
         + n10 * sx * (1.0f - sy)
         + n01 * (1.0f - sx) * sy
         + n11 * sx * sy;
}

std::vector<Glade> expected_forest_glades(const sm::sub::CellContext& ctx) {
    constexpr int kGladeStep = 48;
    constexpr float kGladeThreshold = 0.72f;
    constexpr int kGladeRadiusMin = 6;
    constexpr int kGladeRadiusMax = 16;

    const int gox = ctx.cx * sm::sub::kCellSize;
    const int goy = ctx.cy * sm::sub::kCellSize;
    const int startGX = gox + ((kGladeStep - (gox % kGladeStep)) % kGladeStep);
    const int startGY = goy + ((kGladeStep - (goy % kGladeStep)) % kGladeStep);
    const int endGX = gox + sm::sub::kCellSize;
    const int endGY = goy + sm::sub::kCellSize;

    std::vector<Glade> out;
    out.reserve(64);
    for (int gy = startGY; gy < endGY; gy += kGladeStep) {
        for (int gx = startGX; gx < endGX; gx += kGladeStep) {
            const float n = smooth_noise01(float(gx) * 0.007f + 777.0f,
                                           float(gy) * 0.007f + 888.0f,
                                           ctx.seed);
            if (n < kGladeThreshold) continue;
            const float rn = noise01(gx * 5 + 99999, gy * 7 + 88888, ctx.seed);
            const int radius = kGladeRadiusMin
                + int(std::floor(rn * float(kGladeRadiusMax - kGladeRadiusMin + 1)));
            out.push_back({gx - gox, gy - goy, radius});
        }
    }
    return out;
}

bool heightmaps_nearly_equal(const std::vector<float>& a,
                             const std::vector<float>& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::fabs(a[i] - b[i]) > 0.00001f) return false;
    }
    return true;
}

void fill_flat_neighbors(float nbH[9], sm::Biome nbB[9], std::uint8_t nbF[9],
                         sm::Biome biome, sm::FeatureType feature) {
    for (int i = 0; i < 9; ++i) {
        nbH[i] = 0.62f;
        nbB[i] = biome;
        nbF[i] = std::uint8_t(sm::FT_None);
    }
    nbF[4] = std::uint8_t(feature);
}

int wall_structures_on_protected_tiles(const sm::sub::SubworldMapData& map) {
    int blocked = 0;
    for (const sm::sub::Structure& s : map.structures) {
        // Both kinds of wall the world raises: the city's masonry and the
        // village's trunks (gens/village_palisade.h). The question — "did a
        // wall close over the way through" — is the same for either, and
        // asking it of only one left every stockade unwatched.
        if (s.kind != sm::sub::Structure::Wall
         && s.kind != sm::sub::Structure::Palisade) continue;
        // Gate lintels (zBase > 0) legitimately BRIDGE the road: their solid
        // span starts above head height, so a record over a road tile is the
        // design, not a blocked gate.
        if (s.zBase > 0.0f) continue;
        const int x = std::clamp(int(std::floor(s.x)), 0, sm::sub::kCellSize - 1);
        const int y = std::clamp(int(std::floor(s.y)), 0, sm::sub::kCellSize - 1);
        const std::uint8_t tile = map.tiles[std::size_t(y) * sm::sub::kCellSize + x];
        if (tile == sm::sub::TILE_ROAD || tile == sm::sub::TILE_SQUARE
         || tile == sm::sub::TILE_HOUSE || tile == sm::sub::TILE_FIELD) {
            ++blocked;
        }
    }
    return blocked;
}

std::uint32_t expected_edge_seed(const sm::sub::CellContext& ctx, int dx, int dy) {
    const std::int64_t a = std::int64_t(ctx.cx) * 100003 + std::int64_t(ctx.cy);
    const std::int64_t b = std::int64_t(ctx.cx + dx) * 100003
                         + std::int64_t(ctx.cy + dy);
    const std::int64_t lo = std::min(a, b);
    const std::int64_t hi = std::max(a, b);
    const std::uint32_t seed =
        std::uint32_t(lo * 374761393ll) ^ std::uint32_t(hi * 1274126177ll);
    return seed == 0u ? 1u : seed;
}

void expected_edge_anchor(const sm::sub::CellContext& ctx, int dx, int dy,
                          int& ox, int& oy) {
    sm::Rng r(expected_edge_seed(ctx, dx, dy));
    const int edgePos = int(std::floor(float(sm::sub::kCellSize)
        * (0.35f + r.next_f01() * 0.3f)));
    if (dx == 0) {
        ox = std::clamp(edgePos, 1, sm::sub::kCellSize - 2);
        oy = dy < 0 ? 1 : sm::sub::kCellSize - 2;
    } else if (dy == 0) {
        ox = dx > 0 ? sm::sub::kCellSize - 2 : 1;
        oy = std::clamp(edgePos, 1, sm::sub::kCellSize - 2);
    } else {
        ox = dx > 0 ? sm::sub::kCellSize - 2 : 1;
        oy = dy > 0 ? sm::sub::kCellSize - 2 : 1;
    }
}

// The honest-bridge law (owner, 2026-08-29): stone DECK chords a walker
// stands on (sub/collide.h support_at), round PIERS down to the bed, and the
// gaps between them — the arches — leaving the water and the way under the
// deck free.
//
// The DECK IS LEVEL, at one world height for every span everywhere
// (the scene's water plane + freeboard), because the water it clears is one
// height —
// owner, 2026-08-30, on the stepped deck he had to jump up: «у нас вода
// одной высоты, почему тогда высота моста скачет». So a deck states its
// span in WORLD metres (Structure::zWorld) instead of lifting off the bed:
// a bed-relative deck wobbled by the difference between the generator's
// exact tile heights and the renderer's 16-tile mesh. Over LAND the same
// chain becomes a ramp falling at a gentle grade until it meets the ground,
// which is why a deck below the water plane is legal on the approach and
// only the WET stretch must be level.
int check_bridge_chain(const sm::sub::SubworldMapData& map,
                       float ax, float ay, float bx, float by) {
    using namespace sm::sub;
    static_assert(structure_is_solid(Structure::Bridge),
                  "a bridge that does not collide cannot carry a walker");
    static_assert(structure_draw(Structure::Bridge)
                      == StructureKindRow::Draw::Solid,
                  "a solid bridge must be drawn by the solid pass");
    static_assert(structure_material(Structure::Bridge)
                      == StructureKindRow::Material::Stone,
                  "the owner asked for masonry");
    // THE SCENE'S water, from the scene — the deck's whole law is stated
    // relative to it, so a witness that used a constant here would pass on a
    // world whose sea has moved and the bridge has not.
    const float seaM = sea_level_m(map.waterLevel);
    const float sx = bx - ax;
    const float sy = by - ay;
    const float spanLen = std::sqrt(sx * sx + sy * sy);
    const bool ok1 = !(spanLen <= 1.0f);
    CHECK(ok1, "bridge check needs a real span");
    if (!ok1) return 1;
    int decks = 0;
    int piers = 0;
    float pierWidthSum = 0.0f;
    std::vector<std::pair<float, float>> deckSpans;
    // Every deck top over water, in span order, to prove the ARCH is walked
    // and not jumped: (distance along the span, top).
    std::vector<std::pair<float, float>> wetDeckTops;
    // ОДИН ВЕРДИКТ НА ОБХОД, А НЕ ОДИН НА СТРУКТУРУ (AGENTS §8 п.3). Счёт
    // плохих копится, вердикт выносится ПОСЛЕ обхода и называет «сколько из
    // скольких»; первая виноватая структура печатается на месте, своими
    // координатами. Что обход вообще СОСТОЯЛСЯ, утверждают ok7/ok8/ok9 ниже —
    // отдельные гейты на само население, поэтому «помирил ли» здесь не
    // дублируется (дубль был бы вечнозелёным рядом с живым гейтом).
    int badPierWorld = 0;
    int badPierFoot = 0;
    int badPierRise = 0;
    int badDeckWorld = 0;
    int badWetDeck = 0;
    for (const Structure& s : map.structures) {
        if (s.kind != Structure::Bridge) continue;
        const int tx = std::clamp(int(s.x), 0, kCellSize - 1);
        const int ty = std::clamp(int(s.y), 0, kCellSize - 1);
        const float seatM =
            height_m(map.heightmap[std::size_t(ty) * kCellSize + tx]);
        // THE span, from the shared contract — the same call the renderer and
        // the collision index make, so the test cannot pin a fourth opinion.
        float z0, z1;
        structure_solid_span(s, seatM, z0, z1);
        if (s.shape == Structure::Cylinder) {
            ++piers;
            pierWidthSum += 2.0f * s.radius;
            if (!s.zWorld && badPierWorld++ == 0) {
                std::fprintf(stderr, "pier off world level at (%.2f,%.2f)\n",
                             double(s.x), double(s.y));
            }
            if (z0 > seatM && badPierFoot++ == 0) {
                std::fprintf(stderr,
                             "pier footing above the bed at (%.2f,%.2f): "
                             "z0=%.3f seat=%.3f\n",
                             double(s.x), double(s.y), double(z0),
                             double(seatM));
            }
            if (z1 <= seatM && badPierRise++ == 0) {
                std::fprintf(stderr,
                             "pier does not rise out of the bed at "
                             "(%.2f,%.2f): z1=%.3f seat=%.3f\n",
                             double(s.x), double(s.y), double(z1),
                             double(seatM));
            }
            continue;
        }
        ++decks;
        if (!s.zWorld && badDeckWorld++ == 0) {
            std::fprintf(stderr, "deck off world level at (%.2f,%.2f)\n",
                         double(s.x), double(s.y));
        }
        const float projD = ((s.x - ax) * sx + (s.y - ay) * sy) / spanLen;
        if (seatM < seaM) {
            if (z1 < seaM && badWetDeck++ == 0) {
                std::fprintf(stderr,
                             "deck top under the water plane at (%.2f,%.2f): "
                             "z1=%.3f sea=%.3f\n",
                             double(s.x), double(s.y), double(z1),
                             double(seaM));
            }
            wetDeckTops.emplace_back(projD, z1);
        }
        const float proj = projD;
        deckSpans.emplace_back(proj - structure_half_x(s),
                               proj + structure_half_x(s));
    }
    const bool ok2 = !(badPierWorld != 0);
    if (!ok2) {
        std::fprintf(stderr, "piers off world level: %d of %d\n",
                     badPierWorld, piers);
    }
    CHECK(ok2, "a pier stands at a world level, like its deck");
    if (!ok2) return 1;
    const bool ok3 = !(badPierFoot != 0);
    if (!ok3) {
        std::fprintf(stderr, "pier footings above the bed: %d of %d\n",
                     badPierFoot, piers);
    }
    CHECK(ok3, "a pier's footing must reach into the bed");
    if (!ok3) return 1;
    const bool ok4 = !(badPierRise != 0);
    if (!ok4) {
        std::fprintf(stderr, "piers not reaching the deck: %d of %d\n",
                     badPierRise, piers);
    }
    CHECK(ok4, "a pier must rise out of the bed to meet the deck");
    if (!ok4) return 1;
    const bool ok5 = !(badDeckWorld != 0);
    if (!ok5) {
        std::fprintf(stderr, "decks off world level: %d of %d\n",
                     badDeckWorld, decks);
    }
    CHECK(ok5, "a deck stands at a world level, not on the bed");
    if (!ok5) return 1;
    const bool ok6 = !(badWetDeck != 0);
    if (!ok6) {
        std::fprintf(stderr, "deck tops under the water plane: %d of %zu\n",
                     badWetDeck, wetDeckTops.size());
    }
    CHECK(ok6, "a deck top below the water plane is not a bridge");
    if (!ok6) return 1;
    const bool ok7 = !(decks < 2);
    CHECK(ok7, "a water span must carry a chain of deck chords");
    if (!ok7) return 1;
    const bool ok8 = !(piers < 1);
    CHECK(ok8, "a deck chain needs piers to stand on");
    if (!ok8) return 1;
    const bool ok9 = !(wetDeckTops.size() < 2);
    CHECK(ok9, "a wet crossing must carry a chain of level chords");
    if (!ok9) return 1;
    // THE ARCH, walked and not jumped. Neighbouring chords may differ only by
    // less than a kerb (kStepUpM) — the invariant the flat deck was built to
    // win, and the one the camber may not spend.
    std::sort(wetDeckTops.begin(), wetDeckTops.end());
    float lowest = wetDeckTops.front().second;
    float highest = wetDeckTops.front().second;
    // Население пар утверждает ok9 выше (wetDeckTops >= 2 ⇒ хотя бы одна пара).
    int badStep = 0;
    for (std::size_t i = 1; i < wetDeckTops.size(); ++i) {
        const float step =
            std::fabs(wetDeckTops[i].second - wetDeckTops[i - 1].second);
        if (step >= kStepUpM && badStep++ == 0) {
            std::fprintf(stderr,
                         "deck step %.3f >= kStepUpM %.3f at span d=%.2f "
                         "(tops %.3f -> %.3f)\n",
                         double(step), double(kStepUpM),
                         double(wetDeckTops[i].first),
                         double(wetDeckTops[i - 1].second),
                         double(wetDeckTops[i].second));
        }
        lowest = std::min(lowest, wetDeckTops[i].second);
        highest = std::max(highest, wetDeckTops[i].second);
    }
    const bool ok10 = !(badStep != 0);
    if (!ok10) {
        std::fprintf(stderr, "jumped steps in the deck: %d of %zu\n",
                     badStep, wetDeckTops.size() - 1);
    }
    CHECK(ok10, "a step in the deck: the arch must be walked, not jumped");
    if (!ok10) return 1;
    // The rise itself is only owed where the span HAS two banks — an arch is
    // a shape between shores. A line that runs off into open water (a stub, a
    // fixture with no dry end) has no midpoint to crown, and demanding one
    // would be demanding a ramp call itself an arch.
    const auto dry_at = [&](float d) {
        const float t = d / spanLen;
        const int tx = std::clamp(int(ax + sx * t), 0, kCellSize - 1);
        const int ty = std::clamp(int(ay + sy * t), 0, kCellSize - 1);
        return map.heightmap[std::size_t(ty) * kCellSize + tx] >= WATER_LEVEL;
    };
    if (dry_at(0.0f) && dry_at(spanLen)) {
        const bool ok11 = !(highest - lowest <= 0.01f);
        CHECK(ok11, "a span between two banks must carry an arch");
        if (!ok11) return 1;
        float crownAt = wetDeckTops.front().first;
        for (const auto& d : wetDeckTops) {
            if (d.second >= highest - 0.001f) { crownAt = d.first; break; }
        }
        const float wetFirst = wetDeckTops.front().first;
        const float wetLast = wetDeckTops.back().first;
        const float wetMid = (wetFirst + wetLast) * 0.5f;
        const bool ok12 =
            !(std::fabs(crownAt - wetMid) > 0.25f * (wetLast - wetFirst) + 1.0f);
        CHECK(ok12, "the arch crowns off-centre: that is a ramp, not an arch");
        if (!ok12) return 1;
    }
    // Every WET stretch of the road line must be decked; a dry stretch is the
    // bank's own business (the ramp lands and the ground carries the walker).
    int wetSamples = 0;
    // Что обход что-то намерил, утверждает ok14 сразу под ним (wetSamples > 0).
    int badUncovered = 0;
    for (float d = 0.0f; d <= spanLen; d += 4.0f) {
        const float t = d / spanLen;
        const int tx = std::clamp(int(ax + sx * t), 0, kCellSize - 1);
        const int ty = std::clamp(int(ay + sy * t), 0, kCellSize - 1);
        const float g =
            height_m(map.heightmap[std::size_t(ty) * kCellSize + tx]);
        if (g >= seaM - 0.5f) continue;
        ++wetSamples;
        bool covered = false;
        for (const auto& iv : deckSpans) {
            if (d >= iv.first - 1.0f && d <= iv.second + 1.0f) {
                covered = true;
                break;
            }
        }
        if (!covered && badUncovered++ == 0) {
            std::fprintf(stderr,
                         "open water with no deck at span d=%.2f tile "
                         "(%d,%d)\n",
                         double(d), tx, ty);
        }
    }
    const bool ok13 = !(badUncovered != 0);
    if (!ok13) {
        std::fprintf(stderr, "uncovered wet samples: %d of %d\n",
                     badUncovered, wetSamples);
    }
    CHECK(ok13, "open water on the road line with no deck overhead");
    if (!ok13) return 1;
    const bool ok14 = !(wetSamples == 0);
    CHECK(ok14, "bridge fixture has no wet stretch to bridge");
    if (!ok14) return 1;
    // The arches themselves: piers must not dam the crossing — most of the
    // wet stretch stays open water under the deck.
    const bool ok15 = !(pierWidthSum > 0.5f * float(wetSamples) * 4.0f);
    CHECK(ok15, "piers dam the river: no arches left");
    if (!ok15) return 1;
    return 0;
}

bool anchor_is_road(const sm::sub::SubworldMapData& map,
                    const sm::sub::CellContext& ctx,
                    int dx, int dy) {
    int ax = 0;
    int ay = 0;
    expected_edge_anchor(ctx, dx, dy, ax, ay);
    return map.tiles[std::size_t(ay) * sm::sub::kCellSize + ax]
        == sm::sub::TILE_ROAD;
}

} // namespace

int main() {
    using namespace sm;
    using namespace sm::sub;

    CellContext ctx{};
    ctx.cx = 4;
    ctx.cy = 7;
    ctx.macroHeight = 0.62f;
    ctx.biome = Meadow;
    ctx.feature = FT_None;
    // Forest is a COUNT class now (macro/tree_layer.h): a massif-interior
    // count drives Forest mode, no feature byte involved.
    ctx.treeCount = kMaxTreesPerCell;
    ctx.place.id = -1;
    ctx.place.size = 0;
    ctx.seed = 0x12345678u;

    float nbH[9];
    Biome nbB[9];
    std::uint8_t nbF[9];
    fill_flat_neighbors(nbH, nbB, nbF, Meadow, FT_None);
    int nbForest[9];
    for (int i = 0; i < 9; ++i) nbForest[i] = kMaxTreesPerCell;

    SubworldMapData out{};
    dispatch_generate(ctx, nbH, nbB, /*nbBiome5*/nullptr, nbF, out, nullptr, nbForest);

    const bool ok1 = !(resolve_mode(ctx) != SubworldMode::Forest);
    CHECK(ok1, "forest-class tree count did not resolve to Forest mode");
    if (!ok1) return 1;
    const bool ok2 = !(out.tiles.size() != std::size_t(kCellSize) * kCellSize);
    CHECK(ok2, "forest generator produced wrong tile count");
    if (!ok2) return 1;
    const bool ok3 =
        !(out.trav.size() != out.tiles.size() || out.heightmap.size() != out.tiles.size());
    CHECK(ok3, "forest generator produced mismatched map buffers");
    if (!ok3) return 1;

    const std::vector<Glade> glades = expected_forest_glades(ctx);
    const bool ok4 = !(glades.size() < 8);
    CHECK(ok4, "forest glade constants produced too few test glades");
    if (!ok4) return 1;

    int treeStructures = 0;
    for (const Structure& s : out.structures) {
        if (s.kind == Structure::Tree) ++treeStructures;
    }
    const bool ok5 = !(treeStructures <= 0);
    CHECK(ok5, "forest generator produced no tree structures");
    if (!ok5) return 1;

    const int center = kCellSize / 2;

    // ОДИН ВЕРДИКТ НА ВЕСЬ ОБХОД ВСЕХ ПОЛЯН (AGENTS §8 п.3), а не один на
    // тайл и не один на дерево: раньше эти два гейта били 79 полян × диск
    // радиуса × 16031 древесных структур. Что поляны есть, утверждает ok4
    // выше; что деревья есть — ok5; поэтому «помирил ли» здесь не дублируется.
    int gladeTiles = 0;
    int gladeTrees = 0;
    int badGladeDecor = 0;
    int badGladeTree = 0;
    for (const Glade& g : glades) {
        const int r2 = g.r * g.r;
        for (int dy = -g.r; dy <= g.r; ++dy) {
            for (int dx = -g.r; dx <= g.r; ++dx) {
                if (dx * dx + dy * dy > r2) continue;
                const int px = g.x + dx;
                const int py = g.y + dy;
                if (px < 0 || py < 0 || px >= kCellSize || py >= kCellSize) continue;
                ++gladeTiles;
                if (out.tiles[std::size_t(py) * kCellSize + px] == TILE_TREE_DECOR
                    && badGladeDecor++ == 0) {
                    std::fprintf(stderr,
                                 "tree decor inside glade (%d,%d r=%d) at "
                                 "tile (%d,%d)\n",
                                 g.x, g.y, g.r, px, py);
                }
            }
        }
        for (const Structure& s : out.structures) {
            if (s.kind != Structure::Tree) continue;
            ++gladeTrees;
            const int tx = int(std::floor(s.x));
            const int ty = int(std::floor(s.y));
            const int dx = tx - g.x;
            const int dy = ty - g.y;
            if (dx * dx + dy * dy <= r2 && badGladeTree++ == 0) {
                std::fprintf(stderr,
                             "tree structure inside glade (%d,%d r=%d) at "
                             "(%.2f,%.2f)\n",
                             g.x, g.y, g.r, double(s.x), double(s.y));
            }
        }
    }
    const bool ok6 = !(badGladeDecor != 0);
    if (!ok6) {
        std::fprintf(stderr, "glade tiles carrying tree decor: %d of %d\n",
                     badGladeDecor, gladeTiles);
    }
    CHECK(ok6, "forest glade still contains tree decor tiles");
    if (!ok6) return 1;
    const bool ok7 = !(badGladeTree != 0);
    if (!ok7) {
        std::fprintf(stderr, "tree structures inside glades: %d of %d\n",
                     badGladeTree, gladeTrees);
    }
    CHECK(ok7, "forest glade still contains tree structures");
    if (!ok7) return 1;

    CellContext grassTrail{};
    grassTrail.cx = 8;
    grassTrail.cy = -5;
    grassTrail.macroHeight = 0.62f;
    grassTrail.biome = Meadow;
    grassTrail.feature = FT_None;
    grassTrail.place.id = -1;
    grassTrail.place.size = 0;
    grassTrail.place.kind = SquadType::None;
    grassTrail.seed = 0x51525354u;
    fill_flat_neighbors(nbH, nbB, nbF, Meadow, FT_None);
    nbF[3] = std::uint8_t(FT_Road);
    nbF[5] = std::uint8_t(FT_DirtRoad);
    SubworldMapData grassTrailOut{};
    dispatch_generate(grassTrail, nbH, nbB, /*nbBiome5*/nullptr, nbF, grassTrailOut);
    const bool ok8 = !(resolve_mode(grassTrail) != SubworldMode::Grassland
                       || anchor_is_road(grassTrailOut, grassTrail, -1, 0)
                       || anchor_is_road(grassTrailOut, grassTrail, 1, 0)
                       || grassTrailOut.tiles[std::size_t(center) * kCellSize + center]
                           == TILE_ROAD);
    CHECK(ok8, "grassland wilderness cell must NOT carve roads "
               "(a road is built from the cell's OWN feature, never from its neighbours)");
    if (!ok8) return 1;

    CellContext forestTrail = ctx;
    forestTrail.cx = -8;
    forestTrail.cy = 6;
    forestTrail.seed = 0x61626364u;
    forestTrail.treeCount = kMaxTreesPerCell;
    fill_flat_neighbors(nbH, nbB, nbF, Meadow, FT_None);
    nbF[1] = std::uint8_t(FT_Road);
    nbF[7] = std::uint8_t(FT_DirtRoad);
    SubworldMapData forestTrailOut{};
    dispatch_generate(forestTrail, nbH, nbB, /*nbBiome5*/nullptr, nbF, forestTrailOut, nullptr,
                      nbForest);
    const bool ok9 = !(anchor_is_road(forestTrailOut, forestTrail, 0, -1)
                       || anchor_is_road(forestTrailOut, forestTrail, 0, 1)
                       || forestTrailOut.tiles[std::size_t(center) * kCellSize + center]
                           == TILE_ROAD);
    CHECK(ok9, "forest wilderness cell must NOT carve roads "
               "(a road is built from the cell's OWN feature, never from its neighbours)");
    if (!ok9) return 1;

    CellContext swampTrail{};
    swampTrail.cx = 6;
    swampTrail.cy = 9;
    swampTrail.macroHeight = 0.57f;
    swampTrail.biome = Swamp;
    swampTrail.feature = FT_None;
    swampTrail.place.id = -1;
    swampTrail.place.size = 0;
    swampTrail.place.kind = SquadType::None;
    swampTrail.seed = 0x71727374u;
    fill_flat_neighbors(nbH, nbB, nbF, Swamp, FT_None);
    nbF[5] = std::uint8_t(FT_Road);
    SubworldMapData swampTrailOut{};
    dispatch_generate(swampTrail, nbH, nbB, /*nbBiome5*/nullptr, nbF, swampTrailOut);
    const bool ok10 = !(resolve_mode(swampTrail) != SubworldMode::Swamp
                        || anchor_is_road(swampTrailOut, swampTrail, 1, 0)
                        || swampTrailOut.tiles[std::size_t(center) * kCellSize + center]
                            == TILE_ROAD);
    CHECK(ok10, "swamp wilderness cell must NOT carve roads "
                "(a road is built from the cell's OWN feature, never from its neighbours)");
    if (!ok10) return 1;

    CellContext mountainTrail{};
    mountainTrail.cx = -9;
    mountainTrail.cy = -7;
    mountainTrail.macroHeight = 0.84f;
    mountainTrail.biome = Mountain;   // elevation-classified biome, not a feature
    mountainTrail.feature = FT_None;
    mountainTrail.place.id = -1;
    mountainTrail.place.size = 0;
    mountainTrail.place.kind = SquadType::None;
    mountainTrail.seed = 0x81828384u;
    fill_flat_neighbors(nbH, nbB, nbF, Mountain, FT_None);
    nbF[1] = std::uint8_t(FT_Road);
    SubworldMapData mountainTrailOut{};
    dispatch_generate(mountainTrail, nbH, nbB, /*nbBiome5*/nullptr, nbF, mountainTrailOut);
    const bool ok11 = !(resolve_mode(mountainTrail) != SubworldMode::Mountain
                        || anchor_is_road(mountainTrailOut, mountainTrail, 0, -1)
                        || mountainTrailOut.tiles[std::size_t(center) * kCellSize + center]
                            == TILE_ROAD);
    CHECK(ok11, "mountain wilderness cell must NOT carve roads "
                "(a road is built from the cell's OWN feature, never from its neighbours)");
    if (!ok11) return 1;

    CellContext spire{};
    spire.cx = -2;
    spire.cy = 3;
    spire.macroHeight = 0.62f;
    spire.biome = Meadow;
    spire.feature = FT_None;
    spire.place.id = 77;
    spire.place.size = 0;
    spire.place.kind = SquadType::Spire;
    spire.seed = 0x2468ACE0u;
    fill_flat_neighbors(nbH, nbB, nbF, Meadow, FT_None);

    SubworldMapData spireOut{};
    dispatch_generate(spire, nbH, nbB, /*nbBiome5*/nullptr, nbF, spireOut);
    const bool ok12 = !(resolve_mode(spire) != SubworldMode::Spire);
    CHECK(ok12, "spire place did not resolve to Spire mode");
    if (!ok12) return 1;

    // The tower is ONE round central cylinder of the dungeon layer's shared
    // dimensions (dgn/dispatch.h — the same authority the roof exit reads;
    // literals here would be a second opinion about one tower). It stands at
    // an ABSOLUTE height (zWorld) with its foot buried, so what is asserted is
    // the span it presents to a body: a crown exactly kSpireTowerHeightM above
    // the levelled pad.
    int towerCount = 0;
    for (const Structure& s : spireOut.structures) {
        if (s.kind == Structure::Wall
            && int(std::floor(s.x)) == center
            && int(std::floor(s.y)) == center
            && std::fabs(s.radius - kSpireTowerRadiusTiles) < 0.001f
            && s.zWorld
            && s.height > kSpireTowerHeightM
            && s.shape == Structure::Cylinder) {
            ++towerCount;
        }
    }
    const bool ok13 = !(towerCount != 1);
    CHECK(ok13,
          "a spire cell carries exactly ONE round central tower, sized from the shared spire constants");
    if (!ok13) return 1;

    // ── The crown is a PLACE a body stands on ───────────────────────────────
    // Everything up here shares ONE stated height (zWorld), or the rail ripples
    // and the hatch floats: find the tower's crown and demand the rest agree
    // with it exactly.
    float crownM = 0.0f;
    for (const Structure& s : spireOut.structures) {
        if (s.kind == Structure::Wall && s.shape == Structure::Cylinder
            && s.zWorld && int(std::floor(s.x)) == center) {
            crownM = s.zBase + s.height;
        }
    }
    // 1. The parapet SEALS the rim. Sample the ring at the wall's own centre
    // line and demand every sample lies inside a chord standing on the crown —
    // the same detector the hall's masonry ring is held to.
    {
        const float wallHalf = structure_min_half_xy(Structure::Wall);
        const float ringR = kSpireTowerRadiusTiles - wallHalf;
        int uncovered = 0;
        for (int k = 0; k < 256; ++k) {
            const float a = float(k) * 6.2831853f / 256.0f;
            const float px = float(center) + std::cos(a) * ringR;
            const float py = float(center) + std::sin(a) * ringR;
            bool covered = false;
            for (const Structure& s : spireOut.structures) {
                if (s.kind != Structure::Wall || s.shape != Structure::Box) {
                    continue;
                }
                if (std::fabs(s.zBase - crownM) > 0.001f) continue;
                if (structure_surface_dist2(s, px, py) <= 0.0f) {
                    covered = true;
                    break;
                }
            }
            uncovered += covered ? 0 : 1;
        }
        const bool ok14 = !(uncovered != 0);
        CHECK(ok14, "the crown's parapet has gaps a body can walk out of");
        if (!ok14) return 1;
        // NEGATIVE CONTROL: drop one chord and the same detector must see it,
        // or "sealed" proved only that the detector is blind.
        SubworldMapData holed = spireOut;
        for (std::size_t i = 0; i < holed.structures.size(); ++i) {
            const Structure& s = holed.structures[i];
            if (s.kind == Structure::Wall && s.shape == Structure::Box
                && std::fabs(s.zBase - crownM) <= 0.001f) {
                holed.structures.erase(holed.structures.begin()
                                       + std::ptrdiff_t(i));
                break;
            }
        }
        int holes = 0;
        for (int k = 0; k < 256; ++k) {
            const float a = float(k) * 6.2831853f / 256.0f;
            const float px = float(center) + std::cos(a) * ringR;
            const float py = float(center) + std::sin(a) * ringR;
            bool covered = false;
            for (const Structure& s : holed.structures) {
                if (s.kind != Structure::Wall || s.shape != Structure::Box) {
                    continue;
                }
                if (std::fabs(s.zBase - crownM) > 0.001f) continue;
                if (structure_surface_dist2(s, px, py) <= 0.0f) {
                    covered = true;
                    break;
                }
            }
            holes += covered ? 0 : 1;
        }
        const bool ok15 = !(holes == 0);
        CHECK(ok15, "control: a removed parapet chord is a visible gap");
        if (!ok15) return 1;
    }
    // 2. The rail is chest-high — a body must not be able to step over it
    // (sub/collide.h kStepUpM), which is the whole reason it is built.
    // Один вердикт на обход хорд парапета (AGENTS §8 п.3). Что хорды есть,
    // уже доказали ok14/ok15 выше: при нуле хорд кольцо из 256 лучей было бы
    // открыто целиком и ok14 упал бы — поэтому «помирил ли» не дублируется.
    int crownChords = 0;
    int badRail = 0;
    for (const Structure& s : spireOut.structures) {
        if (s.kind != Structure::Wall || s.shape != Structure::Box) continue;
        if (std::fabs(s.zBase - crownM) > 0.001f) continue;
        ++crownChords;
        if (s.height <= sm::sub::kStepUpM && badRail++ == 0) {
            std::fprintf(stderr,
                         "parapet chord at (%.2f,%.2f) is %.3f high, "
                         "kStepUpM is %.3f\n",
                         double(s.x), double(s.y), double(s.height),
                         double(sm::sub::kStepUpM));
        }
    }
    const bool ok16 = !(badRail != 0);
    if (!ok16) {
        std::fprintf(stderr, "steppable parapet chords: %d of %d\n",
                     badRail, crownChords);
    }
    CHECK(ok16, "a parapet a body steps over is not a parapet");
    if (!ok16) return 1;
    // 3. The way back in stands on the crown, at the SHARED point, and NOT on
    // the axis — the axis is the orb's plinth, and a body put there comes up
    // inside the shrine (the 2026-09-09 defect this whole crown answers).
    {
        float hx = 0.0f, hy = 0.0f;
        sm::sub::spire_crown_hatch_point(hx, hy);
        const Structure* hatch = nullptr;
        const Structure* orb = nullptr;
        for (const Structure& s : spireOut.structures) {
            if (s.kind == Structure::SpireHatch) hatch = &s;
            if (s.kind == Structure::SpireOrb) orb = &s;
        }
        const bool ok17 = !(!hatch || hatch->x != hx || hatch->y != hy
                            || std::fabs(hatch->zBase - crownM) > 0.001f);
        CHECK(ok17, "the crown carries no hatch at the shared point");
        if (!ok17) return 1;
        const bool ok18 = !(!orb);
        CHECK(ok18, "an un-drained spire raises its orb");
        if (!ok18) return 1;
        const bool ok19 =
            !(structure_surface_dist2(*orb, hatch->x, hatch->y) <= 0.0f);
        CHECK(ok19, "the crown's hatch stands inside the orb's plinth");
        if (!ok19) return 1;
        const bool ok20 = !(std::fabs(orb->zBase - crownM) > 0.001f);
        CHECK(ok20, "the orb stands on the same crown as everything else");
        if (!ok20) return 1;
        // And it lands INSIDE the rail, not on it.
        const float dx = hatch->x - float(center);
        const float dy = hatch->y - float(center);
        const float rail = kSpireTowerRadiusTiles
                         - 2.0f * structure_min_half_xy(Structure::Wall);
        const bool ok21 = !(dx * dx + dy * dy >= rail * rail);
        CHECK(ok21, "the crown's hatch is buried in its own parapet");
        if (!ok21) return 1;
    }

    int scorchRock = 0;
    int craterRing = 0;
    int craterBad = 0;
    constexpr int kScorchRadius = 90;
    constexpr int kCraterInner = 18;
    constexpr int kCraterOuter = kCraterInner + 22;
    for (int y = center - kScorchRadius; y <= center + kScorchRadius; ++y) {
        for (int x = center - kScorchRadius; x <= center + kScorchRadius; ++x) {
            const int dx = x - center;
            const int dy = y - center;
            const int d2 = dx * dx + dy * dy;
            const std::uint8_t tile = spireOut.tiles[std::size_t(y) * kCellSize + x];
            if (d2 <= kScorchRadius * kScorchRadius && tile == TILE_ROCK) {
                ++scorchRock;
            }
            if (d2 >= kCraterInner * kCraterInner && d2 <= kCraterOuter * kCraterOuter) {
                ++craterRing;
                if (tile != TILE_ROCK && tile != TILE_SQUARE) {
                    ++craterBad;
                }
            }
        }
    }
    const bool ok22 = !(scorchRock <= 0 || craterRing <= 0 || craterBad != 0);
    CHECK(ok22, "spire scorch/crater tile invariants failed");
    if (!ok22) return 1;

    // Один вердикт на обход (AGENTS §8 п.3). САМО НАСЕЛЕНИЕ ЗДЕСЬ НЕ
    // УТВЕРЖДАЕТСЯ СОЗНАТЕЛЬНО: закон — «в выжженном круге деревьев НЕТ», и
    // клетка шпиля вправе не вырастить ни одного дерева вовсе. Требовать
    // «деревья есть» значило бы выдумать миру обязанность, которой у него нет.
    int spireTrees = 0;
    int badScorchTree = 0;
    for (const Structure& s : spireOut.structures) {
        if (s.kind != Structure::Tree) continue;
        ++spireTrees;
        const int tx = int(std::floor(s.x));
        const int ty = int(std::floor(s.y));
        const int dx = tx - center;
        const int dy = ty - center;
        if (dx * dx + dy * dy < kScorchRadius * kScorchRadius
            && badScorchTree++ == 0) {
            std::fprintf(stderr,
                         "tree inside the scorch radius at (%.2f,%.2f), "
                         "centre (%d,%d) r=%d\n",
                         double(s.x), double(s.y), center, center,
                         kScorchRadius);
        }
    }
    const bool ok23 = !(badScorchTree != 0);
    if (!ok23) {
        std::fprintf(stderr, "trees inside the scorch radius: %d of %d\n",
                     badScorchTree, spireTrees);
    }
    CHECK(ok23, "spire scorch radius still contains tree structures");
    if (!ok23) return 1;

    // ── ЧЕРЕЗ-ДОРОГА НЕ СМЕЕТ ЛОМАТЬСЯ ОТ СОСЕДА-ФИЧИ (регрессия 2026-10-26) ──
    // ШРАМ: общий предикат связности подставили в сбор МАРШРУТА, и сосед-место
    // стал считаться «концом маршрута». Клетка с двумя дорожными концами
    // получала 3+ и падала в ветку хаба — полотно пошло прямыми лучами через
    // ЦЕНТРЫ клеток, органика через-дороги исчезла. Поймал владелец ГЛАЗАМИ
    // («теперь дороги прямые линии что неприемлемо»), ни один прибор — нет.
    // ЗАКОН, который теперь под свидетелем: МАРШРУТ есть вопрос дорожной
    // топологии, СТЫКОВКА с фичей — отросток; добавление недорожного соседа
    // обязано ДОБАВЛЯТЬ съезд, а не ПЕРЕКЛАДЫВАТЬ трассу. Форма утверждения —
    // ПОДМНОЖЕСТВО: все дорожные тайлы чистой через-дороги обязаны уцелеть,
    // когда рядом появляется город.
    {
        CellContext road{};
        road.cx = 3; road.cy = 3;
        road.macroHeight = 0.60f;
        road.biome = Meadow;
        road.feature = FT_Road;
        road.seed = 0x51A7u;

        float rH[9]; sm::Biome rB[9]; std::uint8_t rF[9];
        // Два дорожных конца — запад и восток: чистая через-дорога.
        fill_flat_neighbors(rH, rB, rF, Meadow, FT_None);
        rF[3] = std::uint8_t(FT_Road);
        rF[5] = std::uint8_t(FT_Road);
        SubworldMapData through{};
        dispatch_generate(road, rH, rB, nullptr, rF, through);

        // То же самое ПЛЮС город на юге: обязан появиться съезд, трасса — та же.
        rF[7] = std::uint8_t(FT_City);
        SubworldMapData withCity{};
        dispatch_generate(road, rH, rB, nullptr, rF, withCity);

        int baseTiles = 0, kept = 0, lost = 0, added = 0;
        for (std::size_t i = 0; i < through.tiles.size(); ++i) {
            const bool a = through.tiles[i] == TILE_ROAD;
            const bool b = withCity.tiles[i] == TILE_ROAD;
            if (a) { ++baseTiles; if (b) ++kept; else ++lost; }
            else if (b) ++added;
        }
        CHECK(baseTiles > 0,
              "чистая через-дорога обязана была что-то прорезать — иначе "
              "свидетель меряет пустоту");
        CHECK(lost == 0,
              "сосед-фича ДОБАВЛЯЕТ съезд, но НЕ перекладывает трассу: ни один "
              "тайл через-дороги не смеет пропасть (иначе вернулся хаб и прямые)");
        CHECK(added > 0,
              "съезд к городу обязан появиться — ради него связность и делалась");
        std::printf("through_road: трасса %d · уцелело %d · потеряно %d · "
                    "съезд %d\n", baseTiles, kept, lost, added);
    }

    CellContext ruin{};
    ruin.cx = 8;
    ruin.cy = -5;
    ruin.macroHeight = 0.62f;
    ruin.biome = Meadow;
    // ФИКСТУРЫ ЭТОГО ТЕСТА ВРАЛИ О МИРЕ (вскрыто 2026-10-26, bugs.md Б3): клетка
    // ставилась с НУЛЕВОЙ фичей, тогда как в мире у неё фича есть. Авторитет
    // здесь — ПОЛЕ ФИЧ, одна из двух законных систем (вердикт владельца
    // 2026-10-26: «у нас есть только система поля фич и система сквады всё (у
    // них есть архетипы)»); «род места» — ЛЕГАСИ ПОД СНОС, и на него опираться
    // нельзя. Пока связность спрашивали односторонне, нулевая фича была
    // бесплатной ложью; теперь связность есть свойство ПАРЫ
    // (`features_link@src/sub/gens/kit/streets.h`), и клетка с нулём не связана
    // ни с чем по построению.
    ruin.feature = FT_Ruin;
    ruin.place.id = 88;
    ruin.place.size = 4;
    ruin.place.kind = SquadType::Ruin;
    ruin.seed = 0x13579BDFu;
    fill_flat_neighbors(nbH, nbB, nbF, Meadow, FT_None);

    SubworldMapData ruinOut{};
    dispatch_generate(ruin, nbH, nbB, /*nbBiome5*/nullptr, nbF, ruinOut);
    const bool ok24 = !(resolve_mode(ruin) != SubworldMode::Ruin);
    CHECK(ok24, "ruin place did not resolve to Ruin mode");
    if (!ok24) return 1;
    int wallTiles = 0;
    int blockedWallTiles = 0;
    int centerSquares = 0;
    for (int y = 0; y < kCellSize; ++y) {
        for (int x = 0; x < kCellSize; ++x) {
            const std::size_t idx = std::size_t(y) * kCellSize + x;
            if (ruinOut.tiles[idx] == TILE_WALL) {
                ++wallTiles;
                if (ruinOut.trav[idx] == 0) ++blockedWallTiles;
            }
            const int dx = x - center;
            const int dy = y - center;
            if (dx * dx + dy * dy <= 64 && ruinOut.tiles[idx] == TILE_SQUARE) {
                ++centerSquares;
            }
        }
    }
    const bool ok25 =
        !(wallTiles < 100 || blockedWallTiles != wallTiles || centerSquares <= 0);
    CHECK(ok25, "ruin wall/square invariants failed");
    if (!ok25) return 1;

    fill_flat_neighbors(nbH, nbB, nbF, Meadow, FT_None);
    nbF[5] = std::uint8_t(FT_Road);
    SubworldMapData ruinRoadOut{};
    dispatch_generate(ruin, nbH, nbB, /*nbBiome5*/nullptr, nbF, ruinRoadOut);
    int ruinEastX, ruinEastY;
    expected_edge_anchor(ruin, 1, 0, ruinEastX, ruinEastY);
    int ruinRoadTiles = 0;
    for (const std::uint8_t tile : ruinRoadOut.tiles) {
        if (tile == TILE_ROAD) ++ruinRoadTiles;
    }
    const bool ok26 = !(ruinRoadOut.tiles[std::size_t(ruinEastY) * kCellSize + ruinEastX] != TILE_ROAD
                        || ruinRoadTiles <= 0
                        || wall_structures_on_protected_tiles(ruinRoadOut) != 0);
    CHECK(ok26, "ruin generator ignored anchored road gate");
    if (!ok26) return 1;

    CellContext city{};
    city.cx = -11;
    city.cy = 6;
    city.macroHeight = 0.64f;
    city.biome = Meadow;
    city.feature = FT_City;      // см. заметку о фикстурах-лжецах выше
    city.place.id = 101;
    city.place.size = 6000;
    city.place.kind = SquadType::City;
    city.seed = 0x0BADF00Du;
    fill_flat_neighbors(nbH, nbB, nbF, Meadow, FT_None);

    SubworldMapData cityOut{};
    dispatch_generate(city, nbH, nbB, /*nbBiome5*/nullptr, nbF, cityOut);
    const bool ok27 = !(resolve_mode(city) != SubworldMode::City);
    CHECK(ok27, "city place did not resolve to City mode");
    if (!ok27) return 1;
    int cityHouses = 0;
    int cityWalls = 0;
    int cityFields = 0;
    int citySquares = 0;
    int cityRoads = 0;
    int cityKeeps = 0;
    const int cityBlockedWallRecords = wall_structures_on_protected_tiles(cityOut);
    for (const Structure& s : cityOut.structures) {
        if (s.kind == Structure::House) ++cityHouses;
        if (s.kind == Structure::Wall) ++cityWalls;
        // THE keep, identified by what it IS rather than by where it stands.
        // This used to demand a tall house within 16 tiles of the cell centre
        // and north of it — the old placement law, back when the keep sat on
        // the market square. A castle stands on the wall, on the highest
        // ground it can find (owner, 2026-09-13), so position is no longer the
        // keep's defining property; out-topping every dwelling is. Ordinary
        // houses run 5–9 m; the keep clears the curtain by two courses.
        if (s.kind == Structure::House && s.height >= 15.0f) ++cityKeeps;
    }
    for (const std::uint8_t tile : cityOut.tiles) {
        if (tile == TILE_FIELD) ++cityFields;
        if (tile == TILE_SQUARE) ++citySquares;
        if (tile == TILE_ROAD) ++cityRoads;
    }
    const bool ok28 = !(cityHouses < 40 || cityWalls < 40 || cityFields <= 0
                        || citySquares <= 0 || cityRoads <= 0 || cityKeeps != 1
                        || cityBlockedWallRecords != 0);
    if (!ok28) {
        std::cerr << "city counts houses=" << cityHouses
                  << " walls=" << cityWalls
                  << " fields=" << cityFields
                  << " squares=" << citySquares
                  << " roads=" << cityRoads
                  << " keeps=" << cityKeeps
                  << " blockedWallRecords=" << cityBlockedWallRecords << "\n";
    }
    CHECK(ok28, "city generator settlement invariants failed");
    if (!ok28) return 1;

    fill_flat_neighbors(nbH, nbB, nbF, Meadow, FT_None);
    nbF[3] = std::uint8_t(FT_Road);
    nbF[5] = std::uint8_t(FT_DirtRoad);
    SubworldMapData cityAlignedOut{};
    dispatch_generate(city, nbH, nbB, /*nbBiome5*/nullptr, nbF, cityAlignedOut);
    int cityWestX, cityWestY, cityEastX, cityEastY;
    expected_edge_anchor(city, -1, 0, cityWestX, cityWestY);
    expected_edge_anchor(city, 1, 0, cityEastX, cityEastY);
    const bool ok29 = !(cityAlignedOut.tiles[std::size_t(cityWestY) * kCellSize + cityWestX] != TILE_ROAD
                        || cityAlignedOut.tiles[std::size_t(cityEastY) * kCellSize + cityEastX] != TILE_ROAD
                        || cityAlignedOut.tiles[std::size_t(0) * kCellSize + center] == TILE_ROAD
                        || cityAlignedOut.tiles[std::size_t(kCellSize - 1) * kCellSize + center] == TILE_ROAD
                        || wall_structures_on_protected_tiles(cityAlignedOut) != 0);
    CHECK(ok29, "city generator ignored neighbour road directions");
    if (!ok29) return 1;

    CellContext village{};
    village.cx = 13;
    village.cy = -3;
    village.macroHeight = 0.59f;
    village.biome = Meadow;
    village.feature = FT_Village;  // см. заметку о фикстурах-лжецах выше
    village.place.id = 202;
    village.place.size = 120;
    village.place.kind = SquadType::Village;
    village.seed = 0x00C0FFEEu;
    fill_flat_neighbors(nbH, nbB, nbF, Meadow, FT_None);

    SubworldMapData villageOut{};
    dispatch_generate(village, nbH, nbB, /*nbBiome5*/nullptr, nbF, villageOut);
    const bool ok30 = !(resolve_mode(village) != SubworldMode::Village);
    CHECK(ok30, "village place did not resolve to Village mode");
    if (!ok30) return 1;
    int villageHouses = 0;
    int villageWalls = 0;
    int villageFields = 0;
    int villageSquares = 0;
    int villageRoads = 0;
    const int villageBlockedWallRecords = wall_structures_on_protected_tiles(villageOut);
    for (const Structure& s : villageOut.structures) {
        if (s.kind == Structure::House) ++villageHouses;
        // A village's wall is its OWN kind — trunks, not courses
        // (gens/village_palisade.h). Asking for Structure::Wall here counted
        // the city's masonry in a place that raises none.
        if (s.kind == Structure::Palisade) ++villageWalls;
    }
    for (const std::uint8_t tile : villageOut.tiles) {
        if (tile == TILE_FIELD) ++villageFields;
        if (tile == TILE_SQUARE) ++villageSquares;
        if (tile == TILE_ROAD) ++villageRoads;
    }
    const bool ok31 = !(villageHouses < 10 || villageWalls <= 0 || villageFields <= 0
                        || villageSquares <= 0 || villageRoads <= 0
                        || villageBlockedWallRecords != 0);
    if (!ok31) {
        std::cerr << "village counts houses=" << villageHouses
                  << " walls=" << villageWalls
                  << " fields=" << villageFields
                  << " squares=" << villageSquares
                  << " roads=" << villageRoads << "\n";
    }
    CHECK(ok31, "village generator settlement invariants failed");
    if (!ok31) return 1;

    fill_flat_neighbors(nbH, nbB, nbF, Meadow, FT_None);
    nbF[3] = std::uint8_t(FT_Road);
    nbF[5] = std::uint8_t(FT_DirtRoad);
    SubworldMapData villageAlignedOut{};
    dispatch_generate(village, nbH, nbB, /*nbBiome5*/nullptr, nbF, villageAlignedOut);
    int villageWestX, villageWestY, villageEastX, villageEastY;
    expected_edge_anchor(village, -1, 0, villageWestX, villageWestY);
    expected_edge_anchor(village, 1, 0, villageEastX, villageEastY);
    const bool ok32 = !(villageAlignedOut.tiles[std::size_t(villageWestY) * kCellSize + villageWestX] != TILE_ROAD
                        || villageAlignedOut.tiles[std::size_t(villageEastY) * kCellSize + villageEastX] != TILE_ROAD
                        || villageAlignedOut.tiles[std::size_t(0) * kCellSize + center] == TILE_ROAD
                        || villageAlignedOut.tiles[std::size_t(kCellSize - 1) * kCellSize + center] == TILE_ROAD
                        || wall_structures_on_protected_tiles(villageAlignedOut) != 0);
    CHECK(ok32, "village generator ignored neighbour road directions");
    if (!ok32) return 1;

    CellContext road{};
    road.cx = 1;
    road.cy = -9;
    road.macroHeight = 0.35f;
    road.biome = Water;
    road.feature = FT_Road;
    road.place.id = -1;
    road.place.size = 0;
    road.place.kind = SquadType::None;
    road.seed = 0x10203040u;
    fill_flat_neighbors(nbH, nbB, nbF, Water, FT_Road);
    // Honest water: a Water-biome cell's macro height sits BELOW the macro
    // sea level (0.40). The old 0.62 filler put this "water" cell on a
    // ~1400 m dome — and the phantom single-slab bridge never noticed.
    //
    // And honest BANKS, which the shipping world always has and this fixture
    // never did: a bridgeable crossing is one cell of water with LAND on both
    // sides of its axis (macro/pathfinding.h waterCrossAxes), so the west and
    // east neighbours stand above the sea and the terrain descends into the
    // river inside the cell. That is what gives the span two shores to arch
    // between and two ramps to land on.
    //
    // A BANK IS LAND, AND IT HAS TO SAY SO. Raising the height alone left these
    // two neighbours classified Water at 0.62 — a combination the world cannot
    // produce (macro/map_generator.h is_water: a cell is water exactly when its
    // height is below the plane, and rivers are carved below it). The height law
    // obeyed the lie and squared them up to 0.96, so the "banks" were domes
    // built out of the very arithmetic M-109 retired. A fixture that can only
    // exist while a bug does is not a witness to any law (AGENTS §8 п.5).
    for (int i = 0; i < 9; ++i) nbH[i] = 0.30f;
    nbH[3] = 0.62f;   nbB[3] = Meadow;   // west bank
    nbH[5] = 0.62f;   nbB[5] = Meadow;   // east bank
    nbF[3] = std::uint8_t(FT_Road);
    nbF[5] = std::uint8_t(FT_Road);

    SubworldMapData roadOut{};
    dispatch_generate(road, nbH, nbB, /*nbBiome5*/nullptr, nbF, roadOut);
    const bool ok33 = !(resolve_mode(road) != SubworldMode::Road);
    CHECK(ok33, "water road feature did not resolve to Road mode");
    if (!ok33) return 1;

    int roadWestX, roadWestY, roadEastX, roadEastY;
    expected_edge_anchor(road, -1, 0, roadWestX, roadWestY);
    expected_edge_anchor(road, 1, 0, roadEastX, roadEastY);
    // The through-road water cell bridges its two anchors with an honest
    // deck-and-piers chain along the same anchor→anchor span the old single
    // phantom slab claimed. The chain covers every wet sample of the line,
    // the anchors included, which is what carries a walker across the seam.
    if (check_bridge_chain(roadOut, float(roadWestX), float(roadWestY),
                           float(roadEastX), float(roadEastY)) != 0) {
        return 1;
    }
    // UNDER THE ARCHES THE RIVER IS THE RIVER (owner, 2026-08-30: the road
    // drawn beside its own bridge). The carved line laid road tiles across
    // the whole crossing and the submerged half of them read as a second
    // road lying on the bed; the DECK is the road over water, so no road
    // tile may sit below the plane. In this all-water fixture that means
    // none at all.
    // Один вердикт на обход (AGENTS §8 п.3). НЕПУСТОТА ЗДЕСЬ НЕ УТВЕРЖДАЕТСЯ
    // СОЗНАТЕЛЬНО, и это ровно смысл фикстуры: в сплошь водной клетке сухих
    // дорожных тайлов НОЛЬ по замыслу (см. `roadTiles` ниже — «dry ramp
    // footing only»). Гейт «тайлов > 0» здесь требовал бы от мира обратного.
    int roadTiles = 0;
    int badBedRoad = 0;
    for (std::size_t i = 0; i < roadOut.tiles.size(); ++i) {
        if (roadOut.tiles[i] != TILE_ROAD) continue;
        if (roadOut.heightmap[i] < WATER_LEVEL && badBedRoad++ == 0) {
            std::fprintf(stderr,
                         "road tile on the river bed at (%zu,%zu): h=%.3f "
                         "WATER_LEVEL=%.3f\n",
                         i % std::size_t(kCellSize),
                         i / std::size_t(kCellSize),
                         double(roadOut.heightmap[i]), double(WATER_LEVEL));
        }
        ++roadTiles;   // dry ramp footing only — nil in an all-water cell
    }
    const bool ok34 = !(badBedRoad != 0);
    if (!ok34) {
        std::fprintf(stderr, "road tiles below the water plane: %d of %d\n",
                     badBedRoad, roadTiles);
    }
    CHECK(ok34, "a road tile on the river bed: the bridge is the road");
    if (!ok34) return 1;

    fill_flat_neighbors(nbH, nbB, nbF, Water, FT_Road);
    for (int i = 0; i < 9; ++i) nbH[i] = 0.30f;   // honest water, as above
    nbF[5] = std::uint8_t(FT_Road);
    SubworldMapData roadSingleOut{};
    dispatch_generate(road, nbH, nbB, /*nbBiome5*/nullptr, nbF, roadSingleOut);
    int roadSingleEastX, roadSingleEastY;
    expected_edge_anchor(road, 1, 0, roadSingleEastX, roadSingleEastY);
    // TS road-generator: a single-connection cell carves from the edge anchor
    // to the cell CENTRE (a natural terminus), and the bridge chain spans that
    // same anchor→centre segment.
    const int roadSingleCenter = kCellSize / 2;
    if (check_bridge_chain(roadSingleOut,
                           float(roadSingleEastX), float(roadSingleEastY),
                           float(roadSingleCenter),
                           float(roadSingleCenter)) != 0) {
        return 1;
    }
    // Same law as the through-road above: over water the DECK is the road,
    // so the anchor and the terminus carry stone overhead, not road tiles on
    // the bed. check_bridge_chain has just proven the chain covers both ends.
    const bool ok35 = !(roadSingleOut.tiles[std::size_t(roadSingleEastY) * kCellSize
                                              + roadSingleEastX] == TILE_ROAD
                        || roadSingleOut.tiles[std::size_t(roadSingleCenter) * kCellSize
                                              + roadSingleCenter] == TILE_ROAD);
    CHECK(ok35, "single-neighbour road anchor invariants failed");
    if (!ok35) return 1;

    // ── THE FORK BELONGS ON THE BANK (owner in play, 2026-08-30) ─────────
    // A road running along the far shore is a neighbour that CARRIES a road
    // byte without any path joining it to the crossing, and the old hub-at-
    // centre rule dragged an arm from it out over the river: the bridge
    // forked mid-water. The crossing is the AXIS whose two cardinal
    // neighbours both carry road (macro lays a span square-on and never
    // diagonally, macro/pathfinding.h waterCrossAxes); everything else meets
    // that span at its nearest DRY point.
    fill_flat_neighbors(nbH, nbB, nbF, Water, FT_Road);
    // Land banks say they are land — see the through-road fixture above for why
    // a raised Water cell is a world that cannot exist.
    for (int i = 0; i < 9; ++i) nbH[i] = 0.30f;
    nbH[3] = 0.62f;  nbB[3] = Meadow;      // west bank
    nbH[5] = 0.62f;  nbB[5] = Meadow;      // east bank
    nbH[7] = 0.62f;  nbB[7] = Meadow;      // south shore, carrying a road past
    nbF[3] = std::uint8_t(FT_Road);
    nbF[5] = std::uint8_t(FT_Road);
    nbF[7] = std::uint8_t(FT_Road);        // the passer-by
    SubworldMapData forkOut{};
    dispatch_generate(road, nbH, nbB, /*nbBiome5*/nullptr, nbF, forkOut);
    {
        int spanChords = 0;
        // Один вердикт на обход (AGENTS §8 п.3); что хорды есть, утверждает
        // ok37 сразу под ним.
        int badChordYaw = 0;
        for (const Structure& s : forkOut.structures) {
            if (s.kind != Structure::Bridge || s.shape == Structure::Cylinder) {
                continue;
            }
            ++spanChords;
            // Every deck chord lies along the crossing AXIS (east-west here).
            // A chord aimed at the southern neighbour is the old fork.
            if (std::fabs(std::sin(s.yaw)) > 0.35f && badChordYaw++ == 0) {
                std::fprintf(stderr,
                             "deck chord off axis at (%.2f,%.2f): "
                             "|sin(yaw)|=%.3f > 0.35\n",
                             double(s.x), double(s.y),
                             double(std::fabs(std::sin(s.yaw))));
            }
        }
        const bool ok36 = !(badChordYaw != 0);
        if (!ok36) {
            std::fprintf(stderr, "deck chords off the crossing axis: %d of %d\n",
                         badChordYaw, spanChords);
        }
        CHECK(ok36,
              "a deck chord aimed off the crossing axis: forked span");
        if (!ok36) return 1;
        const bool ok37 = !(spanChords < 2);
        CHECK(ok37, "the fork fixture built no span at all");
        if (!ok37) return 1;
        // The passing road still ARRIVES — it just arrives on land.
        int southRoadTiles = 0;
        int joinY = kCellSize;          // northmost tile of the passing lane
        // Один вердикт на обход (AGENTS §8 п.3); что полоса дошла, утверждает
        // ok39 сразу под ним.
        int badWetLane = 0;
        for (int y = kCellSize / 2; y < kCellSize; ++y) {
            for (int x = 0; x < kCellSize; ++x) {
                const std::size_t i = std::size_t(y) * kCellSize + x;
                if (forkOut.tiles[i] != TILE_ROAD) continue;
                ++southRoadTiles;
                joinY = std::min(joinY, y);
                if (forkOut.heightmap[i] < WATER_LEVEL && badWetLane++ == 0) {
                    std::fprintf(stderr,
                                 "passing lane under water at tile (%d,%d): "
                                 "h=%.3f WATER_LEVEL=%.3f\n",
                                 x, y, double(forkOut.heightmap[i]),
                                 double(WATER_LEVEL));
                }
            }
        }
        const bool ok38 = !(badWetLane != 0);
        if (!ok38) {
            std::fprintf(stderr, "passing-lane tiles under water: %d of %d\n",
                         badWetLane, southRoadTiles);
        }
        CHECK(ok38, "the passing road walked into the water");
        if (!ok38) return 1;
        const bool ok39 = !(southRoadTiles <= 0);
        CHECK(ok39, "the passing road never reached the crossing");
        if (!ok39) return 1;
        (void)joinY;
        // NOT asserted here: WHERE along the span the lane joins (the owner's
        // «не прилеплять вплотную к границе субмира»). The obvious probe —
        // the x of the lane's northmost tiles — cannot tell the two rules
        // apart in this fixture, because the span's own anchors already sit
        // in the southern half, so the measurement reads the SPAN and passes
        // either way. It was written, run against a forced-nearest-point
        // negative control, seen to stay green, and removed: a check that
        // cannot fail is worse than no check, and the bank rule is verified
        // by eye until a fixture exists that separates them.
    }

    // Six assertions stood here, copying the treeDensity/treeStep of three
    // biomes out of base_generator.cpp and comparing the table to itself
    // ("drifted from TS base-generator.ts"). A test that repeats the data it
    // reads guards nothing but the typing: retuning a forest by design — the
    // one thing these numbers exist for — went red for no defect.
    //
    // The RELATION between them is a law and survives any retune: taiga is
    // denser forest than meadow, swamp sits between, and a denser biome does
    // not sample on a coarser grid.
    CHECK(biome_config(Taiga).treeDensity > biome_config(Swamp).treeDensity,
          "taiga is denser forest than swamp");
    CHECK(biome_config(Swamp).treeDensity > biome_config(Meadow).treeDensity,
          "swamp carries more trees than open meadow");
    CHECK(biome_config(Taiga).treeStep <= biome_config(Meadow).treeStep,
          "the denser biome samples on a grid no coarser than the sparse one "
          "— otherwise its density could not be realised");

    CellContext grass{};
    grass.cx = -6;
    grass.cy = 2;
    grass.macroHeight = 0.62f;
    grass.biome = Meadow;
    grass.feature = FT_None;
    grass.place.id = -1;
    grass.place.size = 0;
    grass.place.kind = SquadType::None;
    grass.seed = 0xAABBCCDDu;
    fill_flat_neighbors(nbH, nbB, nbF, Meadow, FT_None);

    SubworldMapData grassOut{};
    dispatch_generate(grass, nbH, nbB, /*nbBiome5*/nullptr, nbF, grassOut);
    const bool ok40 = !(resolve_mode(grass) != SubworldMode::Grassland
                        || std::fabs(grassOut.waterLevel - WATER_LEVEL) > 0.0001f);
    CHECK(ok40,
          "a cell with no feature resolves to Grassland and sits on the one water plane");
    if (!ok40) return 1;

    CellContext invalidFeatureGrass = grass;
    invalidFeatureGrass.feature = static_cast<FeatureType>(255u);
    fill_flat_neighbors(nbH, nbB, nbF, Meadow, FT_None);
    nbF[0] = 255u;
    nbF[4] = 255u;
    SubworldMapData invalidFeatureGrassOut{};
    dispatch_generate(invalidFeatureGrass, nbH, nbB, /*nbBiome5*/nullptr, nbF, invalidFeatureGrassOut);
    const bool ok41 = !(invalidFeatureGrassOut.tiles != grassOut.tiles
                        || invalidFeatureGrassOut.trav != grassOut.trav
                        || invalidFeatureGrassOut.structures.size() != grassOut.structures.size()
                        || !heightmaps_nearly_equal(invalidFeatureGrassOut.heightmap,
                                                    grassOut.heightmap));
    CHECK(ok41,
          "subworld dispatch did not fail closed for invalid feature bytes");
    if (!ok41) return 1;

    // Direct heightmap coverage for the elevation-classified Mountain biome.
    // Relief amplification keys off nbBiome==Mountain (base_generator ridge /
    // peak passes), so a Mountain neighbourhood must produce a strictly larger
    // vertical range than an otherwise identical plains neighbourhood — the
    // only difference between the two calls is the biome, isolating the
    // biome-driven amplification (mountains are no longer a feature).
    std::vector<float> directPlainsHeightmap;
    std::vector<float> directMountainHeightmap;
    float directNbH[9];
    Biome plainsNbB[9];
    Biome mountainNbB[9];
    for (int i = 0; i < 9; ++i) {
        directNbH[i] = 0.84f;
        plainsNbB[i] = Meadow;
        mountainNbB[i] = Mountain;
    }
    generate_heightmap(directPlainsHeightmap, 32, directNbH, plainsNbB, /*nbBiome5*/nullptr,
                       Meadow, 0, 0, grass.seaLevel);
    generate_heightmap(directMountainHeightmap, 32, directNbH, mountainNbB, /*nbBiome5*/nullptr,
                       Mountain, 0, 0, grass.seaLevel);
    const auto vertical_range = [](const std::vector<float>& hm) {
        float lo = 99.0f, hi = -99.0f;
        for (const float h : hm) {
            lo = std::min(lo, h);
            hi = std::max(hi, h);
        }
        return hm.empty() ? 0.0f : hi - lo;
    };
    const bool ok42 = !(directPlainsHeightmap.size() != directMountainHeightmap.size()
                        || directMountainHeightmap.empty()
                        || vertical_range(directMountainHeightmap)
                               <= vertical_range(directPlainsHeightmap));
    CHECK(ok42,
          "Mountain-biome neighbourhood did not amplify heightmap relief");
    if (!ok42) return 1;

    int grassTrees = 0;
    for (const Structure& s : grassOut.structures) {
        if (s.kind == Structure::Tree) ++grassTrees;
    }
    const bool ok43 = !(grassTrees <= 0);
    CHECK(ok43, "grassland generator produced no universal tree scatter");
    if (!ok43) return 1;

    CellContext water{};
    water.cx = 5;
    water.cy = 5;
    water.macroHeight = 0.28f;
    water.biome = Water;
    water.feature = FT_None;
    water.place.id = -1;
    water.place.size = 0;
    water.place.kind = SquadType::None;
    water.seed = 0x31415926u;
    fill_flat_neighbors(nbH, nbB, nbF, Water, FT_None);
    for (float& h : nbH) h = water.macroHeight;

    SubworldMapData waterOut{};
    dispatch_generate(water, nbH, nbB, /*nbBiome5*/nullptr, nbF, waterOut);
    int waterTiles = 0;
    int waterTrees = 0;
    for (const std::uint8_t tile : waterOut.tiles) {
        if (tile == TILE_WATER) ++waterTiles;
    }
    for (const Structure& s : waterOut.structures) {
        if (s.kind == Structure::Tree) ++waterTrees;
    }
    const bool ok44 = !(resolve_mode(water) != SubworldMode::Water
                        || std::fabs(waterOut.waterLevel - WATER_LEVEL) > 0.0001f
                        || waterTiles != int(waterOut.tiles.size())
                        || waterTrees != 0);
    CHECK(ok44, "water generator invariants failed");
    if (!ok44) return 1;

    CellContext coast = water;
    coast.macroHeight = 0.38f;
    coast.seed = 0x514C0A57u;
    fill_flat_neighbors(nbH, nbB, nbF, Water, FT_None);
    for (float& h : nbH) h = coast.macroHeight;
    nbH[2] = 0.62f; nbB[2] = Meadow;
    nbH[5] = 0.62f; nbB[5] = Meadow;
    nbH[8] = 0.62f; nbB[8] = Meadow;

    SubworldMapData coastOut{};
    dispatch_generate(coast, nbH, nbB, /*nbBiome5*/nullptr, nbF, coastOut);
    int coastWater = 0;
    int coastShore = 0;
    int coastLand = 0;
    int badCoastWater = 0;
    int badCoastLand = 0;
    for (std::size_t i = 0; i < coastOut.tiles.size(); ++i) {
        const std::uint8_t tile = coastOut.tiles[i];
        const float h = coastOut.heightmap[i];
        if (tile == TILE_WATER) {
            ++coastWater;
            if (h > WATER_LEVEL + 0.0001f) ++badCoastWater;
        } else {
            if (tile == TILE_SHORE) ++coastShore;
            if (tile == TILE_GRASS) ++coastLand;
            // Land/shore must not dip below the water plane (no submerged
            // land). The shore band [WATER_LEVEL, WATER_LEVEL + kLandMargin)
            // is the intentional smooth beach, not a violation.
            if (h < WATER_LEVEL - 0.0001f) ++badCoastLand;
        }
    }
    const bool ok45 = !(coastWater <= 0 || coastShore <= 0 || coastLand <= 0
                        || badCoastWater != 0 || badCoastLand != 0);
    if (!ok45) {
        std::cerr << "coast water=" << coastWater
                  << " shore=" << coastShore
                  << " land=" << coastLand
                  << " badWater=" << badCoastWater
                  << " badLand=" << badCoastLand << "\n";
    }
    CHECK(ok45,
          "a coastal cell carries water, shore and land at once, and no tile contradicts its height");
    if (!ok45) return 1;

    CellContext swamp{};
    swamp.cx = -4;
    swamp.cy = -4;
    swamp.macroHeight = 0.56f;
    swamp.biome = Swamp;
    swamp.feature = FT_None;
    swamp.place.id = -1;
    swamp.place.size = 0;
    swamp.place.kind = SquadType::None;
    swamp.seed = 0x27182818u;
    fill_flat_neighbors(nbH, nbB, nbF, Swamp, FT_None);

    SubworldMapData swampOut{};
    dispatch_generate(swamp, nbH, nbB, /*nbBiome5*/nullptr, nbF, swampOut);
    int swampTrees = 0;
    for (const Structure& s : swampOut.structures) {
        if (s.kind == Structure::Tree) ++swampTrees;
    }
    const bool ok46 = !(resolve_mode(swamp) != SubworldMode::Swamp
                        || std::fabs(swampOut.waterLevel - WATER_LEVEL) > 0.0001f
                        || swampTrees <= 0);
    CHECK(ok46, "swamp generator invariants failed");
    if (!ok46) return 1;

    CellContext mountain{};
    mountain.cx = 12;
    mountain.cy = -12;
    mountain.macroHeight = 0.84f;
    mountain.biome = Mountain;   // elevation-classified biome, not a feature
    mountain.feature = FT_None;
    mountain.place.id = -1;
    mountain.place.size = 0;
    mountain.place.kind = SquadType::None;
    mountain.seed = 0x55667788u;
    fill_flat_neighbors(nbH, nbB, nbF, Mountain, FT_None);

    SubworldMapData mountainOut{};
    dispatch_generate(mountain, nbH, nbB, /*nbBiome5*/nullptr, nbF, mountainOut);
    float mountainMin = 99.0f;
    float mountainMax = -99.0f;
    for (const float h : mountainOut.heightmap) {
        mountainMin = std::min(mountainMin, h);
        mountainMax = std::max(mountainMax, h);
    }
    const bool ok47 = !(resolve_mode(mountain) != SubworldMode::Mountain
                        || std::fabs(mountainOut.waterLevel - WATER_LEVEL) > 0.0001f
                        || mountainMax <= mountainMin);
    CHECK(ok47, "mountain generator invariants failed");
    if (!ok47) return 1;

    // ── FT_Field: an ORGANIC PLOT MODULE, not a painted cell. Feature rank
    // equals the roads' (one byte per cell, settled at macro stamp time);
    // the cell is a PATCHWORK of oriented plots — coverage bounded on BOTH
    // sides (never the full-cell square), several distinct plots, margin
    // kept from un-ploughed neighbours, underwater ground refused, density
    // following fertility, and plots MERGING across a seam between two
    // field cells. All properties, no reference copies. ──
    CellContext field{};
    field.cx = 9;
    field.cy = 3;
    field.macroHeight = 0.62f;
    field.biome = Meadow;
    field.feature = FT_Field;
    field.place.id = -1;
    field.place.size = 0;
    field.place.kind = SquadType::None;
    field.seed = 0xf1e1d001u;
    field.treeCount = 64;
    field.fertility01 = 0.70f;
    field.worldSeed = 0xab12cd34u;
    fill_flat_neighbors(nbH, nbB, nbF, Meadow, FT_Field);
    int nbFewTrees[9];
    for (int i = 0; i < 9; ++i) nbFewTrees[i] = 64;

    const bool ok48 = !(resolve_mode(field) != SubworldMode::Field);
    CHECK(ok48, "FT_Field did not resolve to Field mode");
    if (!ok48) return 1;

    SubworldMapData fieldOut{};
    dispatch_generate(field, nbH, nbB, /*nbBiome5*/nullptr, nbF, fieldOut, nullptr, nbFewTrees);
    std::size_t fieldTiles = 0;
    std::size_t wetFieldTiles = 0;
    for (std::size_t i = 0; i < fieldOut.tiles.size(); ++i) {
        if (fieldOut.tiles[i] != TILE_FIELD) continue;
        ++fieldTiles;
        if (fieldOut.heightmap[i] < WATER_LEVEL) ++wetFieldTiles;
    }
    const std::size_t fieldTotal = fieldOut.tiles.size();
    const bool ok49 = !(fieldTiles * 20 < fieldTotal);
    CHECK(ok49, "field cell grew almost no ploughland");
    if (!ok49) return 1;
    const bool ok50 = !(fieldTiles * 10 > fieldTotal * 8);
    CHECK(ok50, "field cell is a painted square again (coverage > 80%)");
    if (!ok50) return 1;
    const bool ok51 = !(wetFieldTiles != 0);
    CHECK(ok51, "plough claimed underwater tiles");
    if (!ok51) return 1;
    // An ISOLATED field cell (this fixture: no field neighbours) keeps its
    // plots clear of ground nobody ploughed — the raw edge stays grass.
    // Один вердикт на обход всей рамки (AGENTS §8 п.3). Длина обхода —
    // kCellSize, то есть непустота тут тавтология, и гейта на неё нет
    // намеренно: вечнозелёная проверка не свидетель.
    int badEdge = 0;
    for (int x = 0; x < kCellSize; ++x) {
        if ((fieldOut.tiles[std::size_t(x)] == TILE_FIELD
             || fieldOut.tiles[std::size_t(kCellSize - 1) * kCellSize
                               + std::size_t(x)] == TILE_FIELD
             || fieldOut.tiles[std::size_t(x) * kCellSize] == TILE_FIELD
             || fieldOut.tiles[std::size_t(x) * kCellSize
                               + std::size_t(kCellSize - 1)] == TILE_FIELD)
            && badEdge++ == 0) {
            std::fprintf(stderr, "plot on the raw edge at offset %d\n", x);
        }
    }
    const bool ok52 = !(badEdge != 0);
    if (!ok52) {
        std::fprintf(stderr, "edge offsets carrying plots: %d of %d\n",
                     badEdge, kCellSize);
    }
    CHECK(ok52, "a plot reached the raw edge of an isolated field cell");
    if (!ok52) return 1;
    // LOOK hook: FIELD_PPM=<prefix> dumps the plot layouts as PPMs (field =
    // wheat, grass = green, water = blue, trees = dark) — the eyes-on half
    // of this block's verification.
    const char* ppmPrefix = std::getenv("FIELD_PPM");
    auto dump_ppm = [&](const SubworldMapData& m, const char* name) {
        if (!ppmPrefix) return;
        std::string path = std::string(ppmPrefix) + name + ".ppm";
        FILE* f = std::fopen(path.c_str(), "wb");
        if (!f) return;
        std::fprintf(f, "P6\n%d %d\n255\n", kCellSize, kCellSize);
        for (std::size_t i = 0; i < m.tiles.size(); ++i) {
            unsigned char rgb[3] = {90, 140, 70};                 // grass
            if (m.tiles[i] == TILE_FIELD) { rgb[0] = 168; rgb[1] = 132; rgb[2] = 70; }
            else if (m.tiles[i] == TILE_WATER) { rgb[0] = 60; rgb[1] = 90; rgb[2] = 160; }
            else if (m.tiles[i] == TILE_SHORE) { rgb[0] = 180; rgb[1] = 165; rgb[2] = 120; }
            else if (m.tiles[i] == TILE_TREE_DECOR) { rgb[0] = 40; rgb[1] = 80; rgb[2] = 40; }
            std::fwrite(rgb, 1, 3, f);
        }
        std::fclose(f);
    };
    dump_ppm(fieldOut, "isolated");

    // A patchwork, not one blob: several disjoint plots (4-connected
    // components of real size).
    {
        std::vector<std::uint8_t> seen(fieldOut.tiles.size(), 0);
        std::vector<std::size_t> stack;
        int components = 0;
        for (std::size_t start = 0; start < fieldOut.tiles.size(); ++start) {
            if (seen[start] || fieldOut.tiles[start] != TILE_FIELD) continue;
            std::size_t area = 0;
            stack.assign(1, start);
            seen[start] = 1;
            while (!stack.empty()) {
                const std::size_t i = stack.back();
                stack.pop_back();
                ++area;
                const int x = int(i % kCellSize);
                const int y = int(i / kCellSize);
                const int nx4[4] = {x - 1, x + 1, x, x};
                const int ny4[4] = {y, y, y - 1, y + 1};
                for (int k = 0; k < 4; ++k) {
                    if (nx4[k] < 0 || ny4[k] < 0 || nx4[k] >= kCellSize
                        || ny4[k] >= kCellSize) continue;
                    const std::size_t j =
                        std::size_t(ny4[k]) * kCellSize + std::size_t(nx4[k]);
                    if (!seen[j] && fieldOut.tiles[j] == TILE_FIELD) {
                        seen[j] = 1;
                        stack.push_back(j);
                    }
                }
            }
            if (area >= 200) ++components;
        }
        const bool ok53 = !(components < 3);
        CHECK(ok53, "field cell is not a patchwork (fewer than 3 plots)");
        if (!ok53) return 1;
    }
    // Boulder walls: the balks carry SOME dry-stone rows — they exist, they
    // stand on the balks (never on ploughed or carved ground), and they are
    // far from fencing every parcel (gaps are the design).
    {
        int fences = 0;
        // Один вердикт на обход (AGENTS §8 п.3); что стенки есть, утверждает
        // ok55 сразу под ним.
        int badFence = 0;
        for (const Structure& s : fieldOut.structures) {
            if (s.kind != Structure::Fence) continue;
            ++fences;
            const int sx = std::clamp(int(s.x), 0, kCellSize - 1);
            const int sy = std::clamp(int(s.y), 0, kCellSize - 1);
            const std::uint8_t t =
                fieldOut.tiles[std::size_t(sy) * kCellSize + sx];
            if ((t == TILE_FIELD || t == TILE_ROAD || t == TILE_WATER)
                && badFence++ == 0) {
                std::fprintf(stderr,
                             "boulder wall off its balk at (%.2f,%.2f): "
                             "tile=%u\n",
                             double(s.x), double(s.y), unsigned(t));
            }
        }
        const bool ok54 = !(badFence != 0);
        if (!ok54) {
            std::fprintf(stderr, "boulder walls off their balk: %d of %d\n",
                         badFence, fences);
        }
        CHECK(ok54, "a boulder wall stands off its balk");
        if (!ok54) return 1;
        const bool ok55 = !(fences == 0);
        CHECK(ok55, "the field system grew no boulder walls at all");
        if (!ok55) return 1;
        std::cout << "field_fences=" << fences << "\n";
    }

    // Farm tracks: a neighbouring ROAD cell means a lane turns off into the
    // fields — carved from the SYMMETRIC shared-edge anchor, so the road
    // cell's own spur (gen_road) meets it at the same point.
    {
        std::uint8_t nbRoadEast[9];
        for (int i = 0; i < 9; ++i) nbRoadEast[i] = std::uint8_t(FT_None);
        nbRoadEast[4] = std::uint8_t(FT_Field);
        nbRoadEast[5] = std::uint8_t(FT_Road);   // east neighbour is a road
        SubworldMapData lanedOut{};
        dispatch_generate(field, nbH, nbB, /*nbBiome5*/nullptr, nbRoadEast, lanedOut, nullptr,
                          nbFewTrees);
        const bool ok56 = !(!anchor_is_road(lanedOut, field, 1, 0));
        CHECK(ok56, "field cell grew no lane at the shared road anchor");
        if (!ok56) return 1;
        // The other half of the same lane: the ROAD cell spurs toward its
        // field neighbour, aimed at the SAME anchor.
        CellContext roadCell = field;
        roadCell.cx = field.cx + 1;
        roadCell.feature = FT_Road;
        roadCell.seed = 0x33333333u;
        std::uint8_t nbFieldWest[9];
        for (int i = 0; i < 9; ++i) nbFieldWest[i] = std::uint8_t(FT_None);
        nbFieldWest[4] = std::uint8_t(FT_Road);
        nbFieldWest[3] = std::uint8_t(FT_Field);  // west neighbour = fields
        SubworldMapData roadOut2{};
        dispatch_generate(roadCell, nbH, nbB, /*nbBiome5*/nullptr, nbFieldWest, roadOut2, nullptr,
                          nbFewTrees);
        const bool ok57 = !(!anchor_is_road(roadOut2, roadCell, -1, 0));
        CHECK(ok57, "road cell grew no spur at the shared field anchor");
        if (!ok57) return 1;
    }

    // Fertility is the module's context: a rich cell quilts denser than a
    // poor one (same everything else, only the fertility differs).
    {
        CellContext rich = field;
        rich.fertility01 = 0.90f;
        CellContext poor = field;
        poor.fertility01 = 0.35f;
        SubworldMapData richOut{};
        SubworldMapData poorOut{};
        dispatch_generate(rich, nbH, nbB, /*nbBiome5*/nullptr, nbF, richOut, nullptr, nbFewTrees);
        dispatch_generate(poor, nbH, nbB, /*nbBiome5*/nullptr, nbF, poorOut, nullptr, nbFewTrees);
        std::size_t richTiles = 0;
        std::size_t poorTiles = 0;
        for (const std::uint8_t t : richOut.tiles)
            if (t == TILE_FIELD) ++richTiles;
        for (const std::uint8_t t : poorOut.tiles)
            if (t == TILE_FIELD) ++poorTiles;
        const bool ok58 = !(richTiles <= poorTiles);
        CHECK(ok58, "fertility does not drive the patchwork density");
        if (!ok58) return 1;
    }
    // The SEAM: two adjacent field cells (whole ring ploughed) derive any
    // straddling plot identically — fields MERGE across the border instead
    // of stopping at it, and the two sides agree about the seam columns.
    {
        std::uint8_t nbAllField[9];
        for (int i = 0; i < 9; ++i) nbAllField[i] = std::uint8_t(FT_Field);
        CellContext left = field;
        left.cx = 20;
        left.cy = 6;
        left.seed = 0x11111111u;    // per-cell seeds DIFFER, like the real
        CellContext right = field;  // resolver's — only worldSeed is shared,
        right.cx = 21;              // and the plot lattice must key off THAT
        right.cy = 6;
        right.seed = 0x22222222u;
        SubworldMapData leftOut{};
        SubworldMapData rightOut{};
        dispatch_generate(left, nbH, nbB, /*nbBiome5*/nullptr, nbAllField, leftOut, nullptr,
                          nbFewTrees);
        dispatch_generate(right, nbH, nbB, /*nbBiome5*/nullptr, nbAllField, rightOut, nullptr,
                          nbFewTrees);
        dump_ppm(leftOut, "seam_left");
        dump_ppm(rightOut, "seam_right");
        int both = 0;
        int disagree = 0;
        int either = 0;
        for (int y = 0; y < kCellSize; ++y) {
            const bool l = leftOut.tiles[std::size_t(y) * kCellSize
                                         + std::size_t(kCellSize - 1)]
                           == TILE_FIELD;
            const bool r = rightOut.tiles[std::size_t(y) * kCellSize]
                           == TILE_FIELD;
            if (l && r) ++both;
            if (l != r) ++disagree;
            if (l || r) ++either;
        }
        const bool ok59 = !(both == 0);
        CHECK(ok59, "no plot merges across the field-field seam");
        if (!ok59) return 1;
        const bool ok60 = !(either == 0 || disagree * 4 > either);
        CHECK(ok60, "the two sides of the seam disagree about the fields");
        if (!ok60) return 1;
    }

    // A field on the waterline: with a water column to the west the blended
    // heightmap genuinely dips below WATER_LEVEL inside the cell, and the
    // plough must refuse every one of those tiles. The wetland count above
    // proves nothing on a flat meadow — this fixture is the one that bites.
    CellContext shoreField = field;
    shoreField.cx = 10;
    shoreField.seed = 0xf1e1d002u;
    float nbShoreH[9];
    Biome nbShoreB[9];
    std::uint8_t nbShoreF[9];
    fill_flat_neighbors(nbShoreH, nbShoreB, nbShoreF, Meadow, FT_Field);
    for (int i = 0; i < 9; i += 3) {         // west column = deep water
        nbShoreH[i] = 0.05f;
        nbShoreB[i] = Biome::Water;
    }
    SubworldMapData shoreOut{};
    dispatch_generate(shoreField, nbShoreH, nbShoreB, /*nbBiome5*/nullptr, nbShoreF, shoreOut,
                      nullptr, nbFewTrees);
    std::size_t shoreUnderwater = 0;
    std::size_t shoreWetField = 0;
    for (std::size_t i = 0; i < shoreOut.tiles.size(); ++i) {
        if (shoreOut.heightmap[i] < WATER_LEVEL) {
            ++shoreUnderwater;
            if (shoreOut.tiles[i] == TILE_FIELD) ++shoreWetField;
        }
    }
    const bool ok61 = !(shoreUnderwater == 0);
    CHECK(ok61, "shore-field fixture has no underwater ground to measure");
    if (!ok61) return 1;
    const bool ok62 = !(shoreWetField != 0);
    CHECK(ok62, "plough claimed underwater tiles on the waterline");
    if (!ok62) return 1;

    // Negative control of the mode ladder: the same cell WITHOUT the feature
    // is plain grassland, and nothing in the base fill invents field tiles.
    CellContext bareMeadow = field;
    bareMeadow.feature = FT_None;
    fill_flat_neighbors(nbH, nbB, nbF, Meadow, FT_None);
    const bool ok63 = !(resolve_mode(bareMeadow) != SubworldMode::Grassland);
    CHECK(ok63, "bare meadow stopped resolving to Grassland");
    if (!ok63) return 1;
    SubworldMapData bareOut{};
    dispatch_generate(bareMeadow, nbH, nbB, /*nbBiome5*/nullptr, nbF, bareOut, nullptr, nbFewTrees);
    // Один вердикт на обход всего буфера тайлов (AGENTS §8 п.3). Непустота —
    // тавтология (буфер фиксированной длины), гейта на неё нет намеренно.
    int badBareField = 0;
    for (std::size_t i = 0; i < bareOut.tiles.size(); ++i) {
        if (bareOut.tiles[i] == TILE_FIELD && badBareField++ == 0) {
            std::fprintf(stderr,
                         "grassland grew a field tile at (%zu,%zu)\n",
                         i % std::size_t(kCellSize),
                         i / std::size_t(kCellSize));
        }
    }
    const bool ok64 = !(badBareField != 0);
    if (!ok64) {
        std::fprintf(stderr, "field tiles on bare grassland: %d of %zu\n",
                     badBareField, bareOut.tiles.size());
    }
    CHECK(ok64, "grassland invented field tiles without the feature");
    if (!ok64) return 1;

    // Crop stands: the field grows wheat, every stand roots in ploughed
    // ground, and ground without a field grows none — the ONE scatterer all
    // TILE_FIELD producers share (scatter_field_crops).
    int cropStands = 0;
    // Два вердикта на обход, а не два на стебель (AGENTS §8 п.3); что стебли
    // есть, утверждает ok67 сразу под обходом.
    int badCropGround = 0;
    int badCropHeight = 0;
    for (const Structure& s : fieldOut.structures) {
        if (s.kind != Structure::Crop) continue;
        ++cropStands;
        const int sx = std::clamp(int(s.x), 0, kCellSize - 1);
        const int sy = std::clamp(int(s.y), 0, kCellSize - 1);
        if (fieldOut.tiles[std::size_t(sy) * kCellSize + sx] != TILE_FIELD
            && badCropGround++ == 0) {
            std::fprintf(stderr,
                         "crop stand off the plough at (%.2f,%.2f): tile=%u\n",
                         double(s.x), double(s.y),
                         unsigned(fieldOut.tiles[std::size_t(sy) * kCellSize
                                                 + sx]));
        }
        if (s.height <= 0.0f && badCropHeight++ == 0) {
            std::fprintf(stderr, "crop stand with no height at (%.2f,%.2f)\n",
                         double(s.x), double(s.y));
        }
    }
    const bool ok65 = !(badCropGround != 0);
    if (!ok65) {
        std::fprintf(stderr, "crop stands off the plough: %d of %d\n",
                     badCropGround, cropStands);
    }
    CHECK(ok65, "a crop stand grew off the ploughed ground");
    if (!ok65) return 1;
    const bool ok66 = !(badCropHeight != 0);
    if (!ok66) {
        std::fprintf(stderr, "crop stands with no height: %d of %d\n",
                     badCropHeight, cropStands);
    }
    CHECK(ok66, "a crop stand has no height");
    if (!ok66) return 1;
    const bool ok67 = !(cropStands == 0);
    CHECK(ok67, "field cell grew no crop stands");
    if (!ok67) return 1;
    // Один вердикт на обход. НЕПУСТОТА НЕ УТВЕРЖДАЕТСЯ СОЗНАТЕЛЬНО: закон —
    // «без фичи поля стеблей НЕТ», и голый луг вправе не породить ни одной
    // структуры вовсе.
    int bareStructures = 0;
    int badBareCrop = 0;
    for (const Structure& s : bareOut.structures) {
        ++bareStructures;
        if (s.kind == Structure::Crop && badBareCrop++ == 0) {
            std::fprintf(stderr,
                         "crop on bare grassland at (%.2f,%.2f)\n",
                         double(s.x), double(s.y));
        }
    }
    const bool ok68 = !(badBareCrop != 0);
    if (!ok68) {
        std::fprintf(stderr, "crops on bare grassland: %d of %d structures\n",
                     badBareCrop, bareStructures);
    }
    CHECK(ok68, "grassland grew crops without a field");
    if (!ok68) return 1;

    // The harvest scar (crop_count row, Field Inc F3) thins the replant
    // DETERMINISTICALLY: a cell that lost k stands regrows exactly k fewer,
    // and a razed cell regrows none — returning never resurrects the wheat.
    fill_flat_neighbors(nbH, nbB, nbF, Meadow, FT_Field);
    CellContext scarredField = field;
    const int scarK = std::min(5, cropStands);
    scarredField.cropHarvested = scarK;
    SubworldMapData scarOut{};
    dispatch_generate(scarredField, nbH, nbB, /*nbBiome5*/nullptr, nbF, scarOut, nullptr,
                      nbFewTrees);
    int scarStands = 0;
    for (const Structure& s : scarOut.structures) {
        if (s.kind == Structure::Crop) ++scarStands;
    }
    const bool ok69 = !(scarStands != cropStands - scarK);
    CHECK(ok69, "harvest scar did not thin the replant by exactly k");
    if (!ok69) return 1;
    CellContext razedField = field;
    razedField.cropHarvested = 1 << 20;
    SubworldMapData razedOut{};
    dispatch_generate(razedField, nbH, nbB, /*nbBiome5*/nullptr, nbF, razedOut, nullptr,
                      nbFewTrees);
    // Один вердикт на обход. НЕПУСТОТА НЕ УТВЕРЖДАЕТСЯ СОЗНАТЕЛЬНО — по той
    // же причине, что у голого луга выше: закон говорит «пшеницы НЕТ», и
    // выбитая клетка вправе не вырастить ничего.
    int razedStructures = 0;
    int badRazedCrop = 0;
    for (const Structure& s : razedOut.structures) {
        ++razedStructures;
        if (s.kind == Structure::Crop && badRazedCrop++ == 0) {
            std::fprintf(stderr,
                         "wheat on a fully harvested cell at (%.2f,%.2f)\n",
                         double(s.x), double(s.y));
        }
    }
    const bool ok70 = !(badRazedCrop != 0);
    if (!ok70) {
        std::fprintf(stderr,
                     "crops on a razed cell: %d of %d structures\n",
                     badRazedCrop, razedStructures);
    }
    CHECK(ok70, "a fully harvested cell still grew wheat");
    if (!ok70) return 1;

    // The furrow-orientation door (sub/material.h, twin of macro.frag): if
    // the hash degenerated to a constant, every field on the map would
    // plough one way — both orientations must occur over a small scan.
    int furrowVert = 0;
    int furrowHoriz = 0;
    for (int fy = 0; fy < 16; ++fy) {
        for (int fx = 0; fx < 16; ++fx) {
            if (field_furrows_vertical(fx, fy)) ++furrowVert;
            else ++furrowHoriz;
        }
    }
    const bool ok71 = !(furrowVert == 0 || furrowHoriz == 0);
    CHECK(ok71, "furrow orientation hash is constant");
    if (!ok71) return 1;

    std::cout << "subworld_generator_parity_test ok: forest_glades="
              << glades.size() << " tree_structures=" << treeStructures
              << " spire_scorch_rock=" << scorchRock
              << " ruin_wall_tiles=" << wallTiles
              << " city_houses=" << cityHouses
              << " village_houses=" << villageHouses
              << " road_tiles=" << roadTiles
              << " grassland_trees=" << grassTrees
              << " swamp_trees=" << swampTrees
              << " field_tiles=" << fieldTiles << "\n";
    return sm::test::report("subworld_generator_parity_test");
}

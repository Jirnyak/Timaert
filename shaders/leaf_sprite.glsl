// THE SILHOUETTE OF A LEAF CARD — pixel-cut, old-school, and deliberately not
// smooth (owner, 2026-10-04: «по олдскулу … у нас стиль ретро рпг минимализм
// не подходят гладкие обтекаемые сосенки надо более угловато»).
//
// WHY THIS IS NOT A SECOND COPY OF tree_sprite.glsl. That one draws a WHOLE
// TREE onto a camera-facing quad — trunk, crown, branches, species by species
// — because a billboard has to be the entire thing. This draws ONE CLUMP of
// foliage onto one card of a body that already has a trunk standing in it.
// Different question, different answer; putting the whole-tree sprite on a
// leaf card would print a tree inside a tree.
#ifndef TIMAERT_LEAF_SPRITE
#define TIMAERT_LEAF_SPRITE

// The card's own grid. Sixteen cells across is the same quantisation the
// subworld's older crown used, kept because it IS the retro look and because
// a clump finer than this stops reading as leaves and starts reading as
// noise at the distance a tree is actually seen from.
const float kLeafCells = 16.0;

float leaf_hash(vec2 p, uint seed) {
    vec3 q = fract(vec3(p.xyx) * vec3(0.1031, 0.1030, 0.0973)
                   + float(seed & 0xffffu) * 0.0007);
    q += dot(q, q.yzx + 33.33);
    return fract((q.x + q.y) * q.z);
}

// Coverage of one clump: 1 where the card shows foliage, 0 where it shows
// through. `uv` runs 0..1 over the card, `seed` varies clump to clump.
//
// A LOBED BLOB, NOT A DISC. Three offset lobes with their own radii make an
// outline that turns corners, which is what "angular" means here; a single
// circle quantised to a grid still reads as a circle, only jagged.
float leaf_patch(vec2 uv, uint seed) {
    vec2 cell = floor(uv * kLeafCells);
    vec2 p    = (cell + 0.5) / kLeafCells * 2.0 - 1.0;   // -1..1, quantised
    float cov = 0.0;
    for (int i = 0; i < 3; ++i) {
        float h0 = leaf_hash(vec2(float(i) * 7.0, 3.0), seed);
        float h1 = leaf_hash(vec2(float(i) * 11.0, 5.0), seed);
        float h2 = leaf_hash(vec2(float(i) * 13.0, 9.0), seed);
        vec2  c  = vec2(h0 - 0.5, h1 - 0.5) * 0.55;
        float r  = 0.42 + h2 * 0.30;
        cov = max(cov, step(length(p - c), r));
    }
    // Chew the rim: cells near the edge of the blob drop out on their own
    // hash, so no two clumps end on the same outline and the border is torn
    // rather than drawn.
    float bite = leaf_hash(cell, seed ^ 0x5bd1e995u);
    float edge = 1.0 - smoothstep(0.55, 1.0, length(p));
    return (cov > 0.5 && bite < 0.25 + 0.75 * edge) ? 1.0 : 0.0;
}

#endif // TIMAERT_LEAF_SPRITE

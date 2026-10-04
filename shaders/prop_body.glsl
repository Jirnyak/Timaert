// THE BODY OF A PROFILE — ONE WRITING, read by the lit pass and by the shadow
// pass. A shape written twice is a shape that will be written differently
// twice: the shadow of a tree would stop being the tree's own outline the
// first time one of the two was tuned, and nothing would fail to build.
#ifndef TIMAERT_PROP_BODY
#define TIMAERT_PROP_BODY
#include "prop_profiles.glsl"

const float kTau = 6.28318530718;

// The six corners of a quad, as (along the segment, up the gap).
const vec2 kQuad[6] = vec2[6](vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(0.0, 1.0),
                              vec2(1.0, 0.0), vec2(1.0, 1.0), vec2(0.0, 1.0));

float hash_u(uint h) {
    h ^= h >> 15; h *= 0x2c1b3c6du; h ^= h >> 12; h *= 0x297a2d39u;
    h ^= h >> 15;
    return float(h & 0x00ffffffu) / float(0x00ffffff);
}


// Build one vertex of profile `prof` from its index. Returns the position in
// the prop's own frame (metres, origin at the base centre, +Y up), the
// outward normal in that frame, and the height fraction the fragment stage
// splits bark from crown by.
void prop_body_vertex(uint vi, uint prof, float halfW, float heightM,
                      out vec3 localPos, out vec3 localNormal,
                      out float localY, out vec2 uv, out float isLeaf) {
    uint segs  = kProfileSegments[prof];
    uint rings = kProfileRingCount[prof];
    uint sideVerts = segs * 6u * (rings - 1u);
    uint trunkVerts = sideVerts + segs * 3u;
    vec2  rg;
    float ang;
    uv = vec2(0.0);
    isLeaf = 0.0;
    if (vi >= trunkVerts) {
        // ── FOLIAGE: FLAT CARDS, CROSSED THROUGH THE AXIS ────────────────
        // Leaves are not a surface — they are a great many small things, and
        // a card with a cut-out silhouette says that in two triangles where a
        // body of revolution lies about it with fifty and reads as a toy.
        // This is the old-school answer and it is deliberate (owner,
        // 2026-10-04: «ствол и ветки 3д а листва кроны плоские плоскости …
        // по олдскулу»).
        uint lv     = vi - trunkVerts;
        uint planes = max(kProfileLeafPlanes[prof], 1u);
        uint tier   = lv / (planes * 6u);
        uint plane  = (lv / 6u) % planes;
        vec2 q      = kQuad[lv % 6u];
        vec2 tr     = kProfileLeaves[prof * kProfileMaxTiers + tier];
        float reach = tr.y * halfW;
        // ── A BOUGH LIES ALONG ITS BRANCH, IT DOES NOT HANG FROM IT ──────
        // The cards used to stand UPRIGHT through the axis, and that was
        // wrong twice over: real foliage spreads along the branch, and an
        // upright card lights as a wall while a bough lights as a roof. So
        // the card lies MOSTLY FLAT, running outward from the trunk, with
        // its outer edge dropped — the droop every conifer has.
        //
        // Its normal is the normal OF THAT SURFACE (owner, 2026-10-04: «она
        // же идёт как нормаль к продольной поверхности ветки — как реальная
        // листва от веток»), which is what makes a tier catch the sun on top
        // and fall into shadow underneath instead of flickering edge-on.
        float pa  = float(plane) * 6.28318530718 / float(planes)
                  + float(tier) * 0.7;      // tiers do not stack in a line
        vec2  out2 = vec2(cos(pa), sin(pa));            // along the branch
        vec2  side = vec2(-out2.y, out2.x);             // across it
        float u = q.x;                 // 0 at the trunk, 1 at the tip
        float v = q.y * 2.0 - 1.0;     // across the bough
        // HOW FAR THE TIP HANGS. Flat was wrong in the other direction:
        // seen from a man's eye a horizontal bough is edge-on and the tree
        // becomes a stick. Just over half a reach of drop puts the surface
        // at about 30° — it still lights as a roof, and it still has a
        // silhouette from the side, which is the view the game is played in.
        float droop = 0.62;
        vec3 alongV = vec3(out2.x, -droop, out2.y);
        vec3 sideV  = vec3(side.x, 0.0, side.y);
        localPos = vec3(0.0, tr.x * heightM, 0.0)
                 + alongV * (u * reach)
                 + sideV  * (v * reach * 0.55);
        // Normal of the bough's own plane: across × along, pointing up.
        localNormal = normalize(cross(sideV, alongV));
        if (localNormal.y < 0.0) localNormal = -localNormal;
        localY = tr.x;
        uv     = q;
        isLeaf = 1.0;
        return;
    }
    if (vi < sideVerts) {
        uint quad = vi / 6u;
        vec2 q    = kQuad[vi % 6u];
        uint seg  = quad % segs;
        uint k    = quad / segs;
        vec2 r0 = kProfileRings[prof * kProfileMaxRings + k];
        vec2 r1 = kProfileRings[prof * kProfileMaxRings + k + 1u];
        rg  = mix(r0, r1, q.y);
        ang = (float(seg) + q.x) * kTau / float(segs);
        // THE SLOPE OF THE PROFILE IS THE NORMAL, in METRES: a radius
        // fraction and a height fraction live on different scales, and
        // deriving it from the fractions would light every prop as though it
        // were as wide as it is tall.
        float dr = (r1.x - r0.x) * halfW;
        float dy = (r1.y - r0.y) * heightM;
        vec2  rn = normalize(vec2(dy, -dr));
        localNormal = vec3(cos(ang) * rn.x, rn.y, sin(ang) * rn.x);
    } else {
        // THE CAP: a fan from the top ring to its centre. A top ring of
        // radius 0 — a crown closing to a point — degenerates it to zero area
        // and the rasteriser drops it, so a cone needs no branch anywhere.
        uint t = (vi - sideVerts) / 3u;
        uint c = (vi - sideVerts) % 3u;
        vec2 top = kProfileRings[prof * kProfileMaxRings + (rings - 1u)];
        float a0 = float(t) * kTau / float(segs);
        float a1 = float(t + 1u) * kTau / float(segs);
        ang = (c == 1u) ? a0 : a1;
        rg  = (c == 0u) ? vec2(0.0, top.y) : top;
        localNormal = vec3(0.0, 1.0, 0.0);
    }
    localPos = vec3(cos(ang) * rg.x * halfW, rg.y * heightM,
                    sin(ang) * rg.x * halfW);
    localY   = rg.y;
}

// EVERY INSTANCE STANDS ITS OWN WAY. One profile would otherwise print the
// same silhouette across a whole wood, and with six flat sides that reads
// instantly as a repeat. The angle is a hash of the instance SEED, never a
// function of position, so the pattern is not tied to where the window is.
mat2 prop_body_yaw(uint seed) {
    float a = hash_u(seed) * kTau;
    return mat2(cos(a), sin(a), -sin(a), cos(a));
}
#endif // TIMAERT_PROP_BODY

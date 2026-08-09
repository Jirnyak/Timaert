// Tree sprite coverage — the single source of truth for the procedural 7-species
// tree billboard (pine/birch/willow/jungle/oak/cherry/autumn), keyed by
// species + seed. BOTH the lit pass (billboard.frag) and the depth-only shadow
// caster (shadow_bb.frag) call this, so the cast shadow is the tree's REAL
// silhouette — never a hand-authored blob. Returns coverage (0 = transparent,
// 1 = opaque) and writes the pixel colour.
#ifndef TREE_SPRITE_GLSL
#define TREE_SPRITE_GLSL

float treeHash(float n) {
    n = fract(n * 0.1031);
    n *= n + 33.33;
    n *= n + n;
    return fract(n);
}

// uv: x across [0,1], y up [0,1] (0 = base). Returns coverage; outCol = colour.
float treeCoverage(vec2 uv, float species, float seed, out vec3 outCol) {
    int tp = int(species + 0.5);
    float v1 = treeHash(seed + 1.0);
    float v2 = treeHash(seed + 2.0);

    vec2 p = floor(vec2(uv.x, 1.0 - uv.y) * 16.0);
    float cs = treeHash(seed) * 1e3;
    float ph = treeHash(cs + p.x * 17.1 + p.y * 31.7);
    float cx = 7.0 + floor((v1 - 0.5) * 2.0);

    if (tp == 7) {
        // WHEAT (the Crop prop, sub/map_data.h kStructureKindRows) — not a
        // tree: a stand of stalks drawn straight on the 16×16 pixel grid.
        // Every other column is a stalk with its own hashed head height;
        // heads go golden, stalks fade greener toward the ground, and a few
        // pixels drop out so the stand reads ragged, not woven. The shadow
        // caster calls this same function, so the cast shadow is the stand's
        // real silhouette.
        outCol = vec3(0.0);
        if (mod(p.x, 2.0) < 0.5) return 0.0;
        float colSeed = treeHash(cs + p.x * 29.3);
        float topRow = 2.0 + floor(colSeed * 5.0);     // head row 2..6
        if (p.y < topRow) return 0.0;                  // sky above the head
        if (ph > 0.88) return 0.0;                     // ragged dropout
        float up = clamp((15.0 - p.y) / 13.0, 0.0, 1.0);
        bool head = p.y <= topRow + 2.0;
        vec3 stalk = mix(vec3(0.38, 0.42, 0.16), vec3(0.72, 0.62, 0.28), up);
        outCol = head ? vec3(0.86, 0.72, 0.34) : stalk;
        if (head && ph > 0.6) outCol *= 1.12;          // glinting awns
        return 1.0;
    }

    vec3 bark1, bark2, leaf1, leaf2, leaf3;
    if (tp == 0) {
        bark1 = vec3(79,56,41)/255.0;  bark2 = vec3(101,67,33)/255.0;
        leaf1 = vec3(30,120,30)/255.0; leaf2 = vec3(50,160,50)/255.0; leaf3 = vec3(75,105,42)/255.0;
    } else if (tp == 1) {
        bark1 = vec3(60,40,30)/255.0;   bark2 = vec3(85,55,40)/255.0;
        leaf1 = vec3(255,160,180)/255.0; leaf2 = vec3(255,120,165)/255.0; leaf3 = vec3(225,105,145)/255.0;
    } else if (tp == 2) {
        bark1 = vec3(195,195,190)/255.0; bark2 = vec3(240,240,235)/255.0;
        leaf1 = vec3(105,195,85)/255.0; leaf2 = vec3(135,215,105)/255.0; leaf3 = vec3(85,165,65)/255.0;
    } else if (tp == 3) {
        bark1 = vec3(70,50,40)/255.0;   bark2 = vec3(95,68,48)/255.0;
        leaf1 = vec3(235,125,10)/255.0; leaf2 = vec3(225,65,10)/255.0; leaf3 = vec3(245,200,15)/255.0;
    } else if (tp == 4) {
        bark1 = vec3(88,58,38)/255.0; bark2 = vec3(105,72,52)/255.0;
        leaf1 = vec3(12,82,12)/255.0; leaf2 = vec3(32,115,32)/255.0; leaf3 = vec3(18,68,18)/255.0;
    } else if (tp == 5) {
        bark1 = vec3(88,62,48)/255.0;  bark2 = vec3(105,72,38)/255.0;
        leaf1 = vec3(125,190,45)/255.0; leaf2 = vec3(105,170,35)/255.0; leaf3 = vec3(145,205,55)/255.0;
    } else {
        bark1 = vec3(62,45,30)/255.0; bark2 = vec3(80,55,35)/255.0;
        leaf1 = vec3(15,95,20)/255.0; leaf2 = vec3(25,130,30)/255.0; leaf3 = vec3(10,75,15)/255.0;
    }

    vec3 bk = ph < 0.5 ? bark1 : bark2;
    vec3 lf = ph < 0.33 ? leaf1 : (ph < 0.66 ? leaf2 : leaf3);

    vec3 col = vec3(0.0);
    float drawn = 0.0;

    if (tp == 4) {
        // PINE
        float trT = 10.0 - floor(v2);
        if (p.y >= trT && p.y <= 14.0 && abs(p.x - cx) < 1.0) { col = bk; drawn = 1.0; }
        if (p.y == 15.0 && abs(p.x - cx) <= 1.0) { col = vec3(0.08,0.12,0.04); drawn = 0.45; }

        float baseY = 1.0 + floor(v1 * 2.0);
        for (int i = 0; i < 3; i++) {
            float tT = baseY + float(i) * 3.0;
            float tB = tT + 3.0;
            if (p.y >= tT && p.y <= tB) {
                float frac = (p.y - tT) / 3.0;
                float halfW = 0.5 + frac * (2.2 + float(i) * 0.7);
                float eN = (treeHash(cs + p.y * 7.1 + float(i) * 97.0) - 0.5) * 0.7;
                if (abs(p.x - cx) <= halfW + eN) {
                    vec3 lc = lf;
                    if (p.y < tT + 1.0) lc *= 1.18;
                    else if (p.y >= tB) lc *= 0.72;
                    col = lc; drawn = 1.0;
                }
            }
        }
    } else if (tp == 2) {
        // BIRCH
        float trT = 5.0 - floor(v2);
        if (p.y >= trT && p.y <= 14.0 && abs(p.x - cx) < 1.0) {
            col = bk;
            if (mod(p.y + floor(v1 * 3.0), 3.0) < 1.0 && ph > 0.4) col = vec3(0.22,0.22,0.20);
            drawn = 1.0;
        }
        if (p.y == 15.0 && abs(p.x - cx) <= 1.0) { col = vec3(0.08,0.12,0.04); drawn = 0.45; }

        float cY = trT - 2.5;
        float rX = 2.5 + v1 * 1.2;
        float rY = 3.5 + v2 * 1.5;
        vec2 dd = (p - vec2(cx, cY)) / vec2(rX, rY);
        float eN = (treeHash(cs + p.x * 11.3 + p.y * 19.7) - 0.5) * 0.25;
        if (dot(dd, dd) <= 1.0 + eN) {
            vec3 lc = lf;
            if (dd.y < -0.35) lc *= 1.18;
            else if (dd.y > 0.35) lc *= 0.78;
            col = lc; drawn = 1.0;
        }
    } else if (tp == 5) {
        // WILLOW
        float trT = 7.0;
        if (p.y >= trT && p.y <= 14.0 && abs(p.x - cx) < 1.0) { col = bk; drawn = 1.0; }
        if (p.y == 15.0 && abs(p.x - cx) <= 2.0) { col = vec3(0.08,0.12,0.04); drawn = 0.45; }

        float cY = 4.5;
        float cR = 4.5 + v1;
        float d = length(p - vec2(cx, cY));
        float eN = (treeHash(cs + p.x * 13.3 + p.y * 23.7) - 0.5) * 1.0;
        if (d <= cR + eN) {
            vec3 lc = lf;
            if (p.y < cY - cR * 0.3) lc *= 1.15;
            else if (p.y > cY + cR * 0.15) lc *= 0.82;
            col = lc; drawn = 1.0;
        }

        for (int i = 0; i < 6; i++) {
            float vs = cs + float(i) * 7.3;
            if (treeHash(vs) > 0.55) continue;
            float vx = cx - 3.0 + float(i) * 1.2 + treeHash(vs + 1.0) * 0.5;
            float vineStart = cY + cR * 0.5;
            float vineLen = 2.0 + treeHash(vs + 2.0) * 2.5;
            if (abs(p.x - floor(vx)) < 1.0 && p.y >= vineStart && p.y < vineStart + vineLen) {
                col = lf * 0.82; drawn = 1.0;
            }
        }
    } else if (tp == 6) {
        // JUNGLE
        float trT = 7.0;
        if (p.y >= trT && p.y <= 14.0 && abs(p.x - cx) <= 1.0) { col = bk; drawn = 1.0; }
        if (p.y >= 13.0 && p.y <= 15.0) {
            float rootW = 2.5 - (15.0 - p.y) * 0.5;
            if (abs(p.x - cx) <= rootW && abs(p.x - cx) > 1.0) {
                col = bk * 0.85; drawn = 1.0;
            }
        }
        if (p.y == 15.0 && abs(p.x - cx) <= 3.0) { col = vec3(0.06,0.10,0.03); drawn = 0.45; }

        float cY1 = 3.5;
        float rX1 = 5.5 + v1 * 1.5;
        float rY1 = 4.0 + v2;
        vec2 dd1 = (p - vec2(cx, cY1)) / vec2(rX1, rY1);
        float eN1 = (treeHash(cs + p.x * 11.3 + p.y * 19.7) - 0.5) * 0.35;
        if (dot(dd1, dd1) <= 1.0 + eN1) {
            vec3 lc = lf;
            if (dd1.y < -0.3) lc *= 1.15;
            else if (dd1.y > 0.3) lc *= 0.75;
            if (dot(dd1, dd1) > 0.7 + eN1) lc *= 0.85;
            col = lc; drawn = 1.0;
        }

        float cx2 = cx + (v1 < 0.5 ? -2.0 : 2.0);
        float cY2 = 2.0 + v2;
        float rC2 = 3.0 + v1 * 0.8;
        float d2 = length(p - vec2(cx2, cY2));
        float eN2 = (treeHash(cs + p.x * 9.1 + p.y * 15.3) - 0.5) * 0.5;
        if (d2 <= rC2 + eN2) {
            vec3 lc = leaf2;
            if (p.y < cY2 - rC2 * 0.3) lc *= 1.12;
            else if (p.y > cY2 + rC2 * 0.2) lc *= 0.78;
            col = lc; drawn = 1.0;
        }

        for (int i = 0; i < 7; i++) {
            float vs = cs + float(i) * 5.7;
            if (treeHash(vs) > 0.5) continue;
            float vx = cx - 4.0 + float(i) * 1.3 + treeHash(vs + 1.0) * 0.5;
            float vineStart = cY1 + rY1 * 0.5;
            float vineLen = 2.5 + treeHash(vs + 2.0) * 3.0;
            if (abs(p.x - floor(vx)) < 1.0 && p.y >= vineStart && p.y < vineStart + vineLen) {
                col = leaf3 * 0.9; drawn = 1.0;
            }
        }
    } else {
        // OAK / CHERRY / AUTUMN -- round canopy
        float trT = 9.0 - floor(v2 * 2.0);
        if (p.y >= trT && p.y <= 14.0 && abs(p.x - cx) < 1.0) { col = bk; drawn = 1.0; }
        if (p.y == 15.0 && abs(p.x - cx) <= 2.0) { col = vec3(0.08,0.12,0.04); drawn = 0.45; }

        float cR = 4.5 + v1 * 1.5;
        float cY = trT - cR + 1.5;
        float d = length(p - vec2(cx, cY));
        float eN = (treeHash(cs + p.x * 11.3 + p.y * 19.7) - 0.5) * 1.0;
        if (d <= cR + eN) {
            vec3 lc = lf;
            if (p.y < cY - cR * 0.3) lc *= 1.22;
            else if (p.y > cY + cR * 0.3) lc *= 0.72;
            if (d > cR + eN - 1.2) lc *= 0.88;
            col = lc; drawn = 1.0;
            if (tp == 1 && ph > 0.82) col = vec3(1.0, 0.96, 0.98);
        }
    }

    outCol = col;
    return drawn;
}

#endif

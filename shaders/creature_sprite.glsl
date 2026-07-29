// Procedural creature coverage — the single source of truth for subworld fauna
// billboards, keyed by body-plan archetype + tint + seed. BOTH the lit pass
// (creature.frag) and the depth-only shadow caster (shadow_creature.frag) call
// it, so every creature casts its REAL silhouette with no bespoke shadow code.
//
// This is the PROCEDURAL DEFAULT. A drawn atlas/image sprite overrides it later
// (the universal sprite resolver) with no engine change — the same
// "coverage -> shadow" contract holds for an arbitrary painted sprite.
//
// uv.x across [0,1], uv.y up [0,1] (0 = feet, 1 = crown).
// Archetypes: 0 quadruped, 1 avian, 2 serpent, 3 biped, 4 undead, 5 hulk,
// 6 critter. tint is the creature's base colour (fauna table `color`); seed
// gives per-instance variation. Adding a creature never touches this file
// unless it introduces a genuinely new body plan.
#ifndef CREATURE_SPRITE_GLSL
#define CREATURE_SPRITE_GLSL

float chash(float n) {
    n = fract(n * 0.1031);
    n *= n + 33.33;
    n *= n + n;
    return fract(n);
}

// Returns coverage (0 = transparent, 1 = opaque); writes the pixel colour.
float creatureCoverage(vec2 uv, float archf, float seed, vec3 tint, out vec3 outCol) {
    int a = int(archf + 0.5);
    float x = uv.x;
    float y = uv.y;
    float cx = x - 0.5;

    vec3 body  = clamp(tint, 0.0, 1.0);
    vec3 dark  = body * 0.62;
    vec3 lite  = clamp(body * 1.28, 0.0, 1.0);
    vec3 belly = clamp(mix(body, vec3(0.82), 0.18), 0.0, 1.0);

    // Small deterministic edge wobble so silhouettes are not billiard-smooth.
    float n = (chash(seed + floor(x * 22.0) * 1.7 + floor(y * 22.0) * 5.3) - 0.5);

    vec3 col = vec3(0.0);
    float cov = 0.0;

    if (a == 0) {
        // QUADRUPED — side profile facing +x (rabbit / wolf / bear / boar / croc).
        vec2 d = (vec2(x, y) - vec2(0.44, 0.52)) / vec2(0.30, 0.17);
        if (dot(d, d) <= 1.0 + n * 0.12) {
            col = (y > 0.55) ? lite : ((y < 0.46) ? belly : body);
            cov = 1.0;
        }
        vec2 hd = (vec2(x, y) - vec2(0.75, 0.60)) / vec2(0.13, 0.14);
        if (dot(hd, hd) <= 1.0 + n * 0.10) { col = body; cov = 1.0; }        // head
        if (x > 0.82 && x < 0.95 && y > 0.52 && y < 0.62) { col = dark; cov = 1.0; } // snout
        if (x > 0.68 && x < 0.76 && y > 0.72 && y < 0.82) { col = body; cov = 1.0; } // ear
        if (y > 0.02 && y < 0.34 &&                                          // legs
            (abs(x - 0.28) < 0.035 || abs(x - 0.42) < 0.035 ||
             abs(x - 0.56) < 0.035 || abs(x - 0.66) < 0.035)) { col = dark; cov = 1.0; }
        if (x > 0.09 && x < 0.18 && y > 0.50 && y < 0.66) { col = body; cov = 1.0; } // tail
    } else if (a == 1) {
        // AVIAN — compact body, drooping wings (hawk / eagle).
        vec2 d = (vec2(x, y) - vec2(0.5, 0.46)) / vec2(0.15, 0.20);
        if (dot(d, d) <= 1.0 + n * 0.12) { col = (y > 0.5) ? lite : belly; cov = 1.0; }
        if (length(vec2(x, y) - vec2(0.5, 0.72)) <= 0.11 + n * 0.05) { col = body; cov = 1.0; } // head
        if (y > 0.68 && y < 0.75 && x > 0.60 && x < 0.71) { col = vec3(0.90, 0.70, 0.20); cov = 1.0; } // beak
        float wy = 0.50 - abs(cx) * 0.55;                                    // wings droop outward
        if (abs(cx) > 0.11 && abs(cx) < 0.42 && y > wy - 0.07 && y < wy + 0.10) { col = dark; cov = 1.0; }
        if (y > 0.10 && y < 0.30 && abs(cx) < 0.06) { col = dark; cov = 1.0; }              // tail
        if (y > 0.02 && y < 0.16 && (abs(cx + 0.04) < 0.02 || abs(cx - 0.04) < 0.02)) {
            col = vec3(0.80, 0.60, 0.20); cov = 1.0;                                         // legs
        }
    } else if (a == 2) {
        // SERPENT — sinuous vertical body (snake).
        float sway = sin(y * 6.2 + seed * 3.1) * 0.16 * (0.35 + 0.65 * y);
        float xc = 0.5 + sway;
        float th = 0.075 + 0.02 * sin(y * 4.0 + seed);
        if (y > 0.04 && y < 0.86 && abs(x - xc) < th + n * 0.02) {
            col = (x < xc) ? belly : lite; cov = 1.0;
        }
        float hx = 0.5 + sin(0.9 * 6.2 + seed * 3.1) * 0.16 * 0.95;
        if (length((vec2(x, y) - vec2(hx, 0.88)) / vec2(1.1, 0.9)) <= 0.10 + n * 0.04) { col = body; cov = 1.0; }
    } else if (a == 3 || a == 4) {
        // BIPED (3) / UNDEAD (4) — upright humanoid monster. Undead is thin,
        // bony, with ribs + eye sockets; biped is bulky with optional horns.
        bool  undead   = (a == 4);
        float legHalf  = undead ? 0.025 : 0.05;
        float torsoH   = undead ? 0.07  : 0.21;
        float armInner = undead ? 0.09  : 0.22;
        float armOuter = undead ? 0.17  : 0.32;
        if (y < 0.40 && y > 0.02 &&                                          // legs
            (abs(cx + 0.10) < legHalf || abs(cx - 0.10) < legHalf)) { col = undead ? body : dark; cov = 1.0; }
        if (!undead && y < 0.08 && abs(cx) < 0.22 && abs(cx) > 0.03) { col = dark * 0.8; cov = 1.0; } // feet
        if (y >= 0.38 && y < 0.68 && abs(cx) < torsoH) { col = (y > 0.58) ? lite : body; cov = 1.0; } // torso
        if (undead && y >= 0.42 && y < 0.64 && abs(cx) < 0.15 && fract(y * 9.0) < 0.42) {
            col = lite; cov = 1.0;                                            // ribs
        }
        if (y >= 0.40 && y < 0.66 && abs(cx) >= armInner && abs(cx) < armOuter) { col = body * 0.9; cov = 1.0; } // arms
        if (y >= 0.36 && y < 0.44 && abs(cx) >= armInner && abs(cx) < armOuter + 0.02) { col = dark; cov = 1.0; } // fists
        vec2 hd = (vec2(x, y) - vec2(0.5, 0.80)) / vec2(undead ? 0.12 : 0.15, undead ? 0.13 : 0.14);
        if (dot(hd, hd) <= 1.0 + n * 0.08) {                                 // head / skull
            col = undead ? lite : body;
            if (undead && (length(vec2(x, y) - vec2(0.455, 0.81)) < 0.028 ||
                           length(vec2(x, y) - vec2(0.545, 0.81)) < 0.028)) col = vec3(0.05); // eye sockets
            cov = 1.0;
        }
        if (!undead && chash(seed + 9.0) > 0.45 && y > 0.86 && y < 0.99 &&    // horns (demonic)
            (abs(cx + 0.10) < 0.03 || abs(cx - 0.10) < 0.03)) { col = dark; cov = 1.0; }
    } else if (a == 5) {
        // HULK — massive blocky golem with rocky cracks.
        if (y < 0.34 && y > 0.0 && (abs(cx + 0.14) < 0.10 || abs(cx - 0.14) < 0.10)) { col = dark; cov = 1.0; } // legs
        if (y >= 0.30 && y < 0.72 && abs(cx) < 0.30) { col = body; cov = 1.0; }                                 // torso
        if (y >= 0.34 && y < 0.70 && abs(cx) >= 0.30 && abs(cx) < 0.45) { col = body * 0.9; cov = 1.0; }        // arms
        if (y >= 0.72 && y < 0.90 && abs(cx) < 0.13) { col = body; cov = 1.0; }                                 // head
        if (cov > 0.5) {
            float cr = chash(floor(x * 12.0) + floor(y * 12.0) * 9.0 + seed);
            if (cr > 0.85) col *= 0.55; else if (cr < 0.12) col = lite;      // cracks / highlights
        }
    } else {
        // CRITTER — tiny squat blob with big eyes (frog).
        vec2 d = (vec2(x, y) - vec2(0.5, 0.30)) / vec2(0.27, 0.23);
        if (dot(d, d) <= 1.0 + n * 0.12) { col = (y > 0.30) ? lite : belly; cov = 1.0; }
        if (length(vec2(x, y) - vec2(0.40, 0.47)) < 0.075 ||
            length(vec2(x, y) - vec2(0.60, 0.47)) < 0.075) { col = body; cov = 1.0; }   // eye bumps
        if (length(vec2(x, y) - vec2(0.40, 0.48)) < 0.03 ||
            length(vec2(x, y) - vec2(0.60, 0.48)) < 0.03) { col = vec3(0.05); cov = 1.0; } // pupils
        if (y < 0.12 && (abs(cx + 0.20) < 0.06 || abs(cx - 0.20) < 0.06)) { col = dark; cov = 1.0; } // feet
    }

    outCol = col;
    return cov;
}

#endif

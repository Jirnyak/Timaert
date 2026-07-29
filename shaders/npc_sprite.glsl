// NPC sprite coverage — the single source of truth for the procedural paper-doll
// humanoid, keyed by seed. BOTH the lit pass (npc.frag) and the depth-only
// shadow caster (shadow_npc.frag) call this, so an NPC casts a real-silhouette
// shadow with no bespoke shadow code. In the game this is replaced by an atlas
// alpha sample -- the same universal "coverage -> shadow" contract holds for any
// arbitrary NPC / mob sprite.
#ifndef NPC_SPRITE_GLSL
#define NPC_SPRITE_GLSL

float nhash(float n) {
    n = fract(n * 0.1031);
    n *= n + 33.33;
    n *= n + n;
    return fract(n);
}

// uv: x across [0,1], y up [0,1] (0 = feet). Returns coverage; outCol = colour.
float npcCoverage(vec2 uv, float seed, out vec3 outCol) {
    float s = seed;
    vec3 skin  = vec3(0.86, 0.68, 0.54) * mix(0.78, 1.06, nhash(s + 1.0));
    vec3 shirt = clamp(vec3(nhash(s + 2.0), nhash(s + 3.0), nhash(s + 4.0))
                       * 0.7 + 0.12, 0.0, 1.0);
    vec3 pants = vec3(0.20, 0.22, 0.30) * mix(0.7, 1.5, nhash(s + 5.0));
    vec3 hair  = vec3(0.26, 0.17, 0.09) * mix(0.5, 1.6, nhash(s + 6.0));

    float x = uv.x, y = uv.y;
    float cx = abs(x - 0.5);
    vec3 col = vec3(0.0);
    float a = 0.0;

    if (y < 0.42 && cx < 0.20 && cx > 0.03) { col = pants; a = 1.0; }        // legs
    if (y < 0.08 && cx < 0.20 && cx > 0.03) { col = pants * 0.5; a = 1.0; }  // boots
    if (y >= 0.40 && y < 0.70 && cx < 0.19) { col = shirt; a = 1.0; }        // torso
    if (y >= 0.42 && y < 0.68 && cx >= 0.19 && cx < 0.29) { col = shirt * 0.88; a = 1.0; } // arms
    if (y >= 0.40 && y < 0.47 && cx >= 0.19 && cx < 0.29) { col = skin; a = 1.0; }         // hands
    vec2 hd = vec2(x - 0.5, y - 0.82) / vec2(0.15, 0.13);
    if (dot(hd, hd) <= 1.0) { // head
        col = skin;
        if (hd.y < -0.15 || cx > 0.10) col = hair;
        a = 1.0;
    }

    outCol = col;
    return a;
}

#endif

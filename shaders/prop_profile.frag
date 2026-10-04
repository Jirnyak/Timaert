#version 450
#extension GL_GOOGLE_include_directive : require
// A PROP WITH A SHAPE FROM THE TABLE — fragment stage.
//
// TWO SURFACES, ONE NUMBER. Below the row's `bark_top` the body is trunk,
// above it crown — the same way a house splits wall from roof by its local
// height (struct.frag) rather than by carrying two materials. One number in
// the CSV, no branch in the engine, and a shape that wants no split simply
// puts the line at 0 or 1.
//
// WHERE THE COLOURS COME FROM, and the split is by QUESTION. Bark exists only
// within arm's reach, so it is the prop row's own colour. The CROWN is read at
// two distances — this tree and the far canopy are one wood — so its colour is
// the cover row `canopy`, the very pair far_canopy.frag paints the horizon
// with. Neither can drift from the other, because there is one of each.
#include "prop_profiles.glsl"
#include "ground_surface.glsl"
#include "leaf_sprite.glsl"
#include "shadow_common.glsl"
#include "lighting.glsl"

layout(set = 0, binding = 0) uniform sampler2DShadow u_shadow;
layout(set = 0, binding = 3) uniform sampler2DShadow u_shadowFar;

layout(location = 0) in vec3  vNormal;
layout(location = 1) in vec3  vWorld;
layout(location = 2) in float vLocalY;
layout(location = 3) flat in uint vProfile;
layout(location = 4) flat in uint vSeed;
layout(location = 5) in vec4  vLightClip;
layout(location = 6) in vec2  vLeafUv;
layout(location = 7) in float vIsLeaf;

layout(push_constant) uniform Push {
    mat4 mvp;
    vec4 sunDir;
    vec4 sunColor;
    vec4 ambient;
    mat4 lightMvp;
    vec4 camPos;
    vec4 shore;
} pc;

layout(location = 0) out vec4 outColor;

float hash_u(uint h) {
    h ^= h >> 15; h *= 0x2c1b3c6du; h ^= h >> 12; h *= 0x297a2d39u;
    h ^= h >> 15;
    return float(h & 0x00ffffffu) / float(0x00ffffff);
}

void main() {
    uint prof = min(vProfile, kProfileCount - 1u);

    // A LEAF CARD IS MOSTLY HOLES, and that is the whole of why foliage is
    // cards: the silhouette is cut per fragment instead of being modelled.
    // The clump's seed mixes the instance with the card's own height, so two
    // tiers of one tree are not the same stencil twice.
    if (vIsLeaf > 0.5) {
        uint clumpSeed = vSeed ^ (uint(vLocalY * 255.0) * 2654435761u);
        if (leaf_patch(vLeafUv, clumpSeed) < 0.5) discard;
    }

    // THE CROWN'S OWN PAIR, mixed per instance rather than per fragment: one
    // wood is not one green, and the seed is what the owner asked a tree to
    // vary by. Bounded by construction — every colour a crown can show lies
    // between the two the row authored, exactly as the ground's law works, so
    // no seed can invent a hue nobody chose.
    float wear = hash_u(vSeed ^ 0x9e3779b9u);
    vec3 crown = mix(kCoverFresh[kCoverCanopy], kCoverWorn[kCoverCanopy], wear);
    vec3 bark  = kProfileBark[prof];
    // Smooth over a hand's width of the height rather than a hard line: a
    // crown does not begin on one ring, and the step would read as a painted
    // band at exactly the distance the tree is biggest on screen.
    float top  = kProfileBarkTop[prof];
    // WHICH OF THE TWO THIS FRAGMENT IS is a fact of the GEOMETRY now, not a
    // guess from height: a card is foliage wherever it stands, and the trunk
    // is bark all the way up. `bark_top` stays for props whose body itself
    // changes material partway (a post with a painted cap), which is the same
    // idiom a house uses for its roof.
    vec3 base  = vIsLeaf > 0.5
               ? crown
               : mix(bark, crown, smoothstep(top - 0.04, top + 0.04, vLocalY));

    vec3  n   = normalize(vNormal);
    float ndl = max(dot(n, normalize(pc.sunDir.xyz)), 0.0);
    float sh  = shadowFactorHandoff(u_shadow, u_shadowFar, vLightClip,
                                    far_light_clip(vWorld), ndl,
                                    TIMAERT_SHADOW_SPREAD_MESH);
    vec3 col = lit_surface(base, pc.ambient.rgb, pc.sunColor.rgb, ndl,
                           sh, vWorld);
    col += point_lights(vWorld, n);
    outColor = vec4(aerial_perspective(col, vWorld), 1.0);
}

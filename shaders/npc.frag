#version 450
#extension GL_GOOGLE_include_directive : require
// Instanced paper-doll NPC billboard fragment stage.
//
// This shader spent a while as the ONE lit pass that was not lit. The
// composition of the paper-doll moved to the GPU (7cd71e2) and the include of
// lighting.glsl went with it, so people glowed at full daylight brightness at
// midnight, took no shadow, and stood untouched inside the pool of the torch
// they were themselves carrying. The vertex stage never stopped handing over
// vWorld and vLightClip — both outputs simply had no reader.
//
// Lit exactly like its sibling billboard (creature.frag): the shadow map is
// sampled once at the FEET and applied flat across the quad, because a
// camera-facing card has no normal to self-shadow with, and the positional
// lights come in through the flat form of the same universal function. One
// lit_surface() for every lit pass is the rule (src/sub/lighting.h).
#include "shadow_common.glsl"
#include "lighting.glsl"

layout(location = 0) in vec2 vUv;
layout(location = 1) flat in uint vDescIndex;
layout(location = 2) flat in uint vAnim;
layout(location = 3) flat in vec4 vLightClip;   // feet in light space
layout(location = 4) in vec3 vWorld;            // world pos (point lights)

layout(location = 0) out vec4 fColor;

// The shared lighting set: shadow map + the frame's point-light buffer. Bound
// by the draw already (vk_renderer_3d.cpp binds litSet at set 0 for this pass).
layout(set = 0, binding = 0) uniform sampler2D u_shadow;

// Must match npc.vert byte for byte — one range, both stages.
layout(push_constant) uniform Push {
    mat4 mvp;
    vec4 camRight;
    vec4 sunColor;
    vec4 ambient;
    mat4 lightMvp;
} pc;

layout(set = 1, binding = 0) uniform sampler2D uAtlas;

struct GpuAtlasEntry {
    uint uv_wh;
    uint ox_oy;
    uint pad1;
    uint pad2;
};
layout(std430, set = 1, binding = 1) readonly buffer Entries {
    GpuAtlasEntry gpuEntries_[];
};

layout(std430, set = 1, binding = 2) readonly buffer Ordinals {
    int gpuSheetOrdinals_[];
};

struct GpuCharacterDescriptor {
    uint sprites[10];
    uint hiddenMask[2];
    uint paletteRows[6];
    uint pad[2];
};
layout(std430, set = 1, binding = 3) readonly buffer Descriptors {
    GpuCharacterDescriptor gpuDescriptors_[];
};

const vec4 kBasePalettes[36][4] = {
    { vec4(0.255, 0.075, 0.063, 1.0), vec4(0.357, 0.086, 0.063, 1.0), vec4(0.545, 0.106, 0.086, 1.0), vec4(0.698, 0.173, 0.180, 1.0) },
    { vec4(0.255, 0.055, 0.122, 1.0), vec4(0.384, 0.090, 0.173, 1.0), vec4(0.565, 0.090, 0.224, 1.0), vec4(0.745, 0.235, 0.275, 1.0) },
    { vec4(0.325, 0.090, 0.027, 1.0), vec4(0.455, 0.090, 0.024, 1.0), vec4(0.620, 0.129, 0.031, 1.0), vec4(0.859, 0.294, 0.271, 1.0) },
    { vec4(0.384, 0.153, 0.000, 1.0), vec4(0.514, 0.216, 0.000, 1.0), vec4(0.647, 0.306, 0.039, 1.0), vec4(0.827, 0.439, 0.020, 1.0) },
    { vec4(0.384, 0.153, 0.000, 1.0), vec4(0.541, 0.247, 0.055, 1.0), vec4(0.710, 0.392, 0.063, 1.0), vec4(0.820, 0.549, 0.027, 1.0) },
    { vec4(0.271, 0.129, 0.004, 1.0), vec4(0.420, 0.224, 0.102, 1.0), vec4(0.529, 0.337, 0.078, 1.0), vec4(0.686, 0.459, 0.251, 1.0) },
    { vec4(0.318, 0.212, 0.004, 1.0), vec4(0.545, 0.361, 0.000, 1.0), vec4(0.627, 0.478, 0.004, 1.0), vec4(0.784, 0.604, 0.031, 1.0) },
    { vec4(0.471, 0.306, 0.059, 1.0), vec4(0.659, 0.498, 0.125, 1.0), vec4(0.847, 0.690, 0.224, 1.0), vec4(0.933, 0.843, 0.404, 1.0) },
    { vec4(0.341, 0.302, 0.055, 1.0), vec4(0.416, 0.463, 0.075, 1.0), vec4(0.455, 0.651, 0.180, 1.0), vec4(0.631, 0.812, 0.298, 1.0) },
    { vec4(0.212, 0.188, 0.031, 1.0), vec4(0.302, 0.267, 0.118, 1.0), vec4(0.478, 0.424, 0.216, 1.0), vec4(0.725, 0.541, 0.267, 1.0) },
    { vec4(0.184, 0.106, 0.047, 1.0), vec4(0.208, 0.188, 0.067, 1.0), vec4(0.376, 0.365, 0.125, 1.0), vec4(0.557, 0.514, 0.212, 1.0) },
    { vec4(0.165, 0.235, 0.063, 1.0), vec4(0.184, 0.357, 0.043, 1.0), vec4(0.251, 0.486, 0.031, 1.0), vec4(0.404, 0.643, 0.145, 1.0) },
    { vec4(0.031, 0.122, 0.043, 1.0), vec4(0.047, 0.173, 0.102, 1.0), vec4(0.086, 0.259, 0.184, 1.0), vec4(0.204, 0.443, 0.329, 1.0) },
    { vec4(0.071, 0.271, 0.106, 1.0), vec4(0.114, 0.443, 0.341, 1.0), vec4(0.125, 0.557, 0.584, 1.0), vec4(0.247, 0.690, 0.714, 1.0) },
    { vec4(0.082, 0.314, 0.161, 1.0), vec4(0.157, 0.478, 0.408, 1.0), vec4(0.275, 0.620, 0.557, 1.0), vec4(0.392, 0.757, 0.671, 1.0) },
    { vec4(0.118, 0.235, 0.302, 1.0), vec4(0.082, 0.345, 0.431, 1.0), vec4(0.145, 0.486, 0.502, 1.0), vec4(0.263, 0.753, 0.765, 1.0) },
    { vec4(0.110, 0.157, 0.282, 1.0), vec4(0.090, 0.169, 0.373, 1.0), vec4(0.145, 0.235, 0.486, 1.0), vec4(0.184, 0.322, 0.698, 1.0) },
    { vec4(0.047, 0.231, 0.373, 1.0), vec4(0.078, 0.298, 0.545, 1.0), vec4(0.110, 0.400, 0.612, 1.0), vec4(0.263, 0.533, 0.827, 1.0) },
    { vec4(0.106, 0.188, 0.388, 1.0), vec4(0.235, 0.208, 0.518, 1.0), vec4(0.310, 0.318, 0.667, 1.0), vec4(0.412, 0.439, 0.796, 1.0) },
    { vec4(0.090, 0.129, 0.227, 1.0), vec4(0.165, 0.169, 0.373, 1.0), vec4(0.224, 0.204, 0.443, 1.0), vec4(0.306, 0.345, 0.671, 1.0) },
    { vec4(0.141, 0.071, 0.259, 1.0), vec4(0.212, 0.098, 0.427, 1.0), vec4(0.341, 0.173, 0.549, 1.0), vec4(0.467, 0.275, 0.737, 1.0) },
    { vec4(0.153, 0.063, 0.235, 1.0), vec4(0.263, 0.106, 0.294, 1.0), vec4(0.435, 0.192, 0.447, 1.0), vec4(0.580, 0.294, 0.604, 1.0) },
    { vec4(0.357, 0.078, 0.373, 1.0), vec4(0.486, 0.157, 0.408, 1.0), vec4(0.686, 0.224, 0.486, 1.0), vec4(0.816, 0.357, 0.706, 1.0) },
    { vec4(0.443, 0.094, 0.282, 1.0), vec4(0.584, 0.141, 0.322, 1.0), vec4(0.757, 0.220, 0.337, 1.0), vec4(0.886, 0.376, 0.584, 1.0) },
    { vec4(0.404, 0.106, 0.220, 1.0), vec4(0.529, 0.212, 0.239, 1.0), vec4(0.694, 0.365, 0.365, 1.0), vec4(0.839, 0.490, 0.522, 1.0) },
    { vec4(0.286, 0.298, 0.325, 1.0), vec4(0.420, 0.439, 0.514, 1.0), vec4(0.518, 0.525, 0.659, 1.0), vec4(0.698, 0.682, 0.788, 1.0) },
    { vec4(0.373, 0.302, 0.318, 1.0), vec4(0.561, 0.478, 0.467, 1.0), vec4(0.667, 0.616, 0.600, 1.0), vec4(0.780, 0.722, 0.706, 1.0) },
    { vec4(0.337, 0.349, 0.376, 1.0), vec4(0.502, 0.518, 0.592, 1.0), vec4(0.620, 0.627, 0.761, 1.0), vec4(0.757, 0.769, 0.851, 1.0) },
    { vec4(0.361, 0.341, 0.392, 1.0), vec4(0.620, 0.592, 0.714, 1.0), vec4(0.827, 0.796, 0.949, 1.0), vec4(0.898, 0.871, 0.937, 1.0) },
    { vec4(0.090, 0.071, 0.071, 1.0), vec4(0.098, 0.098, 0.118, 1.0), vec4(0.114, 0.114, 0.133, 1.0), vec4(0.227, 0.176, 0.220, 1.0) },
    { vec4(0.059, 0.047, 0.039, 1.0), vec4(0.075, 0.075, 0.086, 1.0), vec4(0.161, 0.118, 0.176, 1.0), vec4(0.333, 0.157, 0.333, 1.0) },
    { vec4(0.043, 0.039, 0.059, 1.0), vec4(0.071, 0.071, 0.118, 1.0), vec4(0.106, 0.125, 0.188, 1.0), vec4(0.110, 0.263, 0.439, 1.0) },
    { vec4(0.204, 0.125, 0.110, 1.0), vec4(0.302, 0.153, 0.153, 1.0), vec4(0.427, 0.239, 0.180, 1.0), vec4(0.561, 0.318, 0.259, 1.0) },
    { vec4(0.102, 0.055, 0.012, 1.0), vec4(0.161, 0.086, 0.024, 1.0), vec4(0.208, 0.110, 0.035, 1.0), vec4(0.357, 0.247, 0.145, 1.0) },
    { vec4(0.102, 0.055, 0.012, 1.0), vec4(0.184, 0.059, 0.008, 1.0), vec4(0.239, 0.067, 0.016, 1.0), vec4(0.427, 0.173, 0.024, 1.0) },
    { vec4(0.267, 0.157, 0.106, 1.0), vec4(0.384, 0.251, 0.192, 1.0), vec4(0.592, 0.420, 0.341, 1.0), vec4(0.796, 0.580, 0.486, 1.0) },
};
const vec4 kSkintonePalettes[8][4] = {
    { vec4(0.514, 0.271, 0.271, 1.0), vec4(0.925, 0.620, 0.620, 1.0), vec4(0.984, 0.765, 0.765, 1.0), vec4(0.965, 0.867, 0.839, 1.0) },
    { vec4(0.529, 0.302, 0.212, 1.0), vec4(0.886, 0.671, 0.514, 1.0), vec4(0.941, 0.800, 0.706, 1.0), vec4(0.965, 0.886, 0.839, 1.0) },
    { vec4(0.424, 0.247, 0.165, 1.0), vec4(0.914, 0.604, 0.478, 1.0), vec4(0.973, 0.706, 0.616, 1.0), vec4(0.973, 0.796, 0.757, 1.0) },
    { vec4(0.396, 0.224, 0.145, 1.0), vec4(0.725, 0.447, 0.294, 1.0), vec4(0.847, 0.596, 0.400, 1.0), vec4(0.914, 0.714, 0.537, 1.0) },
    { vec4(0.396, 0.220, 0.114, 1.0), vec4(0.682, 0.467, 0.196, 1.0), vec4(0.804, 0.588, 0.353, 1.0), vec4(0.867, 0.698, 0.467, 1.0) },
    { vec4(0.353, 0.173, 0.090, 1.0), vec4(0.561, 0.337, 0.161, 1.0), vec4(0.647, 0.439, 0.278, 1.0), vec4(0.761, 0.565, 0.408, 1.0) },
    { vec4(0.235, 0.110, 0.047, 1.0), vec4(0.384, 0.184, 0.078, 1.0), vec4(0.471, 0.255, 0.141, 1.0), vec4(0.573, 0.337, 0.220, 1.0) },
    { vec4(0.133, 0.035, 0.035, 1.0), vec4(0.263, 0.071, 0.125, 1.0), vec4(0.322, 0.137, 0.173, 1.0), vec4(0.427, 0.216, 0.251, 1.0) },
};
const vec4 kToolPalettes[8][4] = {
    { vec4(0.224, 0.110, 0.075, 1.0), vec4(0.361, 0.173, 0.125, 1.0), vec4(0.502, 0.286, 0.196, 1.0), vec4(0.671, 0.443, 0.322, 1.0) },
    { vec4(0.231, 0.263, 0.290, 1.0), vec4(0.294, 0.361, 0.380, 1.0), vec4(0.412, 0.498, 0.533, 1.0), vec4(0.616, 0.714, 0.761, 1.0) },
    { vec4(0.361, 0.180, 0.086, 1.0), vec4(0.533, 0.286, 0.157, 1.0), vec4(0.690, 0.424, 0.212, 1.0), vec4(0.812, 0.549, 0.298, 1.0) },
    { vec4(0.294, 0.275, 0.267, 1.0), vec4(0.431, 0.416, 0.412, 1.0), vec4(0.604, 0.573, 0.565, 1.0), vec4(0.812, 0.773, 0.765, 1.0) },
    { vec4(0.396, 0.286, 0.129, 1.0), vec4(0.620, 0.439, 0.188, 1.0), vec4(0.863, 0.663, 0.239, 1.0), vec4(0.965, 0.824, 0.322, 1.0) },
    { vec4(0.094, 0.208, 0.275, 1.0), vec4(0.129, 0.322, 0.416, 1.0), vec4(0.180, 0.533, 0.604, 1.0), vec4(0.259, 0.776, 0.765, 1.0) },
    { vec4(0.173, 0.094, 0.275, 1.0), vec4(0.271, 0.129, 0.416, 1.0), vec4(0.490, 0.180, 0.604, 1.0), vec4(0.714, 0.259, 0.776, 1.0) },
    { vec4(0.282, 0.071, 0.180, 1.0), vec4(0.576, 0.137, 0.314, 1.0), vec4(0.741, 0.216, 0.498, 1.0), vec4(1.000, 0.431, 0.682, 1.0) },
};


const int kRenderOrders[4][37] = {
    { 33, 31, 26, 27, 1, 20, 21, 7, 8, 23, 29, 0, 18, 17, 3, 5, 4, 15, 25, 2, 6, 16, 19, 13, 28, 22, 9, 12, 14, 11, 24, 10, 32, 30, 35, 34, 36 },
    { 33, 31, 35, 34, 36, 11, 22, 20, 21, 1, 23, 9, 7, 29, 0, 2, 18, 17, 6, 16, 19, 3, 5, 4, 15, 25, 12, 14, 13, 28, 8, 24, 10, 27, 26, 32, 30 },
    { 33, 31, 1, 20, 21, 7, 8, 23, 29, 27, 0, 18, 17, 3, 5, 4, 15, 25, 2, 6, 16, 19, 11, 13, 28, 26, 22, 9, 12, 14, 24, 10, 32, 30, 35, 34, 36 },
    { 33, 31, 1, 20, 21, 7, 8, 23, 29, 27, 0, 18, 17, 3, 5, 4, 15, 25, 2, 6, 16, 19, 11, 13, 28, 26, 22, 9, 12, 14, 24, 10, 32, 30, 35, 34, 36 },
};

const int kCategoryPaletteSlot[38] = {
    10, 10, 10, 4, 4, 11, 11, 6, 6, 6, 6, 0, 1, 2, 3, 7, 7, 8, 9, 18, 5, 14, 15, 10, 16, 17, 13, 13, 12, 12, 19, 19, 20, 20, 21, 22, 23, 0
};
const int kCategoryPaletteType[38] = {
    1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 2, 2, 2, 2, 2, 2, 2, 0
};
const int kCategoryPaletteCount[38] = {
    4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 3, 3, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4
};

vec4 apply_palette(int category, vec4 color, uint paletteRows[6]) {
    int slot = kCategoryPaletteSlot[category];
    int type = kCategoryPaletteType[category];
    int row = int((paletteRows[slot / 4] >> ((slot % 4) * 8)) & 0xFFu);

    vec4 grays[4] = vec4[4](
        vec4(0.0, 0.0, 0.0, 1.0),
        vec4(0.25, 0.25, 0.25, 1.0),
        vec4(0.5, 0.5, 0.5, 1.0),
        vec4(1.0, 1.0, 1.0, 1.0)
    );
    int numColors = 4;

    if (slot == 10) { // Skintone
        grays[1] = vec4(0.329, 0.329, 0.329, 1.0);
        grays[2] = vec4(0.666, 0.666, 0.666, 1.0);
    } else if (slot == 8) { // Shoe
        grays[0] = vec4(0.0, 0.0, 0.0, 1.0);
        grays[1] = vec4(0.25, 0.25, 0.25, 1.0);
        grays[2] = vec4(0.5, 0.5, 0.5, 1.0);
        numColors = 3;
    } else if (slot == 9) { // Sock
        grays[0] = vec4(0.25, 0.25, 0.25, 1.0);
        grays[1] = vec4(0.5, 0.5, 0.5, 1.0);
        grays[2] = vec4(1.0, 1.0, 1.0, 1.0);
        numColors = 3;
    }

    float bestDist = 100.0;
    int bestIdx = -1;
    for (int i = 0; i < numColors; ++i) {
        float d = distance(color.rgb, grays[i].rgb);
        if (d < bestDist && d < 0.1) {
            bestDist = d;
            bestIdx = i;
        }
    }
    
    if (bestIdx == -1) return color;

    if (slot == 5) { // Eye
        if (bestIdx == 0) return vec4(1.0, 0.941, 0.968, 1.0);
        row = row % 36;
        return kBasePalettes[row][bestIdx - 1];
    }

    if (type == 1) {
        return kSkintonePalettes[row % 8][bestIdx];
    } else if (type == 2) {
        return kToolPalettes[row % 8][bestIdx];
    } else {
        return kBasePalettes[row % 36][bestIdx];
    }
}

void main() {
    uint tileIndex = vAnim >> 16;
    uint direction = vAnim & 0xFFu;

    GpuCharacterDescriptor desc = gpuDescriptors_[vDescIndex];

    vec4 finalColor = vec4(0.0);

    for (int i = 0; i < 37; ++i) {
        int category = kRenderOrders[direction % 4][i];

        uint maskWord = desc.hiddenMask[category / 32];
        if ((maskWord & (1u << (category % 32))) != 0u) continue;

        uint word = desc.sprites[category / 4];
        uint sprite = (word >> ((category % 4) * 8)) & 0xFFu;

        int ordinalIdx = category * 64 + int(sprite);
        int sheet = gpuSheetOrdinals_[ordinalIdx];
        if (sheet < 0) continue;

        uint entryIndex = uint(sheet) * 160u + tileIndex;
        GpuAtlasEntry entry = gpuEntries_[entryIndex];

        uint e_u0 = entry.uv_wh & 0xFFFFu;
        uint e_v0 = entry.uv_wh >> 16;
        uint e_w = entry.ox_oy & 0xFFFFu;
        uint e_h = entry.ox_oy >> 16;
        uint e_ox = entry.pad1 & 0xFFFFu;
        uint e_oy = entry.pad1 >> 16;

        vec2 localUv = vUv * 48.0;
        if (localUv.x < float(e_ox) || localUv.x >= float(e_ox + e_w) ||
            localUv.y < float(e_oy) || localUv.y >= float(e_oy + e_h)) {
            continue;
        }

        vec2 atlasUv = vec2(
            float(e_u0) + (localUv.x - float(e_ox)),
            float(e_v0) + (localUv.y - float(e_oy))
        );

        // Normalized UV for texture()
        // Assuming atlas size is known or we can just divide by textureSize()
        vec2 texSize = vec2(textureSize(uAtlas, 0));
        vec4 color = texture(uAtlas, atlasUv / texSize);

        if (color.a < 0.01) continue;

        color = apply_palette(category, color, desc.paletteRows);

        finalColor.rgb = mix(finalColor.rgb, color.rgb, color.a);
        finalColor.a = finalColor.a + color.a * (1.0 - finalColor.a);
    }

    if (finalColor.a < 0.01) discard;

    // Into the one lighting path, at the very end: the paper-doll is composed
    // first (palettes, layers, alpha), and the finished surface is lit like any
    // other. `base` is kept unlit so a torch adds ITS light to the body rather
    // than multiplying whatever the sun left — the same shape creature.frag uses.
    float sh = shadowFactor(u_shadow, vLightClip, 1.0);
    vec3 base = finalColor.rgb;
    finalColor.rgb = lit_surface(base, pc.ambient.rgb, pc.sunColor.rgb, 0.7, sh, vWorld);
    finalColor.rgb += base * point_lights_flat(vWorld);
    fColor = finalColor;
}

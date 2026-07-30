#version 450
layout(location = 0) in vec2 vUv;
layout(location = 1) flat in uint vDescIndex;
layout(location = 2) flat in uint vAnim;

layout(set = 0, binding = 0) uniform sampler2D uAtlas;

struct GpuAtlasEntry {
    uint uv_wh;
    uint ox_oy;
    uint pad1;
    uint pad2;
};
layout(std430, set = 0, binding = 1) readonly buffer Entries {
    GpuAtlasEntry gpuEntries_[];
};

layout(std430, set = 0, binding = 2) readonly buffer Ordinals {
    int gpuSheetOrdinals_[];
};

struct GpuCharacterDescriptor {
    uint sprites[10];
    uint hiddenMask[2];
    uint paletteRows[6];
    uint pad[2];
};
layout(std430, set = 0, binding = 3) readonly buffer Descriptors {
    GpuCharacterDescriptor gpuDescriptors_[];
};

void main() {
    uint tileIndex = vAnim >> 16;
    GpuCharacterDescriptor desc = gpuDescriptors_[vDescIndex];

    vec2 texSize = vec2(textureSize(uAtlas, 0));

    // For shadow, order doesn't matter, just find any opaque pixel.
    for (int category = 0; category < 37; ++category) {
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
        // The shadow pass has an inverted Y because the billboard is laid flat or something?
        // Wait, the original shadow_npc.frag had:
        // vec2(vUv.x, 1.0 - vUv.y)
        // We must preserve this 1.0 - vUv.y inversion!
        vec2 flippedUv = vec2(localUv.x, 48.0 - localUv.y);
        
        if (flippedUv.x < float(e_ox) || flippedUv.x >= float(e_ox + e_w) ||
            flippedUv.y < float(e_oy) || flippedUv.y >= float(e_oy + e_h)) {
            continue;
        }

        vec2 atlasUv = vec2(
            float(e_u0) + (flippedUv.x - float(e_ox)),
            float(e_v0) + (flippedUv.y - float(e_oy))
        );

        float alpha = texture(uAtlas, atlasUv / texSize).a;
        if (alpha >= 0.25) {
            return; // keep pixel!
        }
    }
    
    discard;
}

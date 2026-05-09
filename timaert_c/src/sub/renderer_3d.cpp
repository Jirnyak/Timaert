#include "sub/renderer_3d.h"
#include "sub/lighting.h"
#include "sub/base_generator.h"
#include "gl/helpers.h"
#include "macro/state.h"
#include <vector>
#include <cmath>

namespace sm::sub {

// Heightmap covers kFullSize × kFullSize tiles. **TS-faithful physical
// scale**: TS uses 1 world unit per tile, with CELL_SIZE=1024 → cell is
// 1024 units wide and HEIGHT_SCALE=500 → ratio 0.49. Mirror that here
// with kTileMeters=1.0, so a 3072-tile world spans 3072 m and a single
// cell is 1024 m × 1024 m. The 192² mesh therefore has 16 m quads (16
// tiles per quad).
static constexpr float kTileMeters  = 1.0f;
static constexpr float kWorldExtent = float(kFullSize) * kTileMeters * 0.5f; // = 1536 m
// Vertical exaggeration. **TS-anchored** — `subworld/camera.ts` HEIGHT_SCALE
// = 500 against a single 1024-unit cell mesh (TS only renders the centre
// cell). C++ renders the full 3×3 grid (3072 m wide) for far-view fog,
// which would make slopes look 3× shallower at the bare TS scale. Scale
// kHeightScale proportionally — 500 × (3072 / 1024) = 1500 — so peaks,
// shorelines, and rolling terrain present the same apparent steepness as
// TS. Heightmap stays absolute 0..1 (mountain-wall cells reach 2.0); a
// plain mountain peak (h = 1.0) renders at 1500 m, real mountain
// proportions for a 1024 m-wide cell.
static constexpr float kHeightScale = 1500.0f;
// Distance fog — proportional to the new 3072 m world extent.
static constexpr float kFogStart    = 800.0f;
static constexpr float kFogEnd      = 2800.0f;

static const char* kVS = R"(#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNrm;
out vec3 vN;
out float vH;
out vec3 vWorldPos;
out vec2 vCellUV;     // 0..3 across the 3x3 composite (xz plane)
out vec2 vTileUV;     // continuous UV used to sample the atlas
uniform mat4 uVP;
uniform float uExtent;
uniform float uTileScale;   // atlas tiles repeated across the world
void main() {
    vN = aNrm;
    vH = aPos.y;
    vWorldPos = aPos;
    vec2 norm = aPos.xz / uExtent * 0.5 + 0.5;   // 0..1 over composite
    vCellUV   = norm * 3.0;                       // 0..3
    vTileUV   = norm * uTileScale;                // tile repeat
    gl_Position = uVP * vec4(aPos, 1.0);
}
)";

// Fragment shader — TS-faithful tile atlas sampling. Each of the 3x3
// macro cells contributes its own per-biome procedural texture; we sample
// all four nearest cells and bilinearly blend by the cell-grid fractional
// position. No biome ever shows up as a flat colour — the atlas tile
// (gen_tundra/gen_desert/gen_swamp/...) supplies real per-pixel detail.
//
// Slope-driven rock/snow overlay sits ON TOP of the biome — flat terrain
// stays its biome colour, only steep mountainsides expose rock and snow.
static const char* kFS = R"(#version 330 core
in vec3 vN;
in float vH;
in vec3 vWorldPos;
in vec2 vCellUV;
in vec2 vTileUV;
out vec4 frag;
uniform vec3      uSunDir;
uniform vec3      uSunCol;
uniform float     uIntensity;
uniform sampler2D uAtlas;        // 64*kCount x 64
uniform int       uBiomeIdx[9];  // row-major 3x3, atlas column per cell
uniform float     uAtlasInvCount; // 1.0 / kTileCount
uniform sampler2D uRoadMask;     // R8 mask covering the 3x3 composite
uniform vec3      uCamPos;
uniform vec3      uFogColor;
uniform float     uFogStart;
uniform float     uFogEnd;

vec3 sampleBiome(int biomeIdx, vec2 t) {
    // Atlas layout: each tile is 64x64 wide, packed left-to-right.
    // Half-pixel inset on the U axis prevents LINEAR filtering from
    // bleeding into the neighbouring biome's tile at fract() wrap-around.
    const float kInset = 0.5 / 64.0;
    float fx = clamp(fract(t.x), kInset, 1.0 - kInset);
    float u = (float(biomeIdx) + fx) * uAtlasInvCount;
    return texture(uAtlas, vec2(u, fract(t.y))).rgb;
}

void main() {
    vec3 n  = normalize(vN);
    float ndl = max(0.0, dot(n, -uSunDir));
    float band = ndl > 0.75 ? 1.0 : ndl > 0.50 ? 0.78 : ndl > 0.25 ? 0.55 : 0.36;

    // Bilinear blend across the 3x3 biome textures (vCellUV in [0..3]).
    vec2 uv  = clamp(vCellUV - 0.5, 0.0, 1.999);
    ivec2 i0 = ivec2(floor(uv));
    vec2  f  = uv - vec2(i0);
    int x0 = clamp(i0.x, 0, 2), y0 = clamp(i0.y, 0, 2);
    int x1 = clamp(x0 + 1, 0, 2), y1 = clamp(y0 + 1, 0, 2);
    vec3 c00 = sampleBiome(uBiomeIdx[y0 * 3 + x0], vTileUV);
    vec3 c10 = sampleBiome(uBiomeIdx[y0 * 3 + x1], vTileUV);
    vec3 c01 = sampleBiome(uBiomeIdx[y1 * 3 + x0], vTileUV);
    vec3 c11 = sampleBiome(uBiomeIdx[y1 * 3 + x1], vTileUV);
    vec3 ground = mix(mix(c00, c10, f.x), mix(c01, c11, f.x), f.y);

    // Road overlay — simple, clean dirt path. The road mask is binary
    // on disk; LINEAR filtering smooths it into a soft anti-aliased band.
    // We paint a single warm earth tone slightly darkened by ground so
    // the road still looks contextual on grass / steppe / desert.
    float roadM = texture(uRoadMask, vCellUV / 3.0).r;
    float roadA = smoothstep(0.30, 0.75, roadM);
    vec3  road  = vec3(0.50, 0.40, 0.27);
    road        = mix(road, ground * 0.8 + road * 0.2, 0.30);
    ground      = mix(ground, road, roadA);

    // Slope-driven rock + height-driven snow overlay. Flat low-altitude
    // terrain is left alone (biome texture, roads, shore stay fully
    // visible). Rock keys off slope only — mountainsides expose stone,
    // rolling plains do not. Snow keys off altitude only — even a flat
    // summit plate caps white. Rock colour gets a low-frequency stripe
    // noise so cliffs read as real rock instead of a flat grey wash.
    float slope    = clamp(1.0 - n.y, 0.0, 1.0);
    float rockN    = 0.5
                   + 0.5 * sin(vWorldPos.x * 0.012 + vWorldPos.z * 0.018)
                       * sin(vWorldPos.y * 0.020);
    vec3  rockCol  = mix(vec3(0.36, 0.34, 0.31),
                         vec3(0.55, 0.52, 0.47), rockN);
    vec3  snowCol  = vec3(0.94, 0.96, 0.99);
    float rockMix  = smoothstep(0.30, 0.65, slope);
    float snowMix  = smoothstep(950.0, 1300.0, vH);
    vec3  base     = mix(ground, rockCol, rockMix);
    base           = mix(base,   snowCol, snowMix);

    vec3 col = base * (0.30 + 0.70 * band) * uSunCol * (0.55 + 0.45 * uIntensity);

    // Distance fog — blend toward sky/horizon colour as objects recede.
    float dist = length(vWorldPos - uCamPos);
    float fog  = clamp((dist - uFogStart) / (uFogEnd - uFogStart), 0.0, 1.0);
    col = mix(col, uFogColor, fog);
    frag = vec4(col, 1.0);
}
)";

static const char* kWaterVS = R"(#version 330 core
layout(location=0) in vec2 aPos;
out vec2 vUv;
out vec3 vWorldPos;
uniform mat4 uVP;
uniform float uHeight;
void main() {
    vUv = aPos;
    vec3 p = vec3(aPos.x, uHeight, aPos.y);
    vWorldPos = p;
    gl_Position = uVP * vec4(p, 1.0);
}
)";

static const char* kWaterFS = R"(#version 330 core
in vec2 vUv;
in vec3 vWorldPos;
out vec4 frag;
uniform float uTime;
uniform vec3 uSunCol;
uniform vec3 uSunDir;        // toward sun (TS convention)
uniform float uIntensity;
uniform vec3 uCamPos;
uniform vec3 uFogColor;
uniform float uFogStart;
uniform float uFogEnd;
void main() {
    // Two-layer sine waves for surface animation (TS-faithful).
    float w1 = sin(vUv.x * 6.0 + uTime * 1.4) * sin(vUv.y * 5.0 + uTime * 1.1);
    float w2 = sin(vUv.x * 3.7 - uTime * 0.9) * sin(vUv.y * 4.3 + uTime * 1.3);
    float wave = (w1 + w2) * 0.5;
    vec3 deepCol    = vec3(0.03, 0.14, 0.30);
    vec3 shallowCol = vec3(0.08, 0.30, 0.42);
    vec3 col = mix(deepCol, shallowCol, wave * 0.5 + 0.5);

    // Sun specular glint — TS uses a Blinn-Phong half-vector with a
    // perturbed normal driven by the same wave field.
    vec3 viewDir = normalize(uCamPos - vWorldPos);
    vec3 wN = normalize(vec3(
        sin(vUv.x * 6.0 + uTime * 1.4) * 0.08,
        1.0,
        cos(vUv.y * 4.3 + uTime * 1.3) * 0.08
    ));
    vec3 halfV = normalize(uSunDir + viewDir);
    float spec = pow(max(dot(wN, halfV), 0.0), 48.0);
    spec = floor(spec * 3.0 + 0.5) / 3.0;
    col += uSunCol * spec * uIntensity * 0.5;

    // Day/night ambient tint.
    col *= mix(vec3(0.30, 0.30, 0.50), vec3(1.0), uIntensity);

    // Fresnel-style alpha — grazing angles are more opaque.
    vec3 toEye    = normalize(uCamPos - vWorldPos);
    float fresnel = 1.0 - abs(toEye.y);
    float alpha   = mix(0.45, 0.82, fresnel * fresnel);

    // Distance fog to match terrain.
    float dist = length(vWorldPos - uCamPos);
    float fog  = clamp((dist - uFogStart) / (uFogEnd - uFogStart), 0.0, 1.0);
    col = mix(col, uFogColor, fog);
    frag = vec4(col, alpha);
}
)";

// ── Billboard pass (trees) ───────────────────────────────────────────
//
// Camera-facing billboards anchored at the tree's base. Per-instance
// attributes encode (worldPos.xyz, scale, height, typeIdx, variantIdx);
// the fragment shader is just a textured discard sampling the baked
// TreeAtlas (kTypes × kVariants pixel-art tiles produced by tree_atlas.cpp
// from the macro tree shader). Mountain rocks no longer exist — rocky
// relief is conveyed by the slope-driven rock/snow overlay on the terrain.
static const char* kBillVS = R"(#version 330 core
layout(location=0) in vec2 aLocal;       // [-0.5..+0.5]
layout(location=1) in vec3 aBaseW;       // world-space base anchor
layout(location=2) in float aScale;      // xz radius (m)
layout(location=3) in float aHeight;     // vertical extent (m)
layout(location=4) in float aTypeIdx;    // 0..kTypes-1
layout(location=5) in float aVariantIdx; // 0..kVariants-1
out vec2 vTileUV;
out vec3 vWorldPos;
uniform mat4 uVP;
uniform vec3 uCamPos;
uniform float uTypes;
uniform float uVariants;
void main() {
    // Cylindrical billboard — vertical axis is always world-up, the quad
    // only rotates around it to face the camera horizontally. Using the
    // camera's actual up axis here would shear/stretch the tree the
    // moment the camera pitched (tree foot stays put while the top swung
    // toward / away from the camera). Horizontal right is derived from
    // the camera-to-tree vector so every tree faces the viewer regardless
    // of yaw, without needing per-instance state.
    vec3 worldUp = vec3(0.0, 1.0, 0.0);
    vec3 toCamH  = vec3(uCamPos.x - aBaseW.x, 0.0, uCamPos.z - aBaseW.z);
    if (dot(toCamH, toCamH) < 1e-4) toCamH = vec3(1.0, 0.0, 0.0);
    vec3 right   = normalize(cross(worldUp, normalize(toCamH))) * aScale * 2.8;
    vec3 up      = worldUp * aHeight;
    vec3 wp = aBaseW + right * aLocal.x + up * (aLocal.y + 0.5);
    vec2 local = aLocal + 0.5;
    vTileUV.x = (aVariantIdx + local.x) / uVariants;
    vTileUV.y = (aTypeIdx    + local.y) / uTypes;
    vWorldPos = wp;
    gl_Position = uVP * vec4(wp, 1.0);
}
)";

static const char* kBillFS = R"(#version 330 core
in vec2 vTileUV;
in vec3 vWorldPos;
out vec4 frag;
uniform sampler2D uTreeAtlas;
uniform vec3  uSunCol;
uniform float uIntensity;
uniform vec3  uCamPos;
uniform vec3  uFogColor;
uniform float uFogStart;
uniform float uFogEnd;
void main() {
    vec4 t = texture(uTreeAtlas, vTileUV);
    if (t.a < 0.5) discard;
    vec3 col = t.rgb * uSunCol * (0.55 + 0.45 * uIntensity);
    float dist = length(vWorldPos - uCamPos);
    float fog  = clamp((dist - uFogStart) / (uFogEnd - uFogStart), 0.0, 1.0);
    col = mix(col, uFogColor, fog);
    frag = vec4(col, 1.0);
}
)";

void Renderer3D::init() {
    prog       = gl_link(kVS, kFS);
    waterProg  = gl_link(kWaterVS, kWaterFS);
    atlas.init();
    // Atlas needs REPEAT + LINEAR for mipless seamless tiling across cells.
    glBindTexture(GL_TEXTURE_2D, atlas.tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glBindTexture(GL_TEXTURE_2D, 0);

    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vboPos);
    glGenBuffers(1, &ibo);

    glGenVertexArrays(1, &waterVao);
    glGenBuffers(1, &waterVbo);
    glBindVertexArray(waterVao);
    float quad[8] = {
        -kWorldExtent, -kWorldExtent,
         kWorldExtent, -kWorldExtent,
        -kWorldExtent,  kWorldExtent,
         kWorldExtent,  kWorldExtent
    };
    glBindBuffer(GL_ARRAY_BUFFER, waterVbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    glBindVertexArray(0);

    // Billboard pass setup — shared unit quad + per-instance attrib buffer.
    billProg = gl_link(kBillVS, kBillFS);
    treeAtlas.bake(0u);
    glGenVertexArrays(1, &billVao);
    glGenBuffers(1, &billQuadVbo);
    glGenBuffers(1, &billInstVbo);
    glBindVertexArray(billVao);
    float bquad[8] = {
        -0.5f, 0.0f,
         0.5f, 0.0f,
        -0.5f, 1.0f,
         0.5f, 1.0f,
    };
    // Re-centre to [-0.5..+0.5] in y by mapping aLocal.y range; vertex
    // shader adds 0.5 so y=0..1 above is fine.
    bquad[1] = -0.5f; bquad[3] = -0.5f; bquad[5] = 0.5f; bquad[7] = 0.5f;
    glBindBuffer(GL_ARRAY_BUFFER, billQuadVbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(bquad), bquad, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    // Instance buffer: 7 floats / instance.
    glBindBuffer(GL_ARRAY_BUFFER, billInstVbo);
    const GLsizei stride = 7 * sizeof(float);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride,
                          reinterpret_cast<void*>(0));
    glVertexAttribDivisor(1, 1);
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, stride,
                          reinterpret_cast<void*>(3 * sizeof(float)));
    glVertexAttribDivisor(2, 1);
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, stride,
                          reinterpret_cast<void*>(4 * sizeof(float)));
    glVertexAttribDivisor(3, 1);
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, stride,
                          reinterpret_cast<void*>(5 * sizeof(float)));
    glVertexAttribDivisor(4, 1);
    glEnableVertexAttribArray(5);
    glVertexAttribPointer(5, 1, GL_FLOAT, GL_FALSE, stride,
                          reinterpret_cast<void*>(6 * sizeof(float)));
    glVertexAttribDivisor(5, 1);
    glBindVertexArray(0);
}

void Renderer3D::destroy() {
    if (prog)      glDeleteProgram(prog);
    if (waterProg) glDeleteProgram(waterProg);
    atlas.destroy();
    treeAtlas.destroy();
    if (roadMask)  glDeleteTextures(1, &roadMask);
    roadMask = 0;
    if (vao)       glDeleteVertexArrays(1, &vao);
    if (vboPos)    glDeleteBuffers(1, &vboPos);
    if (ibo)       glDeleteBuffers(1, &ibo);
    if (waterVao)  glDeleteVertexArrays(1, &waterVao);
    if (waterVbo)  glDeleteBuffers(1, &waterVbo);
    if (billProg)    glDeleteProgram(billProg);
    if (billVao)     glDeleteVertexArrays(1, &billVao);
    if (billQuadVbo) glDeleteBuffers(1, &billQuadVbo);
    if (billInstVbo) glDeleteBuffers(1, &billInstVbo);
    prog = waterProg = vao = vboPos = ibo = waterVao = waterVbo = 0;
    billProg = billVao = billQuadVbo = billInstVbo = 0;
    billCount = 0;
}

void Renderer3D::upload(const SeamlessSubworldManager& mgr) {
    const int N    = kMeshDim;
    const int Nv   = N + 1;
    const int step = kFullSize / N;
    const auto& hm = mgr.heightmap();
    if (hm.empty()) return;

    // Sample heights into a small grid.
    std::vector<float> h(std::size_t(Nv) * Nv, 0.0f);
    for (int y = 0; y < Nv; ++y) {
        int sy = std::min(kFullSize - 1, y * step);
        for (int x = 0; x < Nv; ++x) {
            int sx = std::min(kFullSize - 1, x * step);
            h[std::size_t(y) * Nv + x] = hm[std::size_t(sy) * kFullSize + sx];
        }
    }

    // Build interleaved Position+Normal vertex buffer.
    std::vector<float> verts(std::size_t(Nv) * Nv * 6, 0.0f);
    heightVtxM.assign(std::size_t(Nv) * Nv, 0.0f);
    float cell = 2.0f * kWorldExtent / float(N);
    for (int y = 0; y < Nv; ++y) {
        for (int x = 0; x < Nv; ++x) {
            std::size_t i = std::size_t(y) * Nv + x;
            float wx = -kWorldExtent + float(x) * cell;
            float wz = -kWorldExtent + float(y) * cell;
            float wy = h[i] * kHeightScale;
            heightVtxM[i] = wy;
            // Central-difference normal.
            int xm = std::max(0, x - 1), xp = std::min(Nv - 1, x + 1);
            int ym = std::max(0, y - 1), yp = std::min(Nv - 1, y + 1);
            float hL = h[std::size_t(y) * Nv + xm] * kHeightScale;
            float hR = h[std::size_t(y) * Nv + xp] * kHeightScale;
            float hD = h[std::size_t(ym) * Nv + x] * kHeightScale;
            float hU = h[std::size_t(yp) * Nv + x] * kHeightScale;
            vec3 n = normalize({hL - hR, 2.0f * cell, hD - hU});
            verts[i * 6 + 0] = wx; verts[i * 6 + 1] = wy; verts[i * 6 + 2] = wz;
            verts[i * 6 + 3] = n.x; verts[i * 6 + 4] = n.y; verts[i * 6 + 5] = n.z;
        }
    }

    // Indices (two tris per quad).
    std::vector<std::uint32_t> idx;
    idx.reserve(std::size_t(N) * N * 6);
    for (int y = 0; y < N; ++y) {
        for (int x = 0; x < N; ++x) {
            std::uint32_t a = std::uint32_t(y * Nv + x);
            std::uint32_t b = a + 1;
            std::uint32_t c = a + Nv;
            std::uint32_t d = c + 1;
            idx.push_back(a); idx.push_back(c); idx.push_back(b);
            idx.push_back(b); idx.push_back(c); idx.push_back(d);
        }
    }
    indexCount = GLsizei(idx.size());

    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vboPos);
    glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(verts.size() * sizeof(float)),
                 verts.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
                          reinterpret_cast<void*>(3 * sizeof(float)));
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, GLsizeiptr(idx.size() * sizeof(std::uint32_t)),
                 idx.data(), GL_STATIC_DRAW);
    glBindVertexArray(0);

    // Road mask — composite tile grid downsampled into an R8 texture.
    // Roads are painted by the subworld road generator into TILE_ROAD;
    // 1 byte per tile keeps the 3072² mask small (9 MiB) and lets the
    // fragment shader blend a single texture lookup over the biome.
    {
        const auto& tiles = mgr.tiles();
        std::vector<std::uint8_t> mask(std::size_t(kFullSize) * kFullSize, 0);
        if (!tiles.empty()) {
            for (std::size_t i = 0, n = mask.size(); i < n; ++i) {
                if (tiles[i] == TILE_ROAD) mask[i] = 255;
            }
        }
        if (!roadMask) glGenTextures(1, &roadMask);
        glBindTexture(GL_TEXTURE_2D, roadMask);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R8,
                     kFullSize, kFullSize, 0,
                     GL_RED, GL_UNSIGNED_BYTE, mask.data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    // Billboard instances — every Tree structure in the composite.
    // Rocks no longer exist (mountain relief comes from the heightmap +
    // slope-driven shader overlay). Each tree carries (typeIdx, variantIdx)
    // indexing the baked TreeAtlas; the species is chosen from the macro
    // biome of the cell containing the tree, mirroring TS behaviour.
    {
        const auto& structs = mgr.structures();
        std::vector<float> inst;
        inst.reserve(structs.size() * 7);
        for (const auto& s : structs) {
            if (s.kind != Structure::Tree) continue;
            float wx, wz;
            tile_to_world(s.x, s.y, wx, wz);
            const float baseM = sample_height_m(s.x, s.y);
            // Below the global water plane → submerged, skip.
            if (baseM < WATER_LEVEL * kHeightScale - 0.5f) continue;
            // Stable per-instance hash from tile coords.
            std::uint32_t h = std::uint32_t(s.x * 374761.0f) * 2246822519u
                            ^ std::uint32_t(s.y * 668265.0f) * 3266489917u;
            h ^= h >> 13; h *= 1274126177u; h ^= h >> 16;
            const float hash01 = float(h & 0xffffffu) / float(0xffffff);
            // Pick species from biome of the macro cell containing this tile.
            const int cellCol = std::min(2, std::max(0, int(s.x) / kCellSize));
            const int cellRow = std::min(2, std::max(0, int(s.y) / kCellSize));
            const Biome b = mgr.cell_biome(cellRow * 3 + cellCol);
            const int typeIdx    = tree_type_for(b, hash01);
            const int variantIdx = int(h >> 24) % TreeAtlas::kVariants;
            inst.push_back(wx);
            // Sink the trunk anchor 1 m below sampled terrain — billboards
            // are camera-facing so on a slope the visible quad bottom can
            // appear above the ground when the anchor sits exactly on the
            // mesh; a small downward bias eliminates the visible "floating"
            // gap at all camera angles without affecting tall trees.
            inst.push_back(baseM - 1.0f);
            inst.push_back(wz);
            inst.push_back(s.radius);
            inst.push_back(s.height);
            inst.push_back(float(typeIdx));
            inst.push_back(float(variantIdx));
        }
        billCount = GLsizei(inst.size() / 7);
        glBindBuffer(GL_ARRAY_BUFFER, billInstVbo);
        glBufferData(GL_ARRAY_BUFFER,
                     GLsizeiptr(inst.size() * sizeof(float)),
                     inst.empty() ? nullptr : inst.data(),
                     GL_STATIC_DRAW);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }
}

void Renderer3D::render(int viewW, int viewH, const Camera& cam,
                        const WorldTime& time, float waterLevel01,
                        const SeamlessSubworldManager* mgr) {
    if (!indexCount) return;
    glViewport(0, 0, viewW, viewH);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glClear(GL_DEPTH_BUFFER_BIT);

    SunInfo sun = compute_sun(time);

    float aspect = float(viewW) / float(std::max(1, viewH));
    mat4 P = mat4_perspective(cam.fovDeg * 0.0174533f, aspect, 0.5f, 1500.0f);
    vec3 fwd = cam.forward();
    mat4 V = mat4_lookAt(cam.pos, cam.pos + fwd, {0, 1, 0});
    mat4 VP = mat4_mul(P, V);

    glUseProgram(prog);
    glUniformMatrix4fv(glGetUniformLocation(prog, "uVP"), 1, GL_FALSE, VP.m);
    // Shader expects uSunDir pointing FROM sun TOWARD world; lighting
    // module follows TS convention (toward sun) — negate on upload.
    glUniform3f(glGetUniformLocation(prog, "uSunDir"),
                -sun.sunDir.x, -sun.sunDir.y, -sun.sunDir.z);
    glUniform3f(glGetUniformLocation(prog, "uSunCol"),
                sun.sunColor.x, sun.sunColor.y, sun.sunColor.z);
    glUniform1f(glGetUniformLocation(prog, "uIntensity"), sun.sunIntensity);
    glUniform1f(glGetUniformLocation(prog, "uExtent"), kWorldExtent);
    // Tile-repeat: ~3 atlas tiles per cell × 3 cells = 9 across the world.
    // Keeps each procedural biome texture readable without obvious tiling.
    glUniform1f(glGetUniformLocation(prog, "uTileScale"), 9.0f);
    glUniform1f(glGetUniformLocation(prog, "uAtlasInvCount"),
                1.0f / float(TileAtlas::kTileCount));

    // ── Per-cell atlas index for the 3x3 grid ──
    int biomeIdx[9];
    for (int i = 0; i < 9; ++i) {
        Biome b = mgr ? mgr->cell_biome(i) : Biome::Meadow;
        biomeIdx[i] = atlas.index_for(b);
    }
    glUniform1iv(glGetUniformLocation(prog, "uBiomeIdx[0]"), 9, biomeIdx);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, atlas.tex);
    glUniform1i(glGetUniformLocation(prog, "uAtlas"), 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, roadMask);
    glUniform1i(glGetUniformLocation(prog, "uRoadMask"), 1);
    glActiveTexture(GL_TEXTURE0);

    // Camera + fog. Fog colour follows ambient so distance haze blends
    // smoothly with the sky tint at any time of day.
    const vec3 fogCol = sun.ambientColor;
    glUniform3f(glGetUniformLocation(prog, "uCamPos"), cam.pos.x, cam.pos.y, cam.pos.z);
    glUniform3f(glGetUniformLocation(prog, "uFogColor"), fogCol.x, fogCol.y, fogCol.z);
    glUniform1f(glGetUniformLocation(prog, "uFogStart"), kFogStart);
    glUniform1f(glGetUniformLocation(prog, "uFogEnd"),   kFogEnd);

    glBindVertexArray(vao);
    glDrawElements(GL_TRIANGLES, indexCount, GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);

    // Water plane, alpha-blended.
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(waterProg);
    glUniformMatrix4fv(glGetUniformLocation(waterProg, "uVP"), 1, GL_FALSE, VP.m);
    glUniform1f(glGetUniformLocation(waterProg, "uHeight"), waterLevel01 * kHeightScale);
    glUniform1f(glGetUniformLocation(waterProg, "uTime"),
                float(time.hour) + float(time.minute) / 60.0f);
    glUniform3f(glGetUniformLocation(waterProg, "uSunCol"),
                sun.sunColor.x, sun.sunColor.y, sun.sunColor.z);
    glUniform3f(glGetUniformLocation(waterProg, "uSunDir"),
                sun.sunDir.x, sun.sunDir.y, sun.sunDir.z);
    glUniform1f(glGetUniformLocation(waterProg, "uIntensity"), sun.sunIntensity);
    glUniform3f(glGetUniformLocation(waterProg, "uCamPos"),
                cam.pos.x, cam.pos.y, cam.pos.z);
    glUniform3f(glGetUniformLocation(waterProg, "uFogColor"),
                fogCol.x, fogCol.y, fogCol.z);
    glUniform1f(glGetUniformLocation(waterProg, "uFogStart"), kFogStart);
    glUniform1f(glGetUniformLocation(waterProg, "uFogEnd"),   kFogEnd);
    glBindVertexArray(waterVao);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindVertexArray(0);
    glDisable(GL_BLEND);

    // Billboards (trees + rocks). Alpha-tested via discard, depth-tested
    // against the terrain mesh, drawn after water so submerged trees would
    // be hidden by it (already filtered at upload, defence-in-depth).
    if (billCount > 0 && billProg) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glUseProgram(billProg);
        glUniformMatrix4fv(glGetUniformLocation(billProg, "uVP"), 1, GL_FALSE, VP.m);
        glUniform1f(glGetUniformLocation(billProg, "uTypes"),    float(TreeAtlas::kTypes));
        glUniform1f(glGetUniformLocation(billProg, "uVariants"), float(TreeAtlas::kVariants));
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, treeAtlas.tex);
        glUniform1i(glGetUniformLocation(billProg, "uTreeAtlas"), 2);
        glActiveTexture(GL_TEXTURE0);
        // Cylindrical billboards face the camera horizontally — the
        // shader derives the per-tree right axis from `uCamPos` and
        // world-up, so no per-frame right/up uniform is needed.
        glUniform3f(glGetUniformLocation(billProg, "uSunCol"),
                    sun.sunColor.x, sun.sunColor.y, sun.sunColor.z);
        glUniform1f(glGetUniformLocation(billProg, "uIntensity"), sun.sunIntensity);
        glUniform3f(glGetUniformLocation(billProg, "uCamPos"),
                    cam.pos.x, cam.pos.y, cam.pos.z);
        glUniform3f(glGetUniformLocation(billProg, "uFogColor"),
                    fogCol.x, fogCol.y, fogCol.z);
        glUniform1f(glGetUniformLocation(billProg, "uFogStart"), kFogStart);
        glUniform1f(glGetUniformLocation(billProg, "uFogEnd"),   kFogEnd);
        glBindVertexArray(billVao);
        glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, billCount);
        glBindVertexArray(0);
        glDisable(GL_BLEND);
    }
    glDisable(GL_DEPTH_TEST);
}

void Renderer3D::tile_to_world(float tileX, float tileY, float& wx, float& wz) {
    wx = (tileX - float(kFullSize) * 0.5f) * kTileMeters;
    wz = (tileY - float(kFullSize) * 0.5f) * kTileMeters;
}

float Renderer3D::sample_height_m(float tileX, float tileY) const {
    if (heightVtxM.empty()) return 0.0f;
    const int Nv = kMeshDim + 1;
    // Map composite tile -> vertex grid (continuous bilinear).
    float fx = tileX * float(kMeshDim) / float(kFullSize);
    float fy = tileY * float(kMeshDim) / float(kFullSize);
    if (fx < 0) fx = 0; if (fy < 0) fy = 0;
    if (fx > float(Nv - 1)) fx = float(Nv - 1);
    if (fy > float(Nv - 1)) fy = float(Nv - 1);
    int xi = int(fx), yi = int(fy);
    int xn = xi + 1; if (xn > Nv - 1) xn = Nv - 1;
    int yn = yi + 1; if (yn > Nv - 1) yn = Nv - 1;
    float tx = fx - float(xi), ty = fy - float(yi);
    float h00 = heightVtxM[std::size_t(yi) * Nv + xi];
    float h10 = heightVtxM[std::size_t(yi) * Nv + xn];
    float h01 = heightVtxM[std::size_t(yn) * Nv + xi];
    float h11 = heightVtxM[std::size_t(yn) * Nv + xn];
    float a = h00 * (1.0f - tx) + h10 * tx;
    float b = h01 * (1.0f - tx) + h11 * tx;
    return a * (1.0f - ty) + b * ty;
}

} // namespace sm::sub
